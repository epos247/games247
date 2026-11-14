//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GuiLaunchScreen.h
//
//  Screen shown when launching a game.
//

#ifndef ES_APP_GUIS_GUI_LAUNCH_SCREEN_H
#define ES_APP_GUIS_GUI_LAUNCH_SCREEN_H

#include "GuiComponent.h"
#include "Window.h"
#include "components/BackgroundComponent.h"
#include "components/ComponentGrid.h"
#include "components/ImageComponent.h"
#include "components/TextComponent.h"

class FileData;

class GuiLaunchScreen : public Window::GuiLaunchScreen, GuiComponent
{
public:
    GuiLaunchScreen();
    virtual ~GuiLaunchScreen();

    void displayLaunchScreen(FileData* game) override;
    void closeLaunchScreen() override;

    void onSizeChanged() override;

    void update(int deltaTime) override;
    void render(const glm::mat4& parentTrans) override;

private:
    // Fits a centred line of text to maxWidth, shrinking the font if needed.
    std::shared_ptr<TextComponent> makeText(const std::string& text,
                                            const std::string& fontPath,
                                            float fontSize,
                                            unsigned int color,
                                            float maxWidth);
    void renderText(TextComponent& text,
                    float y,
                    unsigned int textColor,
                    unsigned int glowColor,
                    float opacity);

    Renderer* mRenderer;

    std::shared_ptr<TextComponent> mTitle;
    std::shared_ptr<TextComponent> mGameName;
    std::shared_ptr<TextComponent> mSystemName;

    ImageComponent* mMarquee;
    std::string mImagePath;

    // Milliseconds since the launch screen opened, which drives the animation.
    int mElapsed;
};

#endif // ES_APP_GUIS_GUI_LAUNCH_SCREEN_H
