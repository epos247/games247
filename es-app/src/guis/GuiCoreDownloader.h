//  SPDX-License-Identifier: MIT
//
//  games247
//  GuiCoreDownloader.h
//
//  Shown the first time a game needs a libretro core that isn't installed. Downloads the core
//  from the FTP storage's cores folder if it's there (see FtpCores.h), otherwise from the
//  libretro build server, installs it, then starts the game in the built-in player.
//  B / Escape cancels. Also updates an installed core to the server's newer build before its
//  game starts (see CoreUpdates.h); if that fails or is cancelled, the game starts with the
//  core it has.
//

#ifndef GAMES247_GUI_CORE_DOWNLOADER_H
#define GAMES247_GUI_CORE_DOWNLOADER_H

#include "GuiComponent.h"
#include "HttpReq.h"
#include "components/TextComponent.h"
#include "libretro/FtpCores.h"

#include <atomic>
#include <memory>
#include <thread>

class FileData;

class GuiCoreDownloader : public GuiComponent
{
public:
    GuiCoreDownloader(const std::string& coreName,
                      const std::string& romPath,
                      FileData* game,
                      bool update = false);
    ~GuiCoreDownloader();

    bool input(InputConfig* config, Input input) override;
    void update(int deltaTime) override;
    void render(const glm::mat4& parentTrans) override;
    std::vector<HelpPrompt> getHelpPrompts() override;

private:
    void finish();
    void fail(const std::string& message);
    // Update mode: starts the game with the core already installed.
    void startWithInstalledCore();
    // Downloads the core from the libretro build server.
    void startBuildServerDownload();
    // Runs on mFtpThread: looks for the core in the FTP storage and installs it from there.
    void ftpThread();
    // Called once the FTP thread is done (mFtpState isn't LOOKING or DOWNLOADING).
    void finishFtp();
    void stopFtpThread();
    void launchInstalledCore();

    std::string mCoreName;
    std::string mRomPath;
    FileData* mGame;
    // fail() chose the standalone emulator: it's launched once this is closed.
    bool mLaunchStandalone {false};
    std::string mDisplayName;

    enum FtpState {
        FTP_LOOKING,
        FTP_NOT_THERE,
        FTP_DOWNLOADING,
        FTP_INSTALLED,
        FTP_FAILED
    };
    std::thread mFtpThread;
    std::atomic<int> mFtpState {FTP_NOT_THERE};
    std::atomic<bool> mFtpCancel {false};
    std::atomic<long long> mFtpDownloaded {0};
    std::atomic<long long> mFtpTotal {0};
    FtpCores::Core mFtpCore;
    std::string mFtpError;

    std::unique_ptr<HttpReq> mRequest;
    std::shared_ptr<TextComponent> mText;
    bool mInputUnblocked;
    bool mDone;
    bool mUpdate;
};

#endif // GAMES247_GUI_CORE_DOWNLOADER_H
