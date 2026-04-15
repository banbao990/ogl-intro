#version 460

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 u_projection;
uniform mat4 u_view;
uniform mat4 u_model;
uniform mat4 lightSpaceMatrix;

out vec3 FragPos;
out vec3 Normal;
out vec4 FragPosLightSpace;

void main() {
    vec4 worldPos = u_model * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    Normal = normalize(mat3(u_model) * aNormal);
    FragPosLightSpace = lightSpaceMatrix * worldPos;
    gl_Position = u_projection * u_view * worldPos;
}
