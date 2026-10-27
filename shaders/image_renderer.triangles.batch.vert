/* SPDX-License-Identifier: MPL-2.0 */
#version 450
layout(location=0) in vec4 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 tint;
layout(location=0) out vec2 texCoord;
layout(location=1) out vec4 vertexColor;
void main() {
    gl_Position=vec4((position.xy*2.0-1.0)*position.w,
                     position.z*position.w,position.w);
    texCoord=uv;
    vertexColor=tint;
}
