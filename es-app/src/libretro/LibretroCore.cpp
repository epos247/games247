//  SPDX-License-Identifier: MIT
//
//  games247
//  LibretroCore.cpp
//
//  Loads a libretro core (dynamic library) and runs content in-process.
//

#include "libretro/LibretroCore.h"

#include "Log.h"
#include "libretro/CoreManager.h"
#include "libretro/LibretroVulkan.h"
#include "renderers/ShaderOpenGL.h"
#include "resources/ResourceManager.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <type_traits>

#if defined(_WIN64)
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

    // GPU cores draw with their own OpenGL context, like in RetroArch, so they never see or
    // disturb games247's renderer state (they cache what they have bound). This makes the
    // core's context current for as long as it exists, then switches back.
    class CoreContextScope
    {
    public:
        explicit CoreContextScope(void* coreContext)
            : mWindow {SDL_GL_GetCurrentWindow()}
            , mPrevious {SDL_GL_GetCurrentContext()}
            , mSwitched {coreContext != nullptr && mWindow != nullptr && coreContext != mPrevious}
        {
            if (mSwitched)
                SDL_GL_MakeCurrent(mWindow, static_cast<SDL_GLContext>(coreContext));
        }
        ~CoreContextScope()
        {
            if (mSwitched)
                SDL_GL_MakeCurrent(mWindow, mPrevious);
        }
        CoreContextScope(const CoreContextScope&) = delete;
        CoreContextScope& operator=(const CoreContextScope&) = delete;

    private:
        SDL_Window* mWindow;
        SDL_GLContext mPrevious;
        bool mSwitched;
    };
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
    , mHardwareRender {false}
    , mHWRender {}
    , mHWFramebuffer {0}
    , mHWColorTexture {0}
    , mHWDepthBuffer {0}
    , mHWWidth {0}
    , mHWHeight {0}
    , mHWContext {nullptr}
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

    // Option values to use instead of the core's defaults: games247's own choices first, then
    // the user's, then any for this game only (such as the tape loading speed picked at the
    // start).
    mOptions = readOptionFiles(corePath);
    for (const auto& [key, value] : mOverrides) {
        LOG(LogInfo) << "LibretroCore: " << key << " = \"" << value << "\" for this game";
        mOptions[key] = value;
    }

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

    if (!prepareContent(error)) {
        unload();
        return false;
    }

    retro_game_info gameInfo {};
    gameInfo.path = mLoadPath.c_str();

    if (!mSystemInfo.need_fullpath) {
        std::ifstream file {mLoadPath, std::ios::binary};
        if (!file) {
            error = "couldn't read the game file";
            unload();
            return false;
        }
        mContentData.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        gameInfo.data = mContentData.data();
        gameInfo.size = mContentData.size();
    }

    if (!mRetroLoadGame(&gameInfo)) {
        error = mHardwareRenderRequested && !mHardwareRender ?
                    "core needs a kind of GPU rendering the built-in player doesn't support" :
                    "core couldn't load the game";
        unload();
        return false;
    }
    mGameLoaded = true;

    if (mHardwareRenderRequested && !mHardwareRender) {
        error = "core needs a kind of GPU rendering the built-in player doesn't support";
        unload();
        return false;
    }

    mRetroGetSystemAVInfo(&mAVInfo);

    if (mHardwareRender && mVulkanRender) {
        mVulkan = std::make_unique<LibretroVulkan>();
        mVulkan->setNegotiation(mVulkanNegotiation);
        if (!mVulkan->create(error)) {
            unload();
            return false;
        }
        // The core sets up its own Vulkan objects now; it fetches the device with
        // GET_HW_RENDER_INTERFACE.
        if (mHWRender.context_reset != nullptr)
            mHWRender.context_reset();
    }
    else if (mHardwareRender) {
        if (!createHWFramebuffer(error)) {
            unload();
            return false;
        }
        // The core sets up its shaders and textures now that its framebuffer exists.
        if (mHWRender.context_reset != nullptr) {
            CoreContextScope scope {mHWContext};
            mHWRender.context_reset();
        }
    }
    for (unsigned int port {0}; port < MAX_PORTS; ++port)
        mRetroSetControllerPortDevice(port, RETRO_DEVICE_JOYPAD);
    setupButtonMap();

    loadSaveRAM();

    LOG(LogInfo) << "LibretroCore: running at " << mAVInfo.timing.fps << " fps, audio "
                 << mAVInfo.timing.sample_rate << " Hz, base video " << mAVInfo.geometry.base_width
                 << "x" << mAVInfo.geometry.base_height;
    return true;
}

void LibretroCore::unload()
{
    {
        // A GPU core frees its OpenGL objects during these calls.
        CoreContextScope scope {mHWContext};
        if (mGameLoaded) {
            writeSaveRAM();
            if (mHardwareRender && mHWRender.context_destroy != nullptr)
                mHWRender.context_destroy();
            mRetroUnloadGame();
            mGameLoaded = false;
        }
        // The core's device goes before the core does.
        if (mVulkan != nullptr)
            mVulkan->destroy();
        if (mInitialized) {
            mRetroDeinit();
            mInitialized = false;
        }
        destroyHWFramebuffer();
    }
    mHardwareRender = false;
    mVulkanRender = false;
    mVulkan.reset();
    mVulkanNegotiation = nullptr;
    if (mHandle != nullptr) {
        closeLibrary(mHandle);
        mHandle = nullptr;
    }
    if (sActive == this)
        sActive = nullptr;

    mContentData.clear();
    mOptions.clear();
    for (auto& types : mControllerTypes)
        types.clear();
    if (!mExtractDirectory.empty()) {
        Utils::FileSystem::removeDirectory(mExtractDirectory, true);
        mExtractDirectory.clear();
    }
    mKeyboardCallback = nullptr;
}

bool LibretroCore::prepareContent(std::string& error)
{
    mLoadPath = mContentPath;

    const std::string extension {
        Utils::String::toLower(Utils::FileSystem::getExtension(mContentPath))};
    if (extension != ".zip" && extension != ".7z")
        return true;

    // Like RetroArch, unpack archives unless the core says it opens them itself (block_extract).
    // Listing zip among its file types isn't enough: Caprice32 lists it only because RetroArch
    // unpacks zips for it, and given the zip itself it boots to BASIC without the game.
    if (mSystemInfo.block_extract)
        return true;
    const std::vector<std::string> coreExtensions {Utils::String::delimitedStringToVector(
        Utils::String::toLower(mSystemInfo.valid_extensions ? mSystemInfo.valid_extensions : ""),
        "|")};
    auto coreOpens = [&](const std::string& ext) {
        return ext != "zip" && ext != "7z" &&
               std::find(coreExtensions.cbegin(), coreExtensions.cend(), ext) !=
                   coreExtensions.cend();
    };

    mExtractDirectory = Utils::FileSystem::getAppDataDirectory() + "/tmp/player_content";
    Utils::FileSystem::removeDirectory(mExtractDirectory, true);
    const int result {CoreManager::extractArchive(mContentPath, mExtractDirectory)};
    if (result != 0) {
        LOG(LogError) << "LibretroCore: couldn't unpack " << mContentPath << " (" << result << ")";
        error = "couldn't unpack the game archive";
        return false;
    }

    // Pick the file the core should load. Playlists and cue sheets come first, since they
    // point at the other files.
    std::vector<std::string> files;
    for (const std::string& path : Utils::FileSystem::getDirContent(mExtractDirectory, true)) {
        if (Utils::FileSystem::isRegularFile(path) &&
            coreOpens(Utils::String::toLower(Utils::FileSystem::getExtension(path)).substr(1)))
            files.emplace_back(path);
    }
    if (files.empty()) {
        LOG(LogError) << "LibretroCore: " << mContentPath
                      << " doesn't contain a file this core can open";
        error = "the archive doesn't contain a game this core can open";
        return false;
    }
    std::sort(files.begin(), files.end());
    auto rank = [](const std::string& path) {
        const std::string ext {Utils::String::toLower(Utils::FileSystem::getExtension(path))};
        return ext == ".m3u" ? 0 : (ext == ".cue" || ext == ".gdi") ? 1 : 2;
    };
    std::stable_sort(files.begin(), files.end(),
                     [&](const std::string& a, const std::string& b) { return rank(a) < rank(b); });
    mLoadPath = files.front();
    LOG(LogInfo) << "LibretroCore: unpacked " << mContentPath << ", loading " << mLoadPath;
    return true;
}

void LibretroCore::runFrame()
{
    if (!mGameLoaded)
        return;

    CoreContextScope scope {mHWContext};
    mRetroRun();

    if (mVulkan != nullptr) {
        unsigned int width {0};
        unsigned int height {0};
        if (mVulkan->collectFrame(mFrameBGRA, width, height)) {
            mFrameWidth = width;
            mFrameHeight = height;
            mNewFrame = true;
        }
    }
}

void LibretroCore::reset()
{
    if (mGameLoaded) {
        CoreContextScope scope {mHWContext};
        mRetroReset();
    }
}

bool LibretroCore::saveState(const std::string& path)
{
    if (!mGameLoaded)
        return false;
    std::vector<uint8_t> data;
    {
        // GPU cores (N64 and others) use OpenGL while saving, so they need their own context:
        // otherwise their calls change games247's renderer state and the picture goes black.
        CoreContextScope scope {mHWContext};
        const size_t size {mRetroSerializeSize()};
        if (size == 0)
            return false;
        data.resize(size);
        if (!mRetroSerialize(data.data(), size))
            return false;
    }
    const size_t size {data.size()};
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
    if (data.empty())
        return false;
    CoreContextScope scope {mHWContext};
    if (!mRetroUnserialize(data.data(), data.size()))
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
    CoreContextScope scope {mHWContext};
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

bool LibretroCore::replaceSaveRAM(const std::string& path)
{
    if (!mGameLoaded)
        return false;
    CoreContextScope scope {mHWContext};
    const size_t size {mRetroGetMemorySize(RETRO_MEMORY_SAVE_RAM)};
    void* data {mRetroGetMemoryData(RETRO_MEMORY_SAVE_RAM)};
    if (size == 0 || data == nullptr)
        return false;
    std::ifstream file {path, std::ios::binary};
    if (!file)
        return false;
    file.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
    LOG(LogInfo) << "LibretroCore: loaded save data from " << path;
    return true;
}

void LibretroCore::writeSaveRAM()
{
    if (mSaveRAMPath.empty() && !mSaveDirectory.empty())
        mSaveRAMPath = mSaveDirectory + "/" + Utils::FileSystem::getStem(mContentPath) + ".srm";

    CoreContextScope scope {mHWContext};
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

std::map<std::string, std::string> LibretroCore::readOptionFiles(const std::string& corePath)
{
    // Both files are named after the core, e.g. vice_x64sc_libretro.opt.
    const std::string optionFile {Utils::FileSystem::getStem(corePath) + ".opt"};
    std::map<std::string, std::string> options;
    readOptionFile(
        ResourceManager::getInstance().getResourcePath(":/libretro/" + optionFile, false),
        options);
    readOptionFile(CoreManager::coresDirectory() + "/options/" + optionFile, options);
    return options;
}

void LibretroCore::readOptionFile(const std::string& path,
                                  std::map<std::string, std::string>& options)
{
    if (path.empty() || !Utils::FileSystem::isRegularFile(path))
        return;
    std::ifstream file {path};
    std::string line;
    int count {0};
    while (std::getline(file, line)) {
        line = Utils::String::trim(line);
        if (line.empty() || line.front() == '#')
            continue;
        const size_t equals {line.find('=')};
        if (equals == std::string::npos)
            continue;
        const std::string key {Utils::String::trim(line.substr(0, equals))};
        std::string value {Utils::String::trim(line.substr(equals + 1))};
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);
        if (key.empty())
            continue;
        options[key] = value;
        ++count;
    }
    LOG(LogInfo) << "LibretroCore: read " << count << " core option(s) from " << path;
}

void LibretroCore::setupButtonMap()
{
    for (int i {0}; i < 16; ++i)
        mButtonMap[i] = i;
    mHasButtonMap = false;
    mNextControllerHeld = false;

    // RetroPad button names, in RETRO_DEVICE_ID_JOYPAD_* order.
    static const char* const names[16] {"b", "y",     "select", "start", "up", "down",
                                        "left", "right", "a",   "x",     "l",  "r",
                                        "l2",   "r2",    "l3",  "r3"};
    const auto findButton = [](const std::string& name) {
        for (int i {0}; i < 16; ++i) {
            if (name == names[i])
                return i;
        }
        return -2;
    };
    for (int i {0}; i < 16; ++i) {
        const auto it = mOptions.find(std::string {"games247_button_"} + names[i]);
        if (it == mOptions.end())
            continue;
        const std::string target {Utils::String::toLower(it->second)};
        int mapped {findButton(target)};
        if (target == "none")
            mapped = BUTTON_NONE;
        else if (target == "next_controller")
            mapped = BUTTON_NEXT_CONTROLLER;
        if (mapped == -2) {
            LOG(LogWarning) << "LibretroCore: unknown button \"" << it->second << "\" in "
                            << it->first;
            continue;
        }
        mButtonMap[i] = mapped;
        mHasButtonMap = true;
    }

    // Controller types for port 1, matched by name against what the core offers.
    mCycleTypes.clear();
    mCycleIndex = 0;
    const auto typesIt = mOptions.find("games247_controller_types");
    if (typesIt != mOptions.end()) {
        for (const std::string& name :
             Utils::String::delimitedStringToVector(typesIt->second, "|")) {
            for (const auto& type : mControllerTypes[0]) {
                if (Utils::String::toLower(type.first) ==
                    Utils::String::toLower(Utils::String::trim(name)))
                    mCycleTypes.emplace_back(type);
            }
        }
        if (!mCycleTypes.empty()) {
            CoreContextScope scope {mHWContext};
            mRetroSetControllerPortDevice(0, mCycleTypes.front().second);
            LOG(LogInfo) << "LibretroCore: port 1 controller type \"" << mCycleTypes.front().first
                         << "\"";
        }
    }

    // A core option to go through instead, starting from its current value.
    mCycleOption = getOption("games247_cycle_option");
    mCycleValues.clear();
    mCycleLabels.clear();
    mCycleValueIndex = 0;
    mVariablesChanged = false;
    if (!mCycleOption.empty()) {
        for (const std::string& value :
             Utils::String::delimitedStringToVector(getOption("games247_cycle_values"), "|"))
            mCycleValues.emplace_back(Utils::String::trim(value));
        for (const std::string& label :
             Utils::String::delimitedStringToVector(getOption("games247_cycle_labels"), "|"))
            mCycleLabels.emplace_back(Utils::String::trim(label));
        const std::string current {getOption(mCycleOption)};
        for (size_t i {0}; i < mCycleValues.size(); ++i) {
            if (mCycleValues[i] == current)
                mCycleValueIndex = i;
        }
    }
}

std::string LibretroCore::applyButtonMap()
{
    if (!mHasButtonMap)
        return "";
    bool nextController {false};
    for (int port {0}; port < MAX_PORTS; ++port) {
        const uint16_t pressed {padState[port]};
        uint16_t mapped {0};
        for (int i {0}; i < 16; ++i) {
            if (((pressed >> i) & 1) == 0)
                continue;
            if (mButtonMap[i] >= 0 && mButtonMap[i] < 16)
                mapped |= static_cast<uint16_t>(1u << mButtonMap[i]);
            else if (mButtonMap[i] == BUTTON_NEXT_CONTROLLER)
                nextController = true;
        }
        padState[port] = mapped;
    }

    // Switch once per press.
    const bool switchNow {nextController && !mNextControllerHeld};
    mNextControllerHeld = nextController;
    if (!switchNow)
        return "";
    if (mCycleTypes.size() < 2) {
        if (mCycleValues.size() < 2)
            return "";
        mCycleValueIndex = (mCycleValueIndex + 1) % mCycleValues.size();
        mOptions[mCycleOption] = mCycleValues[mCycleValueIndex];
        mVariablesChanged = true;
        LOG(LogInfo) << "LibretroCore: " << mCycleOption << " = \""
                     << mCycleValues[mCycleValueIndex] << "\"";
        return mCycleValueIndex < mCycleLabels.size() ? mCycleLabels[mCycleValueIndex] :
                                                        mCycleValues[mCycleValueIndex];
    }
    mCycleIndex = (mCycleIndex + 1) % mCycleTypes.size();
    {
        CoreContextScope scope {mHWContext};
        mRetroSetControllerPortDevice(0, mCycleTypes[mCycleIndex].second);
    }
    LOG(LogInfo) << "LibretroCore: port 1 controller type \"" << mCycleTypes[mCycleIndex].first
                 << "\"";
    return mCycleTypes[mCycleIndex].first;
}

void LibretroCore::storeOptionDefault(const char* key, const char* value, const char* firstValue)
{
    // Like RetroArch, an option without a default starts at its first value. PUAE, for one,
    // declares its Kickstart option that way, and an empty value makes it look for a
    // Kickstart file with no name.
    if (value == nullptr)
        value = firstValue;
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
        case RETRO_ENVIRONMENT_SET_HW_RENDER: {
            mHardwareRenderRequested = true;
            return acceptHardwareRender(*static_cast<retro_hw_render_callback*>(data));
        }
        case RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT: {
            auto* negotiation {static_cast<retro_hw_render_context_negotiation_interface*>(data)};
            negotiation->interface_version =
                negotiation->interface_type == RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN ?
                    RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN_VERSION :
                    0;
            return true;
        }
        case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE: {
            const auto* negotiation {
                static_cast<const retro_hw_render_context_negotiation_interface*>(data)};
            if (negotiation == nullptr ||
                negotiation->interface_type != RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN)
                return false;
            mVulkanNegotiation =
                reinterpret_cast<const retro_hw_render_context_negotiation_interface_vulkan*>(
                    negotiation);
            return true;
        }
        case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE: {
            if (mVulkan == nullptr || !mVulkan->isCreated())
                return false;
            *static_cast<const retro_hw_render_interface**>(data) =
                reinterpret_cast<const retro_hw_render_interface*>(mVulkan->getInterface());
            return true;
        }
        case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER: {
#if defined(USE_OPENGLES)
            return false;
#else
            *static_cast<unsigned int*>(data) = RETRO_HW_CONTEXT_OPENGL_CORE;
            return true;
#endif
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
                storeOptionDefault(option->key, option->default_value, option->values[0].value);
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL: {
            const retro_core_options_intl* intl {static_cast<const retro_core_options_intl*>(data)};
            for (const retro_core_option_definition* option {intl->us};
                 option != nullptr && option->key != nullptr; ++option)
                storeOptionDefault(option->key, option->default_value, option->values[0].value);
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: {
            const retro_core_options_v2* options {static_cast<const retro_core_options_v2*>(data)};
            for (const retro_core_option_v2_definition* option {options->definitions};
                 option != nullptr && option->key != nullptr; ++option)
                storeOptionDefault(option->key, option->default_value, option->values[0].value);
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL: {
            const retro_core_options_v2_intl* intl {
                static_cast<const retro_core_options_v2_intl*>(data)};
            if (intl->us != nullptr) {
                for (const retro_core_option_v2_definition* option {intl->us->definitions};
                     option != nullptr && option->key != nullptr; ++option)
                    storeOptionDefault(option->key, option->default_value, option->values[0].value);
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
            *static_cast<bool*>(data) = mVariablesChanged;
            mVariablesChanged = false;
            return true;
        }

        // Accepted but nothing for us to do.
        case RETRO_ENVIRONMENT_SET_ROTATION:
        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
            return true;

        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: {
            // Remember the controller types, for games247_controller_types.
            const retro_controller_info* info {static_cast<const retro_controller_info*>(data)};
            for (unsigned int port {0}; info != nullptr && info[port].types != nullptr &&
                                        port < static_cast<unsigned int>(MAX_PORTS);
                 ++port) {
                mControllerTypes[port].clear();
                for (unsigned int i {0}; i < info[port].num_types; ++i) {
                    if (info[port].types[i].desc != nullptr)
                        mControllerTypes[port].emplace_back(info[port].types[i].desc,
                                                            info[port].types[i].id);
                }
            }
            return true;
        }

        // Accepted but nothing for us to do.
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
    if (sActive != nullptr && data == nullptr && sActive->mVulkan != nullptr)
        sActive->mVulkan->dupeFrame();
    if (sActive == nullptr || data == nullptr || width == 0 || height == 0)
        return;

    if (data == RETRO_HW_FRAME_BUFFER_VALID) {
        if (sActive->mVulkan != nullptr) {
            // Copied once retro_run has returned, see runFrame().
            sActive->mVulkan->frameReady(width, height);
            return;
        }
        sActive->readHWFrame(width, height);
        return;
    }

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
                                         unsigned int index,
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
        case RETRO_DEVICE_ANALOG: {
            if (port >= MAX_PORTS)
                return 0;
            sActive->mUsesAnalog = true;
            if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT || index == RETRO_DEVICE_INDEX_ANALOG_RIGHT) {
                if (id > RETRO_DEVICE_ID_ANALOG_Y)
                    return 0;
                return sActive->analogState[port][index * 2 + id];
            }
            // Pressure-sensitive buttons (triggers): fully pressed or not at all.
            if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON && id < 16)
                return ((sActive->padState[port] >> id) & 1) ? 0x7FFF : 0;
            return 0;
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

// GPU-rendered cores.

uintptr_t LibretroCore::hwGetCurrentFramebuffer()
{
    return sActive != nullptr ? static_cast<uintptr_t>(sActive->mHWFramebuffer) : 0;
}

retro_proc_address_t LibretroCore::hwGetProcAddress(const char* symbol)
{
    return reinterpret_cast<retro_proc_address_t>(SDL_GL_GetProcAddress(symbol));
}

bool LibretroCore::acceptHardwareRender(retro_hw_render_callback& request)
{
    LOG(LogInfo) << "LibretroCore: core asks for GPU rendering, context type "
                 << request.context_type << ", version " << request.version_major << "."
                 << request.version_minor;
#if defined(USE_OPENGLES)
    return false;
#else
    // Vulkan: the device is created once the game has loaded (LibretroVulkan), as in
    // RetroArch. Without a Vulkan loader on the computer the core falls back to RetroArch.
    if (request.context_type == RETRO_HW_CONTEXT_VULKAN) {
        if (!LibretroVulkan::loaderAvailable()) {
            LOG(LogWarning) << "LibretroCore: the core needs Vulkan, which isn't available";
            return false;
        }
        destroyHWFramebuffer();
        mHWRender = request;
        mHardwareRender = true;
        mVulkanRender = true;
        return true;
    }

    // The core gets an OpenGL context of its own, of the kind it asks for: a core profile
    // context (modern OpenGL) or a compatibility profile one (older OpenGL, which some builds
    // of the cores use, for example Mupen64Plus-Next on Windows). Direct3D cores aren't
    // supported.
    if (request.context_type != RETRO_HW_CONTEXT_OPENGL_CORE &&
        request.context_type != RETRO_HW_CONTEXT_OPENGL) {
        LOG(LogWarning) << "LibretroCore: this kind of GPU rendering isn't supported";
        return false;
    }

    // A core can ask more than once, for example trying a newer version first.
    destroyHWFramebuffer();

    std::string error;
    if (!createHWContext(request, error)) {
        LOG(LogWarning) << "LibretroCore: " << error;
        return false;
    }

    request.get_current_framebuffer = &LibretroCore::hwGetCurrentFramebuffer;
    request.get_proc_address = &LibretroCore::hwGetProcAddress;
    mHWRender = request;
    mHardwareRender = true;
    return true;
#endif
}

bool LibretroCore::createHWContext(const retro_hw_render_callback& request, std::string& error)
{
#if defined(USE_OPENGLES)
    error = "GPU rendering isn't supported on this platform";
    return false;
#else
    SDL_Window* window {SDL_GL_GetCurrentWindow()};
    SDL_GLContext mainContext {SDL_GL_GetCurrentContext()};
    if (window == nullptr || mainContext == nullptr) {
        error = "no OpenGL window for the GPU core";
        return false;
    }

    // The context attributes are global in SDL, so put games247's own back afterwards.
    int profile {0};
    int major {0};
    int minor {0};
    int share {0};
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &profile);
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minor);
    SDL_GL_GetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, &share);

    const bool coreProfile {request.context_type == RETRO_HW_CONTEXT_OPENGL_CORE};
    int wantMajor {static_cast<int>(request.version_major)};
    int wantMinor {static_cast<int>(request.version_minor)};
    if (coreProfile && (wantMajor < 3 || (wantMajor == 3 && wantMinor < 2))) {
        // Core profiles start at OpenGL 3.2.
        wantMajor = 3;
        wantMinor = 2;
    }
    else if (!coreProfile && wantMajor < 3) {
        // Any compatibility context runs OpenGL 2 code; ask for the oldest so every driver
        // (including macOS, which only has 2.1 for these) can provide one.
        wantMajor = 2;
        wantMinor = 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, coreProfile ?
                                                         SDL_GL_CONTEXT_PROFILE_CORE :
                                                         SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, wantMajor);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, wantMinor);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
    mHWContext = SDL_GL_CreateContext(window);
    const std::string sdlError {mHWContext == nullptr ? SDL_GetError() : ""};

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, profile);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, share);
    // SDL_GL_CreateContext() makes the new context current.
    SDL_GL_MakeCurrent(window, mainContext);

    if (mHWContext == nullptr) {
        error = "couldn't create an OpenGL " + std::to_string(wantMajor) + "." +
                std::to_string(wantMinor) + (coreProfile ? " core" : " compatibility") +
                " context for the core: " + sdlError;
        return false;
    }

    std::string version;
    {
        CoreContextScope scope {mHWContext};
        const GLubyte* versionString {glGetString(GL_VERSION)};
        version = versionString ? reinterpret_cast<const char*>(versionString) : "unknown";
    }
    LOG(LogInfo) << "LibretroCore: created an OpenGL context for the core ("
                 << (coreProfile ? "core" : "compatibility") << " profile, " << version << ")";
    return true;
#endif
}

bool LibretroCore::createHWFramebuffer(std::string& error)
{
#if defined(USE_OPENGLES)
    error = "GPU rendering isn't supported on this platform";
    return false;
#else
    if (mHWContext == nullptr) {
        error = "no OpenGL context for the GPU core";
        return false;
    }
    CoreContextScope scope {mHWContext};

    mHWWidth = std::max({mAVInfo.geometry.max_width, mAVInfo.geometry.base_width, 1u});
    mHWHeight = std::max({mAVInfo.geometry.max_height, mAVInfo.geometry.base_height, 1u});

    glGenTextures(1, &mHWColorTexture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mHWColorTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(mHWWidth),
                 static_cast<GLsizei>(mHWHeight), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenFramebuffers(1, &mHWFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, mHWFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mHWColorTexture, 0);

    if (mHWRender.depth) {
        glGenRenderbuffers(1, &mHWDepthBuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, mHWDepthBuffer);
        glRenderbufferStorage(GL_RENDERBUFFER,
                              mHWRender.stencil ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24,
                              static_cast<GLsizei>(mHWWidth), static_cast<GLsizei>(mHWHeight));
        glFramebufferRenderbuffer(
            GL_FRAMEBUFFER, mHWRender.stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
            GL_RENDERBUFFER, mHWDepthBuffer);
    }

    const GLenum status {glCheckFramebufferStatus(GL_FRAMEBUFFER)};
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        error = "couldn't set up the GPU framebuffer";
        LOG(LogError) << "LibretroCore: framebuffer incomplete (status " << status << ")";
        return false;
    }

    glViewport(0, 0, static_cast<GLsizei>(mHWWidth), static_cast<GLsizei>(mHWHeight));
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    LOG(LogInfo) << "LibretroCore: GPU framebuffer " << mHWWidth << "x" << mHWHeight
                 << (mHWRender.depth ? (mHWRender.stencil ? ", depth and stencil" : ", depth") :
                                       "");
    return true;
#endif
}

void LibretroCore::destroyHWFramebuffer()
{
#if !defined(USE_OPENGLES)
    if (mHWContext == nullptr)
        return;
    {
        CoreContextScope scope {mHWContext};
        if (mHWDepthBuffer != 0)
            glDeleteRenderbuffers(1, &mHWDepthBuffer);
        if (mHWFramebuffer != 0)
            glDeleteFramebuffers(1, &mHWFramebuffer);
        if (mHWColorTexture != 0)
            glDeleteTextures(1, &mHWColorTexture);
    }
    SDL_GL_DeleteContext(static_cast<SDL_GLContext>(mHWContext));
    mHWContext = nullptr;
#endif
    mHWDepthBuffer = 0;
    mHWFramebuffer = 0;
    mHWColorTexture = 0;
    mHWWidth = 0;
    mHWHeight = 0;
}

void LibretroCore::readHWFrame(unsigned int width, unsigned int height)
{
#if !defined(USE_OPENGLES)
    if (mHWFramebuffer == 0)
        return;

    width = std::min(width, mHWWidth);
    height = std::min(height, mHWHeight);

    // Copy the picture out of the core's framebuffer. The core may keep drawing after this
    // (some cores render more than one frame per retro_run), so leave its state as it was.
    GLint readFramebuffer {0};
    GLint packBuffer {0};
    GLint packAlignment {4};
    GLint packRowLength {0};
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment);
    glGetIntegerv(GL_PACK_ROW_LENGTH, &packRowLength);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, mHWFramebuffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);

    mFrameBGRA.resize(static_cast<size_t>(width) * height * 4);
    glReadPixels(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height), GL_BGRA,
                 GL_UNSIGNED_BYTE, mFrameBGRA.data());

    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFramebuffer));
    glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(packBuffer));
    glPixelStorei(GL_PACK_ALIGNMENT, packAlignment);
    glPixelStorei(GL_PACK_ROW_LENGTH, packRowLength);

    // OpenGL rows start at the bottom. Most cores draw the right way up for that, so flip the
    // rows; the alpha channel isn't meaningful, so make every pixel opaque.
    const size_t rowBytes {static_cast<size_t>(width) * 4};
    if (mHWRender.bottom_left_origin) {
        std::vector<uint8_t> row(rowBytes);
        for (unsigned int y {0}; y < height / 2; ++y) {
            uint8_t* top {mFrameBGRA.data() + y * rowBytes};
            uint8_t* bottom {mFrameBGRA.data() + (height - 1 - y) * rowBytes};
            std::memcpy(row.data(), top, rowBytes);
            std::memcpy(top, bottom, rowBytes);
            std::memcpy(bottom, row.data(), rowBytes);
        }
    }
    for (size_t i {3}; i < mFrameBGRA.size(); i += 4)
        mFrameBGRA[i] = 0xFF;

    mFrameWidth = width;
    mFrameHeight = height;
    mNewFrame = true;
#else
    static_cast<void>(width);
    static_cast<void>(height);
#endif
}
