#pragma once

#include "FullscreenQuad.hpp"

#include <Geode/cocos/platform/CCGL.h>
#include <array>
#include <cstddef>

namespace bv::render {

    struct RenderTarget {
        GLuint framebuffer = 0;
        GLint x = 0;
        GLint y = 0;
        GLsizei width = 0;
        GLsizei height = 0;
    };

    class PostProcessPipeline final {
    public:
        PostProcessPipeline() = default;
        PostProcessPipeline(PostProcessPipeline const&) = delete;
        PostProcessPipeline& operator=(PostProcessPipeline const&) = delete;

        bool prepare(GLsizei width, GLsizei height);
        bool copyViewportFrom(GLuint sourceFramebuffer, std::array<GLint, 4> const& sourceViewport);
        void bindQuad() const;
        RenderTarget nextTarget() const;
        void advanceStage();
        void reset();

        GLuint currentTexture() const;

    private:
        bool initialize();
        bool resizeTargets(GLsizei width, GLsizei height);
        void destroyResources();

        std::array<GLuint, 2> m_textures = {};
        std::array<GLuint, 2> m_framebuffers = {};
        FullscreenQuad m_quad;
        GLsizei m_width = 0;
        GLsizei m_height = 0;
        std::size_t m_currentIndex = 0;
    };

} // namespace bv::render
