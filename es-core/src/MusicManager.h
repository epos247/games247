//  SPDX-License-Identifier: MIT
//
//  games247
//  MusicManager.h
//
//  Background music for the menus. Tracks in resources/music and in <games247 folder>/music
//  play in random order while the system and game list views are shown. Playback pauses while
//  a game runs. Decoding happens on a worker thread using FFmpeg, and the decoded audio is
//  mixed in by AudioManager.
//

#ifndef ES_CORE_MUSIC_MANAGER_H
#define ES_CORE_MUSIC_MANAGER_H

#include <atomic>
#include <random>
#include <string>
#include <thread>
#include <vector>

struct AVFormatContext;
struct AVCodecContext;
struct SwrContext;
struct AVPacket;
struct AVFrame;

class MusicManager
{
public:
    static MusicManager& getInstance();

    // Finds the tracks and starts the worker thread. Does nothing if there are no tracks.
    void start();
    // Stops the worker thread and closes the current track.
    void stop();

    // Called every frame from the main loop. Music plays while the setting is on, no game is
    // running and nothing has paused it.
    void update(bool gameRunning);

    // Pause while a game runs (the built-in player or an emulator). Calls can nest.
    void pause();
    void resume();

private:
    MusicManager() = default;
    ~MusicManager();

    void run();
    bool openNextTrack();
    void closeTrack();
    // Decodes a little audio and hands it to AudioManager. Returns false at the end of the track.
    bool decodeSome();

    std::vector<std::string> mTracks;
    std::vector<size_t> mOrder;
    size_t mOrderPos {0};
    size_t mLastTrack {static_cast<size_t>(-1)};
    std::mt19937 mRandom {std::random_device {}()};

    std::thread mThread;
    std::atomic<bool> mRunning {false};
    std::atomic<bool> mShouldPlay {false};
    std::atomic<int> mPauseCount {0};

    AVFormatContext* mFormat {nullptr};
    AVCodecContext* mCodec {nullptr};
    SwrContext* mResampler {nullptr};
    AVPacket* mPacket {nullptr};
    AVFrame* mFrame {nullptr};
    int mStreamIndex {-1};
};

#endif // ES_CORE_MUSIC_MANAGER_H
