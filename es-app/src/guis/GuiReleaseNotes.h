//  SPDX-License-Identifier: MIT
//
//  games247
//  GuiReleaseNotes.h
//
//  The update prompt when the release feed has release notes: what's new in each release
//  since the installed one, scrolled with up and down, and the update button.
//

#ifndef GAMES247_GUI_RELEASE_NOTES_H
#define GAMES247_GUI_RELEASE_NOTES_H

#include "GuiComponent.h"
#include "components/BackgroundComponent.h"
#include "components/ComponentGrid.h"

#include <functional>

class ButtonComponent;
class TextComponent;

class GuiReleaseNotes : public GuiComponent
{
public:
    GuiReleaseNotes(const std::string& title,
                    const std::string& notes,
                    const std::function<void()>& updateFunc,
                    const std::function<void()>& laterFunc);

    bool input(InputConfig* config, Input input) override;
    void update(int deltaTime) override;
    void render(const glm::mat4& parentTrans) override;
    std::vector<HelpPrompt> getHelpPrompts() override;

private:
    class NotesView;

    void close(const std::function<void()>& func);

    Renderer* mRenderer;
    BackgroundComponent mBackground;
    ComponentGrid mGrid;
    std::shared_ptr<TextComponent> mTitle;
    std::shared_ptr<NotesView> mNotes;
    std::vector<std::shared_ptr<ButtonComponent>> mButtons;
    std::shared_ptr<ComponentGrid> mButtonGrid;
    std::function<void()> mLaterFunc;
    // -1 while up is held, 1 while down is held.
    int mScrollDirection;
};

#endif // GAMES247_GUI_RELEASE_NOTES_H
