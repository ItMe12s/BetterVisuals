#include "BloomShader.hpp"

#include "../render/PostProcessRenderer.hpp"

/*
 * Based on the single-pass kernel structure from:
 * https://github.com/kiwipxl/GLSL-shaders/blob/master/bloom.glsl
 */

namespace aa::shaders::bloom {

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

vec3 brightPass(vec3 color) {
    return clamp((color - vec3(0.7)) / 0.3, vec3(0.0), vec3(1.0));
}

void main() {
    vec4 source = texture2D(u_texture, v_texCoord);
    vec3 bloom = vec3(0.0);
    for (int y = -6; y <= 6; ++y) {
        for (int x = -6; x <= 6; ++x) {
            vec2 offset = vec2(float(x), float(y)) * u_invResolution;
            bloom += brightPass(texture2D(u_texture, v_texCoord + offset).rgb);
        }
    }
    bloom /= 169.0;
    gl_FragColor = vec4(clamp(source.rgb + bloom * 0.3, 0.0, 1.0), source.a);
}
)glsl";

} // namespace aa::shaders::bloom

namespace aa::shaders {

    render::PostProcessShader const kBloomShader{
        "Bloom",
        bloom::kVertexSource,
        bloom::kFragmentSource,
    };

} // namespace aa::shaders
