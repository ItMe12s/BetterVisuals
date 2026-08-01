#include "GlStateGuard.hpp"

#include <cassert>
#include <cstddef>

namespace {

    aa::render::VertexAttributeState captureVertexAttribute(GLuint index) {
        aa::render::VertexAttributeState state;
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &state.enabled);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &state.buffer);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_SIZE, &state.size);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &state.stride);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_TYPE, &state.type);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &state.normalized);
        glGetVertexAttribPointerv(index, GL_VERTEX_ATTRIB_ARRAY_POINTER, &state.pointer);
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

    void restoreVertexAttribute(GLuint index, aa::render::VertexAttributeState const& state) {
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

} // namespace

namespace aa::render {

    GlState captureGlState() {
        GlState state;
        glGetIntegerv(GL_CURRENT_PROGRAM, &state.program);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &state.activeTexture);

        for (std::size_t index = 0; index < state.textures2D.size(); ++index) {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(index));
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.textures2D[index]);
        }
        glActiveTexture(state.activeTexture);

        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state.arrayBuffer);
        glGetIntegerv(GL_VIEWPORT, state.viewport.data());
        glGetFloatv(GL_COLOR_CLEAR_VALUE, state.clearColor.data());
        glGetBooleanv(GL_COLOR_WRITEMASK, state.colorMask.data());
        glGetBooleanv(GL_DEPTH_WRITEMASK, &state.depthMask);
        state.blend = glIsEnabled(GL_BLEND);
        state.depthTest = glIsEnabled(GL_DEPTH_TEST);
        state.stencilTest = glIsEnabled(GL_STENCIL_TEST);
        state.scissorTest = glIsEnabled(GL_SCISSOR_TEST);
        state.cullFace = glIsEnabled(GL_CULL_FACE);

        state.separateFramebufferBindings = GLEW_VERSION_3_0 || GLEW_ARB_framebuffer_object;
        state.framebufferSupported = state.separateFramebufferBindings || GLEW_EXT_framebuffer_object;
        if (state.separateFramebufferBindings) {
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &state.readFramebuffer);
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &state.drawFramebuffer);
        }
        else if (state.framebufferSupported) {
            glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &state.drawFramebuffer);
            state.readFramebuffer = state.drawFramebuffer;
        }

        state.framebufferSrgbSupported =
            GLEW_VERSION_3_0 || GLEW_ARB_framebuffer_sRGB || GLEW_EXT_framebuffer_sRGB;
        if (state.framebufferSrgbSupported) {
            state.framebufferSrgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
        }

        state.attributes[0] = captureVertexAttribute(kPositionAttribute);
        state.attributes[1] = captureVertexAttribute(kTexCoordAttribute);
        return state;
    }

    void restoreGlState(GlState const& state) {
        restoreVertexAttribute(kPositionAttribute, state.attributes[0]);
        restoreVertexAttribute(kTexCoordAttribute, state.attributes[1]);
        glBindBuffer(GL_ARRAY_BUFFER, state.arrayBuffer);

        for (std::size_t index = 0; index < state.textures2D.size(); ++index) {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(index));
            glBindTexture(GL_TEXTURE_2D, state.textures2D[index]);
        }
        glActiveTexture(state.activeTexture);
        glUseProgram(state.program);

        if (state.separateFramebufferBindings) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, state.readFramebuffer);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, state.drawFramebuffer);
        }
        else if (state.framebufferSupported) {
            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, state.drawFramebuffer);
        }

        glViewport(state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
        glClearColor(
            state.clearColor[0], state.clearColor[1], state.clearColor[2], state.clearColor[3]
        );
        glColorMask(state.colorMask[0], state.colorMask[1], state.colorMask[2], state.colorMask[3]);
        glDepthMask(state.depthMask);
        restoreCapability(GL_BLEND, state.blend);
        restoreCapability(GL_DEPTH_TEST, state.depthTest);
        restoreCapability(GL_STENCIL_TEST, state.stencilTest);
        restoreCapability(GL_SCISSOR_TEST, state.scissorTest);
        restoreCapability(GL_CULL_FACE, state.cullFace);
        if (state.framebufferSrgbSupported) {
            restoreCapability(GL_FRAMEBUFFER_SRGB, state.framebufferSrgb);
        }
    }

    GlStateGuard::GlStateGuard() : m_state(captureGlState()) {}

    GlStateGuard::~GlStateGuard() {
        restoreGlState(m_state);
#ifndef NDEBUG
        auto const restored = captureGlState();
        assert((restored == m_state) && "Anti-aliasing renderer failed to restore OpenGL state");
#endif
    }

    std::array<GLint, 4> const& GlStateGuard::viewport() const {
        return m_state.viewport;
    }

    void GlStateGuard::bindOriginalReadFramebuffer() const {
        if (m_state.separateFramebufferBindings) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, m_state.readFramebuffer);
        }
        else if (m_state.framebufferSupported) {
            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, m_state.readFramebuffer);
        }
    }

    void GlStateGuard::bindOriginalDrawFramebuffer() const {
        if (m_state.separateFramebufferBindings) {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_state.drawFramebuffer);
        }
        else if (m_state.framebufferSupported) {
            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, m_state.drawFramebuffer);
        }
    }

} // namespace aa::render
