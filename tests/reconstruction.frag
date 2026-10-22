#version 450
layout(set=0,binding=0) uniform Input { vec4 frame; } input_data;
layout(location=0) out vec4 color;
layout(location=1) out vec4 motion;
void main(){
    vec2 uv=gl_FragCoord.xy/vec2(640,360);
    bool rectangle=abs(uv.x-(.25+input_data.frame.x))<.12 && abs(uv.y-.5)<.2;
    float grid=mod(floor(uv.x*32)+floor(uv.y*18),2);
    color=vec4(rectangle?vec3(.9,.08,.025):mix(vec3(.08,.22,.4),vec3(.12,.32,.6),grid),1);
    motion=vec4(rectangle?input_data.frame.y:0,0,.5,.5);
    gl_FragDepth=.5;
}
