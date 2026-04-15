#pragma once

#include "utils.hpp"
#include <chrono>
#include <vector>
#include <sstream>
#include <imgui/imgui.h>

struct GLFWwindow;

class Application {
public:
  Application(const char *name, int width, int height, bool fix_size = false);
  virtual ~Application();

  void run();
  float average_frame_time();
  void request_screen_shot();
  void toggle_profiler_ui();

protected:
  virtual void init() {}
  virtual void update() {}
  virtual void key_callback(int key, int scancode, int action, int mods) {}
  virtual void character_callback(unsigned int codepoint) {}
  virtual void cursor_position_callback(double xpos, double ypos) {}
  virtual void scroll_callback(double xoffset, double yoffset) {}
  virtual void mouse_button_callback(int button, int action, int mods) {}
  virtual void cursor_enter_callback(bool entered) {}

  virtual void draw_ui();

  GLFWwindow *_window{};

private:
  [[nodiscard]] bool should_draw_profiler_ui() const;
  void draw_profiler_ui() const;

  GLFWwindow *
  create_window(const char *name, int width, int height, bool fix_size);

  static void window_key_callback(
      GLFWwindow *window, int key, int scancode, int action, int mods);

  static void window_character_callback(GLFWwindow *window,
                                        unsigned int codepoint);

  static void
  window_cursor_position_callback(GLFWwindow *window, double xpos, double ypos);

  static void
  window_scroll_callback(GLFWwindow *window, double xoffset, double yoffset);

  static void window_mouse_button_callback(GLFWwindow *window,
                                           int button,
                                           int action,
                                           int mods);

  static void window_cursor_enter_callback(GLFWwindow *window, int entered);

  void screen_shot();

  bool _vsync = true;
  bool _need_screen_shot = false;
  bool _display_profiler = false;
  using Clock = std::chrono::high_resolution_clock;
  using TimeSample = Clock::time_point;
  FixSizeQueue<TimeSample> _frame_time_samples;
};

#include "camera/model_viewer_camera.hpp"
