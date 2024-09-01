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

class JuliaSetMaterial : public IMaterial {
public:
  JuliaSetMaterial();
  void use();
  bool draw_ui();
  bool key_callback(int key, int scancode, int action, int mods);

  void reset_params();

private:
  std::unique_ptr<Program> _program;
  std::unique_ptr<Buffer> _params_buffer;

  // _var1
  // x = x*x + c
  float _c_real, _c_imag;

  // _var2
  float _cx, _cy;
  float _zoom;
  float _escape;

  // _var3
  int _max_iter;
};
