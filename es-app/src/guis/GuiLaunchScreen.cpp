//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GuiLaunchScreen.cpp
//
//  Screen shown when launching a game.
//
//  games247: a full-screen retro "now loading" screen instead of the original dialog box. The
//  screen darkens over a fine scanline pattern, a neon line sweeps out from the middle, the
//  game's marquee and name fade in with a neon glow and NOW LOADING blinks below.
//

#include "guis/GuiLaunchScreen.h"

#include "FileData.h"
#include "SystemData.h"
#include "components/TextComponent.h"
#include "utils/LocalizationUtil.h"
#include "utils/StringUtil.h"

#include <cmath>

namespace
{
    const std::string TITLE_FONT {":/fonts/Orbitron.ttf"};
    const std::string LOADING_FONT {":/fonts/Monoton-Regular.ttf"};
    constexpr unsigned int PINK {0xFF3D8B};
    constexpr unsigned int CYAN {0x2DE2E6};
    constexpr unsigned int WHITE {0xF4ECFF};
    constexpr unsigned int YELLOW {0xFFF35C};

    unsigned int rgba(unsigned int rgb, float alpha)
    {
        return (rgb << 8) | static_cast<unsigned int>(glm::clamp(alpha, 0.0f, 1.0f) * 255.0f);
    }

    // 0 before start, 1 after start + length, smoothly in between.
    float phase(int elapsed, int start, int length)
    {
        const float t {glm::clamp(static_cast<float>(elapsed - start) / length, 0.0f, 1.0f)};
        return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    }
} // namespace

GuiLaunchScreen::GuiLaunchScreen()
    : mRenderer {Renderer::getInstance()}
    , mMarquee {nullptr}
    , mElapsed {0}
{
    mWindow->setLaunchScreen(this);
}

GuiLaunchScreen::~GuiLaunchScreen()
{
    // This only executes when exiting the application.
    closeLaunchScreen();
}

std::shared_ptr<TextComponent> GuiLaunchScreen::makeText(const std::string& text,
                                                         const std::string& fontPath,
                                                         float fontSize,
                                                         unsigned int color,
                                                         float maxWidth)
{
    std::shared_ptr<Font> font {Font::get(fontSize, fontPath)};
    const float width {font->sizeText(text).x};
    if (width > maxWidth)
        font = Font::get(std::max(fontSize * maxWidth / width, fontSize * 0.45f), fontPath);
    auto component = std::make_shared<TextComponent>(
        text, font, rgba(color, 1.0f), ALIGN_CENTER, ALIGN_CENTER, glm::ivec2 {0, 0},
        glm::vec3 {}, glm::vec2 {mRenderer->getScreenWidth(), font->getHeight()});
    component->setOrigin(0.0f, 0.5f);
    return component;
}

void GuiLaunchScreen::displayLaunchScreen(FileData* game)
{
    mElapsed = 0;
    const float screenWidth {mRenderer->getScreenWidth()};
    const float screenHeight {mRenderer->getScreenHeight()};
    const float base {std::min(screenWidth, screenHeight)};
    setSize(screenWidth, screenHeight);

    mGameName = makeText(Utils::String::toUpper(game->getName()), TITLE_FONT, base * 0.075f,
                         WHITE, screenWidth * 0.88f);
    mSystemName = makeText(Utils::String::toUpper(game->getSystem()->getFullName()), TITLE_FONT,
                           base * 0.034f, CYAN, screenWidth * 0.8f);
    mTitle = makeText(_("NOW LOADING"), LOADING_FONT, base * 0.058f, PINK, screenWidth * 0.8f);

    mImagePath = game->getMarqueePath();
    if (mImagePath != "") {
        // The image may be cached at a modified resolution, which would give the wrong size.
        TextureResource::manualUnload(mImagePath, false);
        mMarquee = new ImageComponent;
        mMarquee->setLinearInterpolation(true);
        mMarquee->setImage(mImagePath, false);
        mMarquee->cropTransparentPadding(screenWidth * 0.42f, screenHeight * 0.2f);
        mMarquee->setOrigin(0.5f, 0.5f);
        mMarquee->setPosition(screenWidth / 2.0f, screenHeight * 0.26f);
    }
}

void GuiLaunchScreen::closeLaunchScreen()
{
    mGameName.reset();
    mSystemName.reset();
    mTitle.reset();

    if (mMarquee) {
        delete mMarquee;
        mMarquee = nullptr;
    }

    // An extra precaution.
    if (mImagePath != "") {
        TextureResource::manualUnload(mImagePath, false);
        mImagePath = "";
    }
}

void GuiLaunchScreen::onSizeChanged() {}

void GuiLaunchScreen::update(int deltaTime)
{
    mElapsed += deltaTime;
}

void GuiLaunchScreen::renderText(TextComponent& text,
                                 float y,
                                 unsigned int textColor,
                                 unsigned int glowColor,
                                 float opacity)
{
    // A cheap neon glow: the text drawn a few times, slightly offset, in the glow colour.
    const float spread {std::max(1.5f, mRenderer->getScreenHeight() * 0.0025f)};
    text.setColor(rgba(glowColor, 1.0f));
    text.setOpacity(opacity * 0.35f);
    const float offsets[4][2] {{-spread, 0}, {spread, 0}, {0, -spread}, {0, spread}};
    for (const auto& offset : offsets) {
        text.setPosition(offset[0], y + offset[1]);
        text.render(Renderer::getIdentity());
    }
    text.setColor(rgba(textColor, 1.0f));
    text.setOpacity(opacity);
    text.setPosition(0.0f, y);
    text.render(Renderer::getIdentity());
}

void GuiLaunchScreen::render(const glm::mat4&)
{
    if (!mGameName)
        return;

    const float width {mRenderer->getScreenWidth()};
    const float height {mRenderer->getScreenHeight()};
    mRenderer->setMatrix(Renderer::getIdentity());

    // Darken whatever was on screen, then lay a scanline pattern over it.
    const float dark {phase(mElapsed, 0, 260)};
    mRenderer->drawRect(0.0f, 0.0f, width, height, rgba(0x05010F, 0.97f * dark),
                        rgba(0x1A0533, 0.97f * dark));
    const float lineStep {std::max(3.0f, std::round(height / 270.0f))};
    for (float y {0.0f}; y < height; y += lineStep)
        mRenderer->drawRect(0.0f, y, width, lineStep / 3.0f, rgba(0x000000, 0.28f * dark),
                            rgba(0x000000, 0.28f * dark));

    // A light band rolling down the screen, as on an old TV.
    const float roll {std::fmod(static_cast<float>(mElapsed) / 2600.0f, 1.0f)};
    mRenderer->drawRect(0.0f, roll * height * 1.2f - height * 0.1f, width, height * 0.1f,
                        rgba(0xFFFFFF, 0.0f), rgba(0xFFFFFF, 0.035f * dark));

    // The neon line sweeping out from the middle, with a magenta and cyan glow.
    const float lineY {height * 0.56f};
    const float sweep {phase(mElapsed, 80, 380)};
    const float lineWidth {width * 0.72f * sweep};
    const float lineX {(width - lineWidth) / 2.0f};
    const float thickness {std::max(2.0f, height * 0.004f)};
    const float glow {height * 0.025f};
    mRenderer->drawRect(lineX, lineY - glow, lineWidth, glow, rgba(PINK, 0.0f),
                        rgba(PINK, 0.45f), false);
    mRenderer->drawRect(lineX, lineY + thickness, lineWidth, glow, rgba(CYAN, 0.45f),
                        rgba(CYAN, 0.0f), false);
    mRenderer->drawRect(lineX, lineY, lineWidth, thickness, rgba(PINK, 1.0f), rgba(CYAN, 1.0f),
                        true);

    // The marquee, game name and system name fade in after the line.
    const float reveal {phase(mElapsed, 220, 360)};
    if (mMarquee) {
        mMarquee->setOpacity(reveal);
        mMarquee->render(Renderer::getIdentity());
    }
    const float rise {(1.0f - reveal) * height * 0.03f};
    renderText(*mGameName, lineY - height * 0.075f + rise, WHITE, PINK, reveal);
    renderText(*mSystemName, lineY + height * 0.07f - rise, CYAN, CYAN, reveal);

    // NOW LOADING blinks below, with a little yellow accent bar under it that keeps sliding.
    const float blink {0.55f + 0.45f * std::sin(static_cast<float>(mElapsed) / 160.0f)};
    const float loadingIn {phase(mElapsed, 380, 260)};
    renderText(*mTitle, height * 0.80f, PINK, PINK, loadingIn * blink);
    const float barWidth {width * 0.08f};
    const float travel {std::fmod(static_cast<float>(mElapsed) / 1100.0f, 1.0f)};
    const float trackWidth {width * 0.3f};
    const float trackX {(width - trackWidth) / 2.0f};
    mRenderer->drawRect(trackX, height * 0.865f, trackWidth, thickness,
                        rgba(0xFFFFFF, 0.12f * loadingIn), rgba(0xFFFFFF, 0.12f * loadingIn));
    mRenderer->drawRect(trackX + (trackWidth - barWidth) * travel, height * 0.865f, barWidth,
                        thickness, rgba(YELLOW, loadingIn), rgba(YELLOW, loadingIn));
    mRenderer->setMatrix(Renderer::getIdentity());
}
