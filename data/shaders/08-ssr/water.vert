#version 330 core

layout(location = 0) in vec3 position_os;
layout(location = 1) in vec3 normal_os;
layout(location = 3) in vec2 uv0_os;

out vec3 position_vs;
out vec3 normal_vs;
out vec3 tangent_vs;
out vec3 bitangent_vs;
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
  vec4 g_light_dir_vs;
  vec4 g_light_radiance;
  vec4 g_env_radiance;
  vec4 g_wave_speeds;
  vec4 g_wave_params;
  vec4 g_water_params0;
  vec4 g_water_params1;
  vec4 g_absorption;
  vec4 g_ssr_params0;
  vec4 g_camera_params;
  ivec4 g_screen_params;
  ivec4 g_mode_params;
};

#define g_wave_speed1 g_wave_speeds.xy
#define g_wave_speed2 g_wave_speeds.zw
#define g_wave_global_speed g_wave_params.x
#define g_wave_scale1 g_wave_params.y
#define g_wave_scale2 g_wave_params.z
#define g_time g_water_params0.x
#define g_flat_water g_mode_params.w

layout(std140) uniform Transform {
  mat4 M;
  mat4 MV;
  mat4 I_MV;
  mat4 P;
  mat4 I_P;
};

// Uniforms End

vec3 safe_normalize(vec3 v) {
  return dot(v, v) == 0 ? v : normalize(v);
}

void main() {
  position_vs = transform_position(MV, position_os).xyz;
  normal_vs = safe_normalize(transform_normal(I_MV, normal_os));
  tangent_vs = safe_normalize((MV * vec4(1.0, 0.0, 0.0, 0.0)).xyz);
  bitangent_vs = safe_normalize((MV * vec4(0.0, 0.0, 1.0, 0.0)).xyz);
  uv0_vs = uv0_os;
  position_ws_vs = transform_position(M, position_os).xyz;

  const vec2 one = vec2(1.0, 1.0);
  vec2 pos = position_ws_vs.xz * one.xx;
  if (g_flat_water != 0) {
    wave_uv_vs = vec4(0.0);
  } else {
    float wave_time = g_time * g_wave_global_speed;
    wave_uv_vs.xy =
        pos * (g_wave_scale1 * 0.01) + wave_time * g_wave_speed1;
    wave_uv_vs.zw =
        pos * (g_wave_scale2 * 0.01) + wave_time * g_wave_speed2;
  }

  gl_Position = transform_position(P, position_vs);
}
