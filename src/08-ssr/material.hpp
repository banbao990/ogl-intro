#pragma once

#include <common/shader.hpp>
#include <common/renderer.hpp>
#include <common/texture.hpp>

class DepthMaterial : public IMaterial {
public:
  DepthMaterial();
  void use() override;

private:
  std::unique_ptr<Program> _program;
  GLint _transform_location;
};

class WaterMaterial : public IMaterial {
private:
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
    glm::ivec4 _var3;
  };

public:
  WaterMaterial();
  void use();
  bool draw_ui();
  bool key_callback(int key, int scancode, int action, int mods);

  void reset_params();
  void set_depth_tex(std::shared_ptr<Texture2D> depth_tex);
  void update_window_size(uint32_t width, uint32_t height);

public:
  glm::vec3 light_dir_vs;

private:
  std::unique_ptr<Program> _program;

  std::unique_ptr<Buffer> _params_buffer;
  std::unique_ptr<Buffer> _transform_buffer;

  GLuint _wave_tex_location;
  std::unique_ptr<Texture2D> _wave_tex;
  GLuint _depth_tex_location;
  std::shared_ptr<Texture2D> _depth_tex;

  // _var1: float4
  glm::vec2 _wave_speed1;
  glm::vec2 _wave_speed2;

  // _var2: float4
  // float _time_seconds;
  float _wave_strength;
  float _refraction_distortion_strength;

  // _var3: int4
  uint32_t _windows_width, _windows_height;
};
