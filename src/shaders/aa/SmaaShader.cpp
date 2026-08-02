#include "SmaaShader.hpp"

/*
 * SMAA 1x High and Ultra GLSL 1.20 / GLSL ES 1.00 port.
 *
 * Algorithm and reference implementation:
 * https://www.iryoku.com/smaa/downloads/SMAA-Enhanced-Subpixel-Morphological-Antialiasing.pdf
 * https://github.com/iryoku/smaa/blob/71c806a838bdd7d517df19192a20f0c61b3ca29d/SMAA.hlsl
 *
 * OpenGL coordinate and gamma-handling reference:
 * https://github.com/mrdoob/three.js/blob/b84ded15b430e73a26071ed7a20d020a41210023/examples/jsm/shaders/SMAAShader.js
 *
 * Copyright (C) 2013 Jorge Jimenez, Jose I. Echevarria, Belen Masia,
 * Fernando Navarro, and Diego Gutierrez. Licensed under the MIT license,
 * the complete notice is preserved in kAlgorithmSource below.
 */

namespace bv::shaders::smaa {

    constexpr char kHighCommonSource[] = R"glsl(
uniform vec4 u_metrics;

#define SMAA_RT_METRICS u_metrics
#define mad(a, b, c) ((a) * (b) + (c))

#define SMAA_THRESHOLD 0.1
#define SMAA_MAX_SEARCH_STEPS 16
#define SMAA_MAX_SEARCH_STEPS_DIAG 8
#define SMAA_CORNER_ROUNDING 25
#define SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR 2.0
)glsl";

    constexpr char kUltraCommonSource[] = R"glsl(
uniform vec4 u_metrics;

#define SMAA_RT_METRICS u_metrics
#define mad(a, b, c) ((a) * (b) + (c))

#define SMAA_THRESHOLD 0.05
#define SMAA_MAX_SEARCH_STEPS 32
#define SMAA_MAX_SEARCH_STEPS_DIAG 16
#define SMAA_CORNER_ROUNDING 25
#define SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR 2.0
)glsl";

    constexpr char kVertexStageSource[] = R"glsl(
#define SMAA_INCLUDE_VS 1
#define SMAA_INCLUDE_PS 0
)glsl";

    constexpr char kFragmentStageSource[] = R"glsl(
#define SMAA_INCLUDE_VS 0
#define SMAA_INCLUDE_PS 1
)glsl";

    constexpr char kAlgorithmSource[] = R"glsl(
/*
 * Copyright (C) 2013 Jorge Jimenez (jorge@iryoku.com)
 * Copyright (C) 2013 Jose I. Echevarria (joseignacioechevarria@gmail.com)
 * Copyright (C) 2013 Belen Masia (bmasia@unizar.es)
 * Copyright (C) 2013 Fernando Navarro (fernandn@microsoft.com)
 * Copyright (C) 2013 Diego Gutierrez (diegog@unizar.es)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to
 * do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software. As clarification, there
 * is no requirement that the copyright notice and permission be included in
 * binary distributions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#define SMAA_AREATEX_MAX_DISTANCE 16
#define SMAA_AREATEX_MAX_DISTANCE_DIAG 20
#define SMAA_AREATEX_PIXEL_SIZE (1.0 / vec2(160.0, 560.0))
#define SMAA_AREATEX_SUBTEX_SIZE (1.0 / 7.0)
#define SMAA_SEARCHTEX_SIZE vec2(66.0, 33.0)
#define SMAA_SEARCHTEX_PACKED_SIZE vec2(64.0, 16.0)
#define SMAA_CORNER_ROUNDING_NORM (float(SMAA_CORNER_ROUNDING) / 100.0)

#if SMAA_INCLUDE_VS
/*
 * -----------------------------------------------------------------------------
 * Vertex Shaders
 */

// Edge Detection Vertex Shader
void SMAAEdgeDetectionVS(vec2 texcoord,
                         out vec4 offset[3]) {
    offset[0] = mad(SMAA_RT_METRICS.xyxy, vec4(-1.0, 0.0, 0.0, -1.0), texcoord.xyxy);
    offset[1] = mad(SMAA_RT_METRICS.xyxy, vec4( 1.0, 0.0, 0.0,  1.0), texcoord.xyxy);
    offset[2] = mad(SMAA_RT_METRICS.xyxy, vec4(-2.0, 0.0, 0.0, -2.0), texcoord.xyxy);
}

// Blend Weight Calculation Vertex Shader
void SMAABlendingWeightCalculationVS(vec2 texcoord,
                                     out vec2 pixcoord,
                                     out vec4 offset[3]) {
    pixcoord = texcoord * SMAA_RT_METRICS.zw;

    // We will use these offsets for the searches later on (see @PSEUDO_GATHER4):
    offset[0] = mad(SMAA_RT_METRICS.xyxy, vec4(-0.25, -0.125,  1.25, -0.125), texcoord.xyxy);
    offset[1] = mad(SMAA_RT_METRICS.xyxy, vec4(-0.125, -0.25, -0.125,  1.25), texcoord.xyxy);

    // And these for the searches, they indicate the ends of the loops:
    offset[2] = mad(SMAA_RT_METRICS.xxyy,
                    vec4(-2.0, 2.0, -2.0, 2.0) * float(SMAA_MAX_SEARCH_STEPS),
                    vec4(offset[0].xz, offset[1].yw));
}

// Neighborhood Blending Vertex Shader
void SMAANeighborhoodBlendingVS(vec2 texcoord,
                                out vec4 offset) {
    offset = mad(SMAA_RT_METRICS.xyxy, vec4( 1.0, 0.0, 0.0,  1.0), texcoord.xyxy);
}
#endif // SMAA_INCLUDE_VS

#if SMAA_INCLUDE_PS
vec2 smaaRound(vec2 value) { return sign(value) * floor(abs(value) + 0.5); }
vec4 smaaRound(vec4 value) { return sign(value) * floor(abs(value) + 0.5); }
vec4 smaaSampleOffset(sampler2D source, vec2 coordinate, ivec2 offset) {
    return texture2D(source, coordinate + vec2(offset) * SMAA_RT_METRICS.xy);
}
void SMAAMovc(bvec2 cond, inout vec2 variable, vec2 value) {
    if (cond.x) variable.x = value.x;
    if (cond.y) variable.y = value.y;
}

/*
 * -----------------------------------------------------------------------------
 * Edge Detection Pixel Shaders (First Pass)
 */

/*
 * Color Edge Detection
 *
 * IMPORTANT NOTICE: color edge detection requires gamma-corrected colors, and
 * thus 'colorTex' should be a non-sRGB texture.
 */
vec2 SMAAColorEdgeDetectionPS(vec2 texcoord,
                                vec4 offset[3],
                                sampler2D colorTex) {
    // Color-edge threshold from the selected SMAA preset.
    vec2 threshold = vec2(SMAA_THRESHOLD, SMAA_THRESHOLD);

    // Calculate color deltas:
    vec4 delta;
    vec3 C = texture2D(colorTex, texcoord).rgb;

    vec3 Cleft = texture2D(colorTex, offset[0].xy).rgb;
    vec3 t = abs(C - Cleft);
    delta.x = max(max(t.r, t.g), t.b);

    vec3 Ctop  = texture2D(colorTex, offset[0].zw).rgb;
    t = abs(C - Ctop);
    delta.y = max(max(t.r, t.g), t.b);

    // We do the usual threshold:
    vec2 edges = step(threshold, delta.xy);

    // Then discard if there is no edge:
    if (dot(edges, vec2(1.0, 1.0)) == 0.0)
        discard;

    // Calculate right and bottom deltas:
    vec3 Cright = texture2D(colorTex, offset[1].xy).rgb;
    t = abs(C - Cright);
    delta.z = max(max(t.r, t.g), t.b);

    vec3 Cbottom  = texture2D(colorTex, offset[1].zw).rgb;
    t = abs(C - Cbottom);
    delta.w = max(max(t.r, t.g), t.b);

    // Calculate the maximum delta in the direct neighborhood:
    vec2 maxDelta = max(delta.xy, delta.zw);

    // Calculate left-left and top-top deltas:
    vec3 Cleftleft  = texture2D(colorTex, offset[2].xy).rgb;
    t = abs(C - Cleftleft);
    delta.z = max(max(t.r, t.g), t.b);

    vec3 Ctoptop = texture2D(colorTex, offset[2].zw).rgb;
    t = abs(C - Ctoptop);
    delta.w = max(max(t.r, t.g), t.b);

    // Calculate the final maximum delta:
    maxDelta = max(maxDelta.xy, delta.zw);
    float finalDelta = max(maxDelta.x, maxDelta.y);

    // Local contrast adaptation:
    edges.xy *= step(finalDelta, SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR * delta.xy);

    return edges;
}

/*
 * -----------------------------------------------------------------------------
 * Diagonal Search Functions
 */

// Allows to decode two binary values from a bilinear-filtered access.
vec2 SMAADecodeDiagBilinearAccess(vec2 e) {
    /*
     * Bilinear access for fetching 'e' have a 0.25 offset, and we are
     * interested in the R and G edges:
     *
     * +---G---+-------+
     * |   x o R   x   |
     * +-------+-------+
     *
     * Then, if one of these edge is enabled:
     *   Red:   (0.75 * X + 0.25 * 1) => 0.25 or 1.0
     *   Green: (0.75 * 1 + 0.25 * X) => 0.75 or 1.0
     *
     * This function will unpack the values (mad + mul + round):
     * wolframalpha.com: round(x * abs(5 * x - 5 * 0.75)) plot 0 to 1
     */
    e.r = e.r * abs(5.0 * e.r - 5.0 * 0.75);
    return smaaRound(e);
}

vec4 SMAADecodeDiagBilinearAccess(vec4 e) {
    e.rb = e.rb * abs(5.0 * e.rb - 5.0 * 0.75);
    return smaaRound(e);
}

// These functions allows to perform diagonal pattern searches.
vec2 SMAASearchDiag1(sampler2D edgesTex, vec2 texcoord, vec2 dir, out vec2 e) {
    vec4 coord = vec4(texcoord, -1.0, 1.0);
    vec3 t = vec3(SMAA_RT_METRICS.xy, 1.0);
    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS_DIAG; ++i) {
        if (!(coord.z < float(SMAA_MAX_SEARCH_STEPS_DIAG - 1) && coord.w > 0.9)) break;
        coord.xyz = mad(t, vec3(dir, 1.0), coord.xyz);
        e = texture2D(edgesTex, coord.xy).rg;
        coord.w = dot(e, vec2(0.5, 0.5));
    }
    return coord.zw;
}

vec2 SMAASearchDiag2(sampler2D edgesTex, vec2 texcoord, vec2 dir, out vec2 e) {
    vec4 coord = vec4(texcoord, -1.0, 1.0);
    coord.x += 0.25 * SMAA_RT_METRICS.x; // See @SearchDiag2Optimization
    vec3 t = vec3(SMAA_RT_METRICS.xy, 1.0);
    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS_DIAG; ++i) {
        if (!(coord.z < float(SMAA_MAX_SEARCH_STEPS_DIAG - 1) && coord.w > 0.9)) break;
        coord.xyz = mad(t, vec3(dir, 1.0), coord.xyz);

        /*
         * @SearchDiag2Optimization
         * Fetch both edges at once using bilinear filtering:
         */
        e = texture2D(edgesTex, coord.xy).rg;
        e = SMAADecodeDiagBilinearAccess(e);

        coord.w = dot(e, vec2(0.5, 0.5));
    }
    return coord.zw;
}

/*
 * Similar to SMAAArea, this calculates the area corresponding to a certain
 * diagonal distance and crossing edges 'e'.
 */
vec2 SMAAAreaDiag(sampler2D areaTex, vec2 dist, vec2 e, float offset) {
    vec2 texcoord = mad(vec2(SMAA_AREATEX_MAX_DISTANCE_DIAG, SMAA_AREATEX_MAX_DISTANCE_DIAG), e, dist);

    // We do a scale and bias for mapping to texel space:
    texcoord = mad(SMAA_AREATEX_PIXEL_SIZE, texcoord, 0.5 * SMAA_AREATEX_PIXEL_SIZE);

    // Diagonal areas are on the second half of the texture:
    texcoord.x += 0.5;

    // Move to proper place, according to the subpixel offset:
    texcoord.y += SMAA_AREATEX_SUBTEX_SIZE * offset;

    // Do it!
    return texture2D(areaTex, texcoord).ra;
}

// This searches for diagonal patterns and returns the corresponding weights.
vec2 SMAACalculateDiagWeights(sampler2D edgesTex, sampler2D areaTex, vec2 texcoord, vec2 e, vec4 subsampleIndices) {
    vec2 weights = vec2(0.0, 0.0);

    // Search for the line ends:
    vec4 d;
    vec2 end;
    if (e.r > 0.0) {
        d.xz = SMAASearchDiag1(edgesTex, texcoord, vec2(-1.0,  1.0), end);
        d.x += end.y > 0.9 ? 1.0 : 0.0;
    } else
        d.xz = vec2(0.0, 0.0);
    d.yw = SMAASearchDiag1(edgesTex, texcoord, vec2(1.0, -1.0), end);

    if (d.x + d.y > 2.0) { // d.x + d.y + 1 > 3
        // Fetch the crossing edges:
        vec4 coords = mad(vec4(-d.x + 0.25, d.x, d.y, -d.y - 0.25), SMAA_RT_METRICS.xyxy, texcoord.xyxy);
        vec4 c;
        c.xy = smaaSampleOffset(edgesTex, coords.xy, ivec2(-1,  0)).rg;
        c.zw = smaaSampleOffset(edgesTex, coords.zw, ivec2( 1,  0)).rg;
        c.yxwz = SMAADecodeDiagBilinearAccess(c.xyzw);

        // Merge crossing edges at each side into a single value:
        vec2 cc = mad(vec2(2.0, 2.0), c.xz, c.yw);

        // Remove the crossing edge if we didn't found the end of the line:
        SMAAMovc(greaterThanEqual(d.zw, vec2(0.9)), cc, vec2(0.0, 0.0));

        // Fetch the areas for this line:
        weights += SMAAAreaDiag(areaTex, d.xy, cc, subsampleIndices.z);
    }

    // Search for the line ends:
    d.xz = SMAASearchDiag2(edgesTex, texcoord, vec2(-1.0, -1.0), end);
    if (smaaSampleOffset(edgesTex, texcoord, ivec2(1, 0)).r > 0.0) {
        d.yw = SMAASearchDiag2(edgesTex, texcoord, vec2(1.0, 1.0), end);
        d.y += end.y > 0.9 ? 1.0 : 0.0;
    } else
        d.yw = vec2(0.0, 0.0);

    if (d.x + d.y > 2.0) { // d.x + d.y + 1 > 3
        // Fetch the crossing edges:
        vec4 coords = mad(vec4(-d.x, -d.x, d.y, d.y), SMAA_RT_METRICS.xyxy, texcoord.xyxy);
        vec4 c;
        c.x  = smaaSampleOffset(edgesTex, coords.xy, ivec2(-1,  0)).g;
        c.y  = smaaSampleOffset(edgesTex, coords.xy, ivec2( 0, -1)).r;
        c.zw = smaaSampleOffset(edgesTex, coords.zw, ivec2( 1,  0)).gr;
        vec2 cc = mad(vec2(2.0, 2.0), c.xz, c.yw);

        // Remove the crossing edge if we didn't found the end of the line:
        SMAAMovc(greaterThanEqual(d.zw, vec2(0.9)), cc, vec2(0.0, 0.0));

        // Fetch the areas for this line:
        weights += SMAAAreaDiag(areaTex, d.xy, cc, subsampleIndices.w).gr;
    }

    return weights;
}
/*
 * -----------------------------------------------------------------------------
 * Horizontal/Vertical Search Functions
 */

/*
 * This allows to determine how much length should we add in the last step
 * of the searches. It takes the bilinearly interpolated edge (see 
 * @PSEUDO_GATHER4), and adds 0, 1 or 2, depending on which edges and
 * crossing edges are active.
 */
float SMAASearchLength(sampler2D searchTex, vec2 e, float offset) {
    /*
     * The texture is flipped vertically, with left and right cases taking half
     * of the space horizontally:
     */
    vec2 scale = SMAA_SEARCHTEX_SIZE * vec2(0.5, -1.0);
    vec2 bias = SMAA_SEARCHTEX_SIZE * vec2(offset, 1.0);

    // Scale and bias to access texel centers:
    scale += vec2(-1.0,  1.0);
    bias  += vec2( 0.5, -0.5);

    /*
     * Convert from pixel coordinates to texcoords:
     * (We use SMAA_SEARCHTEX_PACKED_SIZE because the texture is cropped)
     */
    scale *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;
    bias *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;

    // Lookup the search texture:
    return texture2D(searchTex, mad(scale, e, bias)).r;
}

// Horizontal/vertical search functions for the 2nd pass.
float SMAASearchXLeft(sampler2D edgesTex, sampler2D searchTex, vec2 texcoord, float end) {
    /*
     * @PSEUDO_GATHER4
     * This texcoord has been offset by (-0.25, -0.125) in the vertex shader to
     * sample between edge, thus fetching four edges in a row.
     * Sampling with different offsets in each direction allows to disambiguate
     * which edges are active from the four fetched ones.
     */
    vec2 e = vec2(0.0, 1.0);
    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; ++i) {
        if (!(texcoord.x > end && e.g > 0.8281 && e.r == 0.0)) break;
        e = texture2D(edgesTex, texcoord).rg;
        texcoord = mad(-vec2(2.0, 0.0), SMAA_RT_METRICS.xy, texcoord);
    }

    float offset = mad(-(255.0 / 127.0), SMAASearchLength(searchTex, e, 0.0), 3.25);
    return mad(SMAA_RT_METRICS.x, offset, texcoord.x);
}

float SMAASearchXRight(sampler2D edgesTex, sampler2D searchTex, vec2 texcoord, float end) {
    vec2 e = vec2(0.0, 1.0);
    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; ++i) {
        if (!(texcoord.x < end && e.g > 0.8281 && e.r == 0.0)) break;
        e = texture2D(edgesTex, texcoord).rg;
        texcoord = mad(vec2(2.0, 0.0), SMAA_RT_METRICS.xy, texcoord);
    }
    float offset = mad(-(255.0 / 127.0), SMAASearchLength(searchTex, e, 0.5), 3.25);
    return mad(-SMAA_RT_METRICS.x, offset, texcoord.x);
}

float SMAASearchYUp(sampler2D edgesTex, sampler2D searchTex, vec2 texcoord, float end) {
    vec2 e = vec2(1.0, 0.0);
    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; ++i) {
        if (!(texcoord.y > end && e.r > 0.8281 && e.g == 0.0)) break;
        e = texture2D(edgesTex, texcoord).rg;
        texcoord = mad(-vec2(0.0, 2.0), SMAA_RT_METRICS.xy, texcoord);
    }
    float offset = mad(-(255.0 / 127.0), SMAASearchLength(searchTex, e.gr, 0.0), 3.25);
    return mad(SMAA_RT_METRICS.y, offset, texcoord.y);
}

float SMAASearchYDown(sampler2D edgesTex, sampler2D searchTex, vec2 texcoord, float end) {
    vec2 e = vec2(1.0, 0.0);
    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; ++i) {
        if (!(texcoord.y < end && e.r > 0.8281 && e.g == 0.0)) break;
        e = texture2D(edgesTex, texcoord).rg;
        texcoord = mad(vec2(0.0, 2.0), SMAA_RT_METRICS.xy, texcoord);
    }
    float offset = mad(-(255.0 / 127.0), SMAASearchLength(searchTex, e.gr, 0.5), 3.25);
    return mad(-SMAA_RT_METRICS.y, offset, texcoord.y);
}

/*
 * Ok, we have the distance and both crossing edges. So, what are the areas
 * at each side of current edge?
 */
vec2 SMAAArea(sampler2D areaTex, vec2 dist, float e1, float e2, float offset) {
    // Rounding prevents precision errors of bilinear filtering:
    vec2 texcoord = mad(vec2(SMAA_AREATEX_MAX_DISTANCE, SMAA_AREATEX_MAX_DISTANCE), smaaRound(4.0 * vec2(e1, e2)), dist);
    
    // We do a scale and bias for mapping to texel space:
    texcoord = mad(SMAA_AREATEX_PIXEL_SIZE, texcoord, 0.5 * SMAA_AREATEX_PIXEL_SIZE);

    // Move to proper place, according to the subpixel offset:
    texcoord.y = mad(SMAA_AREATEX_SUBTEX_SIZE, offset, texcoord.y);

    // Do it!
    return texture2D(areaTex, texcoord).ra;
}

/*
 * -----------------------------------------------------------------------------
 * Corner Detection Functions
 */

void SMAADetectHorizontalCornerPattern(sampler2D edgesTex, inout vec2 weights, vec4 texcoord, vec2 d) {
    vec2 leftRight = step(d.xy, d.yx);
    vec2 rounding = (1.0 - SMAA_CORNER_ROUNDING_NORM) * leftRight;

    rounding /= leftRight.x + leftRight.y; // Reduce blending for pixels in the center of a line.

    vec2 factor = vec2(1.0, 1.0);
    factor.x -= rounding.x * smaaSampleOffset(edgesTex, texcoord.xy, ivec2(0,  1)).r;
    factor.x -= rounding.y * smaaSampleOffset(edgesTex, texcoord.zw, ivec2(1,  1)).r;
    factor.y -= rounding.x * smaaSampleOffset(edgesTex, texcoord.xy, ivec2(0, -2)).r;
    factor.y -= rounding.y * smaaSampleOffset(edgesTex, texcoord.zw, ivec2(1, -2)).r;

    weights *= clamp(factor, vec2(0.0), vec2(1.0));
}

void SMAADetectVerticalCornerPattern(sampler2D edgesTex, inout vec2 weights, vec4 texcoord, vec2 d) {
    vec2 leftRight = step(d.xy, d.yx);
    vec2 rounding = (1.0 - SMAA_CORNER_ROUNDING_NORM) * leftRight;

    rounding /= leftRight.x + leftRight.y;

    vec2 factor = vec2(1.0, 1.0);
    factor.x -= rounding.x * smaaSampleOffset(edgesTex, texcoord.xy, ivec2( 1, 0)).g;
    factor.x -= rounding.y * smaaSampleOffset(edgesTex, texcoord.zw, ivec2( 1, 1)).g;
    factor.y -= rounding.x * smaaSampleOffset(edgesTex, texcoord.xy, ivec2(-2, 0)).g;
    factor.y -= rounding.y * smaaSampleOffset(edgesTex, texcoord.zw, ivec2(-2, 1)).g;

    weights *= clamp(factor, vec2(0.0), vec2(1.0));
}

/*
 * -----------------------------------------------------------------------------
 * Blending Weight Calculation Pixel Shader (Second Pass)
 */

vec4 SMAABlendingWeightCalculationPS(vec2 texcoord,
                                       vec2 pixcoord,
                                       vec4 offset[3],
                                       sampler2D edgesTex,
                                       sampler2D areaTex,
                                       sampler2D searchTex,
                                       vec4 subsampleIndices) { // Just pass zero for SMAA 1x, see @SUBSAMPLE_INDICES.
    vec4 weights = vec4(0.0, 0.0, 0.0, 0.0);

    vec2 e = texture2D(edgesTex, texcoord).rg;

    if (e.g > 0.0) { // Edge at north
        /*
         * Diagonals have both north and west edges, so searching for them in
         * one of the boundaries is enough.
         */
        weights.rg = SMAACalculateDiagWeights(edgesTex, areaTex, texcoord, e, subsampleIndices);

        /*
         * We give priority to diagonals, so if we find a diagonal we skip 
         * horizontal/vertical processing.
         */
        if (weights.r == -weights.g) { // weights.r + weights.g == 0.0

        vec2 d;

        // Find the distance to the left:
        vec3 coords;
        coords.x = SMAASearchXLeft(edgesTex, searchTex, offset[0].xy, offset[2].x);
        coords.y = offset[1].y; // offset[1].y = texcoord.y - 0.25 * SMAA_RT_METRICS.y (@CROSSING_OFFSET)
        d.x = coords.x;

        /*
         * Now fetch the left crossing edges, two at a time using bilinear
         * filtering. Sampling at -0.25 (see @CROSSING_OFFSET) enables to
         * discern what value each edge has:
         */
        float e1 = texture2D(edgesTex, coords.xy).r;

        // Find the distance to the right:
        coords.z = SMAASearchXRight(edgesTex, searchTex, offset[0].zw, offset[2].y);
        d.y = coords.z;

        /*
         * We want the distances to be in pixel units (doing this here allow to
         * better interleave arithmetic and memory accesses):
         */
        d = abs(smaaRound(mad(SMAA_RT_METRICS.zz, d, -pixcoord.xx)));

        /*
         * SMAAArea below needs a sqrt, as the areas texture is compressed
         * quadratically:
         */
        vec2 sqrt_d = sqrt(d);

        // Fetch the right crossing edges:
        float e2 = smaaSampleOffset(edgesTex, coords.zy, ivec2(1, 0)).r;

        /*
         * Ok, we know how this pattern looks like, now it is time for getting
         * the actual area:
         */
        weights.rg = SMAAArea(areaTex, sqrt_d, e1, e2, subsampleIndices.y);

        // Fix corners:
        coords.y = texcoord.y;
        SMAADetectHorizontalCornerPattern(edgesTex, weights.rg, coords.xyzy, d);

        } else
            e.r = 0.0; // Skip vertical processing.
    }

    if (e.r > 0.0) { // Edge at west
        vec2 d;

        // Find the distance to the top:
        vec3 coords;
        coords.y = SMAASearchYUp(edgesTex, searchTex, offset[1].xy, offset[2].z);
        coords.x = offset[0].x; // offset[1].x = texcoord.x - 0.25 * SMAA_RT_METRICS.x;
        d.x = coords.y;

        // Fetch the top crossing edges:
        float e1 = texture2D(edgesTex, coords.xy).g;

        // Find the distance to the bottom:
        coords.z = SMAASearchYDown(edgesTex, searchTex, offset[1].zw, offset[2].w);
        d.y = coords.z;

        // We want the distances to be in pixel units:
        d = abs(smaaRound(mad(SMAA_RT_METRICS.ww, d, -pixcoord.yy)));

        /*
         * SMAAArea below needs a sqrt, as the areas texture is compressed 
         * quadratically:
         */
        vec2 sqrt_d = sqrt(d);

        // Fetch the bottom crossing edges:
        float e2 = smaaSampleOffset(edgesTex, coords.xz, ivec2(0, 1)).g;

        // Get the area for this direction:
        weights.ba = SMAAArea(areaTex, sqrt_d, e1, e2, subsampleIndices.x);

        // Fix corners:
        coords.x = texcoord.x;
        SMAADetectVerticalCornerPattern(edgesTex, weights.ba, coords.xyxz, d);
    }

    return weights;
}

//-----------------------------------------------------------------------------
#endif // SMAA_INCLUDE_PS

)glsl";

    constexpr char kEdgeVertexMain[] = R"glsl(
attribute vec2 a_position;
attribute vec2 a_texCoord;
varying vec2 v_texCoord;
varying vec4 v_offset[3];
void main() {
    v_texCoord = a_texCoord;
    SMAAEdgeDetectionVS(v_texCoord, v_offset);
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)glsl";

    constexpr char kEdgeFragmentMain[] = R"glsl(
uniform sampler2D u_colorTexture;
varying vec2 v_texCoord;
varying vec4 v_offset[3];
void main() {
    vec2 edges = SMAAColorEdgeDetectionPS(v_texCoord, v_offset, u_colorTexture);
    gl_FragColor = vec4(edges, 0.0, 0.0);
}
)glsl";

    constexpr char kWeightVertexMain[] = R"glsl(
attribute vec2 a_position;
attribute vec2 a_texCoord;
varying vec2 v_texCoord;
varying vec2 v_pixelCoord;
varying vec4 v_offset[3];
void main() {
    v_texCoord = a_texCoord;
    SMAABlendingWeightCalculationVS(v_texCoord, v_pixelCoord, v_offset);
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)glsl";

    constexpr char kWeightFragmentMain[] = R"glsl(
uniform sampler2D u_edgesTexture;
uniform sampler2D u_areaTexture;
uniform sampler2D u_searchTexture;
varying vec2 v_texCoord;
varying vec2 v_pixelCoord;
varying vec4 v_offset[3];
void main() {
    gl_FragColor = SMAABlendingWeightCalculationPS(
        v_texCoord, v_pixelCoord, v_offset, u_edgesTexture,
        u_areaTexture, u_searchTexture, vec4(0.0)
    );
}
)glsl";

    constexpr char kNeighborhoodVertexMain[] = R"glsl(
attribute vec2 a_position;
attribute vec2 a_texCoord;
varying vec2 v_texCoord;
varying vec4 v_offset;
void main() {
    v_texCoord = a_texCoord;
    SMAANeighborhoodBlendingVS(v_texCoord, v_offset);
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)glsl";

    constexpr char kNeighborhoodFragmentMain[] = R"glsl(
uniform sampler2D u_colorTexture;
uniform sampler2D u_blendTexture;
varying vec2 v_texCoord;
varying vec4 v_offset;

vec3 smaaSrgbToLinear(vec3 color) {
    vec3 low = color / 12.92;
    vec3 high = pow((color + vec3(0.055)) / 1.055, vec3(2.4));
    return mix(low, high, step(vec3(0.04045), color));
}
vec3 smaaLinearToSrgb(vec3 color) {
    color = max(color, vec3(0.0));
    vec3 low = color * 12.92;
    vec3 high = 1.055 * pow(color, vec3(1.0 / 2.4)) - vec3(0.055);
    return clamp(
        mix(low, high, step(vec3(0.0031308), color)),
        vec3(0.0), vec3(1.0)
    );
}
void main() {
    /*
     * The RGBA copy contains display-encoded values without automatic
     * sRGB decoding. Edge detection reads them directly; only blended pixels
     * are decoded to linear light and encoded once after blending.
     */
    vec4 weights;
    weights.x = texture2D(u_blendTexture, v_offset.xy).a;
    weights.y = texture2D(u_blendTexture, v_offset.zw).g;
    weights.wz = texture2D(u_blendTexture, v_texCoord).xz;
    if (dot(weights, vec4(1.0)) < 1e-5) {
        gl_FragColor = texture2D(u_colorTexture, v_texCoord);
        return;
    }

    bool horizontal = max(weights.x, weights.z) > max(weights.y, weights.w);
    vec4 blendingOffset = vec4(0.0, weights.y, 0.0, weights.w);
    vec2 blendingWeight = weights.yw;
    if (horizontal) {
        blendingOffset = vec4(weights.x, 0.0, weights.z, 0.0);
        blendingWeight = weights.xz;
    }
    blendingWeight /= dot(blendingWeight, vec2(1.0));

    vec4 blendingCoord =
        blendingOffset * vec4(u_metrics.xy, -u_metrics.xy) + v_texCoord.xyxy;
    vec4 color0 = texture2D(u_colorTexture, blendingCoord.xy);
    vec4 color1 = texture2D(u_colorTexture, blendingCoord.zw);
    vec3 linearColor =
        blendingWeight.x * smaaSrgbToLinear(color0.rgb) +
        blendingWeight.y * smaaSrgbToLinear(color1.rgb);
    float alpha = dot(blendingWeight, vec2(color0.a, color1.a));
    gl_FragColor = vec4(smaaLinearToSrgb(linearColor), alpha);
}
)glsl";

    constexpr std::array<ProgramSource, 3> kPrograms{{
        {"SMAA edge detection", kEdgeVertexMain, kEdgeFragmentMain},
        {"SMAA blending weights", kWeightVertexMain, kWeightFragmentMain},
        {"SMAA neighborhood blending", kNeighborhoodVertexMain, kNeighborhoodFragmentMain},
    }};

    ShaderSet const kSmaaHighShaderSet{
        kHighCommonSource,
        kVertexStageSource,
        kFragmentStageSource,
        kAlgorithmSource,
        kPrograms,
    };

    ShaderSet const kSmaaUltraShaderSet{
        kUltraCommonSource,
        kVertexStageSource,
        kFragmentStageSource,
        kAlgorithmSource,
        kPrograms,
    };

} // namespace bv::shaders::smaa
