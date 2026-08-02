#include "ShaderProgram.hpp"

#include "FullscreenQuad.hpp"

#include <Geode/Geode.hpp>
#include <cstddef>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace {

#ifdef GEODE_IS_MOBILE
    constexpr std::string_view kShaderPrelude =
        "#version 100\n"
        "precision highp float;\n"
        "precision highp int;\n";
#else
    constexpr std::string_view kShaderPrelude = "#version 120\n";
#endif

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
        GLenum type, std::span<std::string_view const> sources, std::string_view name, char const* stage
    ) {
        auto shader = glCreateShader(type);
        if (shader == 0) {
            log::error("Unable to create the {} {} shader", name, stage);
            return 0;
        }

        std::vector<GLchar const*> sourceData;
        std::vector<GLint> sourceLengths;
        sourceData.reserve(sources.size() + 1);
        sourceLengths.reserve(sources.size() + 1);
        sourceData.push_back(kShaderPrelude.data());
        sourceLengths.push_back(static_cast<GLint>(kShaderPrelude.size()));
        for (auto const& source : sources) {
            sourceData.push_back(source.data());
            sourceLengths.push_back(static_cast<GLint>(source.size()));
        }
        glShaderSource(
            shader, static_cast<GLsizei>(sourceData.size()), sourceData.data(), sourceLengths.data()
        );
        glCompileShader(shader);

        GLint compiled = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (compiled != GL_TRUE) {
            log::error("{} {} shader compilation failed: {}", name, stage, shaderLog(shader));
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

} // namespace

namespace bv::render {

    GLuint compileShaderProgram(
        std::string_view name, std::span<std::string_view const> vertexSources,
        std::span<std::string_view const> fragmentSources
    ) {
        auto vertexShader = compileShader(GL_VERTEX_SHADER, vertexSources, name, "vertex");
        if (vertexShader == 0) {
            return 0;
        }

        auto fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSources, name, "fragment");
        if (fragmentShader == 0) {
            glDeleteShader(vertexShader);
            return 0;
        }

        auto program = glCreateProgram();
        if (program != 0) {
            glAttachShader(program, vertexShader);
            glAttachShader(program, fragmentShader);
            glBindAttribLocation(program, FullscreenQuad::kPositionAttribute, "a_position");
            glBindAttribLocation(program, FullscreenQuad::kTexCoordAttribute, "a_texCoord");
            glLinkProgram(program);
        }
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        GLint linked = GL_FALSE;
        if (program != 0) {
            glGetProgramiv(program, GL_LINK_STATUS, &linked);
        }
        if (linked != GL_TRUE) {
            log::error(
                "{} shader link failed: {}",
                name,
                program == 0 ? "unable to create program" : programLog(program)
            );
            if (program != 0) {
                glDeleteProgram(program);
            }
            return 0;
        }

        return program;
    }

} // namespace bv::render
