#include "PostProcessRenderer.hpp"

#include "GlStateGuard.hpp"
#include "ShaderProgram.hpp"

#include <Geode/Geode.hpp>
#include <array>

using namespace geode::prelude;

namespace bv::render {

    bool CaptureTexture::resize(GLsizei width, GLsizei height) {
        if (m_failed) {
            return false;
        }
        if (m_texture != 0 && width == m_width && height == m_height) {
            return true;
        }

        while (glGetError() != GL_NO_ERROR) {}
        if (m_texture == 0) {
            glGenTextures(1, &m_texture);
            if (m_texture == 0) {
                log::error("Unable to allocate the post-process capture texture");
                m_failed = true;
                return false;
            }

            glBindTexture(GL_TEXTURE_2D, m_texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        else {
            glBindTexture(GL_TEXTURE_2D, m_texture);
        }

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        auto const error = glGetError();
        if (error != GL_NO_ERROR) {
            log::error(
                "Unable to allocate the {}x{} post-process capture texture (OpenGL error {})",
                width,
                height,
                error
            );
            destroyResources();
            m_failed = true;
            return false;
        }

        m_width = width;
        m_height = height;
        return true;
    }

    bool CaptureTexture::copyFromFramebuffer(GLint x, GLint y, GLsizei width, GLsizei height) {
        if (!resize(width, height)) {
            return false;
        }

        glBindTexture(GL_TEXTURE_2D, m_texture);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, x, y, width, height);
        return true;
    }

    void CaptureTexture::reset() {
        destroyResources();
        m_failed = false;
    }

    void CaptureTexture::destroyResources() {
        if (m_texture != 0) {
            glDeleteTextures(1, &m_texture);
            m_texture = 0;
        }
        m_width = 0;
        m_height = 0;
    }

    bool PostProcessRenderer::isReady(
        CaptureTexture const& capture, PostProcessShader const& shader, GLsizei width, GLsizei height
    ) const {
        return !m_failed && m_shader == &shader && m_program != 0 && !capture.m_failed &&
            capture.m_texture != 0 && capture.m_width == width && capture.m_height == height;
    }

    bool PostProcessRenderer::prepare(
        CaptureTexture& capture, PostProcessShader const& shader, GLsizei width, GLsizei height
    ) {
        if (isReady(capture, shader, width, height)) {
            return true;
        }

        GlStateGuard state{GlStateProfile::PostProcess};
        glActiveTexture(GL_TEXTURE0);
        return initialize(shader) && capture.resize(width, height);
    }

    bool PostProcessRenderer::initialize(PostProcessShader const& shader) {
        if (m_shader != &shader) {
            destroyResources();
            m_shader = &shader;
            m_failed = false;
        }
        if (m_failed) {
            return false;
        }
        if (m_program != 0) {
            return true;
        }

        std::array vertexSources{shader.vertexSource};
        std::array fragmentSources{shader.fragmentSource};
        m_program = compileShaderProgram(shader.name, vertexSources, fragmentSources);
        if (m_program == 0) {
            m_failed = true;
            return false;
        }

        m_textureUniform = glGetUniformLocation(m_program, "u_texture");
        m_invResolutionUniform = glGetUniformLocation(m_program, "u_invResolution");
        m_scalarUniform =
            shader.scalarUniform ? glGetUniformLocation(m_program, shader.scalarUniform) : -1;
        if (m_textureUniform < 0 || m_invResolutionUniform < 0 ||
            (shader.scalarUniform && m_scalarUniform < 0)) {
            log::error("{} shader is missing required uniforms", shader.name);
            destroyResources();
            m_failed = true;
            return false;
        }

        glUseProgram(m_program);
        glUniform1i(m_textureUniform, 0);

        if (!m_quad.initialize("post-process")) {
            destroyResources();
            m_failed = true;
            return false;
        }

        return true;
    }

    bool PostProcessRenderer::apply(
        CaptureTexture& capture, PostProcessShader const& shader, GLfloat scalar
    ) {
        if (m_failed && m_shader == &shader) {
            return false;
        }

        GlStateGuard state{GlStateProfile::PostProcess};
        glActiveTexture(GL_TEXTURE0);
        auto const& viewport = state.viewport();
        auto const width = static_cast<GLsizei>(viewport[2]);
        auto const height = static_cast<GLsizei>(viewport[3]);
        if (width <= 0 || height <= 0) {
            return true;
        }
        if (!initialize(shader) ||
            !capture.copyFromFramebuffer(viewport[0], viewport[1], width, height)) {
            return false;
        }

        glUseProgram(m_program);
        glUniform2f(
            m_invResolutionUniform, 1.f / static_cast<GLfloat>(width), 1.f / static_cast<GLfloat>(height)
        );
        if (m_scalarUniform >= 0) {
            glUniform1f(m_scalarUniform, scalar);
        }

        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        m_quad.bind();
        m_quad.draw();
        return true;
    }

    void PostProcessRenderer::reset() {
        destroyResources();
        m_shader = nullptr;
        m_failed = false;
    }

    void PostProcessRenderer::destroyResources() {
        m_quad.reset();
        if (m_program != 0) {
            glDeleteProgram(m_program);
            m_program = 0;
        }

        m_textureUniform = -1;
        m_invResolutionUniform = -1;
        m_scalarUniform = -1;
    }

} // namespace bv::render
