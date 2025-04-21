// src/video_core/renderer/shader/cas.h
#pragma once

namespace VideoCore::Shader {

// CAS (Contrast Adaptive Sharpening) shader implementation
// Based on AMD FidelityFX CAS
constexpr const char* CAS_GLSL = R"(// Contrast Adaptive Sharpening
#version 430 core

layout(binding = 0) uniform sampler2D input_texture;
layout(location = 0) out vec4 color;

layout(location = 0) uniform vec2 texture_size;
layout(location = 1) uniform float sharpness;

// CAS shader is based on AMD FidelityFX-CAS implementation
vec3 ContrastAdaptiveSharpening(vec2 tex_coord) {
    // Fetch a 3x3 neighborhood around the pixel
    vec2 pixelSize = 1.0 / texture_size;

    vec3 a = texture(input_texture, tex_coord + vec2(-pixelSize.x, -pixelSize.y)).rgb;
    vec3 b = texture(input_texture, tex_coord + vec2(0.0, -pixelSize.y)).rgb;
    vec3 c = texture(input_texture, tex_coord + vec2(pixelSize.x, -pixelSize.y)).rgb;
    vec3 d = texture(input_texture, tex_coord + vec2(-pixelSize.x, 0.0)).rgb;
    vec3 e = texture(input_texture, tex_coord).rgb;
    vec3 f = texture(input_texture, tex_coord + vec2(pixelSize.x, 0.0)).rgb;
    vec3 g = texture(input_texture, tex_coord + vec2(-pixelSize.x, pixelSize.y)).rgb;
    vec3 h = texture(input_texture, tex_coord + vec2(0.0, pixelSize.y)).rgb;
    vec3 i = texture(input_texture, tex_coord + vec2(pixelSize.x, pixelSize.y)).rgb;

    // Soft min and max.
    // a, b, c form a horizontal line
    // d, e, f form a horizontal line
    // g, h, i form a horizontal line
    vec3 min_sample = min(min(min(d, e), min(f, b)), h);
    vec3 max_sample = max(max(max(d, e), max(f, b)), h);

    // Adjust the range based on sharpness
    vec3 tone = min(min_sample, 1.0 - max_sample);
    vec3 w = 1.0 - sharpness * tone;

    // Filter shape:
    //  1 | 2 | 1
    //  2 | 4 | 2
    //  1 | 2 | 1
    vec3 filtered = (a + c + g + i) * 1.0 + (b + d + f + h) * 2.0 + e * 4.0;
    filtered /= 16.0;

    // Blend based on max and min to avoid ringing
    return mix(filtered, e, w);
}

void main() {
    color = vec4(ContrastAdaptiveSharpening(gl_FragCoord.xy / texture_size), 1.0);
}
)";

} // namespace VideoCore::Shader
