// libSceNgs2 from AnyPS5 (github.com/boykopovar/AnyPS5, GPL-2.0; Adrià Franch, boykopovar),
// as patched for Quake (drakenabarion/AnyPS5). bbport: built into libbbgpu (ngs2_register.cpp).
#include <iterator>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "ngs2_general.h"
#include "Ngs2Internal.hpp"

static constexpr double PHASE_ONE = 4294967296.0;
static constexpr std::uint64_t PHASE_FRACTION = 0xffffffff;

static const std::uint8_t* FrameAt(const Ngs2Voice& voice, const Ngs2Block& block, std::uint32_t frame) {
    return block.data + (static_cast<std::size_t>(block.info.num_skip_samples) + frame) * voice.channels * sizeof(std::int16_t);
}

static void ReadPcm(const Ngs2Voice& voice, const std::uint8_t* frame, float* out) {
    for (std::uint32_t channel = 0; channel < voice.channels; channel++) {
        std::int16_t sample;
        std::memcpy(&sample, frame + channel * sizeof(sample), sizeof(sample));
        out[channel] = sample / 32768.0f;
    }
}

static void ReadFrame(Ngs2Voice& voice, float* out) {
    auto& block = voice.blocks.front();
    if (voice.waveformType == SCE_NGS2_WAVEFORM_TYPE_ATRAC9) {
        std::memcpy(out, Ngs2Atrac9Frame(voice, block, block.cursor), voice.channels * sizeof(float));
        return;
    }
    ReadPcm(voice, FrameAt(voice, block, block.cursor), out);
}

static void FinishBlock(Ngs2Voice& voice) {
    auto& block = voice.blocks.front();
    const bool repeat = block.info.num_repeats != 0;
    if (repeat) {
        if (block.info.num_repeats != UINT32_MAX) block.info.num_repeats--;
        block.numRepeated++;
        block.cursor = 0;
        if (voice.waveformType == SCE_NGS2_WAVEFORM_TYPE_ATRAC9) {
            block.dataCursor = 0;
            Ngs2RestartAtrac9(voice);
        }
    }
    const Ngs2VoiceCallbackInfo info{
        voice.callbackData,
        reinterpret_cast<Ngs2Handle>(&voice),
        repeat ? SCE_NGS2_VOICE_CALLBACK_FLAG_BLOCK_REPEAT : SCE_NGS2_VOICE_CALLBACK_FLAG_BLOCK_END,
        0,
        block.info.user_data,
        block.GuestAt(block.data),
        static_cast<std::uint32_t>(block.info.data_size),
        block.numRepeated,
        0,
        0,
    };
    static const bool dumpPlayed = std::getenv("APS5_NGS2_DUMP") != nullptr;
    if (dumpPlayed && !repeat) Ngs2DumpPlayedBlock(voice, block);
    if (!repeat) {
        voice.waveformEnd = voice.waveformType == SCE_NGS2_WAVEFORM_TYPE_ATRAC9 ? block.data + block.dataCursor : block.GuestAt(FrameAt(voice, block, block.info.num_samples));
        voice.blocks.pop_front();
    }
    if (voice.callback != nullptr && (voice.callbackFlags & info.flag) != 0) voice.callback(&info);
}

static void Advance(Ngs2Voice& voice, std::uint64_t frames) {
    while (frames != 0 && voice.state == Ngs2PlayState::Playing && !voice.blocks.empty()) {
        auto& block = voice.blocks.front();
        // The frame being left becomes the interpolation history.
        ReadFrame(voice, voice.history);
        const auto step = std::min<std::uint64_t>(1, block.info.num_samples - block.cursor);
        block.cursor += static_cast<std::uint32_t>(step);
        voice.decodedSamples += step;
        if (voice.waveformType != SCE_NGS2_WAVEFORM_TYPE_ATRAC9) voice.decodedBytes += step * voice.channels * sizeof(std::int16_t);
        frames -= step;
        if (block.cursor == block.info.num_samples) FinishBlock(voice);
    }
    // Advance past the queued data (a stream fed exactly one grain at a time) is owed to the next
    // block instead of being lost, so the stream keeps its position.
    if (frames != 0 && voice.state == Ngs2PlayState::Playing && voice.blocks.empty() && voice.acceptsBlocks) voice.pendingFrames += frames;
}

static void ConsumeSamples(Ngs2Voice& voice, std::uint32_t grain, std::uint32_t systemRate) {
    const auto step = static_cast<std::uint64_t>(std::llround(voice.sampleRate * static_cast<double>(voice.pitch) / systemRate * PHASE_ONE));
    std::uint32_t written = 0;
    float current[NGS2_MAX_CHANNELS];
    if (voice.pendingFrames != 0 && !voice.blocks.empty()) {
        const auto owed = voice.pendingFrames;
        voice.pendingFrames = 0;
        Advance(voice, owed);
    }
    for (; written < grain && voice.state == Ngs2PlayState::Playing; written++) {
        const bool starved = voice.blocks.empty();
        if (starved && !voice.acceptsBlocks) break;
        // Interpolate from the previous frame towards the current one; a starved stream holds its
        // last frame rather than dropping to silence.
        if (starved) std::copy_n(voice.history, voice.channels, current);
        else ReadFrame(voice, current);
        const auto fraction = static_cast<float>(static_cast<double>(voice.phase) / PHASE_ONE);
        for (std::uint32_t channel = 0; channel < voice.channels; channel++) {
            voice.samples[channel * grain + written] = std::lerp(voice.history[channel], current[channel], fraction);
        }
        voice.phase += step;
        if (starved) voice.pendingFrames += voice.phase >> 32;
        else Advance(voice, voice.phase >> 32);
        voice.phase &= PHASE_FRACTION;
    }
    voice.hasSamples = written != 0;
    for (auto& filter : voice.filters) {
        if (!filter.enabled || written == 0) continue;
        if (filter.rate != systemRate) {
            // RBJ audio-EQ-cookbook biquad.
            const double w0 = 2.0 * 3.14159265358979323846 * std::min<double>(filter.cutoff, systemRate * 0.49) / systemRate;
            const double alpha = std::sin(w0) / (2.0 * filter.q);
            const double c = std::cos(w0);
            const double a0 = 1.0 + alpha;
            const double b1 = filter.type == 0 ? (1.0 - c) : -(1.0 + c);
            const double b0 = filter.type == 0 ? (1.0 - c) / 2.0 : (1.0 + c) / 2.0;
            filter.b0 = static_cast<float>(b0 / a0);
            filter.b1 = static_cast<float>(b1 / a0);
            filter.b2 = static_cast<float>(b0 / a0);
            filter.a1 = static_cast<float>(-2.0 * c / a0);
            filter.a2 = static_cast<float>((1.0 - alpha) / a0);
            filter.rate = systemRate;
        }
        for (std::uint32_t channel = 0; channel < voice.channels && channel < 8; channel++) {
            float z1 = filter.z1[channel], z2 = filter.z2[channel];
            float* samples = &voice.samples[channel * grain];
            for (std::uint32_t i = 0; i < written; i++) {
                const float x = samples[i];
                const float y = filter.b0 * x + z1;
                z1 = filter.b1 * x - filter.a1 * y + z2;
                z2 = filter.b2 * x - filter.a2 * y;
                samples[i] = y * filter.level;
            }
            filter.z1[channel] = z1;
            filter.z2[channel] = z2;
        }
    }
    if (voice.state == Ngs2PlayState::Playing && voice.blocks.empty() && !voice.acceptsBlocks) voice.state = Ngs2PlayState::Empty;
}

static void RenderVoice(Ngs2Voice& voice, const std::vector<Ngs2Voice*>& voices, std::uint32_t grain, std::uint32_t systemRate);

static void MixPort(Ngs2Voice& voice, const Ngs2Voice& source, const Ngs2Port& port, std::uint32_t grain) {
    const auto* matrix = port.matrix < 0 ? nullptr : &source.matrices[port.matrix];
    const std::size_t outputs = matrix == nullptr ? voice.channels : std::min<std::size_t>(voice.channels, matrix->size() / source.channels);
    for (std::size_t dst = 0; dst < outputs; dst++) {
        for (std::uint32_t src = 0; src < source.channels; src++) {
            const float level = port.volume * (matrix == nullptr ? (src == dst ? 1.0f : 0.0f) : (*matrix)[dst * source.channels + src]);
            if (level == 0.0f) continue;
            for (std::uint32_t i = 0; i < grain; i++) voice.samples[dst * grain + i] += source.samples[src * grain + i] * level;
        }
    }
}

static void MixInputs(Ngs2Voice& voice, const std::vector<Ngs2Voice*>& voices, std::uint32_t grain, std::uint32_t systemRate) {
    for (auto* source : voices) {
        for (const auto& port : source->ports) {
            if (port.dest != &voice || port.volume == 0.0f) continue;
            RenderVoice(*source, voices, grain, systemRate);
            if (!source->hasSamples) continue;
            MixPort(voice, *source, port, grain);
            voice.hasSamples = true;
        }
    }
}

static void RenderVoice(Ngs2Voice& voice, const std::vector<Ngs2Voice*>& voices, std::uint32_t grain, std::uint32_t systemRate) {
    if (voice.rendered) return;
    if (voice.rendering) throw std::invalid_argument("NGS2: the voice patches form a cycle");
    voice.rendering = true;
    voice.samples.assign(static_cast<std::size_t>(grain) * voice.channels, 0.0f);
    voice.hasSamples = false;
    if (voice.state == Ngs2PlayState::Playing && voice.channels != 0) {
        if (voice.rack->rackId == SCE_NGS2_RACK_ID_SAMPLER) ConsumeSamples(voice, grain, systemRate);
        else MixInputs(voice, voices, grain, systemRate);
    }
    voice.rendering = false;
    voice.rendered = true;
}

static std::size_t SampleBytes(std::uint32_t waveformType) {
    if (waveformType == SCE_NGS2_WAVEFORM_TYPE_PCM_I16L) return sizeof(std::int16_t);
    if (waveformType == SCE_NGS2_WAVEFORM_TYPE_PCM_F32L) return sizeof(float);
    throw std::runtime_error("NGS2: render waveform type " + Ngs2Hex(waveformType) + " is not implemented");
}

static std::vector<float>& OutputMix(std::vector<std::vector<float>>& mixes, const Ngs2RenderBufferInfo* bufferInfo, std::uint32_t numBufferInfo,
                                     const Ngs2Voice& voice, std::uint32_t grain) {
    if (voice.outputId >= numBufferInfo) throw std::invalid_argument("NGS2: mastering output " + std::to_string(voice.outputId) + " has no render buffer");
    const auto& output = bufferInfo[voice.outputId];
    const auto sampleBytes = SampleBytes(output.waveform_type);
    if (output.num_channels != voice.channels) throw std::runtime_error("NGS2: mixing " + std::to_string(voice.channels) + " mastering channels into " + std::to_string(output.num_channels) + " is not implemented");
    if (output.buffer == nullptr || output.buffer_size < static_cast<std::size_t>(grain) * output.num_channels * sampleBytes) {
        throw std::invalid_argument("NGS2: render buffer " + std::to_string(voice.outputId) + " is missing or too small");
    }
    auto& mix = mixes[voice.outputId];
    if (mix.empty()) mix.resize(static_cast<std::size_t>(grain) * output.num_channels);
    return mix;
}

static void WriteOutput(const Ngs2RenderBufferInfo& output, const std::vector<float>& mix) {
    if (output.waveform_type == SCE_NGS2_WAVEFORM_TYPE_PCM_F32L) {
        std::memcpy(output.buffer, mix.data(), mix.size() * sizeof(float));
        return;
    }
    auto* pcm = static_cast<std::int16_t*>(output.buffer);
    for (std::size_t i = 0; i < mix.size(); i++) {
        pcm[i] = static_cast<std::int16_t>(std::min(std::lrint(std::clamp(mix[i], -1.0f, 1.0f) * 32768.0f), 32767L));
    }
}

void Ngs2RenderSystem(Ngs2System& system, const Ngs2RenderBufferInfo* bufferInfo, std::uint32_t numBufferInfo) {
    const auto grain = system.option.num_grain_samples;
    std::vector<Ngs2Voice*> voices;
    for (auto* rack : system.racks) {
        for (auto& voice : rack->voices) {
            voice.rendered = false;
            voices.push_back(&voice);
        }
    }
    std::vector<std::vector<float>> mixes(numBufferInfo);
    for (auto* voice : voices) {
        RenderVoice(*voice, voices, grain, system.option.sample_rate);
        if (voice->rack->rackId != SCE_NGS2_RACK_ID_MASTERING || !voice->hasSamples) continue;
        auto& mix = OutputMix(mixes, bufferInfo, numBufferInfo, *voice, grain);
        for (std::uint32_t channel = 0; channel < voice->channels; channel++) {
            for (std::uint32_t i = 0; i < grain; i++) mix[i * voice->channels + channel] += voice->samples[channel * grain + i];
        }
    }
    // Debug aid: APS5_NGS2_DUMP appends output 0's mix to /tmp/ngs2-out.raw (f32le, interleaved).
    static const bool dump = std::getenv("APS5_NGS2_DUMP") != nullptr;
    if (dump && numBufferInfo != 0 && !mixes[0].empty()) {
        static std::FILE* file = nullptr;
        if (file == nullptr) {
            file = std::fopen("/tmp/ngs2-out.raw", "wb");
            std::fprintf(stderr, "[ngs2] dumping output: f32le %u Hz %u ch -> /tmp/ngs2-out.raw\n", system.option.sample_rate, bufferInfo[0].num_channels);
        }
        if (file != nullptr) {
            std::fwrite(mixes[0].data(), sizeof(float), mixes[0].size(), file);
            std::fflush(file);
        }
    }
    for (std::uint32_t i = 0; i < numBufferInfo; i++) {
        if (bufferInfo[i].buffer != nullptr) std::memset(bufferInfo[i].buffer, 0, bufferInfo[i].buffer_size);
        if (!mixes[i].empty()) WriteOutput(bufferInfo[i], mixes[i]);
    }
    for (auto* voice : voices) {
        if (voice->state == Ngs2PlayState::Stopped) voice->state = Ngs2PlayState::Empty;
        voice->stateFlags = static_cast<std::uint32_t>(voice->state);
    }
    system.renderCount++;
}
