//  SPDX-License-Identifier: MIT
//
//  games247
//  crt.glsl
//
//  CRT television look for the built-in game player: scanlines that get thicker on bright
//  pixels, an aperture-grille phosphor mask and a little glow. The picture stays flat and fills
//  its whole area, with no curved glass, rounded corners or dark edges.
//
//  texSize is the on-screen size of the picture in pixels. The game's own resolution is read
//  from the texture.
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

// 0 = no scanlines, 1 = full-strength scanlines.
const float SCANLINE_STRENGTH = 0.75;
// Darkening of the two phosphor stripes that aren't lit in each group of three.
const float MASK_STRENGTH = 0.20;
const float GLOW = 0.10;

vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }
vec3 toGamma(vec3 c) { return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2)); }

vec3 sampleLinear(vec2 uv) { return toLinear(texture(textureSampler0, uv).rgb); }

// Brightness of the electron beam at distance d (in source rows) from a scanline's centre.
// Bright pixels make a wider beam, as on a real tube.
float beam(float d, vec3 color)
{
    float width = mix(0.26, 0.46, clamp(max(color.r, max(color.g, color.b)), 0.0, 1.0));
    float x = d / width;
    return exp(-x * x);
}

void main()
{
    vec2 uv = texCoord;

    vec2 sourceSize = vec2(textureSize(textureSampler0, 0));

    // The two nearest source rows. Sampling at row centres keeps each row's colour intact
    // while the texture's linear filtering softens pixels horizontally, like a real beam.
    float row = uv.y * sourceSize.y - 0.5;
    float baseRow = floor(row);
    float f = row - baseRow;
    vec3 c0 = sampleLinear(vec2(uv.x, (baseRow + 0.5) / sourceSize.y));
    vec3 c1 = sampleLinear(vec2(uv.x, (baseRow + 1.5) / sourceSize.y));
    vec3 scanlined = c0 * beam(f, c0) + c1 * beam(1.0 - f, c1);
    // The beam profile averages out at about 0.6, so lift it back up.
    scanlined *= 1.65;
    vec3 plain = mix(c0, c1, f);

    // Scanlines need a few screen pixels per game line, otherwise they shimmer. Fade them out
    // for high-resolution games.
    float pixelsPerLine = texSize.y / sourceSize.y;
    float strength = SCANLINE_STRENGTH * smoothstep(1.5, 3.0, pixelsPerLine);
    vec3 color = mix(plain, scanlined, strength);

    // Glow: a little light bleeding from neighbouring pixels.
    vec2 texel = 1.0 / sourceSize;
    vec3 glow = sampleLinear(uv + vec2(texel.x * 1.5, 0.0)) +
                sampleLinear(uv - vec2(texel.x * 1.5, 0.0)) +
                sampleLinear(uv + vec2(0.0, texel.y * 1.5)) +
                sampleLinear(uv - vec2(0.0, texel.y * 1.5));
    color += glow * 0.25 * GLOW;

    // Aperture-grille mask, aligned to the physical screen pixels.
    float stripe = mod(floor(gl_FragCoord.x), 3.0);
    vec3 mask = vec3(1.0 - MASK_STRENGTH);
    if (stripe < 0.5)
        mask.r = 1.0;
    else if (stripe < 1.5)
        mask.g = 1.0;
    else
        mask.b = 1.0;
    color *= mask / (1.0 - MASK_STRENGTH * 2.0 / 3.0);

    FragColor = vec4(toGamma(color), opacity);
}
#endif
