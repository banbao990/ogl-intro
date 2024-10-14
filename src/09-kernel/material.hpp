#pragma once

#include <common/shader.hpp>
#include <common/renderer.hpp>
#include <common/texture.hpp>

class KernelNewCenterMaterial : public IMaterial {
private:
  struct ParamsBlock {
    glm::ivec4 _var1;
  };

public:
  KernelNewCenterMaterial();
  void use();
  bool draw_ui();
  void draw();

  void reset_params();

private:
  std::unique_ptr<Program> _program;
  std::unique_ptr<Buffer> _params_buffer;
  GLuint _tex_input_location;
  std::unique_ptr<Mesh> _quad{};

  // ivec4
  int _kernel_size; // r * 2 + 1
};

class KernelOldCenterMaterial : public IMaterial {
private:
  struct ParamsBlock {
    glm::ivec4 _var1;
  };

public:
  KernelOldCenterMaterial();
  void use();
  bool draw_ui();
  void draw();

  void reset_params();

private:
  std::unique_ptr<Program> _program;
  std::unique_ptr<Buffer> _params_buffer;
  GLuint _tex_input_location;
  std::unique_ptr<Mesh> _quad{};

  // ivec4
  int _kernel_size; // r * 2 + 1
};
