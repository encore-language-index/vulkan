#!/usr/bin/env python3
"""Build optional official SDK providers without changing system packages."""
from pathlib import Path
import argparse, json, shutil, subprocess, struct

ROOT=Path(__file__).resolve().parents[1]
SOURCES={
    'fidelityfx':('https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git','v1.1.4','c6efa6bf7f2027b3ec94f28578bb5965eabb9e55',
                 ['sdk/include','sdk/src/components','sdk/src/backends/vk','sdk/src/backends/shared','sdk/src/shared']),
    'dlss':('https://github.com/NVIDIA/DLSS.git','v310.9.16','9981c6350dc58c0ce21f024fd65e31e851c31689',['include','lib/Linux_x86_64']),
    'vulkan-headers':('https://github.com/KhronosGroup/Vulkan-Headers.git','v1.3.296','29f979ee5aa58b7b005f805ea8df7a855c39ff37',['include']),
}
def run(args,**kwargs):subprocess.run([str(a) for a in args],check=True,**kwargs)
def main():
    p=argparse.ArgumentParser();p.add_argument('--sdk-dir',type=Path,default=ROOT/'target/reconstruction/sdk')
    p.add_argument('--output',type=Path,default=ROOT/'target/reconstruction/runtime');args=p.parse_args()
    sdk=args.sdk_dir.resolve();out=args.output.resolve();work=out.parent;out.mkdir(parents=True,exist_ok=True)
    for name,(url,tag,revision,paths) in SOURCES.items():
        checkout=sdk/name
        if not (checkout/'.git').exists():
            checkout.parent.mkdir(parents=True,exist_ok=True)
            run(['git','clone','--depth','1','--filter=blob:none','--sparse','--branch',tag,url,checkout])
        actual=subprocess.check_output(['git','rev-parse','HEAD'],cwd=checkout,text=True).strip()
        if actual!=revision:raise SystemExit(f'{name}: expected {revision}, found {actual}')
        run(['git','sparse-checkout','set',*paths],cwd=checkout)
    ffx=sdk/'fidelityfx';include=work/'ffx-include'
    shutil.copytree(ffx/'sdk/include',include,dirs_exist_ok=True)
    types=include/'FidelityFX/host/ffx_types.h';text=types.read_text()
    # wchar_t is 32-bit on Linux. Keep real libc wchar ABI and enlarge the opaque
    # source-built context instead of using the incompatible -fshort-wchar flag.
    old='#define FFX_SDK_DEFAULT_CONTEXT_SIZE (1024 * 128)'
    if old not in text:raise SystemExit('Unexpected FidelityFX context definition')
    types.write_text(text.replace(old,'#define FFX_SDK_DEFAULT_CONTEXT_SIZE (1024 * 256)'))
    shaders=work/'fsr-shaders'
    run(['python3',ROOT/'tools/cook_reconstruction_shaders.py',ffx,shaders])
    display_code='/* Generated from the display conversion GLSL. */\n'
    for name in ['encode','decode']:
        binary=shaders/('display_'+name+'.spv')
        run(['glslangValidator','-V','--target-env','vulkan1.2',ROOT/('shaders/reconstruction_display_'+name+'.comp'),'-o',binary])
        run(['spirv-val','--target-env','vulkan1.2',binary])
        data=binary.read_bytes();words=struct.unpack('<'+'I'*(len(data)//4),data)
        display_code+='static const uint32_t reconstruction_display_'+name+'[]={'+','.join(str(v)+'u' for v in words)+'};\n'
    (shaders/'reconstruction_display_spirv.h').write_text(display_code)
    backend=(ffx/'sdk/src/backends/vk/ffx_vk.cpp').read_text()
    # Upstream packs scratch arrays to 4 bytes. Linux's host structures need
    # 16-byte alignment (including compiler-vectorized EffectContext stores).
    backend=backend.replace('typedef struct BackendContext_VK {','typedef struct alignas(16) BackendContext_VK {')
    alignment_lines=backend.count('sizeof(uint32_t));')
    if alignment_lines!=12:raise SystemExit('Unexpected FidelityFX scratch layout: '+str(alignment_lines))
    backend=backend.replace('sizeof(uint32_t));','size_t(16));')
    patched_backend=work/'ffx_vk_linux.cpp';patched_backend.write_text(backend)
    shared=ffx/'sdk/src/shared'
    sources=[ROOT/'reconstruction_plugin.cpp',ROOT/'reconstruction_fsr.cpp',shaders/'fsr_shaders.cpp',
             patched_backend,*[ffx/('sdk/src/components/'+name+'/ffx_'+name+'.cpp') for name in ['fsr3upscaler','frameinterpolation','opticalflow']],
             *[shared/(name+'.cpp') for name in ['ffx_assert','ffx_message','ffx_breadcrumbs_list','ffx_object_management']]]
    run(['clang++','-std=c++17','-O2','-g','-fPIC','-shared','-DENCORE_WITH_FSR','-Wl,-z,defs',*sources,
         '-include',ROOT/'reconstruction_portability.h','-I'+str(sdk/'dlss/include'),'-I'+str(sdk/'vulkan-headers/include'),
         '-I'+str(shaders),'-I'+str(include),'-I'+str(ffx/'sdk/src/backends/shared'),'-I'+str(shared),
         sdk/'dlss/lib/Linux_x86_64/libnvsdk_ngx.a','-lvulkan','-ldl','-pthread','-o',out/'libencore_reconstruction.so'])
    for feature in ['dlss','dlssg']:
        source=sdk/'dlss/lib/Linux_x86_64/rel'/f'libnvidia-ngx-{feature}.so.310.9.16'
        shutil.copy2(source,out/source.name)
        link=out/f'libnvidia-ngx-{feature}.so'
        if link.is_symlink():link.unlink()
        if not link.exists():link.symlink_to(source.name)
    for name in SOURCES:
        checkout=sdk/name
        candidates=[checkout/'LICENSE.txt',checkout/'LICENSE.md',checkout/'LICENSE',checkout/'license.txt',checkout/'license.md']
        for candidate in candidates:
            if candidate.exists():shutil.copy2(candidate,out/(name+'-LICENSE.txt'));break
    (out/'sdk-versions.json').write_text(json.dumps({k:{'tag':v[1],'revision':v[2]} for k,v in SOURCES.items()},indent=2)+'\n')
    print('Reconstruction runtime:',out,flush=True)
if __name__=='__main__':main()
