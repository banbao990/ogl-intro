add_executable(lightning_bolt main.cpp)
target_compile_features(lightning_bolt PRIVATE cxx_std_17)

target_link_libraries(lightning_bolt PRIVATE glfw glew_s imgui tinygltf)
target_include_directories(lightning_bolt PRIVATE ${CMAKE_SOURCE_DIR}/third_party/glew/include)