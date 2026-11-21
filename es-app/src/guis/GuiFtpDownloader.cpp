//  SPDX-License-Identifier: MIT
//
//  games247
//  GuiFtpDownloader.cpp
//
//  Downloads a game from FTP storage, then launches it.
//

#include "guis/GuiFtpDownloader.h"

#include "FileData.h"
#include "InputConfig.h"
#include "Log.h"
#include "Window.h"
#include "components/BackgroundComponent.h"
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

GuiFtpDownloader::GuiFtpDownloader(FileData* game)
    : mGame {game}
    , mGameName {Utils::String::toUpper(game->metadata.get("name"))}
    , mLocalPath {game->getPath()}
    , mURL {FtpStorage::getInstance().getRemoteURL(game->getPath())}
    , mAccount {FtpStorage::accountFromSettings()}
{
    Renderer* renderer {Renderer::getInstance()};
    setSize(renderer->getScreenWidth(), renderer->getScreenHeight());

    mTitle = std::make_shared<TextComponent>(_("DOWNLOADING FROM FTP STORAGE"),
                                             Font::get(FONT_SIZE_SMALL), mMenuColorSecondary,
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

    LOG(LogInfo) << "GuiFtpDownloader: downloading \"" << mLocalPath << "\"";
    mThread = std::thread(&GuiFtpDownloader::run, this);
}

GuiFtpDownloader::~GuiFtpDownloader()
{
    mCancel = true;
    if (mThread.joinable())
        mThread.join();
}

void GuiFtpDownloader::run()
{
    // A game that's a folder, such as a PS3 game, comes down a file at a time.
    {
        std::string error;
        const bool succeeded {FtpStorage::downloadFolder(
            mAccount, mURL + "/", mLocalPath,
            [this](long long downloaded, long long total) {
                mDownloaded = downloaded;
                mTotal = total;
                return !mCancel;
            },
            error, &mCancel)};
        if (succeeded || error != FtpStorage::NOT_A_FOLDER) {
            mError = succeeded ? "" : error;
            mSucceeded = succeeded;
            mFinished = true;
            return;
        }
    }

    // Download next to the game first, so a cancelled or broken download never looks like a
    // real game file.
    const std::string partPath {mLocalPath + ".part"};
    std::string error;
    const bool succeeded {FtpStorage::download(
        mAccount, mURL, partPath,
        [this](long long downloaded, long long total) {
            mDownloaded = downloaded;
            mTotal = total;
            return !mCancel;
        },
        error)};

    // renameFile() returns true when it fails.
    if (succeeded && Utils::FileSystem::renameFile(partPath, mLocalPath, true)) {
        Utils::FileSystem::removeFile(partPath);
        mError = "couldn't save the game";
    }
    else if (!succeeded) {
        mError = error;
    }
    mSucceeded = succeeded && mError.empty();
    mFinished = true;
}

std::vector<HelpPrompt> GuiFtpDownloader::getHelpPrompts()
{
    return {HelpPrompt("b", _("cancel"))};
}

bool GuiFtpDownloader::input(InputConfig* config, Input input)
{
    if (input.value != 0 && !mCancel &&
        (config->isMappedTo("b", input) ||
         (config->getDeviceId() == DEVICE_KEYBOARD && input.id == SDLK_ESCAPE))) {
        LOG(LogInfo) << "GuiFtpDownloader: download cancelled";
        mCancel = true;
        mStatus->setText(_("CANCELLING..."));
    }
    return true;
}

void GuiFtpDownloader::update(int deltaTime)
{
    // Input is blocked during the launch animation. Unblock it so the download can be cancelled.
    if (!mInputUnblocked) {
        Window::getInstance()->setBlockInput(false);
        mInputUnblocked = true;
    }

    GuiComponent::update(deltaTime);

    if (!mFinished) {
        const long long total {mTotal};
        const long long downloaded {mDownloaded};
        if (!mCancel && total > 0) {
            mProgress =
                static_cast<float>(static_cast<double>(downloaded) / static_cast<double>(total));
            mStatus->setText(Utils::String::format("%s  %i%%  (%s / %s)", mGameName.c_str(),
                                                   static_cast<int>(std::round(mProgress * 100.0f)),
                                                   formatSize(downloaded).c_str(),
                                                   formatSize(total).c_str()));
        }
        else if (!mCancel && downloaded > 0) {
            mStatus->setText(mGameName + "  " + formatSize(downloaded));
        }
        return;
    }

    if (mThread.joinable())
        mThread.join();

    FileData* game {mGame};
    if (mSucceeded) {
        LOG(LogInfo) << "GuiFtpDownloader: downloaded \"" << mLocalPath << "\"";
        FtpStorage::getInstance().forgetRemoteGame(mLocalPath);
        // Refresh the gamelist so the game loses its cloud symbol.
        ViewController::getInstance()->onFileChanged(game, true);
    }
    else if (mError != "cancelled") {
        LOG(LogError) << "GuiFtpDownloader: couldn't download \"" << mLocalPath << "\": " << mError;
        Window::getInstance()->queueInfoPopup(
            Utils::String::format(_("COULDN'T DOWNLOAD %s: %s"), mGameName.c_str(),
                                  Utils::String::toUpper(mError).c_str()),
            6000);
    }

    const bool launch {mSucceeded && !mCancel};
    delete this;

    // Now that it's a local game, launch it the normal way.
    if (launch)
        ViewController::getInstance()->launch(game);
}

void GuiFtpDownloader::render(const glm::mat4& parentTrans)
{
    Renderer* renderer {Renderer::getInstance()};
    const glm::mat4 trans {parentTrans * getTransform()};
    renderer->setMatrix(trans);
    renderer->drawRect(0.0f, 0.0f, mSize.x, mSize.y, 0x000000C0, 0x000000C0);

    BackgroundComponent::renderNeonPanel(
        renderer, trans, mSize.x * 0.2f, mSize.y * 0.35f, mSize.x * 0.6f, mSize.y * 0.3f,
        20.0f * renderer->getScreenResolutionModifier(), mMenuColorFrame, 1.0f);

    // Progress bar.
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

    GuiComponent::renderChildren(trans);
}
