#pragma once

#include "camera.hpp"
#include <imgui/imgui.h>

class ModelViewerCamera final : public Camera {
public:
  ModelViewerCamera() = default;
  ModelViewerCamera(float focus_height,
                    float field_of_view,
                    float pitch,
                    float yaw,
                    float distance);

  void draw_ui() override;
  glm::mat4 view() const override;
  glm::mat4 projection(float aspect) const override;
  glm::vec3 position() const override;

private:
  float _focus_height = 0.25f;
  float _field_of_view = glm::radians(25.0f);
  float _pitch = glm::radians(60.0f);
  float _yaw = glm::radians(-85.0f);
  float _distance = 3.0f;
};
