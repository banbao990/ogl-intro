#include "material.hpp"
#include <imgui/imgui.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

// KernelNewCenterMaterial

KernelNewCenterMaterial::KernelNewCenterMaterial() {
  _program = Program::create_from_files("shaders/09-kernel/new_center.vert",
                                        "shaders/09-kernel/new_center.frag");
  auto id = _program->get();

  const int params_binding_point = 1;
  // following 2 lines are done in shader by (binding = 1)
  GLuint params_index = glGetUniformBlockIndex(_program->get(), "Params");
  glUniformBlockBinding(_program->get(), params_index, params_binding_point);
  _params_buffer = std::make_unique<Buffer>(nullptr, sizeof(ParamsBlock));
  glBindBufferBase(
      GL_UNIFORM_BUFFER, params_binding_point, _params_buffer->get());

  // textures
  _tex_input_location = glGetUniformLocation(_program->get(), "tex_input");

  // draw a big triangle that cover the full screen
  std::vector<Mesh::Vertex> vertices = {
      {{-1.0f, -1.0f, 0.0f}, {}, {}, {0.0f, 0.0f}}, // left-down
      {{3.0f, -1.0f, 0.0f}, {}, {}, {2.0f, 0.0f}},  // right-down
      {{-1.0f, 3.0f, 0.0f}, {}, {}, {0.0f, 2.0f}},  // left-up
  };
  _quad = std::make_unique<Mesh>(
      vertices.data(), (uint32_t)vertices.size(), nullptr, 0);

  reset_params();
}

void KernelNewCenterMaterial::use() {
  glUseProgram(_program->get());

  ParamsBlock params_block{};
  params_block._var1 = glm::ivec4(_kernel_size, 0, 0, 0);

  glBindBuffer(GL_UNIFORM_BUFFER, _params_buffer->get());
  glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ParamsBlock), &params_block);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);

  // textures
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, main_tex != nullptr ? main_tex->get() : 0);
  glUniform1i(_tex_input_location, 0);
}

bool KernelNewCenterMaterial::draw_ui() {
  ImGui::PushID("KernelNewCenterMaterial");
  bool changed = false;

  { // kernel size
    changed |= ImGui::SliderInt("Kernel Size", &_kernel_size, 0, 20);
    const int d = _kernel_size * 2 + 1;
    ImGui::Text("Kernel Size: %d x %d", d, d);
  }

  ImGui::PopID();
  return changed;
}

void KernelNewCenterMaterial::draw() {
  _quad->draw();
}

void KernelNewCenterMaterial::reset_params() {
  _kernel_size = 0;
}

// KernelOldCenterMaterial

KernelOldCenterMaterial::KernelOldCenterMaterial() {
  _program = Program::create_from_files("shaders/09-kernel/old_center.vert",
                                        "shaders/09-kernel/old_center.frag");
  auto id = _program->get();

  const int params_binding_point = 2;
  // following 2 lines are done in shader by (binding = 2)
  GLuint params_index = glGetUniformBlockIndex(_program->get(), "Params");
  glUniformBlockBinding(_program->get(), params_index, params_binding_point);
  _params_buffer = std::make_unique<Buffer>(nullptr, sizeof(ParamsBlock));
  glBindBufferBase(
      GL_UNIFORM_BUFFER, params_binding_point, _params_buffer->get());

  // textures
  _tex_input_location = glGetUniformLocation(_program->get(), "tex_input");

  // TODO: draw a big triangle that cover the full screen
  std::vector<Mesh::Vertex> vertices = {
      {{-1.0f, -1.0f, 0.0f}, {}, {}, {0.0f, 0.0f}}, // left-down
      {{3.0f, -1.0f, 0.0f}, {}, {}, {2.0f, 0.0f}},  // right-down
      {{-1.0f, 3.0f, 0.0f}, {}, {}, {0.0f, 2.0f}},  // left-up
  };
  _quad = std::make_unique<Mesh>(
      vertices.data(), (uint32_t)vertices.size(), nullptr, 0);

  reset_params();
}

void KernelOldCenterMaterial::use() {
  glUseProgram(_program->get());

  ParamsBlock params_block{};
  params_block._var1 = glm::ivec4(_kernel_size, 0, 0, 0);

  glBindBuffer(GL_UNIFORM_BUFFER, _params_buffer->get());
  glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ParamsBlock), &params_block);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);

  // textures
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, main_tex != nullptr ? main_tex->get() : 0);
  glUniform1i(_tex_input_location, 0);
}

bool KernelOldCenterMaterial::draw_ui() {
  ImGui::PushID("KernelOldCenterMaterial");
  bool changed = false;

  { // kernel size
    changed |= ImGui::SliderInt("Kernel Size", &_kernel_size, 0, 20);
    const int d = _kernel_size * 2 + 1;
    ImGui::Text("Kernel Size: %d x %d", d, d);
  }

  ImGui::PopID();
  return changed;
}

void KernelOldCenterMaterial::draw() {
  _quad->draw();
}

void KernelOldCenterMaterial::reset_params() {
  _kernel_size = 0;
}