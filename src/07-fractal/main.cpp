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

#define JULIA_SET_WINDOW_WIDTH 800
#define JULIA_SET_WINDOW_HEIGHT 800

class JuliaSetApp final : public Application {
public:
  JuliaSetApp()
      : Application(
            "Julia Set", JULIA_SET_WINDOW_WIDTH, JULIA_SET_WINDOW_HEIGHT) {}

private:
  void init() override {
    _material = std::make_unique<JuliaSetMaterial>();
    _blit_material = std::make_unique<BlitMaterial>();
    _renderer = std::make_unique<Renderer>();
  }

  void update() override {
    update_frame_buffer();
    draw_ui();
    draw();
  }

  void key_callback(int key, int scancode, int action, int mods) override {
    _should_update_tex |= _material->key_callback(key, scancode, action, mods);
  }

  void update_frame_buffer() {
    glfwGetFramebufferSize(_window, &_screen_fb_width, &_screen_fb_height);
    // make the size of offscreen buffer matches the screen's
    if (_color_attachment != nullptr &&
        _color_attachment->width() == _screen_fb_width &&
        _color_attachment->height() == _screen_fb_height) {
      return;
    }

    _should_update_tex = true;

    // use HDR
    _color_attachment = std::make_unique<Texture2D>(nullptr,
                                                    GL_FLOAT,
                                                    _screen_fb_width,
                                                    _screen_fb_height,
                                                    GL_RGBA32F,
                                                    GL_RGBA);
    glBindImageTexture(
        0, _color_attachment->get(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
  }

  void draw_ui() override {
    Application::draw_ui();

    int id = 0;
    if (ImGui::TreeNode("Graph")) {
      ImGui::PushID(id++);
      _should_update_tex |= _material->draw_ui();
      ImGui::PopID();
      ImGui::TreePop();
    }
  }

  void draw() {
    glViewport(0, 0, _screen_fb_width, _screen_fb_height);

    if (_should_update_tex) {
      _material->use();
      glDispatchCompute(_screen_fb_width / 16, _screen_fb_height / 16, 1);

      // make sure writing to image has finished before read
      glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

      _should_update_tex = false;
    }

    // blit texture to screen
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    _renderer->blit(_color_attachment.get(), _blit_material.get());
  }

private:
  std::unique_ptr<JuliaSetMaterial> _material{};
  std::unique_ptr<BlitMaterial> _blit_material{};
  std::unique_ptr<Renderer> _renderer{};
  std::unique_ptr<Framebuffer> _framebuffer{};
  std::unique_ptr<Texture2D> _color_attachment{};
  int _screen_fb_width{JULIA_SET_WINDOW_HEIGHT},
      _screen_fb_height{JULIA_SET_WINDOW_WIDTH};
  bool _should_update_tex{true};
  bool _vsync{true};
};

int main() {
  try {
    JuliaSetApp app{};
    app.run();
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
  }
}