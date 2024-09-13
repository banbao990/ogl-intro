#version 330 core

layout(location = 0) in vec3 position_os;

uniform mat4 transform;


vec4 transform_position(mat4 mat, vec3 p) {
  return mat * vec4(p, 1.0);
}

void main() {
  gl_Position = transform_position(transform, position_os);
}