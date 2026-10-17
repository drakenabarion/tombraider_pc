// libSceNgs2 from AnyPS5 (github.com/boykopovar/AnyPS5, GPL-2.0; Adrià Franch, boykopovar),
// as patched for Quake (drakenabarion/AnyPS5). bbport: built into libbbgpu (ngs2_register.cpp).
#ifndef CORE_LIBS_PRX_LIBSCENGS2_SRC_NGS2INTERNAL_HPP
#define CORE_LIBS_PRX_LIBSCENGS2_SRC_NGS2INTERNAL_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "Ngs2Types.hpp"

static constexpr std::uint32_t NGS2_MAX_CHANNELS = 8;

struct Ngs2System;
struct Ngs2Rack;

enum class Ngs2PlayState : std::uint32_t {
    Empty = 0,
    Playing = SCE_NGS2_VOICE_STATE_FLAG_INUSE | SCE_NGS2_VOICE_STATE_FLAG_PLAYING,
    Paused = SCE_NGS2_VOICE_STATE_FLAG_INUSE | SCE_NGS2_VOICE_STATE_FLAG_PAUSED,
    Stopped = SCE_NGS2_VOICE_STATE_FLAG_INUSE | SCE_NGS2_VOICE_STATE_FLAG_PLAYING | SCE_NGS2_VOICE_STATE_FLAG_STOPPED,
};

struct Ngs2Block {
    const std::uint8_t* data;
    Ngs2WaveformBlock info;
    std::uint32_t cursor = 0;
    std::uint32_t numRepeated = 0;
    std::size_t dataCursor = 0;
    // PCM blocks play from a copy taken when they are queued: titles stream through ring buffers and
    // refill regions while the blocks are still queued. `guest` is the title's own address, used for
    // everything the title can observe (block callbacks, the reported waveform position).
    const std::uint8_t* guest = nullptr;
    std::shared_ptr<const std::vector<std::uint8_t>> snapshot;
    const std::uint8_t* GuestAt(const std::uint8_t* at) const { return guest != nullptr ? guest + (at - data) : at; }
};

struct Ngs2Atrac9DecoderDeleter {
    void operator()(void* handle) const;
};

struct Ngs2Atrac9 {
    std::unique_ptr<void, Ngs2Atrac9DecoderDeleter> decoder;
    std::uint8_t config[4] = {};
    std::uint32_t frameSamples = 0;
    std::uint32_t framesInSuperframe = 0;
    std::uint32_t superframeBytes = 0;
    std::vector<float> window;
    std::uint32_t windowStart = 0;
};

struct Ngs2Voice;

struct Ngs2Port {
    Ngs2Voice* dest = nullptr;
    float volume = 1.0f;
    std::int32_t matrix = -1;
};

struct Ngs2Voice {
    Ngs2Rack* rack = nullptr;
    Ngs2PlayState state = Ngs2PlayState::Empty;
    std::uint32_t stateFlags = 0;
    std::uint32_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint32_t waveformType = 0;
    Ngs2Atrac9 atrac9;
    float pitch = 1.0f;
    std::uint64_t phase = 0;
    std::deque<Ngs2Block> blocks;
    bool acceptsBlocks = true;
    std::uint64_t decodedSamples = 0;
    std::uint64_t decodedBytes = 0;
    const std::uint8_t* waveformEnd = nullptr;
    Ngs2VoiceCallbackHandler callback = nullptr;
    std::uintptr_t callbackData = 0;
    std::uint32_t callbackFlags = 0;
    std::vector<Ngs2Port> ports;
    std::vector<std::vector<float>> matrices;
    std::uint32_t outputId = 0;
    std::vector<float> samples;
    bool rendering = false;
    bool rendered = false;
    bool hasSamples = false;

    void SetEvent(std::uint32_t eventId);
    // Sampler voice filters (SCE_NGS2_SAMPLER_VOICE_PARAM_FILTER) in cutoff/Q/level form, run as
    // biquads on the voice's resampled output. Coefficients are derived at the render rate.
    struct Filter {
        bool enabled = false;
        std::uint32_t type = 0;
        float cutoff = 0, q = 0, level = 1;
        std::uint32_t rate = 0;
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1[8]{}, z2[8]{};
    };
    std::array<Filter, 4> filters{};
    // Streaming resampler state: the last frame consumed (interpolation runs from it towards the
    // current frame, so no frame from a block not yet queued is needed) and advance owed to a block
    // that has not arrived yet.
    float history[NGS2_MAX_CHANNELS]{};
    std::uint64_t pendingFrames = 0;
    void ResetSetup();
    const std::uint8_t* WaveformData() const;
};

struct Ngs2Rack {
    Ngs2System* system = nullptr;
    std::uint32_t rackId = 0;
    std::uint32_t maxChannels = 0;
    Ngs2ContextBufferInfo bufferInfo{};
    Ngs2BufferAllocator allocator{};
    std::vector<Ngs2Voice> voices;
};

struct Ngs2System {
    Ngs2SystemOption option{};
    Ngs2ContextBufferInfo bufferInfo{};
    Ngs2BufferAllocator allocator{};
    std::uint32_t uid = 0;
    std::int64_t renderCount = 0;
    std::vector<Ngs2Rack*> racks;
};

std::string Ngs2Hex(std::uint32_t value);
std::recursive_mutex& Ngs2Mutex();
Ngs2System* Ngs2FindSystem(Ngs2Handle handle);
Ngs2Rack* Ngs2FindRack(Ngs2Handle handle);
Ngs2Voice* Ngs2FindVoice(Ngs2Handle handle);
void* Ngs2Place(const Ngs2ContextBufferInfo* bufferInfo, std::size_t size, std::size_t alignment);
int Ngs2DestroyRack(Ngs2Rack& rack, Ngs2ContextBufferInfo* outBufferInfo);
int Ngs2ReleaseBuffer(const Ngs2BufferAllocator& allocator, Ngs2ContextBufferInfo bufferInfo, Ngs2ContextBufferInfo* outBufferInfo);
void Ngs2SetupAtrac9(Ngs2Voice& voice, const Ngs2WaveformFormat& format);
std::size_t Ngs2Atrac9BlockBytes(const Ngs2Voice& voice, const Ngs2WaveformBlock& block);
void Ngs2RestartAtrac9(Ngs2Voice& voice);
const float* Ngs2Atrac9Frame(Ngs2Voice& voice, Ngs2Block& block, std::uint32_t frame);
// Debug aid (APS5_NGS2_DUMP): records a PCM block's contents again as it finishes playing.
void Ngs2DumpPlayedBlock(const Ngs2Voice& voice, const Ngs2Block& block);
void Ngs2RenderSystem(Ngs2System& system, const Ngs2RenderBufferInfo* bufferInfo, std::uint32_t numBufferInfo);

#endif
