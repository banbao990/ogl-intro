#version 330 core

uniform vec3 tint;
uniform float reflectivity;
uniform float tint_strength;

out vec4 frag_color;

void main() {
  float overlay = 1.0 - reflectivity * (1.0 - tint_strength);
  frag_color = vec4(tint, clamp(overlay, 0.0, 1.0));
}
