#include "../common/application.hpp"
#include "../common/shader.hpp"
#include "../common/mesh.hpp"
#include "../common/data.hpp"
#include "../common/camera/fps_camera.hpp"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui/imgui.h>
#include <stb_image.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <vector>

static std::unique_ptr<Mesh> create_ground_plane() {
  float s = 6.0f;
  Mesh::Vertex v[] = {
      {{-s, 0, -s}, {0, 1, 0}, {1, 0, 0, 1}, {0, 0}, {0, 0}, {1, 1, 1, 1}},
      {{s, 0, -s}, {0, 1, 0}, {1, 0, 0, 1}, {1, 0}, {0, 0}, {1, 1, 1, 1}},
      {{s, 0, s}, {0, 1, 0}, {1, 0, 0, 1}, {1, 1}, {0, 0}, {1, 1, 1, 1}},
      {{-s, 0, s}, {0, 1, 0}, {1, 0, 0, 1}, {0, 1}, {0, 0}, {1, 1, 1, 1}},
  };
  uint32_t idx[] = {0, 1, 2, 0, 2, 3};
  return std::make_unique<Mesh>(v, 4, idx, 6);
}

static std::unique_ptr<Mesh> create_box() {
  float h = 0.5f;
  struct Face {
    glm::vec3 n;
    glm::vec3 p[4];
  } faces[] = {
      {{0, 1, 0},
       {{-h, 2 * h, -h}, {h, 2 * h, -h}, {h, 2 * h, h}, {-h, 2 * h, h}}},
      {{0, -1, 0}, {{-h, 0, h}, {h, 0, h}, {h, 0, -h}, {-h, 0, -h}}},
      {{0, 0, 1}, {{-h, 0, h}, {h, 0, h}, {h, 2 * h, h}, {-h, 2 * h, h}}},
      {{0, 0, -1}, {{h, 0, -h}, {-h, 0, -h}, {-h, 2 * h, -h}, {h, 2 * h, -h}}},
      {{-1, 0, 0}, {{-h, 0, -h}, {-h, 0, h}, {-h, 2 * h, h}, {-h, 2 * h, -h}}},
      {{1, 0, 0}, {{h, 0, h}, {h, 0, -h}, {h, 2 * h, -h}, {h, 2 * h, h}}},
  };
  Mesh::Vertex verts[24];
  uint32_t idx[36];
  for (int f = 0; f < 6; f++) {
    for (int i = 0; i < 4; i++) {
      verts[f * 4 + i] = {faces[f].p[i],
                          faces[f].n,
                          {1, 0, 0, 1},
                          {0, 0},
                          {0, 0},
                          {0.6, 0.3, 0.15, 1}};
    }
    idx[f * 6 + 0] = f * 4 + 0;
    idx[f * 6 + 1] = f * 4 + 1;
    idx[f * 6 + 2] = f * 4 + 2;
    idx[f * 6 + 3] = f * 4 + 0;
    idx[f * 6 + 4] = f * 4 + 2;
    idx[f * 6 + 5] = f * 4 + 3;
  }
  return std::make_unique<Mesh>(verts, 24, idx, 36);
}

class BlueNoiseShadowApp final : public Application {
public:
  BlueNoiseShadowApp()
      : Application("Blue Noise Shadow - Bilinear Interpolation", 1280, 720) {}

private:
  struct Object {
    glm::vec3 position;
    glm::vec3 scale;
    glm::vec3 color;
  };

  void init() override {
    _shadow_prog =
        Program::create_from_files("shaders/11-blue-noise-sm/shadow_map.vert",
                                   "shaders/11-blue-noise-sm/shadow_map.frag");
    _scene_prog =
        Program::create_from_files("shaders/11-blue-noise-sm/scene.vert",
                                   "shaders/11-blue-noise-sm/scene.frag");

    _ground = create_ground_plane();
    _box = create_box();

    _objects = {
        {{0.0f, 0.01f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.6f, 0.35f, 0.15f}},
        {{-2.0f, 0.01f, -1.0f}, {0.7f, 0.7f, 0.7f}, {0.2f, 0.4f, 0.7f}},
        {{2.0f, 0.01f, 0.5f}, {0.5f, 1.3f, 0.5f}, {0.7f, 0.2f, 0.2f}},
        {{-0.5f, 0.01f, 2.0f}, {0.8f, 0.6f, 0.8f}, {0.3f, 0.6f, 0.3f}},
    };

    create_shadow_map();
    load_blue_noise();

    _camera = std::make_unique<FPSCamera>(
        glm::vec3(3.0f, 4.0f, 6.0f), -45.0f, -30.0f);
    _camera->set_window(_window);

    _light_pos = glm::vec3(-2.0f, 8.0f, -1.0f);

    get_uniforms();
    _last_time = glfwGetTime();
  }

  void key_callback(int key, int scancode, int action, int mods) override {
    _camera->on_key(key, action);
  }
  void mouse_button_callback(int button, int action, int mods) override {
    _camera->on_mouse_button(button, action, mods);
  }
  void cursor_position_callback(double xpos, double ypos) override {
    _camera->on_cursor_position(xpos, ypos);
  }
  void scroll_callback(double xoffset, double yoffset) override {
    _camera->on_scroll(yoffset);
  }

  void create_shadow_map() {
    glGenTextures(1, &_shadow_map);
    glBindTexture(GL_TEXTURE_2D, _shadow_map);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_DEPTH_COMPONENT32F,
                 _shadow_map_res,
                 _shadow_map_res,
                 0,
                 GL_DEPTH_COMPONENT,
                 GL_FLOAT,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

    glGenFramebuffers(1, &_shadow_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, _shadow_fbo);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, _shadow_map, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void load_blue_noise() {
    auto path = Data::resolve(
        "external/FreeBlueNoiseTextures/Data/64_64/LDR_RGBA_0.png");
    stbi_set_flip_vertically_on_load(false);
    int w, h, ch;
    unsigned char *data = stbi_load(path.string().c_str(), &w, &h, &ch, 0);
    if (!data) {
      throw std::runtime_error("failed to load blue noise: " + path.string());
    }
    glGenTextures(1, &_blue_noise_tex);
    glBindTexture(GL_TEXTURE_2D, _blue_noise_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);
    _noise_w = w;
    _noise_h = h;
    stbi_image_free(data);
  }

  void get_uniforms() {
    {
      auto p = _shadow_prog->get();
      _u_light_space_matrix_sh = glGetUniformLocation(p, "lightSpaceMatrix");
      _u_model_sh = glGetUniformLocation(p, "model");
    }
    {
      auto p = _scene_prog->get();
      _u_projection = glGetUniformLocation(p, "u_projection");
      _u_view = glGetUniformLocation(p, "u_view");
      _u_model_s = glGetUniformLocation(p, "u_model");
      _u_light_space_matrix_s = glGetUniformLocation(p, "lightSpaceMatrix");
      _u_light_pos = glGetUniformLocation(p, "u_light_pos");
      _u_view_pos = glGetUniformLocation(p, "u_view_pos");
      _u_base_color = glGetUniformLocation(p, "u_base_color");
      _u_mode = glGetUniformLocation(p, "u_mode");
      _u_bias = glGetUniformLocation(p, "u_bias");
      _u_noise_size = glGetUniformLocation(p, "u_noise_size");
      _u_offset = glGetUniformLocation(p, "u_offset");
      _u_split = glGetUniformLocation(p, "u_split");
      _u_split_x = glGetUniformLocation(p, "u_split_x");
    }
  }

  glm::mat4 compute_light_space_matrix() {
    float near_plane = 1.0f, far_plane = 20.0f;
    glm::mat4 lightProjection =
        glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, near_plane, far_plane);
    glm::mat4 lightView =
        glm::lookAt(_light_pos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    return lightProjection * lightView;
  }

  void draw_scene(GLuint program, GLint model_loc) {
    auto identity = glm::mat4(1.0f);
    glUniformMatrix4fv(model_loc, 1, GL_FALSE, &identity[0][0]);
    _ground->draw();

    for (auto &obj : _objects) {
      glm::mat4 m = glm::translate(glm::mat4(1.0f), obj.position) *
                    glm::scale(glm::mat4(1.0f), obj.scale);
      glUniformMatrix4fv(model_loc, 1, GL_FALSE, &m[0][0]);
      _box->draw();
    }
  }

  void render_shadow_pass(const glm::mat4 &lightSpaceMatrix) {
    glViewport(0, 0, _shadow_map_res, _shadow_map_res);
    glBindFramebuffer(GL_FRAMEBUFFER, _shadow_fbo);
    glEnable(GL_DEPTH_TEST);
    glCullFace(GL_FRONT);
    glClear(GL_DEPTH_BUFFER_BIT);

    glUseProgram(_shadow_prog->get());
    glUniformMatrix4fv(
        _u_light_space_matrix_sh, 1, GL_FALSE, &lightSpaceMatrix[0][0]);
    draw_scene(_shadow_prog->get(), _u_model_sh);

    glCullFace(GL_BACK);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void render_scene_pass(const glm::mat4 &lightSpaceMatrix) {
    int w, h;
    glfwGetFramebufferSize(_window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.45f, 0.6f, 0.85f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    glm::mat4 view = _camera->view();
    glm::mat4 proj = _camera->projection((float)w / (float)h);

    glUseProgram(_scene_prog->get());
    glUniformMatrix4fv(_u_projection, 1, GL_FALSE, &proj[0][0]);
    glUniformMatrix4fv(_u_view, 1, GL_FALSE, &view[0][0]);
    glUniformMatrix4fv(
        _u_light_space_matrix_s, 1, GL_FALSE, &lightSpaceMatrix[0][0]);
    glUniform3fv(_u_light_pos, 1, &_light_pos[0]);
    glUniform3fv(_u_view_pos, 1, &_camera->position()[0]);
    glUniform1i(_u_mode, _mode);
    glUniform1f(_u_bias, _bias);
    glUniform2f(_u_noise_size, (float)_noise_w, (float)_noise_h);
    glUniform2f(_u_offset, _offset_x, _offset_y);
    glUniform1i(_u_split, _split_view);
    glUniform1f(_u_split_x, (float)w * 0.5f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _shadow_map);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, _blue_noise_tex);

    auto identity = glm::mat4(1.0f);
    glUniformMatrix4fv(_u_model_s, 1, GL_FALSE, &identity[0][0]);
    glUniform3f(_u_base_color, 0.9f, 0.9f, 0.9f);
    _ground->draw();

    for (auto &obj : _objects) {
      glm::mat4 m = glm::translate(glm::mat4(1.0f), obj.position) *
                    glm::scale(glm::mat4(1.0f), obj.scale);
      glUniformMatrix4fv(_u_model_s, 1, GL_FALSE, &m[0][0]);
      glUniform3fv(_u_base_color, 1, &obj.color[0]);
      _box->draw();
    }
  }

  void draw_ui() {
    Application::draw_ui();
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 350), ImGuiCond_FirstUseEver);
    ImGui::Begin("Blue Noise Shadow");

    const char *modes[] = {
        "Nearest (No interpolation, blocky)",
        "Blue Noise Bilinear (1-tap stochastic)",
        "4-tap PCF (Bilinear reference)",
        "White Noise Bilinear (1-tap stochastic)",
    };
    ImGui::Combo("Shadow Mode", &_mode, modes, 4);

    const char *split_modes[] = {
        "Off",
        "Left=selected | Right=PCF ref",
        "Left=Blue Noise | Right=White Noise",
    };
    ImGui::Combo("Split View", &_split_view, split_modes, 3);

    ImGui::Separator();
    ImGui::Text("Shadow Map");
    int prev_res = _shadow_map_res;
    ImGui::SliderInt("Resolution", &_shadow_map_res, 32, 2048);
    ImGui::SliderFloat("Bias", &_bias, 0.0f, 0.02f, "%.5f");
    if (_shadow_map_res != prev_res) {
      glDeleteTextures(1, &_shadow_map);
      glDeleteFramebuffers(1, &_shadow_fbo);
      create_shadow_map();
    }

    ImGui::Separator();
    ImGui::Text("Light Position");
    ImGui::SliderFloat3("Pos", &_light_pos[0], -10.0f, 10.0f);

    ImGui::Separator();
    ImGui::Text("Blue Noise Animation");
    ImGui::Checkbox("Animate", &_animate);
    if (_animate) {
      ImGui::SliderFloat("Speed", &_anim_speed, 0.1f, 5.0f, "%.1f");
    }

    ImGui::Separator();
    ImGui::TextWrapped(
        "Blue Noise Bilinear: stochastically selects 1 of 4 shadow map "
        "texels using bilinear weights as probabilities.");

    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(10, 370), ImGuiCond_FirstUseEver);
    ImGui::Begin("Camera");
    _camera->draw_ui();
    ImGui::End();
  }

  void update() override {
    double now = glfwGetTime();
    float dt = (float)(now - _last_time);
    _last_time = now;

    _camera->update(dt);

    draw_ui();

    if (_animate) {
      _offset_x += _anim_speed * 0.5f;
      _offset_y += _anim_speed * 0.3f;
      if (_offset_x > _noise_w)
        _offset_x -= _noise_w;
      if (_offset_y > _noise_h)
        _offset_y -= _noise_h;
    }

    glm::mat4 lightSpaceMatrix = compute_light_space_matrix();
    render_shadow_pass(lightSpaceMatrix);
    render_scene_pass(lightSpaceMatrix);
  }

  std::unique_ptr<Program> _shadow_prog;
  std::unique_ptr<Program> _scene_prog;
  std::unique_ptr<Mesh> _ground;
  std::unique_ptr<Mesh> _box;
  std::unique_ptr<FPSCamera> _camera;
  std::vector<Object> _objects;

  GLuint _shadow_map{};
  GLuint _shadow_fbo{};
  GLuint _blue_noise_tex{};

  int _shadow_map_res{1024};
  int _noise_w{0}, _noise_h{0};
  glm::vec3 _light_pos;
  double _last_time{0.0};

  int _mode{1};
  float _bias{0.005f};
  int _split_view{2};
  bool _animate{true};
  float _anim_speed{1.0f};
  float _offset_x{0.0f};
  float _offset_y{0.0f};

  GLint _u_light_space_matrix_sh{-1}, _u_model_sh{-1};
  GLint _u_projection{-1}, _u_view{-1}, _u_model_s{-1};
  GLint _u_light_space_matrix_s{-1};
  GLint _u_light_pos{-1}, _u_view_pos{-1}, _u_base_color{-1};
  GLint _u_mode{-1}, _u_bias{-1};
  GLint _u_noise_size{-1}, _u_offset{-1};
  GLint _u_split{-1}, _u_split_x{-1};
};

int main() {
  try {
    BlueNoiseShadowApp app{};
    app.run();
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
  }
}
