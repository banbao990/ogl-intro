#version 330 core

in vec3 position_vs;
in vec3 normal_vs;
in vec3 tangent_vs;
in vec3 bitangent_vs;
in vec2 uv0_vs;
in vec4 wave_uv_vs;

layout(std140) uniform Params {
  vec4 g_light_dir_vs;
  vec4 g_light_radiance;
  vec4 g_env_radiance;
  vec4 g_wave_speeds;
  vec4 g_wave_params;
  vec4 g_water_params0;
  vec4 g_water_params1;
  vec4 g_absorption;
  vec4 g_ssr_params0;
  vec4 g_camera_params;
  ivec4 g_screen_params;
  ivec4 g_mode_params;
};

#define g_time g_water_params0.x
#define g_wave_strength g_water_params0.y
#define g_refraction_distortion_strength g_water_params0.z
#define g_fresnel_f0 g_water_params0.w

#define g_absorption_strength g_water_params1.x
#define g_specular_strength g_water_params1.y
#define g_edge_fade_width g_water_params1.z
#define g_max_water_depth g_water_params1.w

#define g_step_size g_ssr_params0.x
#define g_max_distance g_ssr_params0.y
#define g_thickness g_ssr_params0.z
#define g_origin_bias g_ssr_params0.w

#define g_near_plane g_camera_params.x
#define g_far_plane g_camera_params.y

#define g_width g_screen_params.x
#define g_height g_screen_params.y
#define g_max_steps g_screen_params.z
#define g_binary_steps g_screen_params.w

#define g_ssr_mode g_mode_params.x
#define g_debug_view g_mode_params.y
#define g_hiz_max_mip g_mode_params.z
#define g_flat_water g_mode_params.w

layout(std140) uniform Transform {
  mat4 M;
  mat4 MV;
  mat4 I_MV;
  mat4 P;
  mat4 I_P;
};

layout(location = 0) out vec4 frag_color_out;

uniform sampler2D g_wave_tex;
uniform sampler2D g_scene_color_tex;
uniform sampler2D g_scene_depth_tex;
uniform sampler2D g_hiz_tex;

const int SSR_MODE_OFF = 0;
const int SSR_MODE_FIXED = 1;
const int SSR_MODE_PIXEL_DDA = 2;
const int SSR_MODE_HIZ = 3;

const int DEBUG_FINAL = 0;
const int DEBUG_SSR_ONLY = 1;
const int DEBUG_LINEAR_DEPTH = 2;
const int DEBUG_HIT_MASK = 3;
const int DEBUG_HIT_UV = 4;
const int DEBUG_ITERATIONS = 5;

const int MISS_NONE = 0;
const int MISS_SCREEN = 1;
const int MISS_DISTANCE = 2;
const int MISS_ITERATIONS = 3;
const int MISS_DEPTH_REJECTED = 4;
const int MISS_DISABLED = 5;

const int MAX_FIXED_STEPS = 4096;
const int MAX_PIXEL_DDA_STEPS = 4096;
const int MAX_HIZ_STEPS = 4096;
const int MAX_BINARY_STEPS = 8;

struct TraceResult {
  bool hit;
  vec2 uv;
  float distance_vs;
  int steps;
  float confidence;
  int miss_reason;
  int max_mip;
};

struct FixedStepSample {
  bool projected;
  bool valid;
  float distance_vs;
  float depth_delta;
  vec2 uv;
  vec3 scene_position_vs;
};

struct PixelDdaCellData {
  bool valid;
  float scene_depth;
  vec3 scene_position_vs;
  float ray_depth_start;
  float ray_depth_end;
  float depth_epsilon;
};

vec3 safe_normalize(vec3 value) {
  float length_squared = dot(value, value);
  return length_squared > 1e-10 ? value * inversesqrt(length_squared) : value;
}

bool inside_screen(vec2 uv) {
  return all(greaterThanEqual(uv, vec2(0.0))) &&
         all(lessThan(uv, vec2(1.0)));
}

TraceResult make_miss(int reason) {
  TraceResult result;
  result.hit = false;
  result.uv = vec2(0.0);
  result.distance_vs = 0.0;
  result.steps = 0;
  result.confidence = 0.0;
  result.miss_reason = reason;
  result.max_mip = 0;
  return result;
}

vec3 unproject_texture_position(vec3 texture_position) {
  vec4 clip =
      vec4(texture_position.xy * 2.0 - 1.0,
           texture_position.z * 2.0 - 1.0,
           1.0);
  vec4 view = I_P * clip;
  return view.xyz / view.w;
}

float project_view_depth(float view_depth) {
  vec4 clip = P * vec4(0.0, 0.0, -view_depth, 1.0);
  if (clip.w <= 1e-8) {
    return 0.0;
  }
  return (clip.z / clip.w) * 0.5 + 0.5;
}

bool project_view_position(vec3 view_position, out vec3 texture_position) {
  vec4 clip = P * vec4(view_position, 1.0);
  if (clip.w <= 1e-6) {
    texture_position = vec3(0.0);
    return false;
  }

  vec3 ndc = clip.xyz / clip.w;
  texture_position = ndc * 0.5 + 0.5;
  return inside_screen(texture_position.xy) && texture_position.z >= 0.0 &&
         texture_position.z <= 1.0;
}

bool sample_scene_view_position(vec2 uv,
                                out float raw_depth,
                                out vec3 scene_position_vs) {
  if (!inside_screen(uv)) {
    raw_depth = 1.0;
    scene_position_vs = vec3(0.0);
    return false;
  }

  raw_depth = texture(g_scene_depth_tex, uv).r;
  if (raw_depth >= 0.999999) {
    scene_position_vs = vec3(0.0);
    return false;
  }

  scene_position_vs =
      unproject_texture_position(vec3(uv, raw_depth));
  return true;
}

float hit_confidence_for_range(vec2 uv,
                               float distance_vs,
                               float distance_range) {
  float edge_distance =
      min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
  float edge_confidence =
      smoothstep(0.0, max(g_edge_fade_width, 1e-4), edge_distance);
  float distance_confidence =
      1.0 - clamp(distance_vs / max(distance_range, 1e-4), 0.0, 1.0);
  return edge_confidence * distance_confidence;
}

float hit_confidence(vec2 uv, float distance_vs) {
  return hit_confidence_for_range(uv, distance_vs, g_max_distance);
}

bool clip_pixel_line_axis(float origin,
                          float direction,
                          float lower_bound,
                          float upper_bound,
                          inout float enter_parameter,
                          inout float exit_parameter) {
  if (abs(direction) < 1e-8) {
    return origin >= lower_bound && origin <= upper_bound;
  }

  float first = (lower_bound - origin) / direction;
  float second = (upper_bound - origin) / direction;
  if (first > second) {
    float temporary = first;
    first = second;
    second = temporary;
  }

  enter_parameter = max(enter_parameter, first);
  exit_parameter = min(exit_parameter, second);
  return enter_parameter <= exit_parameter;
}

FixedStepSample evaluate_fixed_step_sample(vec3 ray_origin,
                                           vec3 ray_direction,
                                           float distance_along_ray) {
  FixedStepSample sample;
  sample.projected = false;
  sample.valid = false;
  sample.distance_vs = distance_along_ray;
  sample.depth_delta = 0.0;
  sample.uv = vec2(0.0);
  sample.scene_position_vs = vec3(0.0);

  vec3 ray_position = ray_origin + ray_direction * distance_along_ray;
  vec3 texture_position;
  if (!project_view_position(ray_position, texture_position)) {
    return sample;
  }

  sample.projected = true;
  sample.uv = texture_position.xy;
  float raw_depth;
  if (!sample_scene_view_position(
          sample.uv, raw_depth, sample.scene_position_vs)) {
    return sample;
  }

  sample.valid = true;
  // Visible view-space positions have negative Z. Positive means the reflected
  // ray lies behind the recorded surface; either sign-crossing direction is
  // valid because a reflection ray may move toward or away from the camera.
  sample.depth_delta = sample.scene_position_vs.z - ray_position.z;
  return sample;
}

float fixed_step_depth_epsilon(FixedStepSample first_sample,
                               FixedStepSample second_sample) {
  float maximum_depth = max(-first_sample.scene_position_vs.z,
                            -second_sample.scene_position_vs.z);
  return max(maximum_depth * 1e-5, 1e-4);
}

bool fixed_step_depth_bracket(float first_delta,
                              float second_delta,
                              float epsilon) {
  return abs(first_delta) <= epsilon || abs(second_delta) <= epsilon ||
         (first_delta < 0.0 && second_delta > 0.0) ||
         (first_delta > 0.0 && second_delta < 0.0);
}

bool fixed_step_samples_continuous(FixedStepSample first_sample,
                                   FixedStepSample second_sample) {
  if (!first_sample.valid || !second_sample.valid) {
    return false;
  }

  float first_depth = -first_sample.scene_position_vs.z;
  float second_depth = -second_sample.scene_position_vs.z;
  float mean_depth = 0.5 * (first_depth + second_depth);
  if (mean_depth <= 1e-6) {
    return false;
  }

  float depth_epsilon = fixed_step_depth_epsilon(
      first_sample, second_sample);
  vec3 first_at_mean_depth =
      first_sample.scene_position_vs *
      (mean_depth / max(first_depth, 1e-6));
  vec3 second_at_mean_depth =
      second_sample.scene_position_vs *
      (mean_depth / max(second_depth, 1e-6));
  float projected_footprint =
      length(second_at_mean_depth - first_at_mean_depth);
  float continuity_limit =
      min(16.0 * projected_footprint, 0.05 * mean_depth) +
      4.0 * depth_epsilon;
  return abs(second_depth - first_depth) <= continuity_limit;
}

bool refine_fixed_step_bracket(vec3 ray_origin,
                               vec3 ray_direction,
                               FixedStepSample first_sample,
                               FixedStepSample second_sample,
                               out FixedStepSample hit_sample) {
  hit_sample = abs(first_sample.depth_delta) <=
                       abs(second_sample.depth_delta)
                   ? first_sample
                   : second_sample;

  float initial_epsilon = fixed_step_depth_epsilon(
      first_sample, second_sample);
  if (!fixed_step_samples_continuous(first_sample, second_sample) ||
      !fixed_step_depth_bracket(first_sample.depth_delta,
                                second_sample.depth_delta,
                                initial_epsilon)) {
    return false;
  }

  for (int i = 0; i < MAX_BINARY_STEPS; ++i) {
    if (i >= g_binary_steps) {
      break;
    }

    float middle_distance =
        0.5 * (first_sample.distance_vs + second_sample.distance_vs);
    FixedStepSample middle_sample = evaluate_fixed_step_sample(
        ray_origin, ray_direction, middle_distance);
    if (!middle_sample.projected || !middle_sample.valid ||
        !fixed_step_samples_continuous(first_sample, middle_sample) ||
        !fixed_step_samples_continuous(middle_sample, second_sample)) {
      return false;
    }

    if (abs(middle_sample.depth_delta) < abs(hit_sample.depth_delta)) {
      hit_sample = middle_sample;
    }

    float first_half_epsilon = fixed_step_depth_epsilon(
        first_sample, middle_sample);
    float second_half_epsilon = fixed_step_depth_epsilon(
        middle_sample, second_sample);
    bool first_half_brackets = fixed_step_depth_bracket(
        first_sample.depth_delta,
        middle_sample.depth_delta,
        first_half_epsilon);
    bool second_half_brackets = fixed_step_depth_bracket(
        middle_sample.depth_delta,
        second_sample.depth_delta,
        second_half_epsilon);

    if (abs(middle_sample.depth_delta) <=
        max(first_half_epsilon, second_half_epsilon)) {
      hit_sample = middle_sample;
      break;
    } else if (first_half_brackets) {
      second_sample = middle_sample;
    } else if (second_half_brackets) {
      first_sample = middle_sample;
    } else {
      return false;
    }
  }

  if (abs(first_sample.depth_delta) < abs(hit_sample.depth_delta)) {
    hit_sample = first_sample;
  }
  if (abs(second_sample.depth_delta) < abs(hit_sample.depth_delta)) {
    hit_sample = second_sample;
  }
  float final_epsilon = fixed_step_depth_epsilon(
      first_sample, second_sample);
  return hit_sample.valid &&
         abs(hit_sample.depth_delta) <= max(g_thickness, final_epsilon);
}

bool clip_fixed_step_ray(vec3 ray_origin,
                         vec3 ray_direction,
                         out float start_distance,
                         out float end_distance,
                         out bool clipped_by_screen,
                         out int miss_reason) {
  start_distance = 0.0;
  end_distance = 0.0;
  clipped_by_screen = false;
  miss_reason = MISS_SCREEN;

  float ray_length = g_max_distance;
  if (ray_direction.z > 1e-6) {
    float near_distance =
        (-g_near_plane - ray_origin.z) / ray_direction.z;
    ray_length = min(ray_length, near_distance);
  } else if (ray_direction.z < -1e-6) {
    float far_distance =
        (-g_far_plane - ray_origin.z) / ray_direction.z;
    ray_length = min(ray_length, far_distance);
  }
  ray_length = max(ray_length - 1e-4, 0.0);
  if (ray_length <= 0.0) {
    miss_reason = MISS_DISTANCE;
    return false;
  }

  vec3 ray_end_vs = ray_origin + ray_direction * ray_length;
  vec4 homogeneous_start = P * vec4(ray_origin, 1.0);
  vec4 homogeneous_end = P * vec4(ray_end_vs, 1.0);
  if (homogeneous_start.w <= 1e-6 || homogeneous_end.w <= 1e-6) {
    return false;
  }

  float start_k = 1.0 / homogeneous_start.w;
  float end_k = 1.0 / homogeneous_end.w;
  vec3 start_q = ray_origin * start_k;
  vec3 end_q = ray_end_vs * end_k;
  ivec2 depth_size = textureSize(g_scene_depth_tex, 0);
  if (any(lessThanEqual(depth_size, ivec2(0)))) {
    return false;
  }

  vec2 screen_size = vec2(depth_size);
  vec2 start_pixel =
      (homogeneous_start.xy * start_k * 0.5 + 0.5) * screen_size;
  vec2 end_pixel =
      (homogeneous_end.xy * end_k * 0.5 + 0.5) * screen_size;
  vec2 pixel_delta = end_pixel - start_pixel;
  float enter_parameter = 0.0;
  float exit_parameter = 1.0;
  vec2 upper_bound = screen_size - vec2(1e-4);
  bool intersects_viewport =
      clip_pixel_line_axis(start_pixel.x,
                           pixel_delta.x,
                           0.0,
                           upper_bound.x,
                           enter_parameter,
                           exit_parameter) &&
      clip_pixel_line_axis(start_pixel.y,
                           pixel_delta.y,
                           0.0,
                           upper_bound.y,
                           enter_parameter,
                           exit_parameter);
  enter_parameter = clamp(enter_parameter, 0.0, 1.0);
  exit_parameter = clamp(exit_parameter, 0.0, 1.0);
  if (!intersects_viewport || exit_parameter < enter_parameter) {
    return false;
  }

  float clipped_start_k = mix(start_k, end_k, enter_parameter);
  float clipped_end_k = mix(start_k, end_k, exit_parameter);
  if (abs(clipped_start_k) < 1e-8 || abs(clipped_end_k) < 1e-8) {
    return false;
  }
  vec3 clipped_start_vs =
      mix(start_q, end_q, enter_parameter) / clipped_start_k;
  vec3 clipped_end_vs =
      mix(start_q, end_q, exit_parameter) / clipped_end_k;
  start_distance = clamp(
      dot(clipped_start_vs - ray_origin, ray_direction), 0.0, ray_length);
  end_distance = clamp(
      dot(clipped_end_vs - ray_origin, ray_direction), 0.0, ray_length);
  clipped_by_screen =
      enter_parameter > 1e-6 || exit_parameter < 1.0 - 1e-6;
  return end_distance > start_distance + 1e-6;
}

TraceResult trace_fixed_step(vec3 ray_origin, vec3 ray_direction) {
  TraceResult result = make_miss(MISS_ITERATIONS);
  float start_distance;
  float end_distance;
  bool clipped_by_screen;
  int clip_miss_reason;
  if (!clip_fixed_step_ray(ray_origin,
                           ray_direction,
                           start_distance,
                           end_distance,
                           clipped_by_screen,
                           clip_miss_reason)) {
    return make_miss(clip_miss_reason);
  }

  FixedStepSample previous_sample = evaluate_fixed_step_sample(
      ray_origin, ray_direction, start_distance);
  if (!previous_sample.projected) {
    return make_miss(MISS_SCREEN);
  }

  float step_size = max(g_step_size, 1e-4);
  bool saw_depth_rejection = false;
  for (int i = 0; i < MAX_FIXED_STEPS; ++i) {
    if (i >= g_max_steps) {
      result.miss_reason = MISS_ITERATIONS;
      result.steps = i;
      return result;
    }

    float distance_along_ray = min(
        start_distance + float(i + 1) * step_size, end_distance);
    FixedStepSample current_sample = evaluate_fixed_step_sample(
        ray_origin, ray_direction, distance_along_ray);
    if (!current_sample.projected) {
      result.miss_reason = saw_depth_rejection
                               ? MISS_DEPTH_REJECTED
                               : MISS_SCREEN;
      result.steps = i + 1;
      return result;
    }

    if (previous_sample.valid && current_sample.valid) {
      float depth_epsilon = fixed_step_depth_epsilon(
          previous_sample, current_sample);
      if (fixed_step_depth_bracket(previous_sample.depth_delta,
                                   current_sample.depth_delta,
                                   depth_epsilon)) {
        FixedStepSample hit_sample;
        if (refine_fixed_step_bracket(ray_origin,
                                      ray_direction,
                                      previous_sample,
                                      current_sample,
                                      hit_sample)) {
          result.hit = true;
          result.uv = hit_sample.uv;
          result.distance_vs = hit_sample.distance_vs;
          result.steps = i + 1;
          float effective_distance = min(
              g_max_distance,
              step_size *
                  float(max(min(g_max_steps, MAX_FIXED_STEPS), 1)));
          result.confidence = hit_confidence_for_range(
              hit_sample.uv, hit_sample.distance_vs, effective_distance);
          result.miss_reason = MISS_NONE;
          return result;
        }
        // A silhouette, background gap or insufficient binary precision must
        // not hide a later valid crossing. Keep marching from this sample.
        saw_depth_rejection = true;
      }
    }

    previous_sample = current_sample;
    if (distance_along_ray >= end_distance - 1e-6) {
      result.steps = i + 1;
      result.miss_reason =
          saw_depth_rejection
              ? MISS_DEPTH_REJECTED
              : (clipped_by_screen ? MISS_SCREEN : MISS_DISTANCE);
      return result;
    }
  }

  result.steps = min(g_max_steps, MAX_FIXED_STEPS);
  result.miss_reason = MISS_ITERATIONS;
  return result;
}

bool pixel_dda_depth_bracket(float first_delta,
                             float second_delta,
                             float epsilon) {
  return (first_delta <= epsilon && second_delta >= -epsilon) ||
         (first_delta >= -epsilon && second_delta <= epsilon);
}

bool load_pixel_dda_cell_data(ivec2 pixel,
                              float ray_depth_start,
                              float ray_depth_end,
                              ivec2 depth_size,
                              out PixelDdaCellData cell_data) {
  cell_data.valid = false;
  cell_data.scene_depth = 0.0;
  cell_data.scene_position_vs = vec3(0.0);
  cell_data.ray_depth_start = ray_depth_start;
  cell_data.ray_depth_end = ray_depth_end;
  cell_data.depth_epsilon = 1e-4;

  if (any(lessThan(pixel, ivec2(0))) ||
      any(greaterThanEqual(pixel, depth_size))) {
    return false;
  }

  float raw_scene_depth = texelFetch(g_scene_depth_tex, pixel, 0).r;
  if (raw_scene_depth >= 0.999999) {
    return false;
  }

  vec2 scene_uv = (vec2(pixel) + vec2(0.5)) / vec2(depth_size);
  vec3 scene_position_vs =
      unproject_texture_position(vec3(scene_uv, raw_scene_depth));
  float scene_depth = -scene_position_vs.z;

  cell_data.valid = true;
  cell_data.scene_depth = scene_depth;
  cell_data.scene_position_vs = scene_position_vs;
  cell_data.depth_epsilon = max(scene_depth * 1e-5, 1e-4);
  return true;
}

bool test_pixel_dda_cell(ivec2 pixel,
                          vec2 segment_start_pixel,
                          vec2 segment_end_pixel,
                          vec3 segment_start_q,
                          vec3 segment_end_q,
                          float segment_start_k,
                          float segment_end_k,
                          ivec2 depth_size,
                          out bool depth_candidate,
                          out PixelDdaCellData cell_data,
                         out vec2 hit_uv,
                         out vec3 hit_position_vs) {
  depth_candidate = false;
  hit_uv = vec2(0.0);
  hit_position_vs = vec3(0.0);

  if (abs(segment_start_k) < 1e-8 ||
      abs(segment_end_k) < 1e-8) {
    depth_candidate = true;
    cell_data.valid = false;
    return false;
  }

  // Test the full ray interval assigned to this pixel. View-space Z is
  // reconstructed with Q/k, so perspective remains correct across the step.
  float ray_depth_start = -segment_start_q.z / segment_start_k;
  float ray_depth_end = -segment_end_q.z / segment_end_k;
  if (!load_pixel_dda_cell_data(pixel,
                                ray_depth_start,
                                ray_depth_end,
                                depth_size,
                                cell_data)) {
    return false;
  }

  vec2 depth_size_f = vec2(depth_size);
  float scene_depth = cell_data.scene_depth;
  vec3 scene_position_vs = cell_data.scene_position_vs;
  float depth_epsilon = cell_data.depth_epsilon;
  float start_delta = ray_depth_start - scene_depth;
  float end_delta = ray_depth_end - scene_depth;

  // A reflected ray may move either away from or toward the camera. The old
  // one-sided test rejected every positive-to-negative crossing, making whole
  // camera-angle ranges disappear. Require a directed sign bracket, but accept
  // it in either orientation.
  depth_candidate = max(start_delta, end_delta) >= -depth_epsilon;
  if (!pixel_dda_depth_bracket(
          start_delta, end_delta, depth_epsilon)) {
    return false;
  }
  depth_candidate = true;

  // Solve Qz(lambda) / k(lambda) for the recorded surface itself. Never clamp
  // the target to a ray endpoint: without a true directed zero crossing that
  // would manufacture a hit inside the numeric tolerance.
  float target_depth = scene_depth;
  float target_view_z = -target_depth;
  vec3 segment_delta_q = segment_end_q - segment_start_q;
  float segment_delta_k = segment_end_k - segment_start_k;
  float denominator =
      segment_delta_q.z - target_view_z * segment_delta_k;
  float lambda = 0.5;
  if (abs(denominator) > 1e-8) {
    lambda =
        (target_view_z * segment_start_k - segment_start_q.z) /
        denominator;
  } else {
    float start_error = abs(ray_depth_start - target_depth);
    float end_error = abs(ray_depth_end - target_depth);
    lambda = start_error <= end_error ? 0.0 : 1.0;
  }

  if (lambda < -1e-4 || lambda > 1.0001) {
    return false;
  }
  lambda = clamp(lambda, 0.0, 1.0);

  vec3 hit_q = mix(segment_start_q, segment_end_q, lambda);
  float hit_k = mix(segment_start_k, segment_end_k, lambda);
  if (abs(hit_k) < 1e-8) {
    return false;
  }

  hit_position_vs = hit_q / hit_k;
  float depth_delta = scene_position_vs.z - hit_position_vs.z;
  if (abs(depth_delta) > depth_epsilon) {
    return false;
  }

  // The grid traversal guarantees that the segment belongs to this texel. A
  // root on the closed exit boundary is accepted here so a following
  // background/discontinuity cell cannot erase an otherwise valid hit.
  vec2 hit_pixel =
      mix(segment_start_pixel, segment_end_pixel, lambda);
  vec2 bounded_hit_pixel =
      clamp(hit_pixel, vec2(0.0), depth_size_f - vec2(1e-4));
  hit_uv = bounded_hit_pixel / depth_size_f;
  return true;
}

float pixel_dda_ray_depth(vec3 ray_start_q,
                          vec3 ray_end_q,
                          float ray_start_k,
                          float ray_end_k,
                          float ray_parameter) {
  vec3 ray_q = mix(ray_start_q, ray_end_q, ray_parameter);
  float ray_k = mix(ray_start_k, ray_end_k, ray_parameter);
  if (abs(ray_k) < 1e-8) {
    return 1e30;
  }
  return -ray_q.z / ray_k;
}

bool pixel_dda_cells_continuous(PixelDdaCellData first_data,
                                PixelDdaCellData second_data) {
  if (!first_data.valid || !second_data.valid) {
    return false;
  }

  float mean_depth =
      0.5 * (first_data.scene_depth + second_data.scene_depth);
  float depth_epsilon =
      max(max(first_data.depth_epsilon, second_data.depth_epsilon),
          max(mean_depth * 1e-5, 1e-4));

  // Compare the two samples at one common depth so a silhouette cannot enlarge
  // its own tolerance. Sixteen projected pixel footprints retain steep but
  // continuous surfaces; the five-percent cap still rejects real depth jumps.
  vec3 first_at_mean_depth =
      first_data.scene_position_vs *
      (mean_depth / max(first_data.scene_depth, 1e-6));
  vec3 second_at_mean_depth =
      second_data.scene_position_vs *
      (mean_depth / max(second_data.scene_depth, 1e-6));
  float pixel_footprint =
      length(second_at_mean_depth - first_at_mean_depth);
  float continuity_limit =
      min(16.0 * pixel_footprint, 0.05 * mean_depth) +
      4.0 * depth_epsilon;
  return abs(second_data.scene_depth - first_data.scene_depth) <=
         continuity_limit;
}

bool pixel_dda_boundary_pair_brackets(PixelDdaCellData first_data,
                                      PixelDdaCellData second_data,
                                      float boundary_ray_depth) {
  float mean_depth =
      0.5 * (first_data.scene_depth + second_data.scene_depth);
  float depth_epsilon =
      max(max(first_data.depth_epsilon, second_data.depth_epsilon),
          max(mean_depth * 1e-5, 1e-4));
  float first_delta = boundary_ray_depth - first_data.scene_depth;
  float second_delta = boundary_ray_depth - second_data.scene_depth;
  return pixel_dda_depth_bracket(
      first_delta, second_delta, depth_epsilon);
}

bool reconstruct_pixel_dda_hit(float hit_parameter,
                               vec2 ray_start_pixel,
                               vec2 ray_end_pixel,
                               vec3 ray_start_q,
                               vec3 ray_end_q,
                               float ray_start_k,
                               float ray_end_k,
                               ivec2 depth_size,
                               out vec2 hit_uv,
                               out vec3 hit_position_vs) {
  vec3 hit_q = mix(ray_start_q, ray_end_q, hit_parameter);
  float hit_k = mix(ray_start_k, ray_end_k, hit_parameter);
  if (abs(hit_k) < 1e-8) {
    return false;
  }

  hit_position_vs = hit_q / hit_k;
  vec2 hit_pixel =
      mix(ray_start_pixel, ray_end_pixel, hit_parameter);
  vec2 bounded_hit_pixel =
      clamp(hit_pixel, vec2(0.0), vec2(depth_size) - vec2(1e-4));
  hit_uv = bounded_hit_pixel / vec2(depth_size);
  return true;
}

bool test_pixel_dda_boundary(PixelDdaCellData previous_data,
                             PixelDdaCellData current_data,
                             ivec2 previous_cell,
                             ivec2 current_cell,
                             float boundary_parameter,
                             vec2 ray_start_pixel,
                             vec2 ray_end_pixel,
                             vec3 ray_start_q,
                             vec3 ray_end_q,
                             float ray_start_k,
                             float ray_end_k,
                             ivec2 depth_size,
                             out vec2 hit_uv,
                             out vec3 hit_position_vs) {
  hit_uv = vec2(0.0);
  hit_position_vs = vec3(0.0);
  if (!previous_data.valid || !current_data.valid) {
    return false;
  }

  float boundary_ray_depth = pixel_dda_ray_depth(ray_start_q,
                                                  ray_end_q,
                                                  ray_start_k,
                                                  ray_end_k,
                                                  boundary_parameter);
  ivec2 cell_delta = abs(current_cell - previous_cell);
  bool valid_crossing = false;

  if (cell_delta.x + cell_delta.y == 1) {
    valid_crossing =
        pixel_dda_cells_continuous(previous_data, current_data) &&
        pixel_dda_boundary_pair_brackets(
            previous_data, current_data, boundary_ray_depth);
  } else if (cell_delta.x == 1 && cell_delta.y == 1) {
    // A ray crossing an exact grid corner touches two side texels. Treat the
    // transition as a supercover only when one complete two-edge path remains
    // depth-continuous and contains a bidirectional bracket. Background or a
    // silhouette in either edge breaks that path.
    ivec2 side_cell_x = ivec2(current_cell.x, previous_cell.y);
    ivec2 side_cell_y = ivec2(previous_cell.x, current_cell.y);
    PixelDdaCellData side_data_x;
    PixelDdaCellData side_data_y;
    bool side_x_valid = load_pixel_dda_cell_data(side_cell_x,
                                                 boundary_ray_depth,
                                                 boundary_ray_depth,
                                                 depth_size,
                                                 side_data_x);
    bool side_y_valid = load_pixel_dda_cell_data(side_cell_y,
                                                 boundary_ray_depth,
                                                 boundary_ray_depth,
                                                 depth_size,
                                                 side_data_y);

    bool path_x_continuous =
        side_x_valid &&
        pixel_dda_cells_continuous(previous_data, side_data_x) &&
        pixel_dda_cells_continuous(side_data_x, current_data);
    bool path_y_continuous =
        side_y_valid &&
        pixel_dda_cells_continuous(previous_data, side_data_y) &&
        pixel_dda_cells_continuous(side_data_y, current_data);
    bool path_x_brackets =
        path_x_continuous &&
        (pixel_dda_boundary_pair_brackets(
             previous_data, side_data_x, boundary_ray_depth) ||
         pixel_dda_boundary_pair_brackets(
             side_data_x, current_data, boundary_ray_depth));
    bool path_y_brackets =
        path_y_continuous &&
        (pixel_dda_boundary_pair_brackets(
             previous_data, side_data_y, boundary_ray_depth) ||
         pixel_dda_boundary_pair_brackets(
             side_data_y, current_data, boundary_ray_depth));
    valid_crossing = path_x_brackets || path_y_brackets;
  }

  if (!valid_crossing) {
    return false;
  }

  // The two texel depths form a discrete staircase. Once continuity and a
  // signed bracket are proven, the shared edge/corner is the only stable
  // intersection parameter; projecting texel centers onto the ray created the
  // periodic gaps this bridge is intended to remove.
  return reconstruct_pixel_dda_hit(boundary_parameter,
                                   ray_start_pixel,
                                   ray_end_pixel,
                                   ray_start_q,
                                   ray_end_q,
                                   ray_start_k,
                                   ray_end_k,
                                   depth_size,
                                   hit_uv,
                                   hit_position_vs);
}

TraceResult trace_pixel_dda(vec3 ray_origin, vec3 ray_direction) {
  TraceResult result = make_miss(MISS_ITERATIONS);

  float ray_length = g_max_distance;
  if (ray_direction.z > 1e-6) {
    float near_distance =
        (-g_near_plane - ray_origin.z) / ray_direction.z;
    ray_length = min(ray_length, near_distance);
  } else if (ray_direction.z < -1e-6) {
    float far_distance =
        (-g_far_plane - ray_origin.z) / ray_direction.z;
    ray_length = min(ray_length, far_distance);
  }
  ray_length = max(ray_length - 1e-4, 0.0);
  if (ray_length <= 0.0) {
    return make_miss(MISS_DISTANCE);
  }

  vec3 ray_end_vs = ray_origin + ray_direction * ray_length;
  vec4 homogeneous_start = P * vec4(ray_origin, 1.0);
  vec4 homogeneous_end = P * vec4(ray_end_vs, 1.0);
  if (homogeneous_start.w <= 1e-6 || homogeneous_end.w <= 1e-6) {
    return make_miss(MISS_SCREEN);
  }

  float start_k = 1.0 / homogeneous_start.w;
  float end_k = 1.0 / homogeneous_end.w;
  vec3 start_q = ray_origin * start_k;
  vec3 end_q = ray_end_vs * end_k;

  ivec2 depth_size = textureSize(g_scene_depth_tex, 0);
  if (any(lessThanEqual(depth_size, ivec2(0)))) {
    return make_miss(MISS_SCREEN);
  }
  vec2 screen_size = vec2(depth_size);
  vec2 start_pixel =
      (homogeneous_start.xy * start_k * 0.5 + 0.5) * screen_size;
  vec2 end_pixel =
      (homogeneous_end.xy * end_k * 0.5 + 0.5) * screen_size;

  // Clip the projected segment instead of rejecting a ray merely because
  // its far endpoint is off-screen. P, Q and k share the same screen-line
  // parameter and therefore must be clipped together.
  vec2 original_start_pixel = start_pixel;
  vec2 original_end_pixel = end_pixel;
  vec3 original_start_q = start_q;
  vec3 original_end_q = end_q;
  float original_start_k = start_k;
  float original_end_k = end_k;
  vec2 pixel_delta = original_end_pixel - original_start_pixel;
  float enter_parameter = 0.0;
  float exit_parameter = 1.0;
  vec2 upper_bound = screen_size - vec2(1e-4);
  bool intersects_viewport =
      clip_pixel_line_axis(original_start_pixel.x,
                           pixel_delta.x,
                           0.0,
                           upper_bound.x,
                           enter_parameter,
                           exit_parameter) &&
      clip_pixel_line_axis(original_start_pixel.y,
                           pixel_delta.y,
                           0.0,
                           upper_bound.y,
                           enter_parameter,
                           exit_parameter);
  enter_parameter = clamp(enter_parameter, 0.0, 1.0);
  exit_parameter = clamp(exit_parameter, 0.0, 1.0);
  if (!intersects_viewport || exit_parameter <= enter_parameter) {
    return make_miss(MISS_SCREEN);
  }

  bool clipped_by_screen =
      enter_parameter > 1e-6 || exit_parameter < 1.0 - 1e-6;
  start_pixel = mix(
      original_start_pixel, original_end_pixel, enter_parameter);
  end_pixel = mix(
      original_start_pixel, original_end_pixel, exit_parameter);
  start_q = mix(original_start_q, original_end_q, enter_parameter);
  end_q = mix(original_start_q, original_end_q, exit_parameter);
  start_k = mix(original_start_k, original_end_k, enter_parameter);
  end_k = mix(original_start_k, original_end_k, exit_parameter);

  vec2 dda_delta = end_pixel - start_pixel;
  float projected_length = max(abs(dda_delta.x), abs(dda_delta.y));
  bool saw_depth_rejection = false;
  if (projected_length < 1e-5) {
    ivec2 pixel = clamp(ivec2(floor(start_pixel)),
                        ivec2(0),
                        depth_size - ivec2(1));
    bool depth_candidate;
    PixelDdaCellData cell_data;
    vec2 hit_uv;
    vec3 hit_position_vs;
    if (test_pixel_dda_cell(pixel,
                            start_pixel,
                            end_pixel,
                            start_q,
                            end_q,
                            start_k,
                            end_k,
                            depth_size,
                            depth_candidate,
                            cell_data,
                            hit_uv,
                            hit_position_vs)) {
      result.hit = true;
      result.uv = hit_uv;
      result.distance_vs = length(hit_position_vs - ray_origin);
      result.steps = 1;
      result.confidence = hit_confidence(hit_uv, result.distance_vs);
      result.miss_reason = MISS_NONE;
      return result;
    }
    result.steps = 1;
    result.miss_reason = depth_candidate
                             ? MISS_DEPTH_REJECTED
                             : (clipped_by_screen ? MISS_SCREEN
                                                  : MISS_DISTANCE);
    return result;
  }

  ivec2 cell_step = ivec2(0);
  if (dda_delta.x > 1e-8) {
    cell_step.x = 1;
  } else if (dda_delta.x < -1e-8) {
    cell_step.x = -1;
  }
  if (dda_delta.y > 1e-8) {
    cell_step.y = 1;
  } else if (dda_delta.y < -1e-8) {
    cell_step.y = -1;
  }

  ivec2 current_cell = ivec2(floor(start_pixel));
  current_cell =
      clamp(current_cell, ivec2(0), depth_size - ivec2(1));

  const float DDA_INFINITY = 1e30;
  float parameter_delta_x =
      cell_step.x == 0 ? DDA_INFINITY : 1.0 / abs(dda_delta.x);
  float parameter_delta_y =
      cell_step.y == 0 ? DDA_INFINITY : 1.0 / abs(dda_delta.y);
  float next_boundary_x =
      float(current_cell.x + (cell_step.x > 0 ? 1 : 0));
  float next_boundary_y =
      float(current_cell.y + (cell_step.y > 0 ? 1 : 0));
  float next_parameter_x =
      cell_step.x == 0
          ? DDA_INFINITY
          : (next_boundary_x - start_pixel.x) / dda_delta.x;
  float next_parameter_y =
      cell_step.y == 0
          ? DDA_INFINITY
          : (next_boundary_y - start_pixel.y) / dda_delta.y;
  next_parameter_x = max(next_parameter_x, 0.0);
  next_parameter_y = max(next_parameter_y, 0.0);

  // Resolve an exact start-on-boundary ownership transfer before counting
  // visited cells. The in-loop zero-progress branch remains as a numerical
  // safety net for later crossings.
  if (next_parameter_x <= 1e-7) {
    current_cell.x += cell_step.x;
    next_parameter_x += parameter_delta_x;
  }
  if (next_parameter_y <= 1e-7) {
    current_cell.y += cell_step.y;
    next_parameter_y += parameter_delta_y;
  }
  if (any(lessThan(current_cell, ivec2(0))) ||
      any(greaterThanEqual(current_cell, depth_size))) {
    result.miss_reason = MISS_SCREEN;
    return result;
  }

  float current_parameter = 0.0;
  bool has_previous_cell = false;
  ivec2 previous_cell = ivec2(0);
  PixelDdaCellData previous_cell_data;

  for (int iteration = 0; iteration < MAX_PIXEL_DDA_STEPS; ++iteration) {
    if (current_parameter >= 1.0 - 1e-6) {
      result.steps = iteration;
      result.miss_reason =
          saw_depth_rejection
              ? MISS_DEPTH_REJECTED
              : (clipped_by_screen ? MISS_SCREEN : MISS_DISTANCE);
      return result;
    }
    if (iteration >= g_max_steps) {
      result.steps = iteration;
      result.miss_reason = MISS_ITERATIONS;
      return result;
    }

    float next_parameter =
        min(1.0, min(next_parameter_x, next_parameter_y));
    if (next_parameter <= current_parameter + 1e-7) {
      bool cross_x = next_parameter_x <= current_parameter + 1e-7;
      bool cross_y = next_parameter_y <= current_parameter + 1e-7;
      if (cross_x) {
        current_cell.x += cell_step.x;
        next_parameter_x += parameter_delta_x;
      }
      if (cross_y) {
        current_cell.y += cell_step.y;
        next_parameter_y += parameter_delta_y;
      }
      if (any(lessThan(current_cell, ivec2(0))) ||
          any(greaterThanEqual(current_cell, depth_size))) {
        result.steps = iteration;
        result.miss_reason = MISS_SCREEN;
        return result;
      }
      continue;
    }

    vec2 segment_start_pixel =
        mix(start_pixel, end_pixel, current_parameter);
    vec2 segment_end_pixel =
        mix(start_pixel, end_pixel, next_parameter);
    vec3 segment_start_q = mix(start_q, end_q, current_parameter);
    vec3 segment_end_q = mix(start_q, end_q, next_parameter);
    float segment_start_k = mix(start_k, end_k, current_parameter);
    float segment_end_k = mix(start_k, end_k, next_parameter);

    bool depth_candidate;
    PixelDdaCellData cell_data;
    vec2 hit_uv;
    vec3 hit_position_vs;
    if (test_pixel_dda_cell(current_cell,
                            segment_start_pixel,
                            segment_end_pixel,
                            segment_start_q,
                            segment_end_q,
                            segment_start_k,
                            segment_end_k,
                            depth_size,
                            depth_candidate,
                            cell_data,
                            hit_uv,
                            hit_position_vs)) {
      result.hit = true;
      result.uv = hit_uv;
      result.distance_vs = length(hit_position_vs - ray_origin);
      result.steps = iteration + 1;
      result.confidence = hit_confidence(hit_uv, result.distance_vs);
      result.miss_reason = MISS_NONE;
      return result;
    }

    if (has_previous_cell && cell_data.valid &&
        test_pixel_dda_boundary(previous_cell_data,
                                cell_data,
                                previous_cell,
                                current_cell,
                                current_parameter,
                                start_pixel,
                                end_pixel,
                                start_q,
                                end_q,
                                start_k,
                                end_k,
                                depth_size,
                                hit_uv,
                                hit_position_vs)) {
      result.hit = true;
      result.uv = hit_uv;
      result.distance_vs = length(hit_position_vs - ray_origin);
      result.steps = iteration + 1;
      result.confidence = hit_confidence(hit_uv, result.distance_vs);
      result.miss_reason = MISS_NONE;
      return result;
    }
    saw_depth_rejection = saw_depth_rejection || depth_candidate;

    has_previous_cell = cell_data.valid;
    if (has_previous_cell) {
      previous_cell = current_cell;
      previous_cell_data = cell_data;
    }

    current_parameter = next_parameter;
    bool cross_x = next_parameter_x <= next_parameter + 1e-7;
    bool cross_y = next_parameter_y <= next_parameter + 1e-7;
    if (cross_x) {
      current_cell.x += cell_step.x;
      next_parameter_x += parameter_delta_x;
    }
    if (cross_y) {
      current_cell.y += cell_step.y;
      next_parameter_y += parameter_delta_y;
    }

    if (current_parameter < 1.0 - 1e-6 &&
        (any(lessThan(current_cell, ivec2(0))) ||
         any(greaterThanEqual(current_cell, depth_size)))) {
      result.steps = iteration + 1;
      result.miss_reason = MISS_SCREEN;
      return result;
    }
  }

  result.steps = MAX_PIXEL_DDA_STEPS;
  if (current_parameter >= 1.0 - 1e-6) {
    result.miss_reason =
        saw_depth_rejection
            ? MISS_DEPTH_REJECTED
            : (clipped_by_screen ? MISS_SCREEN : MISS_DISTANCE);
  } else {
    result.miss_reason = MISS_ITERATIONS;
  }
  return result;
}

float distance_to_cell_boundary(float position,
                                float direction,
                                int cell,
                                int cell_count) {
  if (abs(direction) < 1e-8) {
    return 1e30;
  }

  float boundary =
      float(cell + (direction > 0.0 ? 1 : 0)) / float(cell_count);
  float distance = (boundary - position) / direction;
  return max(distance, 0.0);
}

float hiz_leaf_entry_parameter(vec2 ray_start,
                               vec2 ray_delta,
                               ivec2 cell,
                               ivec2 cell_count,
                               out bool crossed_x,
                               out bool crossed_y) {
  float entry_x = -1e30;
  float entry_y = -1e30;
  float entry_parameter = 0.0;
  if (ray_delta.x > 1e-8) {
    float boundary = float(cell.x) / float(cell_count.x);
    entry_x = (boundary - ray_start.x) / ray_delta.x;
    entry_parameter = max(entry_parameter, entry_x);
  } else if (ray_delta.x < -1e-8) {
    float boundary = float(cell.x + 1) / float(cell_count.x);
    entry_x = (boundary - ray_start.x) / ray_delta.x;
    entry_parameter = max(entry_parameter, entry_x);
  }

  if (ray_delta.y > 1e-8) {
    float boundary = float(cell.y) / float(cell_count.y);
    entry_y = (boundary - ray_start.y) / ray_delta.y;
    entry_parameter = max(entry_parameter, entry_y);
  } else if (ray_delta.y < -1e-8) {
    float boundary = float(cell.y + 1) / float(cell_count.y);
    entry_y = (boundary - ray_start.y) / ray_delta.y;
    entry_parameter = max(entry_parameter, entry_y);
  }

  entry_parameter = clamp(entry_parameter, 0.0, 1.0);
  crossed_x = entry_parameter > 0.0 &&
              entry_x >= entry_parameter - 1e-7;
  crossed_y = entry_parameter > 0.0 &&
              entry_y >= entry_parameter - 1e-7;
  return entry_parameter;
}

bool test_hiz_leaf_entry_boundary(vec2 ray_start_uv,
                                  vec2 ray_delta_uv,
                                  vec2 ray_start_pixel,
                                  vec2 ray_end_pixel,
                                  vec3 ray_start_q,
                                  vec3 ray_end_q,
                                  float ray_start_k,
                                  float ray_end_k,
                                  ivec2 depth_size,
                                  float current_parameter,
                                  out vec2 hit_uv,
                                  out vec3 hit_position_vs) {
  hit_uv = vec2(0.0);
  hit_position_vs = vec3(0.0);

  vec2 current_uv =
      ray_start_uv + ray_delta_uv * current_parameter;
  ivec2 current_cell = clamp(
      ivec2(floor(current_uv * vec2(depth_size))),
      ivec2(0),
      depth_size - ivec2(1));
  bool crossed_x;
  bool crossed_y;
  float entry_parameter = hiz_leaf_entry_parameter(ray_start_uv,
                                                    ray_delta_uv,
                                                    current_cell,
                                                    depth_size,
                                                    crossed_x,
                                                    crossed_y);
  float max_pixel_delta = max(
      abs(ray_delta_uv.x) * float(depth_size.x),
      abs(ray_delta_uv.y) * float(depth_size.y));
  float ownership_tolerance =
      2e-3 / max(max_pixel_delta, 1.0) + 1e-7;
  if (entry_parameter <= 0.0 ||
      entry_parameter > current_parameter + 1e-6 ||
      current_parameter - entry_parameter > ownership_tolerance ||
      (!crossed_x && !crossed_y)) {
    return false;
  }

  ivec2 cell_step = ivec2(0);
  cell_step.x = ray_delta_uv.x > 1e-8
                    ? 1
                    : (ray_delta_uv.x < -1e-8 ? -1 : 0);
  cell_step.y = ray_delta_uv.y > 1e-8
                    ? 1
                    : (ray_delta_uv.y < -1e-8 ? -1 : 0);
  ivec2 previous_cell =
      current_cell - ivec2(crossed_x ? cell_step.x : 0,
                            crossed_y ? cell_step.y : 0);
  if (any(lessThan(previous_cell, ivec2(0))) ||
      any(greaterThanEqual(previous_cell, depth_size)) ||
      all(equal(previous_cell, current_cell))) {
    return false;
  }

  float boundary_ray_depth = pixel_dda_ray_depth(ray_start_q,
                                                 ray_end_q,
                                                 ray_start_k,
                                                 ray_end_k,
                                                 entry_parameter);
  PixelDdaCellData previous_cell_data;
  PixelDdaCellData current_cell_data;
  if (!load_pixel_dda_cell_data(previous_cell,
                                boundary_ray_depth,
                                boundary_ray_depth,
                                depth_size,
                                previous_cell_data) ||
      !load_pixel_dda_cell_data(current_cell,
                                boundary_ray_depth,
                                boundary_ray_depth,
                                depth_size,
                                current_cell_data)) {
    return false;
  }

  return test_pixel_dda_boundary(previous_cell_data,
                                 current_cell_data,
                                 previous_cell,
                                 current_cell,
                                 entry_parameter,
                                 ray_start_pixel,
                                 ray_end_pixel,
                                 ray_start_q,
                                 ray_end_q,
                                 ray_start_k,
                                 ray_end_k,
                                 depth_size,
                                 hit_uv,
                                 hit_position_vs);
}

TraceResult trace_hiz(vec3 ray_origin, vec3 ray_direction) {
  float ray_length = g_max_distance;
  if (ray_direction.z > 1e-6) {
    ray_length =
        min(ray_length,
            (-g_near_plane - ray_origin.z) / ray_direction.z);
  } else if (ray_direction.z < -1e-6) {
    ray_length =
        min(ray_length,
            (-g_far_plane - ray_origin.z) / ray_direction.z);
  }
  ray_length = max(ray_length - 1e-4, 0.0);

  if (ray_length <= 0.0) {
    return make_miss(MISS_DISTANCE);
  }

  vec3 ray_start;
  vec3 ray_end;
  if (!project_view_position(ray_origin, ray_start)) {
    // DDA clips the complete projected segment and may re-enter the viewport
    // even when the biased origin itself lies just outside it.
    return trace_pixel_dda(ray_origin, ray_direction);
  }

  vec4 start_clip = P * vec4(ray_origin, 1.0);
  if (start_clip.w <= 1e-6) {
    return trace_pixel_dda(ray_origin, ray_direction);
  }

  vec3 end_position_vs = ray_origin + ray_direction * ray_length;
  vec4 end_clip = P * vec4(end_position_vs, 1.0);
  if (end_clip.w <= 1e-6) {
    return trace_pixel_dda(ray_origin, ray_direction);
  }
  ray_end = (end_clip.xyz / end_clip.w) * 0.5 + 0.5;

  float ray_start_k = 1.0 / start_clip.w;
  float ray_end_k = 1.0 / end_clip.w;
  vec3 ray_start_q = ray_origin * ray_start_k;
  vec3 ray_end_q = end_position_vs * ray_end_k;

  ivec2 depth_size = textureSize(g_scene_depth_tex, 0);
  if (any(lessThanEqual(depth_size, ivec2(0)))) {
    return trace_pixel_dda(ray_origin, ray_direction);
  }
  vec2 depth_size_f = vec2(depth_size);
  vec2 ray_start_pixel = ray_start.xy * depth_size_f;
  vec2 ray_end_pixel = ray_end.xy * depth_size_f;

  vec3 ray_delta = ray_end - ray_start;
  vec2 pixel_delta = abs(ray_delta.xy * depth_size_f);
  float max_pixel_delta = max(pixel_delta.x, pixel_delta.y);
  if (max_pixel_delta < 1.0 ||
      ray_delta.z <= 1e-7) {
    // A min-only forward-Z pyramid cannot conservatively skip rays moving
    // toward the camera. Use the strict pixel DDA baseline for those rays.
    return trace_pixel_dda(ray_origin, ray_direction);
  }
  // Move only a tiny fraction of a projected pixel when crossing a cell
  // boundary. This guarantees progress without skipping an adjacent cell.
  float parameter_epsilon = 1e-4 / max(max_pixel_delta, 1.0);

  TraceResult result = make_miss(MISS_ITERATIONS);
  float ray_parameter = 0.0;
  int mip_level = 0;
  int max_mip_visited = 0;
  bool saw_depth_rejection = false;

  for (int iteration = 0; iteration < MAX_HIZ_STEPS; ++iteration) {
    if (iteration >= g_max_steps) {
      // Never pay for a complete second traversal when the hierarchy budget is
      // exhausted. The yellow miss explicitly reports that the chosen budget
      // was insufficient.
      result.miss_reason = MISS_ITERATIONS;
      result.steps = iteration;
      result.max_mip = max_mip_visited;
      return result;
    }

    vec3 current = ray_start + ray_delta * ray_parameter;
    if (!inside_screen(current.xy) || current.z < 0.0 ||
        current.z > 1.0 || ray_parameter > 1.0) {
      result.miss_reason =
          saw_depth_rejection ? MISS_DEPTH_REJECTED : MISS_SCREEN;
      result.steps = iteration;
      result.max_mip = max_mip_visited;
      return result;
    }

    mip_level = clamp(mip_level, 0, max(g_hiz_max_mip, 0));
    max_mip_visited = max(max_mip_visited, mip_level);
    ivec2 level_size = textureSize(g_hiz_tex, mip_level);
    ivec2 cell = clamp(ivec2(floor(current.xy * vec2(level_size))),
                       ivec2(0),
                       level_size - ivec2(1));
    float cell_depth = texelFetch(g_hiz_tex, cell, mip_level).r;

    float dx = distance_to_cell_boundary(
        current.x, ray_delta.x, cell.x, level_size.x);
    float dy = distance_to_cell_boundary(
        current.y, ray_delta.y, cell.y, level_size.y);
    float boundary_delta = min(dx, dy);
    if (boundary_delta >= 1e29) {
      boundary_delta = 1.0 - ray_parameter;
    }

    // floor() selects the positive-side cell at an exact boundary. Nudge the
    // ray in its travel direction, then re-fetch the cell and its depth.
    if (boundary_delta <= parameter_epsilon) {
      ray_parameter += parameter_epsilon;
      continue;
    }

    float exit_parameter =
        min(1.0, ray_parameter + boundary_delta);
    vec3 cell_exit = ray_start + ray_delta * exit_parameter;
    bool has_geometry = cell_depth < 0.999999;
    float conservative_cell_depth = cell_depth;
    if (has_geometry) {
      float scene_view_depth =
          -unproject_texture_position(vec3(current.xy, cell_depth)).z;
      float view_depth_epsilon =
          max(scene_view_depth * 1e-5, 1e-4);
      conservative_cell_depth = project_view_depth(
          max(scene_view_depth - view_depth_epsilon, g_near_plane));
    }
    bool potential_hit =
        has_geometry && cell_exit.z >= conservative_cell_depth - 1e-7;

    if (potential_hit && mip_level > 0) {
      --mip_level;
      continue;
    }

    if (potential_hit) {
      // At mip 0, validate only this leaf interval with the exact same Q/k
      // cell and continuity-boundary oracle used by Pixel DDA. The hierarchy
      // has already proven the prefix empty, so restarting DDA at the ray
      // origin would throw away all acceleration.
      bool leaf_crossed_x;
      bool leaf_crossed_y;
      float leaf_entry_parameter = min(
          hiz_leaf_entry_parameter(ray_start.xy,
                                   ray_delta.xy,
                                   cell,
                                   depth_size,
                                   leaf_crossed_x,
                                   leaf_crossed_y),
          exit_parameter);
      vec2 segment_start_pixel = mix(
          ray_start_pixel, ray_end_pixel, leaf_entry_parameter);
      vec2 segment_end_pixel = mix(
          ray_start_pixel, ray_end_pixel, exit_parameter);
      vec3 segment_start_q = mix(
          ray_start_q, ray_end_q, leaf_entry_parameter);
      vec3 segment_end_q = mix(
          ray_start_q, ray_end_q, exit_parameter);
      float segment_start_k = mix(
          ray_start_k, ray_end_k, leaf_entry_parameter);
      float segment_end_k = mix(
          ray_start_k, ray_end_k, exit_parameter);

      bool depth_candidate;
      PixelDdaCellData cell_data;
      vec2 hit_uv;
      vec3 hit_position_vs;
      if (test_pixel_dda_cell(cell,
                              segment_start_pixel,
                              segment_end_pixel,
                              segment_start_q,
                              segment_end_q,
                              segment_start_k,
                              segment_end_k,
                              depth_size,
                              depth_candidate,
                              cell_data,
                              hit_uv,
                              hit_position_vs)) {
        result.hit = true;
        result.uv = hit_uv;
        result.distance_vs = length(hit_position_vs - ray_origin);
        result.steps = iteration + 1;
        result.confidence = hit_confidence(hit_uv, result.distance_vs);
        result.miss_reason = MISS_NONE;
        result.max_mip = max_mip_visited;
        return result;
      }

      // A discrete depth step may put the zero crossing exactly on the leaf
      // entry. Derive the crossed axes analytically so near-corner rays never
      // skip a very short intermediate cell.
      if (test_hiz_leaf_entry_boundary(ray_start.xy,
                                       ray_delta.xy,
                                       ray_start_pixel,
                                       ray_end_pixel,
                                       ray_start_q,
                                       ray_end_q,
                                       ray_start_k,
                                       ray_end_k,
                                       depth_size,
                                       ray_parameter,
                                       hit_uv,
                                       hit_position_vs)) {
        result.hit = true;
        result.uv = hit_uv;
        result.distance_vs = length(hit_position_vs - ray_origin);
        result.steps = iteration + 1;
        result.confidence = hit_confidence(hit_uv, result.distance_vs);
        result.miss_reason = MISS_NONE;
        result.max_mip = max_mip_visited;
        return result;
      }

      saw_depth_rejection = saw_depth_rejection || depth_candidate;
      ray_parameter = exit_parameter + parameter_epsilon;
      if (ray_parameter >= 1.0) {
        result.miss_reason =
            saw_depth_rejection ? MISS_DEPTH_REJECTED : MISS_DISTANCE;
        result.steps = iteration + 1;
        result.max_mip = max_mip_visited;
        return result;
      }
      mip_level = min(1, max(g_hiz_max_mip, 0));
      continue;
    }

    // Even when a coarse cell is proven empty, DDA may accept a continuity
    // crossing exactly at its entry from the preceding leaf. Validate that one
    // boundary before skipping the proven-empty interval; internal boundaries
    // cannot cross because the min depth proves the ray is in front of every
    // leaf contained by this cell.
    vec2 boundary_hit_uv;
    vec3 boundary_hit_position_vs;
    if (test_hiz_leaf_entry_boundary(ray_start.xy,
                                     ray_delta.xy,
                                     ray_start_pixel,
                                     ray_end_pixel,
                                     ray_start_q,
                                     ray_end_q,
                                     ray_start_k,
                                     ray_end_k,
                                     depth_size,
                                     ray_parameter,
                                     boundary_hit_uv,
                                     boundary_hit_position_vs)) {
      result.hit = true;
      result.uv = boundary_hit_uv;
      result.distance_vs =
          length(boundary_hit_position_vs - ray_origin);
      result.steps = iteration + 1;
      result.confidence =
          hit_confidence(boundary_hit_uv, result.distance_vs);
      result.miss_reason = MISS_NONE;
      result.max_mip = max_mip_visited;
      return result;
    }

    ray_parameter = exit_parameter + parameter_epsilon;
    mip_level = min(mip_level + 1, max(g_hiz_max_mip, 0));

    if (ray_parameter >= 1.0) {
      result.miss_reason =
          saw_depth_rejection ? MISS_DEPTH_REJECTED : MISS_DISTANCE;
      result.steps = iteration + 1;
      result.max_mip = max_mip_visited;
      return result;
    }
  }

  result.miss_reason = MISS_ITERATIONS;
  result.steps = MAX_HIZ_STEPS;
  result.max_mip = max_mip_visited;
  return result;
}

vec3 miss_reason_color(int reason) {
  if (reason == MISS_SCREEN) {
    return vec3(1.0, 0.1, 0.1);
  }
  if (reason == MISS_DISTANCE) {
    return vec3(1.0, 0.45, 0.0);
  }
  if (reason == MISS_ITERATIONS) {
    return vec3(1.0, 1.0, 0.0);
  }
  if (reason == MISS_DEPTH_REJECTED) {
    return vec3(1.0, 0.0, 1.0);
  }
  return vec3(0.05);
}

void main() {
  vec2 screen_size = vec2(max(g_width, 1), max(g_height, 1));
  vec2 screen_uv = gl_FragCoord.xy / screen_size;

  vec2 distortion = vec2(0.0);
  vec3 distorted_normal = safe_normalize(normal_vs);
  if (g_flat_water == 0) {
    float wave_sample1 =
        texture(g_wave_tex, wave_uv_vs.xy).r * 2.0 - 1.0;
    float wave_sample2 =
        texture(g_wave_tex, wave_uv_vs.zw).r * 2.0 - 1.0;
    distortion =
        vec2(wave_sample1 + wave_sample2 * 0.5,
             wave_sample2 - wave_sample1 * 0.5) *
        g_wave_strength;
    distorted_normal =
        safe_normalize(normal_vs + tangent_vs * distortion.x +
                       bitangent_vs * distortion.y);
  }

  vec3 incident = safe_normalize(position_vs);
  vec3 ray_direction = safe_normalize(reflect(incident, distorted_normal));
  vec3 ray_origin =
      position_vs + distorted_normal * g_origin_bias +
      ray_direction * g_origin_bias;

  TraceResult trace = make_miss(MISS_DISABLED);
  if (g_ssr_mode == SSR_MODE_FIXED) {
    trace = trace_fixed_step(ray_origin, ray_direction);
  } else if (g_ssr_mode == SSR_MODE_PIXEL_DDA) {
    trace = trace_pixel_dda(ray_origin, ray_direction);
  } else if (g_ssr_mode == SSR_MODE_HIZ) {
    trace = trace_hiz(ray_origin, ray_direction);
  }

  vec2 refraction_uv =
      screen_uv + distortion * g_refraction_distortion_strength;
  if (!inside_screen(refraction_uv)) {
    refraction_uv = screen_uv;
  }

  float refracted_raw_depth;
  vec3 refracted_scene_position;
  if (sample_scene_view_position(
          refraction_uv, refracted_raw_depth, refracted_scene_position) &&
      refracted_scene_position.z > position_vs.z) {
    // The distorted lookup moved onto foreground geometry. Use the
    // undistorted coordinate instead of pulling foreground color into water.
    refraction_uv = screen_uv;
  }

  vec3 refracted_scene_color =
      texture(g_scene_color_tex, refraction_uv).rgb;

  float scene_raw_depth;
  vec3 scene_position;
  float water_depth = g_max_water_depth;
  if (sample_scene_view_position(
          screen_uv, scene_raw_depth, scene_position)) {
    water_depth =
        clamp(position_vs.z - scene_position.z, 0.0, g_max_water_depth);
  }
  vec3 transmittance =
      exp(-max(g_absorption.rgb, vec3(0.0)) *
          g_absorption_strength * water_depth);
  vec3 refraction = refracted_scene_color * transmittance;

  vec3 ssr_color = g_env_radiance.rgb;
  if (trace.hit) {
    ssr_color = texture(g_scene_color_tex, trace.uv).rgb;
  }
  vec3 reflection =
      mix(g_env_radiance.rgb,
          ssr_color,
          trace.hit ? trace.confidence : 0.0);

  vec3 view_direction = safe_normalize(-position_vs);
  vec3 light_direction = safe_normalize(g_light_dir_vs.xyz);
  vec3 half_direction =
      safe_normalize(view_direction + light_direction);
  float specular = pow(max(dot(distorted_normal, half_direction), 0.0), 96.0);
  reflection +=
      g_light_radiance.rgb * specular * g_specular_strength;

  float n_dot_v = clamp(dot(distorted_normal, view_direction), 0.0, 1.0);
  float fresnel =
      g_fresnel_f0 +
      (1.0 - g_fresnel_f0) * pow(1.0 - n_dot_v, 5.0);
  vec3 final_color =
      refraction * (1.0 - fresnel) + reflection * fresnel;

  if (g_debug_view == DEBUG_SSR_ONLY) {
    final_color = trace.hit ? ssr_color * trace.confidence
                            : g_env_radiance.rgb;
  } else if (g_debug_view == DEBUG_LINEAR_DEPTH) {
    float linear_depth =
        scene_raw_depth < 0.999999 ? -scene_position.z : g_far_plane;
    float depth_visualization =
        clamp(linear_depth / max(g_far_plane, 1e-4), 0.0, 1.0);
    final_color = vec3(depth_visualization);
  } else if (g_debug_view == DEBUG_HIT_MASK) {
    final_color =
        trace.hit ? vec3(trace.confidence)
                  : miss_reason_color(trace.miss_reason);
  } else if (g_debug_view == DEBUG_HIT_UV) {
    final_color = trace.hit ? vec3(trace.uv, 0.0) : vec3(0.0);
  } else if (g_debug_view == DEBUG_ITERATIONS) {
    float iteration_denominator = float(max(g_max_steps, 1));
    if (g_ssr_mode == SSR_MODE_FIXED) {
      float required_fixed_steps =
          ceil(max(g_max_distance, 0.0) / max(g_step_size, 1e-4));
      iteration_denominator =
          max(1.0, min(iteration_denominator, required_fixed_steps));
    }
    float iteration_ratio =
        clamp(float(trace.steps) / iteration_denominator, 0.0, 1.0);
    float mip_ratio =
        float(trace.max_mip) / float(max(g_hiz_max_mip, 1));
    final_color = vec3(iteration_ratio, mip_ratio, 0.0);
  }

  frag_color_out = vec4(max(final_color, vec3(0.0)), 1.0);
}
