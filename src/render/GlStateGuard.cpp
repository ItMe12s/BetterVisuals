#include "GlStateGuard.hpp"

#include "FullscreenQuad.hpp"

#include <cassert>
#include <cstddef>

namespace {

    bv::render::VertexAttributeState captureVertexAttribute(GLuint index) {
        bv::render::VertexAttributeState state;
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

    void restoreVertexAttribute(GLuint index, bv::render::VertexAttributeState const& state) {
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

namespace bv::render {

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
        glGetIntegerv(GL_SCISSOR_BOX, state.scissorBox.data());
        glGetFloatv(GL_COLOR_CLEAR_VALUE, state.clearColor.data());
        glGetBooleanv(GL_COLOR_WRITEMASK, state.colorMask.data());
        state.blend = glIsEnabled(GL_BLEND);
        state.depthTest = glIsEnabled(GL_DEPTH_TEST);
        state.stencilTest = glIsEnabled(GL_STENCIL_TEST);
        state.scissorTest = glIsEnabled(GL_SCISSOR_TEST);
        state.cullFace = glIsEnabled(GL_CULL_FACE);

        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &state.framebuffer);
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &state.renderbuffer);

        state.attributes[0] = captureVertexAttribute(FullscreenQuad::kPositionAttribute);
        state.attributes[1] = captureVertexAttribute(FullscreenQuad::kTexCoordAttribute);
        return state;
    }

    void restoreGlState(GlState const& state) {
        restoreVertexAttribute(FullscreenQuad::kPositionAttribute, state.attributes[0]);
        restoreVertexAttribute(FullscreenQuad::kTexCoordAttribute, state.attributes[1]);
        glBindBuffer(GL_ARRAY_BUFFER, state.arrayBuffer);

        for (std::size_t index = 0; index < state.textures2D.size(); ++index) {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(index));
            glBindTexture(GL_TEXTURE_2D, state.textures2D[index]);
        }
        glActiveTexture(state.activeTexture);
        glUseProgram(state.program);

        glBindFramebuffer(GL_FRAMEBUFFER, state.framebuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, state.renderbuffer);
        glViewport(state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
        glScissor(state.scissorBox[0], state.scissorBox[1], state.scissorBox[2], state.scissorBox[3]);
        glClearColor(
            state.clearColor[0], state.clearColor[1], state.clearColor[2], state.clearColor[3]
        );
        glColorMask(state.colorMask[0], state.colorMask[1], state.colorMask[2], state.colorMask[3]);
        restoreCapability(GL_BLEND, state.blend);
        restoreCapability(GL_DEPTH_TEST, state.depthTest);
        restoreCapability(GL_STENCIL_TEST, state.stencilTest);
        restoreCapability(GL_SCISSOR_TEST, state.scissorTest);
        restoreCapability(GL_CULL_FACE, state.cullFace);
    }

    GlStateGuard::GlStateGuard() : m_state(captureGlState()) {}

    GlStateGuard::~GlStateGuard() {
        restoreGlState(m_state);
#ifndef NDEBUG
        auto const restored = captureGlState();
        assert((restored == m_state) && "Post-processing failed to restore OpenGL state");
#endif
    }

} // namespace bv::render
