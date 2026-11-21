//  SPDX-License-Identifier: MIT
//
//  games247
//  GuiFtpRemover.h
//
//  Removes a game from this computer once it's safe in FTP storage. Checks whether the FTP
//  server has the same file (same name and size). If it does, asks and then removes the local
//  file. If it doesn't, offers to upload it first, with a progress bar. The game stays in the
//  game list as an FTP game (with its metadata and media), and is downloaded again when it's
//  played. B / Escape cancels the check or the upload.
//

#ifndef GAMES247_GUI_FTP_REMOVER_H
#define GAMES247_GUI_FTP_REMOVER_H

#include "FtpStorage.h"
#include "GuiComponent.h"
#include "components/TextComponent.h"

#include <atomic>
#include <memory>
#include <thread>

class FileData;

class GuiFtpRemover : public GuiComponent
{
public:
    explicit GuiFtpRemover(FileData* game);
    ~GuiFtpRemover();

    // True if the game can be removed this way: FTP storage is on, and the game is a file on
    // this computer directly in its system's ROM folder (FTP storage only holds those). The
    // game may be a folder, such as a PS3 game.
    static bool canRemove(FileData* game);

    bool input(InputConfig* config, Input input) override;
    void update(int deltaTime) override;
    void render(const glm::mat4& parentTrans) override;
    std::vector<HelpPrompt> getHelpPrompts() override;

private:
    enum class State {
        CHECKING,
        ASKING,
        UPLOADING,
    };
    // What the question's buttons chose. Acted on in update(), once the question has closed.
    enum class Choice {
        NONE,
        REMOVE,
        UPLOAD,
        CLOSE,
    };

    void check();
    void upload();
    void ask();
    void startUpload();
    void removeLocalCopy();
    void showPanel(const std::string& title);

    FileData* mGame;
    std::string mGameName;
    std::string mLocalPath;
    std::string mFolderName;
    std::string mFileName;
    std::string mURL;
    long long mLocalSize;
    // A game that's a folder, such as a PS3 game, and what's in it.
    bool mIsFolder;
    std::vector<FtpStorage::TreeEntry> mLocalTree;
    FtpStorage::Account mAccount;

    State mState {State::CHECKING};
    Choice mChoice {Choice::NONE};
    std::thread mThread;
    std::atomic<long long> mUploaded {0};
    std::atomic<long long> mTotal {0};
    std::atomic<bool> mCancel {false};
    std::atomic<bool> mFinished {false};
    bool mSucceeded {false};
    FtpStorage::FileInfo mServerFile;
    std::string mError;

    std::shared_ptr<TextComponent> mTitle;
    std::shared_ptr<TextComponent> mStatus;
    float mProgress {0.0f};
};

#endif // GAMES247_GUI_FTP_REMOVER_H
