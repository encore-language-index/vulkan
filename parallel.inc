/* Optional CPU command recording workers and a single VkQueue owner.
 * Only native POD packets cross threads. Window/Encore graph state never does.
 * Command buffers own independent pools; submission order is FIFO. */
#include <threads.h>
#include <stdatomic.h>
#include <stdalign.h>

typedef struct EncoreParallelDevice EncoreParallelDevice;
typedef struct EncoreParallelCommand EncoreParallelCommand;
typedef struct EncoreParallelQueueTask EncoreParallelQueueTask;
typedef struct { uint64_t id, start[5], end[5], cpu[5], wall[5]; } EncoreParallelSample;
typedef void (*EncoreParallelReplay)(unsigned char *);
typedef struct { size_t bytes; EncoreParallelReplay replay; } EncoreParallelOp;
typedef struct EncoreParallelTask {
    void (*run)(struct EncoreParallelTask *);
    struct EncoreParallelTask *next;
    bool done;
} EncoreParallelTask;
typedef struct {
    thrd_t thread;
    mtx_t mutex;
    cnd_t wake, changed;
    EncoreParallelTask *head, *tail;
    size_t count;
    bool stop;
} EncoreParallelWorker;
struct EncoreParallelCommand {
    void *handle, *pool;
    EncoreParallelDevice *owner;
    EncoreParallelWorker *worker;
    unsigned char *bytes;
    size_t length, capacity;
    uint64_t frame_id;
    uint32_t reset_flags;
    bool reset;
    EncoreParallelTask task;
    EncoreParallelCommand *next;
};
struct EncoreParallelDevice {
    void *device, *queue;
    uint32_t family, worker_count, next_worker;
    PFN_vkGetDeviceProcAddr proc;
    EncoreParallelWorker submit, workers[4];
    EncoreParallelCommand *commands;
    EncoreParallelQueueTask *jobs;
    uint32_t next_job;
    uint64_t frame_id;
    mtx_t stats_mutex;
    EncoreParallelSample samples[600];
    void *failed_present_fences[16];
    atomic_int error;
    atomic_uint_fast64_t record_ns, submit_ns, packets, bytes;
    EncoreParallelDevice *next;
};
static EncoreParallelDevice *encore_parallel_devices;
static uint32_t encore_parallel_requested;
static size_t encore_parallel_align(size_t n) { return (n + 15u) & ~(size_t)15u; }
static EncoreParallelDevice *encore_parallel_device(void *handle) {
    for (EncoreParallelDevice *d=encore_parallel_devices;d;d=d->next)
        if (d->device==handle || d->queue==handle) return d;
    return NULL;
}
static EncoreParallelCommand *encore_parallel_command(void *handle) {
    for (EncoreParallelDevice *d=encore_parallel_devices;d;d=d->next)
        for (EncoreParallelCommand *c=d->commands;c;c=c->next) if(c->handle==handle)return c;
    return NULL;
}
static int encore_parallel_loop(void *arg) {
    EncoreParallelWorker *w=arg;
    mtx_lock(&w->mutex);
    for (;;) {
        while (!w->head && !w->stop) cnd_wait(&w->wake,&w->mutex);
        if (!w->head && w->stop) break;
        EncoreParallelTask *t=w->head;
        w->head=t->next;if(!w->head)w->tail=NULL;
        mtx_unlock(&w->mutex);
        t->run(t);
        mtx_lock(&w->mutex);
        t->done=true;--w->count;cnd_broadcast(&w->changed);
    }
    mtx_unlock(&w->mutex);return 0;
}
static bool encore_parallel_worker_start(EncoreParallelWorker *w) {
    if(mtx_init(&w->mutex,mtx_plain)!=thrd_success)return false;
    if(cnd_init(&w->wake)!=thrd_success){mtx_destroy(&w->mutex);return false;}
    if(cnd_init(&w->changed)!=thrd_success){cnd_destroy(&w->wake);mtx_destroy(&w->mutex);return false;}
    if(thrd_create(&w->thread,encore_parallel_loop,w)!=thrd_success){cnd_destroy(&w->changed);cnd_destroy(&w->wake);mtx_destroy(&w->mutex);return false;}
    return true;
}
static void encore_parallel_enqueue(EncoreParallelWorker *w,EncoreParallelTask *t) {
    mtx_lock(&w->mutex);
    // Bounded backpressure, never an unbounded stream of stale frames.
    while(w->count>=64)cnd_wait(&w->changed,&w->mutex);
    t->done=false;t->next=NULL;
    if(w->tail)w->tail->next=t;else w->head=t;
    w->tail=t;++w->count;cnd_signal(&w->wake);mtx_unlock(&w->mutex);
}
static void encore_parallel_join(EncoreParallelWorker *w,EncoreParallelTask *t) {
    mtx_lock(&w->mutex);while(!t->done)cnd_wait(&w->changed,&w->mutex);mtx_unlock(&w->mutex);
}
static void encore_parallel_drain_worker(EncoreParallelWorker *w) {
    mtx_lock(&w->mutex);while(w->count)cnd_wait(&w->changed,&w->mutex);mtx_unlock(&w->mutex);
}
static void encore_parallel_drain(EncoreParallelDevice *d) {
    if(!d)return;
    encore_parallel_drain_worker(&d->submit);
    for(uint32_t i=0;i<d->worker_count;++i)encore_parallel_drain_worker(&d->workers[i]);
}
static void encore_parallel_stop(EncoreParallelWorker *w) {
    encore_parallel_drain_worker(w);mtx_lock(&w->mutex);w->stop=true;cnd_signal(&w->wake);mtx_unlock(&w->mutex);
    thrd_join(w->thread,NULL);cnd_destroy(&w->wake);cnd_destroy(&w->changed);mtx_destroy(&w->mutex);
}
static uint64_t encore_parallel_cpu_clock(void) {
#if defined(CLOCK_THREAD_CPUTIME_ID)
    struct timespec t;if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t)==0)return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;
#endif
    return encore_timing_clock();
}
static void encore_parallel_sample(EncoreParallelDevice *d,uint64_t id,uint32_t thread,uint64_t start,uint64_t end,uint64_t cpu) {
    if(!id)return;
    mtx_lock(&d->stats_mutex);EncoreParallelSample *s=&d->samples[id%600];
    if(s->id==id) {
        if(!s->start[thread] || start<s->start[thread])s->start[thread]=start;
        if(end>s->end[thread])s->end[thread]=end;s->cpu[thread]+=cpu;s->wall[thread]+=end-start;
    }
    mtx_unlock(&d->stats_mutex);
}
void encore_vk_parallel_frame_begin(size_t device,uint64_t id) {
    EncoreVkDevice *w=(EncoreVkDevice *)device;EncoreParallelDevice *d=w?encore_parallel_device(w->device):NULL;if(!d)return;
    d->frame_id=id;mtx_lock(&d->stats_mutex);EncoreParallelSample *s=&d->samples[id%600];memset(s,0,sizeof(*s));s->id=id;mtx_unlock(&d->stats_mutex);
}
uint64_t encore_vk_parallel_frame_value(size_t device,uint64_t id,uint32_t thread,uint32_t field) {
    EncoreVkDevice *w=(EncoreVkDevice *)device;EncoreParallelDevice *d=w?encore_parallel_device(w->device):NULL;if(!d || thread>=5)return 0;
    mtx_lock(&d->stats_mutex);EncoreParallelSample *s=&d->samples[id%600];uint64_t value=0;
    if(s->id==id) {if(field==0)value=s->start[thread];else if(field==1)value=s->end[thread];else if(field==2)value=s->cpu[thread];else if(field==3)value=s->wall[thread];}
    mtx_unlock(&d->stats_mutex);return value;
}
static void encore_parallel_run_command(EncoreParallelTask *task) {
    EncoreParallelCommand *c=(EncoreParallelCommand *)((char *)task - offsetof(EncoreParallelCommand,task));
    uint64_t started=encore_timing_clock(),cpu=encore_parallel_cpu_clock();
    for(size_t at=0;at<c->length;) {
        EncoreParallelOp *op=(EncoreParallelOp *)(c->bytes+at);
        op->replay((unsigned char *)(op+1));at+=op->bytes;
    }
    uint64_t ended=encore_timing_clock();
    encore_parallel_sample(c->owner,c->frame_id,(uint32_t)(c->worker-c->owner->workers),started,ended,encore_parallel_cpu_clock()-cpu);
    atomic_fetch_add(&c->owner->record_ns,ended-started);
    atomic_fetch_add(&c->owner->bytes,c->length);
}
static void *encore_parallel_append(EncoreParallelCommand *c,size_t size,EncoreParallelReplay replay) {
    size_t bytes=encore_parallel_align(sizeof(EncoreParallelOp)+size);
    if(size>SIZE_MAX-sizeof(EncoreParallelOp)-15 || c->length>SIZE_MAX-bytes){atomic_store(&c->owner->error,-1);return NULL;}
    if(c->capacity<c->length+bytes) {
        size_t cap=c->capacity?c->capacity:4096;
        while(cap<c->length+bytes) {if(cap>SIZE_MAX/2){cap=c->length+bytes;break;}cap*=2;}
        void *next=realloc(c->bytes,cap);if(!next){atomic_store(&c->owner->error,-1);return NULL;}
        c->bytes=next;c->capacity=cap;
    }
    EncoreParallelOp *op=(EncoreParallelOp *)(c->bytes+c->length);
    op->bytes=bytes;op->replay=replay;c->length+=bytes;return op+1;
}
static void encore_parallel_flush_command(EncoreParallelCommand *c) {
    if(!c->length)return;
    encore_parallel_enqueue(c->worker,&c->task);encore_parallel_join(c->worker,&c->task);c->length=0;c->frame_id=c->owner->frame_id;
}
static void encore_parallel_fail(EncoreParallelCommand *c,VkResult error) {
    if(error!=VK_SUCCESS)atomic_store(&c->owner->error,error);
}

/* Native runtime API: configure before creating logical devices. Zero is the
 * compatible synchronous path; a borrowed VkDevice shares its existing owner. */
bool encore_vk_parallel_configure(uint32_t workers) {
    if(encore_parallel_devices)return false;
    encore_parallel_requested=workers>4?4:workers;return true;
}
uint64_t encore_vk_parallel_stat(size_t device,uint32_t index) {
    EncoreVkDevice *wrapper=(EncoreVkDevice *)device;
    EncoreParallelDevice *d=wrapper?encore_parallel_device(wrapper->device):NULL;
    if(!d)return 0;
    switch(index) {
        case 0:return d->worker_count;
        case 1:return atomic_load(&d->record_ns);
        case 2:return atomic_load(&d->submit_ns);
        case 3:return atomic_load(&d->packets);
        case 4:return atomic_load(&d->bytes);
        case 5:return (uint64_t)(int64_t)atomic_load(&d->error);
        default:return 0;
    }
}
#include "parallel_commands.inc"

typedef struct { EncoreParallelCommand *command; VkCommandBufferBeginInfo info; } EncoreParallelBegin;
static void encore_parallel_replay_begin(unsigned char *bytes) {
    EncoreParallelBegin *a=(EncoreParallelBegin *)bytes;
    EncoreParallelCommand *c=a->command;
    if(c->reset)encore_parallel_fail(c,((PFN_vkResetCommandBuffer)c->owner->proc(c->owner->device,"vkResetCommandBuffer"))(c->handle,c->reset_flags));
    encore_parallel_fail(c,((PFN_vkBeginCommandBuffer)c->owner->proc(c->owner->device,"vkBeginCommandBuffer"))(c->handle,&a->info));
}
static VkResult encore_parallel_begin(void *cmd,const VkCommandBufferBeginInfo *info) {
    EncoreParallelCommand *c=encore_parallel_command(cmd);
    if(!c || !info || info->pNext || info->pInheritanceInfo)return -1;
    encore_parallel_join(c->worker,&c->task);c->length=0;c->frame_id=c->owner->frame_id;
    EncoreParallelBegin *a=encore_parallel_append(c,sizeof(*a),encore_parallel_replay_begin);
    if(!a)return -1;a->command=c;a->info=*info;return VK_SUCCESS;
}
static void encore_parallel_replay_end(unsigned char *bytes) {
    EncoreParallelCommand *c=*(EncoreParallelCommand **)bytes;
    encore_parallel_fail(c,((PFN_vkEndCommandBuffer)c->owner->proc(c->owner->device,"vkEndCommandBuffer"))(c->handle));
}
static VkResult encore_parallel_end(void *cmd) {
    EncoreParallelCommand *c=encore_parallel_command(cmd);if(!c)return -1;
    EncoreParallelCommand **p=encore_parallel_append(c,sizeof(c),encore_parallel_replay_end);
    if(!p)return -1;*p=c;return atomic_load(&c->owner->error);
}
static VkResult encore_parallel_reset(void *cmd,uint32_t flags) {
    EncoreParallelCommand *c=encore_parallel_command(cmd);if(!c)return -1;
    encore_parallel_join(c->worker,&c->task);c->reset=true;c->reset_flags=flags;return VK_SUCCESS;
}
/* Render-pass clear arrays originate on the caller stack and are copied. */
typedef struct { EncoreParallelCommand *command; void *cmd; VkRenderPassBeginInfo info; uint32_t contents; } EncoreParallelPass;
static void encore_parallel_replay_pass(unsigned char *bytes) {
    EncoreParallelPass a=*(EncoreParallelPass *)bytes;
    EncoreParallelCommand *c=a.command;
    a.info.pClearValues=(VkClearValue *)(bytes+encore_parallel_align(sizeof(a)));
    ((PFN_vkCmdBeginRenderPass)c->owner->proc(c->owner->device,"vkCmdBeginRenderPass"))(a.cmd,&a.info,a.contents);
}
static void encore_parallel_pass(void *cmd,const VkRenderPassBeginInfo *info,uint32_t contents) {
    EncoreParallelCommand *c=encore_parallel_command(cmd);if(!c || !info)return;
    if(info->pNext){atomic_store(&c->owner->error,-1);return;}
    size_t start=encore_parallel_align(sizeof(EncoreParallelPass));
    EncoreParallelPass *a=encore_parallel_append(c,start+info->clearValueCount*sizeof(VkClearValue),encore_parallel_replay_pass);
    if(!a)return;a->command=c;a->cmd=cmd;a->info=*info;a->contents=contents;
    if(info->clearValueCount)memcpy((char *)a+start,info->pClearValues,info->clearValueCount*sizeof(VkClearValue));
}
/* Acceleration builds are infrequent, already synchronous resource creation.
 * Execute their borrowed nested native structs before returning to the caller. */
typedef struct { EncoreParallelTask task; EncoreParallelCommand *command; uint32_t count; const VkAccelerationStructureBuildGeometryInfoKHR *infos; const VkAccelerationStructureBuildRangeInfoKHR *const *ranges; } EncoreParallelBuild;
static void encore_parallel_run_build(EncoreParallelTask *task) {
    EncoreParallelBuild *a=(EncoreParallelBuild *)task;
    ((PFN_vkCmdBuildAccelerationStructuresKHR)a->command->owner->proc(a->command->owner->device,"vkCmdBuildAccelerationStructuresKHR"))(a->command->handle,a->count,a->infos,a->ranges);
}
static void encore_parallel_build(void *cmd,uint32_t count,const VkAccelerationStructureBuildGeometryInfoKHR *infos,const VkAccelerationStructureBuildRangeInfoKHR *const *ranges) {
    EncoreParallelCommand *c=encore_parallel_command(cmd);if(!c)return;
    encore_parallel_flush_command(c);
    EncoreParallelBuild build={.task={.run=encore_parallel_run_build},.command=c,.count=count,.infos=infos,.ranges=ranges};
    encore_parallel_enqueue(c->worker,&build.task);encore_parallel_join(c->worker,&build.task);
}
struct EncoreParallelQueueTask {
    EncoreParallelTask task;
    EncoreParallelDevice *owner;
    uint32_t kind;
    uint64_t frame_id;
    VkSubmitInfo submit;
    VkPresentInfoKHR present;
    VkSwapchainPresentFenceInfoEXT present_fences;
    void *fence;
    void *commands[32], *waits[8], *signals[8], *fences[8];
    uint32_t stages[8], indices[8];
    uint64_t swapchains[8];
    VkResult results[8], result;
};
static bool encore_parallel_frame_pending(EncoreVkDevice *wrapper,uint64_t id) {
    EncoreParallelDevice *d=wrapper?encore_parallel_device(wrapper->device):NULL;if(!d)return false;
    bool pending=false;mtx_lock(&d->submit.mutex);
    for(unsigned n=0;n<64;++n)if(d->jobs[n].frame_id==id && !d->jobs[n].task.done)pending=true;
    mtx_unlock(&d->submit.mutex);return pending;
}
static void encore_parallel_run_queue(EncoreParallelTask *task) {
    EncoreParallelQueueTask *a=(EncoreParallelQueueTask *)task;
    EncoreParallelDevice *d=a->owner;uint64_t started=encore_timing_clock(),work_started=started,cpu=encore_parallel_cpu_clock();
    if(a->kind==0) {
        // The main thread supplies the command records; this owner never
        // accesses its registry while it can be extended by resource loading.
        for(uint32_t n=0;n<a->submit.commandBufferCount;++n) {
            EncoreParallelCommand *c=(EncoreParallelCommand *)a->commands[n];
            encore_parallel_join(c->worker,&c->task);
        }
        void *commands[32];
        for(uint32_t n=0;n<a->submit.commandBufferCount;++n)commands[n]=((EncoreParallelCommand *)a->commands[n])->handle;
        a->submit.pCommandBuffers=commands;
        work_started=encore_timing_clock();cpu=encore_parallel_cpu_clock();
        a->result=atomic_load(&d->error);
        if(a->result==VK_SUCCESS)a->result=((PFN_vkQueueSubmit)d->proc(d->device,"vkQueueSubmit"))(d->queue,1,&a->submit,a->fence);
        if(a->result!=VK_SUCCESS)atomic_store(&d->error,a->result);
    } else if(a->kind==1) {
        a->result=((PFN_vkQueuePresentKHR)d->proc(d->device,"vkQueuePresentKHR"))(d->queue,&a->present);
        if(a->result!=VK_SUCCESS) {
            if(a->result!=1000001003 && a->present.pNext) {
                /* A rejected WSI present may not signal its present fence.
                 * Drain only on this exceptional resize/error path, then make
                 * that fence logically complete so old targets can retire. */
                VkResult idle=((PFN_vkQueueWaitIdle)d->proc(d->device,"vkQueueWaitIdle"))(d->queue);
                if(idle!=VK_SUCCESS)atomic_store(&d->error,idle);
                else {
                    mtx_lock(&d->stats_mutex);
                    for(uint32_t f=0;f<a->present_fences.swapchainCount;++f) {
                        bool stored=false;
                        for(unsigned n=0;n<16;++n)if(!d->failed_present_fences[n] || d->failed_present_fences[n]==a->fences[f]) {d->failed_present_fences[n]=a->fences[f];stored=true;break;}
                        if(!stored)atomic_store(&d->error,-1);
                    }
                    mtx_unlock(&d->stats_mutex);
                }
            }
        }
    } else {
        a->result=((PFN_vkQueueWaitIdle)d->proc(d->device,"vkQueueWaitIdle"))(d->queue);
    }
    uint64_t ended=encore_timing_clock();
    encore_parallel_sample(d,a->frame_id,4,work_started,ended,encore_parallel_cpu_clock()-cpu);
    atomic_fetch_add(&d->submit_ns,ended-started);
    atomic_fetch_add(&d->packets,1);
}
static EncoreParallelQueueTask *encore_parallel_queue_task(EncoreParallelDevice *d,uint32_t kind) {
    EncoreParallelQueueTask *job=&d->jobs[d->next_job++%64];
    encore_parallel_join(&d->submit,&job->task);
    memset(job,0,sizeof(*job));job->owner=d;job->kind=kind;job->frame_id=d->frame_id;job->task.run=encore_parallel_run_queue;return job;
}
static VkResult encore_parallel_submit(void *queue,uint32_t count,const VkSubmitInfo *infos,void *fence) {
    EncoreParallelDevice *d=encore_parallel_device(queue);
    if(!d || count!=1 || !infos || infos->pNext || infos->commandBufferCount>32 || infos->waitSemaphoreCount>8 || infos->signalSemaphoreCount>8)return -1;
    int error=atomic_load(&d->error);if(error)return error;
    for(uint32_t i=0;i<infos->commandBufferCount;++i)if(!encore_parallel_command(infos->pCommandBuffers[i]))return -1;
    EncoreParallelQueueTask *a=encore_parallel_queue_task(d,0);a->submit=*infos;a->fence=fence;
    for(uint32_t i=0;i<infos->commandBufferCount;++i) {
        EncoreParallelCommand *c=encore_parallel_command(infos->pCommandBuffers[i]);if(!c)return -1;
        a->commands[i]=c;encore_parallel_enqueue(c->worker,&c->task);
    }
    for(uint32_t i=0;i<infos->waitSemaphoreCount;++i){a->waits[i]=((void *const *)infos->pWaitSemaphores)[i];a->stages[i]=infos->pWaitDstStageMask[i];}
    for(uint32_t i=0;i<infos->signalSemaphoreCount;++i)a->signals[i]=((void *const *)infos->pSignalSemaphores)[i];
    a->submit.pWaitSemaphores=a->waits;a->submit.pWaitDstStageMask=a->stages;a->submit.pSignalSemaphores=a->signals;
    encore_parallel_enqueue(&d->submit,&a->task);return VK_SUCCESS;
}
static VkResult encore_parallel_present(void *queue,const VkPresentInfoKHR *info) {
    EncoreParallelDevice *d=encore_parallel_device(queue);
    if(!d || !info || info->swapchainCount>8 || info->waitSemaphoreCount>8)return -1;
    int error=atomic_load(&d->error);if(error)return error;
    if(info->pNext) {
        const VkSwapchainPresentFenceInfoEXT *f=info->pNext;
        if(f->sType!=VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT || f->pNext || f->swapchainCount!=info->swapchainCount)return -1;
    }
    EncoreParallelQueueTask *a=encore_parallel_queue_task(d,1);a->present=*info;
    for(uint32_t i=0;i<info->waitSemaphoreCount;++i)a->waits[i]=info->pWaitSemaphores[i];
    for(uint32_t i=0;i<info->swapchainCount;++i){a->swapchains[i]=info->pSwapchains[i];a->indices[i]=info->pImageIndices[i];}
    a->present.pWaitSemaphores=a->waits;a->present.pSwapchains=a->swapchains;a->present.pImageIndices=a->indices;a->present.pResults=a->results;
    if(info->pNext) {
        const VkSwapchainPresentFenceInfoEXT *f=info->pNext;
        if(f->sType!=VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT || f->pNext || f->swapchainCount!=info->swapchainCount)return -1;
        a->present_fences=*f;
        for(uint32_t i=0;i<f->swapchainCount;++i)a->fences[i]=f->pFences[i];
        a->present_fences.pFences=a->fences;a->present.pNext=&a->present_fences;
    }
    encore_parallel_enqueue(&d->submit,&a->task);
    /* SDL may commit the same Wayland surface while processing input/window
     * changes. Complete this CPU-side WSI call before returning control to
     * that thread; GPU execution remains asynchronous with two slots. */
    encore_parallel_join(&d->submit,&a->task);
    if(info->pResults)for(uint32_t i=0;i<info->swapchainCount;++i)info->pResults[i]=a->results[i];
    return a->result;
}
static VkResult encore_parallel_wait_idle(void *queue) {
    EncoreParallelDevice *d=encore_parallel_device(queue);if(!d)return -1;
    EncoreParallelQueueTask *a=encore_parallel_queue_task(d,2);
    encore_parallel_enqueue(&d->submit,&a->task);encore_parallel_join(&d->submit,&a->task);return a->result;
}
static VkResult encore_parallel_fence_status(void *device,void *fence) {
    EncoreParallelDevice *d=encore_parallel_device(device);if(!d)return -1;
    int error=atomic_load(&d->error);if(error)return error;
    bool failed=false;mtx_lock(&d->stats_mutex);
    for(unsigned n=0;n<16;++n)if(d->failed_present_fences[n]==fence && fence)failed=true;
    mtx_unlock(&d->stats_mutex);if(failed)return VK_SUCCESS;
    return ((PFN_vkGetFenceStatus)d->proc(device,"vkGetFenceStatus"))(device,fence);
}
static void encore_parallel_clear_failed_fence(EncoreParallelDevice *d,void *fence) {
    mtx_lock(&d->stats_mutex);for(unsigned n=0;n<16;++n)if(d->failed_present_fences[n]==fence)d->failed_present_fences[n]=NULL;mtx_unlock(&d->stats_mutex);
}
static VkResult encore_parallel_reset_fences(void *device,uint32_t count,void *const *fences) {
    EncoreParallelDevice *d=encore_parallel_device(device);if(!d)return -1;
    for(uint32_t n=0;n<count;++n)encore_parallel_clear_failed_fence(d,fences[n]);
    return ((PFN_vkResetFences)d->proc(device,"vkResetFences"))(device,count,fences);
}
static VkResult encore_parallel_wait_fences(void *device,uint32_t count,void *const *fences,uint32_t all,uint64_t timeout) {
    EncoreParallelDevice *d=encore_parallel_device(device);if(!d)return -1;
    // Wait timeout includes CPU submission completion rather than accidentally
    // observing an old signalled fence before its queued submission exists.
    encore_parallel_drain(d);int error=atomic_load(&d->error);if(error)return error;
    bool complete=all!=0;
    for(uint32_t n=0;n<count;++n) {
        bool ready=encore_parallel_fence_status(device,fences[n])==VK_SUCCESS;
        if(all)complete=complete&&ready;else complete=complete||ready;
    }
    if(complete)return VK_SUCCESS;
    // Backend callers wait a single fence; multi-fence public waits retain the
    // driver's timeout policy, omitting synthetic completed present fences.
    void *local[16];void **pending=count>16?malloc(count*sizeof(void *)):local;uint32_t pending_count=0;
    if(count && !pending)return -1;
    for(uint32_t n=0;n<count;++n)if(encore_parallel_fence_status(device,fences[n])!=VK_SUCCESS)pending[pending_count++]=fences[n];
    VkResult result=((PFN_vkWaitForFences)d->proc(device,"vkWaitForFences"))(device,pending_count,pending,all,timeout);if(pending!=local)free(pending);return result;
}
static VkResult encore_parallel_acquire(void *device,uint64_t swapchain,uint64_t timeout,void *semaphore,void *fence,uint32_t *image) {
    EncoreParallelDevice *d=encore_parallel_device(device);if(!d)return -1;
    int error=atomic_load(&d->error);if(error)return error;
    return ((PFN_vkAcquireNextImageKHR)d->proc(device,"vkAcquireNextImageKHR"))(device,swapchain,timeout,semaphore,fence,image);
}
static VkResult encore_parallel_allocate(void *device,const VkCommandBufferAllocateInfo *info,void **buffers) {
    EncoreParallelDevice *d=encore_parallel_device(device);if(!d || !info || !buffers || info->level!=0 || info->pNext)return -1;
    PFN_vkCreateCommandPool create=(PFN_vkCreateCommandPool)d->proc(device,"vkCreateCommandPool");
    PFN_vkAllocateCommandBuffers allocate=(PFN_vkAllocateCommandBuffers)d->proc(device,"vkAllocateCommandBuffers");
    for(uint32_t i=0;i<info->commandBufferCount;++i) {
        EncoreParallelCommand *c=calloc(1,sizeof(*c));if(!c)return -1;
        c->owner=d;c->worker=&d->workers[d->next_worker++%d->worker_count];c->task.run=encore_parallel_run_command;c->task.done=true;
        VkCommandPoolCreateInfo pool={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,NULL,2,d->family};
        if(create(device,&pool,NULL,&c->pool)!=VK_SUCCESS){free(c);return -1;}
        VkCommandBufferAllocateInfo one=*info;one.commandPool=c->pool;one.commandBufferCount=1;
        VkResult result=allocate(device,&one,&c->handle);
        if(result!=VK_SUCCESS){((PFN_vkDestroyCommandPool)d->proc(device,"vkDestroyCommandPool"))(device,c->pool,NULL);free(c);return result;}
        c->next=d->commands;d->commands=c;buffers[i]=c->handle;
    }
    return VK_SUCCESS;
}
static void encore_parallel_free_commands(void *device,void *pool,uint32_t count,void *const *buffers) {
    (void)pool;EncoreParallelDevice *d=encore_parallel_device(device);if(!d)return;encore_parallel_drain(d);
    for(uint32_t i=0;i<count;++i) {
        EncoreParallelCommand **link=&d->commands;
        while(*link && (*link)->handle!=buffers[i])link=&(*link)->next;
        if(!*link)continue;
        EncoreParallelCommand *c=*link;*link=c->next;
        ((PFN_vkDestroyCommandPool)d->proc(device,"vkDestroyCommandPool"))(device,c->pool,NULL);
        free(c->bytes);free(c);
    }
}
/* Resource destruction must not overtake queued CPU recording/submission.
 * GPU completion remains governed by the caller's existing slot fences. */
static void encore_parallel_vkDestroyBuffer(void *device,void *handle,const void *allocator) {
    EncoreParallelDevice *d=encore_parallel_device(device);if(!d)return;encore_parallel_drain(d);
    ((void (*)(void *,void *,const void *))d->proc(device,"vkDestroyBuffer"))(device,handle,allocator);
}
static void encore_parallel_vkFreeMemory(void *device,void *handle,const void *allocator) {
    EncoreParallelDevice *d=encore_parallel_device(device);if(!d)return;encore_parallel_drain(d);
    ((void (*)(void *,void *,const void *))d->proc(device,"vkFreeMemory"))(device,handle,allocator);
}
static void encore_parallel_vkDestroyImage(void *device,void *handle,const void *allocator) {
    EncoreParallelDevice *d=encore_parallel_device(device);if(!d)return;encore_parallel_drain(d);
    ((void (*)(void *,void *,const void *))d->proc(device,"vkDestroyImage"))(device,handle,allocator);
}
static void encore_parallel_vkDestroyImageView(void *device,void *handle,const void *allocator) {
    EncoreParallelDevice *d=encore_parallel_device(device);if(!d)return;encore_parallel_drain(d);
    ((void (*)(void *,void *,const void *))d->proc(device,"vkDestroyImageView"))(device,handle,allocator);
}
static void encore_parallel_vkDestroyShaderModule(void *device,void *handle,const void *allocator) {
    EncoreParallelDevice *d=encore_parallel_device(device);if(!d)return;encore_parallel_drain(d);
    ((void (*)(void *,void *,const void *))d->proc(device,"vkDestroyShaderModule"))(device,handle,allocator);
}
static void encore_parallel_vkDestroyPipeline(void *device,void *handle,const void *allocator) {
    EncoreParallelDevice *d=encore_parallel_device(device);if(!d)return;encore_parallel_drain(d);
    ((void (*)(void *,void *,const void *))d->proc(device,"vkDestroyPipeline"))(device,handle,allocator);
}
static void encore_parallel_vkDestroyPipelineLayout(void *device,void *handle,const void *allocator) {
    EncoreParallelDevice *d=encore_parallel_device(device);if(!d)return;encore_parallel_drain(d);
    ((void (*)(void *,void *,const void *))d->proc(device,"vkDestroyPipelineLayout"))(device,handle,allocator);
}
static void encore_parallel_vkDestroyDescriptorSetLayout(void *device,void *handle,const void *allocator) {
    EncoreParallelDevice *d=encore_parallel_device(device);if(!d)return;encore_parallel_drain(d);
    ((void (*)(void *,void *,const void *))d->proc(device,"vkDestroyDescriptorSetLayout"))(device,handle,allocator);
}
static void encore_parallel_vkDestroyDescriptorPool(void *device,void *handle,const void *allocator) {
    EncoreParallelDevice *d=encore_parallel_device(device);if(!d)return;encore_parallel_drain(d);
    ((void (*)(void *,void *,const void *))d->proc(device,"vkDestroyDescriptorPool"))(device,handle,allocator);
}
static void encore_parallel_vkDestroySampler(void *device,void *handle,const void *allocator) {
    EncoreParallelDevice *d=encore_parallel_device(device);if(!d)return;encore_parallel_drain(d);
    ((void (*)(void *,void *,const void *))d->proc(device,"vkDestroySampler"))(device,handle,allocator);
}
static void encore_parallel_vkDestroyRenderPass(void *device,void *handle,const void *allocator) {
    EncoreParallelDevice *d=encore_parallel_device(device);if(!d)return;encore_parallel_drain(d);
    ((void (*)(void *,void *,const void *))d->proc(device,"vkDestroyRenderPass"))(device,handle,allocator);
}
static void encore_parallel_vkDestroyFramebuffer(void *device,void *handle,const void *allocator) {
    EncoreParallelDevice *d=encore_parallel_device(device);if(!d)return;encore_parallel_drain(d);
    ((void (*)(void *,void *,const void *))d->proc(device,"vkDestroyFramebuffer"))(device,handle,allocator);
}
static void encore_parallel_vkDestroyCommandPool(void *device,void *handle,const void *allocator) {
    EncoreParallelDevice *d=encore_parallel_device(device);if(!d)return;encore_parallel_drain(d);
    ((void (*)(void *,void *,const void *))d->proc(device,"vkDestroyCommandPool"))(device,handle,allocator);
}
static void encore_parallel_vkDestroyFence(void *device,void *handle,const void *allocator) {
    EncoreParallelDevice *d=encore_parallel_device(device);if(!d)return;encore_parallel_drain(d);
    encore_parallel_clear_failed_fence(d,handle);
    ((void (*)(void *,void *,const void *))d->proc(device,"vkDestroyFence"))(device,handle,allocator);
}
static void encore_parallel_vkDestroySemaphore(void *device,void *handle,const void *allocator) {
    EncoreParallelDevice *d=encore_parallel_device(device);if(!d)return;encore_parallel_drain(d);
    ((void (*)(void *,void *,const void *))d->proc(device,"vkDestroySemaphore"))(device,handle,allocator);
}
static void encore_parallel_vkDestroyAccelerationStructureKHR(void *device,void *handle,const void *allocator) {
    EncoreParallelDevice *d=encore_parallel_device(device);if(!d)return;encore_parallel_drain(d);
    ((void (*)(void *,void *,const void *))d->proc(device,"vkDestroyAccelerationStructureKHR"))(device,handle,allocator);
}
static void encore_parallel_vkDestroySwapchainKHR(void *device,uint64_t handle,const void *allocator) {
    EncoreParallelDevice *d=encore_parallel_device(device);if(!d)return;encore_parallel_drain(d);
    ((void (*)(void *,uint64_t,const void *))d->proc(device,"vkDestroySwapchainKHR"))(device,handle,allocator);
}
static void encore_parallel_vkDestroyQueryPool(void *device,uint64_t handle,const void *allocator) {
    EncoreParallelDevice *d=encore_parallel_device(device);if(!d)return;encore_parallel_drain(d);
    ((void (*)(void *,uint64_t,const void *))d->proc(device,"vkDestroyQueryPool"))(device,handle,allocator);
}
static void encore_parallel_destroy_device(void *device,const void *allocator) {
    EncoreParallelDevice *d=encore_parallel_device(device);if(!d)return;
    encore_parallel_drain(d);
    for(uint32_t i=0;i<d->worker_count;++i)encore_parallel_stop(&d->workers[i]);
    encore_parallel_stop(&d->submit);
    while(d->commands) {
        EncoreParallelCommand *c=d->commands;d->commands=c->next;
        ((PFN_vkDestroyCommandPool)d->proc(device,"vkDestroyCommandPool"))(device,c->pool,NULL);free(c->bytes);free(c);
    }
    EncoreParallelDevice **link=&encore_parallel_devices;while(*link && *link!=d)link=&(*link)->next;
    if(*link)*link=d->next;
    ((PFN_vkDestroyDevice)d->proc(device,"vkDestroyDevice"))(device,allocator);mtx_destroy(&d->stats_mutex);free(d->jobs);free(d);
}
static VkVoidFunction encore_parallel_proc(void *device,const char *name) {
    EncoreParallelDevice *d=encore_parallel_device(device);if(!d)return NULL;
    VkVoidFunction real=d->proc(device,name);if(!real)return NULL;
    VkVoidFunction command=encore_parallel_command_proc(name);if(command)return command;
    if(!strcmp(name,"vkDestroyBuffer"))return (VkVoidFunction)encore_parallel_vkDestroyBuffer;
    if(!strcmp(name,"vkFreeMemory"))return (VkVoidFunction)encore_parallel_vkFreeMemory;
    if(!strcmp(name,"vkDestroyImage"))return (VkVoidFunction)encore_parallel_vkDestroyImage;
    if(!strcmp(name,"vkDestroyImageView"))return (VkVoidFunction)encore_parallel_vkDestroyImageView;
    if(!strcmp(name,"vkDestroyShaderModule"))return (VkVoidFunction)encore_parallel_vkDestroyShaderModule;
    if(!strcmp(name,"vkDestroyPipeline"))return (VkVoidFunction)encore_parallel_vkDestroyPipeline;
    if(!strcmp(name,"vkDestroyPipelineLayout"))return (VkVoidFunction)encore_parallel_vkDestroyPipelineLayout;
    if(!strcmp(name,"vkDestroyDescriptorSetLayout"))return (VkVoidFunction)encore_parallel_vkDestroyDescriptorSetLayout;
    if(!strcmp(name,"vkDestroyDescriptorPool"))return (VkVoidFunction)encore_parallel_vkDestroyDescriptorPool;
    if(!strcmp(name,"vkDestroySampler"))return (VkVoidFunction)encore_parallel_vkDestroySampler;
    if(!strcmp(name,"vkDestroyRenderPass"))return (VkVoidFunction)encore_parallel_vkDestroyRenderPass;
    if(!strcmp(name,"vkDestroyFramebuffer"))return (VkVoidFunction)encore_parallel_vkDestroyFramebuffer;
    if(!strcmp(name,"vkDestroyCommandPool"))return (VkVoidFunction)encore_parallel_vkDestroyCommandPool;
    if(!strcmp(name,"vkDestroyFence"))return (VkVoidFunction)encore_parallel_vkDestroyFence;
    if(!strcmp(name,"vkDestroySemaphore"))return (VkVoidFunction)encore_parallel_vkDestroySemaphore;
    if(!strcmp(name,"vkDestroyAccelerationStructureKHR"))return (VkVoidFunction)encore_parallel_vkDestroyAccelerationStructureKHR;
    if(!strcmp(name,"vkDestroySwapchainKHR"))return (VkVoidFunction)encore_parallel_vkDestroySwapchainKHR;
    if(!strcmp(name,"vkDestroyQueryPool"))return (VkVoidFunction)encore_parallel_vkDestroyQueryPool;
    if(!strcmp(name,"vkDestroyDevice"))return (VkVoidFunction)encore_parallel_destroy_device;
    if(!strcmp(name,"vkAllocateCommandBuffers"))return (VkVoidFunction)encore_parallel_allocate;
    if(!strcmp(name,"vkFreeCommandBuffers"))return (VkVoidFunction)encore_parallel_free_commands;
    if(!strcmp(name,"vkBeginCommandBuffer"))return (VkVoidFunction)encore_parallel_begin;
    if(!strcmp(name,"vkEndCommandBuffer"))return (VkVoidFunction)encore_parallel_end;
    if(!strcmp(name,"vkResetCommandBuffer"))return (VkVoidFunction)encore_parallel_reset;
    if(!strcmp(name,"vkQueueSubmit"))return (VkVoidFunction)encore_parallel_submit;
    if(!strcmp(name,"vkQueuePresentKHR"))return (VkVoidFunction)encore_parallel_present;
    if(!strcmp(name,"vkQueueWaitIdle"))return (VkVoidFunction)encore_parallel_wait_idle;
    if(!strcmp(name,"vkResetFences"))return (VkVoidFunction)encore_parallel_reset_fences;
    if(!strcmp(name,"vkGetFenceStatus"))return (VkVoidFunction)encore_parallel_fence_status;
    if(!strcmp(name,"vkWaitForFences"))return (VkVoidFunction)encore_parallel_wait_fences;
    if(!strcmp(name,"vkAcquireNextImageKHR"))return (VkVoidFunction)encore_parallel_acquire;
    if(!strcmp(name,"vkCmdBeginRenderPass"))return (VkVoidFunction)encore_parallel_pass;
    if(!strcmp(name,"vkCmdBuildAccelerationStructuresKHR"))return (VkVoidFunction)encore_parallel_build;
    return real;
}
static PFN_vkGetDeviceProcAddr encore_parallel_attach(void *device,uint32_t family,PFN_vkGetDeviceProcAddr proc) {
    if(!encore_parallel_requested)return proc;
    if(encore_parallel_device(device))return encore_parallel_proc;
    EncoreParallelDevice *d=calloc(1,sizeof(*d));if(!d)return proc;
    d->device=device;d->family=family;d->proc=proc;
    PFN_vkGetDeviceQueue get=(PFN_vkGetDeviceQueue)proc(device,"vkGetDeviceQueue");
    if(!get){free(d);return proc;}get(device,family,0,&d->queue);
    d->jobs=calloc(64,sizeof(*d->jobs));if(!d->jobs){free(d);return proc;}
    for(unsigned i=0;i<64;++i)d->jobs[i].task.done=true;
    if(!encore_parallel_worker_start(&d->submit)){free(d->jobs);free(d);return proc;}
    for(uint32_t i=0;i<encore_parallel_requested;++i) {
        if(!encore_parallel_worker_start(&d->workers[i]))break;++d->worker_count;
    }
    if(!d->worker_count){encore_parallel_stop(&d->submit);free(d->jobs);free(d);return proc;}
    if(mtx_init(&d->stats_mutex,mtx_plain)!=thrd_success) {
        for(uint32_t i=0;i<d->worker_count;++i)encore_parallel_stop(&d->workers[i]);
        encore_parallel_stop(&d->submit);free(d->jobs);free(d);return proc;
    }
    d->next=encore_parallel_devices;encore_parallel_devices=d;return encore_parallel_proc;
}
