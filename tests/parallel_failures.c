/* No driver: rejected WSI presents and asynchronous failures must retire,
 * and malformed packets must not consume a queue slot forever. */
#include "../backend.c"
#include <assert.h>
void *encore_str_from_cstr(const char *s){return (void *)s;}
static unsigned idles,resets,presents;
static thrd_t main_thread;
static VkResult present(void *q,const VkPresentInfoKHR *p){assert(q && p);assert(!thrd_equal(thrd_current(),main_thread));++presents;return VK_ERROR_OUT_OF_DATE_KHR;}
static VkResult idle(void *q){assert(q);++idles;return VK_SUCCESS;}
static VkResult status(void *d,void *f){assert(d && f);return VK_NOT_READY;}
static VkResult reset(void *d,uint32_t n,void *const *f){assert(d && n==1 && f[0]);++resets;return VK_SUCCESS;}
static VkVoidFunction proc(void *d,const char *s){
 (void)d;
 if(!strcmp(s,"vkQueuePresentKHR"))return(VkVoidFunction)present;
 if(!strcmp(s,"vkQueueWaitIdle"))return(VkVoidFunction)idle;
 if(!strcmp(s,"vkGetFenceStatus"))return(VkVoidFunction)status;
 if(!strcmp(s,"vkResetFences"))return(VkVoidFunction)reset;
 assert(!"unexpected driver call");return NULL;
}
int main(void){
 main_thread=thrd_current();
 EncoreParallelDevice d={0};d.device=(void *)1;d.queue=(void *)2;d.proc=proc;
 assert(mtx_init(&d.stats_mutex,mtx_plain)==thrd_success);assert(encore_parallel_worker_start(&d.submit));
 d.jobs=calloc(64,sizeof(*d.jobs));assert(d.jobs);for(unsigned i=0;i<64;++i)d.jobs[i].task.done=true;
 encore_parallel_devices=&d;
 void *fence=(void *)3;uint64_t swapchain=4;uint32_t image=0;
 VkSwapchainPresentFenceInfoEXT f={VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT,NULL,1,&fence};
 VkPresentInfoKHR p={VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,&f,0,NULL,1,&swapchain,&image,NULL};
 for(unsigned i=0;i<130;++i){
  VkSwapchainPresentFenceInfoEXT invalid=f;invalid.sType=0;p.pNext=&invalid;
  assert(encore_parallel_present(d.queue,&p)!=VK_SUCCESS);
  p.pNext=&f;assert(encore_parallel_present(d.queue,&p)==VK_ERROR_OUT_OF_DATE_KHR);assert(presents==i+1);encore_parallel_drain(&d);
  assert(encore_parallel_fence_status(d.device,fence)==VK_SUCCESS);
  assert(encore_parallel_reset_fences(d.device,1,&fence)==VK_SUCCESS);
  assert(encore_parallel_fence_status(d.device,fence)==VK_NOT_READY);
 }
 assert(idles==130 && resets==130);
 atomic_store(&d.error,-4);assert(encore_parallel_fence_status(d.device,fence)==-4);
 encore_parallel_stop(&d.submit);mtx_destroy(&d.stats_mutex);free(d.jobs);encore_parallel_devices=NULL;
 puts("parallel errors: rejected present, reusable fences and malformed packets passed");
}
