/* Driverless placement and fallback checks for coherent mapped GPU buffers. */
#include "../backend.c"
#include <assert.h>
void *encore_str_from_cstr(const char *s) { return (void *)s; }
static unsigned chosen, allocations, maps, binds, frees, unmapped;
static int failure;
static char mapping[64];
static VkResult create_buffer(void *d,const VkBufferCreateInfo *i,const void *a,void **b) {
    (void)d;(void)i;(void)a;*b=(void *)1;return VK_SUCCESS;
}
static void requirements(void *d,void *b,VkMemoryRequirements *r) {
    (void)d;(void)b;*r=(VkMemoryRequirements){64,4,7};
}
static VkResult allocate(void *d,const VkMemoryAllocateInfo *i,const void *a,void **m) {
    (void)d;(void)a;chosen=i->memoryTypeIndex;allocations++;
    if(failure==1 && chosen==2)return -2;
    *m=(void *)(uintptr_t)(chosen+1);return VK_SUCCESS;
}
static VkResult map(void *d,void *m,uint64_t o,uint64_t n,uint32_t f,void **p) {
    (void)d;(void)o;(void)n;(void)f;maps++;
    if(failure==2 && m==(void *)3)return -5;
    *p=mapping;return VK_SUCCESS;
}
static VkResult bind(void *d,void *b,void *m,uint64_t o) {
    (void)d;(void)b;(void)o;binds++;
    if(failure==3 && m==(void *)3)return -2;
    return VK_SUCCESS;
}
static void free_memory(void *d,void *m,const void *a) {(void)d;(void)m;(void)a;frees++;}
static void unmap(void *d,void *m) {(void)d;(void)m;unmapped++;}
static void check(EncoreVkDevice *d,uint32_t usage,unsigned expected) {
    EncoreVkBuffer *b=(void *)encore_vk_buffer_create((size_t)d,64,usage,true);
    assert(b && b->available && b->mapped==mapping && b->memory_type==expected);
    free(b);
}
int main(void) {
    EncoreVkDevice d={0};d.available=true;
    d.create_buffer=create_buffer;d.get_buffer_requirements=requirements;
    d.allocate_memory=allocate;d.map_memory=map;d.bind_buffer_memory=bind;
    d.free_memory=free_memory;d.unmap_memory=unmap;
    d.memory_properties.memoryTypeCount=3;
    d.memory_properties.memoryTypes[0].propertyFlags=6;
    d.memory_properties.memoryTypes[1].propertyFlags=14;
    d.memory_properties.memoryTypes[2].propertyFlags=7;
    check(&d,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,2);
    check(&d,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,2);
    check(&d,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,1);
    check(&d,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,1);
    for(failure=1;failure<=3;failure++) {
        allocations=maps=binds=frees=unmapped=0;
        check(&d,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,1);
        assert(allocations==2);
        assert(frees==(failure==1?0:1));
        assert(unmapped==(failure==3?1:0));
    }
    failure=0;d.memory_properties.memoryTypeCount=2;
    check(&d,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,1);
    d.memory_properties.memoryTypeCount=1;
    check(&d,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,0);
    puts("Buffer placement: mapped VRAM, cached host readback and allocation/map/bind fallback passed");
}
