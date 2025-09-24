#version 460

#extension GL_NV_shading_rate_image : enable

in vec2 uv_fs;

uniform sampler2D base_color;
uniform int vrs_visualize;

layout(location = 0) out vec4 frag_color;

vec3 jet_map(float jet_max, float v) {
  vec3 c = vec3(1.f, 1.f, 1.f);
  if (v < (0.25f * jet_max)) {
    c[0] = 0;
    c[1] = 4 * v / jet_max;
  } else if (v < (0.5f * jet_max)) {
    c[0] = 0;
    c[2] = 1 + 4 * (0.25f * jet_max - v) / jet_max;
  } else if (v < (0.75f * jet_max)) {
    c[0] = 4 * (v - 0.5f * jet_max) / jet_max;
    c[2] = 0;
  } else {
    v = min(v, jet_max);
    c[1] = 1 + 4 * (0.75f * jet_max - v) / jet_max;
    c[2] = 0;
  }
  return c;
}

void main() {
  if (vrs_visualize == 0) {
    frag_color = vec4(texture(base_color, uv_fs).rgb, 1.0f);
  } else {
    // v: 1 - 7
    float v = gl_FragmentSizeNV.x / 2 + gl_FragmentSizeNV.y / 2 * 2 + 1;
    frag_color = vec4(jet_map(6.0f, 6.0f - (v - 1.0f)), 1.0f);
    // frag_color = vec4(vec3(v / 7.0f), 1.0f);
  }
}