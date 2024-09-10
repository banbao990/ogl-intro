#version 330 core

in vec3 position_vs;
in vec3 normal_vs;
in vec2 uv0_vs;

layout(std140) uniform Params {
  vec3 light_dir_vs;
};

layout(location = 0) out vec4 frag_color_out;

void main() {
  // simple blinn-phong
  vec3 n = normalize(normal_vs);
  vec3 v = normalize(-position_vs);
  vec3 l = normalize(light_dir_vs);
  vec3 h = normalize(v + l);

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