//  SPDX-License-Identifier: MIT
//
//  games247
//  GuiCoreDownloader.cpp
//
//  Downloads a missing libretro core, then starts the game in the built-in player.
//

#include "guis/GuiCoreDownloader.h"

#include "FileData.h"
#include "FtpStorage.h"
#include "InputConfig.h"
#include "Log.h"
#include "Window.h"
#include "guis/GuiLibretroPlayer.h"
#include "libretro/CoreManager.h"
#include "libretro/CoreUpdates.h"
#include "renderers/Renderer.h"
#include "resources/Font.h"
#include "utils/FileSystemUtil.h"
#include "utils/LocalizationUtil.h"
#include "utils/StringUtil.h"
#include "views/ViewController.h"

#include <cmath>
#include <fstream>

GuiCoreDownloader::GuiCoreDownloader(const std::string& coreName,
                                     const std::string& romPath,
                                     FileData* game,
                                     bool update,
                                     const std::string& contentArgument)
    : mCoreName {coreName}
    , mRomPath {romPath}
    , mContentArgument {contentArgument}
    , mGame {game}
    , mInputUnblocked {false}
    , mDone {false}
    , mUpdate {update}
{
    // "vice_x64sc_libretro" -> "VICE X64SC"
    std::string name {Utils::String::replace(coreName, "_libretro", "")};
    mDisplayName = Utils::String::toUpper(Utils::String::replace(name, "_", " "));

    Renderer* renderer {Renderer::getInstance()};
    setSize(renderer->getScreenWidth(), renderer->getScreenHeight());

    mText = std::make_shared<TextComponent>("", Font::get(FONT_SIZE_MEDIUM), mMenuColorTitle,
                                            ALIGN_CENTER, ALIGN_CENTER, glm::ivec2 {0, 0});
    mText->setSize(mSize.x * 0.6f, mSize.y * 0.2f);
    mText->setPosition(mSize.x * 0.2f, mSize.y * 0.4f);
    addChild(mText.get());

    mText->setText(Utils::String::format(
        mUpdate ? _("UPDATING EMULATOR %s") : _("DOWNLOADING EMULATOR %s"), mDisplayName.c_str()));

    // The FTP storage's cores folder comes first.
    if (FtpStorage::isEnabled()) {
        mFtpState = FTP_LOOKING;
        mFtpThread = std::thread {&GuiCoreDownloader::ftpThread, this};
    }
    else {
        startBuildServerDownload();
    }
}

GuiCoreDownloader::~GuiCoreDownloader() { stopFtpThread(); }

void GuiCoreDownloader::stopFtpThread()
{
    if (mFtpThread.joinable()) {
        mFtpCancel = true;
        mFtpThread.join();
    }
}

void GuiCoreDownloader::startBuildServerDownload()
{
    const std::string url {CoreManager::downloadURL(mCoreName)};
    if (url.empty()) {
        fail(_("emulator isn't available for this device"));
        return;
    }
    LOG(LogInfo) << "GuiCoreDownloader: downloading " << url;
    mRequest = std::make_unique<HttpReq>(url, false);
}

void GuiCoreDownloader::ftpThread()
{
    std::string error;
    if (!FtpCores::find(mCoreName, mFtpCore, error, &mFtpCancel)) {
        if (!error.empty() && error != "cancelled") {
            LOG(LogWarning) << "GuiCoreDownloader: couldn't look in the FTP storage's cores "
                               "folder ("
                            << error << "), trying the build server";
        }
        mFtpState = FTP_NOT_THERE;
        return;
    }
    LOG(LogInfo) << "GuiCoreDownloader: downloading " << mCoreName << " from the FTP storage";
    mFtpState = FTP_DOWNLOADING;
    if (FtpCores::install(mCoreName, mFtpCore, mFtpDownloaded, mFtpTotal, mFtpCancel, error)) {
        mFtpState = FTP_INSTALLED;
    }
    else {
        mFtpError = error;
        mFtpState = FTP_FAILED;
    }
}

void GuiCoreDownloader::finishFtp()
{
    mFtpThread.join();
    const int state {mFtpState};
    if (state == FTP_NOT_THERE) {
        startBuildServerDownload();
        return;
    }
    if (state == FTP_FAILED) {
        fail(mFtpError);
        return;
    }
    CoreUpdates::recordInstalled(mCoreName, FtpCores::version(mFtpCore));
    launchInstalledCore();
}

void GuiCoreDownloader::launchInstalledCore()
{
    std::string error;
    if (!GuiLibretroPlayer::launch(CoreManager::findInstalledCore(mCoreName), mRomPath, mGame,
                                   error, mContentArgument)) {
        fail(error);
        return;
    }
    mDone = true;
}

std::vector<HelpPrompt> GuiCoreDownloader::getHelpPrompts()
{
    return {HelpPrompt("b", _("cancel"))};
}

bool GuiCoreDownloader::input(InputConfig* config, Input input)
{
    if (input.value != 0 && !mDone &&
        (config->isMappedTo("b", input) ||
         (config->getDeviceId() == DEVICE_KEYBOARD && input.id == SDLK_ESCAPE))) {
        LOG(LogInfo) << "GuiCoreDownloader: download cancelled";
        mDone = true;
        mRequest.reset();
        // The core file mustn't be changing while the game starts with it.
        stopFtpThread();
        if (mUpdate)
            startWithInstalledCore();
    }
    return true;
}

void GuiCoreDownloader::startWithInstalledCore()
{
    std::string error;
    if (!GuiLibretroPlayer::launch(CoreManager::findInstalledCore(mCoreName), mRomPath, mGame,
                                   error, mContentArgument)) {
        Window::getInstance()->queueInfoPopup(
            Utils::String::format(_("COULDN'T START %s: %s"), mDisplayName.c_str(),
                                  Utils::String::toUpper(error).c_str()),
            6000);
    }
}

void GuiCoreDownloader::fail(const std::string& message)
{
    if (mUpdate) {
        // The game still starts, with the core it has.
        LOG(LogWarning) << "GuiCoreDownloader: couldn't update " << mCoreName << ": " << message;
        mDone = true;
        mRequest.reset();
        startWithInstalledCore();
        return;
    }
    // A core only the FTP storage has: the game runs in the standalone emulator instead.
    if (CoreManager::isFtpOnly(mCoreName) && mGame != nullptr) {
        const std::string standalone {mGame->getStandaloneCommand()};
        if (!standalone.empty()) {
            LOG(LogInfo) << "GuiCoreDownloader: no " << mCoreName << " core (" << message
                         << "), using the standalone emulator instead";
            mGame->setNextLaunchCommand(standalone);
            mLaunchStandalone = true;
            mDone = true;
            return;
        }
    }
    LOG(LogError) << "GuiCoreDownloader: " << message;
    Window::getInstance()->queueInfoPopup(
        Utils::String::format(_("COULDN'T START %s: %s"), mDisplayName.c_str(),
                              Utils::String::toUpper(message).c_str()),
        6000);
    mDone = true;
}

void GuiCoreDownloader::finish()
{
    const std::string zipPath {CoreManager::coresDirectory() + "/" +
                               CoreManager::coreFileName(mCoreName) + ".zip"};
    Utils::FileSystem::createDirectory(CoreManager::coresDirectory());
    {
        const std::string content {mRequest->getContent()};
        std::ofstream file {zipPath, std::ios::binary | std::ios::trunc};
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!file) {
            fail("couldn't save the download");
            return;
        }
    }
    mRequest.reset();

    std::string error;
    if (!CoreManager::installFromZip(zipPath, mCoreName, error)) {
        fail(error);
        return;
    }
    CoreUpdates::recordInstalled(mCoreName);
    launchInstalledCore();
}

void GuiCoreDownloader::update(int deltaTime)
{
    // Input is blocked during the launch animation. Unblock it so the download can be cancelled.
    if (!mInputUnblocked) {
        Window::getInstance()->setBlockInput(false);
        mInputUnblocked = true;
    }

    if (mDone) {
        FileData* game {mGame};
        const bool launchStandalone {mLaunchStandalone};
        delete this;
        if (launchStandalone)
            ViewController::getInstance()->launch(game);
        return;
    }

    GuiComponent::update(deltaTime);

    if (mFtpThread.joinable()) {
        const int state {mFtpState};
        if (state == FTP_LOOKING)
            return;
        if (state == FTP_DOWNLOADING) {
            const long long total {mFtpTotal};
            if (total > 0) {
                const int percent {static_cast<int>(std::round(
                    100.0 * static_cast<double>(mFtpDownloaded) / static_cast<double>(total)))};
                mText->setText(Utils::String::format(
                    mUpdate ? _("UPDATING EMULATOR %s (%i%%)") :
                              _("DOWNLOADING EMULATOR %s (%i%%)"),
                    mDisplayName.c_str(), percent));
            }
            return;
        }
        finishFtp();
        return;
    }
    if (!mRequest)
        return;

    const HttpReq::Status status {mRequest->status()};
    if (status == HttpReq::REQ_IN_PROGRESS) {
        const long total {mRequest->getTotalBytes()};
        const long downloaded {mRequest->getDownloadedBytes()};
        if (total > 0) {
            const int percent {static_cast<int>(
                std::round(100.0 * static_cast<double>(downloaded) / static_cast<double>(total)))};
            mText->setText(Utils::String::format(mUpdate ? _("UPDATING EMULATOR %s (%i%%)") :
                                                           _("DOWNLOADING EMULATOR %s (%i%%)"),
                                                 mDisplayName.c_str(), percent));
        }
        return;
    }
    if (status == HttpReq::REQ_SUCCESS) {
        mText->setText(Utils::String::format(
            mUpdate ? _("UPDATING EMULATOR %s") : _("INSTALLING EMULATOR %s"),
            mDisplayName.c_str()));
        finish();
        return;
    }
    if (status == HttpReq::REQ_RESOURCE_NOT_FOUND)
        fail(_("emulator isn't available for this device"));
    else
        fail(_("network error") + std::string {": "} + mRequest->getErrorMsg());
}

void GuiCoreDownloader::render(const glm::mat4& parentTrans)
{
    Renderer* renderer {Renderer::getInstance()};
    const glm::mat4 trans {parentTrans * getTransform()};
    renderer->setMatrix(trans);
    // Dim the screen, then draw a rounded panel behind the text.
    renderer->drawRect(0.0f, 0.0f, mSize.x, mSize.y, 0x000000C0, 0x000000C0);
    renderer->drawRect(mSize.x * 0.18f, mSize.y * 0.38f, mSize.x * 0.64f, mSize.y * 0.24f,
                       mMenuColorFrame, mMenuColorFrame);
    GuiComponent::renderChildren(trans);
}
