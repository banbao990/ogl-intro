#pragma once

#include <common/shader.hpp>
#include <common/renderer.hpp>
#include <common/texture.hpp>

class DepthMaterial : public IMaterial {
public:
  DepthMaterial();
  void use() override;

private:
  std::unique_ptr<Program> _program;
  GLint _transform_location;
};

class WaterMaterial : public IMaterial {
private:
  struct TransformBlock {
    glm::mat4 _M;
    glm::mat4 _MV;
    glm::mat4 _I_MV;
    glm::mat4 _P;
    glm::mat4 _I_P;
  };

  struct ParamsBlock {
    glm::vec4 _light_dir_vs;
    glm::vec4 _light_radiance;
    glm::vec4 _env_radiance;
    glm::vec4 _wave_speeds;
    glm::vec4 _wave_params;
    glm::vec4 _water_params0;
    glm::vec4 _water_params1;
    glm::vec4 _absorption;
    glm::vec4 _ssr_params0;
    glm::vec4 _camera_params;
    glm::ivec4 _screen_params;
    glm::ivec4 _mode_params;
  };

  static_assert(sizeof(TransformBlock) == 320);
  static_assert(sizeof(ParamsBlock) == 192);

public:
  enum class SSRMode : int {
    Off = 0,
    FixedStep = 1,
    PixelDDA = 2,
    HiZ = 3,
  };
  enum class DebugView : int {
    FinalComposite = 0,
    SSROnly = 1,
    LinearSceneDepth = 2,
    HitMaskConfidence = 3,
    HitUV = 4,
    IterationCount = 5,
  };

  WaterMaterial();
  void use() override;
  bool draw_ui();

  void reset_params();
  void set_scene_textures(Texture2D *color_tex, Texture2D *depth_tex);
  void set_depth_pyramid(GLuint texture, int max_mip_level);
  void update_window_size(uint32_t width, uint32_t height);
  bool uses_hiz() const;

public:
  glm::vec3 light_dir_vs;
  glm::vec3 light_radiance;
  glm::vec3 env_radiance;

private:
  std::unique_ptr<Program> _program;

  std::unique_ptr<Buffer> _params_buffer;
  std::unique_ptr<Buffer> _transform_buffer;

  GLint _wave_tex_location;
  std::unique_ptr<Texture2D> _wave_tex;
  GLint _scene_color_tex_location;
  GLint _scene_depth_tex_location;
  GLint _hiz_tex_location;
  Texture2D *_scene_color_tex{nullptr};
  Texture2D *_scene_depth_tex{nullptr};
  GLuint _hiz_tex{0};
  int _hiz_max_mip{0};

  glm::vec2 _wave_speed1{};
  glm::vec2 _wave_speed2{};
  float _wave_global_speed{1.0f};
  float _wave_scale1{2.0f};
  float _wave_scale2{10.0f};
  float _wave_strength{0.1f};
  bool _flat_water{false};
  float _refraction_distortion_strength{0.02f};
  float _fresnel_f0{0.02f};
  glm::vec3 _absorption_coeff{};
  float _absorption_strength{0.35f};
  float _specular_strength{0.8f};
  float _edge_fade_width{0.08f};
  float _max_water_depth{20.0f};

  float _ssr_step_size{0.1f};
  float _ssr_max_distance{20.0f};
  float _ssr_thickness{0.03f};
  float _ssr_origin_bias{0.005f};
  int _ssr_max_steps{4096};
  int _ssr_binary_steps{5};

  // Must match FPSCamera's projection range.
  float _near_plane{0.1f};
  float _far_plane{200.0f};
  uint32_t _window_width{1};
  uint32_t _window_height{1};
  SSRMode _ssr_mode{SSRMode::PixelDDA};
  DebugView _debug_view{DebugView::FinalComposite};
};
