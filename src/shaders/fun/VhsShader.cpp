#include "VhsShader.hpp"

#include "../../render/PostProcessRenderer.hpp"

/*
 * Code taken and modified from:
 * https://www.shadertoy.com/view/XtBXDt
 */

namespace aa::shaders::vhs {

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
uniform float u_time;
varying vec2 v_texCoord;

const float PI = 3.14159265;
const float DISTORTION_STRENGTH = 0.5;

vec3 sampleVhs(vec2 uv) {
    if (0.5 < abs(uv.x - 0.5)) {
        return vec3(0.1);
    }
    return texture2D(u_texture, uv).rgb;
}

float hash(vec2 value) {
    return fract(sin(dot(value, vec2(89.44, 19.36))) * 22189.22);
}

float interpolatedHash(vec2 value, vec2 resolution) {
    float h00 = hash(floor(value * resolution) / resolution);
    float h10 = hash(floor(value * resolution + vec2(1.0, 0.0)) / resolution);
    float h01 = hash(floor(value * resolution + vec2(0.0, 1.0)) / resolution);
    float h11 = hash(floor(value * resolution + vec2(1.0, 1.0)) / resolution);
    vec2 phase = smoothstep(vec2(0.0), vec2(1.0), mod(value * resolution, 1.0));
    return mix(mix(h00, h10, phase.x), mix(h01, h11, phase.x), phase.y);
}

float noise(vec2 value) {
    float sum = 0.0;
    for (int octave = 1; octave < 9; ++octave) {
        float frequency = pow(2.0, float(octave));
        sum += interpolatedHash(value + vec2(float(octave)), vec2(2.0 * frequency)) /
            frequency;
    }
    return sum;
}

void main() {
    vec2 uv = clamp(
        v_texCoord,
        0.5 * u_invResolution,
        vec2(1.0) - 0.5 * u_invResolution
    );
    vec2 distortedUv = uv;

    distortedUv.x +=
        (noise(vec2(distortedUv.y, u_time)) - 0.5) * 0.005 * DISTORTION_STRENGTH;
    float highFrequencyNoise = noise(vec2(distortedUv.y * 100.0, u_time * 10.0));
    distortedUv.x += (highFrequencyNoise - 0.5) * 0.01 * DISTORTION_STRENGTH;

    float creasePhase = clamp(
        (sin(distortedUv.y * 8.0 - u_time * PI * 1.2) - 0.92) * noise(vec2(u_time)),
        0.0,
        0.01
    ) * 10.0;
    float creaseNoise = max(highFrequencyNoise - 0.5, 0.0);
    distortedUv.x -= creaseNoise * creasePhase * DISTORTION_STRENGTH;

    float switchingPhase = 1.0 - smoothstep(0.0, 0.03, distortedUv.y);
    distortedUv.y += switchingPhase * 0.3 * DISTORTION_STRENGTH;
    distortedUv.x +=
        switchingPhase * ((highFrequencyNoise - 0.5) * 0.2) * DISTORTION_STRENGTH;

    vec3 color = sampleVhs(distortedUv);
    color *= 1.0 - creasePhase;
    color = mix(color, color.yzx, switchingPhase);

    for (int tap = 0; tap < 7; ++tap) {
        float offset = float(tap) - 4.0;
        color += vec3(
            sampleVhs(distortedUv + vec2(offset, 0.0) * 0.007 * DISTORTION_STRENGTH).r,
            sampleVhs(distortedUv + vec2(offset - 2.0, 0.0) * 0.007 * DISTORTION_STRENGTH).g,
            sampleVhs(distortedUv + vec2(offset - 4.0, 0.0) * 0.007 * DISTORTION_STRENGTH).b
        ) * 0.1;
    }
    color *= 0.6;

    color *= 1.0 + clamp(
        noise(vec2(0.0, uv.y + u_time * 0.2)) * 0.6 - 0.25,
        0.0,
        0.1
    );

    gl_FragColor = vec4(color, texture2D(u_texture, uv).a);
}
)glsl";

} // namespace aa::shaders::vhs

namespace aa::shaders {

    render::PostProcessShader const kVhsShader{
        "VHS Filter",
        vhs::kVertexSource,
        vhs::kFragmentSource,
        "u_time",
    };

} // namespace aa::shaders
