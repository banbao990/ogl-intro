#version 330 core

in vec3 position_vs;
in vec3 normal_vs;
in vec2 uv0_vs;
in vec4 wave_uv_vs;

// Uniforms Start

layout(std140) uniform Params {
  vec3 g_light_dir_vs;
  vec4 g_var1;
  vec4 g_var2;
};

#define g_wave_speed1 g_var1.xy
#define g_wave_speed2 g_var1.zw
#define g_time g_var2.x
#define g_wave_strength g_var2.y

// Uniforms End

layout(location = 0) out vec4 frag_color_out;

uniform sampler2D g_wave_tex;

vec3 safe_normalize(vec3 v) {
  return dot(v, v) == 0 ? v : normalize(v);
}

void main() {
  // simple blinn-phong
  vec3 n = normalize(normal_vs);
  vec3 v = normalize(-position_vs);
  vec3 l = normalize(g_light_dir_vs);
  vec3 h = normalize(v + l);

  const vec2 one = vec2(1.0, 1.0);
  // normal distortion
  vec2 distortion1 = texture2D(g_wave_tex, wave_uv_vs.xy).rr * 2.0 - one.xx;
  vec2 distortion2 = texture2D(g_wave_tex, wave_uv_vs.zw).rr * 2.0 - one.xx;
  vec2 distortion = (distortion1 + distortion2 * 0.5);
  distortion *= g_wave_strength;
  // TODO: depth test
  n = safe_normalize(n + vec3(distortion.x, 0.0, distortion.y));

  // red
  vec3 color = vec3(1.0, 0.0, 0.0);
  // ambient
  vec3 ambient = 0.05 * color;
  // diffuse
  float diff = max(dot(l, n), 0.0);
  vec3 diffuse = diff * color;
  // specular
  float spec = pow(max(dot(n, h), 0.0), 32.0);
  vec3 specular = vec3(0.3) * spec; // assuming bright white light color
  frag_color_out = vec4(ambient + diffuse + specular, 1.0);
}