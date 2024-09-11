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
  glm::mat4 _M;
  glm::mat4 _MV;
  glm::mat4 _I_MV;
  glm::mat4 _P;
};

struct ParamsBlock {
  glm::vec4 _light_dir_vs; // use glm::vec4 for padding
  glm::vec4 _var1;
  glm::vec4 _var2;
};
} // namespace

WaterMaterial::WaterMaterial() {
  _program = Program::create_from_files("shaders/08-ssr/water.vert",
                                        "shaders/08-ssr/water.frag");
  const GLuint id = _program->get();

  GLuint transform_index = glGetUniformBlockIndex(id, "Transform");
  glUniformBlockBinding(id, transform_index, 0);
  GLuint params_index = glGetUniformBlockIndex(id, "Params");
  glUniformBlockBinding(id, params_index, 1);

  _transform_buffer = std::make_unique<Buffer>(nullptr, sizeof(TransformBlock));
  _params_buffer = std::make_unique<Buffer>(nullptr, sizeof(ParamsBlock));

  _wave_tex = std::make_unique<Texture2D>("08-ssr/water2.png");
  _wave_tex_location = glGetUniformLocation(id, "g_wave_tex");

  reset_params();
}

void WaterMaterial::use() {
  glUseProgram(_program->get());

  // transform uniforms
  TransformBlock transform_block{};
  transform_block._M = model;
  transform_block._MV = view * model;
  transform_block._I_MV = glm::inverse(transform_block._MV);
  transform_block._P = projection;

  glBindBufferBase(GL_UNIFORM_BUFFER, 0, _transform_buffer->get());

  glBindBuffer(GL_UNIFORM_BUFFER, _transform_buffer->get());
  glBufferSubData(
      GL_UNIFORM_BUFFER, 0, sizeof(TransformBlock), &transform_block);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);

  // params uniforms
  float time_seconds = (float)glfwGetTime(); // time in seconds
  ParamsBlock params_block{};
  params_block._light_dir_vs = glm::vec4(light_dir_vs, 0.0f);
  params_block._var1 = glm::vec4(_wave_speed1, _wave_speed2);
  params_block._var2 = glm::vec4(time_seconds, _wave_strength, 0.0f, 0.0f);

  glBindBuffer(GL_UNIFORM_BUFFER, _params_buffer->get());
  glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ParamsBlock), &params_block);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);

  glBindBufferBase(GL_UNIFORM_BUFFER, 1, _params_buffer->get());

  glBindBuffer(GL_UNIFORM_BUFFER, _params_buffer->get());
  glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ParamsBlock), &params_block);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);

  // wave texture
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, _wave_tex->get());
  glUniform1i(_wave_tex_location, 0);
}

bool WaterMaterial::draw_ui() {
  bool changed = false;

  if (ImGui::Button("Reset")) {
    reset_params();
    changed = true;
  }

  {
    ImGui::Text("Wave Speed 1");
    ImGui::SliderFloat("X##speed1", &_wave_speed1.x, -1.0f, 1.0f);
    ImGui::SliderFloat("Y##speed1", &_wave_speed1.y, -1.0f, 1.0f);
    ImGui::Text("Wave Speed 2");
    ImGui::SliderFloat("X##speed2", &_wave_speed2.x, -1.0f, 1.0f);
    ImGui::SliderFloat("Y##speed2", &_wave_speed2.y, -1.0f, 1.0f);
  }

  ImGui::SliderFloat("Wave Strength", &_wave_strength, 0.0f, 1.0f);

  // show texture
  ImGui::Text("Wave Texture");
  ImGui::Image((ImTextureID)(intptr_t)_wave_tex->get(), ImVec2(128, 128));

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

  return vc;
}

void WaterMaterial::reset_params() {
  _wave_speed1 = glm::vec2(0.1f, 0.1f);
  _wave_speed2 = glm::vec2(0.05f, 0.05f);
  _wave_strength = 0.5f;
}
