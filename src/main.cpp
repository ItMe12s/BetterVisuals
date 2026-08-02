#include "render/PostProcessRenderer.hpp"
#include "render/SmaaRenderer.hpp"
#include "shaders/CasShader.hpp"
#include "shaders/CrtShader.hpp"
#include "shaders/FxaaShader.hpp"
#include "shaders/SmaaShader.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/CCEGLView.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
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
    std::atomic<bool> g_casEnabled = false;
    std::atomic<float> g_casSharpness = 0.f;
    std::atomic<bool> g_crtEnabled = false;
    aa::render::PostProcessRenderer g_postProcessRenderer;
    aa::render::PostProcessRenderer g_casRenderer;
    aa::render::PostProcessRenderer g_crtRenderer;
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

    void updateCasSharpness(double value) {
        auto const sharpness = std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0;
        g_casSharpness.store(static_cast<float>(sharpness), std::memory_order_relaxed);
    }

} // namespace

$on_mod(Loaded) {
    updateAntiAliasingMode(Mod::get()->getSettingValue<std::string_view>("aa-mode"));
    listenForSettingChanges<std::string_view>("aa-mode", [](std::string_view value) {
        updateAntiAliasingMode(value);
    });

    g_casEnabled.store(Mod::get()->getSettingValue<bool>("cas-enabled"), std::memory_order_relaxed);
    listenForSettingChanges<bool>("cas-enabled", [](bool value) {
        g_casEnabled.store(value, std::memory_order_relaxed);
    });

    updateCasSharpness(Mod::get()->getSettingValue<double>("cas-sharpness"));
    listenForSettingChanges<double>("cas-sharpness", [](double value) {
        updateCasSharpness(value);
    });

    g_crtEnabled.store(Mod::get()->getSettingValue<bool>("crt-enabled"), std::memory_order_relaxed);
    listenForSettingChanges<bool>("crt-enabled", [](bool value) {
        g_crtEnabled.store(value, std::memory_order_relaxed);
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
        static bool renderedCasEnabled = false;
        static bool renderedCrtEnabled = false;

        auto const selectedMode = g_antiAliasingMode.load(std::memory_order_relaxed);
        auto const casEnabled = g_casEnabled.load(std::memory_order_relaxed);
        auto const crtEnabled = g_crtEnabled.load(std::memory_order_relaxed);
        if (selectedMode != renderedMode) {
            g_postProcessRenderer.reset();
            g_smaaRenderer.reset();
            renderedMode = selectedMode;
        }
        if (casEnabled != renderedCasEnabled) {
            g_casRenderer.reset();
            renderedCasEnabled = casEnabled;
        }
        if (crtEnabled != renderedCrtEnabled) {
            g_crtRenderer.reset();
            renderedCrtEnabled = crtEnabled;
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

        if (casEnabled) {
            g_casRenderer.apply(
                aa::shaders::kCasShader, g_casSharpness.load(std::memory_order_relaxed)
            );
        }
        if (crtEnabled) {
            g_crtRenderer.apply(aa::shaders::kCrtShader);
        }

        CCEGLView::swapBuffers();
    }

    void toggleFullScreen(bool value, bool borderless, bool fix) {
        g_postProcessRenderer.reset();
        g_casRenderer.reset();
        g_crtRenderer.reset();
        g_smaaRenderer.reset();
        CCEGLView::toggleFullScreen(value, borderless, fix);
    }
};
