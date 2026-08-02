#include "VhsShader.hpp"

#include "../../render/PostProcessRenderer.hpp"

/*
 * Code taken and modified from:
 * https://www.shadertoy.com/view/XtBXDt
 */

namespace bv::shaders::vhs {

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
    vec3 value3 = fract(value.xyx * 0.1031);
    value3 += dot(value3, value3.yzx + 33.33);
    return fract((value3.x + value3.y) * value3.z);
}

float interpolatedHash(vec2 value, float resolution) {
    vec2 scaledValue = value * resolution;
    float invResolution = 1.0 / resolution;
    vec2 base = floor(scaledValue) * invResolution;
    float h00 = hash(base);
    float h10 = hash(base + vec2(invResolution, 0.0));
    float h01 = hash(base + vec2(0.0, invResolution));
    float h11 = hash(base + vec2(invResolution));
    vec2 phase = smoothstep(vec2(0.0), vec2(1.0), fract(scaledValue));
    return mix(mix(h00, h10, phase.x), mix(h01, h11, phase.x), phase.y);
}

float noise(vec2 value) {
    float sum = 0.0;
    float resolution = 4.0;
    float weight = 0.5;
    for (int octave = 1; octave < 3; ++octave) {
        sum += interpolatedHash(value + vec2(float(octave)), resolution) * weight;
        resolution *= 2.0;
        weight *= 0.5;
    }
    return sum * (85.0 / 64.0);
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
    float highFrequencyOffset = noise(vec2(distortedUv.y * 100.0, u_time * 10.0)) - 0.5;
    distortedUv.x += highFrequencyOffset * 0.01 * DISTORTION_STRENGTH;

    float creasePhase = clamp(
        (sin(distortedUv.y * 8.0 - u_time * PI * 1.2) - 0.92) * noise(vec2(u_time)),
        0.0,
        0.01
    ) * 10.0;
    float creaseNoise = max(highFrequencyOffset, 0.0);
    distortedUv.x -= creaseNoise * creasePhase * DISTORTION_STRENGTH;

    float switchingPhase = 1.0 - smoothstep(0.0, 0.03, distortedUv.y);
    distortedUv.y += switchingPhase * 0.3 * DISTORTION_STRENGTH;
    distortedUv.x +=
        switchingPhase * highFrequencyOffset * 0.2 * DISTORTION_STRENGTH;

    vec3 centerColor = vec3(0.0);
    vec3 chromaBlur = vec3(0.0);
    for (int tap = 0; tap < 7; ++tap) {
        float offset = float(tap) - 6.0;
        vec3 tapColor =
            sampleVhs(distortedUv + vec2(offset, 0.0) * 0.007 * DISTORTION_STRENGTH);
        if (tap == 6) {
            centerColor = tapColor;
        }
        if (tap >= 4) {
            chromaBlur.r += tapColor.r;
        }
        if (tap >= 2 && tap <= 4) {
            chromaBlur.g += tapColor.g;
        }
        if (tap <= 2) {
            chromaBlur.b += tapColor.b;
        }
    }

    vec3 color = centerColor;
    color *= 1.0 - creasePhase;
    color = mix(color, color.yzx, switchingPhase);
    color += chromaBlur * (0.7 / 3.0);
    color *= 0.6;

    color *= 1.0 + clamp(
        noise(vec2(0.0, uv.y + u_time * 0.2)) * 0.6 - 0.25,
        0.0,
        0.1
    );

    gl_FragColor = vec4(color, texture2D(u_texture, uv).a);
}
)glsl";

} // namespace bv::shaders::vhs

namespace bv::shaders {

    render::PostProcessShader const kVhsShader{
        "VHS Filter",
        vhs::kVertexSource,
        vhs::kFragmentSource,
        "u_time",
    };

} // namespace bv::shaders
