#include "../../render/PostProcessRenderer.hpp"
#include "../PostProcessShaders.hpp"

namespace bv::shaders::invert {

    constexpr char kFragmentSource[] = R"glsl(
uniform sampler2D u_texture;
uniform vec2 u_invResolution;
varying vec2 v_texCoord;

void main() {
    vec2 uv = clamp(v_texCoord, 0.5 * u_invResolution, vec2(1.0) - 0.5 * u_invResolution);
    vec4 source = texture2D(u_texture, uv);
    gl_FragColor = vec4(1.0 - source.rgb, source.a);
}
)glsl";

} // namespace bv::shaders::invert

namespace bv::shaders {

    render::PostProcessShader const kInvertShader{
        "Invert",
        kFullscreenVertexSource,
        invert::kFragmentSource,
    };

} // namespace bv::shaders