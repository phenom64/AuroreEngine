/* SPDX-License-Identifier: MPL-2.0 */
#version 450
layout(location=0) out vec2 texCoord;
layout(location=1) out vec4 vertexColor;
layout(push_constant) uniform TriangleParams {
    layout(offset=0) vec4 positions[3];
    layout(offset=48) vec4 colors[3];
    layout(offset=96) vec2 uvs[3];
    layout(offset=120) uint index;
} params;
void main() {
    vec4 position=params.positions[gl_VertexIndex];
    gl_Position=vec4((position.xy*2.0-1.0)*position.w,
                     position.z*position.w,position.w);
    texCoord=params.uvs[gl_VertexIndex];
    vertexColor=params.colors[gl_VertexIndex];
}
