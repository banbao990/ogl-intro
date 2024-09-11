#pragma once

#include <common/shader.hpp>
#include <common/renderer.hpp>
#include <common/texture.hpp>

class BlitMaterial : public IMaterial {
public:
  BlitMaterial();
  void use() override;

private:
  std::unique_ptr<Program> _program;
  GLint _transform_location; // for vertex shader
  GLint _image_location;
};

class WaterMaterial : public IMaterial {
private:
public:
  WaterMaterial();
  void use();
  bool draw_ui();
  bool key_callback(int key, int scancode, int action, int mods);

  void reset_params();

public:
  glm::vec3 light_dir_vs;

private:
  std::unique_ptr<Program> _program;

  std::unique_ptr<Buffer> _params_buffer;
  std::unique_ptr<Buffer> _transform_buffer;

  GLuint _wave_tex_location;
  std::unique_ptr<Texture2D> _wave_tex;

  // _var1: float4
  glm::vec2 _wave_speed1;
  glm::vec2 _wave_speed2;

  // _var2: float4
  // float _time_seconds;
  float _wave_strength;
};
