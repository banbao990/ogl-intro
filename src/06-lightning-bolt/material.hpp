#pragma once

#include <common/shader.hpp>
#include <common/renderer.hpp>
#include <common/texture.hpp>

class BoltMaterial : public IMaterial {
public:
  BoltMaterial();
  void use();

private:
  std::unique_ptr<Program> _program;
};