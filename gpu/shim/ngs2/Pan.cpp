// libSceNgs2 from AnyPS5 (github.com/boykopovar/AnyPS5, GPL-2.0; Adrià Franch, boykopovar),
// as patched for Quake (drakenabarion/AnyPS5). bbport: built into libbbgpu (ngs2_register.cpp).
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "ngs2_general.h"
#include "Ngs2Types.hpp"

// Speaker panning helpers. The work area and parameter layouts follow the SDK
// definitions (SceNgs2PanWork / SceNgs2PanParam); both fit inside the opaque types.
namespace {

constexpr std::uint32_t MaxSpeakers = 8;
constexpr float HalfPi = 1.57079632679489661923f;

struct PanWorkLayout {
    float speakerAngle[MaxSpeakers];
    float unitAngle;
    std::uint32_t numSpeakers;
};

struct PanParamLayout {
    float angle;
    float distance;
    float fbwLevel;
    float lfeLevel;
};

static_assert(sizeof(PanWorkLayout) <= sizeof(Ngs2PanWork));

// 5.1 and 7.1 layouts carry the LFE channel at index 3 (L, R, C, LFE, ...).
int LfeIndex(std::uint32_t numSpeakers) {
    return numSpeakers == 6 || numSpeakers == 8 ? 3 : -1;
}

// Standard speaker positions in degrees, used when the caller passes no angle list.
// Channel order matches the Ngs2 layouts: L, R, C, LFE, then surrounds and backs.
bool DefaultAngles(std::uint32_t numSpeakers, float* out) {
    static const float Mono[] = {0.0f};
    static const float Stereo[] = {-30.0f, 30.0f};
    static const float Quad[] = {-45.0f, 45.0f, -135.0f, 135.0f};
    static const float Surround51[] = {-30.0f, 30.0f, 0.0f, 0.0f, -110.0f, 110.0f};
    static const float Surround71[] = {-30.0f, 30.0f, 0.0f, 0.0f, -90.0f, 90.0f, -150.0f, 150.0f};
    const float* table = numSpeakers == 1 ? Mono : numSpeakers == 2 ? Stereo : numSpeakers == 4 ? Quad
                       : numSpeakers == 6 ? Surround51 : numSpeakers == 8 ? Surround71 : nullptr;
    if (!table) return false;
    for (std::uint32_t i = 0; i < numSpeakers; ++i) out[i] = table[i];
    return true;
}

float Wrap(float turns) {
    turns -= std::floor(turns);
    return turns;
}

}

#pragma GCC visibility push(default)

extern "C" {

int APS5_VABI sceNgs2PanInit(Ngs2PanWork* work, const float* speaker_angles, float unit_angle, uint32_t num_speakers) {
    if (!work || !(unit_angle > 0.0f) || num_speakers == 0 || num_speakers > MaxSpeakers) APS5_INVALID_ARG_EX;
    auto* layout = reinterpret_cast<PanWorkLayout*>(work);
    // Only the SDK work area (40 bytes) belongs to the caller: Ngs2PanWork here is an oversized
    // opaque type, and clearing all of it overwrites whatever the title keeps after its work area.
    *layout = PanWorkLayout{};
    if (speaker_angles) {
        for (std::uint32_t i = 0; i < num_speakers; ++i) layout->speakerAngle[i] = speaker_angles[i];
    } else {
        // No list: standard positions for the layout, converted from degrees to the caller's unit.
        if (!DefaultAngles(num_speakers, layout->speakerAngle)) APS5_INVALID_ARG_EX;
        for (std::uint32_t i = 0; i < num_speakers; ++i) layout->speakerAngle[i] *= unit_angle / 360.0f;
    }
    layout->unitAngle = unit_angle;
    layout->numSpeakers = num_speakers;
    return 0;
}

// Fills an output-major matrix (out[speaker * num_params + param]), the layout the voice
// renderer consumes for matrix levels. Equal-power panning between the two full-band
// speakers adjacent to the source angle; distance blends from an even spread (0) to the
// fully panned position (1).
int APS5_VABI sceNgs2PanGetVolumeMatrix(Ngs2PanWork* work, const Ngs2PanParam* params, uint32_t num_params, uint32_t matrix_format, float* out_volume_matrix) {
    (void)matrix_format;
    if (!work || !params || !out_volume_matrix || num_params == 0 || num_params > MaxSpeakers) APS5_INVALID_ARG_EX;
    const auto* layout = reinterpret_cast<const PanWorkLayout*>(work);
    const std::uint32_t speakers = layout->numSpeakers;
    if (speakers == 0 || speakers > MaxSpeakers || !(layout->unitAngle > 0.0f)) APS5_INVALID_ARG_EX;
    const int lfe = LfeIndex(speakers);

    std::vector<std::pair<float, std::uint32_t>> ring;
    for (std::uint32_t s = 0; s < speakers; ++s)
        if (static_cast<int>(s) != lfe) ring.emplace_back(Wrap(layout->speakerAngle[s] / layout->unitAngle), s);
    std::sort(ring.begin(), ring.end());

    const auto* p = reinterpret_cast<const PanParamLayout*>(params);
    for (std::uint32_t i = 0; i < num_params; ++i) {
        float panned[MaxSpeakers] = {};
        if (ring.size() == 1) {
            panned[ring[0].second] = 1.0f;
        } else {
            const float a = Wrap(p[i].angle / layout->unitAngle);
            std::size_t k = ring.size() - 1;
            for (std::size_t j = 0; j < ring.size(); ++j)
                if (ring[j].first <= a) k = j;
            const std::size_t next = (k + 1) % ring.size();
            float arc = ring[next].first - ring[k].first;
            float offset = a - ring[k].first;
            if (arc <= 0.0f) arc += 1.0f;
            if (offset < 0.0f) offset += 1.0f;
            const float t = std::clamp(offset / arc, 0.0f, 1.0f);
            panned[ring[k].second] = std::cos(t * HalfPi);
            panned[ring[next].second] = std::sin(t * HalfPi);
        }
        const float distance = std::clamp(p[i].distance, 0.0f, 1.0f);
        const float even = 1.0f / static_cast<float>(ring.size());
        for (std::uint32_t s = 0; s < speakers; ++s) {
            float gain;
            if (static_cast<int>(s) == lfe) gain = p[i].lfeLevel;
            else gain = std::sqrt((1.0f - distance) * even + distance * panned[s] * panned[s]) * p[i].fbwLevel;
            out_volume_matrix[s * num_params + i] = gain;
        }
    }
    return 0;
}

}

#pragma GCC visibility pop
