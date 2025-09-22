#include "../common/application.hpp"
#include "../common/gltf.hpp"
#include "../common/shader.hpp"
#include "../common/framebuffer.hpp"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui/imgui.h>
#include <iostream>
#include <tiny_gltf.h>
#include <vector>

class VRSTexture {
public:
  VRSTexture(int width, int height) : _width(width), _height(height) {
    glGenTextures(1, &_tex);
    glBindTexture(GL_TEXTURE_2D, _tex);
    // generate storage
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_R8UI, width, height);
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  ~VRSTexture() {
    glDeleteTextures(1, &_tex);
  }

  void update(const std::vector<GLubyte> &data) {
    if ((int)data.size() != _width * _height) {
      throw std::runtime_error("data size mismatch");
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, _tex);
    glTexSubImage2D(GL_TEXTURE_2D,
                    0,
                    0,
                    0,
                    _width,
                    _height,
                    GL_RED_INTEGER,
                    GL_UNSIGNED_BYTE,
                    data.data());
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  GLuint tex() const {
    return _tex;
  }

private:
  GLuint _tex;
  int _width, _height;
};

class VRSApp final : public Application {
public:
  VRSApp() : Application("Variable Rate Shading", 1024, 1024) {} // fixed size
  ~VRSApp() {
    // delete _vrs_types
    for (int i = 0; i < _vrs_types_num; i++) {
      delete[] _vrs_types[i];
    }
    delete[] _vrs_types;
  }

private:
  void check_ext() {
    const char *exts[] = {
        "GL_NV_shading_rate_image",
        // "GL_NV_primitive_shading_rate",
    };
    for (auto ext : exts) {
      if (!glewIsSupported(ext)) {
        throw std::runtime_error(std::string("need extension: ") + ext);
      } else {
        std::cout << "found extension: " << ext << std::endl;
      }
    }
  }

  void set_vrs_palette() {
    // support how many rates in palette
    GLint pal_size;
    glGetIntegerv(GL_SHADING_RATE_IMAGE_PALETTE_SIZE_NV, &pal_size);
    std::cout << "GL_SHADING_RATE_IMAGE_PALETTE_SIZE_NV = " << pal_size
              << std::endl;

    // setup palette
    std::vector<GLenum> rates = {
        GL_SHADING_RATE_NO_INVOCATIONS_NV,
        GL_SHADING_RATE_1_INVOCATION_PER_PIXEL_NV,
        GL_SHADING_RATE_1_INVOCATION_PER_1X2_PIXELS_NV,
        GL_SHADING_RATE_1_INVOCATION_PER_2X1_PIXELS_NV,
        GL_SHADING_RATE_1_INVOCATION_PER_2X2_PIXELS_NV,
        GL_SHADING_RATE_1_INVOCATION_PER_2X4_PIXELS_NV,
        GL_SHADING_RATE_1_INVOCATION_PER_4X2_PIXELS_NV,
        GL_SHADING_RATE_1_INVOCATION_PER_4X4_PIXELS_NV,
        GL_SHADING_RATE_2_INVOCATIONS_PER_PIXEL_NV,
        GL_SHADING_RATE_4_INVOCATIONS_PER_PIXEL_NV,
        GL_SHADING_RATE_8_INVOCATIONS_PER_PIXEL_NV,
        GL_SHADING_RATE_16_INVOCATIONS_PER_PIXEL_NV,
    };
    std::vector<std::string> vrs_types = {
        "NO_INVOCATIONS",
        "1_INVOCATION_PER_PIXEL",
        "1_INVOCATION_PER_1X2_PIXELS",
        "1_INVOCATION_PER_2X1_PIXELS",
        "1_INVOCATION_PER_2X2_PIXELS",
        "1_INVOCATION_PER_2X4_PIXELS",
        "1_INVOCATION_PER_4X2_PIXELS",
        "1_INVOCATION_PER_4X4_PIXELS",
        "2_INVOCATIONS_PER_PIXEL",
        "4_INVOCATIONS_PER_PIXEL",
        "8_INVOCATIONS_PER_PIXEL",
        "16_INVOCATIONS_PER_PIXEL",
    };
    _vrs_types_num = (int)rates.size();
    _vrs_types = new char *[_vrs_types_num];
    for (size_t i = 0; i < _vrs_types_num; i++) {
      // deep copy
      _vrs_types[i] = new char[vrs_types[i].size() + 1];
      std::strcpy(_vrs_types[i], vrs_types[i].c_str());
    }

    // easy error check
    if ((int)rates.size() > pal_size) {
      throw std::runtime_error("too many rates for palette");
    }

    glShadingRateImagePaletteNV(0, 0, (GLsizei)rates.size(), rates.data());
  }

  void update_frame_buffer(const int width, const int height) {
    _color_attachment = std::make_unique<Texture2D>(
        nullptr, GL_UNSIGNED_BYTE, width, height, GL_RGBA, GL_RGBA);
    _depth_stencil_attachment =
        std::make_unique<Texture2D>(nullptr,
                                    GL_UNSIGNED_INT_24_8,
                                    width,
                                    height,
                                    GL_DEPTH24_STENCIL8,
                                    GL_DEPTH_STENCIL);
    Texture2D *color_attachments[] = {_color_attachment.get()};
    _framebuffer = std::make_unique<Framebuffer>(
        color_attachments, 1, _depth_stencil_attachment.get());
  }

  void init() override {
    // check vrs is available
    check_ext();

    // texel size
    glGetIntegerv(GL_SHADING_RATE_IMAGE_TEXEL_WIDTH_NV, &_vrs_texel_width);
    glGetIntegerv(GL_SHADING_RATE_IMAGE_TEXEL_HEIGHT_NV, &_vrs_texel_height);
    std::cout << "GL_SHADING_RATE_IMAGE_TEXEL_WIDTH_NV = " << _vrs_texel_width
              << std::endl
              << "GL_SHADING_RATE_IMAGE_TEXEL_HEIGHT_NV = " << _vrs_texel_height
              << std::endl;

    // setup vrs palette
    set_vrs_palette();

    // create vrs texture
    int width, height;
    glfwGetFramebufferSize(_window, &width, &height);
    int w = (width + _vrs_texel_width - 1) / _vrs_texel_width;
    int h = (height + _vrs_texel_height - 1) / _vrs_texel_height;
    std::cout << "vrs texture size: " << w << " x " << h << std::endl;
    _vrs_texture = std::make_unique<VRSTexture>(w, h);
    _vrs_data.resize(w * h);
    std::fill(_vrs_data.begin(), _vrs_data.end(), 7); // 4x4
    _vrs_texture->update(_vrs_data);

    // framebuffer for vrs
    update_frame_buffer(width, height);

    _program = Program::create_from_files("shaders/10-vrs/vrs.vert",
                                          "shaders/10-vrs/vrs.frag");
    _camera = std::make_unique<ModelViewerCamera>(0.299f,
                                                  glm::radians(23.0f),
                                                  glm::radians(77.0f),
                                                  glm::radians(-274.0f),
                                                  2.395f);
    // _scene = std::make_unique<Gltf>("FlightHelmet/FlightHelmet.gltf");
    _scene = std::make_unique<Gltf>("square/square.gltf");

    // get uniform location by name
    _transform_location = glGetUniformLocation(_program->get(), "transform");
    _image_location = glGetUniformLocation(_program->get(), "base_color");
  }

  void draw_ui() {
    Application::draw_ui();
    int vrs_type = _vrs_data[0];
    if (ImGui::Combo("vrs type", &vrs_type, _vrs_types, (int)_vrs_types_num)) {
      std::fill(_vrs_data.begin(), _vrs_data.end(), uint8_t(vrs_type));
      _vrs_texture->update(_vrs_data);
    }

    ImGui::Text("Camera");
    _camera->draw_ui();
  }

  void check_error(int line) {
    GLuint err;
    while ((err = glGetError()) != GL_NO_ERROR) {
      std::cout << "[line " << line << "] OpenGL Error [" << err
                << "] : " << glewGetErrorString(err) << std::endl;
    }
  }

  void draw() {
    check_error(__LINE__);

    glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer->get());
    int width, height;
    glfwGetFramebufferSize(_window, &width, &height);

    // TODO: update data

    glEnable(GL_SHADING_RATE_IMAGE_NV); // enable vrs start
    glBindShadingRateImageNV(_vrs_texture->tex());

    glViewport(0, 0, width, height);

    glClearColor(0.5, 0.5, 0.5, 1.0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(_program->get());
    float aspect = (float)width / (float)height;

    glm::mat4 view = _camera->view();
    glm::mat4 projection = _camera->projection(aspect);

    for (auto &draw : _scene->draws) {
      auto transform = projection * view * draw.transform;
      glUniformMatrix4fv(_transform_location, 1, false, (GLfloat *)&transform);
      for (auto &prim : _scene->meshes[draw.index]) {
        auto *mat = _scene->materials[prim.material].get();
        auto *base_tex = _scene->textures[mat->base_color].get();

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, base_tex->get());
        glUniform1i(_image_location, 0);

        prim.mesh->draw();
      }
    }

    glDisable(GL_SHADING_RATE_IMAGE_NV); // enable vrs end
    glDisable(GL_DEPTH_TEST);

    // blit to screen
    glBindFramebuffer(GL_READ_FRAMEBUFFER, _framebuffer->get());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0,
                      0,
                      width,
                      height,
                      0,
                      0,
                      width,
                      height,
                      GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void update() override {
    draw_ui();
    draw();
  }

  std::unique_ptr<Program> _program;

  GLint _transform_location;
  GLint _image_location;

  GLint _vrs_texel_width, _vrs_texel_height;
  std::unique_ptr<VRSTexture> _vrs_texture;
  std::vector<uint8_t> _vrs_data;
  char **_vrs_types;
  int _vrs_types_num{0};

  // Note that this extension requires the use of a framebuffer object; the
  // shading rate image and related state are ignored when rendering to the
  // default framebuffer.
  std::unique_ptr<Framebuffer> _framebuffer{};
  std::unique_ptr<Texture2D> _color_attachment{};
  std::unique_ptr<Texture2D> _depth_stencil_attachment{};

  std::unique_ptr<ModelViewerCamera> _camera;
  std::unique_ptr<Gltf> _scene;
};

int main() {
  try {
    VRSApp app{};
    app.run();
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
  }
}