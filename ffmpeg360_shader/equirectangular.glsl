#version 330

in mediump vec2 ex_uv; // absolute u,v
flat in mediump vec2 wh;
flat in mediump vec2 corner;

uniform mediump mat4 ModelMatrix;
uniform mediump mat4 ViewMatrix;
uniform mediump mat4 ProjectionMatrix;

uniform mediump vec2 resolution;
uniform mediump float fov;
uniform mediump float yaw;
uniform mediump float pitch;
uniform mediump float roll;

const mediump float M_PI = 3.141592653589793238462643;
const mediump float M_TWOPI = 6.283185307179586476925286;

out mediump float out_Color;

uniform sampler2D textureSampler;

mediump mat3 rotationMatrix(mediump vec3 euler)
{
    mediump vec3 se = sin(euler);
    mediump vec3 ce = cos(euler);

    return mat3(ce.y, 0, -se.y, 0, 1, 0, se.y, 0, ce.y) * mat3(1, 0, 0, 0, ce.x, se.x, 0, -se.x, ce.x) * mat3(ce.z,  se.z, 0,-se.z, ce.z, 0, 0, 0, 1);
}

mediump vec3 toCartesian(mediump vec2 st)
{
    return normalize(vec3(st.x, st.y, 0.5/tan(0.5 * radians(fov))));
}

mediump vec2 toSpherical(mediump vec3 cartesianCoord)
{
    mediump vec2 st = vec2(
        atan(cartesianCoord.x, cartesianCoord.z),
        acos(cartesianCoord.y)
        );
    if(st.x < 0.0)
        st.x += M_TWOPI;

    return st;
}

void main(void)
{
    mediump vec2 sphericalCoord = gl_FragCoord.xy / resolution ;
    sphericalCoord = sphericalCoord - 0.5 ;
    sphericalCoord.y *= -1;

    mediump vec3 cartesianCoord = rotationMatrix(radians(vec3(-pitch, yaw+180., roll))) * toCartesian(sphericalCoord);

    out_Color = texture(textureSampler, toSpherical( cartesianCoord ) / vec2(M_TWOPI, M_PI)).r;
}
