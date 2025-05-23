#version 300 es

// Use mediump for better mobile performance
precision mediump float;

// Vertex attributes
layout(location = 0) in vec3 position;  // Changed to vec3 for proper 3D position
layout(location = 1) in vec2 texCoord;

// Output to fragment shader
layout(location = 0) out vec2 outTexCoord;

// Uniform for viewport scaling (optional, can be used for aspect ratio correction)
uniform vec2 viewportScale;

void main() {
    // Apply viewport scaling if needed
    vec3 scaledPosition = vec3(position.xy * viewportScale, position.z);

    // Set vertex position
    gl_Position = vec4(scaledPosition, 1.0);

    // Pass texture coordinates to fragment shader
    outTexCoord = texCoord;
}