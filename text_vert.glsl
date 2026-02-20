#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;

void main() {
  // Convert screen coordinates to normalized device coordinates
  // Assuming inPosition is in screen space coordinates
  vec2 ndc = inPosition / vec2(1980.0, 1020.0) * 2.0 - 1.0;

  gl_Position = vec4(ndc, 0.0, 1.0);
  fragTexCoord = inTexCoord;
}
