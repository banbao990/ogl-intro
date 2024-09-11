#version 330 core

layout(location = 0) in vec3 position_os;
layout(location = 1) in vec3 normal_os;
layout(location = 3) in vec2 uv0_os;

out vec3 position_vs;
out vec3 normal_vs;
out vec2 uv0_vs;
out vec4 wave_uv_vs;
out vec3 position_ws_vs;

vec3 transform_normal(mat4 mat_inverse, vec3 n) {
  return vec3(dot(mat_inverse[0].xyz, n),
              dot(mat_inverse[1].xyz, n),
              dot(mat_inverse[2].xyz, n));
}

vec4 transform_position(mat4 mat, vec3 p) {
  return mat * vec4(p, 1.0);
}

// Uniforms Start

layout(std140) uniform Params {
  vec3 g_light_dir_vs;
  vec4 g_var1;
  vec4 g_var2;
};

#define g_wave_speed1 g_var1.xy
#define g_wave_speed2 g_var1.zw
#define g_time g_var2.x

layout(std140) uniform Transform {
  mat4 M;
  mat4 MV;
  mat4 I_MV;
  mat4 P;
};

#define g_wave_xy_scale 0.1
#define g_wave_zw_scale 0.5

// Uniforms End

vec3 safe_normalize(vec3 v) {
  return dot(v, v) == 0 ? v : normalize(v);
}

void main() {
  position_vs = transform_position(MV, position_os).xyz;
  normal_vs = safe_normalize(transform_normal(I_MV, normal_os));
  uv0_vs = uv0_os;
  position_ws_vs = transform_position(M, position_os).xyz;

  const vec2 one = vec2(1.0, 1.0);
  vec2 pos = position_ws_vs.xz * one.xx;
  wave_uv_vs.xy = pos * g_wave_xy_scale + g_time * g_wave_speed1;
  wave_uv_vs.zw = pos * g_wave_zw_scale + g_time * g_wave_speed2;

  gl_Position = transform_position(P, position_vs);
}