#pragma once

#include <string_view>

namespace bv::shaders {

    struct BloomShaderSet {
        std::string_view vertexSource;
        std::string_view prefilterSource;
        std::string_view blurSource;
        std::string_view compositeSource;
    };

    extern BloomShaderSet const kBloomShaderSet;

} // namespace bv::shaders
