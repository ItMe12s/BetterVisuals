#include "FramePipeline.hpp"

#include "../shaders/PostProcessShaders.hpp"
#include "ShaderProgram.hpp"

#include <Geode/Geode.hpp>
#include <array>
#include <cassert>
#include <string_view>

using namespace geode::prelude;

namespace {

    void configureTexture(GLuint texture) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    void clearGlErrors() {
        while (glGetError() != GL_NO_ERROR) {}
    }

    bool allocateTexture(GLuint texture, GLsizei width, GLsizei height) {
        glBindTexture(GL_TEXTURE_2D, texture);
        clearGlErrors();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        return glGetError() == GL_NO_ERROR;
    }

} // namespace

namespace bv::render {

    bool FramePipeline::prepare(GLsizei width, GLsizei height) {
#ifndef NDEBUG
        m_frameCopies = 0;
#endif
        glActiveTexture(GL_TEXTURE0);
        return initialize() && resizeTextures(width, height);
    }

    bool FramePipeline::initialize() {
        if (m_presentProgram != 0) {
            return true;
        }

        std::array<std::string_view, 1> vertexSources{shaders::kFullscreenVertexSource};
        std::array<std::string_view, 1> fragmentSources{shaders::kPresentFragmentSource};
        m_presentProgram = compileShaderProgram("frame presentation", vertexSources, fragmentSources);
        if (m_presentProgram == 0) {
            return false;
        }

        auto const textureUniform = glGetUniformLocation(m_presentProgram, "u_texture");
        if (textureUniform < 0) {
            log::error("Frame presentation shader is missing u_texture");
            destroyResources();
            return false;
        }
        glUseProgram(m_presentProgram);
        glUniform1i(textureUniform, 0);

        glGenTextures(static_cast<GLsizei>(m_textures.size()), m_textures.data());
        glGenFramebuffers(1, &m_framebuffer);
        if (m_textures[0] == 0 || m_textures[1] == 0 || m_framebuffer == 0) {
            log::error("Unable to allocate frame pipeline OpenGL objects");
            destroyResources();
            return false;
        }
        if (!m_quad.initialize("frame pipeline")) {
            destroyResources();
            return false;
        }

        for (auto texture : m_textures) {
            configureTexture(texture);
        }
        return true;
    }

    bool FramePipeline::resizeTextures(GLsizei width, GLsizei height) {
        if (width == m_width && height == m_height) {
            return true;
        }

        if (!allocateTexture(m_textures[0], width, height) ||
            !allocateTexture(m_textures[1], width, height) || !validateTexture(m_textures[0]) ||
            !validateTexture(m_textures[1])) {
            log::error("Unable to allocate complete {}x{} frame pipeline targets", width, height);
            destroyResources();
            return false;
        }

        m_width = width;
        m_height = height;
        m_inputIndex = 0;
        return true;
    }

    void FramePipeline::attachTexture(GLuint texture) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    }

    bool FramePipeline::validateTexture(GLuint texture) {
        attachTexture(texture);
        return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }

    bool FramePipeline::capture(GLuint framebuffer, GLint x, GLint y) {
#ifndef NDEBUG
        assert(m_frameCopies == 0 && "Frame pipeline captured the framebuffer more than once");
#endif
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexture());
        clearGlErrors();
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, x, y, m_width, m_height);
        if (auto const error = glGetError(); error != GL_NO_ERROR) {
            log::error("Unable to capture the frame pipeline source (OpenGL error {})", error);
            return false;
        }
#ifndef NDEBUG
        ++m_frameCopies;
#endif
        return true;
    }

    void FramePipeline::bindQuad() const {
        m_quad.bind();
    }

    void FramePipeline::bindOutput() {
        assert(inputTexture() != outputTexture());
        attachTexture(outputTexture());
        glViewport(0, 0, m_width, m_height);
    }

    void FramePipeline::advance() {
        m_inputIndex = 1 - m_inputIndex;
    }

    bool FramePipeline::present(GLuint framebuffer, std::array<GLint, 4> const& viewport) {
#ifndef NDEBUG
        assert(m_frameCopies == 1 && "Enabled frame pipeline must capture exactly once");
#endif
        if (viewport[2] != m_width || viewport[3] != m_height) {
            return false;
        }
        if (auto const error = glGetError(); error != GL_NO_ERROR) {
            log::error("Frame pipeline pass failed (OpenGL error {})", error);
            return false;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glUseProgram(m_presentProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexture());
        if (auto const error = glGetError(); error != GL_NO_ERROR) {
            log::error("Unable to prepare frame pipeline presentation (OpenGL error {})", error);
            return false;
        }
        FullscreenQuad::draw();
        if (auto const error = glGetError(); error != GL_NO_ERROR) {
            log::error("Unable to present the frame pipeline output (OpenGL error {})", error);
            return false;
        }
        return true;
    }

    GLuint FramePipeline::inputTexture() const {
        return m_textures[m_inputIndex];
    }

    GLuint FramePipeline::outputTexture() const {
        return m_textures[1 - m_inputIndex];
    }

    GLuint FramePipeline::framebuffer() const {
        return m_framebuffer;
    }

    void FramePipeline::reset() {
        destroyResources();
    }

    void FramePipeline::destroyResources() {
        m_quad.reset();
        if (m_framebuffer != 0) {
            glDeleteFramebuffers(1, &m_framebuffer);
            m_framebuffer = 0;
        }
        glDeleteTextures(static_cast<GLsizei>(m_textures.size()), m_textures.data());
        m_textures = {};
        if (m_presentProgram != 0) {
            glDeleteProgram(m_presentProgram);
            m_presentProgram = 0;
        }
        m_width = 0;
        m_height = 0;
        m_inputIndex = 0;
    }

} // namespace bv::render
