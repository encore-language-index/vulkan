#!/usr/bin/env python3
"""Compile the supported FSR Vulkan variants and reflect their actual SPIR-V.

Uses upstream GLSL, not reimplementations of FSR. FP32 kernels avoid requiring
shaderFloat16 and an SDK-specific Windows shader compiler on Linux.
"""
from pathlib import Path
import argparse, json, struct, subprocess

def reflect(data):
    words=struct.unpack('<'+'I'*(len(data)//4),data)
    names,decorations,types,variables,constants={},{},{},[],{}
    pos=5
    while pos<len(words):
        header=words[pos];count=header>>16;op=header&65535;a=words[pos+1:pos+count]
        if not count:raise ValueError('Invalid SPIR-V instruction')
        if op==5:names[a[0]]=struct.pack('<'+'I'*(len(a)-1),*a[1:]).split(b'\0')[0].decode()
        elif op==71:decorations.setdefault(a[0],{})[a[1]]=a[2:]
        elif op in (25,26,27,28,29,30,32):types[a[0]]=(op,a[1:])
        elif op==43:constants[a[1]]=a[2]
        elif op==59:variables.append(a)
        pos+=count
    categories=[[] for _ in range(7)]
    for pointer,identity,storage,*_ in variables:
        d=decorations.get(identity,{})
        if 33 not in d:continue
        binding=d[33][0];space=d.get(34,(0,))[0];kind,params=types[pointer]
        assert kind==32
        target=params[1];count=1
        kind,params=types[target]
        if kind==28:
            count=constants[params[1]];target=params[0];kind,params=types[target]
        name=names.get(identity,'')
        if storage==2:
            if 3 in decorations.get(target,{}):category=4 if name.startswith('rw_') else 3
            else:category=0
        elif storage==12:category=4 if name.startswith('rw_') else 3
        elif kind==25:category=2 if params[5]==2 else 1
        elif kind==26:category=5
        else:raise ValueError(f'Unsupported descriptor {name}: {kind}/{storage}')
        assert name and space==0,(name,space)
        categories[category].append((name,binding,count,space))
    return [sorted(values,key=lambda v:v[1]) for values in categories]

def main():
    parser=argparse.ArgumentParser();parser.add_argument('sdk',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    gpu=args.sdk/'sdk/include/FidelityFX/gpu';src=args.sdk/'sdk/src/backends/vk/shaders'
    flags=['FFX_GPU=1','FFX_GLSL=1','FFX_HALF=0','FFX_FSR3UPSCALER_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0',
        'FFX_FSR3UPSCALER_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0','FFX_FSR3UPSCALER_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=0',
        'FFX_FSR3UPSCALER_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0','FFX_FSR3UPSCALER_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2',
        'FFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE=1','FFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT=1',
        'FFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS=1','FFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS=1',
        'FFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH=0']
    passes=['prepare_inputs','luma_pyramid','shading_change_pyramid','shading_change','prepare_reactivity',
            'luma_instability','accumulate','accumulate_sharpen','rcas','debug_view','autogen_reactive']
    code=['// Generated from AMD FidelityFX SDK v1.1.4 (MIT). Preserve its LICENSE.txt.\n',
          '#include <FidelityFX/host/ffx_interface.h>\n#include <cstring>\n#include "ffx_shader_blobs.h"\n']
    entries=[]
    for index,name in enumerate(passes):
        entries.append(('FFX_EFFECT_FSR3UPSCALER',index,'fsr3upscaler',name,
            'ffx_fsr3upscaler_'+('accumulate' if name=='accumulate_sharpen' else name)+'_pass',
            flags+['FFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING='+str(int(name=='accumulate_sharpen'))]))
    optical=['prepare_luma_pass','compute_luminance_pyramid_pass','generate_scd_histogram_pass','compute_scd_divergence_pass',
             'compute_optical_flow_advanced_pass_v5','filter_optical_flow_pass_v5','scale_optical_flow_advanced_pass_v5']
    for index,name in enumerate(optical):
        entries.append(('FFX_EFFECT_OPTICALFLOW',index,'opticalflow',name,'ffx_opticalflow_'+name,
            ['FFX_GPU=1','FFX_GLSL=1','FFX_HALF=0','FFX_OPTICALFLOW_OPTION_HDR_COLOR_INPUT=0']))
    interpolation=['reconstruct_and_dilate_pass','setup_pass','reconstruct_previous_depth_pass','game_motion_vector_field_pass',
                   'optical_flow_vector_field_pass','disocclusion_mask_pass','pass','compute_inpainting_pyramid_pass',
                   'inpainting_pass','compute_game_vector_field_inpainting_pyramid_pass','debug_view_pass']
    fi_flags=['FFX_GPU=1','FFX_GLSL=1','FFX_HALF=0','FFX_FRAMEINTERPOLATION_OPTION_LOW_RES_MOTION_VECTORS=1',
              'FFX_FRAMEINTERPOLATION_OPTION_HDR_COLOR_INPUT=0','FFX_FRAMEINTERPOLATION_OPTION_JITTER_MOTION_VECTORS=1','FFX_FRAMEINTERPOLATION_OPTION_INVERTED_DEPTH=0',
              'FFX_FRAMEINTERPOLATION_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=0','FFX_FRAMEINTERPOLATION_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2']
    for index,name in enumerate(interpolation):
        entries.append(('FFX_EFFECT_FRAMEINTERPOLATION',index,'frameinterpolation',name,'ffx_frameinterpolation_'+name,fi_flags))
    blobs={};report={}
    for entry_index,(effect,index,folder,name,stem,defines) in enumerate(entries):
        output=args.output/(folder+'_'+name+'.spv')
        cmd=['glslangValidator','-V','--target-env','vulkan1.2','-S','comp','-Os',
             '-I'+str(gpu),'-I'+str(gpu/folder),*['-D'+f for f in defines],
             str(src/folder/(stem+'.glsl')),'-o',str(output)]
        result=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        if result.returncode:raise RuntimeError(result.stdout)
        subprocess.run(['spirv-val','--target-env','vulkan1.2',str(output)],check=True)
        data=output.read_bytes();reflection=reflect(data);report[folder+'_'+name]=reflection
        prefix='fsr_'+str(entry_index);words=struct.unpack('<'+'I'*(len(data)//4),data)
        code.append('alignas(4) static const uint32_t '+prefix+'_code[]={'+','.join(hex(w) for w in words)+'};\n')
        fields=[]
        for category,values in enumerate(reflection):
            if not values:fields.extend(['nullptr']*4);continue
            base=prefix+'_'+str(category)
            code.append('static const char *'+base+'_names[]={'+','.join(json.dumps(v[0]) for v in values)+'};\n')
            for i,label in [(1,'bindings'),(2,'counts'),(3,'spaces')]:
                code.append('static const uint32_t '+base+'_'+label+'[]={'+','.join(str(v[i]) for v in values)+'};\n')
            fields.extend([base+'_names',base+'_bindings',base+'_counts',base+'_spaces'])
        code.append('static const FfxShaderBlob '+prefix+'_blob={(const uint8_t *)'+prefix+'_code,sizeof('+prefix+'_code),'+
                    ','.join(str(len(v)) for v in reflection)+','+','.join(fields)+'};\n')
        blobs.setdefault(effect,[]).append((index,prefix+'_blob'))
    code.append('extern "C" FfxErrorCode ffxGetPermutationBlobByIndex(FfxEffect effect,FfxPass pass,FfxBindStage,uint32_t options,FfxShaderBlob *out){\n')
    code.append('switch(effect){\n')
    for effect,values in blobs.items():
        code.append(f'case {effect}:switch(pass){{\n')
        for i,blob in values:code.append(f'case {i}:std::memcpy(out,&{blob},sizeof(*out));return FFX_OK;\n')
        code.append('default:return FFX_ERROR_INVALID_ARGUMENT;}\n')
    code.append('default:return FFX_ERROR_INVALID_ARGUMENT;}}\n')
    code.append('extern "C" FfxErrorCode ffxIsWave64(FfxEffect,uint32_t,bool &value){value=false;return FFX_OK;}\n')
    (args.output/'fsr_shaders.cpp').write_text(''.join(code))
    (args.output/'reflection.json').write_text(json.dumps(report,indent=2)+'\n')
    print('FSR 3.1 shaders: compiled, validated and reflected',flush=True)

if __name__=='__main__':main()
