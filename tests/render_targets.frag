#version 450
layout(location=0) out vec4 color;
layout(location=1) out vec4 motion;
void main(){color=vec4(.25,.5,.75,1);motion=vec4(.1,-.1,2,2);}
