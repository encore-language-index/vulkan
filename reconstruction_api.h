/* Optional reconstruction provider ABI. No backend-private structures cross it. */
#ifndef ENCORE_RECONSTRUCTION_API_H
#define ENCORE_RECONSTRUCTION_API_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define ENCORE_RECONSTRUCTION_ABI 2u
enum { ENCORE_RECONSTRUCTION_FSR=1, ENCORE_RECONSTRUCTION_DLSS=2,
       ENCORE_RECONSTRUCTION_DLAA=3, ENCORE_RECONSTRUCTION_FSR_FG=4,
       ENCORE_RECONSTRUCTION_DLSS_FG=5 };
typedef struct {
    uintptr_t instance, physical_device, device;
    uintptr_t get_instance_proc, get_device_proc;
    uint32_t queue_family;
} EncoreReconstructionDevice;
typedef struct {
    uintptr_t image, view;
    uint32_t width, height, format, layout;
} EncoreReconstructionImage;
typedef struct {
    uint32_t method, input_width, input_height, output_width, output_height, quality;
} EncoreReconstructionDescription;
/* Color/output: linear sRGB RGBA16F. FG also accepts matching display-ready
 * RGBA8 pairs; linear FG inputs are converted on the GPU to display SDR before
 * interpolation and decoded back afterwards. Composite UI after this stage.
 * Motion is current-minus-previous UV, including projection jitter.
 * Depth is conventional Vulkan 0..1 (near..far), jitter is in render pixels. */
typedef struct {
    EncoreReconstructionImage color, depth, motion, output, reactive, composition;
    float jitter_x, jitter_y, previous_jitter_x, previous_jitter_y;
    float delta_ms, near_plane, far_plane, vertical_fov, sharpness, exposure;
    uint32_t reset, frame_index, multiplier, generated_index;
    float clip_to_previous[16], previous_to_clip[16];
    /* Unjittered column-major transforms, committed per successfully rendered view. */
    float view_projection[16], previous_view_projection[16], projection[16], view[16];
} EncoreReconstructionFrame;
typedef struct {
    uint32_t abi;
    void *(*create)(const EncoreReconstructionDevice *,const EncoreReconstructionDescription *,char *,size_t);
    int (*dispatch)(void *,uintptr_t,const EncoreReconstructionFrame *,char *,size_t);
    void (*destroy)(void *);
    uint32_t (*capabilities)(const EncoreReconstructionDevice *,char *,size_t);
    int (*resolution)(const EncoreReconstructionDevice *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t *,uint32_t *);
} EncoreReconstructionApi;
typedef const EncoreReconstructionApi *(*EncoreReconstructionEntry)(uint32_t);
#ifdef __cplusplus
}
#endif
#endif
