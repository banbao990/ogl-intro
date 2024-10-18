#version 330 core

in vec3 vs_color;

uniform sampler2D tex_input;

layout(location = 0) out vec4 frag_color;

layout(std140) uniform Params {
  ivec4 g_var1;
};

#define g_kernel_size g_var1.x
#define g_width g_var1.y
#define g_height g_var1.z

vec3 kernel(ivec2 coord) {
  vec3 sum = vec3(0.0);
  for (int i = -g_kernel_size; i <= g_kernel_size; i++) {
    for (int j = -g_kernel_size; j <= g_kernel_size; j++) {
      // sum += texture(tex_input, uv_fs + ivec2(i, j) / vec2(g_width,
      // g_height)).rgb;
      sum += texelFetch(tex_input, coord + ivec2(i, j), 0).rgb;
    }
  }
  int total = (2 * g_kernel_size + 1) * (2 * g_kernel_size + 1);
  return sum / float(total);
}

void main() {
  // vec3 hdr = kernel(ivec2(gl_FragCoord.xy));
  // frag_color = vec4(hdr, 1.0);
  frag_color = vec4(vs_color, 1.0);
}