/* Optional asynchronous timestamp streams. One application frame can span
 * several command buffers and borrowed wrappers of the same Vulkan device.
 * No query waits, queue waits, fences, or profiling-only barriers. */
#include <time.h>
#include "timing_properties.inc"
enum { ENCORE_TIMING_SLOTS = 8, ENCORE_TIMING_MARKERS = 256, ENCORE_TIMING_EVENTS = 128 };
typedef struct { uint32_t type; const void *next; uint32_t flags, query_type, count, statistics; } EncoreTimingPoolInfo;
typedef VkResult (*EncoreCreateQueryPool)(void *, const EncoreTimingPoolInfo *, const void *, uint64_t *);
typedef void (*EncoreDestroyQueryPool)(void *, uint64_t, const void *);
typedef void (*EncoreResetQueries)(void *, uint64_t, uint32_t, uint32_t);
typedef void (*EncoreWriteTimestamp)(void *, uint32_t, uint64_t, uint32_t);
typedef VkResult (*EncoreReadQueries)(void *, uint64_t, uint32_t, uint32_t, size_t, void *, uint64_t, uint32_t);
typedef struct { uint32_t label, first, last, depth; uint64_t start_ns, duration_ns; bool open; } EncoreTimingEvent;
typedef struct {
    uint64_t pool, frame_id, wait_ns;
    void *completion_fence;
    uint32_t state, queries, count, depth;
    bool reset, overflow, valid;
    EncoreTimingEvent events[ENCORE_TIMING_EVENTS];
} EncoreTimingSlot;
typedef struct EncoreTimingStream {
    EncoreVkDevice *owner;
    void *device;
    EncoreCreateQueryPool create;
    EncoreDestroyQueryPool destroy;
    EncoreResetQueries reset;
    EncoreWriteTimestamp write;
    EncoreReadQueries read;
    float period;
    uint32_t bits;
    int active, ready;
    bool supported;
    EncoreTimingSlot slots[ENCORE_TIMING_SLOTS];
    struct EncoreTimingStream *next;
} EncoreTimingStream;
static EncoreTimingStream *encore_timing_streams;
static uint64_t encore_timing_clock(void) {
#if defined(_WIN32)
    LARGE_INTEGER now, frequency; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
    return (uint64_t)((double)now.QuadPart * 1e9 / (double)frequency.QuadPart);
#else
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
#endif
}
static EncoreTimingStream *encore_timing_find(EncoreVkDevice *device) {
    for (EncoreTimingStream *s = encore_timing_streams; s; s = s->next)
        if (device && s->device == device->device) return s;
    return NULL;
}
static EncoreTimingStream *encore_timing_create(EncoreVkDevice *device) {
    if (!device || !device->available) return NULL;
    EncoreTimingStream *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->owner = device; s->device = device->device; s->active = s->ready = -1;
    s->next = encore_timing_streams; encore_timing_streams = s;
    PFN_vkGetPhysicalDeviceProperties props = (PFN_vkGetPhysicalDeviceProperties)device->context->get_proc(device->context->instance, "vkGetPhysicalDeviceProperties");
    PFN_vkGetPhysicalDeviceQueueFamilyProperties queues = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)device->context->get_proc(device->context->instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    if (!props || !queues) return s;
    EncoreVkTimingProperties p; memset(&p, 0, sizeof p); props(device->physical_device, &p); s->period = p.limits.timestampPeriod;
    uint32_t count = 0; queues(device->physical_device, &count, NULL);
    VkQueueFamilyProperties *families = calloc(count, sizeof *families);
    if (!families) return s;
    queues(device->physical_device, &count, families);
    if (device->queue_family < count) s->bits = families[device->queue_family].timestampValidBits;
    free(families);
    s->create = (EncoreCreateQueryPool)device->get_device_proc(device->device, "vkCreateQueryPool");
    s->destroy = (EncoreDestroyQueryPool)device->get_device_proc(device->device, "vkDestroyQueryPool");
    s->reset = (EncoreResetQueries)device->get_device_proc(device->device, "vkCmdResetQueryPool");
    s->write = (EncoreWriteTimestamp)device->get_device_proc(device->device, "vkCmdWriteTimestamp");
    s->read = (EncoreReadQueries)device->get_device_proc(device->device, "vkGetQueryPoolResults");
    if (!s->create || !s->destroy || !s->reset || !s->write || !s->read || !s->bits || s->bits > 64 || !(s->period > 0)) return s;
    for (unsigned i = 0; i < ENCORE_TIMING_SLOTS; ++i) {
        EncoreTimingPoolInfo info = {11, NULL, 0, 2, ENCORE_TIMING_MARKERS, 0};
        if (s->create(s->device, &info, NULL, &s->slots[i].pool) != VK_SUCCESS) return s;
    }
    s->supported = true; return s;
}
/* Target retirement already proves GPU completion. A pending profiler sample
 * may outlive its producer target; never poll a fence after vkDestroyFence. */
static void encore_timing_target_retired(EncoreVkSwapchain *target) {
    if (!target || !target->owner) return;
    EncoreTimingStream *s = encore_timing_find(target->owner);
    if (!s) return;
    for (unsigned i = 0; i < ENCORE_TIMING_SLOTS; ++i) {
        EncoreTimingSlot *slot = &s->slots[i];
        if (slot->completion_fence == target->fences[0] ||
            slot->completion_fence == target->fences[1])
            slot->completion_fence = NULL;
    }
}

/* Called only after this wrapper's swapchains and submissions are retired.
 * Borrowed wrappers that did not create the stream do not own its pools. */
static void encore_timing_device_destroy(EncoreVkDevice *device) {
    EncoreTimingStream **link = &encore_timing_streams;
    while (*link) {
        EncoreTimingStream *s = *link;
        if (s->owner != device && !(device->owns_device && s->device == device->device)) { link = &s->next; continue; }
        /* A borrowed producer can retire before the UI consumer. Preserve
         * pending pools until the owning device retires, without a new wait. */
        if (!device->owns_device) {
            bool pending = false;
            for (unsigned i = 0; i < ENCORE_TIMING_SLOTS; ++i) {
                EncoreTimingSlot *slot = &s->slots[i];
                if (slot->state && slot->queries) {
                    if(slot->completion_fence && device->get_fence_status &&
                        device->get_fence_status(s->device,slot->completion_fence)!=VK_SUCCESS) { pending=true;continue; }
                    uint64_t values[ENCORE_TIMING_MARKERS][2];
                    VkResult result = s->read(s->device, slot->pool, 0, slot->queries,
                        sizeof values, values, sizeof values[0], 1u | 4u);
                    if (result == VK_NOT_READY) pending = true;
                }
            }
            if (pending) { s->owner = NULL; s->active = -1; link = &s->next; continue; }
        }
        for (unsigned i = 0; i < ENCORE_TIMING_SLOTS; ++i)
            if (s->slots[i].pool && s->destroy) s->destroy(s->device, s->slots[i].pool, NULL);
        *link = s->next; free(s);
    }
}
uint32_t encore_vk_timing_support(size_t device_handle) {
    EncoreTimingStream *s = encore_timing_find((EncoreVkDevice *)device_handle);
    if (!s) s = encore_timing_create((EncoreVkDevice *)device_handle);
    return s && s->supported ? 1 : 0;
}
bool encore_vk_timing_begin(size_t device_handle, uint64_t frame_id) {
    EncoreTimingStream *s = encore_timing_find((EncoreVkDevice *)device_handle);
    if (!s) s = encore_timing_create((EncoreVkDevice *)device_handle);
    if (!s || !s->supported) return false;
    s->active = -1;
    for (unsigned i = 0; i < ENCORE_TIMING_SLOTS; ++i) if (!s->slots[i].state) {
        uint64_t pool = s->slots[i].pool;
        memset(&s->slots[i], 0, sizeof s->slots[i]); s->slots[i].pool = pool;
        s->slots[i].frame_id = frame_id; s->slots[i].state = 1; s->slots[i].valid = true;
        s->active = (int)i; return true;
    }
    return false;
}
/* Markers are labels supplied by the application, not engine-specific names. */
void encore_vk_frame_timing_mark(size_t frame_handle, uint32_t label, bool begin) {
    EncoreVkFrame *f = (EncoreVkFrame *)frame_handle;
    EncoreTimingStream *s = f && f->owner ? encore_timing_find(f->owner->owner) : NULL;
    if (!s || s->active < 0 || !f->recording || f->complete) return;
    EncoreTimingSlot *slot = &s->slots[s->active];
    slot->completion_fence = f->owner->fences[f->frame_index];
    if (slot->queries >= ENCORE_TIMING_MARKERS || (begin && slot->count >= ENCORE_TIMING_EVENTS)) { slot->overflow = true; return; }
    if (!slot->reset) {
        if (f->in_render_pass) { slot->overflow = true; return; }
        s->reset(f->command_buffer, slot->pool, 0, ENCORE_TIMING_MARKERS); slot->reset = true;
    }
    EncoreTimingEvent *e = NULL;
    if (begin) {
        e = &slot->events[slot->count++]; e->label = label; e->first = slot->queries;
        e->depth = slot->depth++; e->open = true;
    } else {
        for (uint32_t i = slot->count; i > 0; --i) if (slot->events[i-1].open && slot->events[i-1].label == label) { e = &slot->events[i-1]; break; }
        if (!e) { slot->overflow = true; return; }
        e->last = slot->queries; e->open = false; if (slot->depth) --slot->depth;
    }
    /* BOTTOM_OF_PIPE on both boundaries measures ordered intervals without
     * introducing a pipeline barrier. Nested intervals are never summed. */
    s->write(f->command_buffer, 0x2000u, slot->pool, slot->queries++);
}
void encore_vk_timing_finish(size_t device_handle, bool valid) {
    EncoreTimingStream *s = encore_timing_find((EncoreVkDevice *)device_handle);
    if (s && s->active >= 0) {
        EncoreTimingSlot *slot = &s->slots[s->active]; slot->valid = valid && !slot->overflow && slot->depth == 0;
        slot->state = 2; s->active = -1;
    }
}
uint64_t encore_vk_timing_wait_ns(size_t device_handle) {
    EncoreTimingStream *s = encore_timing_find((EncoreVkDevice *)device_handle);
    return s && s->active >= 0 ? s->slots[s->active].wait_ns : 0;
}
static void encore_timing_add_wait(EncoreVkDevice *device, uint64_t elapsed) {
    EncoreTimingStream *s = encore_timing_find(device);
    if (s && s->active >= 0) s->slots[s->active].wait_ns += elapsed;
}
uint64_t encore_vk_timing_poll(size_t device_handle) {
    EncoreTimingStream *s = encore_timing_find((EncoreVkDevice *)device_handle);
    if (!s) return 0;
    if (s->ready >= 0) return s->slots[s->ready].frame_id;
    for (unsigned i = 0; i < ENCORE_TIMING_SLOTS; ++i) {
        EncoreTimingSlot *slot = &s->slots[i]; if (slot->state != 2) continue;
        if(encore_parallel_frame_pending(s->owner,slot->frame_id))continue;
        /* A recycled pool can expose its previous availability before the
         * GPU executes this generation's reset. Poll its last producer fence. */
        if(slot->completion_fence && s->owner && s->owner->get_fence_status &&
            s->owner->get_fence_status(s->device,slot->completion_fence)!=VK_SUCCESS)continue;
        uint64_t values[ENCORE_TIMING_MARKERS][2];
        if (slot->queries) {
            VkResult result = s->read(s->device, slot->pool, 0, slot->queries, sizeof values, values, sizeof values[0], 1u | 4u);
            if (result == VK_NOT_READY) continue;
            if (result != VK_SUCCESS) { slot->valid = false; slot->state = 3; s->ready = (int)i; return slot->frame_id; }
            bool ready = true; for (unsigned q = 0; q < slot->queries; ++q) if (!values[q][1]) ready = false;
            if (!ready) continue;
            uint64_t mask = s->bits == 64 ? UINT64_MAX : (1ull << s->bits) - 1;
            uint64_t origin = values[0][0];
            for (unsigned e = 0; e < slot->count; ++e) {
                EncoreTimingEvent *event = &slot->events[e];
                event->start_ns = (uint64_t)((double)((values[event->first][0] - origin) & mask) * s->period);
                event->duration_ns = event->open ? 0 : (uint64_t)((double)((values[event->last][0] - values[event->first][0]) & mask) * s->period);
            }
        } else slot->valid = false;
        slot->state = 3; s->ready = (int)i; return slot->frame_id;
    }
    return 0;
}
uint64_t encore_vk_timing_value(size_t device_handle, uint32_t event, uint32_t field) {
    EncoreTimingStream *s = encore_timing_find((EncoreVkDevice *)device_handle);
    if (!s || s->ready < 0) return 0;
    EncoreTimingSlot *slot = &s->slots[s->ready];
    if (field == 0) return slot->valid ? 1 : 2;
    if (field == 1) return slot->count;
    if (field == 6) return slot->overflow;
    if (event >= slot->count) return 0;
    EncoreTimingEvent *e = &slot->events[event];
    switch (field) { case 2:return e->label; case 3:return e->start_ns; case 4:return e->duration_ns; case 5:return e->depth; default:return 0; }
}
void encore_vk_timing_release(size_t device_handle) {
    EncoreTimingStream *s = encore_timing_find((EncoreVkDevice *)device_handle);
    if (s && s->ready >= 0) { s->slots[s->ready].state = 0; s->ready = -1; }
}
