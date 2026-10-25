// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_settings.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <set>
#include <string_view>
#include <vector>

namespace BbSettings {

namespace {

const char* Path() {
    const char* env = std::getenv("BB_CONFIG");
    return env && env[0] ? env : "bbport.ini";
}

float Clamp(float v, float lo, float hi) {
    return std::clamp(v, lo, hi);
}

void Set(Values& v, const std::string& key, const std::string& value) {
    const float f = float(std::atof(value.c_str()));
    const int i = std::atoi(value.c_str());
    if (key == "upscaler") {
        for (int u = 0; u < UpscalerCount; ++u) {
            if (value == UpscalerName(u)) {
                v.upscaler = u;
            }
        }
    } else if (key == "frame_gen") {
        for (int g = 0; g < FrameGenCount; ++g) {
            if (value == FrameGenName(g)) {
                v.frame_gen = g;
            }
        }
    } else if (key == "frame_gen_multiplier") {
        v.frame_gen_multiplier = std::clamp(i, 2, 4);
    } else if (key == "preset") {
        v.preset = std::clamp(i, 0, PresetCount - 1);
    } else if (key == "sharpen") {
        v.sharpen = i != 0;
    } else if (key == "sharpness") {
        v.sharpness = Clamp(f, 0.0f, 2.0f);
    } else if (key == "jitter") {
        v.jitter = i != 0;
    } else if (key == "reactive") {
        v.reactive = i != 0;
    } else if (key == "object_motion") {
        v.object_motion = i != 0;
    } else if (key == "reactive_scale") {
        v.reactive_scale = Clamp(f, 0.0f, 16.0f);
    } else if (key == "reactive_threshold") {
        v.reactive_threshold = Clamp(f, 0.0f, 1.0f);
    } else if (key == "reactive_max") {
        v.reactive_max = Clamp(f, 0.0f, 1.0f);
    } else if (key == "debug_view") {
        v.debug_view = std::clamp(i, 0, DebugViewCount - 1);
    } else if (key == "show_fps") {
        v.show_fps = i != 0;
    } else if (key == "render_res") {
        int w = 0, h = 0;
        if (std::sscanf(value.c_str(), "%dx%d", &w, &h) == 2 && w >= 640 && h >= 360 &&
            w <= 3840 && h <= 2160) {
            v.render_w = w;
            v.render_h = h;
        }
    } else if (key == "shadow_quality") {
        v.shadow_quality = value == "highest" ? 2 : value == "high" ? 1 : 0;
    } else if (key == "fullscreen") {
        v.fullscreen = i != 0 ? 1 : 0;
    } else if (key == "hair_shadows") {
        v.hair_shadows = i != 0;
    } else if (key == "fps_limit") {
        v.fps_limit = std::clamp(i, 0, 1000);
    } else if (key == "fsr4_auto_exposure") {
        v.fsr4_auto_exposure = i != 0;
    } else if (key == "fsr4_invert_jitter") {
        v.fsr4_invert_jitter = i != 0;
    } else if (key == "model_lod") {
        v.model_lod = std::clamp(i, -2, 2);
    } else if (key == "live_resolution") {
        v.live_resolution = value == "auto" ? -1 : std::clamp(i, 0, 1);
    } else if (key == "output_res") {
        for (int r = 0; r < OutputCount; ++r) {
            if (value == std::to_string(OutputWidths[r]) + "x" + std::to_string(OutputHeights[r])) {
                v.output_res = r;
            }
        }
    } else {
        for (int e = 0; e < EffectCount; ++e) {
            if (key == Effects[e].key) {
                v.effects[e] = i != 0;
            }
        }
    }
}

} // namespace

Values& Get() {
    static Values values;
    return values;
}

void Load() {
    auto& v = Get();
    for (int e = 0; e < EffectCount; ++e) {
        v.effects[e] = Effects[e].default_on;
    }
    if (FILE* file = std::fopen(Path(), "r")) {
        char line[256];
        while (std::fgets(line, sizeof(line), file)) {
            std::string text{line};
            text.erase(text.find_last_not_of(" \t\r\n") + 1);
            const auto eq = text.find('=');
            if (text.empty() || text[0] == '#' || eq == std::string::npos) {
                continue;
            }
            Set(v, text.substr(0, eq), text.substr(eq + 1));
        }
        std::fclose(file);
        std::printf("Settings: %s\n", Path());
    }
    // Environment overrides (scripts, A/B tests).
    if (const char* env = std::getenv("BB_UPSCALER")) {
        v.upscaler = UpscalerOff;
        for (int u = 0; u < UpscalerCount; ++u) {
            if (std::strcmp(env, UpscalerName(u)) == 0) v.upscaler = u;
        }
    }
    const std::pair<const char*, const char*> env_keys[] = {
        {"BB_FSR_SHARPNESS", "sharpness"},        {"BB_JITTER", "jitter"},
        {"BB_REACTIVE", "reactive"},              {"BB_REACTIVE_SCALE", "reactive_scale"},
        {"BB_REACTIVE_THRESHOLD", "reactive_threshold"}, {"BB_REACTIVE_MAX", "reactive_max"},
        {"BB_UPSCALE_PRESET", "preset"},            {"BB_OBJECT_MOTION", "object_motion"},
        {"BB_FRAME_GEN", "frame_gen"},              {"BB_FRAME_GEN_MULTIPLIER", "frame_gen_multiplier"},
    };
    for (const auto& [env, key] : env_keys) {
        if (const char* value = std::getenv(env)) {
            Set(v, key, value);
        }
    }
    v.startup_preset = v.preset;
    v.startup_upscaler = v.upscaler;
    v.startup_object_motion = v.object_motion;
    for (int e = 0; e < EffectCount; ++e) {
        v.startup_effects[e] = v.effects[e];
    }
    v.startup_model_lod = v.model_lod;
    v.startup_output_res = v.output_res;
    v.startup_live_resolution = v.live_resolution;
    // As runtime_settings.c read them for the eboot patches.
    if (const char* size = std::getenv("BB_RENDER_SIZE")) Set(v, "render_res", size);
    if (const char* shadows = std::getenv("BB_SHADOW_QUALITY")) Set(v, "shadow_quality", shadows);
    v.startup_render_w = v.render_w;
    v.startup_render_h = v.render_h;
    v.startup_shadow_quality = v.shadow_quality;
}

void ConfigureUpscalerSupport(bool fsr4, bool fsr411) {
    auto& v = Get();
    v.fsr4_supported = fsr4;
    v.fsr411_supported = fsr4 && fsr411;
    const int requested = v.upscaler;
    if ((requested == UpscalerFsr4 && !v.fsr4_supported) ||
        (requested == UpscalerFsr411 && !v.fsr411_supported)) {
        v.fsr4_problem = "GPU does not support the selected FSR 4 shaders; using FSR 3.1";
        std::printf("Upscaler: %s unsupported on this GPU; falling back to FSR 3.1 before the first frame\n",
                    UpscalerName(requested));
        v.upscaler = UpscalerFsr3;
    }
}

void ConfigureDlssSupport(bool available, const char* problem) {
    auto& v = Get();
    v.dlss_supported = available;
    static std::string kept;
    kept = problem ? problem : "";
    v.dlss_problem = available || kept.empty() ? nullptr : kept.c_str();
    if (v.upscaler == UpscalerDlss && !available) {
        std::printf("Upscaler: DLSS unavailable (%s); falling back to FSR 3.1\n",
                    kept.empty() ? "bbport_dlss.dll or nvngx_dlss.dll missing" : kept.c_str());
        v.upscaler = UpscalerFsr3;
    }
}

bool FixedRenderSession() {
    const char* size = std::getenv("BB_RENDER_RES");
    return size && size[0];
}

int RenderPreset() {
    const auto& v = Get();
    return FixedRenderSession() ? v.startup_preset :
        v.upscaler == UpscalerTaa ? NativeAA : v.preset.load();
}

bool ResolutionNeedsRestart() {
    const auto& v = Get();
    // TAA needs the live path (native guest targets): run.sh selects it on restart.
    return FixedRenderSession() &&
        (v.preset != v.startup_preset || v.output_res != v.startup_output_res ||
         (v.upscaler == UpscalerOff) != (v.startup_upscaler == UpscalerOff) ||
         (v.upscaler == UpscalerTaa) != (v.startup_upscaler == UpscalerTaa));
}

void Save() {
    const auto& v = Get();
    // Lines this build does not write (keys of other versions, the launcher's) are kept.
    std::vector<std::string> old_lines;
    if (FILE* old = std::fopen(Path(), "r")) {
        char line[512];
        while (std::fgets(line, sizeof(line), old)) {
            std::string text{line};
            text.erase(text.find_last_not_of(" \t\r\n") + 1);
            if (!text.empty() && text[0] != '#' && text.find('=') != std::string::npos) {
                old_lines.push_back(text);
            }
        }
        std::fclose(old);
    }
    FILE* file = std::fopen(Path(), "w");
    if (!file) {
        std::printf("Settings: cannot write %s\n", Path());
        return;
    }
    std::fprintf(file,
                 "# bbport settings (in-game menu: Insert / L3+R3)\n"
                 "upscaler=%s\npreset=%d\nsharpen=%d\nsharpness=%.2f\njitter=%d\n"
                 "reactive=%d\nobject_motion=%d\nreactive_scale=%.2f\nreactive_threshold=%.2f\nreactive_max=%.2f\n"
                 "debug_view=%d\nshow_fps=%d\nfsr4_auto_exposure=%d\nfsr4_invert_jitter=%d\n",
                 UpscalerName(v.upscaler), v.preset.load(), int(v.sharpen.load()),
                 v.sharpness.load(), int(v.jitter.load()), int(v.reactive.load()),
                 int(v.object_motion.load()),
                 v.reactive_scale.load(), v.reactive_threshold.load(), v.reactive_max.load(),
                 v.debug_view.load(), int(v.show_fps.load()),
                 int(v.fsr4_auto_exposure.load()), int(v.fsr4_invert_jitter.load()));
    // Read by patches.py at start.
    for (int e = 0; e < EffectCount; ++e) {
        std::fprintf(file, "%s=%d\n", Effects[e].key, int(v.effects[e].load()));
    }
    std::fprintf(file, "model_lod=%d\noutput_res=%dx%d\n", v.model_lod.load(),
                 OutputWidths[v.output_res], OutputHeights[v.output_res]);
    std::fprintf(file, "frame_gen=%s\nframe_gen_multiplier=%d\n", FrameGenName(v.frame_gen),
                 v.frame_gen_multiplier.load());
    // Read by run.sh at start.
    std::fprintf(file, "live_resolution=%s\n", v.live_resolution < 0 ? "auto"
                                                  : v.live_resolution ? "1" : "0");
    std::fprintf(file, "fps_limit=%d\n", v.fps_limit.load());
    static constexpr const char* shadow_names[] = {"ps4", "high", "highest"};
    std::fprintf(file, "render_res=%dx%d\nshadow_quality=%s\n", v.render_w.load(), v.render_h.load(),
                 shadow_names[std::clamp(v.shadow_quality.load(), 0, 2)]);
    std::fprintf(file, "hair_shadows=%d\n", int(v.hair_shadows.load()));
    if (v.fullscreen >= 0) std::fprintf(file, "fullscreen=%d\n", v.fullscreen.load());
    std::fclose(file);
    std::set<std::string> written;
    if (FILE* now = std::fopen(Path(), "r")) {
        char line[512];
        while (std::fgets(line, sizeof(line), now)) {
            const std::string text{line};
            const auto eq = text.find('=');
            if (text[0] != '#' && eq != std::string::npos) written.insert(text.substr(0, eq));
        }
        std::fclose(now);
    }
    if (FILE* append = std::fopen(Path(), "a")) {
        for (const auto& line : old_lines) {
            if (!written.count(line.substr(0, line.find('=')))) {
                std::fprintf(append, "%s\n", line.c_str());
            }
        }
        std::fclose(append);
    }
}

// The port's Options > Display rows (src/runtime_menu.c).
extern "C" int bbgpu_fps_limit() {
    return Get().fps_limit.load();
}
extern "C" void bbgpu_render_res(int* w, int* h, int* startup_w, int* startup_h) {
    const auto& v = Get();
    *w = v.render_w;
    *h = v.render_h;
    *startup_w = v.startup_render_w;
    *startup_h = v.startup_render_h;
}
extern "C" void bbgpu_set_render_res(int w, int h) {
    Get().render_w = w;
    Get().render_h = h;
    Save();
}
extern "C" int bbgpu_shadow_quality(int* startup) {
    *startup = Get().startup_shadow_quality;
    return Get().shadow_quality;
}
extern "C" void bbgpu_set_shadow_quality(int quality) {
    Get().shadow_quality = std::clamp(quality, 0, 2);
    Save();
}
extern "C" int bbgpu_hair_shadows() {
    return Get().hair_shadows ? 1 : 0;
}
extern "C" void bbgpu_set_hair_shadows(int on) {
    Get().hair_shadows = on != 0;
    Save();
}
extern "C" void bbgpu_set_fps_limit(int fps) {
    Get().fps_limit = std::clamp(fps, 0, 1000);
    Save();
}

float PresetScale(int preset) {
    static constexpr float scales[PresetCount] = {1.0f, 1.5f, 1.7f, 2.0f, 3.0f};
    return scales[std::clamp(preset, 0, PresetCount - 1)];
}

const char* PresetName(int preset) {
    static constexpr const char* names[PresetCount] = {"Native AA", "Quality", "Balanced",
                                                       "Performance", "Ultra Performance"};
    return names[std::clamp(preset, 0, PresetCount - 1)];
}

const char* UpscalerName(int upscaler) {
    static constexpr const char* names[UpscalerCount] = {"off", "fsr3", "fsr4", "fsr411", "taa", "dlss"};
    return names[std::clamp(upscaler, 0, UpscalerCount - 1)];
}

const SoftShadow& SoftShadowKernel() {
    static const SoftShadow kernel = [] {
        int taps = 3;
        float spacing = 1.0f;
        if (const char* env = std::getenv("BB_SOFT_SHADOW")) {
            std::sscanf(env, "%d,%f", &taps, &spacing);
        }
        taps = std::clamp(taps, 1, 6);
        spacing = std::clamp(spacing, 0.25f, 4.0f);
        static char name[32];
        std::snprintf(name, sizeof(name), "%dx%.2f", taps, spacing);
        return SoftShadow{taps, spacing, name};
    }();
    return kernel;
}

const char* FrameGenName(int frame_gen) {
    static constexpr const char* names[FrameGenCount] = {"off", "fsr3", "dlss"};
    return names[std::clamp(frame_gen, 0, FrameGenCount - 1)];
}

} // namespace BbSettings
