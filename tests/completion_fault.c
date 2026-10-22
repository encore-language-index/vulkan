#include "../runtime.c"
#include <assert.h>
static int next_result,polls,submits;
static VkResult fake_poll(void *device,void *fence){assert(device==(void*)1&&fence==(void*)2);polls++;return next_result;}
static VkResult end_fail(void *command){(void)command;return -1;}
static VkResult end_ok(void *command){(void)command;return 0;}
static VkResult submit_ok(void *queue,uint32_t count,const VkSubmitInfo *s,void *fence){(void)queue;(void)count;(void)s;(void)fence;submits++;return 0;}
static int resets;
static VkResult reset_second_fail(void *device,uint32_t count,void *const *fences){(void)device;(void)count;(void)fences;return ++resets==2?-1:0;}
void *encore_str_from_cstr(const char *value){(void)value;return NULL;}
int main(void){
    EncoreVkCompletion *c=calloc(1,sizeof(*c));assert(c);assert(mtx_init(&c->mutex,mtx_plain)==thrd_success);atomic_init(&c->references,1);c->state=1;c->device=(void*)1;c->fence=(void*)2;c->poll=fake_poll;
    next_result=VK_SUCCESS;assert(!encore_vk_completion_submitted(0));assert(!encore_vk_completion_submitted((size_t)c));assert(encore_vk_completion_status((size_t)c)==1&&polls==0);c->state=2;assert(encore_vk_completion_submitted((size_t)c));next_result=-1;assert(encore_vk_completion_status((size_t)c)==4&&c->state==2&&c->fence==(void*)2);next_result=VK_NOT_READY;assert(encore_vk_completion_status((size_t)c)==1);next_result=VK_SUCCESS;assert(encore_vk_completion_status((size_t)c)==2&&!c->fence);assert(polls==3&&encore_vk_completion_submitted((size_t)c));encore_vk_completion_destroy((size_t)c);
    EncoreVkDevice device={0};device.end_command_buffer=end_fail;EncoreVkSwapchain owner={0};owner.owner=&device;EncoreVkFrame *frame=calloc(1,sizeof(*frame));assert(frame);frame->owner=&owner;frame->recording=true;
    size_t token=encore_vk_frame_completion((size_t)frame);assert(token&&encore_vk_completion_status(token)==1);assert(!encore_vk_frame_present((size_t)frame));assert(encore_vk_completion_status(token)==1);assert(encore_vk_frame_destroy((size_t)frame));assert(encore_vk_completion_status(token)==3&&!encore_vk_completion_submitted(token));encore_completion_target_retired(&owner);assert(encore_vk_completion_status(token)==3);encore_vk_completion_destroy(token);
    device.device=(void*)1;device.end_command_buffer=end_ok;device.queue_submit=submit_ok;device.get_fence_status=fake_poll;owner.reset_fences=reset_second_fail;owner.fences[0]=(void*)2;
    frame=calloc(1,sizeof(*frame));assert(frame);frame->owner=&owner;frame->recording=true;token=encore_vk_frame_completion((size_t)frame);next_result=VK_NOT_READY;
    assert(!encore_vk_frame_present((size_t)frame));assert(submits==1&&frame->submitted);assert(encore_vk_completion_status(token)==1);assert(encore_vk_frame_destroy((size_t)frame));assert(submits==1);next_result=VK_SUCCESS;assert(encore_vk_completion_status(token)==2);encore_completion_target_retired(&owner);encore_vk_completion_destroy(token);
    puts("Completion faults: retry polling, cancel unsubmitted, no duplicate submit after presentation failure: PASS");return 0;
}
