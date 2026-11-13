//  SPDX-License-Identifier: MIT
//
//  games247
//  LibretroCore.cpp
//
//  Loads a libretro core (dynamic library) and runs content in-process.
//

#include "libretro/LibretroCore.h"

#include "Log.h"
#include "utils/FileSystemUtil.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <type_traits>

#if defined(_WIN64)
#include "utils/StringUtil.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace
{
    // Small wrappers so the loader works with .dll (Windows) and .dylib/.so (macOS, Linux).
    void* openLibrary(const std::string& path, std::string& error)
    {
#if defined(_WIN64)
        HMODULE module {LoadLibraryW(Utils::String::stringToWideString(path).c_str())};
        if (module == nullptr)
            error = "Windows error " + std::to_string(GetLastError());
        return reinterpret_cast<void*>(module);
#else
        void* handle {dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL)};
        if (handle == nullptr) {
            const char* dlError {dlerror()};
            error = dlError ? dlError : "unknown error";
        }
        return handle;
#endif
    }

    void* librarySymbol(void* handle, const char* name)
    {
#if defined(_WIN64)
        return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name));
#else
        return dlsym(handle, name);
#endif
    }

    void closeLibrary(void* handle)
    {
#if defined(_WIN64)
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
    }
} // namespace

LibretroCore* LibretroCore::sActive {nullptr};

LibretroCore::LibretroCore()
    : mHandle {nullptr}
    , mInitialized {false}
    , mGameLoaded {false}
    , mRetroInit {nullptr}
    , mRetroDeinit {nullptr}
    , mRetroApiVersion {nullptr}
    , mRetroGetSystemInfo {nullptr}
    , mRetroGetSystemAVInfo {nullptr}
    , mRetroSetEnvironment {nullptr}
    , mRetroSetVideoRefresh {nullptr}
    , mRetroSetAudioSample {nullptr}
    , mRetroSetAudioSampleBatch {nullptr}
    , mRetroSetInputPoll {nullptr}
    , mRetroSetInputState {nullptr}
    , mRetroSetControllerPortDevice {nullptr}
    , mRetroRun {nullptr}
    , mRetroReset {nullptr}
    , mRetroSerializeSize {nullptr}
    , mRetroSerialize {nullptr}
    , mRetroUnserialize {nullptr}
    , mRetroLoadGame {nullptr}
    , mRetroUnloadGame {nullptr}
    , mRetroGetMemoryData {nullptr}
    , mRetroGetMemorySize {nullptr}
    , mSystemInfo {}
    , mAVInfo {}
    , mPixelFormat {RETRO_PIXEL_FORMAT_0RGB1555}
    , mKeyboardCallback {nullptr}
    , mHardwareRenderRequested {false}
    , mFrameWidth {0}
    , mFrameHeight {0}
    , mNewFrame {false}
{
}

LibretroCore::~LibretroCore() { unload(); }

bool LibretroCore::load(const std::string& corePath,
                        const std::string& contentPath,
                        const std::string& systemDirectory,
                        const std::string& saveDirectory,
                        std::string& error)
{
    if (sActive != nullptr) {
        error = "another core is already running";
        return false;
    }

    mContentPath = contentPath;
    mSystemDirectory = systemDirectory;
    mSaveDirectory = saveDirectory;

    std::string openError;
    mHandle = openLibrary(corePath, openError);
    if (mHandle == nullptr) {
        error = "couldn't open core: " + openError;
        return false;
    }

    bool symbolsOK {true};
    auto resolve = [&](auto& function, const char* name) {
        function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(
            librarySymbol(mHandle, name));
        if (function == nullptr) {
            LOG(LogError) << "LibretroCore: core is missing symbol " << name;
            symbolsOK = false;
        }
    };

    resolve(mRetroInit, "retro_init");
    resolve(mRetroDeinit, "retro_deinit");
    resolve(mRetroApiVersion, "retro_api_version");
    resolve(mRetroGetSystemInfo, "retro_get_system_info");
    resolve(mRetroGetSystemAVInfo, "retro_get_system_av_info");
    resolve(mRetroSetEnvironment, "retro_set_environment");
    resolve(mRetroSetVideoRefresh, "retro_set_video_refresh");
    resolve(mRetroSetAudioSample, "retro_set_audio_sample");
    resolve(mRetroSetAudioSampleBatch, "retro_set_audio_sample_batch");
    resolve(mRetroSetInputPoll, "retro_set_input_poll");
    resolve(mRetroSetInputState, "retro_set_input_state");
    resolve(mRetroSetControllerPortDevice, "retro_set_controller_port_device");
    resolve(mRetroRun, "retro_run");
    resolve(mRetroReset, "retro_reset");
    resolve(mRetroSerializeSize, "retro_serialize_size");
    resolve(mRetroSerialize, "retro_serialize");
    resolve(mRetroUnserialize, "retro_unserialize");
    resolve(mRetroLoadGame, "retro_load_game");
    resolve(mRetroUnloadGame, "retro_unload_game");
    resolve(mRetroGetMemoryData, "retro_get_memory_data");
    resolve(mRetroGetMemorySize, "retro_get_memory_size");

    if (!symbolsOK) {
        error = "file is not a valid libretro core";
        unload();
        return false;
    }

    if (mRetroApiVersion() != RETRO_API_VERSION) {
        error = "core uses an unsupported libretro API version";
        unload();
        return false;
    }

    sActive = this;

    mRetroGetSystemInfo(&mSystemInfo);
    mCoreName = std::string {mSystemInfo.library_name ? mSystemInfo.library_name : "unknown"} +
                " " + (mSystemInfo.library_version ? mSystemInfo.library_version : "");
    LOG(LogInfo) << "LibretroCore: loading core " << mCoreName << " (" << corePath << ")";

    mRetroSetEnvironment(&LibretroCore::environmentCallback);
    mRetroInit();
    mInitialized = true;

    mRetroSetVideoRefresh(&LibretroCore::videoRefreshCallback);
    mRetroSetAudioSample(&LibretroCore::audioSampleCallback);
    mRetroSetAudioSampleBatch(&LibretroCore::audioSampleBatchCallback);
    mRetroSetInputPoll(&LibretroCore::inputPollCallback);
    mRetroSetInputState(&LibretroCore::inputStateCallback);

    retro_game_info gameInfo {};
    gameInfo.path = mContentPath.c_str();

    if (!mSystemInfo.need_fullpath) {
        std::ifstream file {mContentPath, std::ios::binary};
        if (!file) {
            error = "couldn't read the game file";
            unload();
            return false;
        }
        mContentData.assign(std::istreambuf_iterator<char>(file),
                            std::istreambuf_iterator<char>());
        gameInfo.data = mContentData.data();
        gameInfo.size = mContentData.size();
    }

    if (!mRetroLoadGame(&gameInfo)) {
        error = mHardwareRenderRequested ?
                    "core needs hardware (GPU) rendering, which the built-in player doesn't "
                    "support yet" :
                    "core couldn't load the game";
        unload();
        return false;
    }
    mGameLoaded = true;

    if (mHardwareRenderRequested) {
        error = "core needs hardware (GPU) rendering, which the built-in player doesn't "
                "support yet";
        unload();
        return false;
    }

    mRetroGetSystemAVInfo(&mAVInfo);
    for (unsigned int port {0}; port < MAX_PORTS; ++port)
        mRetroSetControllerPortDevice(port, RETRO_DEVICE_JOYPAD);

    loadSaveRAM();

    LOG(LogInfo) << "LibretroCore: running at " << mAVInfo.timing.fps << " fps, audio "
                 << mAVInfo.timing.sample_rate << " Hz, base video "
                 << mAVInfo.geometry.base_width << "x" << mAVInfo.geometry.base_height;
    return true;
}

void LibretroCore::unload()
{
    if (mGameLoaded) {
        writeSaveRAM();
        mRetroUnloadGame();
        mGameLoaded = false;
    }
    if (mInitialized) {
        mRetroDeinit();
        mInitialized = false;
    }
    if (mHandle != nullptr) {
        closeLibrary(mHandle);
        mHandle = nullptr;
    }
    if (sActive == this)
        sActive = nullptr;

    mContentData.clear();
    mOptions.clear();
    mKeyboardCallback = nullptr;
}

void LibretroCore::runFrame()
{
    if (mGameLoaded)
        mRetroRun();
}

void LibretroCore::reset()
{
    if (mGameLoaded)
        mRetroReset();
}

bool LibretroCore::saveState(const std::string& path)
{
    if (!mGameLoaded)
        return false;
    const size_t size {mRetroSerializeSize()};
    if (size == 0)
        return false;
    std::vector<uint8_t> data(size);
    if (!mRetroSerialize(data.data(), size))
        return false;
    std::ofstream file {path, std::ios::binary | std::ios::trunc};
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(size));
    if (!file)
        return false;
    LOG(LogInfo) << "LibretroCore: saved state to " << path;
    return true;
}

bool LibretroCore::loadState(const std::string& path)
{
    if (!mGameLoaded)
        return false;
    std::ifstream file {path, std::ios::binary};
    if (!file)
        return false;
    std::vector<uint8_t> data {std::istreambuf_iterator<char>(file),
                               std::istreambuf_iterator<char>()};
    if (data.empty() || !mRetroUnserialize(data.data(), data.size()))
        return false;
    LOG(LogInfo) << "LibretroCore: loaded state from " << path;
    return true;
}

float LibretroCore::getAspectRatio() const
{
    if (mAVInfo.geometry.aspect_ratio > 0.0f)
        return mAVInfo.geometry.aspect_ratio;
    if (mFrameHeight > 0)
        return static_cast<float>(mFrameWidth) / static_cast<float>(mFrameHeight);
    if (mAVInfo.geometry.base_height > 0)
        return static_cast<float>(mAVInfo.geometry.base_width) /
               static_cast<float>(mAVInfo.geometry.base_height);
    return 4.0f / 3.0f;
}

void LibretroCore::keyboardEvent(bool down,
                                 unsigned int retrok,
                                 uint32_t character,
                                 uint16_t modifiers)
{
    if (mKeyboardCallback != nullptr && mGameLoaded)
        mKeyboardCallback(down, retrok, character, modifiers);
}

void LibretroCore::loadSaveRAM()
{
    const size_t size {mRetroGetMemorySize(RETRO_MEMORY_SAVE_RAM)};
    void* data {mRetroGetMemoryData(RETRO_MEMORY_SAVE_RAM)};
    if (size == 0 || data == nullptr || mSaveDirectory.empty())
        return;

    mSaveRAMPath = mSaveDirectory + "/" + Utils::FileSystem::getStem(mContentPath) + ".srm";
    std::ifstream file {mSaveRAMPath, std::ios::binary};
    if (file) {
        file.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
        LOG(LogInfo) << "LibretroCore: loaded save data from " << mSaveRAMPath;
    }
}

void LibretroCore::writeSaveRAM()
{
    if (mSaveRAMPath.empty() && !mSaveDirectory.empty())
        mSaveRAMPath = mSaveDirectory + "/" + Utils::FileSystem::getStem(mContentPath) + ".srm";

    const size_t size {mRetroGetMemorySize(RETRO_MEMORY_SAVE_RAM)};
    void* data {mRetroGetMemoryData(RETRO_MEMORY_SAVE_RAM)};
    if (size == 0 || data == nullptr || mSaveRAMPath.empty())
        return;

    Utils::FileSystem::createDirectory(mSaveDirectory);
    std::ofstream file {mSaveRAMPath, std::ios::binary | std::ios::trunc};
    if (file) {
        file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        LOG(LogInfo) << "LibretroCore: wrote save data to " << mSaveRAMPath;
    }
}

void LibretroCore::storeOptionDefault(const char* key, const char* value)
{
    if (key != nullptr && value != nullptr && mOptions.find(key) == mOptions.end())
        mOptions[key] = value;
}

// ---------------------------------------------------------------------------------------------
// Environment: the core's questions to the frontend.

bool LibretroCore::environment(unsigned int cmd, void* data)
{
    switch (cmd) {
        case RETRO_ENVIRONMENT_GET_CAN_DUPE: {
            *static_cast<bool*>(data) = true;
            return true;
        }
        case RETRO_ENVIRONMENT_GET_OVERSCAN: {
            *static_cast<bool*>(data) = false;
            return true;
        }
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
            const retro_pixel_format format {*static_cast<retro_pixel_format*>(data)};
            if (format == RETRO_PIXEL_FORMAT_0RGB1555 || format == RETRO_PIXEL_FORMAT_XRGB8888 ||
                format == RETRO_PIXEL_FORMAT_RGB565) {
                mPixelFormat = format;
                return true;
            }
            return false;
        }
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: {
            *static_cast<const char**>(data) = mSystemDirectory.c_str();
            return true;
        }
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: {
            *static_cast<const char**>(data) = mSaveDirectory.c_str();
            return true;
        }
        case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY: {
            *static_cast<const char**>(data) = mSystemDirectory.c_str();
            return true;
        }
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
            static_cast<retro_log_callback*>(data)->log = &LibretroCore::logCallback;
            return true;
        }
        case RETRO_ENVIRONMENT_GET_LANGUAGE: {
            *static_cast<unsigned int*>(data) = RETRO_LANGUAGE_ENGLISH;
            return true;
        }
        case RETRO_ENVIRONMENT_GET_USERNAME: {
            *static_cast<const char**>(data) = "games247";
            return true;
        }
        case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: {
            return true;
        }
        case RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES: {
            *static_cast<uint64_t*>(data) =
                (1 << RETRO_DEVICE_JOYPAD) | (1 << RETRO_DEVICE_KEYBOARD);
            return true;
        }
        case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE: {
            if (data != nullptr)
                *static_cast<int*>(data) = 3; // Video and audio enabled.
            return true;
        }
        case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK: {
            mKeyboardCallback = static_cast<retro_keyboard_callback*>(data)->callback;
            return true;
        }
        case RETRO_ENVIRONMENT_SET_GEOMETRY: {
            mAVInfo.geometry = *static_cast<retro_game_geometry*>(data);
            return true;
        }
        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
            mAVInfo = *static_cast<retro_system_av_info*>(data);
            return true;
        }
        case RETRO_ENVIRONMENT_SET_MESSAGE: {
            const retro_message* message {static_cast<retro_message*>(data)};
            if (message != nullptr && message->msg != nullptr) {
                LOG(LogInfo) << "LibretroCore: core message: " << message->msg;
            }
            return true;
        }
        case RETRO_ENVIRONMENT_SET_HW_RENDER:
        case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER: {
            // Not supported yet. Remember the request so load() can report why it failed.
            if (cmd == RETRO_ENVIRONMENT_SET_HW_RENDER)
                mHardwareRenderRequested = true;
            return false;
        }

        // Core options. We don't have an options UI yet, so every option uses the
        // core's default value.
        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: {
            *static_cast<unsigned int*>(data) = 2;
            return true;
        }
        case RETRO_ENVIRONMENT_SET_VARIABLES: {
            // Format: "Description; default|other|values". The first value is the default.
            for (const retro_variable* variable {static_cast<const retro_variable*>(data)};
                 variable != nullptr && variable->key != nullptr; ++variable) {
                const std::string value {variable->value ? variable->value : ""};
                const size_t start {value.find("; ")};
                if (start == std::string::npos)
                    continue;
                const std::string values {value.substr(start + 2)};
                storeOptionDefault(variable->key, values.substr(0, values.find('|')).c_str());
            }
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS: {
            for (const retro_core_option_definition* option {
                     static_cast<const retro_core_option_definition*>(data)};
                 option != nullptr && option->key != nullptr; ++option)
                storeOptionDefault(option->key, option->default_value);
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL: {
            const retro_core_options_intl* intl {static_cast<const retro_core_options_intl*>(data)};
            for (const retro_core_option_definition* option {intl->us};
                 option != nullptr && option->key != nullptr; ++option)
                storeOptionDefault(option->key, option->default_value);
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: {
            const retro_core_options_v2* options {static_cast<const retro_core_options_v2*>(data)};
            for (const retro_core_option_v2_definition* option {options->definitions};
                 option != nullptr && option->key != nullptr; ++option)
                storeOptionDefault(option->key, option->default_value);
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL: {
            const retro_core_options_v2_intl* intl {
                static_cast<const retro_core_options_v2_intl*>(data)};
            if (intl->us != nullptr) {
                for (const retro_core_option_v2_definition* option {intl->us->definitions};
                     option != nullptr && option->key != nullptr; ++option)
                    storeOptionDefault(option->key, option->default_value);
            }
            return true;
        }
        case RETRO_ENVIRONMENT_GET_VARIABLE: {
            retro_variable* variable {static_cast<retro_variable*>(data)};
            if (variable == nullptr || variable->key == nullptr)
                return false;
            const auto it = mOptions.find(variable->key);
            if (it == mOptions.end()) {
                variable->value = nullptr;
                return false;
            }
            variable->value = it->second.c_str();
            return true;
        }
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: {
            *static_cast<bool*>(data) = false;
            return true;
        }

        // Accepted but nothing for us to do.
        case RETRO_ENVIRONMENT_SET_ROTATION:
        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
        case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
        case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
        case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
        case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
        case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
            return true;

        default:
            LOG(LogDebug) << "LibretroCore: unhandled environment command " << cmd;
            return false;
    }
}

// ---------------------------------------------------------------------------------------------
// Static C callbacks.

bool LibretroCore::environmentCallback(unsigned int cmd, void* data)
{
    return sActive != nullptr && sActive->environment(cmd, data);
}

void LibretroCore::videoRefreshCallback(const void* data,
                                        unsigned int width,
                                        unsigned int height,
                                        size_t pitch)
{
    // data is null when the core repeats the previous frame.
    if (sActive == nullptr || data == nullptr || data == RETRO_HW_FRAME_BUFFER_VALID ||
        width == 0 || height == 0)
        return;

    LibretroCore& core {*sActive};
    core.mFrameWidth = width;
    core.mFrameHeight = height;
    core.mFrameBGRA.resize(static_cast<size_t>(width) * height * 4);
    uint8_t* out {core.mFrameBGRA.data()};

    for (unsigned int y {0}; y < height; ++y) {
        const uint8_t* row {static_cast<const uint8_t*>(data) + y * pitch};
        switch (core.mPixelFormat) {
            case RETRO_PIXEL_FORMAT_XRGB8888: {
                const uint32_t* pixels {reinterpret_cast<const uint32_t*>(row)};
                for (unsigned int x {0}; x < width; ++x) {
                    const uint32_t p {pixels[x]};
                    *out++ = static_cast<uint8_t>(p);
                    *out++ = static_cast<uint8_t>(p >> 8);
                    *out++ = static_cast<uint8_t>(p >> 16);
                    *out++ = 0xFF;
                }
                break;
            }
            case RETRO_PIXEL_FORMAT_RGB565: {
                const uint16_t* pixels {reinterpret_cast<const uint16_t*>(row)};
                for (unsigned int x {0}; x < width; ++x) {
                    const uint16_t p {pixels[x]};
                    const uint8_t r {static_cast<uint8_t>((p >> 11) & 0x1F)};
                    const uint8_t g {static_cast<uint8_t>((p >> 5) & 0x3F)};
                    const uint8_t b {static_cast<uint8_t>(p & 0x1F)};
                    *out++ = static_cast<uint8_t>((b << 3) | (b >> 2));
                    *out++ = static_cast<uint8_t>((g << 2) | (g >> 4));
                    *out++ = static_cast<uint8_t>((r << 3) | (r >> 2));
                    *out++ = 0xFF;
                }
                break;
            }
            default: { // RETRO_PIXEL_FORMAT_0RGB1555
                const uint16_t* pixels {reinterpret_cast<const uint16_t*>(row)};
                for (unsigned int x {0}; x < width; ++x) {
                    const uint16_t p {pixels[x]};
                    const uint8_t r {static_cast<uint8_t>((p >> 10) & 0x1F)};
                    const uint8_t g {static_cast<uint8_t>((p >> 5) & 0x1F)};
                    const uint8_t b {static_cast<uint8_t>(p & 0x1F)};
                    *out++ = static_cast<uint8_t>((b << 3) | (b >> 2));
                    *out++ = static_cast<uint8_t>((g << 3) | (g >> 2));
                    *out++ = static_cast<uint8_t>((r << 3) | (r >> 2));
                    *out++ = 0xFF;
                }
                break;
            }
        }
    }
    core.mNewFrame = true;
}

void LibretroCore::audioSampleCallback(int16_t left, int16_t right)
{
    if (sActive == nullptr)
        return;
    sActive->mAudio.push_back(left);
    sActive->mAudio.push_back(right);
}

size_t LibretroCore::audioSampleBatchCallback(const int16_t* data, size_t frames)
{
    if (sActive != nullptr && data != nullptr)
        sActive->mAudio.insert(sActive->mAudio.end(), data, data + frames * 2);
    return frames;
}

void LibretroCore::inputPollCallback()
{
    // The player updates the input state before each frame, so there's nothing to poll here.
}

int16_t LibretroCore::inputStateCallback(unsigned int port,
                                         unsigned int device,
                                         unsigned int /*index*/,
                                         unsigned int id)
{
    if (sActive == nullptr)
        return 0;

    switch (device & RETRO_DEVICE_MASK) {
        case RETRO_DEVICE_JOYPAD: {
            if (port >= MAX_PORTS)
                return 0;
            if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
                return static_cast<int16_t>(sActive->padState[port]);
            return (sActive->padState[port] >> id) & 1;
        }
        case RETRO_DEVICE_KEYBOARD: {
            if (port == 0 && sActive->keyboardQuery)
                return sActive->keyboardQuery(id) ? 1 : 0;
            return 0;
        }
        default:
            return 0;
    }
}

void LibretroCore::logCallback(enum retro_log_level level, const char* fmt, ...)
{
    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    std::string message {buffer};
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
        message.pop_back();

    if (level == RETRO_LOG_ERROR) {
        LOG(LogError) << "Core: " << message;
    }
    else if (level == RETRO_LOG_WARN) {
        LOG(LogWarning) << "Core: " << message;
    }
    else if (level == RETRO_LOG_INFO) {
        LOG(LogInfo) << "Core: " << message;
    }
    else {
        LOG(LogDebug) << "Core: " << message;
    }
}
