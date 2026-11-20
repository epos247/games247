//  SPDX-License-Identifier: MIT
//
//  games247
//  LibretroCore.h
//
//  Loads a libretro core (dynamic library) and runs content in-process.
//  This is the games247 built-in player: the same cores RetroArch uses, without RetroArch.
//  Software-rendered cores, OpenGL GPU cores (for example Mupen64Plus-Next for the N64) and
//  Vulkan GPU cores (for example Xenia Edge for the Xbox 360, see LibretroVulkan.h) are
//  supported. Cores that need Direct3D, or Vulkan on a computer without it, are rejected so the
//  caller can fall back to launching RetroArch.
//

#ifndef GAMES247_LIBRETRO_CORE_H
#define GAMES247_LIBRETRO_CORE_H

#include "libretro/libretro.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class LibretroVulkan;
struct retro_hw_render_context_negotiation_interface_vulkan;

class LibretroCore
{
public:
    // MAME ROM sets (parent games, BIOS sets, devices) the core said it couldn't find files in
    // during the last load, other than the game's own set.
    const std::vector<std::string>& getMissingSets() const { return mMissingSets; }
    static constexpr int MAX_PORTS {4};

    LibretroCore();
    ~LibretroCore();

    // Loads the core and the content. On failure, returns false and fills error.
    bool load(const std::string& corePath,
              const std::string& contentPath,
              const std::string& systemDirectory,
              const std::string& saveDirectory,
              std::string& error);
    void unload();
    bool isLoaded() const { return mGameLoaded; }

    // Runs one emulated frame.
    void runFrame();

    // Restarts the game, like pressing the console's reset button.
    void reset();

    // games247: replaces the game save (save RAM) in the core's memory with a file's
    // contents. Call it before the first frame. Used for cloud saves.
    bool replaceSaveRAM(const std::string& path);
    // Where the game save is written, empty if the game has none.
    const std::string& getSaveRAMPath() const { return mSaveRAMPath; }

    // Save states (a snapshot of the whole emulated machine). Return false on failure.
    bool saveState(const std::string& path);
    bool loadState(const std::string& path);

    // Video output from the last frame, converted to BGRA8888 (blue first), which is what
    // games247 textures expect despite TextureResource::initFromPixels() naming it "RGBA".
    const std::vector<uint8_t>& getFrameBGRA() const { return mFrameBGRA; }
    unsigned int getFrameWidth() const { return mFrameWidth; }
    unsigned int getFrameHeight() const { return mFrameHeight; }
    bool hasNewFrame() const { return mNewFrame; }
    void clearNewFrame() { mNewFrame = false; }
    float getAspectRatio() const;

    double getFPS() const { return mAVInfo.timing.fps; }
    double getSampleRate() const { return mAVInfo.timing.sample_rate; }

    // Interleaved stereo int16 samples produced since the last call to clearAudio().
    const std::vector<int16_t>& getAudio() const { return mAudio; }
    void clearAudio() { mAudio.clear(); }

    // Input state, set by the player before each frame.
    // Bitmask of RETRO_DEVICE_ID_JOYPAD_* per port.
    std::array<uint16_t, MAX_PORTS> padState {};
    // Analog sticks per port: left X, left Y, right X, right Y, each -32768 to 32767.
    std::array<std::array<int16_t, 4>, MAX_PORTS> analogState {};
    // True once the core has read an analog stick, so the player stops also treating the left
    // stick as the D-pad.
    bool usesAnalog() const { return mUsesAnalog; }
    // Returns true if the given RETROK_* key is held down.
    std::function<bool(unsigned int retrok)> keyboardQuery;

    // Forwards a key press or release to cores that registered a keyboard callback.
    void keyboardEvent(bool down, unsigned int retrok, uint32_t character, uint16_t modifiers);

    const std::string& getCoreName() const { return mCoreName; }

    // games247: button layouts from the option files, for cores that can't remap the
    // controller themselves:
    //   games247_button_<button> = "<button>"   the button acts as another RetroPad button
    //                                           (a, b, x, y, l, r, l2, r2, l3, r3, start,
    //                                           select, up, down, left, right), "none", or
    //                                           "next_controller"
    //   games247_controller_types = "A|B|C"     controller types (as the core names them) for
    //                                           port 1: the first is used at the start, and
    //                                           "next_controller" goes through them in turn
    //   games247_cycle_option = "<core option>" for cores that switch control modes with a
    //   games247_cycle_values = "a|b|c"         core option instead: "next_controller" sets
    //   games247_cycle_labels = "A|B|C"         the option to the next value, showing its label
    // Call once per frame after setting padState. Returns the new controller type's name when
    // "next_controller" was pressed, otherwise an empty string.
    std::string applyButtonMap();
    // What to hand the core instead of the game file, such as a MAME command line
    // ("apple2e -flop1 \"game.dsk\""), like RetroArch's content argument. Only for cores that
    // load from a path. Call before load().
    void setContentArgument(const std::string& argument) { mContentArgument = argument; }
    // An option value for this game only, winning over the option files. Call before load().
    void setOptionOverride(const std::string& key, const std::string& value)
    {
        mOverrides[key] = value;
    }
    // The option values in a core's option files (games247's own, then the user's), without
    // loading the core.
    static std::map<std::string, std::string> readOptionFiles(const std::string& corePath);
    // A value from the option files (or the core's default), empty if there's none.
    std::string getOption(const std::string& key) const
    {
        const auto it = mOptions.find(key);
        return it == mOptions.end() ? "" : it->second;
    }

private:
    // C callbacks given to the core. libretro has no user-data pointer, so they go
    // through the single active instance.
    static bool environmentCallback(unsigned int cmd, void* data);
    static void videoRefreshCallback(const void* data, unsigned int width, unsigned int height,
                                     size_t pitch);
    static void audioSampleCallback(int16_t left, int16_t right);
    static size_t audioSampleBatchCallback(const int16_t* data, size_t frames);
    static void inputPollCallback();
    static int16_t inputStateCallback(unsigned int port,
                                      unsigned int device,
                                      unsigned int index,
                                      unsigned int id);
    static void logCallback(enum retro_log_level level, const char* fmt, ...);

    bool environment(unsigned int cmd, void* data);

    // GPU-rendered (OpenGL) cores draw into mHWFramebuffer on games247's own OpenGL context.
    // After each frame the picture is copied back into mFrameBGRA, so the rest of the player
    // treats it like a software core.
    static uintptr_t hwGetCurrentFramebuffer();
    static retro_proc_address_t hwGetProcAddress(const char* symbol);
    bool acceptHardwareRender(retro_hw_render_callback& request);
    // Creates the core's own OpenGL context, of the kind it asked for.
    bool createHWContext(const retro_hw_render_callback& request, std::string& error);
    bool createHWFramebuffer(std::string& error);
    void destroyHWFramebuffer();
    void readHWFrame(unsigned int width, unsigned int height);
    void storeOptionDefault(const char* key,
                            const char* value,
                            const char* firstValue = nullptr);
    // Sets mLoadPath, unpacking mContentPath first if the core can't open the archive.
    bool prepareContent(std::string& error);
    // Reads core option values from a RetroArch-style .opt file (lines like
    // key = "value") into options, replacing values already there.
    static void readOptionFile(const std::string& path,
                               std::map<std::string, std::string>& options);
    void loadSaveRAM();
    void writeSaveRAM();

    static LibretroCore* sActive;

    void* mHandle;
    bool mInitialized;
    bool mGameLoaded;

    // Core entry points.
    void (*mRetroInit)();
    void (*mRetroDeinit)();
    unsigned int (*mRetroApiVersion)();
    void (*mRetroGetSystemInfo)(retro_system_info*);
    void (*mRetroGetSystemAVInfo)(retro_system_av_info*);
    void (*mRetroSetEnvironment)(retro_environment_t);
    void (*mRetroSetVideoRefresh)(retro_video_refresh_t);
    void (*mRetroSetAudioSample)(retro_audio_sample_t);
    void (*mRetroSetAudioSampleBatch)(retro_audio_sample_batch_t);
    void (*mRetroSetInputPoll)(retro_input_poll_t);
    void (*mRetroSetInputState)(retro_input_state_t);
    void (*mRetroSetControllerPortDevice)(unsigned int, unsigned int);
    void (*mRetroRun)();
    void (*mRetroReset)();
    size_t (*mRetroSerializeSize)();
    bool (*mRetroSerialize)(void*, size_t);
    bool (*mRetroUnserialize)(const void*, size_t);
    bool (*mRetroLoadGame)(const retro_game_info*);
    void (*mRetroUnloadGame)();
    void* (*mRetroGetMemoryData)(unsigned int);
    size_t (*mRetroGetMemorySize)(unsigned int);

    retro_system_info mSystemInfo;
    retro_system_av_info mAVInfo;
    retro_pixel_format mPixelFormat;
    retro_keyboard_event_t mKeyboardCallback;
    bool mHardwareRenderRequested;
    // The last message the core showed, which can say why it didn't load a game.
    std::string mLastMessage;
    bool mUsesAnalog {false};
    bool mHardwareRender;
    retro_hw_render_callback mHWRender;
    unsigned int mHWFramebuffer;
    unsigned int mHWColorTexture;
    unsigned int mHWDepthBuffer;
    unsigned int mHWWidth;
    unsigned int mHWHeight;
    // The core's own OpenGL context (an SDL_GLContext).
    void* mHWContext;
    // Vulkan cores: the device they draw on, and their context negotiation interface.
    bool mVulkanRender {false};
    std::unique_ptr<LibretroVulkan> mVulkan;
    const retro_hw_render_context_negotiation_interface_vulkan* mVulkanNegotiation {nullptr};

    std::string mCoreName;
    std::string mContentPath;
    // The file handed to the core: mContentPath, or a file unpacked from it when it's an
    // archive the core can't open itself.
    std::string mLoadPath;
    std::vector<std::string> mMissingSets;
    std::string mContentArgument;
    std::string mExtractDirectory;
    std::string mSystemDirectory;
    std::string mSaveDirectory;
    std::string mSaveRAMPath;
    std::vector<uint8_t> mContentData;
    std::map<std::string, std::string> mOptions;
    std::map<std::string, std::string> mOverrides;

    // Controller types each port offers (SET_CONTROLLER_INFO): name and device id.
    std::array<std::vector<std::pair<std::string, unsigned int>>, MAX_PORTS> mControllerTypes;
    // The types games247_controller_types picks for port 1, and which one is in use.
    std::vector<std::pair<std::string, unsigned int>> mCycleTypes;
    size_t mCycleIndex {0};
    // RetroPad button -> button it acts as (0-15), BUTTON_NONE or BUTTON_NEXT_CONTROLLER.
    static constexpr int BUTTON_NONE {-1};
    static constexpr int BUTTON_NEXT_CONTROLLER {100};
    std::array<int, 16> mButtonMap {};
    bool mHasButtonMap {false};
    bool mNextControllerHeld {false};
    // games247_cycle_option: the option, its values and labels, and which value is in use.
    std::string mCycleOption;
    std::vector<std::string> mCycleValues;
    std::vector<std::string> mCycleLabels;
    size_t mCycleValueIndex {0};
    // Set when an option has changed, until the core asks (GET_VARIABLE_UPDATE).
    bool mVariablesChanged {false};
    void setupButtonMap();

    std::vector<uint8_t> mFrameBGRA;
    unsigned int mFrameWidth;
    unsigned int mFrameHeight;
    bool mNewFrame;
    std::vector<int16_t> mAudio;
};

#endif // GAMES247_LIBRETRO_CORE_H
