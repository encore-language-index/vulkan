#define _POSIX_C_SOURCE 200809L
#include "../vulkan_api.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
void *encore_str_from_cstr(const char *s){size_t n=strlen(s);encore_str_object *o=malloc(sizeof(*o)+n+1);assert(o);o->ref_count=1;o->len=n;memcpy(o->data,s,n+1);return o;}
static size_t tokens[50];static atomic_uint published;static atomic_bool stopped;
static int poll_thread(void *ignored){(void)ignored;while(!atomic_load(&stopped)){unsigned count=atomic_load(&published);for(unsigned i=0;i<count;i++){unsigned s=encore_vk_completion_status(tokens[i]);assert(s==1||s==2);}thrd_yield();}return 0;}
int main(void){
    assert(encore_vk_parallel_configure(2));
    size_t instance=encore_vk_create(false);assert(encore_vk_available(instance));size_t device=encore_vk_device_create(instance,0);assert(encore_vk_device_available(device));
    size_t target=encore_vk_offscreen_create(device,8,8);assert(encore_vk_swapchain_available(target));
    size_t source=encore_vk_buffer_create(device,64,3,false),packets[2]={encore_vk_buffer_create(device,64,3,true),encore_vk_buffer_create(device,64,3,true)};
    assert(encore_vk_buffer_available(source)&&encore_vk_buffer_available(packets[0])&&encore_vk_buffer_available(packets[1]));
    assert(!encore_vk_frame_completion(0));assert(!encore_vk_completion_clone(0));assert(encore_vk_completion_status(0)==0);assert(!encore_vk_completion_destroy(0));
    atomic_init(&published,0);atomic_init(&stopped,false);thrd_t thread;assert(thrd_create(&thread,poll_thread,NULL)==thrd_success);unsigned expected[2]={0},observed=0;
    for(unsigned serial=1;serial<=50;serial++){
        size_t frame=encore_vk_frame_acquire(target);assert(encore_vk_frame_available(frame));unsigned slot=encore_vk_frame_slot(frame);assert(slot<2);
        if(expected[slot]){assert(encore_vk_completion_status(tokens[expected[slot]-1])==2);float values[16];assert(encore_vk_buffer_read_f32s(packets[slot],0,values,16));for(unsigned i=0;i<16;i++)assert(values[i]==expected[slot]);observed++;}
        tokens[serial-1]=encore_vk_frame_completion(frame);assert(tokens[serial-1]);assert(encore_vk_completion_status(tokens[serial-1])==1);assert(!encore_vk_completion_submitted(tokens[serial-1]));atomic_store(&published,serial);
        float values[16];for(unsigned i=0;i<16;i++)values[i]=(float)serial;
        assert(encore_vk_frame_update_f32s(frame,source,0,values,16));assert(encore_vk_frame_copy_buffer(frame,source,packets[slot],0,0,64));assert(encore_vk_frame_present(frame));assert(encore_vk_completion_submitted(tokens[serial-1]));assert(!encore_vk_frame_completion(frame));assert(encore_vk_frame_destroy(frame));expected[slot]=serial;
    }
    size_t clone=encore_vk_completion_clone(tokens[49]);assert(clone==tokens[49]);
    assert(encore_vk_swapchain_destroy(target));assert(encore_vk_completion_status(clone)==2);
    encore_vk_buffer_destroy(source);encore_vk_buffer_destroy(packets[0]);encore_vk_buffer_destroy(packets[1]);assert(encore_vk_device_destroy(device));assert(encore_vk_destroy(instance));
    atomic_store(&stopped,true);int result;thrd_join(thread,&result);assert(!result);
    for(unsigned i=0;i<50;i++){assert(encore_vk_completion_status(tokens[i])==2);assert(encore_vk_completion_destroy(tokens[i]));}
    assert(encore_vk_completion_status(clone)==2);assert(encore_vk_completion_destroy(clone));
    assert(observed==48);puts("Completion tokens: 50 frames, 48 async readbacks, concurrent retirement, cloned lifetime: PASS");return 0;
}
