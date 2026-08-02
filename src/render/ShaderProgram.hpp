#pragma once

#include <Geode/cocos/platform/CCGL.h>
#include <span>
#include <string_view>

namespace bv::render {

    GLuint compileShaderProgram(
        std::string_view name, std::span<std::string_view const> vertexSources,
        std::span<std::string_view const> fragmentSources
    );

} // namespace bv::render
