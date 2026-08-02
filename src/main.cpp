#include "render/PostProcessRenderer.hpp"
#include "render/SmaaRenderer.hpp"
#include "shaders/FxaaShader.hpp"
#include "shaders/SmaaShader.hpp"

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
        SmaaHigh,
        SmaaUltra,
    };

    std::atomic<AntiAliasingMode> g_antiAliasingMode = AntiAliasingMode::SmaaHigh;
    aa::render::PostProcessRenderer g_postProcessRenderer;
    aa::render::SmaaRenderer g_smaaRenderer;

    void updateAntiAliasingMode(std::string_view value) {
        if (value == "SMAA High") {
            g_antiAliasingMode.store(AntiAliasingMode::SmaaHigh, std::memory_order_relaxed);
            return;
        }
        if (value == "SMAA Ultra") {
            g_antiAliasingMode.store(AntiAliasingMode::SmaaUltra, std::memory_order_relaxed);
            return;
        }
        if (value == "FXAA") {
            g_antiAliasingMode.store(AntiAliasingMode::Fxaa, std::memory_order_relaxed);
            return;
        }

        if (value != "Off") {
            log::warn("Unknown AA mode '{}', disabling AA", value);
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
        static auto renderedMode = AntiAliasingMode::Off;

        auto const selectedMode = g_antiAliasingMode.load(std::memory_order_relaxed);
        if (selectedMode != renderedMode) {
            g_postProcessRenderer.reset();
            g_smaaRenderer.reset();
            renderedMode = selectedMode;
        }

        switch (selectedMode) {
            case AntiAliasingMode::Fxaa:
                g_postProcessRenderer.apply(aa::shaders::kFxaaShader);
                break;

            case AntiAliasingMode::SmaaHigh:
                g_smaaRenderer.apply(aa::shaders::kSmaaHighShaderSet);
                break;

            case AntiAliasingMode::SmaaUltra:
                g_smaaRenderer.apply(aa::shaders::kSmaaUltraShaderSet);
                break;

            case AntiAliasingMode::Off: break;
        }

        CCEGLView::swapBuffers();
    }

    void toggleFullScreen(bool value, bool borderless, bool fix) {
        g_postProcessRenderer.reset();
        g_smaaRenderer.reset();
        CCEGLView::toggleFullScreen(value, borderless, fix);
    }
};
