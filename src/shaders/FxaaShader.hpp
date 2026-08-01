#pragma once

#include "../render/PostProcessRenderer.hpp"

// References used:

// NVIDIA FXAA 3.x whitepaper
// https://developer.download.nvidia.com/assets/gamedev/files/sdk/11/FXAA_WhitePaper.pdf

// Three.js edge search and subpixel blending
// https://github.com/mrdoob/three.js/blob/dev/examples/jsm/shaders/FXAAShader.js

// Libretro GLSL 1.20 FXAA
// https://github.com/libretro/glsl-shaders/blob/master/anti-aliasing/shaders/fxaa.glsl

namespace aa::shaders::fxaa {

    inline constexpr char kVertexSource[] = R"glsl(
#version 120
attribute vec2 a_position;
attribute vec2 a_texCoord;
varying vec2 v_texCoord;

void main() {
    v_texCoord = a_texCoord;
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)glsl";

    inline constexpr char kFragmentSource[] = R"glsl(
#version 120
uniform sampler2D u_texture;
uniform vec2 u_invResolution;
varying vec2 v_texCoord;

const float FXAA_EDGE_THRESHOLD = 1.0 / 8.0;
const float FXAA_EDGE_THRESHOLD_MIN = 1.0 / 16.0;
const float FXAA_SEARCH_THRESHOLD = 1.0 / 4.0;
const float FXAA_SUBPIXEL_CAP = 0.5;
const vec3 LUMA = vec3(0.299, 0.587, 0.114);

float sampleLuma(vec2 uv) {
    return dot(texture2D(u_texture, uv).rgb, LUMA);
}

void advanceSearch(
    inout vec2 uv,
    vec2 direction,
    float distance,
    float edgeLuma,
    float threshold,
    inout float lumaDelta,
    inout bool reachedEnd
) {
    if (!reachedEnd) {
        uv += direction * distance;
        lumaDelta = sampleLuma(uv) - edgeLuma;
        reachedEnd = abs(lumaDelta) >= threshold;
    }
}

void main() {
    vec4 rgbaM = texture2D(u_texture, v_texCoord);
    float lumaM = dot(rgbaM.rgb, LUMA);
    float lumaN = sampleLuma(v_texCoord + vec2(0.0, u_invResolution.y));
    float lumaS = sampleLuma(v_texCoord - vec2(0.0, u_invResolution.y));
    float lumaE = sampleLuma(v_texCoord + vec2(u_invResolution.x, 0.0));
    float lumaW = sampleLuma(v_texCoord - vec2(u_invResolution.x, 0.0));

    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    float lumaRange = lumaMax - lumaMin;

    if (lumaRange < max(FXAA_EDGE_THRESHOLD_MIN, lumaMax * FXAA_EDGE_THRESHOLD)) {
        gl_FragColor = rgbaM;
        return;
    }

    float lumaNE = sampleLuma(v_texCoord + u_invResolution);
    float lumaNW = sampleLuma(v_texCoord + vec2(-u_invResolution.x, u_invResolution.y));
    float lumaSE = sampleLuma(v_texCoord + vec2(u_invResolution.x, -u_invResolution.y));
    float lumaSW = sampleLuma(v_texCoord - u_invResolution);

    float horizontalEdge =
        2.0 * abs(lumaN + lumaS - 2.0 * lumaM) +
        abs(lumaNE + lumaSE - 2.0 * lumaE) +
        abs(lumaNW + lumaSW - 2.0 * lumaW);
    float verticalEdge =
        2.0 * abs(lumaE + lumaW - 2.0 * lumaM) +
        abs(lumaNE + lumaNW - 2.0 * lumaN) +
        abs(lumaSE + lumaSW - 2.0 * lumaS);
    bool isHorizontal = horizontalEdge >= verticalEdge;

    float positiveLuma = isHorizontal ? lumaN : lumaE;
    float negativeLuma = isHorizontal ? lumaS : lumaW;
    float positiveGradient = abs(positiveLuma - lumaM);
    float negativeGradient = abs(negativeLuma - lumaM);
    float pixelStep = isHorizontal ? u_invResolution.y : u_invResolution.x;

    float oppositeLuma = positiveLuma;
    float gradient = positiveGradient;
    if (negativeGradient > positiveGradient) {
        pixelStep = -pixelStep;
        oppositeLuma = negativeLuma;
        gradient = negativeGradient;
    }

    float edgeLuma = 0.5 * (lumaM + oppositeLuma);
    float searchThreshold = gradient * FXAA_SEARCH_THRESHOLD;
    vec2 edgeStep = isHorizontal
        ? vec2(u_invResolution.x, 0.0)
        : vec2(0.0, u_invResolution.y);
    vec2 edgeUv = v_texCoord;
    if (isHorizontal) {
        edgeUv.y += 0.5 * pixelStep;
    }
    else {
        edgeUv.x += 0.5 * pixelStep;
    }

    vec2 positiveUv = edgeUv;
    vec2 negativeUv = edgeUv;
    float positiveDelta = 0.0;
    float negativeDelta = 0.0;
    bool positiveReached = false;
    bool negativeReached = false;

    advanceSearch(
        positiveUv, edgeStep, 1.0, edgeLuma, searchThreshold, positiveDelta, positiveReached
    );
    advanceSearch(
        negativeUv, -edgeStep, 1.0, edgeLuma, searchThreshold, negativeDelta, negativeReached
    );
    advanceSearch(
        positiveUv, edgeStep, 1.5, edgeLuma, searchThreshold, positiveDelta, positiveReached
    );
    advanceSearch(
        negativeUv, -edgeStep, 1.5, edgeLuma, searchThreshold, negativeDelta, negativeReached
    );
    advanceSearch(
        positiveUv, edgeStep, 2.0, edgeLuma, searchThreshold, positiveDelta, positiveReached
    );
    advanceSearch(
        negativeUv, -edgeStep, 2.0, edgeLuma, searchThreshold, negativeDelta, negativeReached
    );
    advanceSearch(
        positiveUv, edgeStep, 2.0, edgeLuma, searchThreshold, positiveDelta, positiveReached
    );
    advanceSearch(
        negativeUv, -edgeStep, 2.0, edgeLuma, searchThreshold, negativeDelta, negativeReached
    );
    advanceSearch(
        positiveUv, edgeStep, 2.0, edgeLuma, searchThreshold, positiveDelta, positiveReached
    );
    advanceSearch(
        negativeUv, -edgeStep, 2.0, edgeLuma, searchThreshold, negativeDelta, negativeReached
    );
    advanceSearch(
        positiveUv, edgeStep, 4.0, edgeLuma, searchThreshold, positiveDelta, positiveReached
    );
    advanceSearch(
        negativeUv, -edgeStep, 4.0, edgeLuma, searchThreshold, negativeDelta, negativeReached
    );

    if (!positiveReached) {
        positiveUv += edgeStep * 8.0;
    }
    if (!negativeReached) {
        negativeUv -= edgeStep * 8.0;
    }

    float positiveDistance = isHorizontal
        ? positiveUv.x - v_texCoord.x
        : positiveUv.y - v_texCoord.y;
    float negativeDistance = isHorizontal
        ? v_texCoord.x - negativeUv.x
        : v_texCoord.y - negativeUv.y;
    bool usePositive = positiveDistance <= negativeDistance;
    float nearestDistance = min(positiveDistance, negativeDistance);
    float edgeBlend = 0.5 - nearestDistance / (positiveDistance + negativeDistance);
    float endpointDelta = usePositive ? positiveDelta : negativeDelta;
    bool endpointReached = usePositive ? positiveReached : negativeReached;
    bool correctVariation = (endpointDelta < 0.0) != (lumaM < edgeLuma);
    if (!endpointReached || !correctVariation) {
        edgeBlend = 0.0;
    }

    float neighborhoodLuma = (
        2.0 * (lumaN + lumaS + lumaE + lumaW) +
        lumaNE + lumaNW + lumaSE + lumaSW
    ) / 12.0;
    float subpixelBlend = clamp(
        abs(neighborhoodLuma - lumaM) / lumaRange,
        0.0,
        1.0
    );
    subpixelBlend = smoothstep(0.0, 1.0, subpixelBlend);
    subpixelBlend *= subpixelBlend * FXAA_SUBPIXEL_CAP;

    float finalBlend = max(edgeBlend, subpixelBlend);
    vec2 finalUv = v_texCoord;
    if (isHorizontal) {
        finalUv.y += pixelStep * finalBlend;
    }
    else {
        finalUv.x += pixelStep * finalBlend;
    }

    gl_FragColor = vec4(texture2D(u_texture, finalUv).rgb, rgbaM.a);
}
)glsl";

} // namespace aa::shaders::fxaa

namespace aa::shaders {

    inline constexpr render::PostProcessShader kFxaaShader{
        "FXAA",
        fxaa::kVertexSource,
        fxaa::kFragmentSource,
    };

} // namespace aa::shaders
