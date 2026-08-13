#pragma once

#include "PostProcessPipeline.hpp"

#include <Geode/cocos/platform/CCGL.h>
#include <array>

namespace bv::render {

    class BloomRenderer final {
    public:
        BloomRenderer() = default;
        BloomRenderer(BloomRenderer const&) = delete;
        BloomRenderer& operator=(BloomRenderer const&) = delete;

        bool prepare(GLsizei width, GLsizei height);
        void apply(GLuint inputTexture, RenderTarget const& target);
        void setParams(GLfloat threshold, GLfloat intensity, GLfloat radius);
        void reset();

    private:
        struct Program {
            GLuint handle = 0;
            GLint offset = -1;
            GLint param = -1;
        };

        bool initialize();
        bool resizeTextures(GLsizei width, GLsizei height);
        void updateBlurStep();
        void destroyResources();

        std::array<Program, 3> m_programs = {};
        GLuint m_bloomTexture = 0;
        GLuint m_horizontalTexture = 0;
        std::array<GLuint, 2> m_framebuffers = {};
        GLsizei m_width = 0;
        GLsizei m_height = 0;
        GLsizei m_halfWidth = 0;
        GLsizei m_halfHeight = 0;
        GLfloat m_blurStepX = 0.f;
        GLfloat m_blurStepY = 0.f;
        GLfloat m_threshold = 0.7f;
        GLfloat m_intensity = 0.3f;
        GLfloat m_radiusAt1080p = 8.f;
    };

} // namespace bv::render
