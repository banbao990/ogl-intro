#version 460

#extension GL_NV_shading_rate_image : enable

in vec2 uv_fs;

uniform sampler2D base_color;

layout(location = 0) out vec4 frag_color;

void main() {
  frag_color = vec4(texture(base_color, uv_fs).rgb, 1.0);
}