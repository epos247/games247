//  SPDX-License-Identifier: MIT
//
//  games247
//  GuiFtpDownloader.h
//
//  Shown when a game that's only in FTP storage is launched. Downloads the game into the
//  system's local ROM folder with a progress bar, then launches it. B / Escape cancels.
//

#ifndef GAMES247_GUI_FTP_DOWNLOADER_H
#define GAMES247_GUI_FTP_DOWNLOADER_H

#include "FtpStorage.h"
#include "GuiComponent.h"
#include "components/TextComponent.h"

#include <atomic>
#include <memory>
#include <thread>

class FileData;

class GuiFtpDownloader : public GuiComponent
{
public:
    explicit GuiFtpDownloader(FileData* game);
    ~GuiFtpDownloader();

    bool input(InputConfig* config, Input input) override;
    void update(int deltaTime) override;
    void render(const glm::mat4& parentTrans) override;
    std::vector<HelpPrompt> getHelpPrompts() override;

private:
    void run();

    FileData* mGame;
    std::string mGameName;
    std::string mLocalPath;
    std::string mURL;
    FtpStorage::Account mAccount;

    std::thread mThread;
    std::atomic<long long> mDownloaded {0};
    std::atomic<long long> mTotal {0};
    std::atomic<bool> mCancel {false};
    std::atomic<bool> mFinished {false};
    bool mSucceeded {false};
    std::string mError;

    std::shared_ptr<TextComponent> mTitle;
    std::shared_ptr<TextComponent> mStatus;
    float mProgress {0.0f};
    bool mInputUnblocked {false};
};

#endif // GAMES247_GUI_FTP_DOWNLOADER_H
