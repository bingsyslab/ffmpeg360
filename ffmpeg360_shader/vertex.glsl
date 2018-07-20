#version 330

layout(location=0) in vec4 in_Position;
layout(location=1) in vec2 in_uv;
layout(location=2) in vec4 in_uvr; // corner coordinates and width and height

out vec2 ex_uv;
flat out vec2 corner;
flat out vec2 wh;

uniform mat4 ModelMatrix;
uniform mat4 ViewMatrix;
uniform mat4 ProjectionMatrix;

const
mat4 view = mat4(
    1.0, 0.0, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.0, 0.0, 0.0, 1.0
    );
void main(void)
{
    gl_Position =  ProjectionMatrix * ModelMatrix * in_Position;

    ex_uv = in_uv;
    wh = in_uvr.zw;
    corner = in_uvr.xy;
   //ex_Color = in_Color;
}
