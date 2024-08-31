#include <GL/glew.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <iostream>
#include <optional>
#include <vector>
#include <sstream>
#include <array>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>

#include <stb_image_write.h>

#include <common/application.hpp>
#include <common/framebuffer.hpp>

#include "material.hpp"
#include "mesh.hpp"

#define BOLT_WINDOW_WIDTH 800
#define BOLT_WINDOW_HEIGHT 600

class BoltApp final : public Application {
public:
  BoltApp()
      : Application("Lightning Bolt", BOLT_WINDOW_WIDTH, BOLT_WINDOW_HEIGHT) {}

private:
  void init() override {
    _bolt_material = std::make_unique<BoltMaterial>();
    _bolt_mesh = std::make_unique<BoltMesh>();
  }

  void update() override {
    update_frame_buffer();
    draw_ui();
    draw();
  }

  void update_frame_buffer() {
    glfwGetFramebufferSize(_window, &_screen_fb_width, &_screen_fb_height);
  }

  void draw_ui() {
    int id = 0;
    {
      std::stringstream ss;
      float frame_time = average_frame_time();
      ss << "FPS: ";
      if (frame_time == 0.0f) {
        ss << "NAN";
      } else {
        ss << 1.0f / frame_time;
      }
      ss << "(" << frame_time * 1000.0f << "ms)";
      ImGui::Text("%s", ss.str().c_str());
    }
    if (ImGui::Button("Screen Shot")) {
      request_screen_shot();
    }
    if (ImGui::Button("Toggle Profiler")) {
      toggle_profiler_ui();
    }

    if (ImGui::TreeNode("Bolt")) {
      ImGui::PushID(id++);
      _bolt_mesh->draw_ui();
      ImGui::PopID();
      ImGui::TreePop();
    }
  }

  void draw() {
    // render to screen
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glViewport(0, 0, _screen_fb_width, _screen_fb_height);
    glClear(GL_COLOR_BUFFER_BIT);

    _bolt_material->use();
    _bolt_mesh->draw();
  }

private:
  std::unique_ptr<BoltMesh> _bolt_mesh{};
  std::unique_ptr<BoltMaterial> _bolt_material{};
  int _screen_fb_width{BOLT_WINDOW_HEIGHT},
      _screen_fb_height{BOLT_WINDOW_WIDTH};
};

int main() {
  try {
    BoltApp app{};
    app.run();
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
  }
}