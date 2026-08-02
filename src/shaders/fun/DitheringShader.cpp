#include "DitheringShader.hpp"

#include "../../render/PostProcessRenderer.hpp"

/*
 * Ordered-dither method referenced from:
 * https://github.com/paper-design/shaders/blob/main/packages/shaders/src/shaders/dithering.ts
 */

namespace bv::shaders::dithering {

    constexpr char kVertexSource[] = R"glsl(
attribute vec2 a_position;
attribute vec2 a_texCoord;
varying vec2 v_texCoord;

void main() {
    v_texCoord = a_texCoord;
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)glsl";

    constexpr char kFragmentSource[] = R"glsl(
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

void main() {
    float displayScale = (1.0 / u_invResolution.y) / 1080.0;
    float cellSize = max(floor(4.0 * displayScale + 0.5), 1.0);
    vec2 cell = floor((v_texCoord / u_invResolution) / cellSize);
    vec2 uv = (cell + 0.5) * cellSize * u_invResolution;
    uv = clamp(uv, 0.5 * u_invResolution, vec2(1.0) - 0.5 * u_invResolution);
    vec4 source = texture2D(u_texture, uv);
    float threshold = (bayer4Value(cell) + 0.5) / 16.0;
    vec3 channelMax = vec3(7.0, 7.0, 3.0);
    vec3 scaled = source.rgb * channelMax;
    vec3 color = (floor(scaled) + step(vec3(threshold), fract(scaled))) / channelMax;
    gl_FragColor = vec4(clamp(color, 0.0, 1.0), source.a);
}
)glsl";

} // namespace bv::shaders::dithering

namespace bv::shaders {

    render::PostProcessShader const kDitheringShader{
        "Dithering Filter",
        dithering::kVertexSource,
        dithering::kFragmentSource,
    };

} // namespace bv::shaders
