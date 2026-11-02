/* Actual GPU: transfer-produced geometry -> frame BLAS -> frame TLAS, strict
 * build budgets, early-free rejection and post-fence scratch/input retirement. */
#include "../backend.c"
#include <assert.h>
void *encore_str_from_cstr(const char *s){size_t n=strlen(s);encore_str_object *o=malloc(sizeof(*o)+n+1);assert(o);o->ref_count=1;o->len=n;memcpy(o->data,s,n+1);return o;}
int main(int argc,char **argv) {
    assert(argc==2);
    assert(encore_vk_parallel_configure(2));
    size_t context=encore_vk_create(false);assert(encore_vk_available(context));
    size_t adapter=0;bool supported=false;
    for(size_t i=0;i<encore_vk_adapter_count(context);i++)if(encore_vk_adapter_ray_query(context,i)){adapter=i;supported=true;break;}
    if(!supported){puts("SKIP: ray query unavailable");return 77;}
    size_t device=encore_vk_device_create_features(context,adapter,ENCORE_GPU_FEATURE_RAY_QUERY,0);assert(encore_vk_device_available(device));
    EncoreVkDevice *d=(EncoreVkDevice *)device;
    if(!(d->enabled_features&ENCORE_GPU_FEATURE_RAY_QUERY)){puts("SKIP: ray query unavailable");return 77;}
    size_t target=encore_vk_offscreen_create(device,8,8);assert(encore_vk_swapchain_available(target));
    uint32_t usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    size_t vertices=encore_vk_buffer_create(device,36,usage,false),indices=encore_vk_buffer_create(device,12,usage,false);
    assert(encore_vk_buffer_available(vertices)&&encore_vk_buffer_available(indices));
    size_t frame=encore_vk_frame_acquire(target);assert(encore_vk_frame_available(frame));
    float points[]={0,0,0,1,0,0,0,1,0};uint32_t triangles[]={0,1,2};
    assert(encore_vk_frame_update_f32s(frame,vertices,0,points,9));
    assert(encore_vk_frame_update_u32s(frame,indices,0,triangles,3));
    const uint64_t limit=16u*1024u*1024u;
    size_t rejected=encore_vk_frame_build_triangle_blas(frame,vertices,12,3,indices,3,1);
    assert(rejected&&!encore_vk_acceleration_structure_available(rejected));
    assert(!encore_vk_acceleration_structure_memory_bytes(rejected));assert(encore_vk_acceleration_structure_destroy(rejected));
    size_t invalid=encore_vk_frame_build_triangle_blas(frame,vertices,13,3,indices,3,limit);
    assert(invalid&&!encore_vk_acceleration_structure_available(invalid));assert(encore_vk_acceleration_structure_destroy(invalid));
    size_t blas=encore_vk_frame_build_triangle_blas(frame,vertices,12,3,indices,3,limit);
    if(!encore_vk_acceleration_structure_available(blas)){encore_str error=encore_vk_acceleration_structure_error(blas);fprintf(stderr,"BLAS: %.*s\n",(int)error.object->len,error.object->data);return 1;}
    uint64_t blas_bytes=encore_vk_acceleration_structure_memory_bytes(blas);assert(blas_bytes>0&&blas_bytes<=limit);
    assert(!encore_vk_acceleration_structure_destroy(blas));assert(!encore_vk_acceleration_structure_release_build_resources(blas));
    float transform[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    size_t other_target=encore_vk_offscreen_create(device,8,8),other_frame=encore_vk_frame_acquire(other_target);assert(encore_vk_frame_available(other_frame));
    size_t unordered=encore_vk_frame_build_tlas(other_frame,&blas,1,transform,16,limit);
    assert(unordered&&!encore_vk_acceleration_structure_available(unordered));assert(!encore_vk_acceleration_structure_memory_bytes(unordered));assert(encore_vk_acceleration_structure_destroy(unordered));
    encore_vk_frame_destroy(other_frame);encore_vk_swapchain_destroy(other_target);
    transform[0]=0;size_t singular=encore_vk_frame_build_tlas(frame,&blas,1,transform,16,limit);transform[0]=1;
    assert(singular&&!encore_vk_acceleration_structure_available(singular));assert(!encore_vk_acceleration_structure_memory_bytes(singular));assert(encore_vk_acceleration_structure_destroy(singular));
    size_t tlas=encore_vk_frame_build_tlas(frame,&blas,1,transform,16,limit-blas_bytes);
    if(!encore_vk_acceleration_structure_available(tlas)){encore_str error=encore_vk_acceleration_structure_error(tlas);fprintf(stderr,"TLAS: %.*s\n",(int)error.object->len,error.object->data);return 2;}
    uint64_t tlas_bytes=encore_vk_acceleration_structure_memory_bytes(tlas);assert(tlas_bytes&&blas_bytes+tlas_bytes<=limit);
    assert(!encore_vk_acceleration_structure_release_build_resources(tlas));
    FILE *shader_file=fopen(argv[1],"rb");assert(shader_file);assert(!fseek(shader_file,0,SEEK_END));long length=ftell(shader_file);assert(length>0&&!(length%4));rewind(shader_file);
    uint32_t *words=malloc((size_t)length);assert(words&&fread(words,1,(size_t)length,shader_file)==(size_t)length);fclose(shader_file);
    size_t shader=encore_vk_shader_create(device,words,(size_t)length/4,VK_SHADER_STAGE_COMPUTE_BIT);free(words);assert(encore_vk_shader_available(shader));
    uint32_t bindings[]={0,1},types[]={VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},stages[]={VK_SHADER_STAGE_COMPUTE_BIT,VK_SHADER_STAGE_COMPUTE_BIT};
    size_t layout=encore_vk_bind_group_layout_create_entries(device,bindings,2,types,2,stages,2);assert(encore_vk_bind_group_layout_available(layout));
    size_t pipeline=encore_vk_compute_pipeline_create(device,shader,layout);assert(encore_vk_compute_pipeline_available(pipeline));
    size_t output=encore_vk_buffer_create(device,24,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,true);assert(encore_vk_buffer_available(output));
    uint32_t result_binding=1;uint64_t offset=0,range=24;
    size_t group=encore_vk_ray_query_bind_group_create(device,layout,0,tlas,&result_binding,1,&output,1,&offset,1,&range,1);assert(encore_vk_bind_group_available(group));
    assert(encore_vk_frame_dispatch_compute(frame,pipeline,group,3,1,1));assert(encore_vk_frame_compute_barrier(frame));
    size_t token=encore_vk_frame_completion(frame);assert(token&&encore_vk_completion_status(token)==1);
    assert(encore_vk_frame_present(frame));assert(encore_vk_frame_destroy(frame));assert(encore_vk_swapchain_wait(target));
    assert(encore_vk_completion_status(token)==2);
    assert(encore_vk_buffer_read_u32(output,0)==1 && encore_vk_buffer_read_u32(output,4)==0x3f800000u);
    assert(encore_vk_buffer_read_u32(output,8)==0 && encore_vk_buffer_read_u32(output,16)==1 && encore_vk_buffer_read_u32(output,20)==0x3f800000u);
    encore_vk_bind_group_destroy(group);encore_vk_buffer_destroy(output);encore_vk_compute_pipeline_destroy(pipeline);encore_vk_shader_destroy(shader);encore_vk_bind_group_layout_destroy(layout);

    assert(encore_vk_acceleration_structure_release_build_resources(tlas));assert(encore_vk_acceleration_structure_release_build_resources(blas));
    assert(encore_vk_acceleration_structure_memory_bytes(blas)<blas_bytes&&encore_vk_acceleration_structure_memory_bytes(tlas)<tlas_bytes);
    assert(encore_vk_acceleration_structure_destroy(tlas));assert(encore_vk_acceleration_structure_destroy(blas));assert(encore_vk_completion_destroy(token));
    encore_vk_buffer_destroy(indices);encore_vk_buffer_destroy(vertices);encore_vk_swapchain_destroy(target);encore_vk_device_destroy(device);encore_vk_destroy(context);
    puts("Frame acceleration: bounded BLAS/TLAS, owned producer lifetime, asynchronous GPU build, scratch retirement: PASS");return 0;
}
