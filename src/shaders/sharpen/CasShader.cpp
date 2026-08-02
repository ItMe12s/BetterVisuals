#include "CasShader.hpp"

#include "../../render/PostProcessRenderer.hpp"

/*
 * This shader is derived from AMD FidelityFX Contrast Adaptive Sharpening.
 * https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK
 *
 * Copyright (C) 2024 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

namespace bv::shaders::cas {

    constexpr char kVertexSource[] = R"glsl(
attribute vec2 a_position;
attribute vec2 a_texCoord;
varying vec2 v_texCoord;

void main() {
    v_texCoord = a_texCoord;
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)glsl";

    constexpr char kFragmentSource[] = R"glsl(
uniform sampler2D u_texture;
uniform vec2 u_invResolution;
uniform float u_sharpness;
varying vec2 v_texCoord;

vec3 sampleLinear(vec2 uv) {
    vec3 color = texture2D(u_texture, uv).rgb;
    return color * color;
}

void main() {
    vec4 centerSample = texture2D(u_texture, v_texCoord);
    vec3 center = centerSample.rgb * centerSample.rgb;
    vec3 north = sampleLinear(v_texCoord + vec2(0.0, u_invResolution.y));
    vec3 south = sampleLinear(v_texCoord - vec2(0.0, u_invResolution.y));
    vec3 east = sampleLinear(v_texCoord + vec2(u_invResolution.x, 0.0));
    vec3 west = sampleLinear(v_texCoord - vec2(u_invResolution.x, 0.0));

    vec3 minimum = min(min(north, south), min(min(east, west), center));
    vec3 maximum = max(max(north, south), max(max(east, west), center));
    vec3 amplify = sqrt(clamp(
        min(minimum, vec3(1.0) - maximum) / max(maximum, vec3(1e-5)),
        vec3(0.0),
        vec3(1.0)
    ));

    float peak = -1.0 / mix(8.0, 5.0, clamp(u_sharpness, 0.0, 1.0));
    float weight = amplify.g * peak;
    vec3 filtered = (
        (north + south + east + west) * weight + center
    ) / (1.0 + 4.0 * weight);

    gl_FragColor = vec4(sqrt(clamp(filtered, 0.0, 1.0)), centerSample.a);
}
)glsl";

} // namespace bv::shaders::cas

namespace bv::shaders {

    render::PostProcessShader const kCasShader{
        "AMD FidelityFX CAS",
        cas::kVertexSource,
        cas::kFragmentSource,
        "u_sharpness",
    };

} // namespace bv::shaders
