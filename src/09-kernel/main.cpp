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

#include <07-fractal/material.hpp>
#include "material.hpp"

#define KE_WINDOW_WIDTH 800
#define KE_WINDOW_HEIGHT 800

class KernelEstimationApp final : public Application {
private:
  enum KernelType { NEW_CENTER, OLD_CENTER, NUM };

public:
  KernelEstimationApp()
      : Application("Kernel Estimation", KE_WINDOW_WIDTH, KE_WINDOW_HEIGHT) {}

private:
  void init() override {
    _material_new_center = std::make_unique<KernelNewCenterMaterial>();
    _material_old_center = std::make_unique<KernelOldCenterMaterial>();
    _blit_material = std::make_unique<BlitMaterial>();
    _renderer = std::make_unique<Renderer>();

    const char *tex_name = "texture.jpg";
    _tex_input = std::make_unique<Texture2D>(tex_name);
    _material_new_center->main_tex = _tex_input.get();
    _material_old_center->main_tex = _tex_input.get();

    glfwSetWindowSize(_window, _tex_input->width(), _tex_input->height());
  }

  void update() override {
    update_frame_buffer();
    draw_ui();
    draw();
  }

  void update_frame_buffer() {
    glfwGetFramebufferSize(_window, &_screen_fb_width, &_screen_fb_height);

    // make the size of offscreen buffer matches the screen's
    if (_color_attachment != nullptr &&
        _color_attachment->width() == _screen_fb_width &&
        _color_attachment->height() == _screen_fb_height) {
      return;
    }

    // resize
    _material_new_center->resize(_screen_fb_width, _screen_fb_height);
    _material_old_center->resize(_screen_fb_width, _screen_fb_height);

    // use HDR
    _color_attachment = std::make_unique<Texture2D>(nullptr,
                                                    GL_FLOAT,
                                                    _screen_fb_width,
                                                    _screen_fb_height,
                                                    GL_RGBA32F,
                                                    GL_RGBA);

    Texture2D *color_attachments[] = {_color_attachment.get()};
    _framebuffer = std::make_unique<Framebuffer>(
        color_attachments,
        static_cast<uint32_t>(std::size(color_attachments)),
        nullptr);
  }

  void draw_ui() override {
    Application::draw_ui();
    bool vc = false;

    int id = 0;
    if (ImGui::TreeNode("Kernel")) {
      static const char *kernel_types[KernelType::NUM] = {"new center",
                                                          "old center"};
      int kt = static_cast<int>(_kernel_type);
      ImGui::Combo(
          "Type", &kt, kernel_types, static_cast<int>(KernelType::NUM));
      _kernel_type = static_cast<KernelType>(kt);

      ImGui::PushID(id++);
      if (_kernel_type == NEW_CENTER) {
        vc |= _material_new_center->draw_ui();
      } else if (_kernel_type == OLD_CENTER) {
        vc |= _material_old_center->draw_ui();
      }
      ImGui::PopID();
      ImGui::TreePop();
    }
  }

  void draw() {
    glViewport(0, 0, _screen_fb_width, _screen_fb_height);

    // render to offscreen buffer
    glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer->get());
    glDisable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);

    if (_kernel_type == KernelType::NEW_CENTER) {
      _material_new_center->use();
      _material_new_center->draw();
    } else if (_kernel_type == KernelType::OLD_CENTER) {
      _material_old_center->use();
      _material_old_center->draw();
    }

    // blit texture to screen
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    _renderer->blit(_color_attachment.get(), _blit_material.get());
  }

private:
  std::unique_ptr<KernelNewCenterMaterial> _material_new_center{};
  std::unique_ptr<KernelOldCenterMaterial> _material_old_center{};

  std::unique_ptr<BlitMaterial> _blit_material{};
  std::unique_ptr<Renderer> _renderer{};
  std::unique_ptr<Framebuffer> _framebuffer{};
  std::unique_ptr<Texture2D> _color_attachment{};

  std::unique_ptr<Texture2D> _tex_input{};

  int _screen_fb_width{KE_WINDOW_HEIGHT}, _screen_fb_height{KE_WINDOW_WIDTH};

  KernelType _kernel_type{KernelType::NEW_CENTER};
};

int main() {
  try {
    KernelEstimationApp app{};
    app.run();
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
  }
}