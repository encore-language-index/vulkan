/* Reference-counted completion records outlive frame wrappers, recycled fences
 * and retired targets. Tokens are allocated only when a caller asks for one. */
#include <stdatomic.h>
#include <threads.h>
typedef struct EncoreVkCompletion {
    atomic_uint references;
    mtx_t mutex;
    /* 1 recording, 2 submitted, 3 completed, 4 cancelled (never submitted) */
    unsigned state;
    void *device, *fence;
    PFN_vkGetFenceStatus poll;
} EncoreVkCompletion;
size_t encore_vk_completion_clone(size_t handle) {
    EncoreVkCompletion *c=(EncoreVkCompletion *)handle;
    if(c)atomic_fetch_add_explicit(&c->references,1,memory_order_relaxed);
    return handle;
}
bool encore_vk_completion_destroy(size_t handle) {
    EncoreVkCompletion *c=(EncoreVkCompletion *)handle;if(!c)return false;
    if(atomic_fetch_sub_explicit(&c->references,1,memory_order_acq_rel)==1){mtx_destroy(&c->mutex);free(c);}
    return true;
}
/* Detach under the same lock as polling before a target resets/destroys a fence. */
static void encore_completion_finish(EncoreVkCompletion *c,bool success) {
    if(!c)return;mtx_lock(&c->mutex);
    if(c->state<3)c->state=success && c->state==2?3:4;
    c->device=NULL;c->fence=NULL;c->poll=NULL;mtx_unlock(&c->mutex);
}
static void encore_completion_slot_retired(EncoreVkSwapchain *target,unsigned slot) {
    EncoreVkCompletion *c=target->completions[slot];if(!c)return;
    encore_completion_finish(c,true);target->completions[slot]=NULL;
    encore_vk_completion_destroy((size_t)c);
}
static void encore_completion_target_retired(EncoreVkSwapchain *target) {
    if(target)for(unsigned slot=0;slot<2;slot++)encore_completion_slot_retired(target,slot);
}
size_t encore_vk_frame_completion(size_t handle) {
    EncoreVkFrame *f=(EncoreVkFrame *)handle;
    if(!f || !f->recording || f->complete || !f->owner)return 0;
    EncoreVkCompletion *c=f->owner->completions[f->frame_index];
    if(!c){
        c=calloc(1,sizeof *c);if(!c)return 0;
        if(mtx_init(&c->mutex,mtx_plain)!=thrd_success){free(c);return 0;}
        atomic_init(&c->references,1);c->state=1;
        f->owner->completions[f->frame_index]=c;
    }
    return encore_vk_completion_clone((size_t)c);
}
static void encore_completion_submitted(EncoreVkFrame *f) {
    EncoreVkCompletion *c=f->owner->completions[f->frame_index];if(!c)return;
    mtx_lock(&c->mutex);c->device=f->owner->owner->device;
    c->fence=f->owner->fences[f->frame_index];c->poll=f->owner->owner->get_fence_status;
    c->state=2;mtx_unlock(&c->mutex);
}
/* 0 invalid, 1 pending, 2 complete, 3 cancelled, 4 polling fault.
 * A query fault does not prove completion: retain the fence and permit retry. */
uint32_t encore_vk_completion_status(size_t handle) {
    EncoreVkCompletion *c=(EncoreVkCompletion *)handle;if(!c)return 0;
    mtx_lock(&c->mutex);
    if(c->state==2){
        VkResult result=c->poll(c->device,c->fence);
        if(result==VK_SUCCESS)c->state=3;
        else if(result!=VK_NOT_READY){mtx_unlock(&c->mutex);return 4;}
        if(c->state==3){c->device=NULL;c->fence=NULL;c->poll=NULL;}
    }
    unsigned state=c->state;mtx_unlock(&c->mutex);
    return state<3?1:state==3?2:3;
}

/* A recording producer is not ordered before a different command buffer yet.
 * This query does not poll or wait, and a polling fault still counts as submitted. */
bool encore_vk_completion_submitted(size_t handle) {
    EncoreVkCompletion *c=(EncoreVkCompletion *)handle;if(!c)return false;
    mtx_lock(&c->mutex);bool submitted=c->state==2 || c->state==3;mtx_unlock(&c->mutex);
    return submitted;
}
