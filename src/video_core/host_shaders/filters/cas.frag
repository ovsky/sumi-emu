#version 450

layout(binding = 0) uniform sampler2D input_image;
layout(location = 0) out vec4 frag_color;

layout(push_constant) uniform PushConstants {
    float sharpness;
};

layout(location = 0) in vec2 tex_coord;

vec3 CAS(vec3 color, vec2 uv) {
    vec2 tex_size = textureSize(input_image, 0);
    vec2 inv_tex_size = 1.0 / tex_size;

    vec3 c  = color;
    vec3 n1 = textureLod(input_image, uv + vec2(0.0, -inv_tex_size.y), 0.0).rgb;
    vec3 n2 = textureLod(input_image, uv + vec2(0.0,  inv_tex_size.y), 0.0).rgb;
    vec3 n3 = textureLod(input_image, uv + vec2(-inv_tex_size.x, 0.0), 0.0).rgb;
    vec3 n4 = textureLod(input_image, uv + vec2( inv_tex_size.x, 0.0), 0.0).rgb;

    const vec3 luma_weights = vec3(0.2126, 0.7152, 0.0722);
    float luma  = dot(c,  luma_weights);
    float luma1 = dot(n1, luma_weights);
    float luma2 = dot(n2, luma_weights);
    float luma3 = dot(n3, luma_weights);
    float luma4 = dot(n4, luma_weights);

    float range = max(max(luma1, luma2), max(luma3, luma4)) -
                 min(min(luma1, luma2), min(luma3, luma4));
    float sharp = clamp(range * sharpness * 0.166666, 0.0, 0.8);

    return clamp(c * (1.0 + sharp) - (n1 + n2 + n3 + n4) * (sharp * 0.25), 0.0, 1.0);
}

void main() {
    vec3 color = texture(input_image, tex_coord).rgb;
    frag_color = vec4(CAS(color, tex_coord), 1.0);
}