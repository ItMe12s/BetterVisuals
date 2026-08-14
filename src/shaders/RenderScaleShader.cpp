#include "../render/PostProcessRenderer.hpp"
#include "PostProcessShaders.hpp"

namespace bv::shaders::renderScale {

    constexpr char kFragmentSource[] = R"glsl(
uniform sampler2D u_texture;
uniform vec2 u_invResolution;
varying vec2 v_texCoord;

void main() {
    vec2 uv = clamp(v_texCoord, 0.5 * u_invResolution, vec2(1.0) - 0.5 * u_invResolution);
    gl_FragColor = texture2D(u_texture, uv);
}
)glsl";

} // namespace bv::shaders::renderScale

namespace bv::shaders {

    render::PostProcessShader const kRenderScaleShader{
        "Render Scale",
        kFullscreenVertexSource,
        renderScale::kFragmentSource,
    };

} // namespace bv::shaders
