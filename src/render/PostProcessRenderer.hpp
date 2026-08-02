#pragma once

#include "FullscreenQuad.hpp"

#include <Geode/cocos/platform/CCGL.h>
#include <string_view>

namespace bv::render {

    struct PostProcessShader {
        std::string_view name;
        std::string_view vertexSource;
        std::string_view fragmentSource;
        char const* scalarUniform = nullptr;
    };

    class CaptureTexture final {
    public:
        CaptureTexture() = default;
        CaptureTexture(CaptureTexture const&) = delete;
        CaptureTexture& operator=(CaptureTexture const&) = delete;

        void reset();

    private:
        friend class PostProcessRenderer;

        bool copyFromFramebuffer(GLint x, GLint y, GLsizei width, GLsizei height);
        bool resize(GLsizei width, GLsizei height);
        void destroyResources();

        GLuint m_texture = 0;
        GLsizei m_width = 0;
        GLsizei m_height = 0;
        bool m_failed = false;
    };

    class PostProcessRenderer final {
    public:
        PostProcessRenderer() = default;
        PostProcessRenderer(PostProcessRenderer const&) = delete;
        PostProcessRenderer& operator=(PostProcessRenderer const&) = delete;

        bool prepare(
            CaptureTexture& capture, PostProcessShader const& shader, GLsizei width, GLsizei height
        );
        bool apply(CaptureTexture& capture, PostProcessShader const& shader, GLfloat scalar = 0.f);
        void reset();

    private:
        bool isReady(
            CaptureTexture const& capture, PostProcessShader const& shader, GLsizei width, GLsizei height
        ) const;
        bool initialize(PostProcessShader const& shader);
        void destroyResources();

        PostProcessShader const* m_shader = nullptr;
        GLuint m_program = 0;
        FullscreenQuad m_quad;
        GLint m_textureUniform = -1;
        GLint m_invResolutionUniform = -1;
        GLint m_scalarUniform = -1;
        bool m_failed = false;
    };

} // namespace bv::render
