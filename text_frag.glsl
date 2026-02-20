#version 450

layout(binding = 0) uniform sampler2D fontAtlas;

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
  vec4 color;
} pc;

void main() {
  float alpha = texture(fontAtlas, fragTexCoord).r;
  outColor = vec4(pc.color.rgb, pc.color.a * alpha);
}
