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
#include "MusicManager.h"
#include "components/BackgroundComponent.h"
#include "Settings.h"
#include "Window.h"
#include "renderers/Renderer.h"
#include "resources/Font.h"
#include "utils/LocalizationUtil.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"
#include "views/ViewController.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace
{
    // Pause menu entries, in order.
    enum MenuItem {
        MENU_RESUME,
        MENU_SAVE_STATE,
        MENU_LOAD_STATE,
        MENU_SCREEN,
        MENU_CRT_FILTER,
        MENU_RESTART,
        MENU_QUIT
    };

    const std::vector<std::string> screenModes {"fit", "stretch", "pixelperfect"};

    std::string screenModeLabel(const std::string& mode)
    {
        if (mode == "stretch")
            return _("STRETCH TO SCREEN");
        if (mode == "pixelperfect")
            return _("PIXEL PERFECT");
        return _("FIT TO SCREEN");
    }

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

    // The file names in a zip file, read from its central directory. Empty if it isn't a zip
    // file or can't be read.
    std::vector<std::string> zipFileNames(const std::string& path)
    {
        std::vector<std::string> names;
        std::ifstream file {path, std::ios::binary};
        if (!file)
            return names;
        file.seekg(0, std::ios::end);
        const std::streamoff fileSize {file.tellg()};
        // The end record is 22 bytes plus a comment of up to 65535 bytes.
        const std::streamoff tailSize {std::min<std::streamoff>(fileSize, 22 + 65535)};
        if (tailSize < 22)
            return names;
        std::vector<unsigned char> tail(static_cast<size_t>(tailSize));
        file.seekg(fileSize - tailSize);
        file.read(reinterpret_cast<char*>(tail.data()), tailSize);
        if (!file)
            return names;

        auto read16 = [](const unsigned char* p) { return static_cast<uint32_t>(p[0] | p[1] << 8); };
        auto read32 = [](const unsigned char* p) {
            return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
                   static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
        };

        for (std::streamoff i {tailSize - 22}; i >= 0; --i) {
            const unsigned char* end {&tail[static_cast<size_t>(i)]};
            if (read32(end) != 0x06054b50)
                continue;
            const uint32_t count {read16(end + 10)};
            const uint32_t offset {read32(end + 16)};
            file.clear();
            file.seekg(offset);
            for (uint32_t entry {0}; entry < count && entry < 10000; ++entry) {
                unsigned char header[46];
                if (!file.read(reinterpret_cast<char*>(header), sizeof(header)) ||
                    read32(header) != 0x02014b50)
                    break;
                std::string name(read16(header + 28), '\0');
                if (!file.read(name.data(), static_cast<std::streamsize>(name.size())))
                    break;
                names.emplace_back(name);
                file.seekg(read16(header + 30) + read16(header + 32), std::ios::cur);
            }
            break;
        }
        return names;
    }

    // True if the game's extension is one of these, or it's a zip holding such a file.
    bool hasExtension(const std::string& path, const std::vector<std::string>& extensions)
    {
        auto matches = [&extensions](const std::string& fileName) {
            const std::string extension {
                Utils::String::toLower(Utils::FileSystem::getExtension(fileName))};
            return extension.size() > 1 &&
                   std::find(extensions.cbegin(), extensions.cend(), extension.substr(1)) !=
                       extensions.cend();
        };
        if (matches(path))
            return true;
        if (Utils::String::toLower(Utils::FileSystem::getExtension(path)) != ".zip")
            return false;
        const std::vector<std::string> names {zipFileNames(path)};
        return std::any_of(names.cbegin(), names.cend(), matches);
    }
} // namespace

bool GuiLibretroPlayer::launch(const std::string& corePath,
                               const std::string& romPath,
                               FileData* game,
                               std::string& error)
{
    GuiLibretroPlayer* player {new GuiLibretroPlayer()};
    player->mGame = game;

    // A choice to make before the game starts, such as the tape loading speed.
    const std::map<std::string, std::string> options {LibretroCore::readOptionFiles(corePath)};
    auto option = [&options](const std::string& key) {
        const auto it = options.find(key);
        return it == options.end() ? std::string {} : it->second;
    };
    const std::string choiceOption {option("games247_load_choice_option")};
    const std::vector<std::string> values {
        Utils::String::delimitedStringToVector(option("games247_load_choice_values"), "|")};
    const std::vector<std::string> labels {
        Utils::String::delimitedStringToVector(option("games247_load_choice_labels"), "|")};
    const std::vector<std::string> extensions {Utils::String::delimitedStringToVector(
        Utils::String::toLower(option("games247_load_choice_extensions")), "|")};

    if (!choiceOption.empty() && values.size() >= 2 && labels.size() == values.size() &&
        hasExtension(romPath, extensions)) {
        player->mChoosing = true;
        player->mChoiceOption = choiceOption;
        player->mChoiceValues = values;
        player->mPendingCorePath = corePath;
        player->mPendingRomPath = romPath;
        std::vector<std::string> upperLabels;
        for (const std::string& label : labels)
            upperLabels.emplace_back(Utils::String::toUpper(label));
        const std::string title {option("games247_load_choice_title")};
        player->buildPanel(Utils::String::toUpper(title.empty() ? _("LOADING") : title),
                           upperLabels, player->mChoiceTitle, player->mChoiceItems);
        LOG(LogInfo) << "GuiLibretroPlayer: asking for " << choiceOption << " before starting";
    }
    else if (!player->start(corePath, romPath, error)) {
        delete player;
        return false;
    }
    ViewController::getInstance()->stopViewVideos();
    Window::getInstance()->setFullscreenGui(true);
    Window::getInstance()->pushGui(player);
    player->mShown = true;
    return true;
}

void GuiLibretroPlayer::buildPanel(const std::string& title,
                                   const std::vector<std::string>& labels,
                                   std::shared_ptr<TextComponent>& titleComponent,
                                   std::vector<std::shared_ptr<TextComponent>>& items)
{
    const std::shared_ptr<Font> font {Font::get(FONT_SIZE_MEDIUM)};
    const float rowHeight {font->getHeight(1.6f)};
    const float panelHeight {rowHeight * static_cast<float>(labels.size() + 2)};
    const float top {(mSize.y - panelHeight) / 2.0f};
    titleComponent = std::make_shared<TextComponent>(
        title, Font::get(FONT_SIZE_LARGE * 0.72f, FONT_PATH_TITLE), mMenuColorTitle,
        ALIGN_CENTER, ALIGN_CENTER, glm::ivec2 {0, 0});
    titleComponent->setSize(mSize.x * 0.4f, rowHeight * 1.5f);
    titleComponent->setPosition(mSize.x * 0.3f, top + rowHeight * 0.25f);
    items.clear();
    for (size_t i {0}; i < labels.size(); ++i) {
        auto item = std::make_shared<TextComponent>(labels[i], font, mMenuColorPrimary,
                                                    ALIGN_CENTER, ALIGN_CENTER, glm::ivec2 {0, 0});
        item->setSize(mSize.x * 0.4f, rowHeight);
        item->setPosition(mSize.x * 0.3f, top + rowHeight * (static_cast<float>(i) + 1.75f));
        items.push_back(item);
    }
}

void GuiLibretroPlayer::choiceInput(InputConfig* config, Input input)
{
    if (input.value == 0)
        return;
    const int count {static_cast<int>(mChoiceItems.size())};
    if (config->isMappedLike("up", input) || config->isMappedLike("left", input)) {
        mChoiceSelection = (mChoiceSelection + count - 1) % count;
    }
    else if (config->isMappedLike("down", input) || config->isMappedLike("right", input)) {
        mChoiceSelection = (mChoiceSelection + 1) % count;
    }
    else if (config->isMappedTo("a", input)) {
        startAfterChoice();
    }
    else if (config->isMappedTo("b", input) ||
             (config->getDeviceId() == DEVICE_KEYBOARD && input.id == SDLK_ESCAPE)) {
        LOG(LogInfo) << "GuiLibretroPlayer: game not started";
        mCloseRequested = true;
    }
}

void GuiLibretroPlayer::startAfterChoice()
{
    mChoosing = false;
    mCore->setOptionOverride(mChoiceOption,
                             mChoiceValues[static_cast<size_t>(mChoiceSelection)]);
    std::string error;
    if (!start(mPendingCorePath, mPendingRomPath, error)) {
        LOG(LogError) << "GuiLibretroPlayer: couldn't start the game: " << error;
        Window::getInstance()->queueInfoPopup(
            Utils::String::format(_("COULDN'T START THE GAME: %s"),
                                  Utils::String::toUpper(error).c_str()),
            6000);
        mCloseRequested = true;
        return;
    }
    // The button that made the choice mustn't reach the game.
    releaseAllInput();
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
    // No menu music while a game is on screen.
    MusicManager::getInstance().pause();

    Renderer* renderer {Renderer::getInstance()};
    setSize(renderer->getScreenWidth(), renderer->getScreenHeight());
    mKeysHeld.fill(false);
    mKeysLatched.fill(false);

    // Pause menu: a centred panel with a title and five entries.
    // Screen and CRT filter labels are filled in by updateMenuLabels().
    const std::vector<std::string> labels {_("RESUME"),       _("SAVE STATE"), _("LOAD STATE"),
                                           "",                "",              _("RESTART GAME"),
                                           _("QUIT GAME")};
    buildPanel(_("PAUSED"), labels, mMenuTitle, mMenuItems);
    updateMenuLabels();
}

void GuiLibretroPlayer::updateMenuLabels()
{
    const std::string mode {Settings::getInstance()->getString("PlayerScreenMode")};
    mMenuItems[MENU_SCREEN]->setText(
        Utils::String::format(_("SCREEN: %s"), screenModeLabel(mode).c_str()));
    mMenuItems[MENU_CRT_FILTER]->setText(Settings::getInstance()->getBool("PlayerCRTFilter") ?
                                             _("CRT FILTER: ON") :
                                             _("CRT FILTER: OFF"));
}

void GuiLibretroPlayer::createTexture()
{
    // Pixel perfect without the CRT filter wants hard pixel edges; everything else looks better
    // smoothed (the CRT filter does its own sampling).
    const bool sharp {Settings::getInstance()->getString("PlayerScreenMode") == "pixelperfect" &&
                      !Settings::getInstance()->getBool("PlayerCRTFilter")};
    mTexture = TextureResource::get("", false, false, !sharp, false);
    mTextureEmpty = true;
}

void GuiLibretroPlayer::changeScreenOption(int item, int direction)
{
    Settings* settings {Settings::getInstance()};
    if (item == MENU_SCREEN) {
        const std::string mode {settings->getString("PlayerScreenMode")};
        int index {0};
        for (size_t i {0}; i < screenModes.size(); ++i) {
            if (screenModes[i] == mode)
                index = static_cast<int>(i);
        }
        const int count {static_cast<int>(screenModes.size())};
        index = (index + direction + count) % count;
        settings->setString("PlayerScreenMode", screenModes[static_cast<size_t>(index)]);
    }
    else {
        settings->setBool("PlayerCRTFilter", !settings->getBool("PlayerCRTFilter"));
    }
    settings->saveFile();
    createTexture();
    updateMenuLabels();
}

bool GuiLibretroPlayer::isScancodeDown(SDL_Scancode scancode) const
{
    if (scancode <= SDL_SCANCODE_UNKNOWN || scancode >= SDL_NUM_SCANCODES)
        return false;
    return mKeysHeld[scancode] || mKeysLatched[scancode];
}

GuiLibretroPlayer::~GuiLibretroPlayer()
{
    MusicManager::getInstance().resume();

    if (mAudioDevice != 0) {
        SDL_CloseAudioDevice(mAudioDevice);
        mAudioDevice = 0;
    }
    if (mCore && mCore->isLoaded()) {
        mCore->unload();
        LOG(LogInfo) << "GuiLibretroPlayer: closed built-in player";
    }
    if (mShown) {
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

    mStatePath = saveDirectory + "/" + Utils::FileSystem::getStem(romPath) + ".state";

    mCore->keyboardQuery = [this](unsigned int retrok) {
        return isScancodeDown(retrokToScancode(retrok));
    };
    if (!mCore->load(corePath, romPath, systemDirectory, saveDirectory, error))
        return false;

    // Crop black borders automatically for N64 cores.
    const std::string coreFile {Utils::String::toLower(Utils::FileSystem::getFileName(corePath))};
    mAutoCrop = coreFile.find("mupen64plus") != std::string::npos ||
                coreFile.find("parallel_n64") != std::string::npos;

    // A fixed crop, and the border shown while loading, from the core's option file.
    // "256x192" crops to the middle of the frame, "640x200+64+41" to a given position.
    const std::string cropSize {mCore->getOption("games247_crop_size")};
    int cropX {-1};
    int cropY {-1};
    if (std::sscanf(cropSize.c_str(), "%dx%d+%d+%d", &mFixedCropWidth, &mFixedCropHeight, &cropX,
                    &cropY) >= 2) {
        mFixedCropX = cropX;
        mFixedCropY = cropY;
    }
    else {
        mFixedCropWidth = 0;
        mFixedCropHeight = 0;
    }
    mBorderWhileLoading = mFixedCropWidth > 0 && mFixedCropHeight > 0 &&
                          mCore->getOption("games247_border_while_loading") == "enabled";

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

    createTexture();
    mLastTime = SDL_GetPerformanceCounter();
    mTimeAccumulator = 1.0 / mCore->getFPS();
    return true;
}

bool GuiLibretroPlayer::input(InputConfig* config, Input input)
{
    if (mChoosing) {
        choiceInput(config, input);
        return true;
    }
    if (mPaused) {
        menuInput(config, input);
        return true;
    }

    if (config->getDeviceId() == DEVICE_KEYBOARD) {
        if (input.id == SDLK_ESCAPE && input.value != 0) {
            openMenu();
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
        const std::string lowerName {Utils::String::toLower(name)};
        const int button {retroPadButton(lowerName)};
        if (button < 0)
            continue;
        // Cores with analog input get the left stick as a stick, not as the D-pad.
        if (mCore->usesAnalog() && lowerName.rfind("leftanalog", 0) == 0) {
            state &= static_cast<uint16_t>(~(1 << button));
            continue;
        }
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

    // View + Menu (Back + Start) together opens the pause menu.
    constexpr uint16_t menuCombo {(1 << RETRO_DEVICE_ID_JOYPAD_SELECT) |
                                  (1 << RETRO_DEVICE_ID_JOYPAD_START)};
    if ((state & menuCombo) == menuCombo)
        openMenu();

    return true;
}

void GuiLibretroPlayer::releaseAllInput()
{
    mKeysHeld.fill(false);
    mKeysLatched.fill(false);
    for (auto& [device, state] : mControllerState)
        state = 0;
    for (auto& [device, latched] : mControllerLatched)
        latched = 0;
}

void GuiLibretroPlayer::openMenu()
{
    mPaused = true;
    mMenuSelection = 0;
    releaseAllInput();
    if (mAudioDevice != 0) {
        SDL_PauseAudioDevice(mAudioDevice, 1);
        SDL_ClearQueuedAudio(mAudioDevice);
    }
    LOG(LogDebug) << "GuiLibretroPlayer: paused";
}

void GuiLibretroPlayer::closeMenu()
{
    mPaused = false;
    // Buttons used in the menu mustn't reach the game.
    releaseAllInput();
    mLastTime = SDL_GetPerformanceCounter();
    mTimeAccumulator = 1.0 / mCore->getFPS();
    if (mAudioDevice != 0)
        SDL_PauseAudioDevice(mAudioDevice, 0);
    LOG(LogDebug) << "GuiLibretroPlayer: resumed";
}

void GuiLibretroPlayer::menuInput(InputConfig* config, Input input)
{
    if (input.value == 0)
        return;

    const int itemCount {static_cast<int>(mMenuItems.size())};
    if (config->isMappedLike("up", input)) {
        mMenuSelection = (mMenuSelection + itemCount - 1) % itemCount;
    }
    else if (config->isMappedLike("down", input)) {
        mMenuSelection = (mMenuSelection + 1) % itemCount;
    }
    else if ((mMenuSelection == MENU_SCREEN || mMenuSelection == MENU_CRT_FILTER) &&
             (config->isMappedLike("left", input) || config->isMappedLike("right", input))) {
        changeScreenOption(mMenuSelection, config->isMappedLike("left", input) ? -1 : 1);
    }
    else if (config->isMappedTo("a", input)) {
        activateMenuItem();
    }
    else if (config->isMappedTo("b", input) ||
             (config->getDeviceId() == DEVICE_KEYBOARD && input.id == SDLK_ESCAPE)) {
        closeMenu();
    }
}

void GuiLibretroPlayer::activateMenuItem()
{
    Window* window {Window::getInstance()};
    switch (mMenuSelection) {
        case MENU_SCREEN:
        case MENU_CRT_FILTER: {
            // Stay in the menu so the change can be seen behind it.
            changeScreenOption(mMenuSelection, 1);
            return;
        }
        case MENU_SAVE_STATE: {
            window->queueInfoPopup(mCore->saveState(mStatePath) ? _("STATE SAVED") :
                                                                  _("THIS GAME CAN'T SAVE STATES"),
                                   2500);
            break;
        }
        case MENU_LOAD_STATE: {
            if (!Utils::FileSystem::exists(mStatePath))
                window->queueInfoPopup(_("NO SAVED STATE FOR THIS GAME YET"), 2500);
            else if (mCore->loadState(mStatePath))
                window->queueInfoPopup(_("STATE LOADED"), 2500);
            else
                window->queueInfoPopup(_("COULDN'T LOAD THE SAVED STATE"), 2500);
            break;
        }
        case MENU_RESTART: {
            mCore->reset();
            break;
        }
        case MENU_QUIT: {
            mCloseRequested = true;
            return;
        }
        default:
            break;
    }
    closeMenu();
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
    // Analog sticks, read straight from each controller. A port with several controllers
    // uses whichever stick is pushed furthest.
    for (auto& port : mCore->analogState)
        port.fill(0);
    for (const auto& [device, port] : mDevicePorts) {
        if (port >= LibretroCore::MAX_PORTS)
            continue;
        SDL_GameController* controller {SDL_GameControllerFromInstanceID(device)};
        if (controller == nullptr)
            continue;
        const SDL_GameControllerAxis axes[] {SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY,
                                             SDL_CONTROLLER_AXIS_RIGHTX,
                                             SDL_CONTROLLER_AXIS_RIGHTY};
        for (int i {0}; i < 4; ++i) {
            const int16_t value {SDL_GameControllerGetAxis(controller, axes[i])};
            int16_t& current {mCore->analogState[static_cast<size_t>(port)][i]};
            if (std::abs(value) > std::abs(current))
                current = value;
        }
    }

    mCore->padState[0] |=
        keyboardPadState([this](SDL_Scancode scancode) { return isScancodeDown(scancode); });

    // Button layouts from the core's option file (see LibretroCore::applyButtonMap).
    const std::string controllerType {mCore->applyButtonMap()};
    if (!controllerType.empty())
        Window::getInstance()->queueInfoPopup(Utils::String::toUpper(controllerType), 2000);

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
    if (mChoosing || mPaused)
        return;
    runFrames();
}

void GuiLibretroPlayer::render(const glm::mat4& parentTrans)
{
    Renderer* renderer {Renderer::getInstance()};
    const glm::mat4 trans {parentTrans * getTransform()};
    renderer->setMatrix(trans);
    renderer->drawRect(0.0f, 0.0f, mSize.x, mSize.y, 0x000000FF, 0x000000FF);

    if (mChoosing) {
        renderPanel(trans, mChoiceTitle, mChoiceItems, mChoiceSelection);
        return;
    }

    if ((mCore->hasNewFrame() || mTextureEmpty) && !mCore->getFrameBGRA().empty()) {
        mTexture->initFromPixels(mCore->getFrameBGRA().data(), mCore->getFrameWidth(),
                                 mCore->getFrameHeight());
        mCore->clearNewFrame();
        mTextureEmpty = false;
        // Looking at every few frames is enough to follow the game.
        if (mAutoCrop && ++mCropCheckCounter % 6 == 0)
            updateAutoCrop();
        if (mBorderWhileLoading)
            checkBorderBusy();
    }
    if (mCore->getFrameWidth() > 0 && mCore->getFrameHeight() > 0)
        renderGame(trans);
    if (mPaused)
        renderMenu(trans);
}

void GuiLibretroPlayer::updateAutoCrop()
{
    const int width {static_cast<int>(mCore->getFrameWidth())};
    const int height {static_cast<int>(mCore->getFrameHeight())};
    const std::vector<uint8_t>& pixels {mCore->getFrameBGRA()};
    if (width <= 0 || height <= 0 || pixels.size() < static_cast<size_t>(width * height * 4))
        return;

    // A new resolution starts again with nothing drawn.
    if (width != mCropFrameWidth || height != mCropFrameHeight) {
        mCropFrameWidth = width;
        mCropFrameHeight = height;
        mContentLeft = width;
        mContentTop = height;
        mContentRight = 0;
        mContentBottom = 0;
    }

    // Near-black counts as border, as some games' borders aren't quite black.
    const auto isDrawn = [&pixels, width](int x, int y) {
        const uint8_t* pixel {&pixels[(static_cast<size_t>(y) * width + x) * 4]};
        return std::max({pixel[0], pixel[1], pixel[2]}) > 24;
    };

    int top {-1};
    int bottom {-1};
    int left {width};
    int right {0};
    for (int y {0}; y < height; ++y) {
        int first {-1};
        for (int x {0}; x < std::min(left, width); ++x) {
            if (isDrawn(x, y)) {
                first = x;
                break;
            }
        }
        // Only the part to the right of what's already known needs looking at.
        int last {-1};
        for (int x {width - 1}; x >= std::max(right, 0); --x) {
            if (isDrawn(x, y)) {
                last = x;
                break;
            }
        }
        if (first == -1 && last == -1) {
            // The row may still be drawn between left and right.
            bool drawn {false};
            for (int x {left}; x < right && !drawn; ++x)
                drawn = isDrawn(x, y);
            if (!drawn)
                continue;
        }
        if (first != -1)
            left = std::min(left, first);
        if (last != -1)
            right = std::max(right, last + 1);
        if (top == -1)
            top = y;
        bottom = y + 1;
    }
    // A black frame (a fade, a loading screen) says nothing about the border.
    if (top == -1 || right <= left)
        return;

    mContentLeft = std::min(mContentLeft, left);
    mContentTop = std::min(mContentTop, top);
    mContentRight = std::max(mContentRight, right);
    mContentBottom = std::max(mContentBottom, bottom);
}

void GuiLibretroPlayer::checkBorderBusy()
{
    const int width {static_cast<int>(mCore->getFrameWidth())};
    const int height {static_cast<int>(mCore->getFrameHeight())};
    const std::vector<uint8_t>& pixels {mCore->getFrameBGRA()};
    const int borderWidth {mFixedCropX >= 0 ? mFixedCropX : (width - mFixedCropWidth) / 2};
    if (borderWidth < 4 || pixels.size() < static_cast<size_t>(width * height * 4))
        return;

    // Loading stripes run across the whole border, so count the colour changes down the
    // middle of the left border. A still border has none.
    const int x {borderWidth / 2};
    int changes {0};
    const uint8_t* previous {&pixels[static_cast<size_t>(x) * 4]};
    for (int y {1}; y < height; ++y) {
        const uint8_t* pixel {&pixels[(static_cast<size_t>(y) * width + x) * 4]};
        if (pixel[0] != previous[0] || pixel[1] != previous[1] || pixel[2] != previous[2])
            ++changes;
        previous = pixel;
    }
    if (changes >= 8) {
        mBorderBusyTime = SDL_GetTicks();
        mBorderBusySeen = true;
    }
}

void GuiLibretroPlayer::renderGame(const glm::mat4& trans)
{
    Renderer* renderer {Renderer::getInstance()};
    renderer->setMatrix(trans);

    // The part of the frame to show, in texture coordinates.
    float u0 {0.0f};
    float v0 {0.0f};
    float u1 {1.0f};
    float v1 {1.0f};
    float shownHeight {static_cast<float>(mCore->getFrameHeight())};
    float aspect {mCore->getAspectRatio()};
    const int frameWidth {static_cast<int>(mCore->getFrameWidth())};
    const int frameHeight {static_cast<int>(mCore->getFrameHeight())};
    if (mFixedCropWidth > 0 && mFixedCropHeight > 0 && frameWidth > mFixedCropWidth &&
        frameHeight > mFixedCropHeight) {
        // Slide the border in while it's busy, and out two seconds after it's gone still.
        if (mBorderWhileLoading) {
            const bool busy {mBorderBusySeen && SDL_GetTicks() - mBorderBusyTime < 2000};
            const float step {1.0f / 24.0f};
            mBorderAmount = busy ? std::min(1.0f, mBorderAmount + step) :
                                   std::max(0.0f, mBorderAmount - step);
        }
        const float fw {static_cast<float>(frameWidth)};
        const float fh {static_cast<float>(frameHeight)};
        const float cropX {mFixedCropX >= 0 ?
                               static_cast<float>(mFixedCropX) :
                               (fw - static_cast<float>(mFixedCropWidth)) / 2.0f};
        const float cropY {mFixedCropY >= 0 ?
                               static_cast<float>(mFixedCropY) :
                               (fh - static_cast<float>(mFixedCropHeight)) / 2.0f};
        const float cropU0 {cropX / fw};
        const float cropV0 {cropY / fh};
        const float cropU1 {std::min(1.0f, (cropX + static_cast<float>(mFixedCropWidth)) / fw)};
        const float cropV1 {std::min(1.0f, (cropY + static_cast<float>(mFixedCropHeight)) / fh)};
        // Ease in and out.
        const float amount {mBorderAmount * mBorderAmount * (3.0f - 2.0f * mBorderAmount)};
        u0 = cropU0 * (1.0f - amount);
        u1 = cropU1 + (1.0f - cropU1) * amount;
        v0 = cropV0 * (1.0f - amount);
        v1 = cropV1 + (1.0f - cropV1) * amount;
        shownHeight = static_cast<float>(frameHeight) * (v1 - v0);
        aspect *= (u1 - u0) / (v1 - v0);
    }
    if (mAutoCrop && mCropFrameWidth == static_cast<int>(mCore->getFrameWidth()) &&
        mCropFrameHeight == static_cast<int>(mCore->getFrameHeight()) &&
        mContentRight > mContentLeft && mContentBottom > mContentTop) {
        const float frameWidth {static_cast<float>(mCropFrameWidth)};
        const float frameHeight {static_cast<float>(mCropFrameHeight)};
        const float keptWidth {static_cast<float>(mContentRight - mContentLeft)};
        const float keptHeight {static_cast<float>(mContentBottom - mContentTop)};
        // Only crop when the game fills most of the frame. Early on, while only a small logo
        // has been drawn, cropping would zoom right in on it.
        if (keptWidth >= frameWidth * 0.7f) {
            u0 = static_cast<float>(mContentLeft) / frameWidth;
            u1 = static_cast<float>(mContentRight) / frameWidth;
        }
        if (keptHeight >= frameHeight * 0.7f) {
            v0 = static_cast<float>(mContentTop) / frameHeight;
            v1 = static_cast<float>(mContentBottom) / frameHeight;
            shownHeight = keptHeight;
        }
        // Each frame pixel keeps its shape, so the picture's shape follows the crop.
        aspect *= (u1 - u0) / (v1 - v0);
    }

    const std::string mode {Settings::getInstance()->getString("PlayerScreenMode")};
    float width {mSize.x};
    float height {mSize.y};
    if (mode != "stretch") {
        // Fit the picture to the screen, keeping the core's aspect ratio.
        height = mSize.x / aspect;
        if (height > mSize.y) {
            height = mSize.y;
            width = mSize.y * aspect;
        }
    }
    if (mode == "pixelperfect" && shownHeight <= mSize.y) {
        // The largest whole-number multiple of the game's height that fits, so every game
        // pixel is the same size. The width follows the aspect ratio.
        const float frameHeight {shownHeight};
        const float scale {std::max(1.0f, std::floor(mSize.y / frameHeight))};
        height = frameHeight * scale;
        width = std::min(mSize.x, std::round(height * aspect));
    }
    const float x {std::round((mSize.x - width) / 2.0f)};
    const float y {std::round((mSize.y - height) / 2.0f)};
    width = std::round(width);
    height = std::round(height);

    Renderer::Vertex vertices[4];
    // clang-format off
    vertices[0] = {{x,         y         }, {u0, v0}, 0xFFFFFFFF};
    vertices[1] = {{x,         y + height}, {u0, v1}, 0xFFFFFFFF};
    vertices[2] = {{x + width, y         }, {u1, v0}, 0xFFFFFFFF};
    vertices[3] = {{x + width, y + height}, {u1, v1}, 0xFFFFFFFF};
    // clang-format on
    if (Settings::getInstance()->getBool("PlayerCRTFilter"))
        vertices->shaders = Renderer::Shader::CRT;

    mTexture->bind(0);
    renderer->drawTriangleStrips(&vertices[0], 4, Renderer::BlendFactor::SRC_ALPHA,
                                 Renderer::BlendFactor::ONE_MINUS_SRC_ALPHA);
}

void GuiLibretroPlayer::renderMenu(const glm::mat4& trans)
{
    Renderer* renderer {Renderer::getInstance()};
    renderer->setMatrix(trans);
    // Dim the paused game behind the menu.
    renderer->drawRect(0.0f, 0.0f, mSize.x, mSize.y, 0x000000A0, 0x000000A0);
    renderPanel(trans, mMenuTitle, mMenuItems, mMenuSelection);
}

void GuiLibretroPlayer::renderPanel(const glm::mat4& trans,
                                    const std::shared_ptr<TextComponent>& title,
                                    const std::vector<std::shared_ptr<TextComponent>>& items,
                                    int selection)
{
    Renderer* renderer {Renderer::getInstance()};
    renderer->setMatrix(trans);
    const float rowHeight {items.front()->getSize().y};
    const float panelTop {title->getPosition().y - rowHeight * 0.25f};
    const float panelHeight {rowHeight * static_cast<float>(items.size() + 2)};
    BackgroundComponent::renderNeonPanel(renderer, trans, mSize.x * 0.3f, panelTop, mSize.x * 0.4f,
                                         panelHeight, 20.0f * renderer->getScreenResolutionModifier(),
                                         mMenuColorFrame, 1.0f);

    const auto& selected = items[static_cast<size_t>(selection)];
    renderer->drawRect(mSize.x * 0.32f, selected->getPosition().y, mSize.x * 0.36f, rowHeight,
                       mMenuColorSelector, mMenuColorSelectorEnd, true);
    renderer->drawRect(mSize.x * 0.32f, selected->getPosition().y,
                       4.0f * renderer->getScreenResolutionModifier(), rowHeight,
                       mMenuColorNeonEdgeStart, mMenuColorNeonEdgeStart);

    title->render(trans);
    for (size_t i {0}; i < items.size(); ++i) {
        items[i]->setColor(static_cast<int>(i) == selection ? 0xFFFFFFFF : mMenuColorPrimary);
        items[i]->render(trans);
    }
}
