#include "BloomShader.hpp"

#include "../PostProcessShaders.hpp"

/*
 * Based on the bloom kernel from:
 * https://github.com/kiwipxl/GLSL-shaders/blob/master/bloom.glsl
 */

namespace bv::shaders::bloom {

    constexpr char kPrefilterSource[] = R"glsl(
uniform sampler2D u_texture;
uniform vec2 u_invResolution;
uniform float u_threshold;

vec3 brightPass(vec3 color) {
    return clamp(
        (color - vec3(u_threshold)) / max(1.0 - u_threshold, 0.001),
        vec3(0.0),
        vec3(1.0)
    );
}

void main() {
    vec2 baseUv = gl_FragCoord.xy * 2.0 * u_invResolution;
    gl_FragColor = vec4(brightPass(texture2D(u_texture, baseUv).rgb), 1.0);
}
)glsl";

    constexpr char kBlurSource[] = R"glsl(
uniform sampler2D u_texture;
uniform vec2 u_texelStep;
varying vec2 v_texCoord;

void main() {
    vec3 bloom = texture2D(u_texture, v_texCoord).rgb * 0.121569;
    bloom += texture2D(u_texture, v_texCoord - u_texelStep).rgb * 0.116706;
    bloom += texture2D(u_texture, v_texCoord + u_texelStep).rgb * 0.116706;
    bloom += texture2D(u_texture, v_texCoord - u_texelStep * 2.0).rgb * 0.103256;
    bloom += texture2D(u_texture, v_texCoord + u_texelStep * 2.0).rgb * 0.103256;
    bloom += texture2D(u_texture, v_texCoord - u_texelStep * 3.0).rgb * 0.084195;
    bloom += texture2D(u_texture, v_texCoord + u_texelStep * 3.0).rgb * 0.084195;
    bloom += texture2D(u_texture, v_texCoord - u_texelStep * 4.0).rgb * 0.063270;
    bloom += texture2D(u_texture, v_texCoord + u_texelStep * 4.0).rgb * 0.063270;
    bloom += texture2D(u_texture, v_texCoord - u_texelStep * 5.0).rgb * 0.043819;
    bloom += texture2D(u_texture, v_texCoord + u_texelStep * 5.0).rgb * 0.043819;
    bloom += texture2D(u_texture, v_texCoord - u_texelStep * 6.0).rgb * 0.027969;
    bloom += texture2D(u_texture, v_texCoord + u_texelStep * 6.0).rgb * 0.027969;
    gl_FragColor = vec4(bloom, 1.0);
}
)glsl";

    constexpr char kCompositeSource[] = R"glsl(
uniform sampler2D u_source;
uniform sampler2D u_bloom;
uniform float u_intensity;
varying vec2 v_texCoord;

void main() {
    vec4 source = texture2D(u_source, v_texCoord);
    vec3 bloom = texture2D(u_bloom, v_texCoord).rgb;
    gl_FragColor = vec4(clamp(source.rgb + bloom * u_intensity, 0.0, 1.0), source.a);
}
)glsl";

} // namespace bv::shaders::bloom

namespace bv::shaders {

    BloomShaderSet const kBloomShaderSet{
        kFullscreenVertexSource,
        bloom::kPrefilterSource,
        bloom::kBlurSource,
        bloom::kCompositeSource,
    };

} // namespace bv::shaders
