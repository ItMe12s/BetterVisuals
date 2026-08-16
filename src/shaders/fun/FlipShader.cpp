#include "../../render/PostProcessRenderer.hpp"
#include "../PostProcessShaders.hpp"

namespace bv::shaders::flip {

    constexpr char kFragmentSource[] = R"glsl(
uniform sampler2D u_texture;
uniform vec2 u_invResolution;
varying vec2 v_texCoord;

void main() {
    vec2 uv = clamp(
        vec2(v_texCoord.x, 1.0 - v_texCoord.y),
        0.5 * u_invResolution,
        vec2(1.0) - 0.5 * u_invResolution
    );
    gl_FragColor = texture2D(u_texture, uv);
}
)glsl";

} // namespace bv::shaders::flip

namespace bv::shaders {

    render::PostProcessShader const kFlipShader{
        "Flip",
        kFullscreenVertexSource,
        flip::kFragmentSource,
    };

} // namespace bv::shaders