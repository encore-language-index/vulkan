/* Real optional SDK dispatch, pixel output, MFG and command replay regression. */
#include "../vulkan_api.h"
#include <math.h>
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
 assert(argc==3 || argc==4);if(argc==4)assert(encore_vk_parallel_configure((unsigned)atoi(argv[3])));size_t c=encore_vk_create(false);assert(encore_vk_available(c));
 size_t d=encore_vk_device_create(c,0);assert(encore_vk_device_available(d));
 unsigned caps=encore_vk_reconstruction_capabilities(d);printf("SDK capabilities: 0x%x\n",caps);fflush(stdout);
 if(!caps){puts("SKIP: optional reconstruction runtime unavailable");return 77;}
 size_t v=shader(d,argv[1],1),frag=shader(d,argv[2],16);
 size_t source=encore_vk_render_target_create(d,640,360,97,1,97,true);
 size_t sink=encore_vk_render_target_create(d,1280,720,97,1,0,false);
 size_t out=0;
 size_t uniform=encore_vk_buffer_create(d,16,16,true);
 size_t layout=encore_vk_bind_group_layout_create(d,0,16);
 size_t group=encore_vk_bind_group_create(d,layout,uniform,0,16);
 size_t p=encore_vk_pipeline_create(d,source,v,frag,layout,true,true,true,0,false,false,false,0,NULL,0,NULL,0,NULL,0);
 size_t mask_target=encore_vk_render_target_create(d,640,360,9,1,0,false);
 assert(encore_vk_swapchain_available(mask_target));
 size_t mask_pipeline=encore_vk_pipeline_create(d,mask_target,v,frag,layout,true,true,true,0,false,false,false,0,NULL,0,NULL,0,NULL,0);
 assert(encore_vk_pipeline_available(mask_pipeline));
 size_t mask_views[2]={0};
 size_t sink_pipeline=encore_vk_pipeline_create(d,sink,v,frag,layout,true,false,false,0,false,false,false,0,NULL,0,NULL,0,NULL,0);
 assert(encore_vk_pipeline_available(sink_pipeline));
 size_t ib=encore_vk_buffer_create(d,12,64,true);unsigned *indices=encore_vk_buffer_map(ib,12);indices[0]=0;indices[1]=1;indices[2]=2;encore_vk_buffer_unmap(ib);
 for(unsigned method=1;method<=5;++method) {
  if(!(caps&(1u<<(method-1))))continue;
  for(unsigned multiplier=2;multiplier<=(method==5?4:2);++multiplier){
   if(method==5 && multiplier>2 && !(caps&(multiplier==3?32:64)))continue;

  out=encore_vk_texture_create(d,method==2 || method==1?1280:640,method==2 || method==1?720:360,1,97,1|2|4|8);
  size_t context=encore_vk_reconstruction_create(d,method,640,360,method==2 || method==1?1280:640,method==2 || method==1?720:360,3);
  if(!encore_vk_reconstruction_available(context)){encore_str e=encore_vk_reconstruction_error(context);fprintf(stderr,"method %u: %s\n",method,e.object?e.object->data:"unknown");return 3;}
  for(unsigned n=0;n<6;++n) {
   float *input=encore_vk_buffer_map(uniform,16);assert(input);input[0]=n*.04f;input[1]=n?.04f:0;input[2]=input[3]=0;encore_vk_buffer_unmap(uniform);
   size_t a=encore_vk_frame_acquire(source);assert(encore_vk_frame_begin_render_pass(a,p,0,0,0,0));
   assert(encore_vk_frame_bind_group(a,group));assert(encore_vk_frame_bind_index_buffer(a,ib));assert(encore_vk_frame_draw_indexed(a,3,1,0,0,0));assert(encore_vk_frame_end_render_pass(a));assert(encore_vk_frame_present(a));encore_vk_frame_destroy(a);
   size_t mask_frame=encore_vk_frame_acquire(mask_target);assert(encore_vk_frame_begin_render_pass(mask_frame,mask_pipeline,0,0,0,0));
   assert(encore_vk_frame_bind_group(mask_frame,group));assert(encore_vk_frame_bind_index_buffer(mask_frame,ib));assert(encore_vk_frame_draw_indexed(mask_frame,3,1,0,0,0));
   assert(encore_vk_frame_end_render_pass(mask_frame));assert(encore_vk_frame_present(mask_frame));encore_vk_frame_destroy(mask_frame);
   size_t mask_slot=n%2;
   if(!mask_views[mask_slot])mask_views[mask_slot]=encore_vk_texture_borrow(d,encore_vk_target_attachment(mask_target,0),640,360,9,5);
   assert(encore_vk_texture_available(mask_views[mask_slot]));
   double previous_position=0;
   for(unsigned generated=1;generated<(method>=4?multiplier:2);++generated){
   size_t b=encore_vk_frame_acquire(sink);float values[112]={0};values[4]=16.667;values[5]=.1;values[6]=10000;values[7]=1.047;values[9]=1;values[10]=n==0;values[11]=n;values[12]=multiplier;values[13]=generated;
   for(unsigned j=0;j<4;++j){values[16+j*5]=1;values[32+j*5]=1;values[48+j*5]=1;values[64+j*5]=1;values[80+j*5]=1;values[96+j*5]=1;}
   if(n==0){float invalid[112];memcpy(invalid,values,sizeof(invalid));invalid[7]=NAN;assert(!encore_vk_reconstruction_record(context,b,source,out,invalid,112));}
   if(!encore_vk_reconstruction_record_images(context,b,source,out,0,mask_views[mask_slot],mask_views[mask_slot],values,112)){encore_str e=encore_vk_reconstruction_error(context);fprintf(stderr,"record method %u: %s\n",method,e.object?e.object->data:"unknown");return 4;}
   assert(encore_vk_frame_begin_render_pass(b,sink_pipeline,0,0,0,0));assert(encore_vk_frame_end_render_pass(b));
   assert(encore_vk_frame_present(b));encore_vk_frame_destroy(b);
   assert(encore_vk_swapchain_wait(sink));
   encore_str e=encore_vk_reconstruction_error(context);if(e.object && e.object->len){fprintf(stderr,"dispatch method %u: %s\n",method,e.object->data);return 5;}
   size_t bytes=encore_vk_texture_readback(out,0,0);assert(bytes>0);
   uint16_t *pixels=malloc(bytes);assert(pixels);
   assert(!encore_vk_texture_readback(out,(uintptr_t)pixels,bytes-1));
   assert(encore_vk_texture_readback(out,(uintptr_t)pixels,bytes)==bytes);
   size_t lit=0;for(size_t q=0;q<bytes/2;q+=4){
    for(unsigned channel=0;channel<3;++channel)assert((pixels[q+channel]&0x7c00)!=0x7c00);
    if((pixels[q]&0x7fff)>0x2a66 && (pixels[q+2]&0x7fff)>0x2a66)++lit;
   }
   assert(lit>bytes/8/4);
   if(method>=4 && n==5){
    uint64_t sum_x=0,count_red=0;for(unsigned y=0;y<360;++y)for(unsigned x=0;x<640;++x){size_t pixel=((size_t)y*640+x)*4;if(pixels[pixel]>0x3800 && pixels[pixel]<0x7c00){sum_x+=x;++count_red;}}
    assert(count_red>1000);double position=(double)sum_x/count_red/640.;
    /* Endpoints of this controlled motion are ~.409 and ~.449. Reject copies
     * of either endpoint and reversed/duplicate MFG ordering. */
    assert(position>.411 && position<.4485 && position>previous_position+.0005);
    previous_position=position;printf("FG %u %ux index %u red centroid %.5f\n",method,multiplier,generated,position);
   }
   free(pixels);
   }
  }
  printf("SDK method %u %ux: GPU dispatch + finite populated pixels PASS\n",method,multiplier);fflush(stdout);encore_vk_reconstruction_destroy(context);encore_vk_texture_destroy(out);out=0;
  }
 }
 if(caps&8){
  size_t failed=encore_vk_reconstruction_create(d,4,640,360,640,360,1);
  assert(encore_vk_reconstruction_available(failed));
  out=encore_vk_texture_create(d,640,360,1,97,15);
  size_t frame=encore_vk_frame_acquire(sink);float invalid[16]={0,0,0,0,16,.1f,10000,1.047f,0,1,1,0,3,1,0,0};
  assert(encore_vk_reconstruction_record(failed,frame,source,out,invalid,16));
  assert(encore_vk_frame_begin_render_pass(frame,sink_pipeline,0,0,0,0));assert(encore_vk_frame_end_render_pass(frame));
  assert(encore_vk_frame_present(frame));encore_vk_frame_destroy(frame);assert(encore_vk_swapchain_wait(sink));
  assert(!encore_vk_reconstruction_available(failed));assert(encore_vk_device_available(d));
  encore_str error=encore_vk_reconstruction_error(failed);assert(error.object && error.object->len);
  uint16_t pixel[640*360*4];assert(encore_vk_texture_readback(out,(uintptr_t)pixel,sizeof(pixel))==sizeof(pixel));
  assert(pixel[0]!=0 && (pixel[0]&0x7c00)!=0x7c00);
  encore_vk_reconstruction_destroy(failed);encore_vk_texture_destroy(out);out=0;
  puts("Optional provider fault: initialized fallback output, device remains available PASS");
 }
 encore_vk_swapchain_wait(source);encore_vk_texture_destroy(out);encore_vk_texture_destroy(mask_views[0]);encore_vk_texture_destroy(mask_views[1]);encore_vk_swapchain_destroy(mask_target);encore_vk_pipeline_destroy(mask_pipeline);encore_vk_pipeline_destroy(sink_pipeline);encore_vk_bind_group_destroy(group);encore_vk_bind_group_layout_destroy(layout);encore_vk_buffer_destroy(uniform);encore_vk_buffer_destroy(ib);encore_vk_pipeline_destroy(p);encore_vk_shader_destroy(frag);encore_vk_shader_destroy(v);encore_vk_swapchain_destroy(source);encore_vk_swapchain_destroy(sink);encore_vk_device_destroy(d);encore_vk_destroy(c);return 0;
}
