set(pro_name 09_kernel_estimation)

add_executable(${pro_name}
    main.cpp
    "material.cpp"
    "material.hpp"
    "../07-fractal/material.hpp"
    "../07-fractal/material.cpp"
)

target_compile_features(${pro_name} PRIVATE cxx_std_17)
target_link_libraries(${pro_name} PRIVATE common)
target_include_directories(${pro_name} PRIVATE ${CMAKE_SOURCE_DIR}/third_party/glew/include)
target_include_directories(${pro_name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
