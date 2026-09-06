#pragma once

#include <Geode/cocos/platform/CCGL.h>
#include <array>

namespace bv::render {

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
        std::array<GLint, 4> scissorBox = {};
        std::array<GLfloat, 4> clearColor = {};
        std::array<GLboolean, 4> colorMask = {};
        GLboolean blend = GL_FALSE;
        GLboolean depthTest = GL_FALSE;
        GLboolean stencilTest = GL_FALSE;
        GLboolean scissorTest = GL_FALSE;
        GLboolean cullFace = GL_FALSE;
        GLint framebuffer = 0;
        GLint renderbuffer = 0;
        std::array<VertexAttributeState, 3> attributes = {};

        bool operator==(GlState const&) const = default;
    };

    class GlStateGuard final {
    public:
        GlStateGuard();
        ~GlStateGuard();

        GlStateGuard(GlStateGuard const&) = delete;
        GlStateGuard& operator=(GlStateGuard const&) = delete;

    private:
        GlState m_state;
    };

} // namespace bv::render
