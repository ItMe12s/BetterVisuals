#pragma once

#include <Geode/cocos/platform/CCGL.h>
#include <string_view>

namespace aa::render {

    struct PostProcessShader {
        std::string_view name;
        std::string_view vertexSource;
        std::string_view fragmentSource;
    };

    class PostProcessRenderer final {
    public:
        PostProcessRenderer() = default;
        PostProcessRenderer(PostProcessRenderer const&) = delete;
        PostProcessRenderer& operator=(PostProcessRenderer const&) = delete;

        void apply(PostProcessShader const& shader);
        void reset();

    private:
        bool initialize(PostProcessShader const& shader);
        bool resizeTexture(GLsizei width, GLsizei height);
        void destroyResources();

        PostProcessShader const* m_shader = nullptr;
        GLuint m_program = 0;
        GLuint m_texture = 0;
        GLuint m_vbo = 0;
        GLint m_textureUniform = -1;
        GLint m_invResolutionUniform = -1;
        GLsizei m_width = 0;
        GLsizei m_height = 0;
        bool m_failed = false;
    };

} // namespace aa::render
