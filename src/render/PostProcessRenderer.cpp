#include "PostProcessRenderer.hpp"

#include "FullscreenQuad.hpp"
#include "ShaderProgram.hpp"

#include <Geode/Geode.hpp>
#include <array>

using namespace geode::prelude;

namespace bv::render {

    bool PostProcessRenderer::isReady(PostProcessShader const& shader, GLsizei width, GLsizei height) const {
        return !m_failed && m_shader == &shader && m_program != 0 && m_width == width &&
            m_height == height;
    }

    bool PostProcessRenderer::prepare(PostProcessShader const& shader, GLsizei width, GLsizei height) {
        if (isReady(shader, width, height)) {
            return true;
        }

        glActiveTexture(GL_TEXTURE0);
        if (!initialize(shader)) {
            return false;
        }
        if (width != m_width || height != m_height) {
            glUseProgram(m_program);
            glUniform2f(
                m_invResolutionUniform,
                1.f / static_cast<GLfloat>(width),
                1.f / static_cast<GLfloat>(height)
            );
            m_width = width;
            m_height = height;
        }
        return true;
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

        auto const textureUniform = glGetUniformLocation(m_program, "u_texture");
        m_invResolutionUniform = glGetUniformLocation(m_program, "u_invResolution");
        m_scalarUniform =
            shader.scalarUniform ? glGetUniformLocation(m_program, shader.scalarUniform) : -1;
        if (textureUniform < 0 || m_invResolutionUniform < 0 ||
            (shader.scalarUniform && m_scalarUniform < 0)) {
            log::error("{} shader is missing required uniforms", shader.name);
            destroyResources();
            m_failed = true;
            return false;
        }

        glUseProgram(m_program);
        glUniform1i(textureUniform, 0);

        return true;
    }

    bool PostProcessRenderer::apply(GLuint inputTexture, GLfloat scalar) {
        if (m_failed || m_program == 0 || inputTexture == 0) {
            return false;
        }

        glUseProgram(m_program);
        if (m_scalarUniform >= 0) {
            glUniform1f(m_scalarUniform, scalar);
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        FullscreenQuad::draw();
        return true;
    }

    void PostProcessRenderer::reset() {
        destroyResources();
        m_shader = nullptr;
        m_failed = false;
    }

    void PostProcessRenderer::destroyResources() {
        if (m_program != 0) {
            glDeleteProgram(m_program);
            m_program = 0;
        }

        m_invResolutionUniform = -1;
        m_scalarUniform = -1;
        m_width = 0;
        m_height = 0;
    }

} // namespace bv::render
