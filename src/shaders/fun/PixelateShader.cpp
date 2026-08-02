#include "PixelateShader.hpp"

#include "../../render/PostProcessRenderer.hpp"

/*
 * Based on the texture-coordinate snapping from:
 * https://github.com/genekogan/Processing-Shader-Examples/blob/master/TextureShaders/data/pixelate.glsl
 */

namespace bv::shaders::pixelate {

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

void main() {
    vec2 sourceUv = clamp(
        v_texCoord,
        0.5 * u_invResolution,
        vec2(1.0) - 0.5 * u_invResolution
    );
    vec4 source = texture2D(u_texture, sourceUv);
    float displayScale = (1.0 / u_invResolution.y) / 1080.0;
    float blockSize = max(floor(4.0 * displayScale + 0.5), 1.0);
    vec2 pixel = v_texCoord / u_invResolution;
    vec2 uv = (floor(pixel / blockSize) + 0.5) * blockSize * u_invResolution;
    uv = clamp(uv, 0.5 * u_invResolution, vec2(1.0) - 0.5 * u_invResolution);
    gl_FragColor = vec4(texture2D(u_texture, uv).rgb, source.a);
}
)glsl";

} // namespace bv::shaders::pixelate

namespace bv::shaders {

    render::PostProcessShader const kPixelateShader{
        "Pixelate",
        pixelate::kVertexSource,
        pixelate::kFragmentSource,
    };

} // namespace bv::shaders
