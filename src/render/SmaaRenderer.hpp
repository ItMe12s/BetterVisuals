#pragma once

#include "../shaders/SmaaShader.hpp"
#include "FullscreenQuad.hpp"

#include <Geode/cocos/platform/CCGL.h>
#include <array>

namespace aa::render {

    class SmaaRenderer final {
    public:
        SmaaRenderer() = default;
        SmaaRenderer(SmaaRenderer const&) = delete;
        SmaaRenderer& operator=(SmaaRenderer const&) = delete;

        void apply(shaders::SmaaShaderSet const& shaders);
        void reset();

    private:
        struct Program {
            GLuint handle = 0;
            GLint metrics = -1;
            std::array<GLint, 3> textures = {-1, -1, -1};
        };

        bool initialize(shaders::SmaaShaderSet const& shaders);
        bool resizeTextures(GLsizei width, GLsizei height);
        bool validateFramebuffer(GLuint texture);
        void bindIntermediateFramebuffer();
        void attachIntermediateTexture(GLuint texture);
        void destroyResources();

        shaders::SmaaShaderSet const* m_shaders = nullptr;
        std::array<Program, 3> m_programs = {};
        GLuint m_sourceTexture = 0;
        GLuint m_edgeTexture = 0;
        GLuint m_weightTexture = 0;
        GLuint m_areaTexture = 0;
        GLuint m_searchTexture = 0;
        GLuint m_framebuffer = 0;
        FullscreenQuad m_quad;
        GLsizei m_width = 0;
        GLsizei m_height = 0;
        bool m_coreFramebufferApi = false;
        bool m_failed = false;
    };

} // namespace aa::render
