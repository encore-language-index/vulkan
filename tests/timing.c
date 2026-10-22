/* Timestamp lifecycle without a driver: asynchronous readiness, wraparound,
 * nested labels, overflow and cancelled captures must never cause waits. */
#include "../backend.c"
#include <assert.h>
void *encore_str_from_cstr(const char *s) { return (void *)s; }
static uint64_t ticks[256], next_tick;
static bool ready;
static VkResult completed_fence=VK_SUCCESS;
static VkResult fence_status(void *d,void *f){(void)d;(void)f;return completed_fence;}
static unsigned resets, reads;
static void reset_queries(void *cmd, uint64_t pool, uint32_t first, uint32_t count) {
    assert(cmd && pool && first == 0 && count == 256); ++resets;
}
static void write_stamp(void *cmd, uint32_t stage, uint64_t pool, uint32_t query) {
    assert(cmd && pool && stage == 0x2000 && query < 256); ticks[query] = next_tick; next_tick += 10;
}
static VkResult read_queries(void *device, uint64_t pool, uint32_t first, uint32_t count, size_t bytes, void *out, uint64_t stride, uint32_t flags) {
    assert(device && pool && !first && bytes >= count * stride && stride == 16);
    assert(flags == (1|4)); /* Never WAIT_BIT. */
    ++reads;
    uint64_t (*values)[2] = out;
    for (unsigned i=0;i<count;++i) { values[i][0]=ticks[i];values[i][1]=ready; }
    return ready ? VK_SUCCESS : VK_NOT_READY;
}
int main(void) {
    EncoreVkDevice d={0};d.device=(void *)1;d.available=true;
    EncoreVkSwapchain swap={0};swap.owner=&d;
    EncoreVkFrame frame={0};frame.owner=&swap;frame.command_buffer=(void *)2;frame.recording=true;
    EncoreTimingStream stream={0};stream.device=d.device;stream.owner=&d;stream.supported=true;
    stream.active=stream.ready=-1;stream.bits=8;stream.period=2;stream.reset=reset_queries;stream.write=write_stamp;stream.read=read_queries;
    for(unsigned n=0;n<8;++n)stream.slots[n].pool=n+1;
    encore_timing_streams=&stream;
    assert(encore_vk_timing_begin((size_t)&d,1));next_tick=250;
    encore_vk_frame_timing_mark((size_t)&frame,0,true);
    frame.in_render_pass=true;
    encore_vk_frame_timing_mark((size_t)&frame,1,true);
    encore_vk_frame_timing_mark((size_t)&frame,1,false);
    encore_vk_frame_timing_mark((size_t)&frame,0,false);
    encore_vk_timing_finish((size_t)&d,true);
    assert(!encore_vk_timing_poll((size_t)&d) && reads==1 && resets==1);
    ready=true;assert(encore_vk_timing_poll((size_t)&d)==1);
    assert(encore_vk_timing_value((size_t)&d,0,0)==1);
    assert(encore_vk_timing_value((size_t)&d,0,4)==60);
    assert(encore_vk_timing_value((size_t)&d,1,3)==20);
    assert(encore_vk_timing_value((size_t)&d,1,4)==20);
    assert(encore_vk_timing_value((size_t)&d,1,5)==1);
    encore_vk_timing_release((size_t)&d);
    frame.in_render_pass=false;
    /* Old query availability is not this frame's completion. */
    d.get_fence_status=fence_status;swap.fences[0]=(void *)3;
    assert(encore_vk_timing_begin((size_t)&d,100));
    encore_vk_frame_timing_mark((size_t)&frame,0,true);encore_vk_frame_timing_mark((size_t)&frame,0,false);
    encore_vk_timing_finish((size_t)&d,true);completed_fence=VK_NOT_READY;
    assert(!encore_vk_timing_poll((size_t)&d));completed_fence=VK_SUCCESS;
    assert(encore_vk_timing_poll((size_t)&d)==100);encore_vk_timing_release((size_t)&d);
    /* A completed target can retire before the UI collects its sample. */
    assert(encore_vk_timing_begin((size_t)&d,101));
    encore_vk_frame_timing_mark((size_t)&frame,0,true);encore_vk_frame_timing_mark((size_t)&frame,0,false);
    encore_vk_timing_finish((size_t)&d,true);
    encore_timing_target_retired(&swap);
    for(unsigned n=0;n<8;++n)assert(stream.slots[n].completion_fence==NULL);
    completed_fence=VK_NOT_READY; /* A destroyed fence must never be polled. */
    assert(encore_vk_timing_poll((size_t)&d)==101);encore_vk_timing_release((size_t)&d);
    completed_fence=VK_SUCCESS;
    swap.fences[0]=NULL;
    assert(encore_vk_timing_begin((size_t)&d,2));
    for(unsigned n=0;n<129;++n){encore_vk_frame_timing_mark((size_t)&frame,5,true);encore_vk_frame_timing_mark((size_t)&frame,5,false);}
    encore_vk_timing_finish((size_t)&d,true);
    assert(encore_vk_timing_poll((size_t)&d)==2);
    assert(encore_vk_timing_value((size_t)&d,0,0)==2 && encore_vk_timing_value((size_t)&d,0,6)==1);
    encore_vk_timing_release((size_t)&d);
    assert(encore_vk_timing_begin((size_t)&d,3));encore_vk_timing_finish((size_t)&d,false);
    assert(encore_vk_timing_poll((size_t)&d)==3 && encore_vk_timing_value((size_t)&d,0,0)==2);
    encore_vk_timing_release((size_t)&d);
    /* Every in-flight slot remains owned until its timestamps are ready. */
    ready=false;
    for(unsigned n=0;n<8;++n){assert(encore_vk_timing_begin((size_t)&d,10+n));encore_vk_frame_timing_mark((size_t)&frame,0,true);encore_vk_frame_timing_mark((size_t)&frame,0,false);encore_vk_timing_finish((size_t)&d,true);}
    assert(!encore_vk_timing_begin((size_t)&d,99));
    assert(!encore_vk_timing_poll((size_t)&d));
    encore_timing_streams=NULL;
    puts("timestamp lifecycle: pass");return 0;
}
