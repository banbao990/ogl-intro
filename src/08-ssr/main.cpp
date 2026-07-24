#include <common/application.hpp>
#include <common/framebuffer.hpp>
#include <common/gltf.hpp>
#include <common/profile.h>
#include <common/renderer.hpp>
#include <common/shader.hpp>
#include <common/utils.hpp>

#include <05-pbr/material.hpp>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <common/camera/fps_camera.hpp>
#include <glm/glm.hpp>
#include <imgui/imgui.h>
#include <iostream>
#include <sstream>
#include <tiny_gltf.h>
#include <vector>

#include "material.hpp"
#include "depth_pyramid.hpp"
#include "water.hpp"

#define SSR_WINDOW_WIDTH 800
#define SSR_WINDOW_HEIGHT 600

class SSRApp final : public Application {
public:
  SSRApp()
      : Application(
            "Screen Space Reflection", SSR_WINDOW_WIDTH, SSR_WINDOW_HEIGHT),
        _screen_fb_width(SSR_WINDOW_WIDTH),
        _screen_fb_height(SSR_WINDOW_HEIGHT) {}

private:
  void init() override {
    _camera = std::make_unique<FPSCamera>(
        glm::vec3(1.10f, 0.51f, 0.67f), 214.1f, -21.6f);
    _camera->set_window(_window);
    _scene = std::make_unique<Gltf>("FlightHelmet/FlightHelmet.gltf");
    _tone_mapping_material = std::make_unique<ToneMappingMaterial>();
    _renderer = std::make_unique<Renderer>();

    // water material
    _water_material = std::make_unique<WaterMaterial>();
    _water_geometry = std::make_unique<WaterGeometry>(1.0f, 1.0f, 100, 100);
    _depth_pyramid = std::make_unique<DepthPyramid>();

    auto init_mat = [&](PbrMaterial *pbr_mat, Gltf::Material *mat) {
#define ASSIGN_TEXTURE(name)                                                   \
  pbr_mat->name = mat->name < 0 ? nullptr : _scene->textures[mat->name].get()
#define ASSIGN_FIELD(name) pbr_mat->name = mat->name
      ASSIGN_TEXTURE(base_color);
      ASSIGN_FIELD(base_color_factor);
      ASSIGN_FIELD(double_sided);
      ASSIGN_FIELD(metallic_factor);
      ASSIGN_FIELD(roughness_factor);
      ASSIGN_TEXTURE(metallic_roughness);
      ASSIGN_TEXTURE(normal);
      ASSIGN_FIELD(normal_scale);
      ASSIGN_TEXTURE(occlusion);
      ASSIGN_FIELD(occlusion_strength);
      ASSIGN_TEXTURE(emission);
      ASSIGN_FIELD(emission_factor);

#undef ASSIGN_TEXTURE
#undef ASSIGN_FIELD

      switch (mat->mode) {
      case Gltf::Material::Opaque:
        pbr_mat->mode = PbrMaterial::Opaque;
        break;
      case Gltf::Material::Blend:
        pbr_mat->mode = PbrMaterial::Blend;
        break;
      }
    };

    for (auto &mat : _scene->materials) {
      auto pbr_mat = std::make_unique<PbrMaterial>(false);
      init_mat(pbr_mat.get(), mat.get());
      auto base_color_mat = std::make_unique<PbrMaterial>(true);
      init_mat(base_color_mat.get(), mat.get());

      _pbr_materials.emplace_back(std::move(pbr_mat));
      _base_color_materials.emplace_back(std::move(base_color_mat));
    }

    _env_brdf_material = std::make_unique<PrecomputeEnvBrdfMaterial>();
    calculate_env_brdf_lut();
    _last_time = glfwGetTime();
  }

  void key_callback(int key, int scancode, int action, int mods) override {
    (void)scancode;
    (void)mods;
    if (!ImGui::GetIO().WantCaptureKeyboard || action == GLFW_RELEASE) {
      _camera->on_key(key, action);
    }
  }

  void mouse_button_callback(int button, int action, int mods) override {
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS &&
        !ImGui::GetIO().WantCaptureMouse) {
      _camera_mouse_active = true;
      _camera->on_mouse_button(button, action, mods);
    } else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
      if (_camera_mouse_active) {
        _camera->on_mouse_button(button, action, mods);
      }
      _camera_mouse_active = false;
    }
  }

  void cursor_position_callback(double xpos, double ypos) override {
    if (_camera_mouse_active || !ImGui::GetIO().WantCaptureMouse) {
      _camera->on_cursor_position(xpos, ypos);
    }
  }

  void scroll_callback(double xoffset, double yoffset) override {
    (void)xoffset;
    if (!ImGui::GetIO().WantCaptureMouse) {
      _camera->on_scroll(yoffset);
    }
  }

  void draw_ui() {
    Application::draw_ui();

    int id = 0;
    if (ImGui::CollapsingHeader("Camera")) {
      ImGui::PushID(id++);
      _camera->draw_ui();
      ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Tone Mapping")) {
      ImGui::PushID(id++);
      ImGui::SliderFloat(
          "Exposure", &_tone_mapping_material->exposure, 0.0f, 10.0f);
      ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Directional Light")) {
      ImGui::PushID(id++);
      ImGui::SliderAngle("Pitch", &_light_pitch, 0.0f, 180.0f);
      ImGui::SliderAngle("Yaw", &_light_yaw);
      ImGui::ColorEdit3("Color", (float *)&_light_color);
      ImGui::SliderFloat("Strength", &_light_strength, 0.0f, 10.0f);
      ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Environment Light")) {
      ImGui::PushID(id++);
      ImGui::ColorEdit3("Color", (float *)&_env_color);
      ImGui::SliderFloat("Strength", &_env_strength, 0.0f, 10.0f);
      ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Environment BRDF")) {
      ImGui::PushID(id++);
      if (ImGui::Button("Recalculate")) {
        calculate_env_brdf_lut();
      }
      ImGui::Text("LUT");
      ImGui::Image(static_cast<ImTextureID>(_env_brdf_lut->get()),
                   ImVec2(_lut_size, _lut_size));
      ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Water")) {
      ImGui::PushID(id++);
      ImGui::Text("Water");
      _water_material->draw_ui();
      ImGui::PopID();
    }
  }

  void calculate_env_brdf_lut() {
    _env_brdf_lut = std::make_unique<Texture2D>(
        nullptr, GL_FLOAT, _lut_size, _lut_size, GL_RGBA16F, GL_RGBA);

    Texture2D *color_attachments[] = {_env_brdf_lut.get()};
    auto env_brdf_lut_framebuffer = std::make_unique<Framebuffer>(
        color_attachments,
        static_cast<uint32_t>(std::size(color_attachments)),
        nullptr);

    glBindFramebuffer(GL_FRAMEBUFFER, env_brdf_lut_framebuffer->get());
    glViewport(0, 0, _lut_size, _lut_size);
    _renderer->blit(nullptr, _env_brdf_material.get());
    glBindTexture(GL_TEXTURE_2D, _env_brdf_lut->get());
    glGenerateMipmap(GL_TEXTURE_2D);
  }

  void draw_scene() {
    glm::vec3 env_radiance = _env_color * _env_strength;
    glClearColor(env_radiance.x, env_radiance.y, env_radiance.z, 1.0);
    glViewport(0, 0, _screen_fb_width, _screen_fb_height);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float aspect = (float)_screen_fb_width / (float)_screen_fb_height;

    glm::mat4 view = _camera->view();
    glm::mat4 projection = _camera->projection(aspect);

    auto draw_mode =
        [&](PbrMaterial::Mode mode,
            const std::vector<std::unique_ptr<PbrMaterial>> &materials) {
          for (auto &draw : _scene->draws) {
            for (auto &prim : _scene->meshes[draw.index]) {
              auto *mat = materials[prim.material].get();
              if (mat->mode != mode) {
                continue;
              }
              mat->model = draw.transform;
              mat->view = view;
              mat->projection = projection;

              glm::vec3 light_dir_ws =
                  polar_to_cartesian(_light_yaw, _light_pitch);
              glm::vec3 light_dir_vs = view * glm::vec4(light_dir_ws, 0.0f);

              mat->light_dir_vs = glm::normalize(light_dir_vs);
              mat->light_radiance = _light_color * _light_strength;
              mat->env_radiance = env_radiance;
              mat->lut = _env_brdf_lut.get();

              mat->use();
              prim.mesh->draw();
            }
          }
        };

    {
      MICROPROFILE_SCOPEGPUI("Opaque Render", 0x122277);
      MICROPROFILE_SCOPEI("Main", "Opaque Render", 0x122277);
      glDisable(GL_BLEND);
      glDepthMask(GL_TRUE);
      draw_mode(PbrMaterial::Opaque, _pbr_materials);
    }
    {
      MICROPROFILE_SCOPEGPUI("Scene Snapshot", 0x25FF22);
      MICROPROFILE_SCOPEI("Main", "Scene Snapshot", 0x25FF22);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, _framebuffer->get());
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                        _scene_snapshot_framebuffer->get());
      glReadBuffer(GL_COLOR_ATTACHMENT0);
      glDrawBuffer(GL_COLOR_ATTACHMENT0);
      glBlitFramebuffer(0,
                        0,
                        _screen_fb_width,
                        _screen_fb_height,
                        0,
                        0,
                        _screen_fb_width,
                        _screen_fb_height,
                        GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT,
                        GL_NEAREST);
      glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer->get());
    }

    if (_water_material->uses_hiz()) {
      MICROPROFILE_SCOPEGPUI("Hi-Z Build", 0x23AA23);
      MICROPROFILE_SCOPEI("Main", "Hi-Z Build", 0x23AA23);
      _depth_pyramid->build(_scene_depth_attachment.get());
    }

    {
      MICROPROFILE_SCOPEGPUI("Water", 0x22FF22);
      MICROPROFILE_SCOPEI("Main", "Water", 0x22FF22);
      glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer->get());
      glDisable(GL_CULL_FACE);
      glEnable(GL_DEPTH_TEST);
      glDepthFunc(GL_LEQUAL);
      glDepthMask(GL_TRUE);
      glDisable(GL_BLEND);
      _water_material->model = _water_geometry->transform();
      _water_material->view = view;
      _water_material->projection = projection;
      _water_material->set_scene_textures(_scene_color_attachment.get(),
                                          _scene_depth_attachment.get());
      _water_material->set_depth_pyramid(_depth_pyramid->texture(),
                                         _depth_pyramid->max_mip_level());
      glm::vec3 light_dir_ws = polar_to_cartesian(_light_yaw, _light_pitch);
      glm::vec3 light_dir_vs = view * glm::vec4(light_dir_ws, 0.0f);

      _water_material->light_dir_vs = glm::normalize(light_dir_vs);
      _water_material->light_radiance = _light_color * _light_strength;
      _water_material->env_radiance = env_radiance;
      _water_material->use();
      _water_geometry->draw();
      glEnable(GL_CULL_FACE);
      glCullFace(GL_BACK);
    }

    {
      MICROPROFILE_SCOPEGPUI("Transparent Tint", 0x17AAFF);
      MICROPROFILE_SCOPEI("Main", "Transparent Tint", 0x17AAFF);
      glEnable(GL_BLEND);
      glDepthMask(GL_FALSE);
      glBlendFunc(GL_ZERO, GL_SRC_COLOR);
      draw_mode(PbrMaterial::Blend, _base_color_materials);
    }

    {
      MICROPROFILE_SCOPEGPUI("Transparent Lit", 0xBB8122);
      MICROPROFILE_SCOPEI("Main", "Transparent Lit", 0xBB8122);
      glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      draw_mode(PbrMaterial::Blend, _pbr_materials);
      glDepthMask(GL_TRUE);
      glDisable(GL_BLEND);
    }
  }

  void draw() {
    // render scene to texture
    glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer->get());
    draw_scene();

    // blit texture to screen
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    _renderer->blit(_color_attachment.get(), _tone_mapping_material.get());
  }

  void update_frame_buffer() {
    glfwGetFramebufferSize(_window, &_screen_fb_width, &_screen_fb_height);
    if (_screen_fb_width <= 0 || _screen_fb_height <= 0) {
      return;
    }
    // make the size of offscreen buffer matches the screen's
    if (_color_attachment != nullptr &&
        _color_attachment->width() == _screen_fb_width &&
        _color_attachment->height() == _screen_fb_height) {
      return;
    }
    _framebuffer.reset();
    _scene_snapshot_framebuffer.reset();

    TextureSettings color_settings{};
    color_settings.wrap_s = GL_CLAMP_TO_EDGE;
    color_settings.wrap_t = GL_CLAMP_TO_EDGE;
    color_settings.min_filter = GL_LINEAR;
    color_settings.max_filter = GL_LINEAR;
    color_settings.generate_mipmaps = false;

    TextureSettings depth_settings{};
    depth_settings.wrap_s = GL_CLAMP_TO_EDGE;
    depth_settings.wrap_t = GL_CLAMP_TO_EDGE;
    depth_settings.min_filter = GL_NEAREST;
    depth_settings.max_filter = GL_NEAREST;
    depth_settings.generate_mipmaps = false;

    // Main HDR target. The water writes here after sampling the snapshot.
    _color_attachment = std::make_unique<Texture2D>(nullptr,
                                                    GL_FLOAT,
                                                    _screen_fb_width,
                                                    _screen_fb_height,
                                                    GL_RGBA16F,
                                                    GL_RGBA,
                                                    &color_settings);
    _depth_attachment = std::make_unique<Texture2D>(nullptr,
                                                    GL_FLOAT,
                                                    _screen_fb_width,
                                                    _screen_fb_height,
                                                    GL_DEPTH_COMPONENT32F,
                                                    GL_DEPTH_COMPONENT,
                                                    &depth_settings);
    Texture2D *color_attachments[] = {_color_attachment.get()};
    _framebuffer = std::make_unique<Framebuffer>(
        color_attachments,
        static_cast<uint32_t>(std::size(color_attachments)),
        _depth_attachment.get(),
        true);

    // Read-only pre-water snapshot used by refraction and both SSR paths.
    _scene_color_attachment = std::make_unique<Texture2D>(nullptr,
                                                          GL_FLOAT,
                                                          _screen_fb_width,
                                                          _screen_fb_height,
                                                          GL_RGBA16F,
                                                          GL_RGBA,
                                                          &color_settings);
    _scene_depth_attachment = std::make_unique<Texture2D>(nullptr,
                                                          GL_FLOAT,
                                                          _screen_fb_width,
                                                          _screen_fb_height,
                                                          GL_DEPTH_COMPONENT32F,
                                                          GL_DEPTH_COMPONENT,
                                                          &depth_settings);
    Texture2D *snapshot_color_attachments[] = {_scene_color_attachment.get()};
    _scene_snapshot_framebuffer = std::make_unique<Framebuffer>(
        snapshot_color_attachments,
        static_cast<uint32_t>(std::size(snapshot_color_attachments)),
        _scene_depth_attachment.get(),
        true);

    _depth_pyramid->resize(static_cast<uint32_t>(_screen_fb_width),
                           static_cast<uint32_t>(_screen_fb_height));

    _water_material->update_window_size(_screen_fb_width, _screen_fb_height);
  }

  void update() override {
    double now = glfwGetTime();
    float delta_time = static_cast<float>(now - _last_time);
    _last_time = now;
    if (delta_time > 0.1f) {
      delta_time = 0.1f;
    }
    _camera->update(delta_time);

    update_frame_buffer();
    if (_screen_fb_width <= 0 || _screen_fb_height <= 0) {
      return;
    }
    draw_ui();
    draw();
  }

  float _light_yaw = glm::radians(235.0f);
  float _light_pitch = glm::radians(60.0f);
  float _light_strength = 1.0f;
  glm::vec3 _light_color = glm::vec3(1.0, 1.0, 1.0);
  float _env_strength = 1.0f;
  glm::vec3 _env_color = glm::vec3(1.0, 1.0, 1.0);

  std::vector<std::unique_ptr<PbrMaterial>> _pbr_materials;
  std::vector<std::unique_ptr<PbrMaterial>> _base_color_materials;
  std::unique_ptr<ToneMappingMaterial> _tone_mapping_material{};

  int _screen_fb_width, _screen_fb_height;
  std::unique_ptr<Texture2D> _color_attachment{};
  std::unique_ptr<Texture2D> _depth_attachment{};
  std::unique_ptr<Framebuffer> _framebuffer{};

  std::unique_ptr<Texture2D> _scene_color_attachment{};
  std::unique_ptr<Texture2D> _scene_depth_attachment{};
  std::unique_ptr<Framebuffer> _scene_snapshot_framebuffer{};
  std::unique_ptr<DepthPyramid> _depth_pyramid{};

  // LUT (look up table) of pre-integrated BRDF
  std::unique_ptr<PrecomputeEnvBrdfMaterial> _env_brdf_material{};
  int _lut_size = 256;
  std::unique_ptr<Texture2D> _env_brdf_lut{};

  std::unique_ptr<Renderer> _renderer;
  std::unique_ptr<FPSCamera> _camera;
  std::unique_ptr<Gltf> _scene;

  std::unique_ptr<WaterMaterial> _water_material;
  std::unique_ptr<WaterGeometry> _water_geometry;
  double _last_time{0.0};
  bool _camera_mouse_active{false};
};

int main() {
  try {
    SSRApp app{};
    app.run();
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
  }
}
