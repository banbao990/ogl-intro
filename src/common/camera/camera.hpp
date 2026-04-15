#pragma once

#include "../utils.hpp"
#include <glm/gtc/matrix_transform.hpp>

class Camera {
public:
  virtual ~Camera() = default;

  virtual glm::mat4 view() const = 0;
  virtual glm::mat4 projection(float aspect) const = 0;
  virtual glm::vec3 position() const = 0;
  virtual void draw_ui() {}
};
