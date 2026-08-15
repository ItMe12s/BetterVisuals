#include "BloomRenderer.hpp"

#include "../shaders/fun/BloomShader.hpp"
#include "FullscreenQuad.hpp"
#include "Gl.hpp"
#include "ShaderProgram.hpp"

#include <Geode/Geode.hpp>
#include <array>
#include <cassert>
#include <cstddef>
#include <string_view>

using namespace geode::prelude;

namespace {

    constexpr GLfloat kBlurKernelRadius = 3.f;
    constexpr GLfloat kBlurWideStep = 1.41421356f;

} // namespace

namespace bv::render {

    bool BloomRenderer::prepare(GLsizei width, GLsizei height) {
        glActiveTexture(GL_TEXTURE0);
        return initialize() && resizeTextures(width, height);
    }

    bool BloomRenderer::initialize() {
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
                return false;
            }
        }

        for (std::size_t index = 0; index < 2; ++index) {
            auto& program = m_programs[index];
            auto const texture = glGetUniformLocation(program.handle, "u_texture");
            program.offset =
                glGetUniformLocation(program.handle, index == 0 ? "u_invResolution" : "u_texelStep");
            if (texture < 0 || program.offset < 0) {
                log::error("{} shader is missing required uniforms", names[index]);
                destroyResources();
                return false;
            }
            glUseProgram(program.handle);
            glUniform1i(texture, 0);
        }

        auto& composite = m_programs[2];
        auto const sourceTexture = glGetUniformLocation(composite.handle, "u_source");
        auto const bloomTexture = glGetUniformLocation(composite.handle, "u_bloom");
        if (sourceTexture < 0 || bloomTexture < 0) {
            log::error("Bloom composite shader is missing required uniforms");
            destroyResources();
            return false;
        }
        glUseProgram(composite.handle);
        glUniform1i(sourceTexture, 0);
        glUniform1i(bloomTexture, 1);

        m_programs[0].param = glGetUniformLocation(m_programs[0].handle, "u_threshold");
        m_programs[2].param = glGetUniformLocation(m_programs[2].handle, "u_intensity");
        if (m_programs[0].param < 0 || m_programs[2].param < 0) {
            log::error("Bloom shaders are missing required parameter uniforms");
            destroyResources();
            return false;
        }

        std::array<GLuint, 2> textures = {};
        glGenTextures(static_cast<GLsizei>(textures.size()), textures.data());
        glGenFramebuffers(static_cast<GLsizei>(m_framebuffers.size()), m_framebuffers.data());
        m_bloomTexture = textures[0];
        m_horizontalTexture = textures[1];
        if (m_bloomTexture == 0 || m_horizontalTexture == 0 || m_framebuffers[0] == 0 ||
            m_framebuffers[1] == 0) {
            log::error("Unable to allocate bloom OpenGL objects");
            destroyResources();
            return false;
        }
        for (auto texture : textures) {
            gl::configureTexture(texture);
        }
        return true;
    }

    bool BloomRenderer::resizeTextures(GLsizei width, GLsizei height) {
        if (width == m_width && height == m_height) {
            return true;
        }

        auto const halfWidth = (width + 1) / 2;
        auto const halfHeight = (height + 1) / 2;
        if (!gl::allocateTexture(m_bloomTexture, halfWidth, halfHeight) ||
            !gl::allocateTexture(m_horizontalTexture, halfWidth, halfHeight)) {
            log::error("Unable to allocate {}x{} bloom textures", width, height);
            destroyResources();
            return false;
        }
        std::array textures{m_bloomTexture, m_horizontalTexture};
        for (std::size_t index = 0; index < m_framebuffers.size(); ++index) {
            glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffers[index]);
            glFramebufferTexture2D(
                GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[index], 0
            );
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                log::error("Unable to create complete bloom framebuffer {}", index);
                destroyResources();
                return false;
            }
        }

        glUseProgram(m_programs[0].handle);
        glUniform2f(
            m_programs[0].offset, 1.f / static_cast<GLfloat>(width), 1.f / static_cast<GLfloat>(height)
        );

        m_width = width;
        m_height = height;
        m_halfWidth = halfWidth;
        m_halfHeight = halfHeight;
        updateBlurStep();
        return true;
    }

    void BloomRenderer::updateBlurStep() {
        if (m_width == 0) {
            return;
        }

        auto const displayScale = static_cast<GLfloat>(m_height) / 1080.f;
        m_blurStepX = m_radiusAt1080p * displayScale /
            (kBlurKernelRadius * static_cast<GLfloat>(m_width)) * kBlurWideStep;
        m_blurStepY = m_radiusAt1080p * displayScale /
            (kBlurKernelRadius * static_cast<GLfloat>(m_height)) * kBlurWideStep;
    }

    void BloomRenderer::setParams(GLfloat threshold, GLfloat intensity, GLfloat radius) {
        m_threshold = threshold;
        m_intensity = intensity;
        m_radiusAt1080p = radius;
        updateBlurStep();
    }

    void BloomRenderer::apply(GLuint inputTexture, RenderTarget const& target) {
        assert(m_programs[0].handle != 0 && inputTexture != 0);

        glActiveTexture(GL_TEXTURE0);
        glViewport(0, 0, m_halfWidth, m_halfHeight);
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffers[0]);
        glUseProgram(m_programs[0].handle);
        glUniform1f(m_programs[0].param, m_threshold);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        FullscreenQuad::draw();

        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffers[1]);
        glUseProgram(m_programs[1].handle);
        glUniform2f(m_programs[1].offset, m_blurStepX, 0.f);
        glBindTexture(GL_TEXTURE_2D, m_bloomTexture);
        FullscreenQuad::draw();

        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffers[0]);
        glUniform2f(m_programs[1].offset, 0.f, m_blurStepY);
        glBindTexture(GL_TEXTURE_2D, m_horizontalTexture);
        FullscreenQuad::draw();

        glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
        glViewport(target.x, target.y, target.width, target.height);
        glUseProgram(m_programs[2].handle);
        glUniform1f(m_programs[2].param, m_intensity);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_bloomTexture);
        FullscreenQuad::draw();
    }

    void BloomRenderer::reset() {
        destroyResources();
    }

    void BloomRenderer::destroyResources() {
        std::array<GLuint, 2> const textures = {
            m_bloomTexture,
            m_horizontalTexture,
        };
        glDeleteTextures(static_cast<GLsizei>(textures.size()), textures.data());
        glDeleteFramebuffers(static_cast<GLsizei>(m_framebuffers.size()), m_framebuffers.data());
        m_framebuffers = {};
        m_bloomTexture = 0;
        m_horizontalTexture = 0;

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
