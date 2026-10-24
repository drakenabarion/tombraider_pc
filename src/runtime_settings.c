/* Settings the port applies to the game at start, before the GPU side loads bbport.ini:
 * the render resolution and shadow quality (eboot patches in probe.c) and the direct memory
 * they need (runtime_memory.c sizes its pool before anything else runs). Same file and keys
 * as gpu/shim/bbport_settings.cpp, which the menu writes; BB_RENDER_SIZE=WxH and
 * BB_SHADOW_QUALITY=ps4|high|highest override the file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime.h"

static int loaded, shadows;
static unsigned render_w=1920, render_h=1080;

static void parse_size(const char *value) {
    unsigned w=0, h=0;
    if (sscanf(value,"%ux%u",&w,&h)==2 && w>=640 && h>=360 && w<=3840 && h<=2160) {
        render_w=w; render_h=h;
    }
}

static int parse_shadows(const char *value) {
    return !strcmp(value,"high") ? 1 : !strcmp(value,"highest") ? 2 : 0;
}

static void load(void) {
    if (loaded) return;
    loaded=1;
    const char *path=getenv("BB_CONFIG");
    if (!path || !*path) path="bbport.ini";
    FILE *f=fopen(path,"r");
    if (f) {
        char line[256];
        while (fgets(line,sizeof(line),f)) {
            char *eq=strchr(line,'=');
            if (line[0]=='#' || !eq) continue;
            *eq=0;
            char *value=eq+1;
            value[strcspn(value,"\r\n \t")]=0;
            if (!strcmp(line,"render_res")) parse_size(value);
            else if (!strcmp(line,"shadow_quality")) shadows=parse_shadows(value);
        }
        fclose(f);
    }
    const char *env=getenv("BB_RENDER_SIZE");
    if (env && *env) parse_size(env);
    env=getenv("BB_SHADOW_QUALITY");
    if (env && *env) shadows=parse_shadows(env);
}

void runtime_render_size(unsigned *w, unsigned *h) { load(); *w=render_w; *h=render_h; }
int runtime_shadow_quality(void) { load(); return shadows; }

/* The game's GPU heap (288 MiB at 1080p, probe.c): screen-sized targets and the TressFX
 * fragment lists (96 bytes a pixel) grow with the pixel count; a quarter more for the rest.
 * High shadows add the 16384x8192 atlas's extra 192 MiB (256 MiB kept). */
uint64_t runtime_gpu_heap_bytes(void) {
    load();
    const uint64_t base=UINT64_C(0x12000000), pixels=(uint64_t)render_w*render_h, ref=1920u*1080u;
    uint64_t bytes=pixels>ref ? base*pixels/ref*5/4 : base;
    if (shadows) bytes+=UINT64_C(0x10000000);
    bytes=(bytes+0x1fffff)&~UINT64_C(0x1fffff);
    return bytes>UINT64_C(0x7fe00000) ? UINT64_C(0x7fe00000) : bytes;
}

/* Direct memory: the game maps 1 GiB + 3 GiB + the GPU heap (+64 KiB); retail is 5056 MiB. */
uint64_t runtime_direct_memory_mb(void) {
    const uint64_t mb=4096+(runtime_gpu_heap_bytes()>>20)+64;
    return mb>5056 ? mb : 5056;
}
