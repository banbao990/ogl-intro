#include "../common/application.hpp"
#include "../common/framebuffer.hpp"
#include "../common/gltf.hpp"
#include "../common/profile.h"
#include "../common/renderer.hpp"
#include "../common/utils.hpp"
#include "material.hpp"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "../common/camera/fps_camera.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui/imgui.h>
#include <iostream>
#include <vector>

namespace {
constexpr GLuint kMirrorBit = 0x01;

std::unique_ptr<Mesh> create_mirror_quad() {
  const Mesh::Vertex vertices[] = {
      {{-0.5f, -0.5f, 0.0f},
       {0.0f, 0.0f, 1.0f},
       {1.0f, 0.0f, 0.0f, 1.0f},
       {0.0f, 0.0f},
       {0.0f, 0.0f},
       {1.0f, 1.0f, 1.0f, 1.0f}},
      {{0.5f, -0.5f, 0.0f},
       {0.0f, 0.0f, 1.0f},
       {1.0f, 0.0f, 0.0f, 1.0f},
       {1.0f, 0.0f},
       {0.0f, 0.0f},
       {1.0f, 1.0f, 1.0f, 1.0f}},
      {{0.5f, 0.5f, 0.0f},
       {0.0f, 0.0f, 1.0f},
       {1.0f, 0.0f, 0.0f, 1.0f},
       {1.0f, 1.0f},
       {0.0f, 0.0f},
       {1.0f, 1.0f, 1.0f, 1.0f}},
      {{-0.5f, 0.5f, 0.0f},
       {0.0f, 0.0f, 1.0f},
       {1.0f, 0.0f, 0.0f, 1.0f},
       {0.0f, 1.0f},
       {0.0f, 0.0f},
       {1.0f, 1.0f, 1.0f, 1.0f}},
  };
  const uint32_t indices[] = {0, 1, 2, 0, 2, 3};
  return std::make_unique<Mesh>(vertices,
                                static_cast<uint32_t>(std::size(vertices)),
                                indices,
                                static_cast<uint32_t>(std::size(indices)));
}

glm::mat4 reflection_matrix(const glm::vec4 &plane) {
  const glm::vec3 n = glm::normalize(glm::vec3(plane));
  const float d = plane.w;

  glm::mat4 result(1.0f);
  result[0][0] = 1.0f - 2.0f * n.x * n.x;
  result[0][1] = -2.0f * n.x * n.y;
  result[0][2] = -2.0f * n.x * n.z;
  result[1][0] = -2.0f * n.y * n.x;
  result[1][1] = 1.0f - 2.0f * n.y * n.y;
  result[1][2] = -2.0f * n.y * n.z;
  result[2][0] = -2.0f * n.z * n.x;
  result[2][1] = -2.0f * n.z * n.y;
  result[2][2] = 1.0f - 2.0f * n.z * n.z;
  result[3] = glm::vec4(-2.0f * d * n, 1.0f);
  return result;
}
} // namespace

class MirrorApp final : public Application {
public:
  MirrorApp()
      : Application("Stencil Mirror", 800, 600), _screen_fb_width(800),
        _screen_fb_height(600) {}

private:
  struct ScissorRect {
    bool visible = true;
    bool enabled = false;
    GLint x = 0;
    GLint y = 0;
    GLsizei width = 0;
    GLsizei height = 0;
  };

  void init() override {
    _camera = std::make_unique<FPSCamera>(
        glm::vec3(0.23f, 1.75f, -2.99f), 94.3f, -26.6f);
    _camera->set_window(_window);
    _scene = std::make_unique<Gltf>("FlightHelmet/FlightHelmet.gltf");
    _tone_mapping_material = std::make_unique<ToneMappingMaterial>();
    _mirror_material = std::make_unique<MirrorMaterial>();
    _mirror_mesh = create_mirror_quad();
    _renderer = std::make_unique<Renderer>();

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
      ImGui::TextUnformatted("WASD: move, Q/E: down/up");
      ImGui::TextUnformatted("Drag LMB: look, Shift+LMB: pan, wheel: FOV");
      ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Mirror", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::PushID(id++);
      ImGui::Checkbox("Enabled", &_mirror_enabled);
      ImGui::DragFloat3("Position", &_mirror_position.x, 0.01f);
      ImGui::SliderAngle("Yaw", &_mirror_yaw);
      ImGui::SliderAngle("Pitch", &_mirror_pitch, -89.0f, 89.0f);
      ImGui::DragFloat2("Size", &_mirror_size.x, 0.01f, 0.05f, 10.0f);
      ImGui::ColorEdit3("Tint", &_mirror_material->tint.x);
      ImGui::SliderFloat(
          "Reflectivity", &_mirror_material->reflectivity, 0.0f, 1.0f);
      ImGui::SliderFloat(
          "Tint Strength", &_mirror_material->tint_strength, 0.0f, 1.0f);
      ImGui::SliderFloat("Clip Bias", &_mirror_clip_bias, 0.0f, 0.05f, "%.4f");
      ImGui::Checkbox("Use Scissor Test", &_use_scissor);
      ImGui::TextUnformatted(
          "Stencil clips the reflected PBR pass to the quad.");
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
      ImGui::ColorEdit3("Color", &_light_color.x);
      ImGui::SliderFloat("Strength", &_light_strength, 0.0f, 10.0f);
      ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Environment Light")) {
      ImGui::PushID(id++);
      ImGui::ColorEdit3("Color", &_env_color.x);
      ImGui::SliderFloat("Strength", &_env_strength, 0.0f, 10.0f);
      ImGui::PopID();
    }
  }

  void calculate_env_brdf_lut() {
    _env_brdf_lut = std::make_unique<Texture2D>(
        nullptr, GL_FLOAT, _lut_size, _lut_size, GL_RGBA16F, GL_RGBA);

    Texture2D *color_attachments[] = {_env_brdf_lut.get()};
    auto framebuffer = std::make_unique<Framebuffer>(
        color_attachments,
        static_cast<uint32_t>(std::size(color_attachments)),
        nullptr);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer->get());
    glViewport(0, 0, _lut_size, _lut_size);
    _renderer->blit(nullptr, _env_brdf_material.get());
    glBindTexture(GL_TEXTURE_2D, _env_brdf_lut->get());
    glGenerateMipmap(GL_TEXTURE_2D);
  }

  glm::mat4 mirror_model() const {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), _mirror_position);
    model = glm::rotate(model, _mirror_yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, _mirror_pitch, glm::vec3(1.0f, 0.0f, 0.0f));
    return glm::scale(model, glm::vec3(_mirror_size.x, _mirror_size.y, 1.0f));
  }

  glm::vec4 mirror_plane(const glm::mat4 &model) const {
    const glm::vec3 point = glm::vec3(model * glm::vec4(0, 0, 0, 1));
    glm::vec3 normal = glm::normalize(
        glm::transpose(glm::inverse(glm::mat3(model))) * glm::vec3(0, 0, 1));
    if (glm::dot(normal, _camera->position() - point) < 0.0f) {
      normal = -normal;
    }
    return glm::vec4(normal, -glm::dot(normal, point));
  }

  ScissorRect calculate_scissor(const glm::mat4 &mvp) const {
    ScissorRect result{};
    if (!_use_scissor) {
      return result;
    }

    const std::array<glm::vec4, 4> corners = {
        glm::vec4(-0.5f, -0.5f, 0.0f, 1.0f),
        glm::vec4(0.5f, -0.5f, 0.0f, 1.0f),
        glm::vec4(0.5f, 0.5f, 0.0f, 1.0f),
        glm::vec4(-0.5f, 0.5f, 0.0f, 1.0f),
    };

    glm::vec2 min_ndc(1.0f);
    glm::vec2 max_ndc(-1.0f);
    for (const auto &corner : corners) {
      const glm::vec4 clip = mvp * corner;
      if (clip.w <= 0.0001f) {
        // The quad crosses the eye plane; fall back to stencil-only clipping.
        return result;
      }
      const glm::vec2 ndc = glm::vec2(clip) / clip.w;
      min_ndc = glm::min(min_ndc, ndc);
      max_ndc = glm::max(max_ndc, ndc);
    }

    min_ndc = glm::clamp(min_ndc, glm::vec2(-1.0f), glm::vec2(1.0f));
    max_ndc = glm::clamp(max_ndc, glm::vec2(-1.0f), glm::vec2(1.0f));
    if (min_ndc.x >= max_ndc.x || min_ndc.y >= max_ndc.y) {
      result.visible = false;
      return result;
    }

    const float min_x = (min_ndc.x * 0.5f + 0.5f) * _screen_fb_width;
    const float min_y = (min_ndc.y * 0.5f + 0.5f) * _screen_fb_height;
    const float max_x = (max_ndc.x * 0.5f + 0.5f) * _screen_fb_width;
    const float max_y = (max_ndc.y * 0.5f + 0.5f) * _screen_fb_height;

    result.x = static_cast<GLint>(std::floor(min_x));
    result.y = static_cast<GLint>(std::floor(min_y));
    result.width = static_cast<GLsizei>(std::ceil(max_x) - result.x);
    result.height = static_cast<GLsizei>(std::ceil(max_y) - result.y);
    result.enabled = result.width > 0 && result.height > 0;
    return result;
  }

  void set_scissor(const ScissorRect &rect, bool enable) const {
    if (enable && rect.enabled) {
      glEnable(GL_SCISSOR_TEST);
      glScissor(rect.x, rect.y, rect.width, rect.height);
    } else {
      glDisable(GL_SCISSOR_TEST);
    }
  }

  void draw_mirror(const glm::mat4 &model,
                   const glm::mat4 &view,
                   const glm::mat4 &projection) {
    _mirror_material->model = model;
    _mirror_material->view = view;
    _mirror_material->projection = projection;
    _mirror_material->use();
    _mirror_mesh->draw();
  }

  void draw_pbr_scene(const glm::mat4 &view,
                      const glm::mat4 &projection,
                      bool clip_enabled,
                      const glm::vec4 &clip_plane) {
    const glm::vec3 env_radiance = _env_color * _env_strength;
    const glm::vec3 light_dir_ws = polar_to_cartesian(_light_yaw, _light_pitch);
    const glm::vec3 light_dir_vs = view * glm::vec4(light_dir_ws, 0.0f);

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
              mat->clip_enabled = clip_enabled;
              mat->clip_plane_ws = clip_plane;
              mat->light_dir_vs = glm::normalize(light_dir_vs);
              mat->light_radiance = _light_color * _light_strength;
              mat->env_radiance = env_radiance;
              mat->lut = _env_brdf_lut.get();
              mat->use();
              prim.mesh->draw();
            }
          }
        };

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    draw_mode(PbrMaterial::Opaque, _pbr_materials);

    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glBlendFunc(GL_ZERO, GL_SRC_COLOR);
    draw_mode(PbrMaterial::Blend, _base_color_materials);

    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    draw_mode(PbrMaterial::Blend, _pbr_materials);
  }

  void draw_scene() {
    const glm::vec3 env_radiance = _env_color * _env_strength;
    glClearColor(env_radiance.x, env_radiance.y, env_radiance.z, 1.0f);
    glViewport(0, 0, _screen_fb_width, _screen_fb_height);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    const float aspect =
        static_cast<float>(_screen_fb_width) / _screen_fb_height;
    const glm::mat4 view = _camera->view();
    const glm::mat4 projection = _camera->projection(aspect);
    const glm::mat4 mirror_model_matrix = mirror_model();
    const ScissorRect scissor =
        calculate_scissor(projection * view * mirror_model_matrix);
    const bool draw_mirror_effect = _mirror_enabled && scissor.visible;

    if (draw_mirror_effect) {
      MICROPROFILE_SCOPEGPUI("Mirror Reflection", 0x44AAEE);
      MICROPROFILE_SCOPEI("Main", "Mirror Reflection", 0x44AAEE);

      set_scissor(scissor, true);

      // Mark the mirror quad in bit 0 without touching color or depth.
      glDisable(GL_BLEND);
      glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
      glDepthMask(GL_FALSE);
      glStencilMask(kMirrorBit);
      glStencilFunc(GL_ALWAYS, kMirrorBit, kMirrorBit);
      glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
      draw_mirror(mirror_model_matrix, view, projection);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

      // Render the reflected scene only where the mirror bit was marked.
      glStencilMask(0x00);
      glStencilFunc(GL_EQUAL, kMirrorBit, kMirrorBit);
      glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
      glDepthMask(GL_TRUE);

      const glm::vec4 plane = mirror_plane(mirror_model_matrix);
      const glm::mat4 reflected_view = view * reflection_matrix(plane);
      glm::vec4 clip_plane = plane;
      clip_plane.w += _mirror_clip_bias;

      glEnable(GL_CLIP_DISTANCE0);
      glFrontFace(GL_CW);
      draw_pbr_scene(reflected_view, projection, true, clip_plane);
      glFrontFace(GL_CCW);
      glDisable(GL_CLIP_DISTANCE0);
      set_scissor(scissor, false);

      // Reflection depth is not comparable with the real camera depth.
      glDepthMask(GL_TRUE);
      glClear(GL_DEPTH_BUFFER_BIT);

      // Recreate the mirror plane in the real camera's depth buffer.
      set_scissor(scissor, true);
      glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
      glDisable(GL_BLEND);
      glStencilMask(0x00);
      glStencilFunc(GL_EQUAL, kMirrorBit, kMirrorBit);
      glDepthMask(GL_TRUE);
      draw_mirror(mirror_model_matrix, view, projection);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      set_scissor(scissor, false);
    }

    // The real scene uses the mirror plane depth but is not stencil-clipped.
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    draw_pbr_scene(view, projection, false, glm::vec4(0.0f));

    if (draw_mirror_effect) {
      // Tint the reflection. Real objects in front of the plane win by depth.
      set_scissor(scissor, true);
      glEnable(GL_STENCIL_TEST);
      glStencilMask(0x00);
      glStencilFunc(GL_EQUAL, kMirrorBit, kMirrorBit);
      glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
      glEnable(GL_DEPTH_TEST);
      glDepthMask(GL_FALSE);
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      draw_mirror(mirror_model_matrix, view, projection);
      set_scissor(scissor, false);
    }

    glFrontFace(GL_CCW);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilMask(0xFF);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
  }

  void draw() {
    glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer->get());
    draw_scene();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    _renderer->blit(_color_attachment.get(), _tone_mapping_material.get());
  }

  void update_frame_buffer() {
    glfwGetFramebufferSize(_window, &_screen_fb_width, &_screen_fb_height);
    if (_color_attachment != nullptr &&
        _color_attachment->width() == _screen_fb_width &&
        _color_attachment->height() == _screen_fb_height) {
      return;
    }

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
  }

  void update() override {
    const double now = glfwGetTime();
    float delta_time = static_cast<float>(now - _last_time);
    _last_time = now;
    delta_time = std::min(delta_time, 0.1f);
    _camera->update(delta_time);

    update_frame_buffer();
    draw_ui();
    draw();
  }

  float _light_yaw = glm::radians(60.0f);
  float _light_pitch = glm::radians(60.0f);
  float _light_strength = 1.0f;
  glm::vec3 _light_color{1.0f};
  float _env_strength = 1.0f;
  glm::vec3 _env_color{1.0f};

  bool _mirror_enabled = true;
  bool _use_scissor = true;
  glm::vec3 _mirror_position{0.0f, 0.45f, 1.2f};
  glm::vec2 _mirror_size{2.4f, 1.8f};
  float _mirror_yaw = glm::radians(180.0f);
  float _mirror_pitch = 0.0f;
  float _mirror_clip_bias = 0.002f;

  std::vector<std::unique_ptr<PbrMaterial>> _pbr_materials;
  std::vector<std::unique_ptr<PbrMaterial>> _base_color_materials;
  std::unique_ptr<MirrorMaterial> _mirror_material;
  std::unique_ptr<ToneMappingMaterial> _tone_mapping_material;

  int _screen_fb_width;
  int _screen_fb_height;
  std::unique_ptr<Texture2D> _color_attachment;
  std::unique_ptr<Texture2D> _depth_stencil_attachment;
  std::unique_ptr<Framebuffer> _framebuffer;

  std::unique_ptr<PrecomputeEnvBrdfMaterial> _env_brdf_material;
  int _lut_size = 256;
  std::unique_ptr<Texture2D> _env_brdf_lut;

  std::unique_ptr<Mesh> _mirror_mesh;
  std::unique_ptr<Renderer> _renderer;
  std::unique_ptr<FPSCamera> _camera;
  std::unique_ptr<Gltf> _scene;
  double _last_time = 0.0;
  bool _camera_mouse_active = false;
};

int main() {
  try {
    MirrorApp app{};
    app.run();
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
  }
}
