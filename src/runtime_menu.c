/* Tomb Raider: menu entries the port adds. The edited main-menu movie (tools/menu/) adds a
 * "Quit Game" item the game has no action for, and reports through ExternalInterface.call:
 *   BBPortQuitIndex<n>  the item's index when the main menu is entered, -1 when it is left;
 *   BBPortHighlight<n>  every highlighted item.
 * The game's ExternalInterface handler (eboot 0xd1ba80: object, method name, arguments, count)
 * starts with runtime_menu_external_call (runtime_pad.c, guest_callbacks) and goes on to its own
 * names, which these never match.
 *
 * Cross over the item opens the game's own Yes/No popup (the one the pause menu's quit warning
 * uses, with the platform's button prompts): dialog manager = eboot 0xd08350(); 0x818fc0(manager,
 * user, listener, type 1 = yes/no, text) copies the text. The answer goes to listener->vtable[3]
 * (listener, answer, user) (eboot 0x819170); the game's delegate vtable (0x17018a0: dtor,
 * deleting dtor, invoke, call invoke) calls (object + adjust)->*method(answer, user), answer 0
 * being No (the pause menu's handler, 0xce0c90, quits on anything else). Yes closes the game like
 * the window's close button. Where that code doesn't match, or the press comes in on another
 * thread than the menu's, the overlay's Yes/No box asks instead. */
#include "runtime.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void bbgpu_request_quit(void);
void bbgpu_toggle_fullscreen(void);
void bbgpu_show_quit_confirm(void);
int bbgpu_is_fullscreen(void);
static void runtime_menu_settings_pad(uint32_t buttons);

/* BB_TRACE_MENU=1: the methods the game invokes on its menu movies (eboot 0xcad800: object,
 * method name, ...) and the movies' ExternalInterface calls back. */
static int trace_menu(void) {
    static int value=-1;
    if (value<0) { const char *env=getenv("BB_TRACE_MENU"); value=env && env[0]=='1'; }
    return value;
}
static unsigned traced_lines;

static uint64_t guest_image;          /* image + eboot vaddr = the address in memory */
static __thread int on_menu_thread;   /* the thread the menu movies run on */

void __attribute__((ms_abi)) runtime_menu_invoke(uint64_t self, uint64_t name, uint64_t image, uint64_t r14) {
    (void)self; (void)r14;
    guest_image=image;
    on_menu_thread=1;
    const char *method=(const char *)(uintptr_t)name;
    if (trace_menu() && method && __atomic_fetch_add(&traced_lines,1,__ATOMIC_RELAXED)<3000)
        printf("Menu invoke: %.80s\n",method);
}

/* ---- the game's popup ---- */
#define DIALOG_MANAGER 0xd08350u
#define DIALOG_OPEN    0x818fc0u
#define DELEGATE_VTBL  0x17018a0u
static const unsigned char manager_code[]={0xe9,0x7b,0x06,0xb1,0xff};
static const unsigned char open_code[]={0x55,0x48,0x89,0xe5,0x48,0x83,0xec,0x30,0x89,0xc8,0x48,0x89,0xd1,
                                        0x4c,0x89,0x04,0x24,0xc7,0x44,0x24,0x20,0x11,0x00,0x00,0x00};

typedef struct { uint64_t vtable, object, method, adjust; } GuestDelegate;
static GuestDelegate quit_listener;

static void ABI __attribute__((aligned(16))) quit_answer(void *object, int answer, int user) {
    (void)object; (void)user;
    printf("Menu: Quit Game answered %d (%s)\n",answer,answer?"yes":"no");
    fflush(stdout);
    if (answer) bbgpu_request_quit();
}

static int open_game_dialog(void) {
    if (!guest_image || !on_menu_thread) return 0;
    const unsigned char *image=(const unsigned char *)(uintptr_t)guest_image;
    if (memcmp(image+DIALOG_MANAGER,manager_code,sizeof(manager_code)) ||
        memcmp(image+DIALOG_OPEN,open_code,sizeof(open_code))) return 0;
    const uint64_t vtable=guest_image+DELEGATE_VTBL;
    /* the vtable's call slot as the loader relocated it: the game's "call invoke" thunk */
    if (*(const uint64_t *)(uintptr_t)(vtable+0x18)!=guest_image+0x123b540u) return 0;
    typedef uint64_t (ABI *ManagerFn)(void);
    typedef uint32_t (ABI *OpenFn)(uint64_t manager, int user, void *listener, int type, const char *text);
    const uint64_t manager=((ManagerFn)(uintptr_t)(guest_image+DIALOG_MANAGER))();
    if (!manager) return 0;
    /* manager+0x30: the popup on screen (0x819170); cleared however it closes */
    if (*(const volatile uint64_t *)(uintptr_t)(manager+0x30)) return 1;
    quit_listener.vtable=vtable;
    quit_listener.object=(uint64_t)(uintptr_t)&quit_listener;
    quit_listener.method=(uint64_t)(uintptr_t)quit_answer;
    quit_listener.adjust=0;
    if (quit_listener.method & 1) return 0; /* would read as a virtual method */
    ((OpenFn)(uintptr_t)(guest_image+DIALOG_OPEN))(manager,0,&quit_listener,1,
        "Are you sure you would like to quit the game?");
    return 1;
}

static volatile int quit_index=-1, highlighted=-1;

/* The pad sampling (runtime_pad.c, not while the overlay takes the input). */
void runtime_menu_pad(uint32_t buttons) {
    static int was_down;
    const int down=(buttons & 0x4000)!=0; /* Cross */
    if (down && !was_down && quit_index>=0 && highlighted==quit_index) {
        if (open_game_dialog()) {
            printf("Menu: Quit Game (asking, game popup)\n");
        } else {
            printf("Menu: Quit Game (asking, overlay; menu thread %d)\n",on_menu_thread);
            bbgpu_show_quit_confirm();
        }
    }
    was_down=down;
    runtime_menu_settings_pad(buttons);
}

/* ---- settings rows the port adds to Options > Display (tools/menu/edit_display.py) ----
 * The movie reports the highlighted row ("BBPortFocus:<key>", empty for the game's own rows) and
 * asks for the value to show ("BBPortGet:<key>", every frame while highlighted); the port answers
 * through the movie's SetExternalInterfaceRetVal (vtable +0xa8, as the game's own handler does for
 * isSD) and changes the value on left/right (or Cross) of the D-pad. */
typedef struct { uint64_t object; uint32_t type, pad; uint64_t data; } GfxValue; /* GFx::Value */
enum { GFX_STRING=4 }; /* this Scaleform: 2 bool, 3 number, 4 string (the game's own handler) */

static const char *fullscreen_get(void) { return bbgpu_is_fullscreen() ? "$Menu_On" : "$Menu_Off"; }
static void fullscreen_change(int dir) { (void)dir; bbgpu_toggle_fullscreen(); }

/* Frame rate (bbport.ini fps_limit, live). Above 60 the game was never run on PS4: marked
 * experimental (shadows differ slightly at 240). */
int bbgpu_fps_limit(void);
void bbgpu_set_fps_limit(int fps);
static const int fps_values[]={30,40,60,120,144,240};
static const char *const fps_labels[]={"30","40","60","120 (Experimental)","144 (Experimental)",
                                       "240 (Experimental)"};
enum { FPS_COUNT=(int)(sizeof(fps_values)/sizeof(fps_values[0])) };
static int fps_index(void) {
    const int fps=bbgpu_fps_limit();
    int best=2;
    for (int i=0;i<FPS_COUNT;++i) if (fps_values[i]<=fps) best=i;
    return best;
}
static const char *fps_get(void) { return fps_labels[fps_index()]; }
static void fps_change(int dir) {
    const int i=(fps_index()+dir+FPS_COUNT)%FPS_COUNT;
    bbgpu_set_fps_limit(fps_values[i]);
}

/* Render resolution and shadow quality (bbport.ini render_res, shadow_quality): eboot patches
 * applied at start (probe.c), so a change shows "(on restart)" until the next start. */
void bbgpu_render_res(int *w, int *h, int *startup_w, int *startup_h);
void bbgpu_set_render_res(int w, int h);
int bbgpu_shadow_quality(int *startup);
void bbgpu_set_shadow_quality(int quality);
static const int render_sizes[][2]={{1280,720},{1600,900},{1920,1080},{2560,1440},{3200,1800},{3840,2160}};
enum { RENDER_COUNT=(int)(sizeof(render_sizes)/sizeof(render_sizes[0])) };
static const char *restart_note(int changed) { return changed ? " (on restart)" : ""; }
static int render_index(void) {
    int w, h, sw, sh, best=2;
    bbgpu_render_res(&w,&h,&sw,&sh);
    for (int i=0;i<RENDER_COUNT;++i) if (render_sizes[i][0]==w && render_sizes[i][1]==h) best=i;
    return best;
}
static const char *render_get(void) {
    static char text[64];
    int w, h, sw, sh;
    bbgpu_render_res(&w,&h,&sw,&sh);
    snprintf(text,sizeof(text),"%dx%d%s%s",w,h,w==1920 && h==1080 ? " (PS4)" : "",
             restart_note(w!=sw || h!=sh));
    return text;
}
static void render_change(int dir) {
    const int i=(render_index()+dir+RENDER_COUNT)%RENDER_COUNT;
    bbgpu_set_render_res(render_sizes[i][0],render_sizes[i][1]);
}
static const char *const shadow_labels[]={"PS4","High","Highest"};
enum { SHADOW_COUNT=3 };
static const char *shadow_get(void) {
    static char text[48];
    int startup;
    const int q=bbgpu_shadow_quality(&startup);
    snprintf(text,sizeof(text),"%s%s",shadow_labels[q<SHADOW_COUNT ? q : SHADOW_COUNT-1],restart_note(q!=startup));
    return text;
}
static void shadow_change(int dir) {
    int startup;
    const int q=bbgpu_shadow_quality(&startup);
    bbgpu_set_shadow_quality((q+dir+SHADOW_COUNT)%SHADOW_COUNT);
}

typedef struct { const char *key; const char *(*get)(void); void (*change)(int dir); } PortSetting;
static const PortSetting port_settings[]={
    {"fullscreen",fullscreen_get,fullscreen_change},
    {"fps",fps_get,fps_change},
    {"render_res",render_get,render_change},
    {"shadows",shadow_get,shadow_change},
};
static const PortSetting *volatile focused_setting;

static const PortSetting *find_setting(const char *key) {
    for (size_t i=0;i<sizeof(port_settings)/sizeof(port_settings[0]);++i)
        if (!strcmp(port_settings[i].key,key)) return &port_settings[i];
    return NULL;
}

static void return_string(uint64_t movie, const char *text) {
    if (!movie) return;
    typedef void (ABI *SetRetFn)(uint64_t movie, const GfxValue *value);
    const uint64_t vtable=*(const uint64_t *)(uintptr_t)movie;
    SetRetFn set=(SetRetFn)(uintptr_t)*(const uint64_t *)(uintptr_t)(vtable+0xa8);
    GfxValue value={0,GFX_STRING,0,(uint64_t)(uintptr_t)text};
    set(movie,&value);
}

static void runtime_menu_settings_pad(uint32_t buttons) {
    static uint32_t was;
    const uint32_t pressed=buttons & ~was;
    was=buttons;
    const PortSetting *s=focused_setting;
    if (!s) return;
    const int dir=(pressed & 0x80) ? -1 : (pressed & (0x20|0x4000)) ? 1 : 0; /* Left; Right, Cross */
    if (dir) { s->change(dir); printf("Menu: %s changed\n",s->key); }
}

void __attribute__((ms_abi)) runtime_menu_external_call(uint64_t self, uint64_t name, uint64_t image, uint64_t r14) {
    (void)r14;
    guest_image=image;
    on_menu_thread=1;
    const char *method=(const char *)(uintptr_t)name;
    if (trace_menu() && method && __atomic_fetch_add(&traced_lines,1,__ATOMIC_RELAXED)<3000)
        printf("Menu external call: %.80s\n",method);
    if (!method || strncmp(method,"BBPort",6)) return;
    if (!strncmp(method,"BBPortFocus:",12)) {
        focused_setting=method[12] ? find_setting(method+12) : NULL;
        return;
    }
    if (!strncmp(method,"BBPortGet:",10)) {
        const PortSetting *setting=find_setting(method+10);
        return_string(self,setting ? setting->get() : "");
        return;
    }
    if (!strncmp(method,"BBPortHighlight",15)) {
        highlighted=(int)strtol(method+15,NULL,10);
    } else if (!strncmp(method,"BBPortQuitIndex",15)) {
        quit_index=(int)strtol(method+15,NULL,10);
        if (quit_index<0) highlighted=-1;
    } else if (!strcmp(method,"BBPortQuit")) {
        printf("Menu: Quit Game\n");
        fflush(stdout);
        bbgpu_request_quit();
    } else if (!strcmp(method,"BBPortFullscreen")) {
        printf("Menu: Fullscreen toggled\n");
        bbgpu_toggle_fullscreen();
    }
}
