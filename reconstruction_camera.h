/* Camera constants for temporal SDKs. Matrices are column-major, without jitter. */
#ifndef ENCORE_RECONSTRUCTION_CAMERA_H
#define ENCORE_RECONSTRUCTION_CAMERA_H
#include <cmath>
#include <algorithm>
#include <cstring>
namespace encore_reconstruction {
inline bool inverse(const float *input, float *output) {
    double a[4][8]{};
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){a[r][c]=input[c*4+r];a[r][c+4]=r==c;}
    for(unsigned c=0;c<4;++c){
        unsigned pivot=c;for(unsigned r=c+1;r<4;++r)if(std::abs(a[r][c])>std::abs(a[pivot][c]))pivot=r;
        if(!std::isfinite(a[pivot][c]) || std::abs(a[pivot][c])<1.e-12)return false;
        for(unsigned k=0;k<8;++k)std::swap(a[c][k],a[pivot][k]);
        const double scale=a[c][c];for(double &v:a[c])v/=scale;
        for(unsigned r=0;r<4;++r)if(r!=c){const double scale2=a[r][c];for(unsigned k=0;k<8;++k)a[r][k]-=scale2*a[c][k];}
    }
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){output[c*4+r]=float(a[r][c+4]);if(!std::isfinite(output[c*4+r]))return false;}
    return true;
}
inline void multiply(const float *a,const float *b,float *result){
    float out[16]{};for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)for(unsigned k=0;k<4;++k)out[c*4+r]+=a[k*4+r]*b[c*4+k];
    std::memcpy(result,out,sizeof(out));
}
struct Camera {
    float inverse_projection[16]{}, inverse_view[16]{}, clip_to_previous[16]{}, previous_to_clip[16]{};
};
inline bool camera(const EncoreReconstructionFrame &f,Camera &out){
    float inverse_current[16],inverse_previous[16];
    if(!inverse(f.projection,out.inverse_projection) || !inverse(f.view,out.inverse_view) ||
       !inverse(f.view_projection,inverse_current) || !inverse(f.previous_view_projection,inverse_previous))return false;
    multiply(f.previous_view_projection,inverse_current,out.clip_to_previous);
    multiply(f.view_projection,inverse_previous,out.previous_to_clip);return true;
}
}
#endif
