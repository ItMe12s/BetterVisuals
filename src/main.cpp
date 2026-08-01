#include "render/PostProcessRenderer.hpp"
#include "shaders/FxaaShader.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/CCEGLView.hpp>
#include <atomic>
#include <string_view>

using namespace geode::prelude;

namespace {

    enum class AntiAliasingMode {
        Off,
        Fxaa,
    };

    std::atomic<AntiAliasingMode> g_antiAliasingMode = AntiAliasingMode::Fxaa;

    void updateAntiAliasingMode(std::string_view value) {
        if (value == "FXAA") {
            g_antiAliasingMode.store(AntiAliasingMode::Fxaa, std::memory_order_relaxed);
            return;
        }

        if (value != "Off") {
            log::warn("Unknown AA mode '{}'; disabling AA", value);
        }
        g_antiAliasingMode.store(AntiAliasingMode::Off, std::memory_order_relaxed);
    }

} // namespace

$on_mod(Loaded) {
    updateAntiAliasingMode(Mod::get()->getSettingValue<std::string_view>("aa-mode"));
    listenForSettingChanges<std::string_view>("aa-mode", [](std::string_view value) {
        updateAntiAliasingMode(value);
    });
}

class $modify(AntiAliasingCCEGLView, CCEGLView) {
    static void onModify(auto& self) {
        if (!self.setHookPriorityPre("cocos2d::CCEGLView::swapBuffers", Priority::Last)) {
            log::warn("Unable to set the anti-aliasing swap hook priority");
        }
    }

    void swapBuffers() {
        static aa::render::PostProcessRenderer renderer;
        if (g_antiAliasingMode.load(std::memory_order_relaxed) == AntiAliasingMode::Fxaa) {
            renderer.apply(aa::shaders::kFxaaShader);
        }
        else {
            renderer.reset();
        }

        CCEGLView::swapBuffers();
    }
};
