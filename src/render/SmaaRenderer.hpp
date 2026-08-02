#pragma once

#include "../shaders/aa/SmaaShader.hpp"

#include <Geode/cocos/platform/CCGL.h>
#include <array>

namespace bv::render {

    class SmaaRenderer final {
    public:
        SmaaRenderer() = default;
        SmaaRenderer(SmaaRenderer const&) = delete;
        SmaaRenderer& operator=(SmaaRenderer const&) = delete;

        bool prepare(
            shaders::smaa::ShaderSet const& shaders, GLsizei width, GLsizei height, GLuint framebuffer
        );
        bool apply(GLuint inputTexture, GLuint outputTexture, GLuint framebuffer);
        void reset();

    private:
        bool isReady(shaders::smaa::ShaderSet const& shaders, GLsizei width, GLsizei height) const;

        struct Program {
            GLuint handle = 0;
            GLint metrics = -1;
        };

        bool initialize(shaders::smaa::ShaderSet const& shaders);
        bool resizeTextures(GLsizei width, GLsizei height, GLuint framebuffer);
        bool validateFramebuffer(GLuint framebuffer, GLuint texture);
        void attachTexture(GLuint framebuffer, GLuint texture);
        void destroyResources();

        shaders::smaa::ShaderSet const* m_shaders = nullptr;
        std::array<Program, 3> m_programs = {};
        GLuint m_edgeTexture = 0;
        GLuint m_weightTexture = 0;
        GLuint m_areaTexture = 0;
        GLuint m_searchTexture = 0;
        GLsizei m_width = 0;
        GLsizei m_height = 0;
        bool m_failed = false;
    };

} // namespace bv::render
