#version 330

in mediump vec2 ex_uv; // absolute u,v
flat in mediump vec2 wh;
flat in mediump vec2 corner;

out mediump float out_Color;
uniform sampler2D textureSampler;

const mediump float PI = 3.1415926535897932384626433832795;
const mediump float PI_2 = 1.57079632679489661923;
const mediump float PI_4 = 0.785398163397448309616;

void main(void)
{
    mediump vec2 uv;
    mediump vec2 ratio;

    ratio = (ex_uv - corner - wh/2.0) / (wh/2.0);
    ratio /= 1.01;
    uv = corner + tan(ratio * PI_4) * (wh/2.0) + wh/2.0;

    out_Color = texture(textureSampler, uv).r;
}
