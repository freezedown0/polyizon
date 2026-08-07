add_executable(polyizon_scene_tests
    scene_authoring_tests.cpp
)

target_link_libraries(polyizon_scene_tests PRIVATE polyizon_core)
target_compile_features(polyizon_scene_tests PRIVATE cxx_std_20)

add_test(NAME polyizon.scene_authoring COMMAND polyizon_scene_tests)

add_executable(polyizon_render_graph_tests
    render_graph_tests.cpp
)
target_link_libraries(polyizon_render_graph_tests PRIVATE polyizon_core)
target_compile_features(polyizon_render_graph_tests PRIVATE cxx_std_20)
add_test(NAME polyizon.render_graph COMMAND polyizon_render_graph_tests)
