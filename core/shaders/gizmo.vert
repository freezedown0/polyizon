#version 450

// See GizmoPipeline: one draw call per axis/ring, MVP + color both arrive
// as push constants rather than a UBO/descriptor set.
layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;
} pc;

layout(location = 0) in vec3 inPosition;
// location 1 (normal) is present in Vertex3D's vertex input state but
// unused/undeclared here, same as shadow.vert.

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
}
