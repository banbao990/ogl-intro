#include "material.hpp"
#include <imgui/imgui.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iterator>

// DepthMaterial

DepthMaterial::DepthMaterial() {
  _program = Program::create_from_files("shaders/08-ssr/depth.vert",
                                        "shaders/08-ssr/depth.frag");
  _transform_location = glGetUniformLocation(_program->get(), "transform");
}

void DepthMaterial::use() {
  glUseProgram(_program->get());
  glm::mat4 transform = projection * view * model;
  glUniformMatrix4fv(_transform_location, 1, false, (GLfloat *)&transform);
}

// WaterMaterial

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
  _scene_color_tex_location = glGetUniformLocation(id, "g_scene_color_tex");
  _scene_depth_tex_location = glGetUniformLocation(id, "g_scene_depth_tex");
  _hiz_tex_location = glGetUniformLocation(id, "g_hiz_tex");

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
  transform_block._I_P = glm::inverse(projection);

  glBindBufferBase(GL_UNIFORM_BUFFER, 0, _transform_buffer->get());

  glBindBuffer(GL_UNIFORM_BUFFER, _transform_buffer->get());
  glBufferSubData(
      GL_UNIFORM_BUFFER, 0, sizeof(TransformBlock), &transform_block);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);

  // params uniforms
  float time_seconds = (float)glfwGetTime(); // time in seconds
  ParamsBlock params_block{};
  params_block._light_dir_vs = glm::vec4(light_dir_vs, 0.0f);
  params_block._light_radiance = glm::vec4(light_radiance, 0.0f);
  params_block._env_radiance = glm::vec4(env_radiance, 1.0f);
  params_block._wave_speeds = glm::vec4(_wave_speed1, _wave_speed2);
  params_block._wave_params =
      glm::vec4(_wave_global_speed, _wave_scale1, _wave_scale2, 0.0f);
  params_block._water_params0 = glm::vec4(time_seconds,
                                          _wave_strength,
                                          _refraction_distortion_strength,
                                          _fresnel_f0);
  params_block._water_params1 = glm::vec4(_absorption_strength,
                                          _specular_strength,
                                          _edge_fade_width,
                                          _max_water_depth);
  params_block._absorption = glm::vec4(_absorption_coeff, 0.0f);
  params_block._ssr_params0 = glm::vec4(
      _ssr_step_size, _ssr_max_distance, _ssr_thickness, _ssr_origin_bias);
  params_block._camera_params = glm::vec4(_near_plane, _far_plane, 0.0f, 0.0f);
  params_block._screen_params = glm::ivec4(static_cast<int>(_window_width),
                                           static_cast<int>(_window_height),
                                           _ssr_max_steps,
                                           _ssr_binary_steps);
  params_block._mode_params = glm::ivec4(static_cast<int>(_ssr_mode),
                                         static_cast<int>(_debug_view),
                                         _hiz_max_mip,
                                         _flat_water ? 1 : 0);

  glBindBuffer(GL_UNIFORM_BUFFER, _params_buffer->get());
  glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ParamsBlock), &params_block);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);

  glBindBufferBase(GL_UNIFORM_BUFFER, 1, _params_buffer->get());

  // wave texture
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, _wave_tex->get());
  glUniform1i(_wave_tex_location, 0);

  // scene color texture
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D,
                _scene_color_tex != nullptr ? _scene_color_tex->get() : 0);
  glUniform1i(_scene_color_tex_location, 1);

  // scene depth texture
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D,
                _scene_depth_tex != nullptr ? _scene_depth_tex->get() : 0);
  glUniform1i(_scene_depth_tex_location, 2);

  // hierarchical depth texture
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, _hiz_tex);
  glUniform1i(_hiz_tex_location, 3);
}

bool WaterMaterial::draw_ui() {
  bool changed = false;
  bool resize_window = false;

  if (ImGui::Button("Reset")) {
    const bool was_flat_water = _flat_water;
    const SSRMode previous_ssr_mode = _ssr_mode;
    reset_params();
    resize_window =
        was_flat_water != _flat_water || previous_ssr_mode != _ssr_mode;
    changed = true;
  }

  static const char *ssr_modes[] = {
      "Off", "Fixed Step", "Pixel DDA (1 px)", "Hi-Z"};
  int ssr_mode = static_cast<int>(_ssr_mode);
  if (ImGui::Combo("SSR Mode", &ssr_mode, ssr_modes, std::size(ssr_modes))) {
    _ssr_mode = static_cast<SSRMode>(ssr_mode);
    resize_window = true;
    changed = true;
  }

  static const char *debug_views[] = {"Final Composite",
                                      "SSR Only",
                                      "Linear Scene Depth",
                                      "Hit Mask / Confidence",
                                      "Hit UV",
                                      "Iteration Count"};
  int debug_view = static_cast<int>(_debug_view);
  if (ImGui::Combo(
          "Debug View", &debug_view, debug_views, std::size(debug_views))) {
    _debug_view = static_cast<DebugView>(debug_view);
    changed = true;
  }

  if (ImGui::Checkbox("Flat Water (Test)", &_flat_water)) {
    resize_window = true;
    changed = true;
  }

  ImGui::SeparatorText("Ray Marching");
  changed |= ImGui::SliderInt("Max Steps", &_ssr_max_steps, 8, 4096);
  if (_ssr_mode == SSRMode::PixelDDA) {
    ImGui::TextDisabled("Perspective-correct, 1 pixel cell per step");
    ImGui::TextDisabled("Bidirectional cell crossing + continuity bridge");
  } else if (_ssr_mode == SSRMode::HiZ) {
    ImGui::TextDisabled("Conservative min-depth skip; DDA hit confirmation");
  } else if (_ssr_mode == SSRMode::FixedStep) {
    ImGui::TextDisabled(
        "Bidirectional valid bracket; discontinuities rejected");
    changed |= ImGui::SliderInt("Binary Steps", &_ssr_binary_steps, 0, 8);
  }
  if (_ssr_mode == SSRMode::FixedStep) {
    changed |=
        ImGui::SliderFloat("Step Size", &_ssr_step_size, 0.01f, 1.0f, "%.3f");
  }
  changed |= ImGui::SliderFloat(
      "Max Distance", &_ssr_max_distance, 0.5f, 100.0f, "%.2f");
  if (_ssr_mode == SSRMode::FixedStep) {
    const float step_budget_distance =
        _ssr_step_size * static_cast<float>(_ssr_max_steps);
    const float effective_distance = step_budget_distance < _ssr_max_distance
                                         ? step_budget_distance
                                         : _ssr_max_distance;
    ImGui::TextDisabled("Nominal effective range: %.2f view units",
                        effective_distance);
    if (step_budget_distance < _ssr_max_distance) {
      ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
                         "Limited by Max Steps x Step Size");
    } else {
      ImGui::TextDisabled("Step budget covers Max Distance");
    }
    changed |=
        ImGui::SliderFloat("Thickness", &_ssr_thickness, 0.001f, 2.0f, "%.3f");
  }
  changed |=
      ImGui::SliderFloat("Origin Bias", &_ssr_origin_bias, 0.0f, 0.25f, "%.3f");
  changed |=
      ImGui::SliderFloat("Edge Fade", &_edge_fade_width, 0.001f, 0.5f, "%.3f");

  ImGui::SeparatorText("Water Shading");
  if (_flat_water) {
    ImGui::TextDisabled("Wave motion and distortion disabled");
  } else {
    changed |= ImGui::SliderFloat(
        "Wave Global Speed", &_wave_global_speed, 0.0f, 10.0f);
    changed |= ImGui::SliderFloat("Wave Scale 1", &_wave_scale1, 0.0f, 20.0f);
    changed |=
        ImGui::SliderFloat2("Wave Speed 1", &_wave_speed1.x, -1.0f, 1.0f);
    changed |= ImGui::SliderFloat("Wave Scale 2", &_wave_scale2, 0.0f, 20.0f);
    changed |=
        ImGui::SliderFloat2("Wave Speed 2", &_wave_speed2.x, -1.0f, 1.0f);
    changed |= ImGui::SliderFloat("Wave Strength", &_wave_strength, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Refraction Distortion",
                                  &_refraction_distortion_strength,
                                  0.0f,
                                  0.1f,
                                  "%.3f");
  }
  changed |= ImGui::SliderFloat("Fresnel F0", &_fresnel_f0, 0.0f, 0.2f, "%.3f");
  changed |= ImGui::ColorEdit3("Absorption Coefficient", &_absorption_coeff.x);
  changed |= ImGui::SliderFloat(
      "Absorption Strength", &_absorption_strength, 0.0f, 4.0f);
  changed |=
      ImGui::SliderFloat("Specular Strength", &_specular_strength, 0.0f, 4.0f);
  changed |=
      ImGui::SliderFloat("Max Water Depth", &_max_water_depth, 0.1f, 100.0f);

  if (!_flat_water) {
    ImGui::Text("Wave Texture");
    ImGui::Image((ImTextureID)(intptr_t)_wave_tex->get(), ImVec2(128, 128));
  }

  if (resize_window) {
    // Re-run ImGui auto-fit for two frames so hiding/showing all wave controls
    // also shrinks/grows the containing controls window.
    ImGui::SetWindowSize(ImVec2(0.0f, 0.0f));
  }

  return changed;
}

void WaterMaterial::reset_params() {
  _wave_speed1 = glm::vec2(0.1f, 0.1f);
  _wave_speed2 = glm::vec2(0.05f, 0.05f);
  _wave_global_speed = 1.0f;
  _wave_scale1 = 2.0f;
  _wave_scale2 = 10.0f;
  _wave_strength = 0.1f;
  _flat_water = false;
  _refraction_distortion_strength = 0.02f;
  _fresnel_f0 = 0.2f;
  _absorption_coeff = glm::vec3(0.8f, 0.25f, 0.12f);
  _absorption_strength = 0.35f;
  _specular_strength = 0.8f;
  _edge_fade_width = 0.08f;
  _max_water_depth = 20.0f;

  _ssr_step_size = 0.1f;
  _ssr_max_distance = 20.0f;
  _ssr_thickness = 0.03f;
  _ssr_origin_bias = 0.005f;
  _ssr_max_steps = 4096;
  _ssr_binary_steps = 5;
  _ssr_mode = SSRMode::PixelDDA;
  _debug_view = DebugView::FinalComposite;
}

void WaterMaterial::set_scene_textures(Texture2D *color_tex,
                                       Texture2D *depth_tex) {
  _scene_color_tex = color_tex;
  _scene_depth_tex = depth_tex;
}

void WaterMaterial::set_depth_pyramid(GLuint texture, int max_mip_level) {
  _hiz_tex = texture;
  _hiz_max_mip = max_mip_level;
}

void WaterMaterial::update_window_size(uint32_t width, uint32_t height) {
  _window_height = height;
  _window_width = width;
}

bool WaterMaterial::uses_hiz() const {
  return _ssr_mode == SSRMode::HiZ;
}
