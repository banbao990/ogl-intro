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
#include <glm/glm.hpp>
#include <imgui/imgui.h>
#include <iostream>
#include <sstream>
#include <tiny_gltf.h>
#include <vector>

#include "material.hpp"
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
    _camera = std::make_unique<ModelViewerCamera>();
    _scene = std::make_unique<Gltf>("FlightHelmet/FlightHelmet.gltf");
    _tone_mapping_material = std::make_unique<ToneMappingMaterial>();
    _renderer = std::make_unique<Renderer>();

    // water material
    _water_material = std::make_unique<WaterMaterial>();
    _water_geometry = std::make_unique<WaterGeometry>(1.0f, 1.0f, 100, 100);
    _depth_material = std::make_unique<DepthMaterial>();

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
      ImGui::Image(reinterpret_cast<ImTextureID>(
                       static_cast<uint64_t>(_env_brdf_lut->get())),
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
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float aspect = (float)_screen_fb_width / (float)_screen_fb_height;

    glm::mat4 view = _camera->view();
    glm::mat4 projection = _camera->projection(aspect);

    auto draw_mode =
        [&](PbrMaterial::Mode mode,
            const std::vector<std::unique_ptr<PbrMaterial>> &materials) {
          return; // TODO: remove this line
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
      MICROPROFILE_SCOPEGPUI("Transparent Tint", 0x17AAFF);
      MICROPROFILE_SCOPEI("Main", "Transparent Tint", 0x17AAFF);
      glEnable(GL_BLEND);
      // disable z-write for transparent objects
      glDepthMask(GL_FALSE); // can be read, but not written
      glBlendFunc(GL_ZERO, GL_SRC_COLOR);
      // tint objects covered by transparent ones
      draw_mode(PbrMaterial::Blend, _base_color_materials);
    }

    {
      MICROPROFILE_SCOPEGPUI("Transparent Lit", 0xBB8122);
      MICROPROFILE_SCOPEI("Main", "Transparent Lit", 0xBB8122);
      glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      draw_mode(PbrMaterial::Blend, _pbr_materials);
    }

    {
      MICROPROFILE_SCOPEGPUI("Depth", 0x25FF22);
      MICROPROFILE_SCOPEI("Main", "Depth", 0x25FF22);
      // copy depth to new framebuffer
      glBindFramebuffer(GL_READ_FRAMEBUFFER, _framebuffer->get());
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _framebuffer_2_depth->get());
      glBlitFramebuffer(0,
                        0,
                        _screen_fb_width,
                        _screen_fb_height,
                        0,
                        0,
                        _screen_fb_width,
                        _screen_fb_height,
                        GL_DEPTH_BUFFER_BIT,
                        GL_NEAREST);
    }

    {
      MICROPROFILE_SCOPEGPUI("Water Depth", 0x23FF23);
      MICROPROFILE_SCOPEI("Main", "Water Depth", 0x23FF23);
      glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer_2_depth->get());

      glDepthMask(GL_TRUE);
      glDrawBuffer(GL_NONE);
      _depth_material->model = _water_geometry->transform();
      _depth_material->view = view;
      _depth_material->projection = projection;
      _depth_material->use();
      _water_geometry->draw();
      glDrawBuffer(GL_COLOR_ATTACHMENT0);
    }

    {
      MICROPROFILE_SCOPEGPUI("Water", 0x22FF22);
      MICROPROFILE_SCOPEI("Main", "Water", 0x22FF22);
      glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer->get());
      glDisable(GL_CULL_FACE);
      glDepthMask(GL_FALSE);
      // glDisable(GL_DEPTH_TEST);
      glBlendFunc(GL_ONE, GL_ZERO);
      _water_material->model = _water_geometry->transform();
      _water_material->view = view;
      _water_material->projection = projection;
      _water_material->set_depth_tex(_depth_attachment_2_depth);
      glm::vec3 light_dir_ws = polar_to_cartesian(_light_yaw, _light_pitch);
      glm::vec3 light_dir_vs = view * glm::vec4(light_dir_ws, 0.0f);

      _water_material->light_dir_vs = glm::normalize(light_dir_vs);
      _water_material->use();
      _water_geometry->draw();
      glEnable(GL_CULL_FACE);
      glEnable(GL_DEPTH_TEST);
      glCullFace(GL_BACK);
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
    // make the size of offscreen buffer matches the screen's
    if (_color_attachment != nullptr &&
        _color_attachment->width() == _screen_fb_width &&
        _color_attachment->height() == _screen_fb_height) {
      return;
    }
    // use HDR
    _color_attachment = std::make_unique<Texture2D>(nullptr,
                                                    GL_FLOAT,
                                                    _screen_fb_width,
                                                    _screen_fb_height,
                                                    GL_RGBA16F,
                                                    GL_RGBA);
    _depth_stencil_attachment =
        std::make_unique<Texture2D>(nullptr,
                                    GL_UNSIGNED_INT_24_8,
                                    _screen_fb_width,
                                    _screen_fb_height,
                                    GL_DEPTH24_STENCIL8,
                                    GL_DEPTH_STENCIL);
    Texture2D *color_attachments[] = {_color_attachment.get()};
    _framebuffer = std::make_unique<Framebuffer>(
        color_attachments,
        static_cast<uint32_t>(std::size(color_attachments)),
        _depth_stencil_attachment.get());

    // depth
    // [Banbao] only depth_stencil_attachment is invalid
    _depth_attachment_2_depth = std::make_shared<Texture2D>(nullptr,
                                                            GL_FLOAT,
                                                            _screen_fb_width,
                                                            _screen_fb_height,
                                                            GL_DEPTH_COMPONENT,
                                                            GL_DEPTH_COMPONENT);
    _framebuffer_2_depth = std::make_unique<Framebuffer>(
        nullptr, 0, _depth_attachment_2_depth.get(), true);

    _water_material->update_window_size(_screen_fb_width, _screen_fb_height);
  }

  void update() override {
    update_frame_buffer();
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
  std::unique_ptr<Texture2D> _depth_stencil_attachment{};
  std::unique_ptr<Framebuffer> _framebuffer{};

  std::shared_ptr<Texture2D> _depth_attachment_2_depth{};
  std::unique_ptr<Framebuffer> _framebuffer_2_depth{};

  // LUT (look up table) of pre-integrated BRDF
  std::unique_ptr<PrecomputeEnvBrdfMaterial> _env_brdf_material{};
  int _lut_size = 256;
  std::unique_ptr<Texture2D> _env_brdf_lut{};

  std::unique_ptr<Renderer> _renderer;
  std::unique_ptr<ModelViewerCamera> _camera;
  std::unique_ptr<Gltf> _scene;

  std::unique_ptr<WaterMaterial> _water_material;
  std::unique_ptr<WaterGeometry> _water_geometry;
  std::unique_ptr<DepthMaterial> _depth_material;
};

int main() {
  try {
    SSRApp app{};
    app.run();
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
  }
}