//  SPDX-License-Identifier: MIT
//
//  games247
//  GuiCoreDownloader.h
//
//  Shown the first time a game needs a libretro core that isn't installed. Downloads the core
//  from the libretro build server, installs it, then starts the game in the built-in player.
//  B / Escape cancels. Also updates an installed core to the server's newer build before its
//  game starts (see CoreUpdates.h); if that fails or is cancelled, the game starts with the
//  core it has.
//

#ifndef GAMES247_GUI_CORE_DOWNLOADER_H
#define GAMES247_GUI_CORE_DOWNLOADER_H

#include "GuiComponent.h"
#include "HttpReq.h"
#include "components/TextComponent.h"

#include <memory>

class FileData;

class GuiCoreDownloader : public GuiComponent
{
public:
    GuiCoreDownloader(const std::string& coreName,
                      const std::string& romPath,
                      FileData* game,
                      bool update = false);

    bool input(InputConfig* config, Input input) override;
    void update(int deltaTime) override;
    void render(const glm::mat4& parentTrans) override;
    std::vector<HelpPrompt> getHelpPrompts() override;

private:
    void finish();
    void fail(const std::string& message);
    // Update mode: starts the game with the core already installed.
    void startWithInstalledCore();

    std::string mCoreName;
    std::string mRomPath;
    FileData* mGame;
    std::string mDisplayName;

    std::unique_ptr<HttpReq> mRequest;
    std::shared_ptr<TextComponent> mText;
    bool mInputUnblocked;
    bool mDone;
    bool mUpdate;
};

#endif // GAMES247_GUI_CORE_DOWNLOADER_H
