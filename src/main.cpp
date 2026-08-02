#include "render/BloomRenderer.hpp"
#include "render/FramePipeline.hpp"
#include "render/GlStateGuard.hpp"
#include "render/PostProcessRenderer.hpp"
#include "render/SmaaRenderer.hpp"
#include "shaders/PostProcessShaders.hpp"
#include "shaders/aa/SmaaShader.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/CCEGLView.hpp>
#include <Geode/platform/cplatform.h>
#ifdef GEODE_IS_MOBILE
    #include <Geode/modify/AppDelegate.hpp>
#endif
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
    bv::render::FramePipeline g_framePipeline;
    bv::render::PostProcessRenderer g_postProcessRenderer;
    bv::render::PostProcessRenderer g_casRenderer;
    bv::render::BloomRenderer g_bloomRenderer;
    bv::render::PostProcessRenderer g_grayscaleRenderer;
    bv::render::PostProcessRenderer g_pixelateRenderer;
    bv::render::PostProcessRenderer g_ditheringRenderer;
    bv::render::PostProcessRenderer g_vhsRenderer;
    bv::render::PostProcessRenderer g_crtRenderer;
    bv::render::SmaaRenderer g_smaaRenderer;
    bool g_pipelineFailed = false;

    void resetRenderResources() {
        g_framePipeline.reset();
        g_postProcessRenderer.reset();
        g_casRenderer.reset();
        g_bloomRenderer.reset();
        g_grayscaleRenderer.reset();
        g_pixelateRenderer.reset();
        g_ditheringRenderer.reset();
        g_vhsRenderer.reset();
        g_crtRenderer.reset();
        g_smaaRenderer.reset();
        g_pipelineFailed = false;
    }

    void disableRenderPipeline() {
        resetRenderResources();
        g_pipelineFailed = true;
        log::error("BetterVisuals rendering disabled for the current OpenGL context");
    }

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
        bool configurationChanged = false;
        if (selectedMode != renderedMode) {
            g_postProcessRenderer.reset();
            g_smaaRenderer.reset();
            renderedMode = selectedMode;
            configurationChanged = true;
        }
        auto resetChanged = [](bool selected, bool& rendered, auto& renderer) {
            if (selected == rendered) {
                return false;
            }
            renderer.reset();
            rendered = selected;
            return true;
        };
        configurationChanged |= resetChanged(casEnabled, renderedCasEnabled, g_casRenderer);
        configurationChanged |= resetChanged(bloomEnabled, renderedBloomEnabled, g_bloomRenderer);
        configurationChanged |=
            resetChanged(grayscaleEnabled, renderedGrayscaleEnabled, g_grayscaleRenderer);
        configurationChanged |=
            resetChanged(pixelateEnabled, renderedPixelateEnabled, g_pixelateRenderer);
        configurationChanged |=
            resetChanged(ditheringEnabled, renderedDitheringEnabled, g_ditheringRenderer);
        configurationChanged |= resetChanged(vhsEnabled, renderedVhsEnabled, g_vhsRenderer);
        configurationChanged |= resetChanged(crtEnabled, renderedCrtEnabled, g_crtRenderer);
        if (configurationChanged) {
            g_pipelineFailed = false;
        }

        if (g_pipelineFailed) {
            CCEGLView::swapBuffers();
            return;
        }

        auto const hasEffects = selectedMode != AntiAliasingMode::Off || casEnabled || bloomEnabled ||
            grayscaleEnabled || pixelateEnabled || ditheringEnabled || vhsEnabled || crtEnabled;
        if (!hasEffects) {
            if (configurationChanged) {
                g_framePipeline.reset();
            }
            CCEGLView::swapBuffers();
            return;
        }

        auto const renderSucceeded = [&]() {
            bv::render::GlStateGuard state;
            auto const& viewport = state.viewport();
            auto const width = static_cast<GLsizei>(viewport[2]);
            auto const height = static_cast<GLsizei>(viewport[3]);
            if (width <= 0 || height <= 0) {
                return true;
            }

            bool prepared = g_framePipeline.prepare(width, height);
            auto const pipelineFramebuffer = g_framePipeline.framebuffer();
            switch (selectedMode) {
                case AntiAliasingMode::Fxaa:
                    prepared = prepared &&
                        g_postProcessRenderer.prepare(bv::shaders::kFxaaShader, width, height);
                    break;

                case AntiAliasingMode::SmaaHigh:
                    prepared = prepared &&
                        g_smaaRenderer.prepare(
                            bv::shaders::smaa::kSmaaHighShaderSet, width, height, pipelineFramebuffer
                        );
                    break;

                case AntiAliasingMode::SmaaUltra:
                    prepared = prepared &&
                        g_smaaRenderer.prepare(
                            bv::shaders::smaa::kSmaaUltraShaderSet, width, height, pipelineFramebuffer
                        );
                    break;

                case AntiAliasingMode::Off: break;
            }
            if (prepared && casEnabled) {
                prepared = g_casRenderer.prepare(bv::shaders::kCasShader, width, height);
            }
            if (prepared && bloomEnabled) {
                prepared = g_bloomRenderer.prepare(width, height, pipelineFramebuffer);
            }
            if (prepared && grayscaleEnabled) {
                prepared = g_grayscaleRenderer.prepare(bv::shaders::kGrayscaleShader, width, height);
            }
            if (prepared && pixelateEnabled) {
                prepared = g_pixelateRenderer.prepare(bv::shaders::kPixelateShader, width, height);
            }
            if (prepared && ditheringEnabled) {
                prepared = g_ditheringRenderer.prepare(bv::shaders::kDitheringShader, width, height);
            }
            if (prepared && vhsEnabled) {
                prepared = g_vhsRenderer.prepare(bv::shaders::kVhsShader, width, height);
            }
            if (prepared && crtEnabled) {
                prepared = g_crtRenderer.prepare(bv::shaders::kCrtShader, width, height);
            }
            if (!prepared || !g_framePipeline.capture(state.framebuffer(), viewport[0], viewport[1])) {
                return false;
            }

            glDisable(GL_BLEND);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_STENCIL_TEST);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_CULL_FACE);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            g_framePipeline.bindQuad();

            auto runPostProcess = [&](bv::render::PostProcessRenderer& renderer,
                                      GLfloat scalar = 0.f) {
                g_framePipeline.bindOutput();
                if (!renderer.apply(g_framePipeline.inputTexture(), scalar)) {
                    return false;
                }
                g_framePipeline.advance();
                return true;
            };
            auto runSmaa = [&]() {
                if (!g_smaaRenderer.apply(
                        g_framePipeline.inputTexture(),
                        g_framePipeline.outputTexture(),
                        g_framePipeline.framebuffer()
                    )) {
                    return false;
                }
                g_framePipeline.advance();
                return true;
            };

            bool applied = true;
            switch (selectedMode) {
                case AntiAliasingMode::Fxaa: applied = runPostProcess(g_postProcessRenderer); break;
                case AntiAliasingMode::SmaaHigh: applied = runSmaa(); break;
                case AntiAliasingMode::SmaaUltra: applied = runSmaa(); break;
                case AntiAliasingMode::Off: break;
            }
            if (applied && casEnabled) {
                applied =
                    runPostProcess(g_casRenderer, g_casSharpness.load(std::memory_order_relaxed));
            }
            if (applied && bloomEnabled) {
                applied = g_bloomRenderer.apply(
                    g_framePipeline.inputTexture(),
                    g_framePipeline.outputTexture(),
                    g_framePipeline.framebuffer()
                );
                if (applied) {
                    g_framePipeline.advance();
                }
            }
            if (applied && grayscaleEnabled) {
                applied = runPostProcess(g_grayscaleRenderer);
            }
            if (applied && pixelateEnabled) {
                applied = runPostProcess(g_pixelateRenderer);
            }
            if (applied && ditheringEnabled) {
                applied = runPostProcess(g_ditheringRenderer);
            }
            if (applied && vhsEnabled) {
                static auto const clockStart = std::chrono::steady_clock::now();
                auto const elapsed =
                    std::chrono::duration<GLfloat>(std::chrono::steady_clock::now() - clockStart).count();
                applied = runPostProcess(g_vhsRenderer, elapsed);
            }
            if (applied && crtEnabled) {
                applied = runPostProcess(g_crtRenderer);
            }
            return applied && g_framePipeline.present(state.framebuffer(), viewport);
        }();
        if (!renderSucceeded) {
            disableRenderPipeline();
        }

        CCEGLView::swapBuffers();
    }

#ifdef GEODE_IS_WINDOWS
    void toggleFullScreen(bool value, bool borderless, bool fix) {
        resetRenderResources();
        CCEGLView::toggleFullScreen(value, borderless, fix);
    }
#endif
};

#ifdef GEODE_IS_MOBILE
class $modify(BetterVisualsAppDelegate, AppDelegate) {
    void applicationDidEnterBackground() {
        resetRenderResources();
        AppDelegate::applicationDidEnterBackground();
    }
};
#endif
