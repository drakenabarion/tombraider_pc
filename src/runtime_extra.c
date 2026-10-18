/* Imports the Uncharted runtime never needed: Virtual-On Masterpiece (CUSA16156) and its newer
 * libc.prx, then Tomb Raider: Definitive Edition (CUSA00109). Offline behaviour throughout: no
 * USB devices, no RUDP peers, no sockets, no PlayStation Camera, not broadcasting, dialogs that
 * close at once, no screenshots. Return values follow shadPS4's implementations where it has them. */
#include "runtime.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define KERNEL_EINVAL ((int32_t)0x80020016)
#define KERNEL_ESRCH ((int32_t)0x80020003)
#define USBD_NO_DEVICE ((int32_t)0x80240004)
#define USBD_NOT_FOUND ((int32_t)0x80240005)
#define RUDP_NOT_CONNECTED ((int32_t)0x80770001)
#define APP_CONTENT_NO_ENTITLEMENT ((int32_t)0x80D90007)
#define CAMERA_NOT_CONNECTED ((int32_t)0x802E0010)

static void once(const char *what) {
    /* First call only: these paths are expected to stay quiet, so a call is worth noting. */
    printf("Runtime: %s (offline stub)\n", what);
}
#define NOTE(what) do { static int noted; if (!noted) { noted = 1; once(what); } } while (0)

/* ---- kernel and libc.prx internals ---------------------------------------------------------- */
static ABI int32_t is_neo_mode(void) { return 0; }  /* base PS4 */
static ABI int32_t elf_phdr_match_addr(const void *info, uintptr_t addr) { (void)info; (void)addr; return 0; }
static ABI int32_t is_signal_return(uintptr_t addr) { (void)addr; return 0; }
/* C++ exception unwinding asks which module holds an address; the image is not described yet. */
static ABI int32_t module_info_for_unwind(uintptr_t addr, int32_t flags, void *info) {
    (void)addr; (void)flags; (void)info;
    NOTE("sceKernelGetModuleInfoForUnwind: C++ exception unwinding is not supported yet");
    return KERNEL_ESRCH;
}
static ABI int32_t module_info_from_addr(uintptr_t addr, int32_t flags, void *info) {
    (void)addr; (void)flags; (void)info;
    NOTE("sceKernelGetModuleInfoFromAddr");
    return KERNEL_ESRCH;
}
static ABI void pthread_cxa_finalize(void *dso) { (void)dso; }
/* Thread destructors run from libc.prx's own exit path; the runtime's thread exit does not call it. */
static ABI void set_thread_dtors(void *dtor) { (void)dtor; }
static ABI void set_thread_atexit_count(void *fn) { (void)fn; }
static ABI void set_thread_atexit_report(void *fn) { (void)fn; }
static ABI void rtld_thread_atexit_increment(void *dso) { (void)dso; }
static ABI void rtld_thread_atexit_decrement(void *dso) { (void)dso; }
static ABI uint32_t asan_enabled(void) { return 0; }
static ABI void *sanitizer_replace(void) { return NULL; }
static ABI int32_t module_segment_info(void *info) { (void)info; return KERNEL_EINVAL; }
/* libc.prx's heap tracing hooks (shadPS4: pointers to zeroed tables). */
typedef struct { uint64_t size; uint32_t flag, get_segment_info; uint64_t *atomic_id_mask, *mstate_table; } HeapTraceInfo;
static uint64_t heap_atomic_id_mask, heap_mstate_table[64];
static ABI void heap_trace_info(HeapTraceInfo *info) {
    if (!info) return;
    info->atomic_id_mask = &heap_atomic_id_mask;
    info->mstate_table = heap_mstate_table;
    info->get_segment_info = 0;
}
static ABI int32_t sysmodule_load_internal_with_arg(uint32_t id, size_t args, const void *argp, uint32_t flags, int32_t *result) {
    (void)id; (void)args; (void)argp; (void)flags;
    if (result) *result = 0;
    return 0;
}
static ABI int32_t posix_pthread_equal(uintptr_t a, uintptr_t b) { return a == b; }
static ABI int32_t pthread_getname(uintptr_t thread, char *name) {
    (void)thread;
    if (name) snprintf(name, 32, "guest");
    return 0;
}
static ABI int32_t pthread_cancel_stub(uintptr_t thread) { (void)thread; NOTE("scePthreadCancel"); return 0; }

/* ---- dialogs and system services ------------------------------------------------------------ */
typedef struct { int32_t mode, result, button; uint8_t reserved[32]; } MsgDialogResult;
static ABI int32_t dialog_close(void) { return 0; }
static ABI int32_t msg_dialog_result(MsgDialogResult *r) {
    if (r) { memset(r, 0, sizeof(*r)); r->button = 1; }  /* OK / Yes */
    return 0;
}
typedef struct { int32_t result; uint8_t authorized; uint8_t reserved[32]; } CommerceDialogResult;
static ABI int32_t commerce_dialog_result(CommerceDialogResult *r) {
    if (r) { memset(r, 0, sizeof(*r)); r->result = 1; }  /* user canceled: nothing bought */
    return 0;
}
static ABI int32_t ime_panel_size_extended(const void *param, const void *extended, uint32_t *w, uint32_t *h) {
    (void)param; (void)extended;
    if (w) *w = 0;
    if (h) *h = 0;
    return 0;
}
static ABI int32_t ok(void) { return 0; }
static ABI int32_t entitlement_key(const void *label, void *key) { (void)label; (void)key; return APP_CONTENT_NO_ENTITLEMENT; }
static ABI int32_t user_color(int32_t user, int32_t *color) { (void)user; if (color) *color = 0; return 0; }

/* ---- USB (arcade sticks on PS4; SDL handles controllers here) -------------------------------- */
static ABI int32_t usbd_init(void) { return 0; }
static ABI int64_t usbd_device_list(void ***list) { if (list) *list = NULL; return 0; }
static ABI void usbd_free_list(void **list, int32_t unref) { (void)list; (void)unref; }
static ABI int32_t usbd_no_device(void) { return USBD_NO_DEVICE; }
static ABI void usbd_close(void *handle) { (void)handle; }
static ABI int32_t usbd_cancel(void *transfer) { (void)transfer; return USBD_NOT_FOUND; }
static ABI int32_t usbd_handle_events(void) { return 0; }

/* ---- RUDP (online play): contexts exist, no peer ever connects -------------------------------- */
static ABI int32_t rudp_poll_create(int32_t size) { (void)size; return 1; }
static ABI int32_t rudp_poll_wait(int32_t poll, void *events, int32_t count, uint64_t timeout) {
    (void)poll; (void)events; (void)count; (void)timeout;
    return 0;
}
static ABI int32_t rudp_create_context(void *handler, void *arg, int32_t *context) {
    (void)handler; (void)arg;
    if (context) *context = 1;
    return 0;
}
static ABI int32_t rudp_not_connected(void) { NOTE("RUDP: online play"); return RUDP_NOT_CONNECTED; }
static ABI int64_t rudp_io(int32_t context, void *data, uint64_t size, uint32_t flags) {
    (void)context; (void)data; (void)size; (void)flags;
    return RUDP_NOT_CONNECTED;
}

/* libSceDiscMap's unnamed query (shadPS4 Func_7C980FFB0AA27E7A): everything is on the drive. */
static ABI int32_t disc_map_query(const char *path, int64_t offset, int64_t size, int32_t *flags, int32_t *a, int32_t *b) {
    (void)path; (void)offset; (void)size;
    if (flags) *flags = 0;
    if (a) *a = 0;
    if (b) *b = 0;
    return 0;
}

static const RuntimeExport exports[] = {
    {"sceKernelIsNeoMode", is_neo_mode}, {"__elf_phdr_match_addr", elf_phdr_match_addr},
    {"_is_signal_return", is_signal_return}, {"sceKernelGetModuleInfoForUnwind", module_info_for_unwind},
    {"sceKernelGetModuleInfoFromAddr", module_info_from_addr},
    {"sceSysmoduleGetModuleInfoForUnwind", module_info_for_unwind},
    {"__pthread_cxa_finalize", pthread_cxa_finalize}, {"_sceKernelSetThreadDtors", set_thread_dtors},
    {"_sceKernelSetThreadAtexitCount", set_thread_atexit_count},
    {"_sceKernelSetThreadAtexitReport", set_thread_atexit_report},
    {"_sceKernelRtldThreadAtexitIncrement", rtld_thread_atexit_increment},
    {"_sceKernelRtldThreadAtexitDecrement", rtld_thread_atexit_decrement},
    {"sceKernelIsAddressSanitizerEnabled", asan_enabled},
    {"sceKernelGetSanitizerMallocReplaceExternal", sanitizer_replace},
    {"sceKernelGetSanitizerNewReplaceExternal", sanitizer_replace},
    {"sceKernelInternalMemoryGetModuleSegmentInfo", module_segment_info},
    {"sceLibcHeapGetTraceInfo", heap_trace_info},
    {"sceSysmoduleLoadModuleInternalWithArg", sysmodule_load_internal_with_arg},
    {"pthread_equal", posix_pthread_equal}, {"scePthreadGetname", pthread_getname},
    {"scePthreadCancel", pthread_cancel_stub},
    {"sceMsgDialogClose", dialog_close}, {"sceMsgDialogGetResult", msg_dialog_result},
    {"sceErrorDialogClose", dialog_close}, {"sceNpCommerceDialogClose", dialog_close},
    {"sceNpCommerceDialogGetResult", commerce_dialog_result},
    {"sceImeDialogGetPanelSizeExtended", ime_panel_size_extended},
    {"sceScreenShotEnable", ok}, {"sceScreenShotDisable", ok}, {"sceScreenShotSetOverlayImage", ok},
    {"sceSystemServiceEnableSuspendConfirmationDialog", ok},
    {"sceSystemServiceDisableSuspendConfirmationDialog", ok},
    {"sceAppContentGetEntitlementKey", entitlement_key}, {"sceUserServiceGetUserColor", user_color},
    {"sceUsbdInit", usbd_init}, {"sceUsbdGetDeviceList", usbd_device_list},
    {"sceUsbdFreeDeviceList", usbd_free_list}, {"sceUsbdCheckConnected", usbd_no_device},
    {"sceUsbdGetDeviceDescriptor", usbd_no_device}, {"sceUsbdSubmitTransfer", usbd_no_device},
    {"sceUsbdClose", usbd_close}, {"sceUsbdCancelTransfer", usbd_cancel},
    {"sceUsbdHandleEvents", usbd_handle_events},
    {"sceRudpInit", ok}, {"sceRudpEnd", ok}, {"sceRudpEnableInternalIOThread", ok},
    {"sceRudpSetEventHandler", ok}, {"sceRudpPollCreate", rudp_poll_create}, {"sceRudpPollDestroy", ok},
    {"sceRudpPollControl", ok}, {"sceRudpPollWait", rudp_poll_wait},
    {"sceRudpCreateContext", rudp_create_context}, {"sceRudpTerminate", ok},
    {"sceRudpSetOption", ok}, {"sceRudpGetOption", ok}, {"sceRudpBind", ok},
    {"sceRudpInitiate", rudp_not_connected}, {"sceRudpRead", rudp_io}, {"sceRudpWrite", rudp_io},
};

/* ---- Tomb Raider: Definitive Edition -------------------------------------------------------- */
static ABI int32_t ime_panel_size(const void *param, uint32_t *w, uint32_t *h) {
    (void)param;
    if (w) *w = 0;
    if (h) *h = 0;
    return 0;
}
static ABI int32_t audio_mix_level_pad_speaker(int32_t handle, int32_t level) { (void)handle; (void)level; return 0; }
/* Unwinding through a signal frame or a saved context: only reached by C++ exceptions or
 * signal handlers, which the port does not deliver yet. */
static ABI int32_t context_unsupported(void) { NOTE("sigreturn/__Ux86_64_setcontext called"); return -1; }

/* BSD sockets (debug and telemetry connections): no network. FreeBSD errno values. */
typedef struct { int64_t sec, usec; } GuestTimeval;
static ABI int32_t socket_none(int32_t domain, int32_t type, int32_t protocol) {
    (void)domain; (void)type; (void)protocol;
    NOTE("socket: no network");
    *runtime_errno() = 47; /* EAFNOSUPPORT */
    return -1;
}
static ABI int64_t socket_bad(void) { *runtime_errno() = 9; return -1; } /* EBADF: no socket exists */
/* select() with no descriptors is also used as a sleep. */
static ABI int32_t socket_select(int32_t n, void *r, void *w, void *e, const GuestTimeval *timeout) {
    (void)n; (void)r; (void)w; (void)e;
    if (timeout && timeout->sec >= 0 && timeout->usec >= 0)
        runtime_sleep_ns((uint64_t)timeout->sec * 1000000000 + (uint64_t)timeout->usec * 1000);
    return 0;
}

/* Live streaming and social feedback: not broadcasting. */
static ABI int32_t streaming_status(void *status) { if (status) memset(status, 0, 72); return 0; }
static ABI int32_t streaming_feedback_count(uint32_t *count) { if (count) *count = 0; return 0; }
static ABI int32_t streaming_feedback(void *messages, uint32_t max, uint32_t *count) {
    (void)messages; (void)max;
    if (count) *count = 0;
    return 0;
}

/* PlayStation Camera, depth and light-bar tracking: no camera attached. */
static ABI int32_t camera_attached(int32_t index) { (void)index; return 0; }
static ABI int32_t camera_none(void) { NOTE("PlayStation Camera: none attached"); return CAMERA_NOT_CONNECTED; }
static ABI int32_t depth_none(void) { NOTE("libSceDepth: no camera"); return CAMERA_NOT_CONNECTED; }
static ABI int32_t pad_tracker_memory(uint32_t *size) { if (size) *size = 4096; return 0; }
static ABI int32_t pad_tracker_none(void) { NOTE("libScePadTracker: no camera, nothing tracked"); return CAMERA_NOT_CONNECTED; }

static const RuntimeExport tomb_raider_exports[] = {
    {"sceImeDialogGetPanelSize", ime_panel_size}, {"sceAudioOutSetMixLevelPadSpk", audio_mix_level_pad_speaker},
    {"sceScreenShotSetParam", ok},
    {"sigreturn", context_unsupported}, {"__Ux86_64_setcontext", context_unsupported},
    {"socket", socket_none}, {"bind", socket_bad}, {"connect", socket_bad}, {"listen", socket_bad},
    {"accept", socket_bad}, {"recv", socket_bad}, {"recvfrom", socket_bad}, {"send", socket_bad},
    {"sendto", socket_bad}, {"shutdown", socket_bad}, {"setsockopt", socket_bad}, {"getsockopt", socket_bad},
    {"getsockname", socket_bad}, {"select", socket_select},
    {"sceGameLiveStreamingInitialize", ok}, {"sceGameLiveStreamingTerminate", ok},
    {"sceGameLiveStreamingSetMaxBitrate", ok}, {"sceGameLiveStreamingPermitLiveStreaming", ok},
    {"sceGameLiveStreamingEnableLiveStreaming", ok}, {"sceGameLiveStreamingPermitServerSideRecording", ok},
    {"sceGameLiveStreamingEnableSocialFeedback", ok}, {"sceGameLiveStreamingSetPresetSocialFeedbackCommands", ok},
    {"sceGameLiveStreamingSetSpoilerTag", ok}, {"sceGameLiveStreamingGetCurrentStatus", streaming_status},
    {"sceGameLiveStreamingGetSocialFeedbackMessagesCount", streaming_feedback_count},
    {"sceGameLiveStreamingGetSocialFeedbackMessages", streaming_feedback},
    {"sceCameraIsAttached", camera_attached}, {"sceCameraOpen", camera_none}, {"sceCameraSetConfig", camera_none},
    {"sceCameraClose", camera_none}, {"sceCameraStop", camera_none}, {"sceCameraGetFrameData", camera_none},
    {"sceDepthQueryMemory", depth_none}, {"sceDepthInitialize", depth_none}, {"sceDepthTerminate", ok},
    {"sceDepthHasCalibrationData", depth_none}, {"sceDepthLoadCalibrationData", depth_none},
    {"sceDepthUpdate", depth_none}, {"sceDepthHeadCandidateTrackerGetResult", depth_none},
    {"sceDepthHeadCandidateTrackerSetValidationInformation", depth_none},
    {"scePadTrackerGetWorkingMemorySize", pad_tracker_memory}, {"scePadTrackerInit", ok},
    {"scePadTrackerTerm", ok}, {"scePadTrackerCalibrate", ok}, {"scePadTrackerUpdate", ok},
    {"scePadTrackerReadState", pad_tracker_none},
};

uintptr_t runtime_extra_resolve(const char *name) {
    if (!strcmp(name, "fJgP+wqifno#libSceDiscMap")) return (uintptr_t)disc_map_query;
    uintptr_t found = RUNTIME_LOOKUP(exports, name);
    return found ? found : RUNTIME_LOOKUP(tomb_raider_exports, name);
}
