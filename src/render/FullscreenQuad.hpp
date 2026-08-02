#pragma once

#include <Geode/cocos/platform/CCGL.h>
#include <string_view>

namespace bv::render {

    class FullscreenQuad final {
    public:
        static constexpr GLuint kPositionAttribute = 0;
        static constexpr GLuint kTexCoordAttribute = 1;

        FullscreenQuad() = default;
        ~FullscreenQuad() = default;

        FullscreenQuad(FullscreenQuad const&) = delete;
        FullscreenQuad& operator=(FullscreenQuad const&) = delete;

        bool initialize(std::string_view label);
        void bind() const;
        void draw() const;
        void reset();

    private:
        GLuint m_vbo = 0;
    };

} // namespace bv::render
