#include "../vulkan_api.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void *encore_str_from_cstr(const char *s){size_t n=strlen(s);encore_str_object *o=malloc(sizeof(*o)+n+1);assert(o);o->ref_count=1;o->len=n;memcpy(o->data,s,n+1);return o;}
int main(void){
    size_t instance=encore_vk_create(false);assert(encore_vk_available(instance));size_t device=encore_vk_device_create(instance,0);assert(encore_vk_device_available(device));
    uint32_t bindings[32],types[32],stages[32];size_t buffers[32];uint64_t offsets[32]={0},ranges[32];
    size_t buffer=encore_vk_buffer_create(device,16,32,true);assert(encore_vk_buffer_available(buffer));
    for(unsigned i=0;i<32;i++){bindings[i]=i;types[i]=7;stages[i]=32;buffers[i]=buffer;ranges[i]=16;}
    size_t layout=encore_vk_bind_group_layout_create_entries(device,bindings,32,types,32,stages,32);assert(encore_vk_bind_group_layout_available(layout));
    size_t group=encore_vk_buffer_bind_group_create_many(device,layout,bindings,32,buffers,32,offsets,32,ranges,32);assert(encore_vk_bind_group_available(group));
    size_t invalid=encore_vk_bind_group_layout_create_entries(device,bindings,33,types,33,stages,33);assert(!encore_vk_bind_group_layout_available(invalid));encore_vk_bind_group_layout_destroy(invalid);
    invalid=encore_vk_bind_group_layout_create_entries(device,NULL,1,types,1,stages,1);assert(!encore_vk_bind_group_layout_available(invalid));encore_vk_bind_group_layout_destroy(invalid);
    bindings[31]=0;invalid=encore_vk_bind_group_layout_create_entries(device,bindings,32,types,32,stages,32);assert(!encore_vk_bind_group_layout_available(invalid));encore_vk_bind_group_layout_destroy(invalid);
    encore_vk_bind_group_destroy(group);encore_vk_bind_group_layout_destroy(layout);encore_vk_buffer_destroy(buffer);encore_vk_device_destroy(device);encore_vk_destroy(instance);puts("Descriptor capacity: 32 entries, 33 rejected, null/duplicate rejected: PASS");return 0;
}
