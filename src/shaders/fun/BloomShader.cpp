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

vec3 brightPass(vec3 color) {
    return clamp((color - vec3(0.7)) / 0.3, vec3(0.0), vec3(1.0));
}

void main() {
    vec2 baseUv = gl_FragCoord.xy * 2.0 * u_invResolution;
    vec2 sampleOffset = 0.5 * u_invResolution;
    vec3 bloom = brightPass(texture2D(
        u_texture, baseUv + vec2(-sampleOffset.x, -sampleOffset.y)
    ).rgb);
    bloom += brightPass(texture2D(
        u_texture, baseUv + vec2(sampleOffset.x, -sampleOffset.y)
    ).rgb);
    bloom += brightPass(texture2D(
        u_texture, baseUv + vec2(-sampleOffset.x, sampleOffset.y)
    ).rgb);
    bloom += brightPass(texture2D(u_texture, baseUv + sampleOffset).rgb);
    gl_FragColor = vec4(bloom * 0.25, 1.0);
}
)glsl";

    constexpr char kBlurSource[] = R"glsl(
uniform sampler2D u_texture;
uniform vec2 u_texelStep;
varying vec2 v_texCoord;

void main() {
    vec3 bloom = texture2D(u_texture, v_texCoord - u_texelStep * 3.0).rgb * 0.070159;
    bloom += texture2D(u_texture, v_texCoord - u_texelStep * 2.0).rgb * 0.131075;
    bloom += texture2D(u_texture, v_texCoord - u_texelStep).rgb * 0.190713;
    bloom += texture2D(u_texture, v_texCoord).rgb * 0.216106;
    bloom += texture2D(u_texture, v_texCoord + u_texelStep).rgb * 0.190713;
    bloom += texture2D(u_texture, v_texCoord + u_texelStep * 2.0).rgb * 0.131075;
    bloom += texture2D(u_texture, v_texCoord + u_texelStep * 3.0).rgb * 0.070159;
    gl_FragColor = vec4(bloom, 1.0);
}
)glsl";

    constexpr char kCompositeSource[] = R"glsl(
uniform sampler2D u_source;
uniform sampler2D u_bloom;
varying vec2 v_texCoord;

void main() {
    vec4 source = texture2D(u_source, v_texCoord);
    vec3 bloom = texture2D(u_bloom, v_texCoord).rgb;
    gl_FragColor = vec4(clamp(source.rgb + bloom * 0.3, 0.0, 1.0), source.a);
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
