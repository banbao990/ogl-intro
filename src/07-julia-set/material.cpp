#include "material.hpp"
#include <imgui/imgui.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

// BlitMaterial

BlitMaterial::BlitMaterial() {
  _program =
      Program::create_from_files("shaders/blit.vert", "shaders/blit.frag");
  _transform_location = glGetUniformLocation(_program->get(), "transform");
  _image_location = glGetUniformLocation(_program->get(), "main_tex");
}

void BlitMaterial::use() {
  glUseProgram(_program->get());
  glm::mat4 transform = projection * model * view;
  glUniformMatrix4fv(_transform_location, 1, false, (GLfloat *)&transform);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, main_tex != nullptr ? main_tex->get() : 0);
  glUniform1i(_image_location, 0);
}

// JuliaSetMaterial

namespace {
struct ParamsBlock {
  glm::vec4 _var1;
  glm::vec4 _var2;
  glm::ivec4 _var3;
};
} // namespace

JuliaSetMaterial::JuliaSetMaterial() {
  _program = Program::create_compute_shader_from_file(
      "shaders/07-julia-set/sample.comp");
  auto id = _program->get();

  const int params_binding_point = 1;
  // following 2 lines are dne in shader by (binding = 1)
  GLuint params_index = glGetUniformBlockIndex(_program->get(), "Params");
  glUniformBlockBinding(_program->get(), params_index, params_binding_point);
  _params_buffer = std::make_unique<Buffer>(nullptr, sizeof(ParamsBlock));
  glBindBufferBase(
      GL_UNIFORM_BUFFER, params_binding_point, _params_buffer->get());

  reset_params();
}

void JuliaSetMaterial::use() {
  glUseProgram(_program->get());

  ParamsBlock params_block{};
  params_block._var1 = glm::vec4(_c_real, _c_imag, 0.0f, 0.0f);
  params_block._var2 = glm::vec4(_cx, _cy, _zoom, _escape);
  params_block._var3 = glm::ivec4(_max_iter, 0, 0, 0);

  glBindBuffer(GL_UNIFORM_BUFFER, _params_buffer->get());
  glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ParamsBlock), &params_block);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

bool JuliaSetMaterial::draw_ui() {
  bool changed = false;
  ImGui::Text("c = %.3f + %.3fi", _c_real, _c_imag);
  changed |= ImGui::SliderFloat("c_real", &_c_real, -1.0f, 1.0f, "%.4f");
  changed |= ImGui::SliderFloat("c_imag", &_c_imag, -1.0f, 1.0f, "%.4f");
  changed |= ImGui::SliderFloat("escape", &_escape, 0.0f, 1000.0f, "%.1f");
  changed |= ImGui::SliderInt("max_iter", &_max_iter, 1, 1000);

  ImGui::Text("Zoom in/out: Page Up/Down");
  ImGui::Text("Move: Left/Right/Up/Down");

  if (ImGui::Button("Reset")) {
    reset_params();
    changed = true;
  }

  return changed;
}

bool JuliaSetMaterial::key_callback(int key,
                                    int scancode,
                                    int action,
                                    int mods) {
  static int s_last_key = -1;
  static bool s_continue = false;

  if (action == GLFW_PRESS) {
    s_last_key = key;
    s_continue = true;
  }

  if (action == GLFW_RELEASE && s_continue) {
    s_continue = false;
    return false;
  }

  if (!s_continue) {
    return false;
  }

  bool vc = false; // value changed

  // move left/right/up/down
  float step = 10 * (1 / 800.0f) * _zoom;
  if (s_last_key == GLFW_KEY_LEFT) {
    _cx -= step;
    vc = true;
  } else if (s_last_key == GLFW_KEY_RIGHT) {
    _cx += step;
    vc = true;
  }
  if (s_last_key == GLFW_KEY_UP) {
    _cy += step;
    vc = true;
  } else if (s_last_key == GLFW_KEY_DOWN) {
    _cy -= step;
    vc = true;
  }

  // zoom in/out
  if (s_last_key == GLFW_KEY_PAGE_UP) {
    _zoom *= 0.95f;
    vc = true;
  } else if (s_last_key == GLFW_KEY_PAGE_DOWN) {
    _zoom *= 1.05f;
    vc = true;
  }

  return vc;
}

void JuliaSetMaterial::reset_params() {
  _c_real = -0.8f;
  _c_imag = 0.156f;
  _cx = 0;
  _cy = 0;
  _zoom = 1.0f;
  _escape = 100.0f;
  _max_iter = 200;
}
