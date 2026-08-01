#include "SmaaRenderer.hpp"

#include "../shaders/SmaaLookupTextures.hpp"
#include "../shaders/SmaaShader.hpp"
#include "GlStateGuard.hpp"

#include <Geode/Geode.hpp>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

using namespace geode::prelude;

namespace {

    struct Vertex {
        GLfloat position[2];
        GLfloat texCoord[2];
    };

    constexpr std::array<Vertex, 4> kFullscreenQuad = {{
        {{-1.f, -1.f}, {0.f, 0.f}},
        {{1.f, -1.f}, {1.f, 0.f}},
        {{-1.f, 1.f}, {0.f, 1.f}},
        {{1.f, 1.f}, {1.f, 1.f}},
    }};

    std::string shaderLog(GLuint shader) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        if (length <= 1) {
            return {};
        }

        std::vector<GLchar> buffer(static_cast<std::size_t>(length));
        glGetShaderInfoLog(shader, length, nullptr, buffer.data());
        return buffer.data();
    }

    std::string programLog(GLuint program) {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        if (length <= 1) {
            return {};
        }

        std::vector<GLchar> buffer(static_cast<std::size_t>(length));
        glGetProgramInfoLog(program, length, nullptr, buffer.data());
        return buffer.data();
    }

    GLuint compileShader(
        GLenum type, aa::shaders::SmaaShaderSet const& shaders, std::string_view stageDefines,
        std::string_view mainSource, std::string_view programName, char const* stageName
    ) {
        auto shader = glCreateShader(type);
        if (shader == 0) {
            log::error("Unable to create the {} {} shader", programName, stageName);
            return 0;
        }

        std::array<std::string_view, 5> const sources = {
            shaders.version,
            shaders.portDefines,
            stageDefines,
            shaders.library,
            mainSource,
        };
        std::array<GLchar const*, 5> sourceData = {};
        std::array<GLint, 5> sourceLengths = {};
        for (std::size_t index = 0; index < sources.size(); ++index) {
            sourceData[index] = sources[index].data();
            sourceLengths[index] = static_cast<GLint>(sources[index].size());
        }
        glShaderSource(
            shader, static_cast<GLsizei>(sourceData.size()), sourceData.data(), sourceLengths.data()
        );
        glCompileShader(shader);

        GLint compiled = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (compiled != GL_TRUE) {
            log::error(
                "{} {} shader compilation failed: {}", programName, stageName, shaderLog(shader)
            );
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    GLuint compileProgram(
        aa::shaders::SmaaShaderSet const& shaders, aa::shaders::smaa::ProgramSource const& source
    ) {
        auto vertexShader = compileShader(
            GL_VERTEX_SHADER,
            shaders,
            shaders.vertexDefines,
            source.vertexMain,
            source.name,
            "vertex"
        );
        if (vertexShader == 0) {
            return 0;
        }

        auto fragmentShader = compileShader(
            GL_FRAGMENT_SHADER,
            shaders,
            shaders.fragmentDefines,
            source.fragmentMain,
            source.name,
            "fragment"
        );
        if (fragmentShader == 0) {
            glDeleteShader(vertexShader);
            return 0;
        }

        auto program = glCreateProgram();
        if (program != 0) {
            glAttachShader(program, vertexShader);
            glAttachShader(program, fragmentShader);
            glBindAttribLocation(program, aa::render::kPositionAttribute, "a_position");
            glBindAttribLocation(program, aa::render::kTexCoordAttribute, "a_texCoord");
            glLinkProgram(program);
        }
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        GLint linked = GL_FALSE;
        if (program != 0) {
            glGetProgramiv(program, GL_LINK_STATUS, &linked);
        }
        if (linked != GL_TRUE) {
            log::error(
                "{} shader link failed: {}",
                source.name,
                program == 0 ? "unable to create program" : programLog(program)
            );
            if (program != 0) {
                glDeleteProgram(program);
            }
            return 0;
        }
        return program;
    }

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
            m_programs[index].handle = compileProgram(shaders, shaders.programs[index]);
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
        glGenBuffers(1, &m_vbo);
        if (m_coreFramebufferApi) {
            glGenFramebuffers(1, &m_framebuffer);
        }
        else {
            glGenFramebuffersEXT(1, &m_framebuffer);
        }
        if (m_sourceTexture == 0 || m_edgeTexture == 0 || m_weightTexture == 0 ||
            m_areaTexture == 0 || m_searchTexture == 0 || m_vbo == 0 || m_framebuffer == 0) {
            log::error("Unable to allocate SMAA OpenGL objects");
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

        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(sizeof(kFullscreenQuad)),
            kFullscreenQuad.data(),
            GL_STATIC_DRAW
        );
        GLint bufferSize = 0;
        glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufferSize);
        if (bufferSize != static_cast<GLint>(sizeof(kFullscreenQuad))) {
            log::error("Unable to upload the SMAA fullscreen quad");
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

    void SmaaRenderer::drawFullscreen() {
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glEnableVertexAttribArray(kPositionAttribute);
        glEnableVertexAttribArray(kTexCoordAttribute);
        glVertexAttribPointer(
            kPositionAttribute,
            2,
            GL_FLOAT,
            GL_FALSE,
            sizeof(Vertex),
            reinterpret_cast<void*>(offsetof(Vertex, position))
        );
        glVertexAttribPointer(
            kTexCoordAttribute,
            2,
            GL_FLOAT,
            GL_FALSE,
            sizeof(Vertex),
            reinterpret_cast<void*>(offsetof(Vertex, texCoord))
        );
        glDrawArrays(GL_TRIANGLE_STRIP, 0, static_cast<GLsizei>(kFullscreenQuad.size()));
    }

    void SmaaRenderer::apply(shaders::SmaaShaderSet const& shaders) {
        if (m_failed && m_shaders == &shaders) {
            return;
        }

        GlStateGuard state;
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
        glDepthMask(GL_FALSE);
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glViewport(0, 0, width, height);

        auto const setMetrics = [&](Program const& program) {
            glUseProgram(program.handle);
            glUniform4f(
                program.metrics,
                1.f / static_cast<GLfloat>(width),
                1.f / static_cast<GLfloat>(height),
                static_cast<GLfloat>(width),
                static_cast<GLfloat>(height)
            );
        };

        bindIntermediateFramebuffer();
        attachIntermediateTexture(m_edgeTexture);
        glClear(GL_COLOR_BUFFER_BIT);
        setMetrics(m_programs[0]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_sourceTexture);
        glUniform1i(m_programs[0].textures[0], 0);
        drawFullscreen();

        attachIntermediateTexture(m_weightTexture);
        glClear(GL_COLOR_BUFFER_BIT);
        setMetrics(m_programs[1]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_edgeTexture);
        glUniform1i(m_programs[1].textures[0], 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_areaTexture);
        glUniform1i(m_programs[1].textures[1], 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_searchTexture);
        glUniform1i(m_programs[1].textures[2], 2);
        drawFullscreen();

        state.bindOriginalDrawFramebuffer();
        glViewport(viewport[0], viewport[1], width, height);
        setMetrics(m_programs[2]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_sourceTexture);
        glUniform1i(m_programs[2].textures[0], 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_weightTexture);
        glUniform1i(m_programs[2].textures[1], 1);
        drawFullscreen();
    }

    void SmaaRenderer::reset() {
        destroyResources();
        m_shaders = nullptr;
        m_failed = false;
    }

    void SmaaRenderer::destroyResources() {
        if (m_vbo != 0) {
            glDeleteBuffers(1, &m_vbo);
            m_vbo = 0;
        }
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
        for (auto texture : textures) {
            if (texture != 0) {
                glDeleteTextures(1, &texture);
            }
        }
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

} // namespace aa::render
