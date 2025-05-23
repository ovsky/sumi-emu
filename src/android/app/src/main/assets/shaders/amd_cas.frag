#version 300 es

// Use mediump for better mobile performance
precision mediump float;

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

uniform sampler2D inputTexture;
uniform float sharpness;
uniform float qualityLevel;

// Constants for quality levels and optimization
const float QUALITY_ULTRA = 4.0;
const float QUALITY_QUALITY = 3.0;
const float QUALITY_BALANCED = 2.0;
const float QUALITY_PERFORMANCE = 1.0;

// Optimized luminance calculation using BT.709 coefficients
const vec3 LUM_COEFF = vec3(0.2126, 0.7152, 0.0722);

// Optimized CAS implementation for mobile GPUs
vec4 CAS(vec2 uv) {
    // Calculate sampling pattern based on quality level
    float quality = qualityLevel;
    float sampleScale = 1.0 + (quality * 0.25);

    vec2 texelSize = 1.0 / vec2(textureSize(inputTexture, 0));
    vec2 scaledTexelSize = texelSize * sampleScale;

    // Optimize sampling for mobile GPUs
    // Use textureGather for better performance where available
    vec4 center = texture(inputTexture, uv);

    // Optimize texture lookups for mobile
    vec2 offset = scaledTexelSize;
    vec4 topLeft = texture(inputTexture, uv - offset);
    vec4 topRight = texture(inputTexture, uv + vec2(offset.x, -offset.y));
    vec4 bottomLeft = texture(inputTexture, uv + vec2(-offset.x, offset.y));
    vec4 bottomRight = texture(inputTexture, uv + offset);

    // Optimize luminance calculation for mobile
    float centerLum = dot(center.rgb, LUM_COEFF);
    float topLeftLum = dot(topLeft.rgb, LUM_COEFF);
    float topRightLum = dot(topRight.rgb, LUM_COEFF);
    float bottomLeftLum = dot(bottomLeft.rgb, LUM_COEFF);
    float bottomRightLum = dot(bottomRight.rgb, LUM_COEFF);

    // Optimize contrast calculation for mobile
    float maxLum = max(max(max(topLeftLum, topRightLum), max(bottomLeftLum, bottomRightLum)), centerLum);
    float minLum = min(min(min(topLeftLum, topRightLum), min(bottomLeftLum, bottomRightLum)), centerLum);
    float lumRange = maxLum - minLum;

    // Optimize sharpening calculation for mobile
    float adaptiveSharpness = sharpness * (1.0 - smoothstep(0.0, 0.5, lumRange));

    // Quality-based sharpening factor with mobile optimization
    float qualityFactor = 1.0 + (quality * 0.25);
    float sharpeningFactor = adaptiveSharpness * qualityFactor;

    // Optimize sharpening application for mobile
    vec4 sharpened = center * (1.0 + sharpeningFactor) -
                    (topLeft + topRight + bottomLeft + bottomRight) * (sharpeningFactor * 0.25);

    // Optimize clamping for mobile
    return clamp(sharpened, 0.0, 1.0);
}

// Main function with mobile-optimized CAS call
void main() {
    fragColor = CAS(texCoord);
}