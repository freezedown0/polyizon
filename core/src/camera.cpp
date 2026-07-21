#include "polyizon/camera.hpp"

#include <GLFW/glfw3.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace polyizon {

namespace {
constexpr float kMaxPitchDegrees = 89.0f; // strictly less than 90 to avoid the front/up cross-product degenerating
} // namespace

Camera::Camera(glm::vec3 position, float yawDegrees, float pitchDegrees)
    : m_Position(position)
    , m_YawDegrees(yawDegrees)
    , m_PitchDegrees(pitchDegrees) {
    UpdateVectors();
}

glm::mat4 Camera::GetViewMatrix() const {
    return glm::lookAt(m_Position, m_Position + m_Front, m_Up);
}

void Camera::ProcessKeyboard(GLFWwindow* window, float deltaTime) {
    const float velocity = movementSpeed * deltaTime;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) m_Position += m_Front * velocity;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) m_Position -= m_Front * velocity;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) m_Position -= m_Right * velocity;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) m_Position += m_Right * velocity;
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) m_Position += m_WorldUp * velocity;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) m_Position -= m_WorldUp * velocity;
}

void Camera::ProcessMouseMovement(float xOffset, float yOffset) {
    m_YawDegrees += xOffset * mouseSensitivity;
    m_PitchDegrees = std::clamp(m_PitchDegrees + yOffset * mouseSensitivity, -kMaxPitchDegrees, kMaxPitchDegrees);
    UpdateVectors();
}

void Camera::UpdateVectors() {
    const float yawRad = glm::radians(m_YawDegrees);
    const float pitchRad = glm::radians(m_PitchDegrees);

    glm::vec3 front;
    front.x = std::cos(yawRad) * std::cos(pitchRad);
    front.y = std::sin(pitchRad);
    front.z = std::sin(yawRad) * std::cos(pitchRad);
    m_Front = glm::normalize(front);

    m_Right = glm::normalize(glm::cross(m_Front, m_WorldUp));
    m_Up = glm::normalize(glm::cross(m_Right, m_Front));
}

} // namespace polyizon
