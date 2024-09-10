#pragma once
#include <cstdint>
#include <memory>

#include <common/mesh.hpp>

class WaterGeometry {
public:
  WaterGeometry(float width, float height, uint32_t num_x, uint32_t num_y);
  glm::mat4 transform();
  void draw();

private:
  float _width{1.0f}, _height{1.0f};
  uint32_t _num_x{0u}, _num_y{0u};
  std::unique_ptr<Mesh> _mesh;
};