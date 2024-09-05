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
private:
  enum ImageMode { kJuliaSet = 0, kMandelbrot = 1, kCayley = 2, kCount = 3 };
  const char *JuliaSetMaterial::_image_mode_names[kCount] = {
      "Julia Set", "Mandelbrot", "Cayley"};

public:
  JuliaSetMaterial();
  void use();
  bool draw_ui();
  bool key_callback(int key, int scancode, int action, int mods);

  void reset_params();

private:
  std::unique_ptr<Program> _program;
  std::unique_ptr<Buffer> _params_buffer;

  // _var1: float4
  // x = x*x + c
  float _c_real, _c_imag;
  // z^3 - c_cayley = 0
  float _c_cayley;
  float _delta_cayley;

  // _var2: float4
  float _cx, _cy;
  float _zoom;
  float _escape;

  // _var3: int4
  int _max_iter;
  ImageMode _image_mode{ImageMode::kMandelbrot};
  bool _square;

  // _var4: float4
  float _c_real_mb, _c_imag_mb;
};
