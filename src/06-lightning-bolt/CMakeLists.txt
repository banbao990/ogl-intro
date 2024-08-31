add_executable(lightning_bolt 
    main.cpp 
    "material.hpp"
    "material.cpp"
    "mesh.hpp"
    "mesh.cpp"
)
target_compile_features(lightning_bolt PRIVATE cxx_std_17)

target_link_libraries(lightning_bolt PRIVATE common)
target_include_directories(lightning_bolt PRIVATE ${CMAKE_SOURCE_DIR}/third_party/glew/include)
target_include_directories(lightning_bolt PRIVATE ${CMAKE_SOURCE_DIR}/src)