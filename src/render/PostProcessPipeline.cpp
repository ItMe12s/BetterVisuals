#include "PostProcessPipeline.hpp"

#include <Geode/Geode.hpp>
#include <cassert>

using namespace geode::prelude;

namespace {

    void clearGlErrors() {
        while (glGetError() != GL_NO_ERROR) {}
    }

    void configureTexture(GLuint texture) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    bool allocateTexture(GLuint texture, GLsizei width, GLsizei height) {
        glBindTexture(GL_TEXTURE_2D, texture);
        clearGlErrors();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        return glGetError() == GL_NO_ERROR;
    }

} // namespace

namespace bv::render {

    bool PostProcessPipeline::prepare(GLsizei width, GLsizei height) {
        if (width <= 0 || height <= 0) {
            return false;
        }
        glActiveTexture(GL_TEXTURE0);
        return initialize() && resizeTargets(width, height);
    }

    bool PostProcessPipeline::initialize() {
        if (m_framebuffers[0] != 0) {
            return true;
        }

        glGenTextures(static_cast<GLsizei>(m_textures.size()), m_textures.data());
        glGenFramebuffers(static_cast<GLsizei>(m_framebuffers.size()), m_framebuffers.data());
        if (m_textures[0] == 0 || m_textures[1] == 0 || m_framebuffers[0] == 0 ||
            m_framebuffers[1] == 0 || !m_quad.initialize("post-process pipeline")) {
            log::error("Unable to allocate post-process pipeline OpenGL objects");
            destroyResources();
            return false;
        }

        for (auto texture : m_textures) {
            configureTexture(texture);
        }
        return true;
    }

    bool PostProcessPipeline::resizeTargets(GLsizei width, GLsizei height) {
        if (width == m_width && height == m_height) {
            return true;
        }

        if (!allocateTexture(m_textures[0], width, height) ||
            !allocateTexture(m_textures[1], width, height)) {
            log::error("Unable to allocate {}x{} post-process textures", width, height);
            destroyResources();
            return false;
        }

        for (std::size_t index = 0; index < m_framebuffers.size(); ++index) {
            glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffers[index]);
            glFramebufferTexture2D(
                GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_textures[index], 0
            );
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                log::error("Unable to create complete post-process target {}", index);
                destroyResources();
                return false;
            }
        }

        m_width = width;
        m_height = height;
        return true;
    }

    bool PostProcessPipeline::copyViewportFrom(
        GLuint sourceFramebuffer, std::array<GLint, 4> const& sourceViewport
    ) {
        assert(m_textures[0] != 0 && "Post-process pipeline must be prepared before copying");
        assert(sourceViewport[2] == m_width && sourceViewport[3] == m_height);
        m_currentIndex = 0;
        glBindFramebuffer(GL_FRAMEBUFFER, sourceFramebuffer);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_textures[0]);
        clearGlErrors();
        glCopyTexSubImage2D(
            GL_TEXTURE_2D, 0, 0, 0, sourceViewport[0], sourceViewport[1], m_width, m_height
        );
        if (auto const error = glGetError(); error != GL_NO_ERROR) {
            log::error("Unable to copy source viewport (OpenGL error {})", error);
            return false;
        }
        return true;
    }

    void PostProcessPipeline::bindQuad() const {
        m_quad.bind();
    }

    RenderTarget PostProcessPipeline::nextTarget() const {
        assert(m_textures[m_currentIndex] != m_textures[1 - m_currentIndex]);
        return {m_framebuffers[1 - m_currentIndex], 0, 0, m_width, m_height};
    }

    void PostProcessPipeline::advanceStage() {
        m_currentIndex = 1 - m_currentIndex;
    }

    GLuint PostProcessPipeline::currentTexture() const {
        return m_textures[m_currentIndex];
    }

    void PostProcessPipeline::reset() {
        destroyResources();
    }

    void PostProcessPipeline::destroyResources() {
        m_quad.reset();
        glDeleteFramebuffers(static_cast<GLsizei>(m_framebuffers.size()), m_framebuffers.data());
        glDeleteTextures(static_cast<GLsizei>(m_textures.size()), m_textures.data());
        m_framebuffers = {};
        m_textures = {};
        m_width = 0;
        m_height = 0;
        m_currentIndex = 0;
    }

} // namespace bv::render
