add_executable(julia_set 
    main.cpp 
    "material.hpp"
    "material.cpp"
)
target_compile_features(julia_set PRIVATE cxx_std_17)

target_link_libraries(julia_set PRIVATE common)
target_include_directories(julia_set PRIVATE ${CMAKE_SOURCE_DIR}/third_party/glew/include)
target_include_directories(julia_set PRIVATE ${CMAKE_SOURCE_DIR}/src)