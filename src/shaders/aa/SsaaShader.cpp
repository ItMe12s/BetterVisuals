#include "../../render/PostProcessRenderer.hpp"
#include "../PostProcessShaders.hpp"

namespace bv::shaders::ssaa {

    constexpr char kFragmentSource[] = R"glsl(
uniform sampler2D u_texture;
uniform vec2 u_invResolution;
varying vec2 v_texCoord;

void main() {
    vec2 halfTexel = 0.5 * u_invResolution;
    vec2 uv = clamp(v_texCoord, halfTexel, vec2(1.0) - halfTexel);
    vec4 color =
        texture2D(u_texture, uv + vec2(-halfTexel.x, -halfTexel.y)) +
        texture2D(u_texture, uv + vec2(halfTexel.x, -halfTexel.y)) +
        texture2D(u_texture, uv + vec2(-halfTexel.x, halfTexel.y)) +
        texture2D(u_texture, uv + vec2(halfTexel.x, halfTexel.y));
    gl_FragColor = 0.25 * color;
}
)glsl";

    constexpr char k3xFragmentSource[] = R"glsl(
uniform sampler2D u_texture;
uniform vec2 u_invResolution;
varying vec2 v_texCoord;

void main() {
    vec2 uv = clamp(v_texCoord, u_invResolution, vec2(1.0) - u_invResolution);
    vec2 texel = u_invResolution;
    vec4 color =
        texture2D(u_texture, uv + vec2(-texel.x, -texel.y)) +
        texture2D(u_texture, uv + vec2(0.0, -texel.y)) +
        texture2D(u_texture, uv + vec2(texel.x, -texel.y)) +
        texture2D(u_texture, uv + vec2(-texel.x, 0.0)) +
        texture2D(u_texture, uv) +
        texture2D(u_texture, uv + vec2(texel.x, 0.0)) +
        texture2D(u_texture, uv + vec2(-texel.x, texel.y)) +
        texture2D(u_texture, uv + vec2(0.0, texel.y)) +
        texture2D(u_texture, uv + vec2(texel.x, texel.y));
    gl_FragColor = color / 9.0;
}
)glsl";

} // namespace bv::shaders::ssaa

namespace bv::shaders {

    render::PostProcessShader const kSsaaShader{
        "SSAA Downsample",
        kFullscreenVertexSource,
        ssaa::kFragmentSource,
    };

    render::PostProcessShader const kSsaa3xShader{
        "SSAA 3x Downsample",
        kFullscreenVertexSource,
        ssaa::k3xFragmentSource,
    };

} // namespace bv::shaders