#pragma once

#include "camera.hpp"
#include <GLFW/glfw3.h>

class FPSCamera final : public Camera {
public:
  FPSCamera() = default;
  FPSCamera(glm::vec3 position, float yaw, float pitch);

  glm::mat4 view() const override;
  glm::mat4 projection(float aspect) const override;
  glm::vec3 position() const override;
  void draw_ui() override;

  void set_window(GLFWwindow *window);

  void on_key(int key, int action);
  void on_mouse_button(int button, int action, int mods);
  void on_cursor_position(double xpos, double ypos);
  void on_scroll(double yoffset);
  void update(float dt);

  void set_enabled(bool enabled);
  bool is_enabled() const;

  void set_move_speed(float speed) {
    _move_speed = speed;
  }
  void set_sensitivity(float sens) {
    _sensitivity = sens;
  }

private:
  void recalculate_vectors();

  glm::vec3 _position{2.0f, 3.0f, 5.0f};
  glm::vec3 _forward{0.0f, 0.0f, -1.0f};
  glm::vec3 _right{1.0f, 0.0f, 0.0f};
  glm::vec3 _up{0.0f, 1.0f, 0.0f};

  float _yaw{-90.0f};
  float _pitch{-35.0f};
  float _fov{glm::radians(45.0f)};
  float _move_speed{5.0f};
  float _sensitivity{0.15f};
  float _near{0.1f};
  float _far{200.0f};

  bool _rotating{false};
  bool _panning{false};
  double _last_x{0.0};
  double _last_y{0.0};
  bool _first_mouse{true};

  bool _keys[6]{};
  bool _enabled{true};
};
