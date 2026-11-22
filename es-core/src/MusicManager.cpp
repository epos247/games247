//  SPDX-License-Identifier: MIT
//
//  games247
//  MusicManager.cpp
//
//  Background music for the menus. Tracks in resources/music and in <games247 folder>/music
//  play in random order while the system and game list views are shown. Playback pauses while
//  a game runs. Decoding happens on a worker thread using FFmpeg, and the decoded audio is
//  mixed in by AudioManager.
//

#include "MusicManager.h"

#include "AudioManager.h"
#include "Log.h"
#include "Settings.h"
#include "resources/ResourceManager.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <algorithm>
#include <chrono>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

namespace
{
    // How much decoded music to keep ready, in seconds.
    constexpr float BUFFER_SECONDS {0.5f};
    // How long the music takes to fade out when a game starts.
    constexpr int FADE_OUT_MS {1200};
    // How long it takes to fade back in, such as after a game.
    constexpr int FADE_IN_MS {2500};

    void addTracks(const std::string& directory, std::vector<std::string>& tracks)
    {
        if (directory.empty() || !Utils::FileSystem::isDirectory(directory))
            return;

        for (const std::string& path : Utils::FileSystem::getDirContent(directory)) {
            const std::string extension {
                Utils::String::toLower(Utils::FileSystem::getExtension(path))};
            if (extension == ".mp3" || extension == ".ogg" || extension == ".flac" ||
                extension == ".wav" || extension == ".opus" || extension == ".m4a")
                tracks.emplace_back(path);
        }
    }
} // namespace

MusicManager& MusicManager::getInstance()
{
    static MusicManager instance;
    return instance;
}

MusicManager::~MusicManager() { stop(); }

void MusicManager::start()
{
    if (mRunning)
        return;

    mTracks.clear();
    addTracks(ResourceManager::getInstance().getResourcePath(":/music", false), mTracks);
    addTracks(Utils::FileSystem::getAppDataDirectory() + "/music", mTracks);

    if (mTracks.empty()) {
        LOG(LogInfo) << "MusicManager: no background music found";
        return;
    }

    LOG(LogInfo) << "MusicManager: " << mTracks.size() << " background music tracks";
    mOrder.clear();
    mOrderPos = 0;
    mRunning = true;
    mThread = std::thread(&MusicManager::run, this);
}

void MusicManager::stop()
{
    if (!mRunning)
        return;

    mRunning = false;
    if (mThread.joinable())
        mThread.join();
    closeTrack();
    AudioManager::getInstance().clearMusic();
}

void MusicManager::update(bool gameRunning)
{
    mShouldPlay = !gameRunning && Settings::getInstance()->getBool("BackgroundMusic") &&
                  Settings::getInstance()->getInt("SoundVolumeMusic") > 0;
}

void MusicManager::pause()
{
    // The worker thread notices within a few milliseconds and fades the music out.
    ++mPauseCount;
    mShouldPlay = false;
}

void MusicManager::resume()
{
    if (mPauseCount > 0)
        --mPauseCount;
}

void MusicManager::run()
{
    AudioManager& audio {AudioManager::getInstance()};
    bool playing {false};

    while (mRunning) {
        const bool wantMusic {mShouldPlay && mPauseCount == 0};

        if (!wantMusic) {
            if (playing) {
                audio.fadeOutMusic(FADE_OUT_MS);
                playing = false;
                LOG(LogDebug) << "MusicManager: fading out";
            }
            // Keep the music coming until the fade has finished.
            if (audio.isMusicFading() && mFormat != nullptr &&
                audio.getMusicBuffered() < BUFFER_SECONDS) {
                if (!decodeSome())
                    closeTrack();
            }
            else {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            continue;
        }

        if (!playing) {
            audio.resumeMusic(FADE_IN_MS);
            playing = true;
            LOG(LogDebug) << "MusicManager: playing";
        }

        if (mFormat == nullptr && !openNextTrack()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        if (audio.getMusicBuffered() >= BUFFER_SECONDS) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        if (!decodeSome())
            closeTrack();
    }

    closeTrack();
}

bool MusicManager::openNextTrack()
{
    // Play every track once in random order, then shuffle again. A new round never starts with
    // the track that just finished.
    if (mOrderPos >= mOrder.size()) {
        mOrder.resize(mTracks.size());
        for (size_t i {0}; i < mOrder.size(); ++i)
            mOrder[i] = i;
        std::shuffle(mOrder.begin(), mOrder.end(), mRandom);
        if (mOrder.size() > 1 && mOrder.front() == mLastTrack)
            std::swap(mOrder.front(), mOrder.back());
        mOrderPos = 0;
    }

    const size_t track {mOrder[mOrderPos++]};
    mLastTrack = track;
    const std::string& path {mTracks[track]};

    if (avformat_open_input(&mFormat, path.c_str(), nullptr, nullptr) != 0) {
        LOG(LogWarning) << "MusicManager: couldn't open \"" << path << "\"";
        mFormat = nullptr;
        return false;
    }

    const AVCodec* codec {nullptr};
    if (avformat_find_stream_info(mFormat, nullptr) < 0 ||
        (mStreamIndex = av_find_best_stream(mFormat, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0)) <
            0 ||
        codec == nullptr) {
        LOG(LogWarning) << "MusicManager: no audio in \"" << path << "\"";
        closeTrack();
        return false;
    }

    mCodec = avcodec_alloc_context3(codec);
    if (mCodec == nullptr ||
        avcodec_parameters_to_context(mCodec, mFormat->streams[mStreamIndex]->codecpar) < 0 ||
        avcodec_open2(mCodec, codec, nullptr) < 0) {
        LOG(LogWarning) << "MusicManager: couldn't decode \"" << path << "\"";
        closeTrack();
        return false;
    }

    AVChannelLayout stereo;
    av_channel_layout_default(&stereo, 2);
    if (swr_alloc_set_opts2(&mResampler, &stereo, AV_SAMPLE_FMT_FLT, 44100, &mCodec->ch_layout,
                            mCodec->sample_fmt, mCodec->sample_rate, 0, nullptr) < 0 ||
        swr_init(mResampler) < 0) {
        LOG(LogWarning) << "MusicManager: couldn't convert the audio of \"" << path << "\"";
        closeTrack();
        return false;
    }

    mPacket = av_packet_alloc();
    mFrame = av_frame_alloc();
    LOG(LogDebug) << "MusicManager: playing \"" << path << "\"";
    return true;
}

void MusicManager::closeTrack()
{
    if (mFrame != nullptr)
        av_frame_free(&mFrame);
    if (mPacket != nullptr)
        av_packet_free(&mPacket);
    if (mResampler != nullptr)
        swr_free(&mResampler);
    if (mCodec != nullptr)
        avcodec_free_context(&mCodec);
    if (mFormat != nullptr)
        avformat_close_input(&mFormat);
    mStreamIndex = -1;
}

bool MusicManager::decodeSome()
{
    // Read packets until one of them gives audio.
    while (true) {
        if (av_read_frame(mFormat, mPacket) < 0)
            return false;

        if (mPacket->stream_index != mStreamIndex) {
            av_packet_unref(mPacket);
            continue;
        }

        const int sent {avcodec_send_packet(mCodec, mPacket)};
        av_packet_unref(mPacket);
        if (sent < 0)
            continue;

        bool gotAudio {false};
        while (avcodec_receive_frame(mCodec, mFrame) == 0) {
            const int maxSamples {swr_get_out_samples(mResampler, mFrame->nb_samples)};
            if (maxSamples > 0) {
                std::vector<float> samples(static_cast<size_t>(maxSamples) * 2);
                uint8_t* output {reinterpret_cast<uint8_t*>(samples.data())};
                const int converted {
                    swr_convert(mResampler, &output, maxSamples,
                                const_cast<const uint8_t**>(mFrame->extended_data),
                                mFrame->nb_samples)};
                if (converted > 0) {
                    AudioManager::getInstance().processMusic(
                        samples.data(), converted * 2 * static_cast<int>(sizeof(float)));
                    gotAudio = true;
                }
            }
            av_frame_unref(mFrame);
        }

        if (gotAudio)
            return true;
    }
}
