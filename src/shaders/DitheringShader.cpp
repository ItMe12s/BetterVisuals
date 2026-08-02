#include "DitheringShader.hpp"

#include "../render/PostProcessRenderer.hpp"

/*
 * Ordered-dither method referenced from:
 * https://github.com/paper-design/shaders/blob/main/packages/shaders/src/shaders/dithering.ts
 */

namespace aa::shaders::dithering {

    constexpr char kVertexSource[] = R"glsl(#version 120
attribute vec2 a_position;
attribute vec2 a_texCoord;
varying vec2 v_texCoord;

void main() {
    v_texCoord = a_texCoord;
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)glsl";

    constexpr char kFragmentSource[] = R"glsl(#version 120
uniform sampler2D u_texture;
uniform vec2 u_invResolution;
varying vec2 v_texCoord;

float bayer2(vec2 cell) {
    if (cell.y < 1.0) {
        return cell.x < 1.0 ? 0.0 : 2.0;
    }
    return cell.x < 1.0 ? 3.0 : 1.0;
}

float bayer4Value(vec2 cell) {
    cell = mod(floor(cell), 4.0);
    return 4.0 * bayer2(mod(cell, 2.0)) + bayer2(floor(cell / 2.0));
}

float bayer8(vec2 pixel) {
    vec2 cell = mod(floor(pixel), 8.0);
    float value = 4.0 * bayer4Value(mod(cell, 4.0)) + bayer2(floor(cell / 4.0));
    return (value + 0.5) / 64.0;
}

void main() {
    vec2 uv = clamp(v_texCoord, 0.5 * u_invResolution, vec2(1.0) - 0.5 * u_invResolution);
    vec4 source = texture2D(u_texture, uv);
    float threshold = bayer8(v_texCoord / u_invResolution);
    vec3 scaled = source.rgb * 7.0;
    vec3 color = (floor(scaled) + step(vec3(threshold), fract(scaled))) / 7.0;
    gl_FragColor = vec4(clamp(color, 0.0, 1.0), source.a);
}
)glsl";

} // namespace aa::shaders::dithering

namespace aa::shaders {

    render::PostProcessShader const kDitheringShader{
        "Dithering Filter",
        dithering::kVertexSource,
        dithering::kFragmentSource,
    };

} // namespace aa::shaders
