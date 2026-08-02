#include "FullscreenQuad.hpp"

#include <Geode/Geode.hpp>
#include <array>
#include <cstddef>

using namespace geode::prelude;

namespace {

    struct Vertex {
        GLfloat position[2];
        GLfloat texCoord[2];
    };

    constexpr std::array<Vertex, 4> kVertices = {{
        {{-1.f, -1.f}, {0.f, 0.f}},
        {{1.f, -1.f}, {1.f, 0.f}},
        {{-1.f, 1.f}, {0.f, 1.f}},
        {{1.f, 1.f}, {1.f, 1.f}},
    }};

} // namespace

namespace bv::render {

    bool FullscreenQuad::initialize(std::string_view label) {
        if (m_vbo != 0) {
            return true;
        }

        glGenBuffers(1, &m_vbo);
        if (m_vbo == 0) {
            log::error("Unable to allocate {} fullscreen quad", label);
            return false;
        }

        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(
            GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(kVertices)), kVertices.data(), GL_STATIC_DRAW
        );

        GLint size = 0;
        glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &size);
        if (size != static_cast<GLint>(sizeof(kVertices))) {
            log::error("Unable to upload {} fullscreen quad", label);
            reset();
            return false;
        }

        return true;
    }

    void FullscreenQuad::bind() const {
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glEnableVertexAttribArray(FullscreenQuad::kPositionAttribute);
        glEnableVertexAttribArray(FullscreenQuad::kTexCoordAttribute);
        glVertexAttribPointer(
            FullscreenQuad::kPositionAttribute,
            2,
            GL_FLOAT,
            GL_FALSE,
            sizeof(Vertex),
            reinterpret_cast<void*>(offsetof(Vertex, position))
        );
        glVertexAttribPointer(
            FullscreenQuad::kTexCoordAttribute,
            2,
            GL_FLOAT,
            GL_FALSE,
            sizeof(Vertex),
            reinterpret_cast<void*>(offsetof(Vertex, texCoord))
        );
    }

    void FullscreenQuad::draw() {
        glDrawArrays(GL_TRIANGLE_STRIP, 0, static_cast<GLsizei>(kVertices.size()));
    }

    void FullscreenQuad::reset() {
        if (m_vbo != 0) {
            glDeleteBuffers(1, &m_vbo);
            m_vbo = 0;
        }
    }

} // namespace bv::render
