#include "PostProcessRenderer.hpp"

#include "GlStateGuard.hpp"
#include "ShaderProgram.hpp"

#include <Geode/Geode.hpp>
#include <array>

using namespace geode::prelude;

namespace aa::render {

    bool PostProcessRenderer::initialize(PostProcessShader const& shader) {
        if (m_shader != &shader) {
            destroyResources();
            m_shader = &shader;
            m_failed = false;
        }
        if (m_failed) {
            return false;
        }
        if (m_program != 0 && m_texture != 0) {
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

        glGenTextures(1, &m_texture);
        if (m_texture == 0) {
            log::error("Unable to allocate post-process OpenGL objects");
            destroyResources();
            m_failed = true;
            return false;
        }
        if (!m_quad.initialize("post-process")) {
            destroyResources();
            m_failed = true;
            return false;
        }

        glBindTexture(GL_TEXTURE_2D, m_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        return true;
    }

    bool PostProcessRenderer::resizeTexture(GLsizei width, GLsizei height) {
        if (width == m_width && height == m_height) {
            return true;
        }

        glBindTexture(GL_TEXTURE_2D, m_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        GLint allocatedWidth = 0;
        GLint allocatedHeight = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &allocatedWidth);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &allocatedHeight);
        if (allocatedWidth != width || allocatedHeight != height) {
            log::error("Unable to allocate the {}x{} post-process source texture", width, height);
            destroyResources();
            m_failed = true;
            return false;
        }

        glUseProgram(m_program);
        glUniform2f(
            m_invResolutionUniform, 1.f / static_cast<GLfloat>(width), 1.f / static_cast<GLfloat>(height)
        );
        m_width = width;
        m_height = height;
        return true;
    }

    void PostProcessRenderer::apply(PostProcessShader const& shader, GLfloat scalar) {
        if (m_failed && m_shader == &shader) {
            return;
        }

        GlStateGuard state{GlStateProfile::PostProcess};
        glActiveTexture(GL_TEXTURE0);
        auto const& viewport = state.viewport();
        auto const width = static_cast<GLsizei>(viewport[2]);
        auto const height = static_cast<GLsizei>(viewport[3]);
        if (width <= 0 || height <= 0) {
            return;
        }
        if (!initialize(shader) || !resizeTexture(width, height)) {
            return;
        }

        glBindTexture(GL_TEXTURE_2D, m_texture);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, viewport[0], viewport[1], width, height);

        glUseProgram(m_program);
        if (m_scalarUniform >= 0) {
            glUniform1f(m_scalarUniform, scalar);
        }

        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        if (GLEW_VERSION_3_0 || GLEW_ARB_framebuffer_sRGB || GLEW_EXT_framebuffer_sRGB) {
            glDisable(GL_FRAMEBUFFER_SRGB);
        }
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        m_quad.bind();
        m_quad.draw();
    }

    void PostProcessRenderer::reset() {
        destroyResources();
        m_shader = nullptr;
        m_failed = false;
    }

    void PostProcessRenderer::destroyResources() {
        m_quad.reset();
        if (m_texture != 0) {
            glDeleteTextures(1, &m_texture);
            m_texture = 0;
        }
        if (m_program != 0) {
            glDeleteProgram(m_program);
            m_program = 0;
        }

        m_textureUniform = -1;
        m_invResolutionUniform = -1;
        m_scalarUniform = -1;
        m_width = 0;
        m_height = 0;
    }

} // namespace aa::render
