#include "SmaaRenderer.hpp"

#include "../shaders/aa/SmaaLookupTextures.hpp"
#include "FullscreenQuad.hpp"
#include "Gl.hpp"
#include "ShaderProgram.hpp"

#include <Geode/Geode.hpp>
#include <array>
#include <cassert>
#include <cstddef>

using namespace geode::prelude;

namespace bv::render {

    bool SmaaRenderer::prepare(shaders::smaa::ShaderSet const& shaders, GLsizei width, GLsizei height) {
        glActiveTexture(GL_TEXTURE0);
        return initialize(shaders) && resizeTextures(width, height);
    }

    bool SmaaRenderer::initialize(shaders::smaa::ShaderSet const& shaders) {
        if (m_shaders != &shaders) {
            destroyResources();
            m_shaders = &shaders;
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
                return false;
            }
            m_programs[index].metrics = glGetUniformLocation(m_programs[index].handle, "u_metrics");
            if (m_programs[index].metrics < 0) {
                log::error("{} shader is missing u_metrics", shaders.programs[index].name);
                destroyResources();
                return false;
            }
        }

        auto const colorTexture = glGetUniformLocation(m_programs[0].handle, "u_colorTexture");
        auto const edgesTexture = glGetUniformLocation(m_programs[1].handle, "u_edgesTexture");
        auto const areaTexture = glGetUniformLocation(m_programs[1].handle, "u_areaTexture");
        auto const searchTexture = glGetUniformLocation(m_programs[1].handle, "u_searchTexture");
        auto const neighborhoodColor = glGetUniformLocation(m_programs[2].handle, "u_colorTexture");
        auto const blendTexture = glGetUniformLocation(m_programs[2].handle, "u_blendTexture");
        if (colorTexture < 0 || edgesTexture < 0 || areaTexture < 0 || searchTexture < 0 ||
            neighborhoodColor < 0 || blendTexture < 0) {
            log::error("SMAA shader is missing required texture uniforms");
            destroyResources();
            return false;
        }

        glUseProgram(m_programs[0].handle);
        glUniform1i(colorTexture, 0);
        glUseProgram(m_programs[1].handle);
        glUniform1i(edgesTexture, 0);
        glUniform1i(areaTexture, 1);
        glUniform1i(searchTexture, 2);
        glUseProgram(m_programs[2].handle);
        glUniform1i(neighborhoodColor, 0);
        glUniform1i(blendTexture, 1);

        std::array<GLuint*, 4> textureHandles = {
            &m_edgeTexture,
            &m_weightTexture,
            &m_areaTexture,
            &m_searchTexture,
        };
        std::array<GLuint, 4> textures = {};
        glGenTextures(static_cast<GLsizei>(textures.size()), textures.data());
        glGenFramebuffers(static_cast<GLsizei>(m_framebuffers.size()), m_framebuffers.data());
        for (std::size_t index = 0; index < textures.size(); ++index) {
            *textureHandles[index] = textures[index];
        }
        if (m_edgeTexture == 0 || m_weightTexture == 0 || m_areaTexture == 0 ||
            m_searchTexture == 0 || m_framebuffers[0] == 0 || m_framebuffers[1] == 0) {
            log::error("Unable to allocate SMAA OpenGL objects");
            destroyResources();
            return false;
        }
        for (auto texture : textures) {
            gl::configureTexture(texture);
        }

        glBindTexture(GL_TEXTURE_2D, m_areaTexture);
        if (!gl::uploadTexture(
                GL_LUMINANCE_ALPHA,
                static_cast<GLsizei>(shaders::smaa::kAreaTextureWidth),
                static_cast<GLsizei>(shaders::smaa::kAreaTextureHeight),
                GL_LUMINANCE_ALPHA,
                shaders::smaa::kAreaTextureBytes.data()
            )) {
            log::error("Unable to upload the SMAA area lookup texture");
            destroyResources();
            return false;
        }

        glBindTexture(GL_TEXTURE_2D, m_searchTexture);
        if (!gl::uploadTexture(
                GL_LUMINANCE,
                static_cast<GLsizei>(shaders::smaa::kSearchTextureWidth),
                static_cast<GLsizei>(shaders::smaa::kSearchTextureHeight),
                GL_LUMINANCE,
                shaders::smaa::kSearchTextureBytes.data()
            )) {
            log::error("Unable to upload the SMAA search lookup texture");
            destroyResources();
            return false;
        }

        return true;
    }

    bool SmaaRenderer::resizeTextures(GLsizei width, GLsizei height) {
        if (width == m_width && height == m_height) {
            return true;
        }

        for (auto texture : {m_edgeTexture, m_weightTexture}) {
            glBindTexture(GL_TEXTURE_2D, texture);
            if (!gl::uploadTexture(GL_RGBA, width, height, GL_RGBA, nullptr)) {
                log::error("Unable to allocate a {}x{} SMAA framebuffer texture", width, height);
                destroyResources();
                return false;
            }
        }

        std::array textures{m_edgeTexture, m_weightTexture};
        for (std::size_t index = 0; index < m_framebuffers.size(); ++index) {
            glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffers[index]);
            glFramebufferTexture2D(
                GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[index], 0
            );
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                log::error("Unable to create complete SMAA framebuffer {}", index);
                destroyResources();
                return false;
            }
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

    void SmaaRenderer::apply(GLuint inputTexture, RenderTarget const& target) {
        assert(m_programs[0].handle != 0 && inputTexture != 0);

        glActiveTexture(GL_TEXTURE0);
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glViewport(0, 0, m_width, m_height);
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffers[0]);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(m_programs[0].handle);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        FullscreenQuad::draw();

        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffers[1]);
        glUseProgram(m_programs[1].handle);
        glBindTexture(GL_TEXTURE_2D, m_edgeTexture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_areaTexture);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_searchTexture);
        FullscreenQuad::draw();

        glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
        glViewport(target.x, target.y, target.width, target.height);
        glUseProgram(m_programs[2].handle);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_weightTexture);
        FullscreenQuad::draw();
    }

    void SmaaRenderer::reset() {
        destroyResources();
        m_shaders = nullptr;
    }

    void SmaaRenderer::destroyResources() {
        std::array<GLuint, 4> const textures = {
            m_edgeTexture,
            m_weightTexture,
            m_areaTexture,
            m_searchTexture,
        };
        glDeleteTextures(static_cast<GLsizei>(textures.size()), textures.data());
        glDeleteFramebuffers(static_cast<GLsizei>(m_framebuffers.size()), m_framebuffers.data());
        m_framebuffers = {};
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
