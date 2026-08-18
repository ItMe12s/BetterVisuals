#pragma once

namespace bv::render {
    struct PostProcessShader;
}

namespace bv::shaders {

    inline constexpr char kFullscreenVertexSource[] = R"glsl(
attribute vec2 a_position;
attribute vec2 a_texCoord;
varying vec2 v_texCoord;

void main() {
    v_texCoord = a_texCoord;
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)glsl";

    extern render::PostProcessShader const kFxaaShader;
    extern render::PostProcessShader const kSsaaShader;
    extern render::PostProcessShader const kSsaa3xShader;
    extern render::PostProcessShader const kCasShader;
    extern render::PostProcessShader const kGrayscaleShader;
    extern render::PostProcessShader const kPixelateShader;
    extern render::PostProcessShader const kInvertShader;
    extern render::PostProcessShader const kFlipShader;
    extern render::PostProcessShader const kDitheringShader;
    extern render::PostProcessShader const kVhsShader;
    extern render::PostProcessShader const kCrtShader;
    extern render::PostProcessShader const kRenderScaleShader;
    extern render::PostProcessShader const kFsrShader;

} // namespace bv::shaders
