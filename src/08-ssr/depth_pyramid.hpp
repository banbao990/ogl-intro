#pragma once

#include <cstdint>
#include <memory>

#include <GL/glew.h>

#include <common/shader.hpp>
#include <common/texture.hpp>

class DepthPyramid {
public:
  DepthPyramid();
  ~DepthPyramid();

  DepthPyramid(const DepthPyramid &) = delete;
  DepthPyramid &operator=(const DepthPyramid &) = delete;

  void resize(uint32_t width, uint32_t height);
  void build(Texture2D *source_depth);

  GLuint texture() const;
  int max_mip_level() const;

private:
  static int level_dimension(uint32_t base, int level);

  std::unique_ptr<Program> _program;
  GLuint _texture{0};
  uint32_t _width{0};
  uint32_t _height{0};
  int _level_count{0};

  GLint _source_tex_location{-1};
  GLint _source_size_location{-1};
  GLint _target_size_location{-1};
  GLint _source_level_location{-1};
  GLint _copy_level_location{-1};
};
