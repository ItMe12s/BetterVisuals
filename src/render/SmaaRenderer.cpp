#include "SmaaRenderer.hpp"

#include "../shaders/aa/SmaaLookupTextures.hpp"
#include "GlStateGuard.hpp"
#include "ShaderProgram.hpp"

#include <Geode/Geode.hpp>
#include <array>
#include <cstddef>

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

    bool uploadTexture(
        GLint internalFormat, GLsizei width, GLsizei height, GLenum format, void const* data
    ) {
        clearGlErrors();
        glTexImage2D(
            GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, data
        );
        return glGetError() == GL_NO_ERROR;
    }

} // namespace

namespace bv::render {

    bool SmaaRenderer::isReady(
        shaders::smaa::ShaderSet const& shaders, GLsizei width, GLsizei height
    ) const {
        return !m_failed && m_shaders == &shaders && m_programs[0].handle != 0 &&
            m_width == width && m_height == height;
    }

    bool SmaaRenderer::prepare(shaders::smaa::ShaderSet const& shaders, GLsizei width, GLsizei height) {
        if (isReady(shaders, width, height)) {
            return true;
        }

        GlStateGuard state{GlStateProfile::Multipass};
        glActiveTexture(GL_TEXTURE0);
        return initialize(shaders) && resizeTextures(width, height);
    }

    bool SmaaRenderer::initialize(shaders::smaa::ShaderSet const& shaders) {
        if (m_shaders != &shaders) {
            destroyResources();
            m_shaders = &shaders;
            m_failed = false;
        }
        if (m_failed) {
            return false;
        }
        if (m_programs[0].handle != 0) {
            return true;
        }

        for (std::size_t index = 0; index < m_programs.size(); ++index) {
            auto const& source = shaders.programs[index];
            std::array vertexSources{
                shaders.commonSource,
                shaders.vertexStageSource,
                shaders.algorithmSource,
                source.vertexMain,
            };
            std::array fragmentSources{
                shaders.commonSource,
                shaders.fragmentStageSource,
                shaders.algorithmSource,
                source.fragmentMain,
            };
            m_programs[index].handle =
                compileShaderProgram(source.name, vertexSources, fragmentSources);
            if (m_programs[index].handle == 0) {
                destroyResources();
                m_failed = true;
                return false;
            }
            m_programs[index].metrics = glGetUniformLocation(m_programs[index].handle, "u_metrics");
            if (m_programs[index].metrics < 0) {
                log::error("{} shader is missing u_metrics", shaders.programs[index].name);
                destroyResources();
                m_failed = true;
                return false;
            }
        }

        m_programs[0].textures[0] = glGetUniformLocation(m_programs[0].handle, "u_colorTexture");
        m_programs[1].textures[0] = glGetUniformLocation(m_programs[1].handle, "u_edgesTexture");
        m_programs[1].textures[1] = glGetUniformLocation(m_programs[1].handle, "u_areaTexture");
        m_programs[1].textures[2] = glGetUniformLocation(m_programs[1].handle, "u_searchTexture");
        m_programs[2].textures[0] = glGetUniformLocation(m_programs[2].handle, "u_colorTexture");
        m_programs[2].textures[1] = glGetUniformLocation(m_programs[2].handle, "u_blendTexture");
        if (m_programs[0].textures[0] < 0 || m_programs[1].textures[0] < 0 ||
            m_programs[1].textures[1] < 0 || m_programs[1].textures[2] < 0 ||
            m_programs[2].textures[0] < 0 || m_programs[2].textures[1] < 0) {
            log::error("SMAA shader is missing required texture uniforms");
            destroyResources();
            m_failed = true;
            return false;
        }

        glUseProgram(m_programs[0].handle);
        glUniform1i(m_programs[0].textures[0], 0);
        glUseProgram(m_programs[1].handle);
        glUniform1i(m_programs[1].textures[0], 0);
        glUniform1i(m_programs[1].textures[1], 1);
        glUniform1i(m_programs[1].textures[2], 2);
        glUseProgram(m_programs[2].handle);
        glUniform1i(m_programs[2].textures[0], 0);
        glUniform1i(m_programs[2].textures[1], 1);

        std::array<GLuint*, 5> textureHandles = {
            &m_sourceTexture,
            &m_edgeTexture,
            &m_weightTexture,
            &m_areaTexture,
            &m_searchTexture,
        };
        std::array<GLuint, 5> textures = {};
        glGenTextures(static_cast<GLsizei>(textures.size()), textures.data());
        for (std::size_t index = 0; index < textures.size(); ++index) {
            *textureHandles[index] = textures[index];
        }
        glGenFramebuffers(1, &m_framebuffer);
        if (m_sourceTexture == 0 || m_edgeTexture == 0 || m_weightTexture == 0 ||
            m_areaTexture == 0 || m_searchTexture == 0 || m_framebuffer == 0) {
            log::error("Unable to allocate SMAA OpenGL objects");
            destroyResources();
            m_failed = true;
            return false;
        }
        if (!m_quad.initialize("SMAA")) {
            destroyResources();
            m_failed = true;
            return false;
        }

        for (auto texture : textures) {
            configureTexture(texture);
        }

        glBindTexture(GL_TEXTURE_2D, m_areaTexture);
        if (!uploadTexture(
                GL_LUMINANCE_ALPHA,
                static_cast<GLsizei>(shaders::smaa::kAreaTextureWidth),
                static_cast<GLsizei>(shaders::smaa::kAreaTextureHeight),
                GL_LUMINANCE_ALPHA,
                shaders::smaa::kAreaTextureBytes.data()
            )) {
            log::error("Unable to upload the SMAA area lookup texture");
            destroyResources();
            m_failed = true;
            return false;
        }

        glBindTexture(GL_TEXTURE_2D, m_searchTexture);
        if (!uploadTexture(
                GL_LUMINANCE,
                static_cast<GLsizei>(shaders::smaa::kSearchTextureWidth),
                static_cast<GLsizei>(shaders::smaa::kSearchTextureHeight),
                GL_LUMINANCE,
                shaders::smaa::kSearchTextureBytes.data()
            )) {
            log::error("Unable to upload the SMAA search lookup texture");
            destroyResources();
            m_failed = true;
            return false;
        }

        return true;
    }

    bool SmaaRenderer::resizeTextures(GLsizei width, GLsizei height) {
        if (width == m_width && height == m_height) {
            return true;
        }

        for (auto texture : {m_sourceTexture, m_edgeTexture, m_weightTexture}) {
            glBindTexture(GL_TEXTURE_2D, texture);
            if (!uploadTexture(GL_RGBA, width, height, GL_RGBA, nullptr)) {
                log::error("Unable to allocate a {}x{} SMAA framebuffer texture", width, height);
                destroyResources();
                m_failed = true;
                return false;
            }
        }

        if (!validateFramebuffer(m_edgeTexture) || !validateFramebuffer(m_weightTexture)) {
            log::error("Unable to create complete SMAA framebuffers");
            destroyResources();
            m_failed = true;
            return false;
        }

        for (auto const& program : m_programs) {
            glUseProgram(program.handle);
            glUniform4f(
                program.metrics,
                1.f / static_cast<GLfloat>(width),
                1.f / static_cast<GLfloat>(height),
                static_cast<GLfloat>(width),
                static_cast<GLfloat>(height)
            );
        }

        m_width = width;
        m_height = height;
        return true;
    }

    bool SmaaRenderer::validateFramebuffer(GLuint texture) {
        bindIntermediateFramebuffer();
        attachIntermediateTexture(texture);
        return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }

    void SmaaRenderer::bindIntermediateFramebuffer() {
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    }

    void SmaaRenderer::attachIntermediateTexture(GLuint texture) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    }

    bool SmaaRenderer::apply(shaders::smaa::ShaderSet const& shaders) {
        if (m_failed && m_shaders == &shaders) {
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
        if (!initialize(shaders) || !resizeTextures(width, height)) {
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
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glViewport(0, 0, width, height);
        m_quad.bind();

        bindIntermediateFramebuffer();
        attachIntermediateTexture(m_edgeTexture);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(m_programs[0].handle);
        m_quad.draw();

        attachIntermediateTexture(m_weightTexture);
        glUseProgram(m_programs[1].handle);
        glBindTexture(GL_TEXTURE_2D, m_edgeTexture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_areaTexture);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_searchTexture);
        m_quad.draw();

        state.bindOriginalFramebuffer();
        glViewport(viewport[0], viewport[1], width, height);
        glUseProgram(m_programs[2].handle);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_sourceTexture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_weightTexture);
        m_quad.draw();
        return true;
    }

    void SmaaRenderer::reset() {
        destroyResources();
        m_shaders = nullptr;
        m_failed = false;
    }

    void SmaaRenderer::destroyResources() {
        m_quad.reset();
        if (m_framebuffer != 0) {
            glDeleteFramebuffers(1, &m_framebuffer);
            m_framebuffer = 0;
        }

        std::array<GLuint, 5> const textures = {
            m_sourceTexture,
            m_edgeTexture,
            m_weightTexture,
            m_areaTexture,
            m_searchTexture,
        };
        glDeleteTextures(static_cast<GLsizei>(textures.size()), textures.data());
        m_sourceTexture = 0;
        m_edgeTexture = 0;
        m_weightTexture = 0;
        m_areaTexture = 0;
        m_searchTexture = 0;

        for (auto& program : m_programs) {
            if (program.handle != 0) {
                glDeleteProgram(program.handle);
            }
            program = {};
        }
        m_width = 0;
        m_height = 0;
    }

} // namespace bv::render
