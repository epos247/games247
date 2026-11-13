//  SPDX-License-Identifier: MIT
//
//  games247
//  GuiLibretroPlayer.h
//
//  Full-screen built-in game player. Runs a libretro core inside games247 and handles
//  video, audio, input and frame pacing.
//
//  Exit: Escape on the keyboard, or hold Back (Select) + Start on a controller.
//

#ifndef GAMES247_GUI_LIBRETRO_PLAYER_H
#define GAMES247_GUI_LIBRETRO_PLAYER_H

#include "GuiComponent.h"
#include "libretro/LibretroCore.h"
#include "resources/TextureResource.h"

#include <SDL2/SDL.h>

#include <map>
#include <memory>

class FileData;

class GuiLibretroPlayer : public GuiComponent
{
public:
    // If the expanded launch command runs RetroArch with "-L <core>", returns the core's path.
    // Otherwise returns an empty string.
    static std::string findRetroArchCore(const std::string& command);

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

    std::unique_ptr<LibretroCore> mCore;
    std::shared_ptr<TextureResource> mTexture;
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
    bool mInputUnblocked {false};
    Uint64 mStatsStart {0};
    int mStatsFrames {0};
};

#endif // GAMES247_GUI_LIBRETRO_PLAYER_H
