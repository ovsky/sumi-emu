// src/video_core/renderer_vulkan/cas_shader.h
#pragma once

#include <string>

namespace Vulkan {

// CAS (Contrast Adaptive Sharpening) shader implementation
// Based on AMD FidelityFX CAS
static const std::string CAS_COMP_SHADER = R"(
#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 0, rgba8) uniform readonly image2D input_image;
layout(set = 0, binding = 1, rgba8) uniform writeonly image2D output_image;

layout(push_constant) uniform PushConstants {
    float sharpness;
    float reserved1;
    float reserved2;
    float reserved3;
} push_constants;

void main() {
    // Ensure we don't go out of bounds
    ivec2 img_size = imageSize(input_image);
    if (gl_GlobalInvocationID.x >= img_size.x || gl_GlobalInvocationID.y >= img_size.y) {
        return;
    }

    ivec2 coords = ivec2(gl_GlobalInvocationID.xy);
    float sharpness = clamp(push_constants.sharpness, 0.0, 1.0);

    // Read a 3x3 neighborhood
    vec3 a = imageLoad(input_image, coords + ivec2(-1, -1)).rgb;
    vec3 b = imageLoad(input_image, coords + ivec2( 0, -1)).rgb;
    vec3 c = imageLoad(input_image, coords + ivec2( 1, -1)).rgb;
    vec3 d = imageLoad(input_image, coords + ivec2(-1,  0)).rgb;
    vec3 e = imageLoad(input_image, coords + ivec2( 0,  0)).rgb;
    vec3 f = imageLoad(input_image, coords + ivec2( 1,  0)).rgb;
    vec3 g = imageLoad(input_image, coords + ivec2(-1,  1)).rgb;
    vec3 h = imageLoad(input_image, coords + ivec2( 0,  1)).rgb;
    vec3 i = imageLoad(input_image, coords + ivec2( 1,  1)).rgb;

    // Soft min and max
    vec3 min_rgb = min(min(min(d, e), min(f, b)), h);
    vec3 max_rgb = max(max(max(d, e), max(f, b)), h);

    // Compute local contrast
    vec3 contrast = max_rgb - min_rgb;

    // CAS filter
    // Use lower sharpening amount in high contrast areas to reduce artifacts
    float contrast_luma = dot(contrast, vec3(0.2126, 0.7152, 0.0722));
    float scaled_sharpness = sharpness * (1.0 - contrast_luma);

    // Weight center pixel more when sharpness is reduced
    float center_weight = 4.0 + 4.0 * scaled_sharpness;
    float corner_weight = 0.5 - 0.5 * scaled_sharpness;
    float edge_weight = 1.0 - scaled_sharpness;

    // Apply weighted filter
    vec3 result = (a + c + g + i) * corner_weight +
                  (b + d + f + h) * edge_weight +
                   e * center_weight;

    result /= corner_weight * 4.0 + edge_weight * 4.0 + center_weight;

    // Output final color
    imageStore(output_image, coords, vec4(result, 1.0));
}
)";

} // namespace Vulkan