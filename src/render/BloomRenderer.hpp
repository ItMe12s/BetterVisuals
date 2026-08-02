#pragma once

#include <Geode/cocos/platform/CCGL.h>
#include <array>

namespace bv::render {

    class BloomRenderer final {
    public:
        BloomRenderer() = default;
        BloomRenderer(BloomRenderer const&) = delete;
        BloomRenderer& operator=(BloomRenderer const&) = delete;

        bool prepare(GLsizei width, GLsizei height, GLuint framebuffer);
        bool apply(GLuint inputTexture, GLuint outputTexture, GLuint framebuffer);
        void reset();

    private:
        bool isReady(GLsizei width, GLsizei height) const;

        struct Program {
            GLuint handle = 0;
            GLint offset = -1;
        };

        bool initialize();
        bool resizeTextures(GLsizei width, GLsizei height, GLuint framebuffer);
        bool validateFramebuffer(GLuint framebuffer, GLuint texture);
        void attachTexture(GLuint framebuffer, GLuint texture);
        void destroyResources();

        std::array<Program, 3> m_programs = {};
        GLuint m_prefilterTexture = 0;
        GLuint m_horizontalTexture = 0;
        GLuint m_verticalTexture = 0;
        GLsizei m_width = 0;
        GLsizei m_height = 0;
        GLsizei m_halfWidth = 0;
        GLsizei m_halfHeight = 0;
        GLfloat m_blurStepX = 0.f;
        GLfloat m_blurStepY = 0.f;
        bool m_failed = false;
    };

} // namespace bv::render
