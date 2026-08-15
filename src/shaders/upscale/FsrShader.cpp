#include "../../render/PostProcessRenderer.hpp"
#include "../PostProcessShaders.hpp"

/*
 * This shader is derived from AMD FidelityFX Super Resolution 1.0 (EASU).
 * https://github.com/GPUOpen-Effects/FidelityFX-FSR
 *
 * Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
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

namespace bv::shaders::fsr {

    constexpr char kFragmentSource[] = R"glsl(
#ifdef GL_ES
    #ifdef GL_FRAGMENT_PRECISION_HIGH
        precision highp float;
    #else
        precision mediump float;
    #endif
#endif

uniform sampler2D u_texture;
uniform vec2 u_invResolution;
varying vec2 v_texCoord;

#define FSR_EPSILON (1e-7)

float tapLuma(vec3 color) {
    return color.b * 0.5 + (color.r * 0.5 + color.g);
}

void accumulateDirection(
    inout vec2 dir,
    inout float len,
    float weight,
    float lA,
    float lB,
    float lC,
    float lD,
    float lE
) {
    float dc = lD - lC;
    float cb = lC - lB;
    float lenX = max(abs(dc), abs(cb));
    lenX = 1.0 / max(lenX, FSR_EPSILON);
    float dirX = lD - lB;
    dir.x += dirX * weight;
    lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);
    lenX *= lenX;
    len += lenX * weight;

    float ec = lE - lC;
    float ca = lC - lA;
    float lenY = max(abs(ec), abs(ca));
    lenY = 1.0 / max(lenY, FSR_EPSILON);
    float dirY = lE - lA;
    dir.y += dirY * weight;
    lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);
    lenY *= lenY;
    len += lenY * weight;
}

void accumulateTap(
    inout vec3 color,
    inout float weight,
    vec2 offset,
    vec2 dir,
    vec2 len2,
    float lob,
    float clip,
    vec3 tapColor
) {
    vec2 rotated;
    rotated.x = offset.x * dir.x + offset.y * dir.y;
    rotated.y = offset.x * (-dir.y) + offset.y * dir.x;
    rotated *= len2;
    float d2 = rotated.x * rotated.x + rotated.y * rotated.y;
    d2 = min(d2, clip);
    float wB = 0.4 * d2 - 1.0;
    float wA = lob * d2 - 1.0;
    wB *= wB;
    wA *= wA;
    wB = 1.5625 * wB - 0.5625;
    float w = wB * wA;
    color += tapColor * w;
    weight += w;
}

void main() {
    vec2 inputSize = 1.0 / u_invResolution;

    // Output pixel centers {0 to output-1} map to input texel space with a
    // half-texel shift, matching FsrEasuCon() constants.
    vec2 pp = v_texCoord * inputSize - 0.5;
    vec2 fp = floor(pp);
    pp -= fp;

    // Tap coordinates are texel-space offsets from the center, built with
    // per-axis steps to avoid repeated multiplies.
    vec2 base = (fp + vec2(0.5)) * u_invResolution;
    vec2 stepX = vec2(u_invResolution.x, 0.0);
    vec2 stepY = vec2(0.0, u_invResolution.y);

    vec3 b = texture2D(u_texture, base - stepY).rgb;
    vec3 c = texture2D(u_texture, base + stepX - stepY).rgb;
    vec3 i = texture2D(u_texture, base - stepX + stepY).rgb;
    vec3 j = texture2D(u_texture, base + stepY).rgb;
    vec4 f = texture2D(u_texture, base);
    vec3 e = texture2D(u_texture, base - stepX).rgb;
    vec3 k = texture2D(u_texture, base + stepX + stepY).rgb;
    vec3 l = texture2D(u_texture, base + 2.0 * stepX + stepY).rgb;
    vec3 h = texture2D(u_texture, base + 2.0 * stepX).rgb;
    vec3 g = texture2D(u_texture, base + stepX).rgb;
    vec3 o = texture2D(u_texture, base + stepX + 2.0 * stepY).rgb;
    vec3 n = texture2D(u_texture, base + 2.0 * stepY).rgb;

    float bL = tapLuma(b);
    float cL = tapLuma(c);
    float iL = tapLuma(i);
    float jL = tapLuma(j);
    float fL = tapLuma(f.rgb);
    float eL = tapLuma(e);
    float kL = tapLuma(k);
    float lL = tapLuma(l);
    float hL = tapLuma(h);
    float gL = tapLuma(g);
    float oL = tapLuma(o);
    float nL = tapLuma(n);

    vec2 dir = vec2(0.0);
    float len = 0.0;
    accumulateDirection(
        dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL
    );
    accumulateDirection(
        dir, len, pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL
    );
    accumulateDirection(
        dir, len, (1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL
    );
    accumulateDirection(
        dir, len, pp.x * pp.y, gL, jL, kL, lL, oL
    );

    vec2 dir2 = dir * dir;
    float dirR = dir2.x + dir2.y;
    bool zero = dirR < (1.0 / 32768.0);
    dirR = zero ? 1.0 : inversesqrt(max(dirR, 1e-10));
    dir.x = zero ? 1.0 : dir.x;
    dir *= vec2(dirR);

    len = len * 0.5;
    len *= len;
    float stretch =
        (dir.x * dir.x + dir.y * dir.y) /
        max(max(abs(dir.x), abs(dir.y)), FSR_EPSILON);
    vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
    float lob = 0.5 - 0.29 * len;
    float clip = 1.0 / max(lob, FSR_EPSILON);

    vec3 min4 = min(min(f.rgb, g), min(j, k));
    vec3 max4 = max(max(f.rgb, g), max(j, k));

    vec3 accumulation = vec3(0.0);
    float weight = 0.0;
    accumulateTap(accumulation, weight, vec2(0.0, -1.0), dir, len2, lob, clip, b);
    accumulateTap(accumulation, weight, vec2(1.0, -1.0), dir, len2, lob, clip, c);
    accumulateTap(accumulation, weight, vec2(-1.0, 1.0), dir, len2, lob, clip, i);
    accumulateTap(accumulation, weight, vec2(0.0, 1.0), dir, len2, lob, clip, j);
    accumulateTap(accumulation, weight, vec2(0.0, 0.0), dir, len2, lob, clip, f.rgb);
    accumulateTap(accumulation, weight, vec2(-1.0, 0.0), dir, len2, lob, clip, e);
    accumulateTap(accumulation, weight, vec2(1.0, 1.0), dir, len2, lob, clip, k);
    accumulateTap(accumulation, weight, vec2(2.0, 1.0), dir, len2, lob, clip, l);
    accumulateTap(accumulation, weight, vec2(2.0, 0.0), dir, len2, lob, clip, h);
    accumulateTap(accumulation, weight, vec2(1.0, 0.0), dir, len2, lob, clip, g);
    accumulateTap(accumulation, weight, vec2(1.0, 2.0), dir, len2, lob, clip, o);
    accumulateTap(accumulation, weight, vec2(0.0, 2.0), dir, len2, lob, clip, n);

    vec3 pixel = min(max4, max(min4, accumulation * (1.0 / weight)));
    gl_FragColor = vec4(pixel, f.a);
}
)glsl";

} // namespace bv::shaders::fsr

namespace bv::shaders {

    render::PostProcessShader const kFsrShader{
        "AMD FidelityFX FSR 1 (EASU)",
        kFullscreenVertexSource,
        fsr::kFragmentSource,
    };

} // namespace bv::shaders