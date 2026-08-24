#version 330 core

uniform vec3 outline_color;

out vec4 frag_color;

void main() {
  frag_color = vec4(outline_color, 1.0);
}
