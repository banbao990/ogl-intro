#include "mesh.hpp"

#include <array>
#include <vector>

#include <imgui/imgui.h>

BoltMesh::BoltMesh() {
  glGenVertexArrays(1, &_vao);
  glBindVertexArray(_vao);
  glGenBuffers(1, &_vertex_buffer);

  generate_bolts();

  glVertexAttribPointer(
      0, 2, GL_FLOAT, GL_FALSE, sizeof(Point), (void *)offsetof(Point, x));
  glEnableVertexAttribArray(0);
}

BoltMesh::~BoltMesh() {
  glDeleteBuffers(1, &_vertex_buffer);
  glDeleteVertexArrays(1, &_vao);
}

void BoltMesh::draw_ui() {
  bool should_gen_bolt = false;
  should_gen_bolt |= ImGui::Button("Update Bolts");
  // choose bolt types
  should_gen_bolt |=
      ImGui::Combo("Bolt Type", (int *)&_bolt_type, "Original\0Real\0");

  should_gen_bolt |= ImGui::SliderInt("Bolt Depth", &_max_iterations, 1, 10);
  should_gen_bolt |= ImGui::SliderFloat(
      "Branch Probability", &_branch_probability, 0.0f, 1.0f);

  if (should_gen_bolt) {
    generate_bolts();
  }
}

void BoltMesh::draw() {
  glBindVertexArray(_vao);
  glDrawArrays(GL_LINES, 0, _vertices.size());
}

float BoltMesh::get_random_float(float minVal, float maxVal) {
  return minVal + static_cast<float>(rand()) /
                      (static_cast<float>(RAND_MAX / (maxVal - minVal)));
}

void BoltMesh::random_start_end() {
  // random generate start and end points
  // range [-1, 1]^2 - [-0.5, 0.5]^2

  // [STEP#1] [-0.5, 0.5]^2
  Point start = {get_random_float(-0.5, 0.5), get_random_float(-0.5, 0.5)};
  Point end = {get_random_float(-0.5, 0.5), get_random_float(-0.5, 0.5)};

  // [STEP#2] make the bolt bigger
  // if lr = true, then the start point and the end point must have opposite
  // sign of x else opposite sign of y
  bool lr = rand() % 2;
  if (lr) {
    if (start.x * end.x > 0) {
      if (start.x > 0) {
        start.x = -start.x;
      } else {
        end.x = -end.x;
      }
    }
  } else {
    if (start.y * end.y > 0) {
      if (start.y > 0) {
        start.y = -start.y;
      } else {
        end.y = -end.y;
      }
    }
  }

  // [STEP#3] [-0.5, 0.5]^2 => [-1, 1]^2
  start.x += 0.5 * (start.x > 0 ? 1 : -1);
  start.y += 0.5 * (start.y > 0 ? 1 : -1);
  end.x += 0.5 * (end.x > 0 ? 1 : -1);
  end.y += 0.5 * (end.y > 0 ? 1 : -1);

  _vertices.push_back(start);
  _vertices.push_back(end);
}

void BoltMesh::random_segments() {
  // random number between 1 - 10
  int num_segments = 1 + rand() % 10;
  for (int i = 0; i < num_segments; i++) {
    // random start and end points, range [-1, 1]
    Point start = {get_random_float(), get_random_float()};
    Point end = {get_random_float(), get_random_float()};
    _vertices.push_back(start);
    _vertices.push_back(end);
  }
}

void BoltMesh::bolt1() {
  // start and end points
  // random_start_end();
  const float c_se_scale = 0.8f;
  const Point c_boltEnd = {c_se_scale, -c_se_scale};
  _vertices.push_back({-c_se_scale, c_se_scale});
  _vertices.push_back(c_boltEnd);

  // generate a bolt
  float max_offset = 0.4f;
  const float c_max_branch_angle = 1.57f;
  const float c_branch_length_scale = 0.7f;
  const float c_branch_length_scale_for_real_bolt = 0.3f;
  const uint32_t c_max_depth_for_real_bolt = 2u;

  std::vector<Point> new_vertices;
  new_vertices.clear();
  std::array<std::vector<Point>, 2> arr = {_vertices, new_vertices};

  for (int iter = 0; iter < _max_iterations; ++iter) {
    auto &now = arr[iter % 2];
    auto &next = arr[(iter + 1) % 2];

    uint32_t nowLength = now.size() / 2;
    for (uint32_t pointIndex = 0; pointIndex < nowLength; ++pointIndex) {
      const Point &s = now[pointIndex * 2];
      const Point &e = now[pointIndex * 2 + 1];
      // L system
      Point mid = {(s.x + e.x) / 2, (s.y + e.y) / 2};
      // random offset along the normal.
      const float offset = get_random_float(-max_offset, max_offset);
      // normalized normal
      Line normal = {s.y - e.y, e.x - s.x};
      float length = sqrt(normal.x * normal.x + normal.y * normal.y);
      normal.x /= length;
      normal.y /= length;
      mid.x += offset * normal.x;
      mid.y += offset * normal.y;

      next.push_back(s);
      next.push_back(mid);
      next.push_back(mid);
      next.push_back(e);

      // branch
      if (iter > 1 && get_random_float(0.0, 1.0) > _branch_probability) {
        continue;
      }
      Line dir = {mid.x - s.x, mid.y - s.y};
      float branchAngle =
          get_random_float(-c_max_branch_angle, c_max_branch_angle);
      Point branchEnd = {mid.x, mid.y};
      float sinVal = sin(branchAngle);
      float cosVal = cos(branchAngle);

      float branchLengthScale = c_branch_length_scale;
      if (iter <= c_max_depth_for_real_bolt && _bolt_type == REAL) {
        Line dis2end = {c_boltEnd.x - mid.x, c_boltEnd.y - mid.y};
        float length = sqrt(dis2end.x * dis2end.x + dis2end.y * dis2end.y);
        branchLengthScale = length * c_branch_length_scale_for_real_bolt;
        float dirLength = sqrt(dir.x * dir.x + dir.y * dir.y);
        branchLengthScale *= 1.414 / dirLength;
      }

      branchEnd.x += branchLengthScale * (dir.x * cosVal - dir.y * sinVal);
      branchEnd.y += branchLengthScale * (dir.x * sinVal + dir.y * cosVal);
      next.push_back(mid);
      next.push_back(branchEnd);
    }

    max_offset /= 2;
    now.clear();
  }

  _vertices = arr[(_max_iterations % 2) ? 1 : 0];
}

void BoltMesh::generate_bolts() {
  _vertices.clear();

  // random_segments();
  bolt1();

  glBindBuffer(GL_ARRAY_BUFFER, _vertex_buffer);
  glBufferData(GL_ARRAY_BUFFER,
               _vertices.size() * sizeof(Point),
               _vertices.data(),
               GL_DYNAMIC_DRAW);
}
