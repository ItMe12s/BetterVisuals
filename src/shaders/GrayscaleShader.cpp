#include "GrayscaleShader.hpp"

#include "../render/PostProcessRenderer.hpp"

namespace aa::shaders::grayscale {

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
    vec2 uv = clamp(v_texCoord, 0.5 * u_invResolution, vec2(1.0) - 0.5 * u_invResolution);
    vec4 source = texture2D(u_texture, uv);
    float luminance = dot(source.rgb, vec3(0.2126, 0.7152, 0.0722));
    gl_FragColor = vec4(vec3(luminance), source.a);
}
)glsl";

} // namespace aa::shaders::grayscale

namespace aa::shaders {

    render::PostProcessShader const kGrayscaleShader{
        "Grayscale",
        grayscale::kVertexSource,
        grayscale::kFragmentSource,
    };

} // namespace aa::shaders
