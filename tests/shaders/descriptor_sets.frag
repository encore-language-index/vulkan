#version 460
#extension GL_EXT_ray_query : require
layout(set=0,binding=0) uniform Material {vec4 tint;} material;
layout(set=1,binding=0) uniform accelerationStructureEXT scene;
layout(set=1,binding=1,std430) readonly buffer Control {uint enabled[];} control;
layout(location=0) out vec4 color;
void main() {
    rayQueryEXT q;
    rayQueryInitializeEXT(q,scene,gl_RayFlagsOpaqueEXT|gl_RayFlagsTerminateOnFirstHitEXT,255u,vec3(.25,.25,1),.001,vec3(0,0,-1),10.);
    while(rayQueryProceedEXT(q)){}
    bool hit=rayQueryGetIntersectionTypeEXT(q,true)==gl_RayQueryCommittedIntersectionTriangleEXT;
    color=hit && control.enabled[0]!=0u?material.tint:vec4(0,1,0,1);
}
