#include "PixelateShader.hpp"

#include "../render/PostProcessRenderer.hpp"

/*
 * Based on the texture-coordinate snapping from:
 * https://github.com/genekogan/Processing-Shader-Examples/blob/master/TextureShaders/data/pixelate.glsl
 */

namespace aa::shaders::pixelate {

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

void main() {
    vec2 sourceUv = clamp(
        v_texCoord,
        0.5 * u_invResolution,
        vec2(1.0) - 0.5 * u_invResolution
    );
    vec4 source = texture2D(u_texture, sourceUv);
    vec2 pixel = v_texCoord / u_invResolution;
    vec2 uv = (floor(pixel / 4.0) * 4.0 + 2.0) * u_invResolution;
    uv = clamp(uv, 0.5 * u_invResolution, vec2(1.0) - 0.5 * u_invResolution);
    gl_FragColor = vec4(texture2D(u_texture, uv).rgb, source.a);
}
)glsl";

} // namespace aa::shaders::pixelate

namespace aa::shaders {

    render::PostProcessShader const kPixelateShader{
        "Pixelate",
        pixelate::kVertexSource,
        pixelate::kFragmentSource,
    };

} // namespace aa::shaders
