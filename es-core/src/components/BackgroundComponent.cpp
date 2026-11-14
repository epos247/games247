//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  BackgroundComponent.cpp
//
//  Displays a background frame with rounded corners.
//  Used by menus, popups etc.
//

#include "components/BackgroundComponent.h"

BackgroundComponent::BackgroundComponent(const glm::vec2 cornerSize)
    : mRenderer {Renderer::getInstance()}
    , mCornerSize {cornerSize}
    , mFrameColor {mMenuColorFrame}
{
}

void BackgroundComponent::fitTo(glm::vec2 size, glm::vec3 position, glm::vec2 padding)
{
    if (padding != glm::vec2 {0.0f, 0.0f}) {
        size += padding;
        position.x -= padding.x / 2.0f;
        position.y -= padding.y / 2.0f;
    }

    setSize(size);
    setPosition(position);
}

void BackgroundComponent::render(const glm::mat4& parentTrans)
{
    if (!isVisible())
        return;

    glm::mat4 trans {parentTrans * getTransform()};

    renderNeonPanel(mRenderer, trans, 0.0f, 0.0f, mSize.x, mSize.y,
                    mCornerSize.x * mRenderer->getScreenResolutionModifier(), mFrameColor, mOpacity);
}

void BackgroundComponent::renderNeonPanel(Renderer* renderer,
                                          const glm::mat4& trans,
                                          float x,
                                          float y,
                                          float w,
                                          float h,
                                          float cornerRadius,
                                          unsigned int fillColor,
                                          float opacity)
{
    const float scale {renderer->getScreenResolutionModifier()};
    const float alpha {opacity * static_cast<float>(fillColor & 0xFF) / 255.0f};
    if (alpha <= 0.0f)
        return;

    // Rounded corners are measured from the rectangle's own origin, so each layer is drawn
    // with the matrix moved to its top left corner.
    auto drawLayer = [&](float grow, unsigned int color, unsigned int colorEnd, bool horizontal) {
        renderer->setMatrix(glm::translate(trans, glm::vec3 {x - grow, y - grow, 0.0f}));
        renderer->drawRect(0.0f, 0.0f, w + grow * 2.0f, h + grow * 2.0f, color, colorEnd,
                           horizontal, alpha, 1.0f, Renderer::BlendFactor::SRC_ALPHA,
                           Renderer::BlendFactor::ONE_MINUS_SRC_ALPHA, cornerRadius + grow);
    };

    // Soft glow around the panel, widest and faintest first.
    const float glowSizes[] {12.0f, 7.0f, 4.0f};
    const unsigned int glowAlphas[] {0x10, 0x1C, 0x30};
    for (int i {0}; i < 3; ++i)
        drawLayer(glowSizes[i] * scale, (mMenuColorNeonEdgeStart & 0xFFFFFF00) | glowAlphas[i],
                  (mMenuColorNeonEdgeEnd & 0xFFFFFF00) | glowAlphas[i], true);

    // The neon edge, then the panel inside it.
    drawLayer(2.0f * scale, mMenuColorNeonEdgeStart, mMenuColorNeonEdgeEnd, true);
    drawLayer(0.0f, fillColor | 0xFF, mMenuColorFrameEnd | 0xFF, false);

    renderer->setMatrix(trans);
}
