//  SPDX-License-Identifier: MIT
//
//  games247
//  GuiCoreDownloader.cpp
//
//  Downloads a missing libretro core, then starts the game in the built-in player.
//

#include "guis/GuiCoreDownloader.h"

#include "InputConfig.h"
#include "Log.h"
#include "Window.h"
#include "guis/GuiLibretroPlayer.h"
#include "libretro/CoreManager.h"
#include "renderers/Renderer.h"
#include "resources/Font.h"
#include "utils/FileSystemUtil.h"
#include "utils/LocalizationUtil.h"
#include "utils/StringUtil.h"

#include <cmath>
#include <fstream>

GuiCoreDownloader::GuiCoreDownloader(const std::string& coreName,
                                     const std::string& romPath,
                                     FileData* game)
    : mCoreName {coreName}
    , mRomPath {romPath}
    , mGame {game}
    , mInputUnblocked {false}
    , mDone {false}
{
    // "vice_x64sc_libretro" -> "VICE X64SC"
    std::string name {Utils::String::replace(coreName, "_libretro", "")};
    mDisplayName = Utils::String::toUpper(Utils::String::replace(name, "_", " "));

    Renderer* renderer {Renderer::getInstance()};
    setSize(renderer->getScreenWidth(), renderer->getScreenHeight());

    mText = std::make_shared<TextComponent>("", Font::get(FONT_SIZE_MEDIUM), mMenuColorTitle,
                                            ALIGN_CENTER, ALIGN_CENTER);
    mText->setSize(mSize.x * 0.6f, mSize.y * 0.2f);
    mText->setPosition(mSize.x * 0.2f, mSize.y * 0.4f);
    addChild(mText.get());

    const std::string url {CoreManager::downloadURL(coreName)};
    LOG(LogInfo) << "GuiCoreDownloader: downloading " << url;
    mRequest = std::make_unique<HttpReq>(url, false);
    mText->setText(Utils::String::format(_("DOWNLOADING EMULATOR %s"), mDisplayName.c_str()));
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
    }
    return true;
}

void GuiCoreDownloader::fail(const std::string& message)
{
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

    if (!GuiLibretroPlayer::launch(CoreManager::findInstalledCore(mCoreName), mRomPath, mGame,
                                   error)) {
        fail(error);
        return;
    }
    mDone = true;
}

void GuiCoreDownloader::update(int deltaTime)
{
    // Input is blocked during the launch animation. Unblock it so the download can be cancelled.
    if (!mInputUnblocked) {
        Window::getInstance()->setBlockInput(false);
        mInputUnblocked = true;
    }

    if (mDone) {
        delete this;
        return;
    }

    GuiComponent::update(deltaTime);

    const HttpReq::Status status {mRequest->status()};
    if (status == HttpReq::REQ_IN_PROGRESS) {
        const long total {mRequest->getTotalBytes()};
        const long downloaded {mRequest->getDownloadedBytes()};
        if (total > 0) {
            const int percent {static_cast<int>(
                std::round(100.0 * static_cast<double>(downloaded) / static_cast<double>(total)))};
            mText->setText(Utils::String::format(_("DOWNLOADING EMULATOR %s (%i%%)"),
                                                 mDisplayName.c_str(), percent));
        }
        return;
    }
    if (status == HttpReq::REQ_SUCCESS) {
        mText->setText(Utils::String::format(_("INSTALLING EMULATOR %s"), mDisplayName.c_str()));
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
                       mMenuColorFrame, mMenuColorFrame, false, 1.0f, 1.0f,
                       Renderer::BlendFactor::SRC_ALPHA, Renderer::BlendFactor::ONE_MINUS_SRC_ALPHA,
                       mSize.y * 0.02f);
    GuiComponent::renderChildren(trans);
}
