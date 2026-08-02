#include "CrtShader.hpp"

#include "../../render/PostProcessRenderer.hpp"

/*
 * CRT effect derived from Mattias Gustavsson's crtview/crtemu_pc.h.
 * https://github.com/mattiasgustavsson/crtview/blob/6b996d780265269108b6dcd697c224746cacb8af/source/crtemu_pc.h
 *
 * Copyright (c) 2016 Mattias Gustavsson
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

namespace bv::shaders::crt {

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
varying vec2 v_texCoord;

vec3 sampleCrt(vec2 uv) {
    return pow(abs(texture2D(u_texture, uv).rgb), vec3(2.2)) * 1.25;
}

vec3 filmic(vec3 color) {
    vec3 x = max(vec3(0.0), color - vec3(0.004));
    return (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06);
}

vec2 curve(vec2 uv) {
    uv = (uv - 0.5) * 2.0;
    uv *= 1.1;
    uv.x *= 1.0 + pow(abs(uv.y) / 5.0, 2.0);
    uv.y *= 1.0 + pow(abs(uv.x) / 4.0, 2.0);
    uv = uv / 2.0 + 0.5;
    return uv * 0.92 + 0.04;
}

void main() {
    vec4 source = texture2D(u_texture, v_texCoord);
    vec2 curvedUv = mix(v_texCoord, curve(v_texCoord), 0.5);

    vec3 color;
    color.r = sampleCrt(curvedUv + vec2(0.0009, 0.0009) * 0.25).r + 0.02;
    color.g = sampleCrt(curvedUv + vec2(0.0, -0.0011) * 0.25).g + 0.02;
    color.b = sampleCrt(curvedUv + vec2(-0.0015, 0.0) * 0.25).b + 0.02;

    color *= vec3(0.95, 1.05, 0.95);
    color = clamp(
        color * 1.3 + 0.75 * color * color + 1.25 * color * color * color * color * color,
        vec3(0.0),
        vec3(10.0)
    );

    float vignette = 0.1 + 16.0 * curvedUv.x * curvedUv.y *
        (1.0 - curvedUv.x) * (1.0 - curvedUv.y);
    color *= 1.3 * pow(vignette, 0.5);

    float height = 1.0 / u_invResolution.y;
    float displayScale = height / 1080.0;
    float referenceY = curvedUv.y * height / displayScale;
    float referenceX = gl_FragCoord.x / displayScale;
    float scanline = clamp(0.35 + 0.18 * sin(referenceY * 1.2), 0.0, 1.0);
    color *= pow(scanline, 0.9);
    color *= 1.0 - 0.23 * clamp(mod(referenceX, 3.75) / 2.5, 0.0, 1.0);
    color = filmic(color);

    vec2 edgeDistance = min(curvedUv, vec2(1.0) - curvedUv);
    color *= smoothstep(0.0, 0.005, min(edgeDistance.x, edgeDistance.y));

    gl_FragColor = vec4(color, source.a);
}
)glsl";

} // namespace bv::shaders::crt

namespace bv::shaders {

    render::PostProcessShader const kCrtShader{
        "CRT Filter",
        crt::kVertexSource,
        crt::kFragmentSource,
    };

} // namespace bv::shaders
