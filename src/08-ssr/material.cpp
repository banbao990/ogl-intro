#include "material.hpp"
#include <imgui/imgui.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

// DepthMaterial

DepthMaterial::DepthMaterial() {
  _program = Program::create_from_files("shaders/08-ssr/depth.vert",
                                        "shader/08-ssr/depth.frag");
  _transform_location = glGetUniformLocation(_program->get(), "transform");
}

void DepthMaterial::use() {
  glUseProgram(_program->get());
  glm::mat4 transform = projection * model * view;
  glUniformMatrix4fv(_transform_location, 1, false, (GLfloat *)&transform);
}

// WaterMaterial

namespace {} // namespace

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
  _depth_tex_location = glGetUniformLocation(id, "g_depth_tex");

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
  params_block._var2 = glm::vec4(
      time_seconds, _wave_strength, _refraction_distortion_strength, 0.0f);
  params_block._var3 = glm::ivec4(_windows_width, _windows_height, 0, 0);

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

  // depth texture
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, _depth_tex->get());
  glUniform1i(_depth_tex_location, 1);
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
  ImGui::SliderFloat("Refraction Distortion Strength",
                     &_refraction_distortion_strength,
                     0.0f,
                     1.0f);

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
  _refraction_distortion_strength = 0.1f;
}

void WaterMaterial::set_depth_tex(std::shared_ptr<Texture2D> depth_tex) {
  _depth_tex = depth_tex;
}

void WaterMaterial::update_window_size(uint32_t width, uint32_t height) {
  _windows_height = height;
  _windows_width = width;
}
