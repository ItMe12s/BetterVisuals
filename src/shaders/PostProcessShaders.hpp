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

    inline constexpr char kPresentFragmentSource[] = R"glsl(
uniform sampler2D u_texture;
varying vec2 v_texCoord;

void main() {
    gl_FragColor = texture2D(u_texture, v_texCoord);
}
)glsl";

    extern render::PostProcessShader const kFxaaShader;
    extern render::PostProcessShader const kCasShader;
    extern render::PostProcessShader const kGrayscaleShader;
    extern render::PostProcessShader const kPixelateShader;
    extern render::PostProcessShader const kDitheringShader;
    extern render::PostProcessShader const kVhsShader;
    extern render::PostProcessShader const kCrtShader;

} // namespace bv::shaders
