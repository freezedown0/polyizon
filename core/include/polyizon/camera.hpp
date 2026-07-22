#pragma once

#include <glm/glm.hpp>

struct GLFWwindow;

namespace polyizon {

// Classic FPS-style free-fly camera: position + yaw/pitch orientation.
// Movement is driven by continuous per-frame glfwGetKey() polling
// (ProcessKeyboard); look is driven by raw mouse-delta injection
// (ProcessMouseMovement). Owned directly by Application — no
// ECS/transform-component involvement yet.
class Camera {
public:
    // yawDegrees/pitchDegrees follow the standard yaw=0 -> +X, right-handed,
    // Y-up convention; yawDegrees=-90 (the default) points the front vector
    // down -Z, matching the previous hardcoded eye=(0,0,2) lookAt(origin).
    explicit Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 2.0f),
                     float yawDegrees = -90.0f,
                     float pitchDegrees = 0.0f);

    glm::mat4 GetViewMatrix() const;

    // Polls WASD + Space/Left-Shift via glfwGetKey each call. NOT driven by
    // Window's KeyCallback: that fires only on press/release transitions and
    // can't produce smooth, deltaTime-scaled motion while a key is held.
    // Thin wrapper around the GLFW-agnostic overload below — kept for the
    // GLFW-driven game client; a Qt-hosted caller (no GLFWwindow*) tracks its
    // own held-key state and calls that overload directly.
    void ProcessKeyboard(GLFWwindow* window, float deltaTime);

    // The real implementation: each flag is "is this movement direction
    // currently held", already resolved by the caller from whatever input
    // system it owns (GLFW polling, Qt key events, etc.) — this class has no
    // windowing-system dependency beyond this header's forward-declared
    // GLFWwindow* for the convenience overload above.
    void ProcessKeyboard(bool forward, bool backward, bool left, bool right, bool up, bool down, float deltaTime);

    // xOffset/yOffset are RAW pixel deltas since the last sample (computed by
    // the caller from consecutive CursorPosCallback invocations), already
    // Y-inverted by the caller so "mouse up" means "look up". Applies pitch
    // clamping internally to avoid the +/-90 degree gimbal flip.
    void ProcessMouseMovement(float xOffset, float yOffset);

    glm::vec3 GetPosition() const noexcept { return m_Position; }

    float movementSpeed = 2.5f;     // world units / second
    float mouseSensitivity = 0.1f;  // degrees per raw pixel of mouse delta

private:
    void UpdateVectors();

    glm::vec3 m_Position;
    glm::vec3 m_Front{0.0f, 0.0f, -1.0f};
    glm::vec3 m_Up{0.0f, 1.0f, 0.0f};
    glm::vec3 m_Right{1.0f, 0.0f, 0.0f};
    const glm::vec3 m_WorldUp{0.0f, 1.0f, 0.0f};

    float m_YawDegrees;
    float m_PitchDegrees;
};

} // namespace polyizon
