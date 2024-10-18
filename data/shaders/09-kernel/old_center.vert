#version 330 core

// layout(location = 0) in vec3 position;
// layout(location = 3) in vec2 uv;

uniform sampler2D tex_input;

layout(std140) uniform Params {
  ivec4 g_var1;
};

#define g_kernel_size g_var1.x
#define g_width g_var1.y
#define g_height g_var1.z

out vec3 vs_color;

void main() {
  int r = 2 * g_kernel_size + 1;
  gl_PointSize = r;
  int idx = gl_InstanceID;
  int x = idx % g_width;
  int y = idx / g_width;

  float k_inv = 1.0 / (r * r);
  vs_color = (texelFetch(tex_input, ivec2(x, y), 0).rgb) * k_inv;

  vec2 xy = (vec2(x, y) + 0.5) / vec2(g_width, g_height);

  xy = xy * 2.0 - 1.0;

  gl_Position = vec4(xy, 0.0, 1.0);
}
