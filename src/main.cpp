#include "render/BloomRenderer.hpp"
#include "render/PostProcessRenderer.hpp"
#include "render/SmaaRenderer.hpp"
#include "shaders/aa/FxaaShader.hpp"
#include "shaders/aa/SmaaShader.hpp"
#include "shaders/fun/CrtShader.hpp"
#include "shaders/fun/DitheringShader.hpp"
#include "shaders/fun/GrayscaleShader.hpp"
#include "shaders/fun/PixelateShader.hpp"
#include "shaders/fun/VhsShader.hpp"
#include "shaders/sharpen/CasShader.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/CCEGLView.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
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
    std::atomic<bool> g_bloomEnabled = false;
    std::atomic<bool> g_grayscaleEnabled = false;
    std::atomic<bool> g_pixelateEnabled = false;
    std::atomic<bool> g_ditheringEnabled = false;
    std::atomic<bool> g_vhsEnabled = false;
    std::atomic<bool> g_crtEnabled = false;
    bv::render::PostProcessRenderer g_postProcessRenderer;
    bv::render::PostProcessRenderer g_casRenderer;
    bv::render::BloomRenderer g_bloomRenderer;
    bv::render::PostProcessRenderer g_grayscaleRenderer;
    bv::render::PostProcessRenderer g_pixelateRenderer;
    bv::render::PostProcessRenderer g_ditheringRenderer;
    bv::render::PostProcessRenderer g_vhsRenderer;
    bv::render::PostProcessRenderer g_crtRenderer;
    bv::render::SmaaRenderer g_smaaRenderer;

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

    g_bloomEnabled.store(Mod::get()->getSettingValue<bool>("bloom-enabled"), std::memory_order_relaxed);
    listenForSettingChanges<bool>("bloom-enabled", [](bool value) {
        g_bloomEnabled.store(value, std::memory_order_relaxed);
    });

    g_grayscaleEnabled.store(
        Mod::get()->getSettingValue<bool>("grayscale-enabled"), std::memory_order_relaxed
    );
    listenForSettingChanges<bool>("grayscale-enabled", [](bool value) {
        g_grayscaleEnabled.store(value, std::memory_order_relaxed);
    });

    g_pixelateEnabled.store(
        Mod::get()->getSettingValue<bool>("pixelate-enabled"), std::memory_order_relaxed
    );
    listenForSettingChanges<bool>("pixelate-enabled", [](bool value) {
        g_pixelateEnabled.store(value, std::memory_order_relaxed);
    });

    g_ditheringEnabled.store(
        Mod::get()->getSettingValue<bool>("dithering-enabled"), std::memory_order_relaxed
    );
    listenForSettingChanges<bool>("dithering-enabled", [](bool value) {
        g_ditheringEnabled.store(value, std::memory_order_relaxed);
    });

    g_vhsEnabled.store(Mod::get()->getSettingValue<bool>("vhs-enabled"), std::memory_order_relaxed);
    listenForSettingChanges<bool>("vhs-enabled", [](bool value) {
        g_vhsEnabled.store(value, std::memory_order_relaxed);
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
        static bool renderedBloomEnabled = false;
        static bool renderedGrayscaleEnabled = false;
        static bool renderedPixelateEnabled = false;
        static bool renderedDitheringEnabled = false;
        static bool renderedVhsEnabled = false;
        static bool renderedCrtEnabled = false;

        auto const selectedMode = g_antiAliasingMode.load(std::memory_order_relaxed);
        auto const casEnabled = g_casEnabled.load(std::memory_order_relaxed);
        auto const bloomEnabled = g_bloomEnabled.load(std::memory_order_relaxed);
        auto const grayscaleEnabled = g_grayscaleEnabled.load(std::memory_order_relaxed);
        auto const pixelateEnabled = g_pixelateEnabled.load(std::memory_order_relaxed);
        auto const ditheringEnabled = g_ditheringEnabled.load(std::memory_order_relaxed);
        auto const vhsEnabled = g_vhsEnabled.load(std::memory_order_relaxed);
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
        if (bloomEnabled != renderedBloomEnabled) {
            g_bloomRenderer.reset();
            renderedBloomEnabled = bloomEnabled;
        }
        if (grayscaleEnabled != renderedGrayscaleEnabled) {
            g_grayscaleRenderer.reset();
            renderedGrayscaleEnabled = grayscaleEnabled;
        }
        if (pixelateEnabled != renderedPixelateEnabled) {
            g_pixelateRenderer.reset();
            renderedPixelateEnabled = pixelateEnabled;
        }
        if (ditheringEnabled != renderedDitheringEnabled) {
            g_ditheringRenderer.reset();
            renderedDitheringEnabled = ditheringEnabled;
        }
        if (vhsEnabled != renderedVhsEnabled) {
            g_vhsRenderer.reset();
            renderedVhsEnabled = vhsEnabled;
        }
        if (crtEnabled != renderedCrtEnabled) {
            g_crtRenderer.reset();
            renderedCrtEnabled = crtEnabled;
        }

        switch (selectedMode) {
            case AntiAliasingMode::Fxaa:
                g_postProcessRenderer.apply(bv::shaders::kFxaaShader);
                break;

            case AntiAliasingMode::SmaaHigh:
                g_smaaRenderer.apply(bv::shaders::kSmaaHighShaderSet);
                break;

            case AntiAliasingMode::SmaaUltra:
                g_smaaRenderer.apply(bv::shaders::kSmaaUltraShaderSet);
                break;

            case AntiAliasingMode::Off: break;
        }

        if (casEnabled) {
            g_casRenderer.apply(
                bv::shaders::kCasShader, g_casSharpness.load(std::memory_order_relaxed)
            );
        }
        if (bloomEnabled) {
            g_bloomRenderer.apply();
        }
        if (grayscaleEnabled) {
            g_grayscaleRenderer.apply(bv::shaders::kGrayscaleShader);
        }
        if (pixelateEnabled) {
            g_pixelateRenderer.apply(bv::shaders::kPixelateShader);
        }
        if (ditheringEnabled) {
            g_ditheringRenderer.apply(bv::shaders::kDitheringShader);
        }
        if (vhsEnabled) {
            static auto const clockStart = std::chrono::steady_clock::now();
            auto const elapsed =
                std::chrono::duration<GLfloat>(std::chrono::steady_clock::now() - clockStart).count();
            g_vhsRenderer.apply(bv::shaders::kVhsShader, elapsed);
        }
        if (crtEnabled) {
            g_crtRenderer.apply(bv::shaders::kCrtShader);
        }

        CCEGLView::swapBuffers();
    }

    void toggleFullScreen(bool value, bool borderless, bool fix) {
        g_postProcessRenderer.reset();
        g_casRenderer.reset();
        g_bloomRenderer.reset();
        g_grayscaleRenderer.reset();
        g_pixelateRenderer.reset();
        g_ditheringRenderer.reset();
        g_vhsRenderer.reset();
        g_crtRenderer.reset();
        g_smaaRenderer.reset();
        CCEGLView::toggleFullScreen(value, borderless, fix);
    }
};
