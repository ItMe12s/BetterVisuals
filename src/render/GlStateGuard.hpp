#pragma once

#include <Geode/cocos/platform/CCGL.h>
#include <array>

namespace aa::render {

    constexpr GLuint kPositionAttribute = 0;
    constexpr GLuint kTexCoordAttribute = 1;

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
        std::array<GLint, 3> textures2D = {};
        GLint arrayBuffer = 0;
        std::array<GLint, 4> viewport = {};
        std::array<GLfloat, 4> clearColor = {};
        std::array<GLboolean, 4> colorMask = {};
        GLboolean depthMask = GL_TRUE;
        GLboolean blend = GL_FALSE;
        GLboolean depthTest = GL_FALSE;
        GLboolean stencilTest = GL_FALSE;
        GLboolean scissorTest = GL_FALSE;
        GLboolean cullFace = GL_FALSE;
        GLboolean framebufferSrgb = GL_FALSE;
        bool framebufferSupported = false;
        bool separateFramebufferBindings = false;
        bool framebufferSrgbSupported = false;
        GLint readFramebuffer = 0;
        GLint drawFramebuffer = 0;
        std::array<VertexAttributeState, 2> attributes = {};

        bool operator==(GlState const&) const = default;
    };

    GlState captureGlState();
    void restoreGlState(GlState const& state);

    class GlStateGuard final {
    public:
        GlStateGuard();
        ~GlStateGuard();

        GlStateGuard(GlStateGuard const&) = delete;
        GlStateGuard& operator=(GlStateGuard const&) = delete;

        std::array<GLint, 4> const& viewport() const;
        void bindOriginalReadFramebuffer() const;
        void bindOriginalDrawFramebuffer() const;

    private:
        GlState m_state;
    };

} // namespace aa::render
