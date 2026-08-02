#pragma once

#include <array>
#include <string_view>

namespace bv::shaders::smaa {

    struct ProgramSource {
        std::string_view name;
        std::string_view vertexMain;
        std::string_view fragmentMain;
    };

    struct ShaderSet {
        std::string_view commonSource;
        std::string_view vertexStageSource;
        std::string_view fragmentStageSource;
        std::string_view algorithmSource;
        std::array<ProgramSource, 3> programs;
    };

    extern ShaderSet const kSmaaHighShaderSet;
    extern ShaderSet const kSmaaUltraShaderSet;

} // namespace bv::shaders::smaa
