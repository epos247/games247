//  SPDX-License-Identifier: MIT
//
//  games247
//  lcd.glsl
//
//  Handheld LCD look for the built-in game player (Game Boy, Game Gear, Lynx and the like):
//  - Sharp pixels with smooth edges at any size (each game pixel is one LCD cell).
//  - A thin grid between the cells, which fades out when the cells are too small to show it.
//  - RGB subpixel stripes inside each cell on colour screens, when the cells are big enough.
//  - The colours of the console's own screen (shaderFlags: see the LCD_* values below).
//  Motion blur (LCD ghosting) is done by the player before the frame is uploaded.
//
//  texSize is the on-screen size of the picture in pixels. The game's own resolution is read
//  from the texture, which must use linear filtering.
//

// Vertex section of code:
#if defined(VERTEX)

#ifdef GL_ES
precision highp float;
#endif

uniform mat4 MVPMatrix;
in vec2 positionVertex;
in vec2 texCoordVertex;
in vec4 colorVertex;

out vec2 texCoord;

void main()
{
    gl_Position = MVPMatrix * vec4(positionVertex, 0.0, 1.0);
    texCoord = texCoordVertex;
}

// Fragment section of code:
#elif defined(FRAGMENT)

#ifdef GL_ES
precision highp float;
#endif

uniform vec2 texSize;
uniform float opacity;
uniform uint shaderFlags;
uniform sampler2D textureSampler0;
in vec2 texCoord;
out vec4 FragColor;

// Screen colour profiles (Renderer::ShaderFlags).
const uint LCD_GBA = 0x00000100u;
const uint LCD_GBC = 0x00000200u;
const uint LCD_NDS = 0x00000400u;
const uint LCD_PASSIVE = 0x00000800u;
// A black and white screen: no colour subpixels.
const uint LCD_MONO = 0x00001000u;

// Width of the gap between cells, as a part of a cell, and how dark it is.
const float GRID_WIDTH = 0.14;
const float GRID_STRENGTH = 0.50;
// How much the two unlit subpixel stripes of each cell are dimmed.
const float SUBPIXEL_STRENGTH = 0.25;

vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }
vec3 toGamma(vec3 c) { return pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2)); }

// Each profile mixes the game's colours the way that console's screen showed them (its
// backlight and colour filters), in linear light. Columns are the input red, green and blue.
vec3 screenColors(vec3 c)
{
    if ((shaderFlags & LCD_GBA) != 0u) {
        // Game Boy Advance: dark, washed-out colours. Games were made bright and saturated
        // to make up for that, so they look right again here.
        c = pow(c, vec3(1.15));
        c = mat3(0.82, 0.125, 0.195, 0.24, 0.665, 0.075, -0.06, 0.21, 0.73) * c;
        c *= 0.94;
    }
    else if ((shaderFlags & LCD_GBC) != 0u) {
        // Game Boy Color: softer colours, greens leaning towards blue.
        c = mat3(0.80, 0.03, 0.12, 0.12, 0.75, 0.10, 0.00, 0.22, 0.78) * c;
        c *= 0.95;
    }
    else if ((shaderFlags & LCD_NDS) != 0u) {
        // Nintendo DS: close to accurate, a little less saturated.
        c = mat3(0.90, 0.05, 0.02, 0.10, 0.88, 0.08, 0.00, 0.07, 0.90) * c;
    }
    else if ((shaderFlags & LCD_PASSIVE) != 0u) {
        // Early colour LCDs (Game Gear, Lynx, Neo Geo Pocket Color, WonderSwan Color):
        // a little less saturated, and black isn't quite black.
        float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
        c = mix(c, vec3(luma), 0.12);
        c = mix(vec3(0.004), vec3(0.97), c);
    }
    return max(c, vec3(0.0));
}

void main()
{
    vec2 sourceSize = vec2(textureSize(textureSampler0, 0));
    vec2 pos = texCoord * sourceSize;
    // Screen pixels per game pixel.
    vec2 scale = 1.0 / max(fwidth(pos), vec2(0.0001));

    // Sharp pixels: sample each cell's centre, blending into the neighbour only across the
    // last screen pixel before a cell edge so edges don't shimmer at non-integer sizes.
    vec2 edgePos = floor(pos + 0.5);
    vec2 offset = clamp((pos - edgePos) * scale, -0.5, 0.5);
    vec3 color = toLinear(texture(textureSampler0, (edgePos + offset) / sourceSize).rgb);
    color = screenColors(color);

    vec2 cell = fract(pos);

    // Grid: distance to the nearest cell edge in screen pixels, with the gap antialiased.
    vec2 edgeDistance = min(cell, 1.0 - cell) * scale;
    vec2 halfGap = 0.5 * GRID_WIDTH * scale;
    vec2 lit = smoothstep(halfGap - 0.5, halfGap + 0.5, edgeDistance);
    float gridStrength = GRID_STRENGTH * smoothstep(2.5, 4.5, min(scale.x, scale.y));
    float grid = mix(1.0 - gridStrength, 1.0, lit.x * lit.y);
    // Keep the overall brightness about the same.
    float gapArea = 1.0 - (1.0 - GRID_WIDTH) * (1.0 - GRID_WIDTH);
    grid /= 1.0 - gridStrength * gapArea * 0.8;

    // Subpixels: red, green and blue stripes across each cell, on colour screens.
    float subStrength = (shaderFlags & LCD_MONO) != 0u ?
                            0.0 :
                            SUBPIXEL_STRENGTH * smoothstep(6.0, 10.0, scale.x);
    float aa = 0.5 / scale.x;
    float red = 1.0 - smoothstep(1.0 / 3.0 - aa, 1.0 / 3.0 + aa, cell.x);
    float blue = smoothstep(2.0 / 3.0 - aa, 2.0 / 3.0 + aa, cell.x);
    vec3 stripes = vec3(red, 1.0 - red - blue, blue);
    vec3 subpixels = mix(vec3(1.0 - subStrength), vec3(1.0), stripes);
    subpixels /= 1.0 - subStrength * 2.0 / 3.0;

    // Bright colours can go over full brightness after making up for the grid. Scale them
    // back as a whole, so they don't change colour where one channel would be cut off.
    color *= grid * subpixels;
    color /= max(1.0, max(color.r, max(color.g, color.b)));

    FragColor = vec4(toGamma(color), opacity);
}
#endif
