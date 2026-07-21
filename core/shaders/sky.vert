#version 450

// Fullscreen-triangle trick: 3 hardcoded vertices covering the whole clip
// space (the third corner extends past it — clipped away by the
// rasterizer), so no vertex/index buffer is needed. gl_VertexIndex alone
// drives which corner this invocation is.
layout(location = 0) out vec2 vNdc;

void main() {
    const vec2 positions[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    vNdc = positions[gl_VertexIndex];
    gl_Position = vec4(vNdc, 1.0, 1.0); // z=1 (far plane); interpolated vNdc reconstructs per-pixel NDC in the fragment shader
}
