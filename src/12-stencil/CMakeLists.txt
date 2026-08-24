add_executable(stencil main.cpp material.hpp material.cpp)

target_link_libraries(stencil PRIVATE common)
