#pragma once

#include "FullscreenQuad.hpp"

#include <Geode/cocos/platform/CCGL.h>
#include <array>
#include <cstddef>

namespace bv::render {

    class FramePipeline final {
    public:
        FramePipeline() = default;
        FramePipeline(FramePipeline const&) = delete;
        FramePipeline& operator=(FramePipeline const&) = delete;

        bool prepare(GLsizei width, GLsizei height);
        bool capture(GLuint framebuffer, GLint x, GLint y);
        void bindQuad() const;
        void bindOutput();
        void advance();
        bool present(GLuint framebuffer, std::array<GLint, 4> const& viewport);
        void reset();

        GLuint inputTexture() const;
        GLuint outputTexture() const;
        GLuint framebuffer() const;

    private:
        bool initialize();
        bool resizeTextures(GLsizei width, GLsizei height);
        void attachTexture(GLuint texture);
        bool validateTexture(GLuint texture);
        void destroyResources();

        std::array<GLuint, 2> m_textures = {};
        GLuint m_framebuffer = 0;
        GLuint m_presentProgram = 0;
        FullscreenQuad m_quad;
        GLsizei m_width = 0;
        GLsizei m_height = 0;
        std::size_t m_inputIndex = 0;
#ifndef NDEBUG
        unsigned m_frameCopies = 0;
#endif
    };

} // namespace bv::render
