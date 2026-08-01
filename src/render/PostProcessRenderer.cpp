#include "PostProcessRenderer.hpp"

#include <Geode/Geode.hpp>
#include <array>
#include <cassert>
#include <cstddef>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace {

    constexpr GLuint kPositionAttribute = 0;
    constexpr GLuint kTexCoordAttribute = 1;

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

    struct VertexAttributeState {
        GLint enabled = GL_FALSE;
        GLint buffer = 0;
        GLint size = 0;
        GLint stride = 0;
        GLint type = GL_FLOAT;
        GLint normalized = GL_FALSE;
        void* pointer = nullptr;

        bool operator==(VertexAttributeState const&) const = default;
    };

    struct GlState {
        GLint program = 0;
        GLint activeTexture = GL_TEXTURE0;
        GLint texture2D = 0;
        GLint arrayBuffer = 0;
        std::array<GLint, 4> viewport = {};
        std::array<GLboolean, 4> colorMask = {};
        GLboolean depthMask = GL_TRUE;
        GLboolean blend = GL_FALSE;
        GLboolean depthTest = GL_FALSE;
        GLboolean stencilTest = GL_FALSE;
        GLboolean scissorTest = GL_FALSE;
        GLboolean cullFace = GL_FALSE;
        std::array<VertexAttributeState, 2> attributes = {};

        bool operator==(GlState const&) const = default;
    };

    VertexAttributeState captureVertexAttribute(GLuint index) {
        VertexAttributeState state;
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &state.enabled);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &state.buffer);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_SIZE, &state.size);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &state.stride);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_TYPE, &state.type);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &state.normalized);
        glGetVertexAttribPointerv(index, GL_VERTEX_ATTRIB_ARRAY_POINTER, &state.pointer);
        return state;
    }

    GlState captureGlState() {
        GlState state;
        glGetIntegerv(GL_CURRENT_PROGRAM, &state.program);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &state.activeTexture);

        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.texture2D);
        glActiveTexture(state.activeTexture);

        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state.arrayBuffer);
        glGetIntegerv(GL_VIEWPORT, state.viewport.data());
        glGetBooleanv(GL_COLOR_WRITEMASK, state.colorMask.data());
        glGetBooleanv(GL_DEPTH_WRITEMASK, &state.depthMask);
        state.blend = glIsEnabled(GL_BLEND);
        state.depthTest = glIsEnabled(GL_DEPTH_TEST);
        state.stencilTest = glIsEnabled(GL_STENCIL_TEST);
        state.scissorTest = glIsEnabled(GL_SCISSOR_TEST);
        state.cullFace = glIsEnabled(GL_CULL_FACE);
        state.attributes[0] = captureVertexAttribute(kPositionAttribute);
        state.attributes[1] = captureVertexAttribute(kTexCoordAttribute);
        return state;
    }

    void restoreCapability(GLenum capability, GLboolean enabled) {
        if (enabled == GL_TRUE) {
            glEnable(capability);
        }
        else {
            glDisable(capability);
        }
    }

    void restoreVertexAttribute(GLuint index, VertexAttributeState const& state) {
        glBindBuffer(GL_ARRAY_BUFFER, state.buffer);
        glVertexAttribPointer(
            index,
            state.size,
            state.type,
            static_cast<GLboolean>(state.normalized),
            state.stride,
            state.pointer
        );

        if (state.enabled == GL_TRUE) {
            glEnableVertexAttribArray(index);
        }
        else {
            glDisableVertexAttribArray(index);
        }
    }

    void restoreGlState(GlState const& state) {
        restoreVertexAttribute(kPositionAttribute, state.attributes[0]);
        restoreVertexAttribute(kTexCoordAttribute, state.attributes[1]);
        glBindBuffer(GL_ARRAY_BUFFER, state.arrayBuffer);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, state.texture2D);
        glActiveTexture(state.activeTexture);
        glUseProgram(state.program);

        glViewport(state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
        glColorMask(state.colorMask[0], state.colorMask[1], state.colorMask[2], state.colorMask[3]);
        glDepthMask(state.depthMask);
        restoreCapability(GL_BLEND, state.blend);
        restoreCapability(GL_DEPTH_TEST, state.depthTest);
        restoreCapability(GL_STENCIL_TEST, state.stencilTest);
        restoreCapability(GL_SCISSOR_TEST, state.scissorTest);
        restoreCapability(GL_CULL_FACE, state.cullFace);
    }

    class ScopedGlState final {
    public:
        ScopedGlState() : m_state(captureGlState()) {}

        ~ScopedGlState() {
            restoreGlState(m_state);
#ifndef NDEBUG
            auto const restored = captureGlState();
            assert((restored == m_state) && "Post-process renderer failed to restore OpenGL state");
#endif
        }

        std::array<GLint, 4> const& viewport() const {
            return m_state.viewport;
        }

    private:
        GlState m_state;
    };

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
        GLenum type, std::string_view source, std::string_view shaderName, char const* stageName
    ) {
        auto shader = glCreateShader(type);
        if (shader == 0) {
            log::error("Unable to create the {} {} shader", shaderName, stageName);
            return 0;
        }

        auto const* sourceData = source.data();
        auto const sourceLength = static_cast<GLint>(source.size());
        glShaderSource(shader, 1, &sourceData, &sourceLength);
        glCompileShader(shader);

        GLint compiled = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (compiled != GL_TRUE) {
            log::error("{} {} shader compilation failed: {}", shaderName, stageName, shaderLog(shader));
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

} // namespace

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
        if (m_program != 0 && m_texture != 0 && m_vbo != 0) {
            return true;
        }

        auto vertexShader =
            compileShader(GL_VERTEX_SHADER, shader.vertexSource, shader.name, "vertex");
        if (vertexShader == 0) {
            m_failed = true;
            return false;
        }

        auto fragmentShader =
            compileShader(GL_FRAGMENT_SHADER, shader.fragmentSource, shader.name, "fragment");
        if (fragmentShader == 0) {
            glDeleteShader(vertexShader);
            m_failed = true;
            return false;
        }

        m_program = glCreateProgram();
        if (m_program != 0) {
            glAttachShader(m_program, vertexShader);
            glAttachShader(m_program, fragmentShader);
            glBindAttribLocation(m_program, kPositionAttribute, "a_position");
            glBindAttribLocation(m_program, kTexCoordAttribute, "a_texCoord");
            glLinkProgram(m_program);
        }

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        GLint linked = GL_FALSE;
        if (m_program != 0) {
            glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
        }
        if (linked != GL_TRUE) {
            log::error(
                "{} shader link failed: {}",
                shader.name,
                m_program == 0 ? "unable to create program" : programLog(m_program)
            );
            destroyResources();
            m_failed = true;
            return false;
        }

        m_textureUniform = glGetUniformLocation(m_program, "u_texture");
        m_invResolutionUniform = glGetUniformLocation(m_program, "u_invResolution");
        if (m_textureUniform < 0 || m_invResolutionUniform < 0) {
            log::error("{} shader is missing required uniforms", shader.name);
            destroyResources();
            m_failed = true;
            return false;
        }

        glGenTextures(1, &m_texture);
        glGenBuffers(1, &m_vbo);
        if (m_texture == 0 || m_vbo == 0) {
            log::error("Unable to allocate post-process OpenGL objects");
            destroyResources();
            m_failed = true;
            return false;
        }

        glBindTexture(GL_TEXTURE_2D, m_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

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
            log::error("Unable to upload the post-process fullscreen quad");
            destroyResources();
            m_failed = true;
            return false;
        }

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

        m_width = width;
        m_height = height;
        return true;
    }

    void PostProcessRenderer::apply(PostProcessShader const& shader) {
        if (m_failed && m_shader == &shader) {
            return;
        }

        ScopedGlState state;
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
        glUniform1i(m_textureUniform, 0);
        glUniform2f(
            m_invResolutionUniform, 1.f / static_cast<GLfloat>(width), 1.f / static_cast<GLfloat>(height)
        );

        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_FALSE);
        glViewport(viewport[0], viewport[1], width, height);

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

    void PostProcessRenderer::reset() {
        destroyResources();
        m_shader = nullptr;
        m_failed = false;
    }

    void PostProcessRenderer::destroyResources() {
        if (m_vbo != 0) {
            glDeleteBuffers(1, &m_vbo);
            m_vbo = 0;
        }
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
        m_width = 0;
        m_height = 0;
    }

} // namespace aa::render
