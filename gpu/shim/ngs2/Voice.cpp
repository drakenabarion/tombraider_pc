// libSceNgs2 from AnyPS5 (github.com/boykopovar/AnyPS5, GPL-2.0; Adrià Franch, boykopovar),
// as patched for Quake (drakenabarion/AnyPS5). bbport: built into libbbgpu (ngs2_register.cpp).
#include <vector>
#include <iterator>
#include <cstring>
#include <cstdlib>
#include <atomic>
#include <set>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>

#include "ngs2_general.h"
#include "Ngs2Internal.hpp"

static constexpr std::uint32_t COMMAND_EVENT = 2;
static constexpr std::uint32_t COMMAND_MATRIX_LEVELS = 5;
static constexpr std::uint32_t COMMAND_PORT_VOLUME = 6;
static constexpr std::uint32_t COMMAND_PORT_MATRIX = 7;
static constexpr std::uint8_t COMMAND_TYPE_FLOAT = 1;
static constexpr std::uint8_t COMMAND_TYPE_INT = 3;
static constexpr std::uint8_t COMMAND_TYPE_UINT = 4;
static constexpr std::uint8_t COMMAND_TYPE_FLOAT_ARRAY = 0x11;

void Ngs2Voice::SetEvent(std::uint32_t eventId) {
    switch (eventId) {
        case SCE_NGS2_VOICE_EVENT_PLAY:
            if (state == Ngs2PlayState::Empty || state == Ngs2PlayState::Stopped) {
                state = Ngs2PlayState::Playing;
                stateFlags |= SCE_NGS2_VOICE_STATE_FLAG_INUSE;
            }
            break;
        case SCE_NGS2_VOICE_EVENT_STOP:
            if (state == Ngs2PlayState::Playing || state == Ngs2PlayState::Paused) state = Ngs2PlayState::Stopped;
            break;
        case SCE_NGS2_VOICE_EVENT_STOP_IMM:
        case SCE_NGS2_VOICE_EVENT_KILL:
            state = Ngs2PlayState::Empty;
            break;
        case SCE_NGS2_VOICE_EVENT_PAUSE:
            if (state == Ngs2PlayState::Playing) state = Ngs2PlayState::Paused;
            break;
        case SCE_NGS2_VOICE_EVENT_RESUME:
            if (state == Ngs2PlayState::Paused) state = Ngs2PlayState::Playing;
            break;
        default: throw std::invalid_argument("NGS2: unknown voice event " + Ngs2Hex(eventId));
    }
}

void Ngs2Voice::ResetSetup() {
    SetEvent(SCE_NGS2_VOICE_EVENT_STOP_IMM);
    std::fill(ports.begin(), ports.end(), Ngs2Port{});
    for (auto& matrix : matrices) matrix.clear();
    channels = 0;
    sampleRate = 0;
    waveformType = 0;
    atrac9 = {};
    pitch = 1.0f;
    filters = {};
    std::fill(std::begin(history), std::end(history), 0.0f);
    pendingFrames = 0;
    phase = 0;
    blocks.clear();
    acceptsBlocks = true;
    decodedSamples = 0;
    decodedBytes = 0;
    waveformEnd = nullptr;
}

const std::uint8_t* Ngs2Voice::WaveformData() const {
    if (blocks.empty()) return waveformEnd;
    const auto& block = blocks.front();
    if (waveformType == SCE_NGS2_WAVEFORM_TYPE_ATRAC9) return block.data + block.dataCursor;
    return block.GuestAt(block.data + (static_cast<std::size_t>(block.info.num_skip_samples) + block.cursor) * channels * sizeof(std::int16_t));
}

template <typename TParam>
static const TParam& ParamAs(const Ngs2VoiceParamHeader& param) {
    if (param.size < sizeof(TParam)) throw std::invalid_argument("NGS2: voice param " + Ngs2Hex(param.id) + " is too small");
    return reinterpret_cast<const TParam&>(param);
}

static Ngs2Port& PortAt(Ngs2Voice& voice, std::uint32_t port) {
    if (port >= voice.ports.size()) throw std::invalid_argument("NGS2: voice port " + std::to_string(port) + " is out of range");
    return voice.ports[port];
}

static void SetMatrixLevels(Ngs2Voice& voice, std::uint32_t matrixId, const float* levels, std::uint32_t numLevels) {
    if (matrixId >= voice.matrices.size() || numLevels > NGS2_MAX_CHANNELS * NGS2_MAX_CHANNELS || (levels == nullptr && numLevels != 0)) {
        APS5_INVALID_ARG_EX;
    }
    voice.matrices[matrixId].assign(levels, levels + numLevels);
}

static void SetPortMatrix(Ngs2Voice& voice, std::uint32_t port, std::int32_t matrixId) {
    if (matrixId < -1 || matrixId >= static_cast<std::int32_t>(voice.matrices.size())) APS5_INVALID_ARG_EX;
    PortAt(voice, port).matrix = matrixId;
}

static void Patch(Ngs2Voice& voice, const Ngs2VoicePatchParam& patch) {
    auto& port = PortAt(voice, patch.port);
    if (patch.dest_input_id != 0) throw std::runtime_error("NGS2: voice input " + std::to_string(patch.dest_input_id) + " is not implemented");
    if (patch.dest_handle == 0) {
        port.dest = nullptr;
        return;
    }
    auto* dest = Ngs2FindVoice(patch.dest_handle);
    if (dest == nullptr || dest->rack->system != voice.rack->system || dest->rack->rackId == SCE_NGS2_RACK_ID_SAMPLER) APS5_INVALID_ARG_EX;
    port.dest = dest;
}

static void ApplyCommonParam(Ngs2Voice& voice, const Ngs2VoiceParamHeader& param) {
    switch (param.id) {
        case SCE_NGS2_VOICE_PARAM_MATRIX_LEVELS: {
            const auto& levels = ParamAs<Ngs2VoiceMatrixLevelsParam>(param);
            SetMatrixLevels(voice, levels.matrix_id, levels.levels, levels.num_levels);
            break;
        }
        case SCE_NGS2_VOICE_PARAM_PORT_VOLUME: {
            const auto& volume = ParamAs<Ngs2VoicePortVolumeParam>(param);
            PortAt(voice, volume.port).volume = volume.level;
            break;
        }
        case SCE_NGS2_VOICE_PARAM_PORT_MATRIX: {
            const auto& matrix = ParamAs<Ngs2VoicePortMatrixParam>(param);
            SetPortMatrix(voice, matrix.port, matrix.matrix_id);
            break;
        }
        case SCE_NGS2_VOICE_PARAM_PORT_DELAY: {
            const auto& delay = ParamAs<Ngs2VoicePortDelayParam>(param);
            PortAt(voice, delay.port);
            if (delay.num_samples != 0) throw std::runtime_error("NGS2: port delay is not implemented");
            break;
        }
        case SCE_NGS2_VOICE_PARAM_PATCH: Patch(voice, ParamAs<Ngs2VoicePatchParam>(param)); break;
        case SCE_NGS2_VOICE_PARAM_EVENT: voice.SetEvent(ParamAs<Ngs2VoiceEventParam>(param).event_id); break;
        case SCE_NGS2_VOICE_PARAM_CALLBACK: {
            const auto& callback = ParamAs<Ngs2VoiceCallbackParam>(param);
            voice.callback = callback.callback;
            voice.callbackData = callback.callback_data;
            voice.callbackFlags = callback.flags;
            break;
        }
        default: throw std::runtime_error("NGS2: voice param " + Ngs2Hex(param.id) + " is not implemented");
    }
}

// Debug aid: APS5_TRACE_NGS2 logs the first sampler setups, block lists, pitch changes and the raw
// words of ignored parameters, capped so a play session stays readable.
static bool Ngs2TraceLine() {
    static const bool enabled = std::getenv("APS5_TRACE_NGS2") != nullptr;
    static std::atomic<int> lines{0};
    return enabled && lines.fetch_add(1) < 400;
}

static void SetupSampler(Ngs2Voice& voice, const Ngs2WaveformFormat& format) {
    voice.ResetSetup();
    if (format.waveform_type == 0 && format.num_channels == 0 && format.sample_rate == 0) return;
    if (format.waveform_type != SCE_NGS2_WAVEFORM_TYPE_PCM_I16L && format.waveform_type != SCE_NGS2_WAVEFORM_TYPE_ATRAC9) {
        throw std::runtime_error("NGS2: waveform type " + Ngs2Hex(format.waveform_type) + " is not implemented");
    }
    if (format.frame_offset != 0 || format.frame_margin != 0) throw std::runtime_error("NGS2: waveform frame offset and margin are not implemented");
    if (format.num_channels == 0 || format.num_channels > NGS2_MAX_CHANNELS || format.sample_rate == 0) APS5_INVALID_ARG_EX;
    voice.channels = format.num_channels;
    voice.sampleRate = format.sample_rate;
    if (Ngs2TraceLine()) std::fprintf(stderr, "[ngs2] voice %p setup type 0x%x channels %u rate %u config 0x%x\n", static_cast<void*>(&voice), format.waveform_type, format.num_channels, format.sample_rate, format.config_data);
    voice.waveformType = format.waveform_type;
    if (format.waveform_type == SCE_NGS2_WAVEFORM_TYPE_ATRAC9) Ngs2SetupAtrac9(voice, format);
}

struct DumpedVoice { const void* voice; std::FILE* file; std::FILE* played; std::uint64_t blocks, frames, repeating; std::uint32_t minSize, maxSize, flags; };
static std::vector<DumpedVoice> dumped;

void Ngs2DumpPlayedBlock(const Ngs2Voice& voice, const Ngs2Block& block) {
    if (voice.waveformType != SCE_NGS2_WAVEFORM_TYPE_PCM_I16L) return;
    const auto it = std::find_if(dumped.begin(), dumped.end(), [&](const DumpedVoice& d) { return d.voice == &voice; });
    if (it == dumped.end() || it->played == nullptr) return;
    const std::size_t frameBytes = voice.channels * sizeof(std::int16_t);
    std::fwrite(block.data + static_cast<std::size_t>(block.info.num_skip_samples) * frameBytes, frameBytes, block.info.num_samples, it->played);
    std::fflush(it->played);
}

static void AddWaveformBlocks(Ngs2Voice& voice, const Ngs2SamplerVoiceWaveformBlocksParam& param) {
    constexpr std::uint32_t knownFlags = SCE_NGS2_WAVEFORM_BLOCKS_FLAG_CONTINUE | SCE_NGS2_WAVEFORM_BLOCKS_FLAG_RESET;
    // bbport: Virtual-On's menu (PS4) also sends flags 0x10 and 0x8, whose meaning is not known
    // yet: logged once per value and otherwise ignored instead of refusing the blocks.
    if ((param.flags & ~knownFlags) != 0) {
        static std::uint32_t reported[8];
        static int count;
        bool seen = false;
        for (int i = 0; i < count; i++) seen = seen || reported[i] == param.flags;
        if (!seen && count < 8) {
            reported[count++] = param.flags;
            std::fprintf(stderr, "NGS2: waveform block flags 0x%x: bits 0x%x not implemented (ignored)\n", param.flags,
                         param.flags & ~knownFlags);
        }
    }
    if (voice.channels == 0 || (param.num_blocks != 0 && (param.blocks == nullptr || param.data == nullptr))) APS5_INVALID_ARG_EX;
    const bool reset = (param.flags & SCE_NGS2_WAVEFORM_BLOCKS_FLAG_RESET) != 0;
    if (!voice.acceptsBlocks && !reset) throw std::invalid_argument("NGS2: the voice waveform was already closed");
    if (reset) {
        voice.blocks.clear();
        voice.phase = 0;
        voice.pendingFrames = 0;
        std::fill(std::begin(voice.history), std::end(voice.history), 0.0f);
        voice.waveformEnd = nullptr;
        if (voice.waveformType == SCE_NGS2_WAVEFORM_TYPE_ATRAC9) Ngs2RestartAtrac9(voice);
    }
    voice.acceptsBlocks = (param.flags & SCE_NGS2_WAVEFORM_BLOCKS_FLAG_CONTINUE) != 0;
    if (Ngs2TraceLine()) {
        std::fprintf(stderr, "[ngs2] voice %p blocks %u flags 0x%x (type 0x%x rate %u ch %u):", static_cast<const void*>(&voice), param.num_blocks, param.flags, voice.waveformType, voice.sampleRate, voice.channels);
        for (std::uint32_t i = 0; i < param.num_blocks && i < 4; i++) {
            const auto& b = param.blocks[i];
            std::fprintf(stderr, " [off 0x%llx size 0x%llx rep %u skip %u samples %u]", static_cast<unsigned long long>(b.data_offset), static_cast<unsigned long long>(b.data_size), b.num_repeats, b.num_skip_samples, b.num_samples);
        }
        std::fprintf(stderr, "\n");
    }
    const std::size_t frameBytes = voice.channels * sizeof(std::int16_t);
    for (std::uint32_t i = 0; i < param.num_blocks; i++) {
        auto block = param.blocks[i];
        if (block.num_samples == 0 && block.data_size == 0) continue;
        // bbport: a PCM block with no sample count plays its whole data (Virtual-On's menu streams
        // the SEGA logo's sound this way: 4 KiB ring blocks with flags 0x11, refilled at block end).
        if (block.num_samples == 0 && voice.waveformType == SCE_NGS2_WAVEFORM_TYPE_PCM_I16L) {
            const std::uint64_t frames = block.data_size / frameBytes;
            block.num_samples = frames > block.num_skip_samples ? static_cast<std::uint32_t>(frames - block.num_skip_samples) : 0;
        }
        const std::uint64_t bytes = voice.waveformType == SCE_NGS2_WAVEFORM_TYPE_ATRAC9 ? Ngs2Atrac9BlockBytes(voice, block)
                                  : (static_cast<std::uint64_t>(block.num_skip_samples) + block.num_samples) * frameBytes;
        if (block.num_samples == 0 || bytes > block.data_size) {
            throw std::invalid_argument("NGS2: waveform block " + std::to_string(i) + " does not fit its data");
        }
        const auto* source = static_cast<const std::uint8_t*>(param.data) + block.data_offset;
        Ngs2Block queued{source, block};
        if (voice.waveformType == SCE_NGS2_WAVEFORM_TYPE_PCM_I16L) {
            auto copy = std::make_shared<std::vector<std::uint8_t>>(source, source + bytes);
            queued.guest = source;
            queued.data = copy->data();
            queued.snapshot = std::move(copy);
        }
        voice.blocks.push_back(std::move(queued));
        // Debug aid: APS5_NGS2_DUMP appends each PCM sampler voice's blocks to /tmp/ngs2-in-<n>.raw
        // (s16le at the voice's rate and channels) and logs every voice's block pattern periodically.
        static const bool dump = std::getenv("APS5_NGS2_DUMP") != nullptr;
        if (dump && voice.waveformType == SCE_NGS2_WAVEFORM_TYPE_PCM_I16L) {
            auto it = std::find_if(dumped.begin(), dumped.end(), [&](const DumpedVoice& d) { return d.voice == &voice; });
            if (it == dumped.end() && dumped.size() < 4) {
                const auto path = "/tmp/ngs2-in-" + std::to_string(dumped.size()) + ".raw";
                const auto played = "/tmp/ngs2-played-" + std::to_string(dumped.size()) + ".raw";
                dumped.push_back({&voice, std::fopen(path.c_str(), "wb"), std::fopen(played.c_str(), "wb"), 0, 0, 0, UINT32_MAX, 0, 0});
                std::fprintf(stderr, "[ngs2] dumping voice %zu: s16le %u Hz %u ch -> %s\n", dumped.size() - 1, voice.sampleRate, voice.channels, path.c_str());
                it = dumped.end() - 1;
            }
            if (it != dumped.end()) {
                if (it->file != nullptr) {
                    std::fwrite(static_cast<const std::uint8_t*>(param.data) + block.data_offset + static_cast<std::size_t>(block.num_skip_samples) * frameBytes, frameBytes, block.num_samples, it->file);
                    std::fflush(it->file);
                }
                it->blocks++;
                it->frames += block.num_samples;
                if (block.num_repeats != 0) it->repeating++;
                it->minSize = std::min(it->minSize, block.num_samples);
                it->maxSize = std::max(it->maxSize, block.num_samples);
                it->flags |= param.flags;
                if (it->blocks % 400 == 1) std::fprintf(stderr, "[ngs2] voice %zu: %llu blocks, %llu frames, sizes %u-%u, repeating %llu, flags seen 0x%x, queued now %zu, last skip %u repeats %u\n", static_cast<std::size_t>(it - dumped.begin()), static_cast<unsigned long long>(it->blocks), static_cast<unsigned long long>(it->frames), it->minSize, it->maxSize, static_cast<unsigned long long>(it->repeating), it->flags, voice.blocks.size(), block.num_skip_samples, block.num_repeats);
            }
        }
    }
}

static void ApplySamplerParam(Ngs2Voice& voice, const Ngs2VoiceParamHeader& param) {
    switch (param.id) {
        case SCE_NGS2_SAMPLER_VOICE_PARAM_SETUP: SetupSampler(voice, ParamAs<Ngs2SamplerVoiceSetupParam>(param).format); break;
        case SCE_NGS2_SAMPLER_VOICE_PARAM_ADD_WAVEFORM_BLOCKS: AddWaveformBlocks(voice, ParamAs<Ngs2SamplerVoiceWaveformBlocksParam>(param)); break;
        case SCE_NGS2_SAMPLER_VOICE_PARAM_EXIT_LOOP: {
            const auto looping = std::find_if(voice.blocks.begin(), voice.blocks.end(), [](const Ngs2Block& block) { return block.info.num_repeats != 0; });
            if (looping != voice.blocks.end()) looping->info.num_repeats = 0;
            break;
        }
        case SCE_NGS2_SAMPLER_VOICE_PARAM_PITCH: {
            const float ratio = ParamAs<Ngs2SamplerVoicePitchParam>(param).ratio;
            if (!std::isfinite(ratio) || ratio < 0.0f) APS5_INVALID_ARG_EX;
            if (voice.pitch != ratio && Ngs2TraceLine()) std::fprintf(stderr, "[ngs2] voice %p pitch %g (rate %u)\n", static_cast<void*>(&voice), ratio, voice.sampleRate);
            voice.pitch = ratio;
            break;
        }
        // Sound-shaping parameters (envelope 0x6, distortion 0x7, user effect 0x8, peak meter 0x9,
        // filter 0xa, filter count 0xb): accepted and not applied, so voices play unshaped instead of
        // failing. Parameters that change what plays (waveform address 0x2, frame offset 0x3) stay strict.
        case 0x1000000a: {
            // SCE_NGS2_SAMPLER_VOICE_PARAM_FILTER: index, location, type, channel mask, then a form word;
            // form 1 carries cutoff (Hz), Q and level. Types 0 (low-pass) and 1 (high-pass) are run as
            // biquads; other types and forms are not implemented.
            const auto* words = reinterpret_cast<const std::uint32_t*>(&param);
            if (param.size >= 40) {
                const auto index = words[2];
                const auto type = words[4];
                const auto form = words[6];
                float cutoff, q, level;
                std::memcpy(&cutoff, &words[7], 4);
                std::memcpy(&q, &words[8], 4);
                std::memcpy(&level, &words[9], 4);
                if (index < voice.filters.size()) {
                    auto& filter = voice.filters[index];
                    if (form != 1 || (type != 0 && type != 1)) throw std::runtime_error("NGS2: sampler voice filter type " + std::to_string(type) + " form " + std::to_string(form) + " is not implemented");
                    const bool usable = std::isfinite(cutoff) && cutoff > 0 && std::isfinite(q) && q > 0 && std::isfinite(level);
                    if (usable && (!filter.enabled || filter.type != type || filter.cutoff != cutoff || filter.q != q)) filter.rate = 0;
                    filter.enabled = usable;
                    filter.type = type;
                    filter.cutoff = cutoff;
                    filter.q = q;
                    filter.level = level;
                }
            }
            if (Ngs2TraceLine()) {
                std::fprintf(stderr, "[ngs2] param %s words:", Ngs2Hex(param.id).c_str());
                for (std::size_t w = 0; w < 12 && w * 4 < param.size; w++) std::fprintf(stderr, " %08x", words[w]);
                std::fprintf(stderr, "\n");
            }
            break;
        }
        default: throw std::runtime_error("NGS2: sampler voice param " + Ngs2Hex(param.id) + " is not implemented");
    }
}

static void SetupMixer(Ngs2Voice& voice, std::uint32_t numChannels) {
    if (numChannels == 0 || numChannels > voice.rack->maxChannels) APS5_INVALID_ARG_EX;
    voice.ResetSetup();
    voice.channels = numChannels;
}

static void ApplyParam(Ngs2Voice& voice, const Ngs2VoiceParamHeader& param) {
    const std::uint32_t rackId = param.id >> 16;
    if (rackId == 0) {
        ApplyCommonParam(voice, param);
        return;
    }
    if (rackId != voice.rack->rackId) throw std::invalid_argument("NGS2: voice param " + Ngs2Hex(param.id) + " belongs to another rack");
    switch (rackId) {
        case SCE_NGS2_RACK_ID_SAMPLER: ApplySamplerParam(voice, param); return;
        case SCE_NGS2_RACK_ID_SUBMIXER:
            if (param.id != SCE_NGS2_SUBMIXER_VOICE_PARAM_SETUP) break;
            if (ParamAs<Ngs2SubmixerVoiceSetupParam>(param).flags != 0) throw std::runtime_error("NGS2: submixer setup flags are not implemented");
            SetupMixer(voice, ParamAs<Ngs2SubmixerVoiceSetupParam>(param).num_io_channels);
            return;
        case SCE_NGS2_RACK_ID_MASTERING:
            if (param.id == SCE_NGS2_MASTERING_VOICE_PARAM_SETUP) {
                SetupMixer(voice, ParamAs<Ngs2MasteringVoiceSetupParam>(param).num_io_channels);
                return;
            }
            if (param.id == SCE_NGS2_MASTERING_VOICE_PARAM_OUTPUT) {
                voice.outputId = ParamAs<Ngs2MasteringVoiceOutputParam>(param).output_id;
                return;
            }
            break;
        default: break;
    }
    throw std::runtime_error("NGS2: voice param " + Ngs2Hex(param.id) + " is not implemented");
}

static Ngs2Voice& CheckedVoice(Ngs2Handle handle) {
    auto* voice = Ngs2FindVoice(handle);
    if (voice == nullptr) throw std::invalid_argument("NGS2: invalid voice handle");
    return *voice;
}

#pragma GCC visibility push(default)

extern "C" {

int APS5_VABI sceNgs2VoiceControl(uintptr_t voice_handle, const Ngs2VoiceParamHeader* param_list) {
    std::lock_guard lock(Ngs2Mutex());
    auto& voice = CheckedVoice(voice_handle);
    if (param_list == nullptr) APS5_INVALID_ARG_EX;
    for (const auto* param = param_list;; param = reinterpret_cast<const Ngs2VoiceParamHeader*>(reinterpret_cast<const std::uint8_t*>(param) + param->next)) {
        ApplyParam(voice, *param);
        if (param->next == 0) break;
    }
    return SCE_NGS2_OK;
}

int APS5_VABI sceNgs2VoiceRunCommands(uintptr_t voice_handle, const Ngs2VoiceCommand* commands, size_t num_commands) {
    std::lock_guard lock(Ngs2Mutex());
    auto& voice = CheckedVoice(voice_handle);
    if (commands == nullptr && num_commands != 0) APS5_INVALID_ARG_EX;
    for (std::size_t i = 0; i < num_commands; i++) {
        const auto& command = commands[i];
        const std::uint32_t index = command.id >> 24;
        const std::uint32_t id = command.id & 0xffffff;
        const std::uint8_t expectedType = id == COMMAND_EVENT ? COMMAND_TYPE_UINT
                                        : id == COMMAND_MATRIX_LEVELS ? COMMAND_TYPE_FLOAT_ARRAY
                                        : id == COMMAND_PORT_VOLUME ? COMMAND_TYPE_FLOAT
                                        : id == COMMAND_PORT_MATRIX ? COMMAND_TYPE_INT : 0;
        if (expectedType == 0) throw std::runtime_error("NGS2: voice command " + Ngs2Hex(command.id) + " is not implemented");
        if (command.type != expectedType) throw std::invalid_argument("NGS2: voice command " + Ngs2Hex(command.id) + " has value type " + Ngs2Hex(command.type));
        if (id == COMMAND_EVENT) voice.SetEvent(command.value.u);
        else if (id == COMMAND_MATRIX_LEVELS) SetMatrixLevels(voice, index, command.value.levels, command.count);
        else if (id == COMMAND_PORT_VOLUME) PortAt(voice, index).volume = command.value.f;
        else SetPortMatrix(voice, index, command.value.i);
    }
    return SCE_NGS2_OK;
}

int APS5_VABI sceNgs2VoiceGetState(uintptr_t voice_handle, Ngs2VoiceState* state, size_t state_size) {
    if (state == nullptr) return SCE_NGS2_ERROR_INVALID_OUT_ADDRESS;
    std::lock_guard lock(Ngs2Mutex());
    const auto& voice = CheckedVoice(voice_handle);
    switch (voice.rack->rackId) {
        case SCE_NGS2_RACK_ID_SAMPLER: {
            if (state_size != sizeof(Ngs2SamplerVoiceState)) return SCE_NGS2_ERROR_INVALID_OUT_SIZE;
            auto& sampler = *reinterpret_cast<Ngs2SamplerVoiceState*>(state);
            sampler = {};
            sampler.voice_state.state_flags = voice.stateFlags;
            sampler.envelope_height = 1.0f;
            sampler.num_decoded_samples = voice.decodedSamples;
            sampler.decoded_data_size = voice.decodedBytes;
            sampler.user_data = voice.blocks.empty() ? 0 : voice.blocks.front().info.user_data;
            sampler.waveform_data = voice.WaveformData();
            return SCE_NGS2_OK;
        }
        case SCE_NGS2_RACK_ID_SUBMIXER: {
            if (state_size != sizeof(Ngs2SubmixerVoiceState)) return SCE_NGS2_ERROR_INVALID_OUT_SIZE;
            auto& submixer = *reinterpret_cast<Ngs2SubmixerVoiceState*>(state);
            submixer = {};
            submixer.voice_state.state_flags = voice.stateFlags;
            return SCE_NGS2_OK;
        }
        default: throw std::runtime_error("NGS2: voice state of rack " + Ngs2Hex(voice.rack->rackId) + " is not implemented");
    }
}

int APS5_VABI sceNgs2VoiceGetStateFlags(uintptr_t voice_handle, uint32_t* state_flags) {
    if (state_flags == nullptr) return SCE_NGS2_ERROR_INVALID_OUT_ADDRESS;
    std::lock_guard lock(Ngs2Mutex());
    *state_flags = CheckedVoice(voice_handle).stateFlags;
    return SCE_NGS2_OK;
}

}

#pragma GCC visibility pop
