// bbport: registers AnyPS5's libSceNgs2 (the files beside this one) with the loader. Each export
// goes through Safe<>: the library reports bad arguments and unimplemented features with C++
// exceptions, which must not unwind into guest code; they become an NGS2 error code and a log
// line instead.
#include <cstdio>
#include <exception>
#include "Ngs2Types.hpp"
#include "core/libraries/libs.h"

extern "C" {
int APS5_VABI sceNgs2ParseWaveformData(const void* data, size_t data_size, Ngs2WaveformInfo* info);
int APS5_VABI sceNgs2CalcWaveformBlock(const Ngs2WaveformFormat* format, uint32_t sample_pos, uint32_t num_samples, Ngs2WaveformBlock* block);
int APS5_VABI sceNgs2PanInit(Ngs2PanWork* work, const float* speaker_angles, float unit_angle, uint32_t num_speakers);
int APS5_VABI sceNgs2PanGetVolumeMatrix(Ngs2PanWork* work, const Ngs2PanParam* params, uint32_t num_params, uint32_t matrix_format, float* out_volume_matrix);
int APS5_VABI sceNgs2RackQueryBufferSize(uint32_t rack_id, const Ngs2RackOption* option, Ngs2ContextBufferInfo* buffer_info);
int APS5_VABI sceNgs2RackCreate(uintptr_t system_handle, uint32_t rack_id, const Ngs2RackOption* option, const Ngs2ContextBufferInfo* buffer_info, uintptr_t* handle);
int APS5_VABI sceNgs2RackCreateWithAllocator(uintptr_t system_handle, uint32_t rack_id, const Ngs2RackOption* option, const Ngs2BufferAllocator* allocator, uintptr_t* handle);
int APS5_VABI sceNgs2RackDestroy(uintptr_t rack_handle, Ngs2ContextBufferInfo* buffer_info);
int APS5_VABI sceNgs2RackGetVoiceHandle(uintptr_t rack_handle, uint32_t voice_id, uintptr_t* handle);
int APS5_VABI sceNgs2RackLock(uintptr_t rack_handle);
int APS5_VABI sceNgs2RackUnlock(uintptr_t rack_handle);
int APS5_VABI sceNgs2SystemQueryBufferSize(const Ngs2SystemOption* option, Ngs2ContextBufferInfo* buffer_info);
int APS5_VABI sceNgs2SystemCreate(const Ngs2SystemOption* option, const Ngs2ContextBufferInfo* buffer_info, uintptr_t* handle);
int APS5_VABI sceNgs2SystemCreateWithAllocator(const Ngs2SystemOption* option, const Ngs2BufferAllocator* allocator, uintptr_t* handle);
int APS5_VABI sceNgs2SystemDestroy(uintptr_t system_handle, Ngs2ContextBufferInfo* buffer_info);
int APS5_VABI sceNgs2SystemGetInfo(uintptr_t system_handle, Ngs2SystemInfo* info, size_t info_size);
int APS5_VABI sceNgs2SystemSetGrainSamples(uintptr_t system_handle, uint32_t num_samples);
int APS5_VABI sceNgs2SystemRender(uintptr_t system_handle, const Ngs2RenderBufferInfo* buffer_info, uint32_t num_buffer_info);
int APS5_VABI sceNgs2SystemResetOption(Ngs2SystemOption* option);
int APS5_VABI sceNgs2GeomApply(const Ngs2GeomListenerWork* listener, const Ngs2GeomSourceParam* source, Ngs2GeomAttribute* out_attrib, uint32_t flags);
int APS5_VABI sceNgs2GeomCalcListener(const Ngs2GeomListenerParam* param, Ngs2GeomListenerWork* out_work, uint32_t flags);
int APS5_VABI sceNgs2GeomResetListenerParam(Ngs2GeomListenerParam* out_listener_param);
int APS5_VABI sceNgs2GeomResetSourceParam(Ngs2GeomSourceParam* out_source_param);
int APS5_VABI sceNgs2VoiceControl(uintptr_t voice_handle, const Ngs2VoiceParamHeader* param_list);
int APS5_VABI sceNgs2VoiceRunCommands(uintptr_t voice_handle, const Ngs2VoiceCommand* commands, size_t num_commands);
int APS5_VABI sceNgs2VoiceGetState(uintptr_t voice_handle, Ngs2VoiceState* state, size_t state_size);
int APS5_VABI sceNgs2VoiceGetStateFlags(uintptr_t voice_handle, uint32_t* state_flags);

void NotImplemented_nid_no_patch(const char* funcName) {
    std::fprintf(stderr, "NGS2: %s is not implemented (returns success)\n", funcName);
}
}

namespace {
// Not a documented code: what the library returns when it gave up on a call (an exception).
constexpr int NGS2_ERROR_FAILED = static_cast<int>(0x804A8001);

template <auto Fn>
struct Safe;
template <typename... Args, int (APS5_VABI* Fn)(Args...)>
struct Safe<Fn> {
    static int APS5_VABI Call(Args... args) {
        try {
            return Fn(args...);
        } catch (const std::exception& error) {
            static int reported;
            if (reported < 32) {
                ++reported;
                std::fprintf(stderr, "NGS2: %s\n", error.what());
            }
            return NGS2_ERROR_FAILED;
        }
    }
};
} // namespace

namespace Libraries::Ngs2 {
void RegisterLib(Core::Loader::SymbolsResolver* sym) {
#define NGS2(nid, fn) LIB_FUNCTION(nid, "libSceNgs2", 1, "libSceNgs2", Safe<fn>::Call)
    NGS2("hyVLT2VlOYk", sceNgs2ParseWaveformData);
    NGS2("3pCNbVM11UA", sceNgs2CalcWaveformBlock);
    NGS2("xa8oL9dmXkM", sceNgs2PanInit);
    NGS2("gbMKV+8Enuo", sceNgs2PanGetVolumeMatrix);
    NGS2("0eFLVCfWVds", sceNgs2RackQueryBufferSize);
    NGS2("cLV4aiT9JpA", sceNgs2RackCreate);
    NGS2("U546k6orxQo", sceNgs2RackCreateWithAllocator);
    NGS2("lCqD7oycmIM", sceNgs2RackDestroy);
    NGS2("MwmHz8pAdAo", sceNgs2RackGetVoiceHandle);
    NGS2("MzTa7VLjogY", sceNgs2RackLock);
    NGS2("++YZ7P9e87U", sceNgs2RackUnlock);
    NGS2("pgFAiLR5qT4", sceNgs2SystemQueryBufferSize);
    NGS2("koBbCMvOKWw", sceNgs2SystemCreate);
    NGS2("mPYgU4oYpuY", sceNgs2SystemCreateWithAllocator);
    NGS2("u-WrYDaJA3k", sceNgs2SystemDestroy);
    NGS2("vU7TQ62pItw", sceNgs2SystemGetInfo);
    NGS2("l4Q2dWEH6UM", sceNgs2SystemSetGrainSamples);
    NGS2("i0VnXM-C9fc", sceNgs2SystemRender);
    NGS2("AQkj7C0f3PY", sceNgs2SystemResetOption);
    NGS2("eF8yRCC6W64", sceNgs2GeomApply);
    NGS2("1WsleK-MTkE", sceNgs2GeomCalcListener);
    NGS2("7Lcfo8SmpsU", sceNgs2GeomResetListenerParam);
    NGS2("0lbbayqDNoE", sceNgs2GeomResetSourceParam);
    NGS2("uu94irFOGpA", sceNgs2VoiceControl);
    NGS2("AbYvTOZ8Pts", sceNgs2VoiceRunCommands);
    NGS2("-TOuuAQ-buE", sceNgs2VoiceGetState);
    NGS2("rEh728kXk3w", sceNgs2VoiceGetStateFlags);
#undef NGS2
}
} // namespace Libraries::Ngs2
