#version 330 core

layout(location = 0) in vec3 position_os;
layout(location = 1) in vec3 normal_os;
layout(location = 3) in vec2 uv0_os;

out vec3 position_vs;
out vec3 normal_vs;
out vec2 uv0_vs;

vec3 transform_normal(mat4 mat_inverse, vec3 n) {
  return vec3(dot(mat_inverse[0].xyz, n),
              dot(mat_inverse[1].xyz, n),
              dot(mat_inverse[2].xyz, n));
}

vec4 transform_position(mat4 mat, vec3 p) {
  return mat * vec4(p, 1.0);
}

layout(std140) uniform Transform {
  mat4 MV;
  mat4 I_MV;
  mat4 P;
};

vec3 safe_normalize(vec3 v) {
  return dot(v, v) == 0 ? v : normalize(v);
}

void main() {
  position_vs = transform_position(MV, position_os).xyz;
  normal_vs = safe_normalize(transform_normal(I_MV, normal_os));
  uv0_vs = uv0_os;

  gl_Position = transform_position(P, position_vs);
}