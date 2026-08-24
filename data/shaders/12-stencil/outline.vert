#version 330 core

layout(location = 0) in vec3 position_os;
layout(location = 1) in vec3 normal_os;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float outline_width;

void main() {
  vec3 position_ws = vec3(model * vec4(position_os, 1.0));
  mat3 normal_matrix = transpose(inverse(mat3(model)));
  vec3 normal_ws = normalize(normal_matrix * normal_os);
  position_ws += normal_ws * outline_width;
  gl_Position = projection * view * vec4(position_ws, 1.0);
}
