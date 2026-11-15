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

#ifndef GAMES247_GUI_LIBRETRO_PLAYER_H
#define GAMES247_GUI_LIBRETRO_PLAYER_H

#include "GuiComponent.h"
#include "components/TextComponent.h"
#include "libretro/LibretroCore.h"
#include "resources/TextureResource.h"

#include <SDL2/SDL.h>

#include <map>
#include <memory>

class FileData;

class GuiLibretroPlayer : public GuiComponent
{
public:
    // Loads the core and game and opens the player. Returns false (with error set) if the
    // core can't run in the built-in player, so the caller can launch RetroArch instead.
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
    // Finds the part of the latest frame that isn't black border, see mAutoCrop.
    void updateAutoCrop();

    // games247: N64 games often draw inside a black border that differs from game to game.
    // When this is on, edges that have stayed black since the game started are cropped off.
    // The area kept only ever grows, so the picture never cuts into anything the game has
    // drawn.
    bool mAutoCrop {false};
    int mCropFrameWidth {0};
    int mCropFrameHeight {0};
    // What the game has drawn so far: left/top inclusive, right/bottom exclusive.
    int mContentLeft {0};
    int mContentTop {0};
    int mContentRight {0};
    int mContentBottom {0};
    int mCropCheckCounter {0};

    // games247: a fixed crop to the middle of the frame (games247_crop_size = "256x192" in
    // the core's option file), for computers whose border is the same in every game. With
    // games247_border_while_loading = "enabled", the border slides back in while it's busy
    // (the coloured stripes of a tape loading) and out again once it has been still for a
    // couple of seconds.
    int mFixedCropWidth {0};
    int mFixedCropHeight {0};
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
