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

// WaterMaterial

namespace {
struct TransformBlock {
  glm::mat4 MV;
  glm::mat4 I_MV;
  glm::mat4 P;
};

struct ParamsBlock {
  glm::vec4 light_dir_vs; // use glm::vec4 for padding
};
} // namespace

WaterMaterial::WaterMaterial() {
  _program = Program::create_from_files("shaders/08-ssr/water.vert",
                                        "shaders/08-ssr/water.frag");
  auto id = _program->get();

  GLuint transform_index = glGetUniformBlockIndex(_program->get(), "Transform");
  glUniformBlockBinding(_program->get(), transform_index, 0);
  GLuint params_index = glGetUniformBlockIndex(_program->get(), "Params");
  glUniformBlockBinding(_program->get(), params_index, 1);

  _transform_buffer = std::make_unique<Buffer>(nullptr, sizeof(TransformBlock));
  _params_buffer = std::make_unique<Buffer>(nullptr, sizeof(ParamsBlock));

  reset_params();
}

void WaterMaterial::use() {
  glUseProgram(_program->get());

  // transform uniforms
  TransformBlock transform_block{};
  transform_block.MV = view * model;
  transform_block.I_MV = glm::inverse(transform_block.MV);
  transform_block.P = projection;

  glBindBufferBase(GL_UNIFORM_BUFFER, 0, _transform_buffer->get());

  glBindBuffer(GL_UNIFORM_BUFFER, _transform_buffer->get());
  glBufferSubData(
      GL_UNIFORM_BUFFER, 0, sizeof(TransformBlock), &transform_block);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);

  // params uniforms
  ParamsBlock params_block{};
  params_block.light_dir_vs = glm::vec4(light_dir_vs, 0.0f);

  glBindBuffer(GL_UNIFORM_BUFFER, _params_buffer->get());
  glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ParamsBlock), &params_block);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);

  glBindBufferBase(GL_UNIFORM_BUFFER, 1, _params_buffer->get());

  glBindBuffer(GL_UNIFORM_BUFFER, _params_buffer->get());
  glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ParamsBlock), &params_block);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

bool WaterMaterial::draw_ui() {
  bool changed = false;

  if (ImGui::Button("Reset")) {
    reset_params();
    changed = true;
  }

  return changed;
}

bool WaterMaterial::key_callback(int key, int scancode, int action, int mods) {
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

void WaterMaterial::reset_params() {
  _c_real = -0.8f;
  _c_imag = 0.156f;
  _c_cayley = 1.0f;
  _delta_cayley = 1.0f;
  _cx = 0;
  _cy = 0;
  _zoom = 1.0f;
  _escape = 100.0f;
  _max_iter = 200;
  _square = false;
  _c_real_mb = 0.0f;
  _c_imag_mb = 0.0f;
}
