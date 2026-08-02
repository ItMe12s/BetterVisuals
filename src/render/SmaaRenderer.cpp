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

    bool textureHasSize(GLsizei width, GLsizei height) {
        GLint allocatedWidth = 0;
        GLint allocatedHeight = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &allocatedWidth);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &allocatedHeight);
        return allocatedWidth == width && allocatedHeight == height;
    }

} // namespace

namespace aa::render {

    bool SmaaRenderer::initialize(shaders::SmaaShaderSet const& shaders) {
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

        m_coreFramebufferApi = GLEW_VERSION_3_0 || GLEW_ARB_framebuffer_object;
        if (!m_coreFramebufferApi && !GLEW_EXT_framebuffer_object) {
            log::error("SMAA requires framebuffer object support");
            m_failed = true;
            return false;
        }

        for (std::size_t index = 0; index < m_programs.size(); ++index) {
            auto const& source = shaders.programs[index];
            std::array vertexSources{
                shaders.versionSource,
                shaders.commonSource,
                shaders.vertexStageSource,
                shaders.algorithmSource,
                source.vertexMain,
            };
            std::array fragmentSources{
                shaders.versionSource,
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
        if (m_coreFramebufferApi) {
            glGenFramebuffers(1, &m_framebuffer);
        }
        else {
            glGenFramebuffersEXT(1, &m_framebuffer);
        }
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
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_LUMINANCE8_ALPHA8,
            static_cast<GLsizei>(shaders::smaa::kAreaTextureWidth),
            static_cast<GLsizei>(shaders::smaa::kAreaTextureHeight),
            0,
            GL_LUMINANCE_ALPHA,
            GL_UNSIGNED_BYTE,
            shaders::smaa::kAreaTextureBytes.data()
        );
        if (!textureHasSize(
                static_cast<GLsizei>(shaders::smaa::kAreaTextureWidth),
                static_cast<GLsizei>(shaders::smaa::kAreaTextureHeight)
            )) {
            log::error("Unable to upload the SMAA area lookup texture");
            destroyResources();
            m_failed = true;
            return false;
        }

        glBindTexture(GL_TEXTURE_2D, m_searchTexture);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_LUMINANCE8,
            static_cast<GLsizei>(shaders::smaa::kSearchTextureWidth),
            static_cast<GLsizei>(shaders::smaa::kSearchTextureHeight),
            0,
            GL_LUMINANCE,
            GL_UNSIGNED_BYTE,
            shaders::smaa::kSearchTextureBytes.data()
        );
        if (!textureHasSize(
                static_cast<GLsizei>(shaders::smaa::kSearchTextureWidth),
                static_cast<GLsizei>(shaders::smaa::kSearchTextureHeight)
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
            glTexImage2D(
                GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr
            );
            if (!textureHasSize(width, height)) {
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
        auto const status = m_coreFramebufferApi ? glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) :
                                                   glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
        return status == GL_FRAMEBUFFER_COMPLETE;
    }

    void SmaaRenderer::bindIntermediateFramebuffer() {
        if (m_coreFramebufferApi) {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_framebuffer);
        }
        else {
            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, m_framebuffer);
        }
    }

    void SmaaRenderer::attachIntermediateTexture(GLuint texture) {
        if (m_coreFramebufferApi) {
            glFramebufferTexture2D(
                GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0
            );
        }
        else {
            glFramebufferTexture2DEXT(
                GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, texture, 0
            );
        }
    }

    void SmaaRenderer::apply(shaders::SmaaShaderSet const& shaders) {
        if (m_failed && m_shaders == &shaders) {
            return;
        }

        GlStateGuard state{GlStateProfile::Multipass};
        glActiveTexture(GL_TEXTURE0);
        auto const& viewport = state.viewport();
        auto const width = static_cast<GLsizei>(viewport[2]);
        auto const height = static_cast<GLsizei>(viewport[3]);
        if (width <= 0 || height <= 0) {
            return;
        }
        if (!initialize(shaders) || !resizeTextures(width, height)) {
            return;
        }

        state.bindOriginalReadFramebuffer();
        glBindTexture(GL_TEXTURE_2D, m_sourceTexture);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, viewport[0], viewport[1], width, height);

        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        if (GLEW_VERSION_3_0 || GLEW_ARB_framebuffer_sRGB || GLEW_EXT_framebuffer_sRGB) {
            glDisable(GL_FRAMEBUFFER_SRGB);
        }
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

        state.bindOriginalDrawFramebuffer();
        glViewport(viewport[0], viewport[1], width, height);
        glUseProgram(m_programs[2].handle);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_sourceTexture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_weightTexture);
        m_quad.draw();
    }

    void SmaaRenderer::reset() {
        destroyResources();
        m_shaders = nullptr;
        m_failed = false;
    }

    void SmaaRenderer::destroyResources() {
        m_quad.reset();
        if (m_framebuffer != 0) {
            if (m_coreFramebufferApi) {
                glDeleteFramebuffers(1, &m_framebuffer);
            }
            else {
                glDeleteFramebuffersEXT(1, &m_framebuffer);
            }
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
        m_coreFramebufferApi = false;
    }

} // namespace aa::render
