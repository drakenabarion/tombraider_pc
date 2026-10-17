/* libSceNgs2 (sample-based audio, the collection menu's sounds): silent for now. Handles are
 * real, voices report themselves idle and rendering fills the game's buffers with silence.
 * shadPS4 stubs the same calls and the menu runs on it. */
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NGS2_INVALID_SYSTEM_HANDLE ((int32_t)0x804A0230)
#define NGS2_INVALID_RACK_HANDLE ((int32_t)0x804A0260)
#define NGS2_INVALID_OUT_ADDRESS ((int32_t)0x804A0053)
typedef struct { uint32_t magic, id, flags; } Handle; /* system, rack or voice (flags: voices) */
enum { SYSTEM = 0x4e475331, RACK = 0x4e475352, VOICE = 0x4e475356 };
typedef struct { void *buffer; size_t size; uint32_t waveform, channels; } RenderBuffer;
typedef struct { Handle handle; Handle voices[256]; } Rack;
static size_t systems, racks, renders;
static Handle *make(uint32_t magic) {
    Handle *h = calloc(1, sizeof(Handle));
    if (h) h->magic = magic;
    return h;
}
static ABI int32_t system_create(const void *option, const void *allocator, Handle **out) {
    (void)option; (void)allocator;
    if (!out) return NGS2_INVALID_OUT_ADDRESS;
    if (!(*out = make(SYSTEM))) return (int32_t)0x804A0050;
    if (!systems++) printf("Runtime: NGS2 system created (silent: menu audio is not implemented yet)\n");
    return 0;
}
static ABI int32_t system_destroy(Handle *system, void *info) {
    if (info) memset(info, 0, 0x30);
    if (!system || system->magic != SYSTEM) return NGS2_INVALID_SYSTEM_HANDLE;
    system->magic = 0;
    free(system);
    return 0;
}
static ABI int32_t system_render(Handle *system, const RenderBuffer *buffers, uint32_t count) {
    if (!system || system->magic != SYSTEM) return NGS2_INVALID_SYSTEM_HANDLE;
    for (uint32_t i = 0; buffers && i < count; ++i)
        if (buffers[i].buffer) memset(buffers[i].buffer, 0, buffers[i].size);
    ++renders;
    return 0;
}
static ABI int32_t rack_create(Handle *system, uint32_t rack_id, const void *option, const void *allocator, Handle **out) {
    (void)rack_id; (void)option; (void)allocator;
    if (!system || system->magic != SYSTEM) return NGS2_INVALID_SYSTEM_HANDLE;
    if (!out) return NGS2_INVALID_OUT_ADDRESS;
    Rack *r = calloc(1, sizeof(Rack));
    if (!r) return (int32_t)0x804A0050;
    r->handle.magic = RACK;
    for (uint32_t i = 0; i < 256; ++i) { r->voices[i].magic = VOICE; r->voices[i].id = i; }
    *out = &r->handle;
    ++racks;
    return 0;
}
static ABI int32_t rack_destroy(Handle *rack, void *info) {
    if (info) memset(info, 0, 0x30);
    if (!rack || rack->magic != RACK) return NGS2_INVALID_RACK_HANDLE;
    rack->magic = 0;
    free(rack);
    return 0;
}
static ABI int32_t rack_voice(Handle *rack, uint32_t index, Handle **out) {
    if (!rack || rack->magic != RACK) return NGS2_INVALID_RACK_HANDLE;
    if (!out) return NGS2_INVALID_OUT_ADDRESS;
    *out = &((Rack *)rack)->voices[index & 255];
    return 0;
}
/* Voice state flags and events (libSceNgs2): a voice that is told to play reports itself in use
 * and playing until it is stopped. Nothing is rendered, so voices never run out by themselves. */
enum { FLAG_INUSE = 0x1, FLAG_PLAYING = 0x2, FLAG_PAUSED = 0x4 };
enum { PARAM_EVENT = 0x5, EVENT_PLAY = 0, EVENT_STOP = 1, EVENT_STOP_IMM = 2, EVENT_KILL = 3,
       EVENT_PAUSE = 4, EVENT_RESUME = 5 };
typedef struct { uint16_t size; int16_t next; uint32_t id; } ParamHeader;
static size_t controls, state_queries;
static ABI int32_t voice_control(Handle *voice, const ParamHeader *param) {
    ++controls;
    for (int guard = 0; param && guard < 64; ++guard) {
        if (param->id == PARAM_EVENT && voice && voice->magic == VOICE) {
            const uint32_t event = *(const uint32_t *)(param + 1);
            if (event == EVENT_PLAY) voice->flags = FLAG_INUSE | FLAG_PLAYING;
            else if (event == EVENT_STOP || event == EVENT_STOP_IMM || event == EVENT_KILL) voice->flags = 0;
            else if (event == EVENT_PAUSE) voice->flags |= FLAG_PAUSED;
            else if (event == EVENT_RESUME) voice->flags &= ~FLAG_PAUSED;
        }
        if (param->next == 0) break;
        param = (const ParamHeader *)((const char *)param + param->next);
    }
    return 0;
}
static ABI int32_t voice_state_flags(Handle *voice, uint32_t *flags) {
    ++state_queries;
    if (flags) *flags = voice && voice->magic == VOICE ? voice->flags : 0;
    return 0;
}
static ABI int32_t waveform_block(const void *format, uint32_t position, uint32_t samples, void *block) {
    (void)format; (void)position; (void)samples;
    if (block) memset(block, 0, 0x20);
    return 0;
}
static const RuntimeExport exports[] = {
    {"sceNgs2SystemCreateWithAllocator", system_create}, {"sceNgs2SystemDestroy", system_destroy},
    {"sceNgs2SystemRender", system_render},             {"sceNgs2RackCreateWithAllocator", rack_create},
    {"sceNgs2RackDestroy", rack_destroy},               {"sceNgs2RackGetVoiceHandle", rack_voice},
    {"sceNgs2VoiceControl", voice_control},             {"sceNgs2VoiceGetStateFlags", voice_state_flags},
    {"sceNgs2CalcWaveformBlock", waveform_block},
};
uintptr_t runtime_ngs2_resolve(const char *name) { return RUNTIME_LOOKUP(exports, name); }
void runtime_ngs2_report(void) { printf("Runtime: NGS2 systems=%zu, racks=%zu, renders=%zu, voice controls=%zu, state queries=%zu\n", systems, racks, renders, controls, state_queries); }
