#include "render/BloomRenderer.hpp"
#include "render/GlStateGuard.hpp"
#include "render/PostProcessPipeline.hpp"
#include "render/PostProcessRenderer.hpp"
#include "render/SmaaRenderer.hpp"
#include "shaders/PostProcessShaders.hpp"
#include "shaders/aa/SmaaShader.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/platform/cplatform.h>
#include <Geode/ui/Popup.hpp>
#ifdef GEODE_IS_WINDOWS
    #include <Geode/modify/CCEGLView.hpp>
#endif
#ifdef GEODE_IS_MOBILE
    #include <Geode/modify/AppDelegate.hpp>
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <optional>
#include <string_view>

using namespace geode::prelude;

namespace {

    enum class AntiAliasingMethod {
        Off,
        Fxaa,
        SmaaHigh,
        SmaaUltra,
    };

    enum class UpscaleMethod {
        Nearest,
        Bilinear,
        Fsr,
    };

    struct PostProcessConfig {
        AntiAliasingMethod aa = AntiAliasingMethod::Off;
        bool cas = false;
        bool bloom = false;
        float bloomThreshold = 0.7f;
        float bloomIntensity = 0.3f;
        float bloomRadius = 8.f;
        bool grayscale = false;
        bool pixelate = false;
        bool dithering = false;
        bool vhs = false;
        bool crt = false;
        UpscaleMethod upscaling = UpscaleMethod::Nearest;

        bool operator==(PostProcessConfig const&) const = default;

        int effectCount() const {
            return (aa != AntiAliasingMethod::Off) + cas + bloom + grayscale + pixelate +
                dithering + vhs + crt;
        }
    };

    struct PostProcessKey {
        PostProcessConfig config;
        GLsizei width = 0;
        GLsizei height = 0;
        bool needsPresentation = false;

        bool operator==(PostProcessKey const&) const = default;
    };

    struct PostProcessFailureKey {
        PostProcessKey postProcess;
        GLuint framebuffer = 0;
        std::array<GLint, 4> viewport = {};

        bool operator==(PostProcessFailureKey const&) const = default;
    };

    std::atomic<AntiAliasingMethod> g_antiAliasingMethod = AntiAliasingMethod::SmaaHigh;
    std::atomic<UpscaleMethod> g_upscaleMethod = UpscaleMethod::Nearest;
    std::atomic<float> g_renderScale = 1.f;
    std::atomic<bool> g_casEnabled = false;
    std::atomic<float> g_casSharpness = 0.f;
    std::atomic<float> g_fsrSharpness = 0.5f;
    std::atomic<bool> g_bloomEnabled = false;
    std::atomic<float> g_bloomThreshold = 0.7f;
    std::atomic<float> g_bloomIntensity = 0.3f;
    std::atomic<float> g_bloomRadius = 8.f;
    std::atomic<bool> g_grayscaleEnabled = false;
    std::atomic<bool> g_pixelateEnabled = false;
    std::atomic<bool> g_ditheringEnabled = false;
    std::atomic<bool> g_vhsEnabled = false;
    std::atomic<bool> g_crtEnabled = false;
    std::atomic<bool> g_modEnabled = true;
    std::atomic<bool> g_funEnabled = true;
    std::atomic<bool> g_temporarilyDisabled = false;
    std::atomic<bool> g_disablePopupShown = false;
    bv::render::PostProcessPipeline g_postProcessPipeline;
    bv::render::PostProcessRenderer g_fxaaRenderer;
    bv::render::PostProcessRenderer g_renderScaleRenderer;
    bv::render::PostProcessRenderer g_fsrRenderer;
    bv::render::PostProcessRenderer g_casRenderer;
    bv::render::BloomRenderer g_bloomRenderer;
    bv::render::PostProcessRenderer g_grayscaleRenderer;
    bv::render::PostProcessRenderer g_pixelateRenderer;
    bv::render::PostProcessRenderer g_ditheringRenderer;
    bv::render::PostProcessRenderer g_vhsRenderer;
    bv::render::PostProcessRenderer g_crtRenderer;
    bv::render::SmaaRenderer g_smaaRenderer;
    std::optional<PostProcessKey> g_preparedPostProcessKey;
    std::optional<PostProcessFailureKey> g_failedPostProcessKey;
    bool g_isGameLayerVisitActive = false;
    bool g_isSceneCaptureActive = false;
    GLsizei g_captureWidth = 0;
    GLsizei g_captureHeight = 0;

    void applyCaptureViewport() {
        if (g_isSceneCaptureActive) {
            glViewport(0, 0, g_captureWidth, g_captureHeight);
        }
    }

    void resetRenderResources() {
        g_postProcessPipeline.reset();
        g_fxaaRenderer.reset();
        g_renderScaleRenderer.reset();
        g_fsrRenderer.reset();
        g_casRenderer.reset();
        g_bloomRenderer.reset();
        g_grayscaleRenderer.reset();
        g_pixelateRenderer.reset();
        g_ditheringRenderer.reset();
        g_vhsRenderer.reset();
        g_crtRenderer.reset();
        g_smaaRenderer.reset();
        g_preparedPostProcessKey.reset();
        g_failedPostProcessKey.reset();
    }

    void updateAntiAliasingMethod(std::string_view value) {
        if (value == "SMAA High") {
            g_antiAliasingMethod.store(AntiAliasingMethod::SmaaHigh, std::memory_order_relaxed);
            return;
        }
        if (value == "SMAA Ultra") {
            g_antiAliasingMethod.store(AntiAliasingMethod::SmaaUltra, std::memory_order_relaxed);
            return;
        }
        if (value == "FXAA") {
            g_antiAliasingMethod.store(AntiAliasingMethod::Fxaa, std::memory_order_relaxed);
            return;
        }

        if (value != "Off") {
            log::warn("Unknown AA method '{}', disabling AA", value);
        }
        g_antiAliasingMethod.store(AntiAliasingMethod::Off, std::memory_order_relaxed);
    }

    void updateUpscaleMethod(std::string_view value) {
        if (value == "Bilinear") {
            g_upscaleMethod.store(UpscaleMethod::Bilinear, std::memory_order_relaxed);
            return;
        }
        if (value == "FSR 1") {
            g_upscaleMethod.store(UpscaleMethod::Fsr, std::memory_order_relaxed);
            return;
        }

        if (value != "Nearest") {
            log::warn("Unknown upscale method '{}', using nearest neighbour", value);
        }
        g_upscaleMethod.store(UpscaleMethod::Nearest, std::memory_order_relaxed);
    }

    void bindBoolSetting(char const* name, std::atomic<bool>& value) {
        value.store(Mod::get()->getSettingValue<bool>(name), std::memory_order_relaxed);
        listenForSettingChanges<bool>(name, [&value](bool updated) {
            value.store(updated, std::memory_order_relaxed);
        });
    }

    void bindDoubleSetting(char const* name, std::atomic<float>& value) {
        auto update = [&value](double input) {
            value.store(static_cast<float>(input), std::memory_order_relaxed);
        };
        update(Mod::get()->getSettingValue<double>(name));
        listenForSettingChanges<double>(name, [update](double input) {
            update(input);
        });
    }

    void bindStringSetting(char const* name, void (*update)(std::string_view)) {
        update(Mod::get()->getSettingValue<std::string>(name));
        listenForSettingChanges<std::string>(name, [update](std::string value) {
            update(value);
        });
    }

    GLsizei scaledDimension(GLsizei output, float scale) {
        return static_cast<GLsizei>(
            std::max<long>(1, std::lround(static_cast<double>(output) * scale))
        );
    }

    std::array<GLint, 4> scaledScissorBox(
        std::array<GLint, 4> const& box, std::array<GLint, 4> const& viewport, GLsizei width,
        GLsizei height
    ) {
        auto mapStart = [](GLint value, GLint origin, GLsizei internalSize, GLsizei outputSize) {
            return static_cast<GLint>(
                std::floor(static_cast<double>(value - origin) * internalSize / outputSize)
            );
        };
        auto mapEnd = [](GLint value, GLint origin, GLsizei internalSize, GLsizei outputSize) {
            return static_cast<GLint>(
                std::ceil(static_cast<double>(value - origin) * internalSize / outputSize)
            );
        };

        auto const left = mapStart(box[0], viewport[0], width, viewport[2]);
        auto const bottom = mapStart(box[1], viewport[1], height, viewport[3]);
        auto const right = mapEnd(box[0] + box[2], viewport[0], width, viewport[2]);
        auto const top = mapEnd(box[1] + box[3], viewport[1], height, viewport[3]);
        return {
            left,
            bottom,
            std::max<GLint>(0, right - left),
            std::max<GLint>(0, top - bottom),
        };
    }

    PostProcessConfig selectedPostProcessConfig() {
        bool const fun = g_funEnabled.load(std::memory_order_relaxed);
        return {
            g_antiAliasingMethod.load(std::memory_order_relaxed),
            g_casEnabled.load(std::memory_order_relaxed),
            fun && g_bloomEnabled.load(std::memory_order_relaxed),
            g_bloomThreshold.load(std::memory_order_relaxed),
            g_bloomIntensity.load(std::memory_order_relaxed),
            g_bloomRadius.load(std::memory_order_relaxed),
            fun && g_grayscaleEnabled.load(std::memory_order_relaxed),
            fun && g_pixelateEnabled.load(std::memory_order_relaxed),
            fun && g_ditheringEnabled.load(std::memory_order_relaxed),
            fun && g_vhsEnabled.load(std::memory_order_relaxed),
            fun && g_crtEnabled.load(std::memory_order_relaxed),
            g_upscaleMethod.load(std::memory_order_relaxed),
        };
    }

    bool preparePostProcess(PostProcessKey const& key) {
        if (g_preparedPostProcessKey == key) {
            return true;
        }

        bv::render::GlStateGuard prepareState;
        auto const& config = key.config;
        bool prepared = g_postProcessPipeline.prepare(key.width, key.height);
        switch (config.aa) {
            case AntiAliasingMethod::Fxaa:
                g_smaaRenderer.reset();
                prepared = prepared &&
                    g_fxaaRenderer.prepare(bv::shaders::kFxaaShader, key.width, key.height);
                break;
            case AntiAliasingMethod::SmaaHigh:
                g_fxaaRenderer.reset();
                prepared = prepared &&
                    g_smaaRenderer.prepare(
                        bv::shaders::smaa::kSmaaHighShaderSet, key.width, key.height
                    );
                break;
            case AntiAliasingMethod::SmaaUltra:
                g_fxaaRenderer.reset();
                prepared = prepared &&
                    g_smaaRenderer.prepare(
                        bv::shaders::smaa::kSmaaUltraShaderSet, key.width, key.height
                    );
                break;
            case AntiAliasingMethod::Off:
                g_fxaaRenderer.reset();
                g_smaaRenderer.reset();
                break;
        }
        if (prepared && config.cas) {
            prepared = g_casRenderer.prepare(bv::shaders::kCasShader, key.width, key.height);
        }
        else if (!config.cas) {
            g_casRenderer.reset();
        }
        if (prepared && config.bloom) {
            prepared = g_bloomRenderer.prepare(key.width, key.height);
            if (prepared) {
                g_bloomRenderer.setParams(
                    config.bloomThreshold, config.bloomIntensity, config.bloomRadius
                );
            }
        }
        else if (!config.bloom) {
            g_bloomRenderer.reset();
        }

        struct Effect {
            bool enabled;
            bv::render::PostProcessRenderer* renderer;
            bv::render::PostProcessShader const* shader;
        };

        std::array<Effect, 5> const effects{{
            {config.grayscale, &g_grayscaleRenderer, &bv::shaders::kGrayscaleShader},
            {config.pixelate, &g_pixelateRenderer, &bv::shaders::kPixelateShader},
            {config.dithering, &g_ditheringRenderer, &bv::shaders::kDitheringShader},
            {config.vhs, &g_vhsRenderer, &bv::shaders::kVhsShader},
            {config.crt, &g_crtRenderer, &bv::shaders::kCrtShader},
        }};
        for (auto const& effect : effects) {
            if (prepared && effect.enabled) {
                prepared = effect.renderer->prepare(*effect.shader, key.width, key.height);
            }
            else if (!effect.enabled) {
                effect.renderer->reset();
            }
        }

        if (prepared && key.needsPresentation) {
            if (config.upscaling == UpscaleMethod::Fsr) {
                g_renderScaleRenderer.reset();
                prepared = g_fsrRenderer.prepare(bv::shaders::kFsrShader, key.width, key.height);
            }
            else {
                g_fsrRenderer.reset();
                prepared = g_renderScaleRenderer.prepare(
                    bv::shaders::kRenderScaleShader, key.width, key.height
                );
            }
        }
        else if (!key.needsPresentation) {
            g_renderScaleRenderer.reset();
            g_fsrRenderer.reset();
        }

        if (prepared) {
            g_preparedPostProcessKey = key;
        }
        return prepared;
    }

    void renderEffects(
        PostProcessConfig const& config, GLfloat casSharpness,
        bv::render::RenderTarget const& frameTarget, bool needsPresentation
    ) {
        auto remaining = config.effectCount();
        assert(remaining > 0);

        auto runStage = [&](auto&& render) {
            auto const terminal = --remaining == 0;
            auto const writeToCaller = terminal && !needsPresentation;
            auto const target = writeToCaller ? frameTarget : g_postProcessPipeline.nextTarget();
            render(target);
            if (!writeToCaller) {
                g_postProcessPipeline.advanceStage();
            }
        };
        auto runPost = [&](bv::render::PostProcessRenderer& renderer, GLfloat scalar = 0.f) {
            runStage([&](bv::render::RenderTarget const& target) {
                glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
                glViewport(target.x, target.y, target.width, target.height);
                renderer.apply(g_postProcessPipeline.currentTexture(), scalar);
            });
        };

        switch (config.aa) {
            case AntiAliasingMethod::Fxaa: runPost(g_fxaaRenderer); break;
            case AntiAliasingMethod::SmaaHigh:
            case AntiAliasingMethod::SmaaUltra:
                runStage([&](bv::render::RenderTarget const& target) {
                    g_smaaRenderer.apply(g_postProcessPipeline.currentTexture(), target);
                });
                break;
            case AntiAliasingMethod::Off: break;
        }
        if (config.cas) {
            runPost(g_casRenderer, casSharpness);
        }
        if (config.bloom) {
            runStage([&](bv::render::RenderTarget const& target) {
                g_bloomRenderer.apply(g_postProcessPipeline.currentTexture(), target);
            });
        }
        if (config.grayscale) {
            runPost(g_grayscaleRenderer);
        }
        if (config.pixelate) {
            runPost(g_pixelateRenderer);
        }
        if (config.dithering) {
            runPost(g_ditheringRenderer);
        }
        if (config.vhs) {
            static auto const clockStart = std::chrono::steady_clock::now();
            auto const elapsed =
                std::chrono::duration<GLfloat>(std::chrono::steady_clock::now() - clockStart).count();
            runPost(g_vhsRenderer, elapsed);
        }
        if (config.crt) {
            runPost(g_crtRenderer);
        }
    }

    void renderSceneWithPostProcessing(auto&& visitNext) {
        auto const config = selectedPostProcessConfig();
        GLint callerFramebuffer = 0;
        std::array<GLint, 4> callerViewport = {};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &callerFramebuffer);
        glGetIntegerv(GL_VIEWPORT, callerViewport.data());
        auto const width = static_cast<GLsizei>(callerViewport[2]);
        auto const height = static_cast<GLsizei>(callerViewport[3]);
        if (width <= 0 || height <= 0) {
            visitNext();
            return;
        }

        auto const effectCount = config.effectCount();
        auto const renderScale = g_renderScale.load(std::memory_order_relaxed);
        auto const internalWidth = scaledDimension(width, renderScale);
        auto const internalHeight = scaledDimension(height, renderScale);
        auto const needsPresentation = internalWidth != width || internalHeight != height;
        if (effectCount == 0 && !needsPresentation) {
            if (g_preparedPostProcessKey || g_failedPostProcessKey) {
                resetRenderResources();
            }
            visitNext();
            return;
        }

        auto const scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
        std::array<GLint, 4> callerScissor = {};
        glGetIntegerv(GL_SCISSOR_BOX, callerScissor.data());

        PostProcessKey const key{config, internalWidth, internalHeight, needsPresentation};
        PostProcessFailureKey const failureKey{
            key,
            static_cast<GLuint>(callerFramebuffer),
            callerViewport,
        };

        if (g_failedPostProcessKey && *g_failedPostProcessKey == failureKey) {
            visitNext();
            return;
        }
        if (!preparePostProcess(key)) {
            resetRenderResources();
            g_failedPostProcessKey = failureKey;
            log::warn("Post-processing disabled for this configuration");
            visitNext();
            return;
        }

        bv::render::RenderTarget const callerTarget{
            static_cast<GLuint>(callerFramebuffer),
            callerViewport[0],
            callerViewport[1],
            width,
            height,
        };

        g_postProcessPipeline.beginSceneCapture();
        if (scissorEnabled == GL_TRUE) {
            auto const scissor =
                scaledScissorBox(callerScissor, callerViewport, internalWidth, internalHeight);
            glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
        }
        g_isSceneCaptureActive = true;
        g_captureWidth = internalWidth;
        g_captureHeight = internalHeight;
        visitNext();
        g_isSceneCaptureActive = false;

        {
            bv::render::GlStateGuard postSceneState;
            glDisable(GL_BLEND);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_STENCIL_TEST);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_CULL_FACE);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            g_postProcessPipeline.bindQuad();

            if (effectCount > 0) {
                renderEffects(
                    config,
                    config.cas ? g_casSharpness.load(std::memory_order_relaxed) : 0.f,
                    callerTarget,
                    needsPresentation
                );
            }

            if (needsPresentation) {
                auto const sourceTexture = g_postProcessPipeline.currentTexture();
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, sourceTexture);
                glTexParameteri(
                    GL_TEXTURE_2D,
                    GL_TEXTURE_MIN_FILTER,
                    config.upscaling == UpscaleMethod::Bilinear ? GL_LINEAR : GL_NEAREST
                );
                glTexParameteri(
                    GL_TEXTURE_2D,
                    GL_TEXTURE_MAG_FILTER,
                    config.upscaling == UpscaleMethod::Bilinear ? GL_LINEAR : GL_NEAREST
                );
                glBindFramebuffer(GL_FRAMEBUFFER, callerTarget.framebuffer);
                glViewport(callerTarget.x, callerTarget.y, callerTarget.width, callerTarget.height);
                if (config.upscaling == UpscaleMethod::Fsr) {
                    g_fsrRenderer.apply(sourceTexture, g_fsrSharpness.load(std::memory_order_relaxed));
                }
                else {
                    g_renderScaleRenderer.apply(sourceTexture);
                }
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, callerTarget.framebuffer);
        glViewport(callerTarget.x, callerTarget.y, callerTarget.width, callerTarget.height);
        g_failedPostProcessKey.reset();
    }

} // namespace

$on_mod(Loaded) {
    bindBoolSetting("enabled", g_modEnabled);
    bindBoolSetting("enable-fun", g_funEnabled);
    bindDoubleSetting("render-scale", g_renderScale);
    bindStringSetting("upscale-method", updateUpscaleMethod);
    bindStringSetting("aa-method", updateAntiAliasingMethod);
    bindBoolSetting("cas-enabled", g_casEnabled);
    bindDoubleSetting("cas-sharpness", g_casSharpness);
    bindDoubleSetting("fsr-sharpness", g_fsrSharpness);
    bindBoolSetting("bloom-enabled", g_bloomEnabled);
    bindDoubleSetting("bloom-threshold", g_bloomThreshold);
    bindDoubleSetting("bloom-intensity", g_bloomIntensity);
    bindDoubleSetting("bloom-radius", g_bloomRadius);
    bindBoolSetting("grayscale-enabled", g_grayscaleEnabled);
    bindBoolSetting("pixelate-enabled", g_pixelateEnabled);
    bindBoolSetting("dithering-enabled", g_ditheringEnabled);
    bindBoolSetting("vhs-enabled", g_vhsEnabled);
    bindBoolSetting("crt-enabled", g_crtEnabled);
}

class $modify(BetterVisualsDirectorHook, CCDirector) {
    void setProjection(ccDirectorProjection kProjection) {
        CCDirector::setProjection(kProjection);
        applyCaptureViewport();
    }

    void setViewport() {
        CCDirector::setViewport();
        applyCaptureViewport();
    }
};

#ifdef GEODE_IS_WINDOWS
class $modify(BetterVisualsEGLViewHook, CCEGLView) {
    void setViewPortInPoints(float x, float y, float w, float h) {
        CCEGLView::setViewPortInPoints(x, y, w, h);
        applyCaptureViewport();
    }
};
#endif

class $modify(BetterVisualsGameLayer, GJBaseGameLayer) {
    static void onModify(auto& self) {
        if (!self.setHookPriorityPre("GJBaseGameLayer::visit", Priority::VeryLate)) {
            log::warn("Unable to set GJBaseGameLayer::visit hook priority");
        }
    }

    void visit() {
        if (g_temporarilyDisabled.load(std::memory_order_relaxed) ||
            !g_modEnabled.load(std::memory_order_relaxed) || g_isGameLayerVisitActive ||
            static_cast<GJBaseGameLayer*>(this) != GJBaseGameLayer::get()) {
            GJBaseGameLayer::visit();
            return;
        }

        g_isGameLayerVisitActive = true;
        renderSceneWithPostProcessing([this] {
            GJBaseGameLayer::visit();
        });
        g_isGameLayerVisitActive = false;
    }
};

#ifdef GEODE_IS_WINDOWS
class $modify(BetterVisualsEGLView, CCEGLView) {
    void toggleFullScreen(bool value, bool borderless, bool fix) {
        if (!g_temporarilyDisabled.exchange(true)) {
            resetRenderResources();
        }
        CCEGLView::toggleFullScreen(value, borderless, fix);
    }
};
#endif

class $modify(BetterVisualsMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        if (g_temporarilyDisabled.load(std::memory_order_relaxed) &&
            !g_disablePopupShown.exchange(true)) {
            queueInMainThread([] {
                createQuickPopup(
                    "Better Visuals and FPS",
                    "Better Visuals and FPS temporary disabled.\n"
                    " Please restart the game to keep fullscreen/windowed mode.\n"
                    "Sorry for the inconvenience!",
                    "OK",
                    nullptr,
                    [](FLAlertLayer*, bool) {}
                );
            });
        }
        return true;
    }
};

#ifdef GEODE_IS_MOBILE
class $modify(BetterVisualsAppDelegate, AppDelegate) {
    void applicationDidEnterBackground() {
        resetRenderResources();
        AppDelegate::applicationDidEnterBackground();
    }
};
#endif
