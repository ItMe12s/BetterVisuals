#include "../../render/PostProcessRenderer.hpp"
#include "../PostProcessShaders.hpp"

namespace bv::shaders::grayscale {

    constexpr char kFragmentSource[] = R"glsl(
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

} // namespace bv::shaders::grayscale

namespace bv::shaders {

    render::PostProcessShader const kGrayscaleShader{
        "Grayscale",
        kFullscreenVertexSource,
        grayscale::kFragmentSource,
    };

} // namespace bv::shaders
