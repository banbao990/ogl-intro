#version 330 core

in vec2 uv_fs;

uniform sampler2D main_tex;

layout(location = 0) out vec4 frag_color;

void main() {
  vec3 hdr = texture(main_tex, uv_fs).rgb;
  frag_color = vec4(hdr, 1.0);
}