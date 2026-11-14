//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  BackgroundComponent.h
//
//  Displays a background frame with rounded corners.
//  Used by menus, popups etc.
//

#ifndef ES_CORE_COMPONENTS_BACKGROUND_COMPONENT_H
#define ES_CORE_COMPONENTS_BACKGROUND_COMPONENT_H

#include "GuiComponent.h"
#include "renderers/Renderer.h"

class BackgroundComponent : public GuiComponent
{
public:
    BackgroundComponent(const glm::vec2 cornerSize = glm::vec2 {30.0f, 30.0f});

    void fitTo(glm::vec2 size,
               glm::vec3 position = {0.0f, 0.0f, 0.0f},
               glm::vec2 padding = {0.0f, 0.0f});
    void setFrameColor(unsigned int frameColor) { mFrameColor = frameColor; }

    void render(const glm::mat4& parentTrans) override;

    // games247: a Neon panel (dark gradient, pink-to-cyan edge and a soft glow) in the area
    // x, y, w, h of trans. The fill's alpha also fades the edge and glow. Leaves the renderer's
    // matrix set to trans.
    static void renderNeonPanel(Renderer* renderer,
                                const glm::mat4& trans,
                                float x,
                                float y,
                                float w,
                                float h,
                                float cornerRadius,
                                unsigned int fillColor,
                                float opacity);

private:
    Renderer* mRenderer;
    glm::vec2 mCornerSize;
    unsigned int mFrameColor;
};

#endif // ES_CORE_COMPONENTS_BACKGROUND_COMPONENT_H
