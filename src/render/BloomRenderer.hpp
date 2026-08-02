#pragma once

#include "FullscreenQuad.hpp"

#include <Geode/cocos/platform/CCGL.h>
#include <array>

namespace aa::render {

    class BloomRenderer final {
    public:
        BloomRenderer() = default;
        BloomRenderer(BloomRenderer const&) = delete;
        BloomRenderer& operator=(BloomRenderer const&) = delete;

        void apply();
        void reset();

    private:
        struct Program {
            GLuint handle = 0;
            std::array<GLint, 2> textures = {-1, -1};
            GLint offset = -1;
        };

        bool initialize();
        bool resizeTextures(GLsizei width, GLsizei height);
        bool validateFramebuffer(GLuint texture);
        void bindIntermediateFramebuffer();
        void attachIntermediateTexture(GLuint texture);
        void destroyResources();

        std::array<Program, 3> m_programs = {};
        GLuint m_sourceTexture = 0;
        GLuint m_prefilterTexture = 0;
        GLuint m_horizontalTexture = 0;
        GLuint m_verticalTexture = 0;
        GLuint m_framebuffer = 0;
        FullscreenQuad m_quad;
        GLsizei m_width = 0;
        GLsizei m_height = 0;
        GLsizei m_halfWidth = 0;
        GLsizei m_halfHeight = 0;
        GLfloat m_blurStepX = 0.f;
        GLfloat m_blurStepY = 0.f;
        bool m_coreFramebufferApi = false;
        bool m_failed = false;
    };

} // namespace aa::render
