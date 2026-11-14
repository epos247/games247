//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  AudioManager.h
//
//  Low-level audio functions (using SDL2).
//

#ifndef ES_CORE_AUDIO_MANAGER_H
#define ES_CORE_AUDIO_MANAGER_H

#include <SDL2/SDL_audio.h>
#include <atomic>
#include <memory>
#include <vector>

class Sound;

class AudioManager
{
public:
    virtual ~AudioManager();
    static AudioManager& getInstance();

    void init();
    void deinit();

    void registerSound(std::shared_ptr<Sound> sound);
    void unregisterSound(std::shared_ptr<Sound> sound);

    void play();
    void stop();

    // Used for streaming audio from videos.
    void setupAudioStream(int sampleRate);
    void processStream(const void* samples, unsigned count);
    void clearStream();

    void muteStream() { sMuteStream = true; }
    void unmuteStream() { sMuteStream = false; }

    bool getHasAudioDevice() { return sHasAudioDevice; }

    // games247: background music from MusicManager, as 32-bit float stereo at 44.1 kHz.
    void processMusic(const void* samples, int bytes);
    // Seconds of music waiting to be played.
    float getMusicBuffered();
    // Stops the music straight away.
    void clearMusic();
    // Fades the music out over the given time, then silences it until resumeMusic().
    void fadeOutMusic(int milliseconds);
    bool isMusicFading() { return sMusicFadeRemaining > 0; }
    void resumeMusic();

    static inline SDL_AudioDeviceID sAudioDevice {0};
    static inline SDL_AudioSpec sAudioFormat;

private:
    AudioManager() noexcept;

    static void mixAudio(void* unused, Uint8* stream, int len);

    static inline SDL_AudioStream* sConversionStream {nullptr};
    static inline SDL_AudioStream* sMusicStream {nullptr};
    // Music fade-out, counted in output sample frames. Only changed with the device locked.
    static inline std::atomic<int> sMusicFadeRemaining {0};
    static inline int sMusicFadeTotal {0};
    static inline bool sMusicSilenced {false};
    static inline std::vector<std::shared_ptr<Sound>> sSoundVector;
    static inline std::atomic<bool> sMuteStream {false};
    static inline bool sHasAudioDevice {true};
};

#endif // ES_CORE_AUDIO_MANAGER_H
