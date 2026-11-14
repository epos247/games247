//  SPDX-License-Identifier: MIT
//
//  games247
//  LibretroCore.h
//
//  Loads a libretro core (dynamic library) and runs content in-process.
//  This is the games247 built-in player: the same cores RetroArch uses, without RetroArch.
//  Only software-rendered cores are supported for now. Cores that ask for OpenGL/Vulkan
//  hardware rendering are rejected so the caller can fall back to launching RetroArch.
//

#ifndef GAMES247_LIBRETRO_CORE_H
#define GAMES247_LIBRETRO_CORE_H

#include "libretro/libretro.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

class LibretroCore
{
public:
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
    // Returns true if the given RETROK_* key is held down.
    std::function<bool(unsigned int retrok)> keyboardQuery;

    // Forwards a key press or release to cores that registered a keyboard callback.
    void keyboardEvent(bool down, unsigned int retrok, uint32_t character, uint16_t modifiers);

    const std::string& getCoreName() const { return mCoreName; }

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
    void storeOptionDefault(const char* key, const char* value);
    // Sets mLoadPath, unpacking mContentPath first if the core can't open the archive.
    bool prepareContent(std::string& error);
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

    std::string mCoreName;
    std::string mContentPath;
    // The file handed to the core: mContentPath, or a file unpacked from it when it's an
    // archive the core can't open itself.
    std::string mLoadPath;
    std::string mExtractDirectory;
    std::string mSystemDirectory;
    std::string mSaveDirectory;
    std::string mSaveRAMPath;
    std::vector<uint8_t> mContentData;
    std::map<std::string, std::string> mOptions;

    std::vector<uint8_t> mFrameBGRA;
    unsigned int mFrameWidth;
    unsigned int mFrameHeight;
    bool mNewFrame;
    std::vector<int16_t> mAudio;
};

#endif // GAMES247_LIBRETRO_CORE_H
