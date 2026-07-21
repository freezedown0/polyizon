# Game Executable Target

add_executable(polyizon)

# Include directories
target_include_directories(polyizon
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Source files
target_sources(polyizon
    PRIVATE
        src/main.cpp

        # Not yet implemented
        # src/game.cpp
        # src/player.cpp
        # src/world.cpp
        # src/systems.cpp
)

# Dependencies
target_link_libraries(polyizon
    PRIVATE
        polyizon_core
        glfw
        Vulkan::Vulkan
)

# C++ standard
target_compile_features(polyizon PRIVATE cxx_std_20)

# Set output directory
set_target_properties(polyizon PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
