#include "render/BloomRenderer.hpp"
#include "render/GlStateGuard.hpp"
#include "render/PostProcessPipeline.hpp"
#include "render/PostProcessRenderer.hpp"
#include "render/SmaaRenderer.hpp"
#include "shaders/PostProcessShaders.hpp"
#include "shaders/aa/SmaaShader.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/CCNode.hpp>
#include <Geode/platform/cplatform.h>
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

    enum class AntiAliasingMode {
        Off,
        Fxaa,
        SmaaHigh,
        SmaaUltra,
    };

    struct PostProcessConfig {
        AntiAliasingMode aa = AntiAliasingMode::Off;
        bool cas = false;
        bool bloom = false;
        bool grayscale = false;
        bool pixelate = false;
        bool dithering = false;
        bool vhs = false;
        bool crt = false;

        bool operator==(PostProcessConfig const&) const = default;

        int effectCount() const {
            return (aa != AntiAliasingMode::Off) + cas + bloom + grayscale + pixelate + dithering +
                vhs + crt;
        }
    };

    struct PostProcessKey {
        PostProcessConfig config;
        GLsizei width = 0;
        GLsizei height = 0;

        bool operator==(PostProcessKey const&) const = default;
    };

    struct PostProcessFailureKey {
        PostProcessConfig config;
        GLuint framebuffer = 0;
        std::array<GLint, 4> viewport = {};

        bool operator==(PostProcessFailureKey const&) const = default;
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
    bv::render::PostProcessPipeline g_postProcessPipeline;
    bv::render::PostProcessRenderer g_fxaaRenderer;
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
    bool g_isRootSceneVisitActive = false;

    void resetRenderResources() {
        g_postProcessPipeline.reset();
        g_fxaaRenderer.reset();
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

    PostProcessConfig selectedPostProcessConfig() {
        return {
            g_antiAliasingMode.load(std::memory_order_relaxed),
            g_casEnabled.load(std::memory_order_relaxed),
            g_bloomEnabled.load(std::memory_order_relaxed),
            g_grayscaleEnabled.load(std::memory_order_relaxed),
            g_pixelateEnabled.load(std::memory_order_relaxed),
            g_ditheringEnabled.load(std::memory_order_relaxed),
            g_vhsEnabled.load(std::memory_order_relaxed),
            g_crtEnabled.load(std::memory_order_relaxed),
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
            case AntiAliasingMode::Fxaa:
                g_smaaRenderer.reset();
                prepared = prepared &&
                    g_fxaaRenderer.prepare(bv::shaders::kFxaaShader, key.width, key.height);
                break;
            case AntiAliasingMode::SmaaHigh:
                g_fxaaRenderer.reset();
                prepared = prepared &&
                    g_smaaRenderer.prepare(
                        bv::shaders::smaa::kSmaaHighShaderSet, key.width, key.height
                    );
                break;
            case AntiAliasingMode::SmaaUltra:
                g_fxaaRenderer.reset();
                prepared = prepared &&
                    g_smaaRenderer.prepare(
                        bv::shaders::smaa::kSmaaUltraShaderSet, key.width, key.height
                    );
                break;
            case AntiAliasingMode::Off:
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
        }
        else if (!config.bloom) {
            g_bloomRenderer.reset();
        }
        if (prepared && config.grayscale) {
            prepared =
                g_grayscaleRenderer.prepare(bv::shaders::kGrayscaleShader, key.width, key.height);
        }
        else if (!config.grayscale) {
            g_grayscaleRenderer.reset();
        }
        if (prepared && config.pixelate) {
            prepared =
                g_pixelateRenderer.prepare(bv::shaders::kPixelateShader, key.width, key.height);
        }
        else if (!config.pixelate) {
            g_pixelateRenderer.reset();
        }
        if (prepared && config.dithering) {
            prepared =
                g_ditheringRenderer.prepare(bv::shaders::kDitheringShader, key.width, key.height);
        }
        else if (!config.dithering) {
            g_ditheringRenderer.reset();
        }
        if (prepared && config.vhs) {
            prepared = g_vhsRenderer.prepare(bv::shaders::kVhsShader, key.width, key.height);
        }
        else if (!config.vhs) {
            g_vhsRenderer.reset();
        }
        if (prepared && config.crt) {
            prepared = g_crtRenderer.prepare(bv::shaders::kCrtShader, key.width, key.height);
        }
        else if (!config.crt) {
            g_crtRenderer.reset();
        }

        if (prepared) {
            g_preparedPostProcessKey = key;
        }
        return prepared;
    }

    void renderEffects(
        PostProcessConfig const& config, GLfloat casSharpness,
        bv::render::RenderTarget const& frameTarget
    ) {
        auto remaining = config.effectCount();
        assert(remaining > 0);

        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        g_postProcessPipeline.bindQuad();

        auto runStage = [&](auto&& render) {
            auto const terminal = --remaining == 0;
            auto const target = terminal ? frameTarget : g_postProcessPipeline.nextTarget();
            render(target);
            if (!terminal) {
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
            case AntiAliasingMode::Fxaa: runPost(g_fxaaRenderer); break;
            case AntiAliasingMode::SmaaHigh:
            case AntiAliasingMode::SmaaUltra:
                runStage([&](bv::render::RenderTarget const& target) {
                    g_smaaRenderer.apply(g_postProcessPipeline.currentTexture(), target);
                });
                break;
            case AntiAliasingMode::Off: break;
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
        if (config.effectCount() == 0) {
            if (g_preparedPostProcessKey || g_failedPostProcessKey) {
                resetRenderResources();
            }
            visitNext();
            return;
        }

        GLint callerFramebuffer = 0;
        std::array<GLint, 4> callerViewport = {};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &callerFramebuffer);
        glGetIntegerv(GL_VIEWPORT, callerViewport.data());
        auto const width = static_cast<GLsizei>(callerViewport[2]);
        auto const height = static_cast<GLsizei>(callerViewport[3]);
        PostProcessKey const key{config, width, height};
        PostProcessFailureKey const failureKey{
            config,
            static_cast<GLuint>(callerFramebuffer),
            callerViewport,
        };

        if (width <= 0 || height <= 0 ||
            (g_failedPostProcessKey && *g_failedPostProcessKey == failureKey)) {
            visitNext();
            return;
        }
        if (!preparePostProcess(key)) {
            resetRenderResources();
            g_failedPostProcessKey = failureKey;
            log::error("Post-processing disabled for this configuration");
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
        visitNext();
        {
            bv::render::GlStateGuard postSceneState;
            renderEffects(
                config, config.cas ? g_casSharpness.load(std::memory_order_relaxed) : 0.f, callerTarget
            );
        }
        glBindFramebuffer(GL_FRAMEBUFFER, callerTarget.framebuffer);
        glViewport(callerTarget.x, callerTarget.y, callerTarget.width, callerTarget.height);
        g_failedPostProcessKey.reset();
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

class $modify(BetterVisualsSceneVisitHook, CCNode) {
    static void onModify(auto& self) {
        if (!self.setHookPriorityPre("cocos2d::CCNode::visit", Priority::VeryLate)) {
            log::warn("Unable to set CCNode::visit hook priority");
        }
    }

    void visit() {
        if (g_isRootSceneVisitActive ||
            static_cast<CCNode*>(this) != CCDirector::get()->getRunningScene()) {
            CCNode::visit();
            return;
        }

        g_isRootSceneVisitActive = true;
        renderSceneWithPostProcessing([this] {
            CCNode::visit();
        });
        g_isRootSceneVisitActive = false;
    }
};

#ifdef GEODE_IS_WINDOWS
class $modify(BetterVisualsEGLView, CCEGLView) {
    void toggleFullScreen(bool value, bool borderless, bool fix) {
        resetRenderResources();
        CCEGLView::toggleFullScreen(value, borderless, fix);
    }
};
#endif

#ifdef GEODE_IS_MOBILE
class $modify(BetterVisualsAppDelegate, AppDelegate) {
    void applicationDidEnterBackground() {
        resetRenderResources();
        AppDelegate::applicationDidEnterBackground();
    }
};
#endif
