#version 330

in mediump vec2 ex_uv; // absolute u,v
flat in mediump vec2 wh;
flat in mediump vec2 corner;

out mediump float out_Color;

uniform sampler2D textureSampler;

void main(void)
{
    mediump vec2 uv = ex_uv.rg;
    out_Color = texture(textureSampler, uv).r;
}

