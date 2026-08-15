#pragma once

#include <Geode/cocos/platform/CCGL.h>

namespace bv::render::gl {

    inline void clearErrors() {
        while (glGetError() != GL_NO_ERROR) {}
    }

    inline void configureTexture(GLuint texture) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    inline bool uploadTexture(
        GLint internalFormat, GLsizei width, GLsizei height, GLenum format, void const* data
    ) {
        clearErrors();
        glTexImage2D(
            GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, data
        );
        return glGetError() == GL_NO_ERROR;
    }

    inline bool allocateTexture(GLuint texture, GLsizei width, GLsizei height) {
        glBindTexture(GL_TEXTURE_2D, texture);
        return uploadTexture(GL_RGBA, width, height, GL_RGBA, nullptr);
    }

} // namespace bv::render::gl