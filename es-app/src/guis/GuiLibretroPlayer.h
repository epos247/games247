//  SPDX-License-Identifier: MIT
//
//  games247
//  GuiLibretroPlayer.h
//
//  Full-screen built-in game player. Runs a libretro core inside games247 and handles
//  video, audio, input and frame pacing.
//
//  Pause menu: Escape on the keyboard, or press View + Menu (Back + Start) together on a
//  controller. The menu has Resume, Save state, Load state, Screen (fit, stretch or pixel
//  perfect), CRT filter, Restart game and Quit game. Screen and CRT filter are remembered.
//
//  Load choice: a core's option file can ask for a choice before some games start, such as
//  the tape loading speed for ZX Spectrum and Amstrad CPC tapes:
//    games247_load_choice_option = "<core option>"     the option the choice sets
//    games247_load_choice_values = "a|b"               its value for each choice
//    games247_load_choice_labels = "A|B"               the choices, the first one selected
//    games247_load_choice_extensions = "tap|tzx"       the games it's for (also inside a zip)
//    games247_load_choice_title = "Title"              the heading above the choices
//  A second choice, asked first, can be set up the same way with games247_machine_choice_*
//  lines, such as the ZX Spectrum model. A choice without _extensions is asked for every game.
//  The core is only loaded once the choices have been made, so the options are in place from
//  the start.
//
//  System ROMs: before an Amiga or Atari ST game starts, the Kickstart ROM for its model (see
//  AmigaKickstart.h) or the TOS ROM (see AtariTos.h) is looked for in the bios folder. If it's
//  missing and the FTP storage has a bios folder with such files, the player offers to
//  download them. Otherwise an Amiga game starts with AROS, the free Kickstart replacement
//  built into the core, and a popup says which file to add; an Atari ST game doesn't start, as
//  Hatari has no TOS of its own.
//

#ifndef GAMES247_GUI_LIBRETRO_PLAYER_H
#define GAMES247_GUI_LIBRETRO_PLAYER_H

#include "GuiComponent.h"
#include "CloudSaves.h"
#include "HttpReq.h"
#include "components/TextComponent.h"
#include "libretro/LibretroCore.h"
#include "resources/TextureResource.h"

#include <SDL2/SDL.h>

#include <atomic>
#include <map>
#include <memory>
#include <thread>

class FileData;

class GuiLibretroPlayer : public GuiComponent
{
public:
    // Loads the core and game and opens the player. Returns false (with error set) if the
    // core can't run in the built-in player, so the caller can launch RetroArch instead. A game
    // that can't start for want of a system ROM opens a player that just says so and closes.
    static bool launch(const std::string& corePath,
                       const std::string& romPath,
                       FileData* game,
                       std::string& error);

    ~GuiLibretroPlayer();

    bool input(InputConfig* config, Input input) override;
    void update(int deltaTime) override;
    void render(const glm::mat4& parentTrans) override;
    std::vector<HelpPrompt> getHelpPrompts() override { return {}; }

private:
    GuiLibretroPlayer();

    bool start(const std::string& corePath, const std::string& romPath, std::string& error);
    void runFrames();
    void updateInput();
    void queueAudio();

    // Pause menu.
    void openMenu();
    void closeMenu();
    void menuInput(InputConfig* config, Input input);
    void activateMenuItem();
    void releaseAllInput();
    void renderGame(const glm::mat4& trans);
    void updateMenuLabels();
    void changeScreenOption(int item, int direction);
    void createTexture();
    void renderMenu(const glm::mat4& trans);
    // Draws a menu panel: the title and entries, with the selected entry highlighted.
    void renderPanel(const glm::mat4& trans,
                     const std::shared_ptr<TextComponent>& title,
                     const std::vector<std::shared_ptr<TextComponent>>& items,
                     int selection);
    // Makes the title and entries of a menu panel, centred on the screen.
    void buildPanel(const std::string& title,
                    const std::vector<std::string>& labels,
                    std::shared_ptr<TextComponent>& titleComponent,
                    std::vector<std::shared_ptr<TextComponent>>& items);

    // The folder the cores look in for BIOS files.
    static std::string biosDirectory();

    // Steps before the core is loaded: the load choice and the system ROM (see the top of this
    // file). Each step either shows a panel and returns true, or is done; if the game can't
    // start, mPreStartError says why.
    enum class PreStart {
        NONE,
        LOAD_CHOICE,
        FTP_CHECK,
        ROM_OFFER,
        DOWNLOADING,
        HDD_DOWNLOAD,
        CLOUD_CHECK,
        CLOUD_CHOICE,
        CLOUD_DOWNLOAD
    };
    bool nextPreStartStep();
    // Runs the steps still to do, then starts the game if none of them shows a panel.
    void continuePreStart();
    void startPending();
    void choiceInput(InputConfig* config, Input input);
    void showPanel(PreStart step,
                   const std::string& title,
                   const std::vector<std::string>& labels);
    // The system ROM check, and its download from FTP storage.
    enum class SystemRom { NONE, AMIGA_KICKSTART, ATARI_TOS, XBOX_BIOS, PSX_BIOS, PS2_BIOS, SATURN_BIOS };
    std::string systemRomName() const;
    // Copies ROMs found under other names, then returns the files still missing.
    std::vector<std::string> missingSystemRoms() const;
    // What happens without the ROM: AROS for the Amiga, mPreStartError for the Atari ST, and
    // for the Xbox the blank hard disk image is downloaded if that's all that's missing.
    // Returns true if that shows a panel.
    bool noSystemRom(const std::vector<std::string>& missing);
    // Lists the FTP storage's bios folder (live, not the saved listing) for missing ROMs.
    void startFtpCheck(const std::vector<std::string>& missing);
    void finishFtpCheck();
    void startRomDownload();
    void finishRomDownload();
    PreStart mPreStart {PreStart::NONE};
    std::string mPreStartError;
    // The choices still to ask, in order (see the top of this file).
    struct LoadChoice {
        std::string option;
        std::vector<std::string> values;
        std::vector<std::string> labels;
        std::string title;
    };
    std::vector<LoadChoice> mLoadChoices;
    SystemRom mSystemRom {SystemRom::NONE};
    bool mSystemRomPending {false};
    int mChoiceSelection {0};
    // The load choice being asked.
    LoadChoice mLoadChoice;
    std::shared_ptr<TextComponent> mChoiceTitle;
    std::vector<std::shared_ptr<TextComponent>> mChoiceItems;
    std::string mPendingCorePath;
    std::string mPendingRomPath;
    std::string mAmigaModel;
    // From the FTP check: the missing files, every file in the server's bios folder (with
    // its folder inside it, if any), and that folder's name on the server.
    std::vector<std::string> mMissingRoms;
    std::vector<std::string> mFtpFiles;
    std::string mFtpBiosFolder;
    // Paths inside the server's bios folder.
    std::vector<std::string> mRomDownloads;
    std::thread mDownloadThread;
    std::atomic<bool> mDownloadCancel {false};
    std::atomic<bool> mDownloadDone {false};
    std::atomic<int> mDownloadIndex {0};
    int mShownDownloadIndex {-1};
    std::string mDownloadError;
    // The Xbox's blank hard disk image, downloaded from xemu's GitHub releases.
    std::unique_ptr<HttpReq> mHddRequest;
    void finishHddDownload();

    // Cloud saves (see CloudSaves.h). Checked once the game is loaded, before its first frame.
    void beginCloudSaves();
    void finishCloudStep();
    void resumeFromCloudState();
    // Starts (or continues) running the game after a cloud save step.
    void runGame();
    std::string mRomPath;

    // Handhelds get an LCD filter instead of the CRT one (see lcd.glsl).
    void setUpScreenType();
    bool filterOn() const;
    void addGhosting(const std::vector<uint8_t>& frame, unsigned int width, unsigned int height);
    bool mHandheld {false};
    // Renderer::ShaderFlags LCD_* colour profile for the console's screen.
    unsigned int mLcdFlags {0};
    // Older LCDs were slow to change, so moving things left a faint trail. Some games rely
    // on it, flickering sprites on alternate frames to make them look see-through.
    bool mLcdGhosting {false};
    std::vector<uint8_t> mGhostFrame;
    unsigned int mGhostWidth {0};
    unsigned int mGhostHeight {0};

    std::string mCloudSystem;
    std::thread mCloudThread;
    std::atomic<bool> mCloudCancel {false};
    std::atomic<bool> mCloudDone {false};
    bool mCloudOk {false};
    std::string mCloudError;
    CloudSaves::CheckResult mCloudResult;
    // Save and upload when the game is quit: only after a successful check, so an
    // unreachable server can't lead to an older save replacing a newer one there.
    bool mCloudUpload {false};
    // The game has run at least one frame.
    bool mGameRan {false};
    // True once the player is on screen (it can be deleted before that if the game can't
    // start).
    bool mShown {false};
    // Finds the part of the latest frame that isn't black border, see mAutoCrop.
    void updateAutoCrop();

    // games247: N64 and Saturn games often draw inside a black border that differs from game
    // to game.
    // When this is on, edges that have stayed black since the game started are cropped off.
    // The area kept only ever grows, so the picture never cuts into anything the game has
    // drawn.
    bool mAutoCrop {false};
    // False for cores whose save states hang (xemu on OpenGL waits 30 seconds, then fails).
    bool mCanSaveStates {true};
    int mCropFrameWidth {0};
    int mCropFrameHeight {0};
    // What the game has drawn so far: left/top inclusive, right/bottom exclusive.
    int mContentLeft {0};
    int mContentTop {0};
    int mContentRight {0};
    int mContentBottom {0};
    int mCropCheckCounter {0};

    // games247: SNES games like Street Fighter II draw black bars above and below the picture
    // during play, but not on their title and menu screens. When this is on, black rows at the
    // top and the bottom (each cropped on its own, up to a quarter of the height) are cropped
    // once they've been there for a second, and the crop eases back out as soon as the game
    // draws into them. All-dark frames (fades) are ignored.
    bool mLetterboxCrop {false};
    void updateLetterboxCrop();
    // Rows cropped off the top and the bottom: aimed for, and shown (easing towards it).
    int mLetterboxTop {0};
    int mLetterboxBottom {0};
    float mLetterboxTopShown {0.0f};
    float mLetterboxBottomShown {0.0f};
    // A larger crop seen, and in how many checks in a row.
    int mLetterboxTopCandidate {0};
    int mLetterboxBottomCandidate {0};
    int mLetterboxSeen {0};
    int mLetterboxFrameHeight {0};
    // When the bars measured were last logged.
    unsigned int mLetterboxLogTime {0};

    // games247: a fixed crop to the middle of the frame (games247_crop_size = "256x192" in
    // the core's option file), for computers whose border is the same in every game. With
    // games247_border_while_loading = "enabled", the border slides back in while it's busy
    // (the coloured stripes of a tape loading) and out again once it has been still for a
    // couple of seconds.
    int mFixedCropWidth {0};
    int mFixedCropHeight {0};
    // Where the crop starts; -1 centres it ("WxH+X+Y" sets these).
    int mFixedCropX {-1};
    int mFixedCropY {-1};
    bool mBorderWhileLoading {false};
    // 0: cropped, 1: the whole frame.
    float mBorderAmount {0.0f};
    Uint32 mBorderBusyTime {0};
    bool mBorderBusySeen {false};
    void checkBorderBusy();

    std::unique_ptr<LibretroCore> mCore;
    std::shared_ptr<TextureResource> mTexture;
    bool mTextureEmpty {true};
    FileData* mGame;

    SDL_AudioDeviceID mAudioDevice;
    size_t mAudioBytesPerFrame;

    Uint64 mLastTime;
    double mTimeAccumulator;

    bool isScancodeDown(SDL_Scancode scancode) const;

    // Controller buttons held, per input device, as RETRO_DEVICE_ID_JOYPAD_* bitmasks.
    // "Latched" holds presses not yet seen by the core, so quick taps aren't lost.
    std::map<int, uint16_t> mControllerState;
    std::map<int, uint16_t> mControllerLatched;
    std::map<int, int> mDevicePorts;

    // Keyboard state from key events, indexed by SDL scancode.
    std::array<bool, SDL_NUM_SCANCODES> mKeysHeld;
    std::array<bool, SDL_NUM_SCANCODES> mKeysLatched;
    std::vector<bool> mPreviousKeys;

    bool mCloseRequested;

    bool mPaused {false};
    int mMenuSelection {0};
    std::vector<std::shared_ptr<TextComponent>> mMenuItems;
    std::shared_ptr<TextComponent> mMenuTitle;
    std::string mStatePath;
    bool mInputUnblocked {false};
    Uint64 mStatsStart {0};
    int mStatsFrames {0};
};

#endif // GAMES247_GUI_LIBRETRO_PLAYER_H
