set(pro_name 07_julia_set)

add_executable(${pro_name}
    main.cpp
    "material.hpp"
    "material.cpp"
)
target_compile_features(${pro_name} PRIVATE cxx_std_17)

target_link_libraries(${pro_name} PRIVATE common)
target_include_directories(${pro_name} PRIVATE ${CMAKE_SOURCE_DIR}/third_party/glew/include)
target_include_directories(${pro_name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
