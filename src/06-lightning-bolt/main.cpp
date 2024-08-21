#include <GL/glew.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <iostream>
#include <optional>
#include <vector>
#include <array>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>
#include <filesystem>
#include <stb_image_write.h>

namespace fs = std::filesystem;

static const char *vertex_shader_text = R"(
#version 330 core

layout(location = 0) in vec2 position;

void main() {
  gl_Position = vec4(position, 0.0, 1.0);
}
)";

static const char *fragment_shader_text = R"(
#version 330 core

layout(location = 0) out vec4 color;

void main() {
  // all white
  color = vec4(1.0, 1.0, 1.0, 1.0);
}
)";

struct Point {
  float x, y;
};
using Line = Point;

// original:
// https://link.zhihu.com/?target=http%3A//drilian.com/2009/02/25/lightning-bolts/
// real: https://zhuanlan.zhihu.com/p/111904859
enum BoltType { Original, REAL, NUM_BOLT_TYPES };

static GLFWwindow *s_window = nullptr;
static GLuint s_vertexBuffer = (GLuint)0u;
static std::vector<Point> s_vertices;
static enum BoltType s_boltType = REAL;
static int s_maxIterations = 5;
static float s_branchProbability = 0.3f;
static bool s_screenShot = false;

std::optional<GLuint> compile_shader(const char *text, GLenum type) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &text, NULL);
  glCompileShader(shader);

  GLint is_compiled = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &is_compiled);
  if (is_compiled == GL_FALSE) {
    GLint max_length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &max_length);

    // The maxLength includes the NULL character
    std::vector<GLchar> error_log(max_length);
    glGetShaderInfoLog(shader, max_length, &max_length, &error_log[0]);

    std::cerr << error_log.data() << std::endl;

    glDeleteShader(shader); // Don't leak the shader.
    return std::nullopt;
  }
  return shader;
}

std::optional<GLuint> link_program(GLuint *shaders, uint32_t shader_count) {
  GLuint program = glCreateProgram();
  for (uint32_t i = 0; i < shader_count; i++) {
    glAttachShader(program, shaders[i]);
  }
  glLinkProgram(program);

  int success;
  // check for linking errors
  glGetProgramiv(program, GL_LINK_STATUS, &success);
  if (!success) {
    GLint max_length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &max_length);
    std::vector<GLchar> error_log(max_length);
    glGetProgramInfoLog(program, max_length, &max_length, &error_log[0]);

    std::cout << error_log.data() << std::endl;
    glDeleteProgram(program);
    return std::nullopt;
  }
  return program;
}

static const std::string current_data_time() {
  time_t now = time(0);
  struct tm tstruct;
  char buf[80];
  tstruct = *localtime(&now);
  strftime(buf, sizeof(buf), "%Y-%m-%d-%X", &tstruct);

  return buf;
}

static const std::string get_screen_shot_filename() {
  auto t = current_data_time();
  for (auto &c : t) {
    if (c == ':') {
      c = '-';
    }
  }
  return "ogl-screen-shot-" + t + ".png";
}

void screen_shot() {
  int width, height;
  glfwGetFramebufferSize(s_window, &width, &height);
  std::vector<uint8_t> data;
  glFlush();
  glReadBuffer(GL_BACK);
  data.resize(width * height * 4);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, data.data());

  auto output_path = fs::absolute(get_screen_shot_filename()).string();

  stbi_flip_vertically_on_write(true);
  stbi_write_png(output_path.c_str(), width, height, 4, data.data(), 0);
  std::cout << "screen shot written to " << output_path << std::endl;
}

GLFWwindow *init() {
  if (glfwInit() == GLFW_FALSE) {
    throw std::runtime_error("failed to init glfw");
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__ // for macos
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
  GLFWwindow *window = glfwCreateWindow(800, 600, "Hello", nullptr, nullptr);
  if (!window) {
    throw std::runtime_error("failed to create window");
  }
  glfwMakeContextCurrent(window);

  GLenum e = glewInit();
  if (e != GLEW_OK) {
    glfwDestroyWindow(window);
    std::cerr << glewGetErrorString(e) << std::endl;
    throw std::runtime_error("failed to init glew");
  }

  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard
  ImGui::StyleColorsDark();

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 130");

  s_window = window;
  return window;
}

float getRandomFloat(float minVal = -1.0, float maxVal = 1.0) {
  return minVal + static_cast<float>(rand()) /
                      (static_cast<float>(RAND_MAX / (maxVal - minVal)));
}

void randomStartEnd() {
  // random generate start and end points
  // range [-1, 1]^2 - [-0.5, 0.5]^2

  // [STEP#1] [-0.5, 0.5]^2
  Point start = {getRandomFloat(-0.5, 0.5), getRandomFloat(-0.5, 0.5)};
  Point end = {getRandomFloat(-0.5, 0.5), getRandomFloat(-0.5, 0.5)};

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

  s_vertices.push_back(start);
  s_vertices.push_back(end);
}

void randomSegments() {
  // random number between 1 - 10
  int num_segments = 1 + rand() % 10;
  for (int i = 0; i < num_segments; i++) {
    // random start and end points, range [-1, 1]
    Point start = {getRandomFloat(), getRandomFloat()};
    Point end = {getRandomFloat(), getRandomFloat()};
    s_vertices.push_back(start);
    s_vertices.push_back(end);
  }
}

void bolt1() {
  // start and end points
  // randomStartEnd();
  const float c_seScale = 0.8f;
  const Point c_boltEnd = {c_seScale, -c_seScale};
  s_vertices.push_back({-c_seScale, c_seScale});
  s_vertices.push_back(c_boltEnd);

  // generate a bolt
  float maxOffset = 0.4f;
  const float c_maxBranchAngle = 1.57f;
  const float c_branchLengthScale = 0.7f;
  const float c_branchLengthScaleForRealBolt = 0.3f;
  const uint32_t c_maxDepthForRealBolt = 2u;

  std::vector<Point> newVertices;
  newVertices.clear();
  std::array<std::vector<Point>, 2> arr = {s_vertices, newVertices};

  for (int iter = 0; iter < s_maxIterations; ++iter) {
    auto &now = arr[iter % 2];
    auto &next = arr[(iter + 1) % 2];

    uint32_t nowLength = now.size() / 2;
    for (uint32_t pointIndex = 0; pointIndex < nowLength; ++pointIndex) {
      const Point &s = now[pointIndex * 2];
      const Point &e = now[pointIndex * 2 + 1];
      // L system
      Point mid = {(s.x + e.x) / 2, (s.y + e.y) / 2};
      // random offset along the normal.
      const float offset = getRandomFloat(-maxOffset, maxOffset);
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
      if (iter > 1 && getRandomFloat(0.0, 1.0) > s_branchProbability) {
        continue;
      }
      Line dir = {mid.x - s.x, mid.y - s.y};
      float branchAngle = getRandomFloat(-c_maxBranchAngle, c_maxBranchAngle);
      Point branchEnd = {mid.x, mid.y};
      float sinVal = sin(branchAngle);
      float cosVal = cos(branchAngle);

      float branchLengthScale = c_branchLengthScale;
      if (iter <= c_maxDepthForRealBolt && s_boltType == REAL) {
        Line dis2end = {c_boltEnd.x - mid.x, c_boltEnd.y - mid.y};
        float length = sqrt(dis2end.x * dis2end.x + dis2end.y * dis2end.y);
        branchLengthScale = length * c_branchLengthScaleForRealBolt;
        float dirLength = sqrt(dir.x * dir.x + dir.y * dir.y);
        branchLengthScale *= 1.414 / dirLength;
      }

      branchEnd.x += branchLengthScale * (dir.x * cosVal - dir.y * sinVal);
      branchEnd.y += branchLengthScale * (dir.x * sinVal + dir.y * cosVal);
      next.push_back(mid);
      next.push_back(branchEnd);
    }

    maxOffset /= 2;
    now.clear();
  }

  s_vertices = arr[(s_maxIterations % 2) ? 1 : 0];
}

void generateBolts() {
  s_vertices.clear();

  // randomSegments();
  bolt1();

  glBindBuffer(GL_ARRAY_BUFFER, s_vertexBuffer);
  glBufferData(GL_ARRAY_BUFFER,
               s_vertices.size() * sizeof(Point),
               s_vertices.data(),
               GL_DYNAMIC_DRAW);
}

float getAverageFPS() {
  static float fps = 0.0f;
  static float avg_fps = 0.0f;
  static float avg_count = 0.0f;
  static float avg_time = 0.0f;
  static float last_time = 0.0f;
  avg_count += 1.0f;
  float current_time = glfwGetTime();
  float delta = current_time - last_time;
  last_time = current_time;
  avg_time += delta;
  if (avg_time > 1.0f) {
    avg_fps = avg_count / avg_time;
    avg_count = 0.0f;
    avg_time = 0.0f;
  }

  return avg_fps;
}

void drawUI() {
  ImGui::Begin("Lightning Bolt!");

  // show FPS
  float fps = getAverageFPS();
  ImGui::Text("Average FPS: %.2f", fps);

  bool shouldGenBolt = false;
  shouldGenBolt |= ImGui::Button("Update Bolts");
  // choose bolt types
  shouldGenBolt |=
      ImGui::Combo("Bolt Type", (int *)&s_boltType, "Original\0Real\0");

  shouldGenBolt |= ImGui::SliderInt("Bolt Depth", &s_maxIterations, 1, 10);
  shouldGenBolt |= ImGui::SliderFloat(
      "Branch Probability", &s_branchProbability, 0.0f, 1.0f);

  if (shouldGenBolt) {
    generateBolts();
  }

  s_screenShot = ImGui::Button("Screen Shot");

  ImGui::End();
}

int main() {
  GLFWwindow *window = init();
  GLuint vertex_shader =
      compile_shader(vertex_shader_text, GL_VERTEX_SHADER).value();
  GLuint fragment_shader =
      compile_shader(fragment_shader_text, GL_FRAGMENT_SHADER).value();

  GLuint shaders[] = {vertex_shader, fragment_shader};
  GLuint program = link_program(shaders, 2).value();

  GLuint vao;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);

  glGenBuffers(1, &s_vertexBuffer);
  generateBolts();

  glVertexAttribPointer(
      0, 2, GL_FLOAT, GL_FALSE, sizeof(Point), (void *)offsetof(Point, x));
  glEnableVertexAttribArray(0);

  glClearColor(0.0, 0.0, 0.0, 1.0);
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    glfwSwapBuffers(window);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(program);
    glBindVertexArray(vao);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glLineWidth(2.0f);
    glDrawArrays(GL_LINES, 0, s_vertices.size());

    drawUI();

    if (s_screenShot) {
      screen_shot();
      s_screenShot = false;
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  }

  glDeleteBuffers(1, &s_vertexBuffer);
  glDeleteVertexArrays(1, &vao);

  ImGui_ImplOpenGL3_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();
}