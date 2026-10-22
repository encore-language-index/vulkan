/* Configurable attachments/resolve regression; link only against opaque Vulkan. */
#include "../vulkan_api.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void *encore_str_from_cstr(const char *s) {
    size_t n=strlen(s); encore_str_object *o=malloc(sizeof(*o)+n+1); assert(o);
    o->ref_count=1;o->len=n;memcpy(o->data,s,n+1);return o;
}
static size_t shader(size_t d,const char *path,uint32_t stage) {
    FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    uint32_t *data=malloc((size_t)n);assert(fread(data,1,(size_t)n,f)==(size_t)n);fclose(f);
    size_t s=encore_vk_shader_create(d,data,(size_t)n/4,stage);free(data);assert(encore_vk_shader_available(s));return s;
}
int main(int argc,char **argv) {
    assert(argc==3);
    size_t c=encore_vk_create(false);assert(encore_vk_available(c));
    size_t d=encore_vk_device_create(c,0);assert(encore_vk_device_available(d));
    size_t v=shader(d,argv[1],1),f=shader(d,argv[2],16);
    uint32_t mask=encore_vk_target_sample_counts(d,97);assert(mask&1);printf("Supported RGBA16F/D32 sample mask: %u\n",mask);
    size_t ib=encore_vk_buffer_create(d,12,64,true);
    uint32_t *indices=encore_vk_buffer_map(ib,12);assert(indices);indices[0]=0;indices[1]=1;indices[2]=2;encore_vk_buffer_unmap(ib);
    for (int variant=0;variant<5;++variant) {
        uint32_t samples=variant==1?2:variant==2?4:1;
        if (!(mask&samples)) continue;
        size_t t=encore_vk_render_target_create(d,64,64,97,samples,variant==3?97:0,variant==4);
        if(!encore_vk_swapchain_available(t)) {fprintf(stderr,"%s\n",encore_vk_swapchain_error_cstr(t));abort();}
        /* A sampled view must be constructible before the first frame/present. */
        assert(encore_vk_target_attachment(t,0));
        assert(encore_vk_target_render_signature(t)==(97ull|((uint64_t)samples<<16)|((uint64_t)(variant==3?97:0)<<32)));
        size_t p=encore_vk_pipeline_create(d,t,v,f,0,true,false,false,0,false,false,false,0,NULL,0,NULL,0,NULL,0);
        if(!encore_vk_pipeline_available(p)){fprintf(stderr,"%s\n",encore_vk_pipeline_error_cstr(p));abort();}
        for(int frame=0;frame<6;++frame) {
            size_t fr=encore_vk_frame_acquire(t);assert(encore_vk_frame_available(fr));
            assert(encore_vk_frame_begin_render_pass(fr,p,0,0,0,0));
            assert(encore_vk_frame_bind_index_buffer(fr,ib));
            assert(encore_vk_frame_draw_indexed(fr,3,1,0,0,0));
            assert(encore_vk_frame_end_render_pass(fr));assert(encore_vk_frame_present(fr));encore_vk_frame_destroy(fr);
        }
        assert(encore_vk_swapchain_wait(t));assert(encore_vk_target_attachment(t,0));
        size_t bytes=encore_vk_target_readback(t,0,0,0);assert(bytes==64u*64u*8u);
        uint16_t *pixels=malloc(bytes);assert(pixels);
        assert(!encore_vk_target_readback(t,0,(uintptr_t)pixels,bytes-1));
        assert(encore_vk_target_readback(t,0,(uintptr_t)pixels,bytes)==bytes);
        assert(pixels[0]==0x3400 && pixels[1]==0x3800 && pixels[2]==0x3a00 && pixels[3]==0x3c00);
        if (variant==3) {
            assert(encore_vk_target_readback(t,1,(uintptr_t)pixels,bytes)==bytes);
            assert(pixels[2]==0x4000 && pixels[3]==0x4000);
        }
        free(pixels);
        assert((encore_vk_target_attachment(t,1)!=0)==(variant==3));
        assert((encore_vk_target_attachment(t,2)!=0)==(variant==4));
        encore_vk_swapchain_destroy(t);encore_vk_pipeline_destroy(p);
    }
    encore_vk_buffer_destroy(ib);encore_vk_shader_destroy(f);encore_vk_shader_destroy(v);encore_vk_device_destroy(d);encore_vk_destroy(c);
    puts("Render targets: single sample, 2x/4x resolve, MRT and sampled depth passed");
}
