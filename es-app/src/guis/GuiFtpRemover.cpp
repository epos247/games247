//  SPDX-License-Identifier: MIT
//
//  games247
//  GuiFtpRemover.cpp
//
//  Removes a game from this computer once it's in FTP storage, uploading it first if needed.
//

#include "guis/GuiFtpRemover.h"

#include "FileData.h"
#include "InputConfig.h"
#include "Log.h"
#include "SystemData.h"
#include "Window.h"
#include "components/BackgroundComponent.h"
#include "guis/GuiMsgBox.h"
#include "renderers/Renderer.h"
#include "resources/Font.h"
#include "utils/FileSystemUtil.h"
#include "utils/LocalizationUtil.h"
#include "utils/StringUtil.h"
#include "views/ViewController.h"

#include <algorithm>
#include <cmath>

namespace
{
    std::string formatSize(long long bytes)
    {
        const double megabytes {static_cast<double>(bytes) / (1024.0 * 1024.0)};
        if (megabytes < 10.0)
            return Utils::String::format("%.1f MB", megabytes);
        return Utils::String::format("%.0f MB", megabytes);
    }
} // namespace

bool GuiFtpRemover::canRemove(FileData* game)
{
    if (game == nullptr || game->getType() != GAME || !FtpStorage::isEnabled())
        return false;
    FileData* source {game->getSourceFileData()};
    const std::string& path {source->getPath()};
    if (FtpStorage::getInstance().isRemoteGame(path))
        return false;
    if (!Utils::FileSystem::isRegularFile(path) || Utils::FileSystem::isDirectory(path))
        return false;
    return Utils::FileSystem::getParent(path) ==
           Utils::FileSystem::getGenericPath(source->getSystemEnvData()->mStartPath);
}

GuiFtpRemover::GuiFtpRemover(FileData* game)
    : mGame {game->getSourceFileData()}
    , mGameName {Utils::String::toUpper(mGame->metadata.get("name"))}
    , mLocalPath {mGame->getPath()}
    , mFolderName {Utils::FileSystem::getFileName(mGame->getSystemEnvData()->mStartPath)}
    , mFileName {Utils::FileSystem::getFileName(mLocalPath)}
    , mURL {FtpStorage::getInstance().getFileURL(mFolderName, mFileName)}
    , mLocalSize {FtpStorage::localFileSize(mLocalPath)}
    , mAccount {FtpStorage::accountFromSettings()}
{
    Renderer* renderer {Renderer::getInstance()};
    setSize(renderer->getScreenWidth(), renderer->getScreenHeight());

    mTitle = std::make_shared<TextComponent>("", Font::get(FONT_SIZE_SMALL), mMenuColorSecondary,
                                             ALIGN_CENTER, ALIGN_CENTER, glm::ivec2 {0, 0});
    mTitle->setSize(mSize.x * 0.56f, mSize.y * 0.05f);
    mTitle->setPosition(mSize.x * 0.22f, mSize.y * 0.38f);
    addChild(mTitle.get());

    mStatus =
        std::make_shared<TextComponent>(mGameName, Font::get(FONT_SIZE_MEDIUM), mMenuColorTitle,
                                        ALIGN_CENTER, ALIGN_CENTER, glm::ivec2 {0, 0});
    mStatus->setSize(mSize.x * 0.56f, mSize.y * 0.08f);
    mStatus->setPosition(mSize.x * 0.22f, mSize.y * 0.43f);
    addChild(mStatus.get());

    showPanel(_("CHECKING FTP STORAGE"));
    LOG(LogInfo) << "GuiFtpRemover: checking whether FTP storage has \"" << mLocalPath << "\"";
    mThread = std::thread(&GuiFtpRemover::check, this);
}

GuiFtpRemover::~GuiFtpRemover()
{
    mCancel = true;
    if (mThread.joinable())
        mThread.join();
}

void GuiFtpRemover::showPanel(const std::string& title)
{
    mTitle->setText(title);
    mStatus->setText(mGameName);
    mProgress = 0.0f;
}

void GuiFtpRemover::check()
{
    std::string error;
    mSucceeded = FtpStorage::fileInfo(mAccount, mURL, mServerFile, error);
    mError = error;
    mFinished = true;
}

void GuiFtpRemover::upload()
{
    // Upload under another name first, so a part-uploaded file is never taken for the game.
    // The upload renames it once it's all there.
    const std::string partURL {FtpStorage::getInstance().getFileURL(mFolderName,
                                                                     mFileName + ".part")};
    std::string error;
    bool succeeded {FtpStorage::upload(
        mAccount, mLocalPath, partURL, error,
        [this](long long uploaded, long long total) {
            mUploaded = uploaded;
            mTotal = total;
            return !mCancel;
        },
        mFileName)};

    // Only remove the game once the server really has all of it.
    if (succeeded) {
        FtpStorage::FileInfo info;
        if (!FtpStorage::fileInfo(mAccount, mURL, info, error)) {
            succeeded = false;
        }
        else if (!info.exists || info.size != mLocalSize) {
            succeeded = false;
            error = "the uploaded file isn't complete on the server";
        }
    }
    mError = error;
    mSucceeded = succeeded;
    mFinished = true;
}

void GuiFtpRemover::ask()
{
    mState = State::ASKING;
    mChoice = Choice::NONE;
    const auto choose = [this](Choice choice) { return [this, choice] { mChoice = choice; }; };
    Window* window {Window::getInstance()};

    if (mServerFile.exists && mServerFile.size == mLocalSize) {
        window->pushGui(new GuiMsgBox(
            Utils::String::format(_("%s IS IN FTP STORAGE. REMOVE IT FROM THIS COMPUTER? IT "
                                    "STAYS IN THE GAME LIST AND DOWNLOADS AGAIN WHEN YOU PLAY "
                                    "IT."),
                                  mGameName.c_str()),
            _("REMOVE"), choose(Choice::REMOVE), _("CANCEL"), choose(Choice::CLOSE), "", nullptr,
            "", nullptr, choose(Choice::CLOSE)));
    }
    else if (mServerFile.exists) {
        window->pushGui(new GuiMsgBox(
            Utils::String::format(_("FTP STORAGE HAS A DIFFERENT COPY OF %s (%s THERE, %s ON "
                                    "THIS COMPUTER). UPLOAD THIS COPY IN ITS PLACE, THEN REMOVE "
                                    "IT FROM THIS COMPUTER?"),
                                  mGameName.c_str(), formatSize(mServerFile.size).c_str(),
                                  formatSize(mLocalSize).c_str()),
            _("UPLOAD AND REMOVE"), choose(Choice::UPLOAD), _("CANCEL"), choose(Choice::CLOSE), "",
            nullptr, "", nullptr, choose(Choice::CLOSE)));
    }
    else {
        window->pushGui(new GuiMsgBox(
            Utils::String::format(_("%s ISN'T IN FTP STORAGE YET. UPLOAD IT (%s), THEN REMOVE IT "
                                    "FROM THIS COMPUTER?"),
                                  mGameName.c_str(), formatSize(mLocalSize).c_str()),
            _("UPLOAD AND REMOVE"), choose(Choice::UPLOAD), _("CANCEL"), choose(Choice::CLOSE), "",
            nullptr, "", nullptr, choose(Choice::CLOSE)));
    }
}

void GuiFtpRemover::startUpload()
{
    mState = State::UPLOADING;
    mFinished = false;
    mSucceeded = false;
    mUploaded = 0;
    mTotal = mLocalSize;
    showPanel(_("UPLOADING TO FTP STORAGE"));
    LOG(LogInfo) << "GuiFtpRemover: uploading \"" << mLocalPath << "\" to FTP storage";
    mThread = std::thread(&GuiFtpRemover::upload, this);
}

void GuiFtpRemover::removeLocalCopy()
{
    Window* window {Window::getInstance()};
    if (!Utils::FileSystem::removeFile(mLocalPath)) {
        LOG(LogError) << "GuiFtpRemover: couldn't delete \"" << mLocalPath << "\"";
        window->queueInfoPopup(
            Utils::String::format(_("COULDN'T REMOVE %s FROM THIS COMPUTER"), mGameName.c_str()),
            6000);
        return;
    }
    LOG(LogInfo) << "GuiFtpRemover: removed \"" << mLocalPath
                 << "\" from this computer, it's in FTP storage";

    // It's an FTP game from now on, like one that was never downloaded.
    FtpStorage& ftp {FtpStorage::getInstance()};
    ftp.addToListing(mFolderName, mFileName);
    ftp.addRemoteGame(mLocalPath, mFolderName, mFileName);
    // Refresh the gamelist so the game gets its cloud symbol.
    ViewController::getInstance()->onFileChanged(mGame, true);
    window->queueInfoPopup(
        Utils::String::format(_("REMOVED %s FROM THIS COMPUTER"), mGameName.c_str()), 4000);
}

std::vector<HelpPrompt> GuiFtpRemover::getHelpPrompts()
{
    return {HelpPrompt("b", _("cancel"))};
}

bool GuiFtpRemover::input(InputConfig* config, Input input)
{
    if (input.value != 0 && !mCancel && mState != State::ASKING &&
        (config->isMappedTo("b", input) ||
         (config->getDeviceId() == DEVICE_KEYBOARD && input.id == SDLK_ESCAPE))) {
        LOG(LogInfo) << "GuiFtpRemover: cancelled";
        mCancel = true;
        mStatus->setText(_("CANCELLING..."));
    }
    return true;
}

void GuiFtpRemover::update(int deltaTime)
{
    GuiComponent::update(deltaTime);

    if (mState == State::ASKING) {
        // The question closes before this runs again.
        switch (mChoice) {
            case Choice::NONE:
                return;
            case Choice::REMOVE:
                removeLocalCopy();
                delete this;
                return;
            case Choice::UPLOAD:
                startUpload();
                return;
            case Choice::CLOSE:
                delete this;
                return;
        }
    }

    if (!mFinished) {
        if (mState == State::UPLOADING && !mCancel) {
            const long long total {mTotal};
            const long long uploaded {mUploaded};
            if (total > 0) {
                mProgress = static_cast<float>(static_cast<double>(uploaded) /
                                               static_cast<double>(total));
                mStatus->setText(Utils::String::format(
                    "%s  %i%%  (%s / %s)", mGameName.c_str(),
                    static_cast<int>(std::round(mProgress * 100.0f)), formatSize(uploaded).c_str(),
                    formatSize(total).c_str()));
            }
        }
        return;
    }

    if (mThread.joinable())
        mThread.join();

    if (mCancel) {
        delete this;
        return;
    }

    if (mState == State::CHECKING) {
        if (!mSucceeded) {
            LOG(LogError) << "GuiFtpRemover: couldn't check FTP storage for \"" << mLocalPath
                          << "\": " << mError;
            Window::getInstance()->queueInfoPopup(
                Utils::String::format(_("COULDN'T CHECK FTP STORAGE: %s"),
                                      Utils::String::toUpper(mError).c_str()),
                6000);
            delete this;
            return;
        }
        LOG(LogInfo) << "GuiFtpRemover: FTP storage "
                     << (!mServerFile.exists ? "doesn't have the game" :
                                               (mServerFile.size == mLocalSize ?
                                                    "has the game" :
                                                    "has a different copy of the game"));
        ask();
        return;
    }

    // Uploaded.
    if (mSucceeded) {
        LOG(LogInfo) << "GuiFtpRemover: uploaded \"" << mLocalPath << "\"";
        removeLocalCopy();
    }
    else {
        LOG(LogError) << "GuiFtpRemover: couldn't upload \"" << mLocalPath << "\": " << mError;
        Window::getInstance()->queueInfoPopup(
            Utils::String::format(_("COULDN'T UPLOAD %s: %s"), mGameName.c_str(),
                                  Utils::String::toUpper(mError).c_str()),
            6000);
    }
    delete this;
}

void GuiFtpRemover::render(const glm::mat4& parentTrans)
{
    // The question has its own window.
    if (mState == State::ASKING)
        return;

    Renderer* renderer {Renderer::getInstance()};
    const glm::mat4 trans {parentTrans * getTransform()};
    renderer->setMatrix(trans);
    renderer->drawRect(0.0f, 0.0f, mSize.x, mSize.y, 0x000000C0, 0x000000C0);

    BackgroundComponent::renderNeonPanel(
        renderer, trans, mSize.x * 0.2f, mSize.y * 0.35f, mSize.x * 0.6f, mSize.y * 0.3f,
        20.0f * renderer->getScreenResolutionModifier(), mMenuColorFrame, 1.0f);

    // Progress bar, while uploading.
    if (mState == State::UPLOADING) {
        renderer->setMatrix(trans);
        const float barX {mSize.x * 0.26f};
        const float barY {mSize.y * 0.555f};
        const float barW {mSize.x * 0.48f};
        const float barH {std::max(4.0f, mSize.y * 0.012f)};
        renderer->drawRect(barX, barY, barW, barH, 0xFFFFFF22, 0xFFFFFF22);
        if (mProgress > 0.0f) {
            renderer->drawRect(barX, barY, barW * std::min(mProgress, 1.0f), barH,
                               mMenuColorNeonEdgeStart, mMenuColorNeonEdgeEnd, true);
        }
    }

    GuiComponent::renderChildren(trans);
}
