#include "material.hpp"

static const char *vertex_shader_text = R"(
#version 330 core

layout(location = 0) in vec2 position;

void main() {
  gl_Position = vec4(position, 0.0, 1.0);
}
)";

static const char *fragment_shader_text = R"(
#version 330 core

layout(location = 0) out vec4 color;

void main() {
  // all white
  color = vec4(1.0, 1.0, 1.0, 1.0);
}
)";

BoltMaterial::BoltMaterial() {
  _program =
      Program::create_from_source(vertex_shader_text, fragment_shader_text);
}

void BoltMaterial::use() {
  glUseProgram(_program->get());
}
