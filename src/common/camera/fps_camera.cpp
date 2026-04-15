#include "fps_camera.hpp"
#include <imgui/imgui.h>
#include <algorithm>

FPSCamera::FPSCamera(glm::vec3 position, float yaw, float pitch)
    : _position(position), _yaw(yaw), _pitch(pitch) {
  recalculate_vectors();
}

void FPSCamera::recalculate_vectors() {
  float yr = glm::radians(_yaw);
  float pr = glm::radians(_pitch);
  _forward =
      glm::normalize(glm::vec3(cos(pr) * cos(yr), sin(pr), cos(pr) * sin(yr)));
  _right = glm::normalize(glm::cross(_forward, glm::vec3(0, 1, 0)));
  _up = glm::vec3(0, 1, 0);
}

void FPSCamera::set_window(GLFWwindow *window) {
  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

glm::vec3 FPSCamera::position() const {
  return _position;
}

glm::mat4 FPSCamera::view() const {
  return glm::lookAt(_position, _position + _forward, glm::vec3(0, 1, 0));
}

glm::mat4 FPSCamera::projection(float aspect) const {
  return glm::perspective(_fov, aspect, _near, _far);
}

void FPSCamera::draw_ui() {
  ImGui::Checkbox("Enable Camera Control", &_enabled);
  if (!_enabled) {
    ImGui::Text("Camera control disabled");
    return;
  }
  ImGui::SliderFloat("Move Speed", &_move_speed, 0.5f, 50.0f);
  ImGui::SliderFloat("Sensitivity", &_sensitivity, 0.01f, 1.0f);
  ImGui::SliderAngle("Field Of View", &_fov, 5.0f, 170.0f);
  ImGui::Text("Pos: %.2f, %.2f, %.2f", _position.x, _position.y, _position.z);
  ImGui::Text("Yaw: %.1f  Pitch: %.1f", _yaw, _pitch);
}

void FPSCamera::on_key(int key, int action) {
  if (!_enabled)
    return;
  if (action == GLFW_PRESS || action == GLFW_RELEASE) {
    bool pressed = (action == GLFW_PRESS);
    switch (key) {
    case GLFW_KEY_W:
      _keys[0] = pressed;
      break;
    case GLFW_KEY_S:
      _keys[1] = pressed;
      break;
    case GLFW_KEY_A:
      _keys[2] = pressed;
      break;
    case GLFW_KEY_D:
      _keys[3] = pressed;
      break;
    case GLFW_KEY_Q:
      _keys[4] = pressed;
      break;
    case GLFW_KEY_E:
      _keys[5] = pressed;
      break;
    }
  }
}

void FPSCamera::on_mouse_button(int button, int action, int mods) {
  if (!_enabled)
    return;
  if (button == GLFW_MOUSE_BUTTON_LEFT) {
    if (action == GLFW_PRESS) {
      if (mods & GLFW_MOD_SHIFT) {
        _panning = true;
      } else {
        _rotating = true;
      }
      _first_mouse = true;
    } else if (action == GLFW_RELEASE) {
      _rotating = false;
      _panning = false;
    }
  }
}

void FPSCamera::on_cursor_position(double xpos, double ypos) {
  if (!_enabled)
    return;
  if (_first_mouse) {
    _last_x = xpos;
    _last_y = ypos;
    _first_mouse = false;
  }

  double dx = xpos - _last_x;
  double dy = ypos - _last_y;
  _last_x = xpos;
  _last_y = ypos;

  if (_rotating) {
    _yaw += (float)dx * _sensitivity;
    _pitch -= (float)dy * _sensitivity;
    _pitch = std::clamp(_pitch, -89.0f, 89.0f);
    recalculate_vectors();
  }

  if (_panning) {
    _position -= _right * (float)dx * _sensitivity * 0.1f;
    _position += glm::vec3(0, 1, 0) * (float)dy * _sensitivity * 0.1f;
  }
}

void FPSCamera::on_scroll(double yoffset) {
  if (!_enabled)
    return;
  _fov -= (float)yoffset * glm::radians(2.0f);
  _fov = std::clamp(_fov, glm::radians(5.0f), glm::radians(170.0f));
}

void FPSCamera::update(float dt) {
  if (!_enabled)
    return;
  float velocity = _move_speed * dt;
  if (_keys[0])
    _position += _forward * velocity;
  if (_keys[1])
    _position -= _forward * velocity;
  if (_keys[2])
    _position -= _right * velocity;
  if (_keys[3])
    _position += _right * velocity;
  if (_keys[4])
    _position -= glm::vec3(0, 1, 0) * velocity;
  if (_keys[5])
    _position += glm::vec3(0, 1, 0) * velocity;
}

void FPSCamera::set_enabled(bool enabled) {
  _enabled = enabled;
}

bool FPSCamera::is_enabled() const {
  return _enabled;
}
