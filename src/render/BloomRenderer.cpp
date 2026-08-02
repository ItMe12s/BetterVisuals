#include "BloomRenderer.hpp"

#include "../shaders/fun/BloomShader.hpp"
#include "GlStateGuard.hpp"
#include "ShaderProgram.hpp"

#include <Geode/Geode.hpp>
#include <array>
#include <cstddef>
#include <string_view>

using namespace geode::prelude;

namespace {

    constexpr GLfloat kBlurKernelRadius = 3.f;
    constexpr GLfloat kBloomRadiusAt1080p = 8.f;

    void configureTexture(GLuint texture) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    bool allocateTexture(GLuint texture, GLsizei width, GLsizei height) {
        glBindTexture(GL_TEXTURE_2D, texture);
        while (glGetError() != GL_NO_ERROR) {}
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        return glGetError() == GL_NO_ERROR;
    }

} // namespace

namespace bv::render {

    bool BloomRenderer::isReady(GLsizei width, GLsizei height) const {
        return !m_failed && m_programs[0].handle != 0 && m_width == width && m_height == height;
    }

    bool BloomRenderer::prepare(GLsizei width, GLsizei height) {
        if (isReady(width, height)) {
            return true;
        }

        GlStateGuard state{GlStateProfile::Multipass};
        glActiveTexture(GL_TEXTURE0);
        return initialize() && resizeTextures(width, height);
    }

    bool BloomRenderer::initialize() {
        if (m_failed) {
            return false;
        }
        if (m_programs[0].handle != 0) {
            return true;
        }

        auto const& shaderSet = shaders::kBloomShaderSet;
        std::array fragmentSources{
            shaderSet.prefilterSource,
            shaderSet.blurSource,
            shaderSet.compositeSource,
        };
        constexpr std::array<std::string_view, 3> names{
            "Bloom prefilter",
            "Bloom blur",
            "Bloom composite",
        };
        for (std::size_t index = 0; index < m_programs.size(); ++index) {
            std::array vertexSource{shaderSet.vertexSource};
            std::array fragmentSource{fragmentSources[index]};
            m_programs[index].handle =
                compileShaderProgram(names[index], vertexSource, fragmentSource);
            if (m_programs[index].handle == 0) {
                destroyResources();
                m_failed = true;
                return false;
            }
        }

        for (std::size_t index = 0; index < 2; ++index) {
            auto& program = m_programs[index];
            program.textures[0] = glGetUniformLocation(program.handle, "u_texture");
            program.offset =
                glGetUniformLocation(program.handle, index == 0 ? "u_invResolution" : "u_texelStep");
            if (program.textures[0] < 0 || program.offset < 0) {
                log::error("{} shader is missing required uniforms", names[index]);
                destroyResources();
                m_failed = true;
                return false;
            }
            glUseProgram(program.handle);
            glUniform1i(program.textures[0], 0);
        }

        auto& composite = m_programs[2];
        composite.textures[0] = glGetUniformLocation(composite.handle, "u_source");
        composite.textures[1] = glGetUniformLocation(composite.handle, "u_bloom");
        if (composite.textures[0] < 0 || composite.textures[1] < 0) {
            log::error("Bloom composite shader is missing required uniforms");
            destroyResources();
            m_failed = true;
            return false;
        }
        glUseProgram(composite.handle);
        glUniform1i(composite.textures[0], 0);
        glUniform1i(composite.textures[1], 1);

        std::array<GLuint, 4> textures = {};
        glGenTextures(static_cast<GLsizei>(textures.size()), textures.data());
        m_sourceTexture = textures[0];
        m_prefilterTexture = textures[1];
        m_horizontalTexture = textures[2];
        m_verticalTexture = textures[3];
        glGenFramebuffers(1, &m_framebuffer);
        if (m_sourceTexture == 0 || m_prefilterTexture == 0 || m_horizontalTexture == 0 ||
            m_verticalTexture == 0 || m_framebuffer == 0) {
            log::error("Unable to allocate bloom OpenGL objects");
            destroyResources();
            m_failed = true;
            return false;
        }
        if (!m_quad.initialize("Bloom")) {
            destroyResources();
            m_failed = true;
            return false;
        }

        for (auto texture : textures) {
            configureTexture(texture);
        }
        return true;
    }

    bool BloomRenderer::resizeTextures(GLsizei width, GLsizei height) {
        if (width == m_width && height == m_height) {
            return true;
        }

        auto const halfWidth = (width + 1) / 2;
        auto const halfHeight = (height + 1) / 2;
        if (!allocateTexture(m_sourceTexture, width, height) ||
            !allocateTexture(m_prefilterTexture, halfWidth, halfHeight) ||
            !allocateTexture(m_horizontalTexture, halfWidth, halfHeight) ||
            !allocateTexture(m_verticalTexture, halfWidth, halfHeight)) {
            log::error("Unable to allocate {}x{} bloom textures", width, height);
            destroyResources();
            m_failed = true;
            return false;
        }
        if (!validateFramebuffer(m_prefilterTexture) || !validateFramebuffer(m_horizontalTexture) ||
            !validateFramebuffer(m_verticalTexture)) {
            log::error("Unable to create complete bloom framebuffers");
            destroyResources();
            m_failed = true;
            return false;
        }

        glUseProgram(m_programs[0].handle);
        glUniform2f(
            m_programs[0].offset, 1.f / static_cast<GLfloat>(width), 1.f / static_cast<GLfloat>(height)
        );

        auto const displayScale = static_cast<GLfloat>(height) / 1080.f;
        m_width = width;
        m_height = height;
        m_halfWidth = halfWidth;
        m_halfHeight = halfHeight;
        m_blurStepX =
            kBloomRadiusAt1080p * displayScale / (kBlurKernelRadius * static_cast<GLfloat>(width));
        m_blurStepY =
            kBloomRadiusAt1080p * displayScale / (kBlurKernelRadius * static_cast<GLfloat>(height));
        return true;
    }

    bool BloomRenderer::validateFramebuffer(GLuint texture) {
        bindIntermediateFramebuffer();
        attachIntermediateTexture(texture);
        return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }

    void BloomRenderer::bindIntermediateFramebuffer() {
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    }

    void BloomRenderer::attachIntermediateTexture(GLuint texture) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    }

    bool BloomRenderer::apply() {
        if (m_failed) {
            return false;
        }

        GlStateGuard state{GlStateProfile::Multipass};
        glActiveTexture(GL_TEXTURE0);
        auto const& viewport = state.viewport();
        auto const width = static_cast<GLsizei>(viewport[2]);
        auto const height = static_cast<GLsizei>(viewport[3]);
        if (width <= 0 || height <= 0) {
            return true;
        }
        if (!initialize() || !resizeTextures(width, height)) {
            return false;
        }

        state.bindOriginalFramebuffer();
        glBindTexture(GL_TEXTURE_2D, m_sourceTexture);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, viewport[0], viewport[1], width, height);

        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glViewport(0, 0, m_halfWidth, m_halfHeight);
        m_quad.bind();

        bindIntermediateFramebuffer();
        attachIntermediateTexture(m_prefilterTexture);
        glUseProgram(m_programs[0].handle);
        glBindTexture(GL_TEXTURE_2D, m_sourceTexture);
        m_quad.draw();

        attachIntermediateTexture(m_horizontalTexture);
        glUseProgram(m_programs[1].handle);
        glUniform2f(m_programs[1].offset, m_blurStepX, 0.f);
        glBindTexture(GL_TEXTURE_2D, m_prefilterTexture);
        m_quad.draw();

        attachIntermediateTexture(m_verticalTexture);
        glUniform2f(m_programs[1].offset, 0.f, m_blurStepY);
        glBindTexture(GL_TEXTURE_2D, m_horizontalTexture);
        m_quad.draw();

        state.bindOriginalFramebuffer();
        glViewport(viewport[0], viewport[1], width, height);
        glUseProgram(m_programs[2].handle);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_sourceTexture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_verticalTexture);
        m_quad.draw();
        return true;
    }

    void BloomRenderer::reset() {
        destroyResources();
        m_failed = false;
    }

    void BloomRenderer::destroyResources() {
        m_quad.reset();
        if (m_framebuffer != 0) {
            glDeleteFramebuffers(1, &m_framebuffer);
            m_framebuffer = 0;
        }

        std::array<GLuint, 4> const textures = {
            m_sourceTexture,
            m_prefilterTexture,
            m_horizontalTexture,
            m_verticalTexture,
        };
        glDeleteTextures(static_cast<GLsizei>(textures.size()), textures.data());
        m_sourceTexture = 0;
        m_prefilterTexture = 0;
        m_horizontalTexture = 0;
        m_verticalTexture = 0;

        for (auto& program : m_programs) {
            if (program.handle != 0) {
                glDeleteProgram(program.handle);
            }
            program = {};
        }
        m_width = 0;
        m_height = 0;
        m_halfWidth = 0;
        m_halfHeight = 0;
        m_blurStepX = 0.f;
        m_blurStepY = 0.f;
    }

} // namespace bv::render
