#include "depth_pyramid.hpp"

#include <algorithm>
#include <stdexcept>

DepthPyramid::DepthPyramid() {
  _program = Program::create_compute_shader_from_file(
      "shaders/08-ssr/depth_pyramid.comp");

  const GLuint id = _program->get();
  _source_tex_location = glGetUniformLocation(id, "g_source_tex");
  _source_size_location = glGetUniformLocation(id, "g_source_size");
  _target_size_location = glGetUniformLocation(id, "g_target_size");
  _source_level_location = glGetUniformLocation(id, "g_source_level");
  _copy_level_location = glGetUniformLocation(id, "g_copy_level");
}

DepthPyramid::~DepthPyramid() {
  if (_texture != 0) {
    glDeleteTextures(1, &_texture);
  }
}

int DepthPyramid::level_dimension(uint32_t base, int level) {
  return std::max(1, static_cast<int>(base >> level));
}

void DepthPyramid::resize(uint32_t width, uint32_t height) {
  if (_texture != 0 && _width == width && _height == height) {
    return;
  }

  if (_texture != 0) {
    glDeleteTextures(1, &_texture);
    _texture = 0;
  }

  _width = width;
  _height = height;
  _level_count = 0;

  if (width == 0 || height == 0) {
    return;
  }

  uint32_t largest_dimension = std::max(width, height);
  _level_count = 1;
  while (largest_dimension > 1) {
    largest_dimension >>= 1;
    ++_level_count;
  }

  glGenTextures(1, &_texture);
  glBindTexture(GL_TEXTURE_2D, _texture);
  glTexStorage2D(GL_TEXTURE_2D,
                 _level_count,
                 GL_R32F,
                 static_cast<GLsizei>(width),
                 static_cast<GLsizei>(height));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(
      GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, _level_count - 1);
  glBindTexture(GL_TEXTURE_2D, 0);
}

void DepthPyramid::build(Texture2D *source_depth) {
  if (_texture == 0 || source_depth == nullptr || _level_count == 0) {
    return;
  }
  if (source_depth->width() != static_cast<int>(_width) ||
      source_depth->height() != static_cast<int>(_height)) {
    throw std::runtime_error(
        "depth pyramid source dimensions do not match its allocation");
  }

  glUseProgram(_program->get());
  glUniform1i(_source_tex_location, 0);

  for (int level = 0; level < _level_count; ++level) {
    const bool copy_level = level == 0;
    const int source_level = copy_level ? 0 : level - 1;
    const int source_width = copy_level ? static_cast<int>(_width)
                                        : level_dimension(_width, source_level);
    const int source_height = copy_level
                                  ? static_cast<int>(_height)
                                  : level_dimension(_height, source_level);
    const int target_width = level_dimension(_width, level);
    const int target_height = level_dimension(_height, level);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, copy_level ? source_depth->get() : _texture);
    glBindImageTexture(0, _texture, level, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);

    glUniform2i(_source_size_location, source_width, source_height);
    glUniform2i(_target_size_location, target_width, target_height);
    glUniform1i(_source_level_location, source_level);
    glUniform1i(_copy_level_location, copy_level ? 1 : 0);

    const GLuint group_count_x = static_cast<GLuint>((target_width + 7) / 8);
    const GLuint group_count_y = static_cast<GLuint>((target_height + 7) / 8);
    glDispatchCompute(group_count_x, group_count_y, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                    GL_TEXTURE_FETCH_BARRIER_BIT);
  }

  glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
  glBindTexture(GL_TEXTURE_2D, 0);
}

GLuint DepthPyramid::texture() const {
  return _texture;
}

int DepthPyramid::max_mip_level() const {
  return std::max(0, _level_count - 1);
}
