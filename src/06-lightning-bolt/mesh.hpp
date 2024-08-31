#pragma once
#include <vector>
#include <GL/glew.h>

// original:
// https://link.zhihu.com/?target=http%3A//drilian.com/2009/02/25/lightning-bolts/
// real: https://zhuanlan.zhihu.com/p/111904859

enum BoltType { Original, REAL, NUM_BOLT_TYPES };

struct Point {
  float x, y;
};
using Line = Point;

class BoltMesh {

public:
  BoltMesh();

  virtual ~BoltMesh();

  void draw_ui();

  void draw();

  float get_random_float(float minVal = -1.0, float maxVal = 1.0);

  void random_start_end();

  void random_segments();

  void bolt1();

  void generate_bolts();

private:
  GLuint _vao{0u};
  GLuint _vertex_buffer{0u};
  std::vector<Point> _vertices;

  // bolt attributes
  enum BoltType _bolt_type = REAL;
  float _branch_probability = 0.3f;
  int _max_iterations = 5;
};
