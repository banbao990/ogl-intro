#include "water.hpp"
#include <common/mesh.hpp>
#include <glm/gtx/transform.hpp>

WaterGeometry::WaterGeometry(float width,
                             float height,
                             uint32_t num_x,
                             uint32_t num_y)
    : _width(width), _height(height), _num_x(num_x), _num_y(num_y) {

  std::vector<Mesh::Vertex> vertices;
  std::vector<uint32_t> indices;

  for (uint32_t y = 0; y < num_y; ++y) {
    for (uint32_t x = 0; x < num_x; ++x) {
      float u = static_cast<float>(x) / (num_x - 1);
      float v = static_cast<float>(y) / (num_y - 1);
      Mesh::Vertex vertex = {glm::vec3(u * 2.0f - 1.0f, 0.0f, v * 2.0f - 1.0f),
                             glm::vec3(0.0f, 1.0f, 0.0f),
                             {},
                             glm::vec2(u, v),
                             {},
                             {}};
      vertices.push_back(vertex);
    }
  }

  for (uint32_t y = 0; y < num_y - 1; ++y) {
    for (uint32_t x = 0; x < num_x - 1; ++x) {
      uint32_t i0 = y * num_x + x;
      uint32_t i1 = y * num_x + x + 1;
      uint32_t i2 = (y + 1) * num_x + x;
      uint32_t i3 = (y + 1) * num_x + x + 1;
      // face y up
      indices.push_back(i0);
      indices.push_back(i2);
      indices.push_back(i1);
      indices.push_back(i1);
      indices.push_back(i2);
      indices.push_back(i3);
    }
  }

  _mesh = std::make_unique<Mesh>(vertices.data(),
                                 (uint32_t)vertices.size(),
                                 indices.data(),
                                 (uint32_t)indices.size());
}

glm::mat4 WaterGeometry::transform() {
  return glm::scale(glm::vec3(_width, 1.0f, _height));
}

void WaterGeometry::draw() {
  _mesh->draw();
}
