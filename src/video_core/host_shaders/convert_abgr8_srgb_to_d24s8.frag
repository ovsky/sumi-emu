// SPDX-License-Identifier: GPL-2.0-or-later

#version 450
#extension GL_ARB_shader_stencil_export : require

layout(binding = 0) uniform sampler2D color_texture;

// More accurate sRGB to linear conversion
float srgbToLinear(float srgb) {
    if (srgb <= 0.04045) {
        return srgb / 12.92;
    } else {
        return pow((srgb + 0.055) / 1.055, 2.4);
    }
}

void main() {
    ivec2 coord = ivec2(gl_FragCoord.xy);
    vec4 srgbColor = texelFetch(color_texture, coord, 0);

    // Convert sRGB to linear space with proper gamma correction
    vec3 linearColor = vec3(
        srgbToLinear(srgbColor.r),
        srgbToLinear(srgbColor.g),
        srgbToLinear(srgbColor.b)
    );

    // Use standard luminance coefficients
    float luminance = dot(linearColor, vec3(0.2126, 0.7152, 0.0722));

    // Ensure proper depth range
    luminance = clamp(luminance, 0.0, 1.0);

    // Convert to 24-bit depth value
    uint depth_val = uint(luminance * float(0xFFFFFF));

    // Extract 8-bit stencil from alpha
    uint stencil_val = uint(srgbColor.a * 255.0);

    // Pack values efficiently
    uint depth_stencil = (stencil_val << 24) | (depth_val & 0x00FFFFFF);

    gl_FragDepth = float(depth_val) / float(0xFFFFFF);
    gl_FragStencilRefARB = int(stencil_val);
}