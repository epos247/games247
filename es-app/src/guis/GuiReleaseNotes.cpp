//  SPDX-License-Identifier: MIT
//
//  games247
//  GuiReleaseNotes.cpp
//
//  The update prompt with release notes, see GuiReleaseNotes.h.
//

#include "guis/GuiReleaseNotes.h"

#include "components/ButtonComponent.h"
#include "components/MenuComponent.h"
#include "components/TextComponent.h"
#include "utils/LocalizationUtil.h"

#include <algorithm>

// The notes, clipped to the view and scrolled by an offset.
class GuiReleaseNotes::NotesView : public GuiComponent
{
public:
    explicit NotesView(const std::string& notes)
        : mScroll {0.0f}
    {
        mText = std::make_shared<TextComponent>(notes, Font::get(FONT_SIZE_SMALL),
                                                mMenuColorPrimary, ALIGN_LEFT, ALIGN_TOP,
                                                glm::ivec2 {0, 1});
        addChild(mText.get());
    }

    // The height of the notes when wrapped to this width.
    float textHeight(float width)
    {
        mText->setSize(width, 0.0f);
        return mText->getSize().y;
    }

    void scrollBy(float pixels)
    {
        const float maxScroll {std::max(0.0f, mText->getSize().y - mSize.y)};
        mScroll = std::clamp(mScroll + pixels, 0.0f, maxScroll);
        mText->setPosition(0.0f, -mScroll);
    }

    bool canScroll() const { return mText->getSize().y > mSize.y + 1.0f; }

    void onSizeChanged() override
    {
        mText->setSize(mSize.x, 0.0f);
        scrollBy(0.0f);
    }

    void render(const glm::mat4& parentTrans) override
    {
        const glm::mat4 trans {parentTrans * getTransform()};
        Renderer* renderer {Renderer::getInstance()};
        renderer->pushClipRect(
            glm::ivec2 {static_cast<int>(trans[3].x), static_cast<int>(trans[3].y)},
            glm::ivec2 {static_cast<int>(mSize.x), static_cast<int>(mSize.y)});
        renderChildren(trans);
        renderer->popClipRect();
    }

private:
    std::shared_ptr<TextComponent> mText;
    float mScroll;
};

GuiReleaseNotes::GuiReleaseNotes(const std::string& title,
                                 const std::string& notes,
                                 const std::function<void()>& updateFunc,
                                 const std::function<void()>& laterFunc)
    : mRenderer {Renderer::getInstance()}
    , mGrid {glm::ivec2 {1, 4}}
    , mLaterFunc {laterFunc}
    , mScrollDirection {0}
{
    mTitle = std::make_shared<TextComponent>(title, Font::get(FONT_SIZE_MEDIUM), mMenuColorTitle,
                                             ALIGN_CENTER, ALIGN_CENTER, glm::ivec2 {0, 1});
    mGrid.setEntry(mTitle, glm::ivec2 {0, 0}, false, false, glm::ivec2 {1, 1},
                   GridFlags::BORDER_BOTTOM);

    auto whatsNew = std::make_shared<TextComponent>(_("WHAT'S NEW"), Font::get(FONT_SIZE_MINI),
                                                    mMenuColorSecondary, ALIGN_LEFT);
    mGrid.setEntry(whatsNew, glm::ivec2 {0, 1}, false, false);

    mNotes = std::make_shared<NotesView>(notes);
    mGrid.setEntry(mNotes, glm::ivec2 {0, 2}, false, false);

    mButtons.push_back(std::make_shared<ButtonComponent>(
        _("UPDATE"), _("update"), [this, updateFunc] { close(updateFunc); }));
    mButtons.push_back(std::make_shared<ButtonComponent>(
        _("LATER"), _("later"), [this, laterFunc] { close(laterFunc); }));
    mButtonGrid = MenuComponent::makeButtonGrid(mButtons);
    mGrid.setEntry(mButtonGrid, glm::ivec2 {0, 3}, true, false, glm::ivec2 {1, 1},
                   GridFlags::BORDER_TOP);

    // Size: a fixed width, and as tall as the notes up to most of the screen.
    const float aspectValue {1.778f / mRenderer->getScreenAspectRatio()};
    const float width {std::floor(
        (mRenderer->getIsVerticalOrientation() ? 0.90f : std::min(0.60f * aspectValue, 0.90f)) *
        mRenderer->getScreenWidth())};
    const float padding {std::ceil(40.0f * mRenderer->getScreenWidthModifier())};
    const float innerWidth {width - padding * 2.0f};

    mTitle->setSize(innerWidth, 0.0f);
    const float titleHeight {mTitle->getSize().y * 1.6f};
    const float headerHeight {Font::get(FONT_SIZE_MINI)->getHeight() * 1.6f};
    const float buttonsHeight {mButtonGrid->getSize().y};
    const float maxNotesHeight {mRenderer->getScreenHeight() * 0.75f - titleHeight -
                                headerHeight - buttonsHeight};
    const float notesHeight {
        std::min(mNotes->textHeight(innerWidth), std::max(maxNotesHeight, 100.0f))};

    setSize(width, titleHeight + headerHeight + notesHeight + buttonsHeight + padding * 0.5f);
    setPosition((mRenderer->getScreenWidth() - mSize.x) / 2.0f,
                (mRenderer->getScreenHeight() - mSize.y) / 2.0f);

    // The grid centres each cell's component, so the text components are given the inner
    // width and end up inside the side padding.
    mTitle->setSize(innerWidth, titleHeight);
    whatsNew->setSize(innerWidth, headerHeight);
    mNotes->setSize(innerWidth, notesHeight);
    mGrid.setRowHeightPerc(0, titleHeight / mSize.y);
    mGrid.setRowHeightPerc(1, headerHeight / mSize.y);
    mGrid.setRowHeightPerc(3, buttonsHeight / mSize.y);
    mGrid.setSize(mSize);
    mBackground.fitTo(mSize);

    addChild(&mBackground);
    addChild(&mGrid);
    mGrid.setCursorTo(mButtonGrid);
}

void GuiReleaseNotes::close(const std::function<void()>& func)
{
    const std::function<void()> funcCopy {func};
    delete this;
    if (funcCopy)
        funcCopy();
}

bool GuiReleaseNotes::input(InputConfig* config, Input input)
{
    if (config->isMappedLike("up", input) || config->isMappedLike("down", input)) {
        if (input.value != 0)
            mScrollDirection = config->isMappedLike("up", input) ? -1 : 1;
        else
            mScrollDirection = 0;
        return true;
    }
    if (config->isMappedTo("b", input) && input.value != 0) {
        close(mLaterFunc);
        return true;
    }
    return GuiComponent::input(config, input);
}

void GuiReleaseNotes::update(int deltaTime)
{
    if (mScrollDirection != 0) {
        // About ten lines a second.
        const float speed {Font::get(FONT_SIZE_SMALL)->getHeight() * 1.5f * 10.0f};
        mNotes->scrollBy(mScrollDirection * speed * static_cast<float>(deltaTime) / 1000.0f);
    }
    GuiComponent::update(deltaTime);
}

void GuiReleaseNotes::render(const glm::mat4& parentTrans)
{
    GuiComponent::render(parentTrans);
}

std::vector<HelpPrompt> GuiReleaseNotes::getHelpPrompts()
{
    std::vector<HelpPrompt> prompts {mGrid.getHelpPrompts()};
    if (mNotes->canScroll())
        prompts.push_back(HelpPrompt("up/down", _("scroll")));
    prompts.push_back(HelpPrompt("b", _("later")));
    return prompts;
}
