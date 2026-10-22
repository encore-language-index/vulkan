/* Actual driver exercise: owned command packets, four recording workers,
 * one queue owner, borrowed device, stack-array capture and clean teardown. */
#include "../backend.c"
#include <assert.h>
void *encore_str_from_cstr(const char *s){return (void *)s;}
int main(void) {
    assert(encore_vk_parallel_configure(4));
    size_t context=encore_vk_create(false);assert(encore_vk_available(context));
    size_t device=encore_vk_device_create(context,0);assert(encore_vk_device_available(device));
    EncoreVkDevice *d=(EncoreVkDevice *)device;
    size_t borrowed=encore_vk_device_borrow(context,(size_t)d->device,d->queue_family);
    assert(encore_vk_device_available(borrowed));
    assert(encore_vk_parallel_stat(device,0)==4);
    size_t targets[4];for(unsigned n=0;n<4;++n){targets[n]=encore_vk_offscreen_create(n&1?borrowed:device,32,32);assert(encore_vk_swapchain_available(targets[n]));}
    for(unsigned frame=0;frame<128;++frame) {
        encore_vk_parallel_frame_begin(device,frame+1);
        for(unsigned n=0;n<4;++n)assert(encore_vk_swapchain_clear(targets[n],0.25f,0.5f,0.75f,1));
        for(unsigned n=0;n<4;++n) {
            assert(encore_vk_swapchain_wait(targets[n]));
            unsigned char *px=(void *)encore_vk_swapchain_pixels(targets[n]);assert(px);
            assert(px[0]>=190 && px[0]<=192 && px[1]>=127 && px[1]<=129 && px[2]>=63 && px[2]<=65 && px[3]==255);
        }
    }
    size_t storage=encore_vk_buffer_create(device,80000,32|2,true);assert(encore_vk_buffer_available(storage));
    float *values=malloc(80000);assert(values);for(unsigned n=0;n<20000;++n)values[n]=(float)n;
    size_t upload_frame=encore_vk_frame_acquire(targets[0]);assert(encore_vk_frame_available(upload_frame));
    assert(encore_vk_frame_update_f32s(upload_frame,storage,0,values,20000));
    assert(!encore_vk_frame_update_f32s(upload_frame,storage,1,values,1));
    assert(!encore_vk_frame_update_f32s(upload_frame,storage,0,values,20001));
    memset(values,0,80000);free(values); // Neither caller storage nor its lifetime escapes.
    assert(encore_vk_frame_present(upload_frame));encore_vk_frame_destroy(upload_frame);assert(encore_vk_swapchain_wait(targets[0]));
    assert(encore_vk_buffer_read_f32(storage,16384*4)==16384.0f);
    assert(encore_vk_buffer_read_f32(storage,19999*4)==19999.0f);encore_vk_buffer_destroy(storage);
    bool measured=false;for(unsigned n=0;n<4;++n)if(encore_vk_parallel_frame_value(device,128,n,0))measured=true;assert(measured);
    assert(encore_vk_parallel_stat(device,3)>=512);
    assert(encore_vk_parallel_stat(device,4)>0);
    assert(encore_vk_parallel_stat(device,5)==0);
    /* Reconfiguration retires a producer before its profiler result is read. */
    assert(encore_vk_timing_support(device));
    assert(encore_vk_timing_begin(device,129));
    size_t timed=encore_vk_frame_acquire(targets[0]);assert(encore_vk_frame_available(timed));
    encore_vk_frame_timing_mark(timed,0,true);encore_vk_frame_timing_mark(timed,0,false);
    assert(encore_vk_frame_present(timed));encore_vk_frame_destroy(timed);
    encore_vk_timing_finish(device,true);
    assert(encore_vk_swapchain_destroy(targets[0]));
    assert(encore_vk_timing_poll(device)==129);assert(encore_vk_timing_value(device,0,0)==1);
    encore_vk_timing_release(device);
    for(unsigned n=1;n<4;++n)assert(encore_vk_swapchain_destroy(targets[n]));
    assert(encore_vk_device_destroy(borrowed));assert(encore_vk_device_destroy(device));
    assert(!encore_parallel_devices);assert(encore_vk_destroy(context));
    puts("parallel command capture, GPU readback and teardown: pass");
}
