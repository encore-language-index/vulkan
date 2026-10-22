#include "../reconstruction_api.h"
#include "../reconstruction_camera.h"
#include <cassert>
#include <cstdio>
int main(){
    EncoreReconstructionFrame f{};
    for(unsigned i=0;i<4;++i){f.view[i*5]=1;f.projection[i*5]=1;f.view_projection[i*5]=1;f.previous_view_projection[i*5]=1;}
    f.view[12]=-5;f.view[13]=2;
    f.view_projection[12]=.25f;f.previous_view_projection[12]=-.5f;
    // Perspective (Vulkan Y-down, 0..1 depth), not an affine-only inverse.
    f.projection[0]=1.2f;f.projection[5]=-1.7f;f.projection[10]=-1.001f;f.projection[11]=-1;f.projection[14]=-.1001f;f.projection[15]=0;
    encore_reconstruction::Camera camera;assert(encore_reconstruction::camera(f,camera));
    assert(std::abs(camera.inverse_view[12]-5)<1.e-5f && std::abs(camera.inverse_view[13]+2)<1.e-5f);
    assert(std::abs(camera.clip_to_previous[12]+.75f)<1.e-5f && std::abs(camera.previous_to_clip[12]-.75f)<1.e-5f);
    float identity[16];encore_reconstruction::multiply(f.projection,camera.inverse_projection,identity);
    for(unsigned i=0;i<16;++i)assert(std::abs(identity[i]-float(i%5==0))<1.e-5f);
    f.projection[0]=0;assert(!encore_reconstruction::camera(f,camera));
    puts("Camera: perspective inverse, clip reprojection, world basis and singular rejection PASS");
}
