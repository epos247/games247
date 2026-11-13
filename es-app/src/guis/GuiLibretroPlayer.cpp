//  SPDX-License-Identifier: MIT
//
//  games247
//  GuiLibretroPlayer.cpp
//
//  Full-screen built-in game player. Runs a libretro core inside games247.
//

#include "guis/GuiLibretroPlayer.h"

#include "FileData.h"
#include "InputConfig.h"
#include "Log.h"
#include "Window.h"
#include "renderers/Renderer.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"
#include "views/ViewController.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Maps a libretro RETROK_* key code to an SDL scancode.
    SDL_Scancode retrokToScancode(unsigned int key)
    {
        if (key > 0 && key < 128)
            return SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(key));
        if (key == RETROK_KP0)
            return SDL_SCANCODE_KP_0;
        if (key >= RETROK_KP1 && key <= RETROK_KP9)
            return static_cast<SDL_Scancode>(SDL_SCANCODE_KP_1 + (key - RETROK_KP1));
        if (key >= RETROK_F1 && key <= RETROK_F12)
            return static_cast<SDL_Scancode>(SDL_SCANCODE_F1 + (key - RETROK_F1));
        if (key >= RETROK_F13 && key <= RETROK_F15)
            return static_cast<SDL_Scancode>(SDL_SCANCODE_F13 + (key - RETROK_F13));

        switch (key) {
            case RETROK_KP_PERIOD: return SDL_SCANCODE_KP_PERIOD;
            case RETROK_KP_DIVIDE: return SDL_SCANCODE_KP_DIVIDE;
            case RETROK_KP_MULTIPLY: return SDL_SCANCODE_KP_MULTIPLY;
            case RETROK_KP_MINUS: return SDL_SCANCODE_KP_MINUS;
            case RETROK_KP_PLUS: return SDL_SCANCODE_KP_PLUS;
            case RETROK_KP_ENTER: return SDL_SCANCODE_KP_ENTER;
            case RETROK_KP_EQUALS: return SDL_SCANCODE_KP_EQUALS;
            case RETROK_UP: return SDL_SCANCODE_UP;
            case RETROK_DOWN: return SDL_SCANCODE_DOWN;
            case RETROK_RIGHT: return SDL_SCANCODE_RIGHT;
            case RETROK_LEFT: return SDL_SCANCODE_LEFT;
            case RETROK_INSERT: return SDL_SCANCODE_INSERT;
            case RETROK_HOME: return SDL_SCANCODE_HOME;
            case RETROK_END: return SDL_SCANCODE_END;
            case RETROK_PAGEUP: return SDL_SCANCODE_PAGEUP;
            case RETROK_PAGEDOWN: return SDL_SCANCODE_PAGEDOWN;
            case RETROK_NUMLOCK: return SDL_SCANCODE_NUMLOCKCLEAR;
            case RETROK_CAPSLOCK: return SDL_SCANCODE_CAPSLOCK;
            case RETROK_SCROLLOCK: return SDL_SCANCODE_SCROLLLOCK;
            case RETROK_RSHIFT: return SDL_SCANCODE_RSHIFT;
            case RETROK_LSHIFT: return SDL_SCANCODE_LSHIFT;
            case RETROK_RCTRL: return SDL_SCANCODE_RCTRL;
            case RETROK_LCTRL: return SDL_SCANCODE_LCTRL;
            case RETROK_RALT: return SDL_SCANCODE_RALT;
            case RETROK_LALT: return SDL_SCANCODE_LALT;
            case RETROK_RMETA:
            case RETROK_RSUPER: return SDL_SCANCODE_RGUI;
            case RETROK_LMETA:
            case RETROK_LSUPER: return SDL_SCANCODE_LGUI;
            case RETROK_MENU: return SDL_SCANCODE_APPLICATION;
            case RETROK_PRINT: return SDL_SCANCODE_PRINTSCREEN;
            case RETROK_BREAK: return SDL_SCANCODE_PAUSE;
            default: return SDL_SCANCODE_UNKNOWN;
        }
    }

    // games247 controller mapping names -> RetroPad buttons. games247's "a" is the bottom face
    // button, which is the RetroPad's B (SNES layout).
    int retroPadButton(const std::string& name)
    {
        if (name == "a") return RETRO_DEVICE_ID_JOYPAD_B;
        if (name == "b") return RETRO_DEVICE_ID_JOYPAD_A;
        if (name == "x") return RETRO_DEVICE_ID_JOYPAD_Y;
        if (name == "y") return RETRO_DEVICE_ID_JOYPAD_X;
        if (name == "back") return RETRO_DEVICE_ID_JOYPAD_SELECT;
        if (name == "start") return RETRO_DEVICE_ID_JOYPAD_START;
        if (name == "leftshoulder") return RETRO_DEVICE_ID_JOYPAD_L;
        if (name == "rightshoulder") return RETRO_DEVICE_ID_JOYPAD_R;
        if (name == "lefttrigger") return RETRO_DEVICE_ID_JOYPAD_L2;
        if (name == "righttrigger") return RETRO_DEVICE_ID_JOYPAD_R2;
        if (name == "leftthumbstickclick") return RETRO_DEVICE_ID_JOYPAD_L3;
        if (name == "rightthumbstickclick") return RETRO_DEVICE_ID_JOYPAD_R3;
        if (name == "up" || name == "leftanalogup") return RETRO_DEVICE_ID_JOYPAD_UP;
        if (name == "down" || name == "leftanalogdown") return RETRO_DEVICE_ID_JOYPAD_DOWN;
        if (name == "left" || name == "leftanalogleft") return RETRO_DEVICE_ID_JOYPAD_LEFT;
        if (name == "right" || name == "leftanalogright") return RETRO_DEVICE_ID_JOYPAD_RIGHT;
        return -1;
    }

    // Keyboard -> RetroPad, using RetroArch's default keyboard layout.
    template <typename KeyDown> uint16_t keyboardPadState(KeyDown keyDown)
    {
        uint16_t state {0};
        auto set = [&](SDL_Scancode scancode, int button) {
            if (keyDown(scancode))
                state |= static_cast<uint16_t>(1 << button);
        };
        set(SDL_SCANCODE_UP, RETRO_DEVICE_ID_JOYPAD_UP);
        set(SDL_SCANCODE_DOWN, RETRO_DEVICE_ID_JOYPAD_DOWN);
        set(SDL_SCANCODE_LEFT, RETRO_DEVICE_ID_JOYPAD_LEFT);
        set(SDL_SCANCODE_RIGHT, RETRO_DEVICE_ID_JOYPAD_RIGHT);
        set(SDL_SCANCODE_X, RETRO_DEVICE_ID_JOYPAD_A);
        set(SDL_SCANCODE_Z, RETRO_DEVICE_ID_JOYPAD_B);
        set(SDL_SCANCODE_S, RETRO_DEVICE_ID_JOYPAD_X);
        set(SDL_SCANCODE_A, RETRO_DEVICE_ID_JOYPAD_Y);
        set(SDL_SCANCODE_Q, RETRO_DEVICE_ID_JOYPAD_L);
        set(SDL_SCANCODE_W, RETRO_DEVICE_ID_JOYPAD_R);
        set(SDL_SCANCODE_RETURN, RETRO_DEVICE_ID_JOYPAD_START);
        set(SDL_SCANCODE_RSHIFT, RETRO_DEVICE_ID_JOYPAD_SELECT);
        return state;
    }

    uint16_t currentKeyModifiers()
    {
        const SDL_Keymod mod {SDL_GetModState()};
        uint16_t result {0};
        if (mod & KMOD_SHIFT)
            result |= RETROKMOD_SHIFT;
        if (mod & KMOD_CTRL)
            result |= RETROKMOD_CTRL;
        if (mod & KMOD_ALT)
            result |= RETROKMOD_ALT;
        if (mod & KMOD_GUI)
            result |= RETROKMOD_META;
        return result;
    }
} // namespace

bool GuiLibretroPlayer::launch(const std::string& corePath,
                               const std::string& romPath,
                               FileData* game,
                               std::string& error)
{
    GuiLibretroPlayer* player {new GuiLibretroPlayer()};
    player->mGame = game;
    if (!player->start(corePath, romPath, error)) {
        delete player;
        return false;
    }
    ViewController::getInstance()->stopViewVideos();
    Window::getInstance()->setFullscreenGui(true);
    Window::getInstance()->pushGui(player);
    return true;
}

GuiLibretroPlayer::GuiLibretroPlayer()
    : mCore {std::make_unique<LibretroCore>()}
    , mGame {nullptr}
    , mAudioDevice {0}
    , mAudioBytesPerFrame {0}
    , mLastTime {0}
    , mTimeAccumulator {0.0}
    , mPreviousKeys(RETROK_LAST, false)
    , mCloseRequested {false}
{
    Renderer* renderer {Renderer::getInstance()};
    setSize(renderer->getScreenWidth(), renderer->getScreenHeight());
    mKeysHeld.fill(false);
    mKeysLatched.fill(false);
}

bool GuiLibretroPlayer::isScancodeDown(SDL_Scancode scancode) const
{
    if (scancode <= SDL_SCANCODE_UNKNOWN || scancode >= SDL_NUM_SCANCODES)
        return false;
    return mKeysHeld[scancode] || mKeysLatched[scancode];
}

GuiLibretroPlayer::~GuiLibretroPlayer()
{
    if (mAudioDevice != 0) {
        SDL_CloseAudioDevice(mAudioDevice);
        mAudioDevice = 0;
    }
    if (mCore && mCore->isLoaded()) {
        mCore->unload();
        LOG(LogInfo) << "GuiLibretroPlayer: closed built-in player";
        Window::getInstance()->setFullscreenGui(false);
        ViewController::getInstance()->startViewVideos();
        Window::getInstance()->normalizeNextUpdate();
    }
}

bool GuiLibretroPlayer::start(const std::string& corePath,
                              const std::string& romPath,
                              std::string& error)
{
    const std::string appData {Utils::FileSystem::getAppDataDirectory()};

    // Reuse RetroArch's BIOS folder when it exists, so BIOS files only need to be set up once.
    std::string systemDirectory {Utils::FileSystem::getHomePath() +
                                 "/Library/Application Support/RetroArch/system"};
    if (!Utils::FileSystem::isDirectory(systemDirectory)) {
        systemDirectory = appData + "/bios";
        Utils::FileSystem::createDirectory(systemDirectory);
    }
    const std::string saveDirectory {appData + "/saves"};
    Utils::FileSystem::createDirectory(saveDirectory);

    mCore->keyboardQuery = [this](unsigned int retrok) {
        return isScancodeDown(retrokToScancode(retrok));
    };
    if (!mCore->load(corePath, romPath, systemDirectory, saveDirectory, error))
        return false;

    // Open our own audio output at the core's sample rate. SDL converts it to the device format.
    SDL_AudioSpec wanted {};
    SDL_AudioSpec obtained {};
    wanted.freq = static_cast<int>(std::lround(mCore->getSampleRate()));
    wanted.format = AUDIO_S16SYS;
    wanted.channels = 2;
    wanted.samples = 512;
    mAudioDevice = SDL_OpenAudioDevice(nullptr, 0, &wanted, &obtained, 0);
    if (mAudioDevice == 0) {
        LOG(LogWarning) << "GuiLibretroPlayer: no audio output (" << SDL_GetError() << ")";
    }
    else {
        mAudioBytesPerFrame = static_cast<size_t>(mCore->getSampleRate() / mCore->getFPS()) * 4;
        SDL_PauseAudioDevice(mAudioDevice, 0);
    }

    mTexture = TextureResource::get("", false, false, true, false);
    mLastTime = SDL_GetPerformanceCounter();
    mTimeAccumulator = 1.0 / mCore->getFPS();
    return true;
}

bool GuiLibretroPlayer::input(InputConfig* config, Input input)
{
    if (config->getDeviceId() == DEVICE_KEYBOARD) {
        if (input.id == SDLK_ESCAPE && input.value != 0) {
            mCloseRequested = true;
            return true;
        }
        const SDL_Scancode scancode {SDL_GetScancodeFromKey(input.id)};
        if (scancode > SDL_SCANCODE_UNKNOWN && scancode < SDL_NUM_SCANCODES) {
            mKeysHeld[scancode] = input.value != 0;
            // Latch presses so a tap shorter than one frame still reaches the core.
            if (input.value != 0)
                mKeysLatched[scancode] = true;
        }
        return true;
    }

    uint16_t& state {mControllerState[input.device]};
    uint16_t& latched {mControllerLatched[input.device]};
    for (const std::string& name : config->getMappedTo(input)) {
        const int button {retroPadButton(Utils::String::toLower(name))};
        if (button < 0)
            continue;
        if (input.value != 0) {
            state |= static_cast<uint16_t>(1 << button);
            latched |= static_cast<uint16_t>(1 << button);
        }
        else {
            state &= static_cast<uint16_t>(~(1 << button));
        }
    }

    if (mDevicePorts.find(input.device) == mDevicePorts.end()) {
        const int port {static_cast<int>(mDevicePorts.size())};
        mDevicePorts[input.device] = port;
        LOG(LogInfo) << "GuiLibretroPlayer: controller " << input.device << " is player "
                     << port + 1;
    }

    // Hold Back (Select) + Start to leave the game.
    constexpr uint16_t exitCombo {(1 << RETRO_DEVICE_ID_JOYPAD_SELECT) |
                                  (1 << RETRO_DEVICE_ID_JOYPAD_START)};
    if ((state & exitCombo) == exitCombo)
        mCloseRequested = true;

    return true;
}

void GuiLibretroPlayer::updateInput()
{
    mCore->padState.fill(0);
    for (const auto& [device, state] : mControllerState) {
        const auto portIt = mDevicePorts.find(device);
        const int port {portIt == mDevicePorts.end() ? 0 : portIt->second};
        if (port < LibretroCore::MAX_PORTS)
            mCore->padState[port] |= state | mControllerLatched[device];
    }
    mCore->padState[0] |=
        keyboardPadState([this](SDL_Scancode scancode) { return isScancodeDown(scancode); });

    // Send key presses and releases to cores that use a keyboard callback (computers).
    const uint16_t modifiers {currentKeyModifiers()};
    for (unsigned int key {1}; key < RETROK_LAST; ++key) {
        const bool down {isScancodeDown(retrokToScancode(key))};
        if (down != mPreviousKeys[key]) {
            mPreviousKeys[key] = down;
            const uint32_t character {key < 128 ? key : 0};
            mCore->keyboardEvent(down, key, character, modifiers);
        }
    }
}

void GuiLibretroPlayer::queueAudio()
{
    const std::vector<int16_t>& audio {mCore->getAudio()};
    if (mAudioDevice != 0 && !audio.empty())
        SDL_QueueAudio(mAudioDevice, audio.data(),
                       static_cast<Uint32>(audio.size() * sizeof(int16_t)));
    mCore->clearAudio();
}

void GuiLibretroPlayer::runFrames()
{
    const Uint64 now {SDL_GetPerformanceCounter()};
    double elapsed {static_cast<double>(now - mLastTime) /
                    static_cast<double>(SDL_GetPerformanceFrequency())};
    mLastTime = now;
    elapsed = std::min(elapsed, 0.25);

    const double frameTime {1.0 / mCore->getFPS()};

    if (mAudioDevice != 0 && mAudioBytesPerFrame > 0) {
        // Keep about four frames of audio queued. Nudge the emulation speed by up to 0.5%
        // to stay there, which avoids crackles without audible pitch changes.
        const double queuedFrames {static_cast<double>(SDL_GetQueuedAudioSize(mAudioDevice)) /
                                   static_cast<double>(mAudioBytesPerFrame)};
        const double targetFrames {4.0};
        if (queuedFrames > targetFrames * 4.0) {
            SDL_ClearQueuedAudio(mAudioDevice);
        }
        else {
            const double error {std::clamp((targetFrames - queuedFrames) / targetFrames, -1.0, 1.0)};
            elapsed *= 1.0 + error * 0.005;
        }
    }

    mTimeAccumulator += elapsed;

    int framesRun {0};
    while (mTimeAccumulator >= frameTime && framesRun < 3) {
        updateInput();
        mCore->runFrame();
        queueAudio();
        // The core has now seen every latched press once.
        mKeysLatched.fill(false);
        for (auto& [device, latched] : mControllerLatched)
            latched = 0;
        mTimeAccumulator -= frameTime;
        ++framesRun;
    }
    // If we fell far behind (for example after a stall), don't try to catch up.
    if (mTimeAccumulator > frameTime * 3.0)
        mTimeAccumulator = 0.0;

    // Debug statistics every 5 seconds: actual emulation speed and audio buffer level.
    mStatsFrames += framesRun;
    if (mStatsStart == 0)
        mStatsStart = now;
    const double statsSeconds {static_cast<double>(now - mStatsStart) /
                               static_cast<double>(SDL_GetPerformanceFrequency())};
    if (statsSeconds >= 5.0) {
        const double queuedMs {mAudioDevice == 0 ?
                                   0.0 :
                                   SDL_GetQueuedAudioSize(mAudioDevice) /
                                       (mCore->getSampleRate() * 4.0) * 1000.0};
        LOG(LogDebug) << "GuiLibretroPlayer: " << mStatsFrames / statsSeconds
                      << " fps (target " << mCore->getFPS() << "), audio queued " << queuedMs
                      << " ms";
        mStatsFrames = 0;
        mStatsStart = now;
    }
}

void GuiLibretroPlayer::update(int /*deltaTime*/)
{
    if (mCloseRequested) {
        delete this;
        return;
    }
    // games247 blocks input during the launch animation and normally unblocks it when an
    // external emulator exits. The built-in player is still running, so unblock it here.
    if (!mInputUnblocked) {
        Window::getInstance()->setBlockInput(false);
        mInputUnblocked = true;
    }
    runFrames();
}

void GuiLibretroPlayer::render(const glm::mat4& parentTrans)
{
    Renderer* renderer {Renderer::getInstance()};
    const glm::mat4 trans {parentTrans * getTransform()};
    renderer->setMatrix(trans);
    renderer->drawRect(0.0f, 0.0f, mSize.x, mSize.y, 0x000000FF, 0x000000FF);

    if (mCore->hasNewFrame() && !mCore->getFrameBGRA().empty()) {
        mTexture->initFromPixels(mCore->getFrameBGRA().data(), mCore->getFrameWidth(),
                                 mCore->getFrameHeight());
        mCore->clearNewFrame();
    }
    if (mCore->getFrameWidth() == 0 || mCore->getFrameHeight() == 0)
        return;

    // Fit the picture to the screen, keeping the core's aspect ratio.
    const float aspect {mCore->getAspectRatio()};
    float width {mSize.x};
    float height {mSize.x / aspect};
    if (height > mSize.y) {
        height = mSize.y;
        width = mSize.y * aspect;
    }
    const float x {std::round((mSize.x - width) / 2.0f)};
    const float y {std::round((mSize.y - height) / 2.0f)};
    width = std::round(width);
    height = std::round(height);

    Renderer::Vertex vertices[4];
    // clang-format off
    vertices[0] = {{x,         y         }, {0.0f, 0.0f}, 0xFFFFFFFF};
    vertices[1] = {{x,         y + height}, {0.0f, 1.0f}, 0xFFFFFFFF};
    vertices[2] = {{x + width, y         }, {1.0f, 0.0f}, 0xFFFFFFFF};
    vertices[3] = {{x + width, y + height}, {1.0f, 1.0f}, 0xFFFFFFFF};
    // clang-format on

    mTexture->bind(0);
    renderer->drawTriangleStrips(&vertices[0], 4, Renderer::BlendFactor::SRC_ALPHA,
                                 Renderer::BlendFactor::ONE_MINUS_SRC_ALPHA);
}
