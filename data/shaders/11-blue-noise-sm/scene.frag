#version 460

in vec3 FragPos;
in vec3 Normal;
in vec4 FragPosLightSpace;

layout(binding = 0) uniform sampler2D shadowMap;
layout(binding = 1) uniform sampler2D u_blue_noise;

uniform vec3 u_light_pos;
uniform vec3 u_view_pos;
uniform vec3 u_base_color;
uniform int u_mode;
uniform float u_bias;
uniform vec2 u_noise_size;
uniform vec2 u_offset;
uniform int u_split;
uniform float u_split_x;

layout(location = 0) out vec4 frag_color;

float compare_depth(float current, float closest) {
    return current - u_bias > closest ? 1.0 : 0.0;
}

float sample_shadow_nearest(vec3 projCoords) {
    float closestDepth = texture(shadowMap, projCoords.xy).r;
    return compare_depth(projCoords.z, closestDepth);
}

float sample_shadow_pcf_4tap(vec3 projCoords) {
    vec2 texelSize = vec2(1.0) / vec2(textureSize(shadowMap, 0));
    vec2 pos = projCoords.xy / texelSize - vec2(0.5);
    ivec2 base = ivec2(floor(pos));
    vec2 frac = pos - floor(pos);

    vec2 uv00 = (vec2(base) + vec2(0.5, 0.5)) * texelSize;
    vec2 uv10 = (vec2(base) + vec2(1.5, 0.5)) * texelSize;
    vec2 uv01 = (vec2(base) + vec2(0.5, 1.5)) * texelSize;
    vec2 uv11 = (vec2(base) + vec2(1.5, 1.5)) * texelSize;

    float s00 = compare_depth(projCoords.z, texture(shadowMap, uv00).r);
    float s10 = compare_depth(projCoords.z, texture(shadowMap, uv10).r);
    float s01 = compare_depth(projCoords.z, texture(shadowMap, uv01).r);
    float s11 = compare_depth(projCoords.z, texture(shadowMap, uv11).r);

    return mix(mix(s00, s10, frac.x), mix(s01, s11, frac.x), frac.y);
}

float sample_stochastic_bilinear_blue(vec3 projCoords) {
    vec2 texelSize = vec2(1.0) / vec2(textureSize(shadowMap, 0));
    vec2 pos = projCoords.xy / texelSize - vec2(0.5);
    ivec2 base = ivec2(floor(pos));
    vec2 frac = pos - floor(pos);

    float w00 = (1.0 - frac.x) * (1.0 - frac.y);
    float w10 = frac.x * (1.0 - frac.y);
    float w01 = (1.0 - frac.x) * frac.y;

    ivec2 sp = ivec2(gl_FragCoord.xy);
    float noise = texelFetch(u_blue_noise,
        (sp + ivec2(u_offset)) % ivec2(u_noise_size), 0).r;

    ivec2 sel;
    if (noise < w00) {
        sel = base;
    } else if (noise < w00 + w10) {
        sel = base + ivec2(1, 0);
    } else if (noise < w00 + w10 + w01) {
        sel = base + ivec2(0, 1);
    } else {
        sel = base + ivec2(1, 1);
    }

    vec2 sampleUV = (vec2(sel) + vec2(0.5)) * texelSize;
    float closestDepth = texture(shadowMap, sampleUV).r;
    return compare_depth(projCoords.z, closestDepth);
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float sample_stochastic_bilinear_white(vec3 projCoords) {
    vec2 texelSize = vec2(1.0) / vec2(textureSize(shadowMap, 0));
    vec2 pos = projCoords.xy / texelSize - vec2(0.5);
    ivec2 base = ivec2(floor(pos));
    vec2 frac = pos - floor(pos);

    float w00 = (1.0 - frac.x) * (1.0 - frac.y);
    float w10 = frac.x * (1.0 - frac.y);
    float w01 = (1.0 - frac.x) * frac.y;

    vec2 sp = gl_FragCoord.xy + u_offset;
    float noise = hash12(sp);

    ivec2 sel;
    if (noise < w00) {
        sel = base;
    } else if (noise < w00 + w10) {
        sel = base + ivec2(1, 0);
    } else if (noise < w00 + w10 + w01) {
        sel = base + ivec2(0, 1);
    } else {
        sel = base + ivec2(1, 1);
    }

    vec2 sampleUV = (vec2(sel) + vec2(0.5)) * texelSize;
    float closestDepth = texture(shadowMap, sampleUV).r;
    return compare_depth(projCoords.z, closestDepth);
}

float ShadowCalculation(vec4 fragPosLightSpace) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.z > 1.0)
        return 0.0;

    int mode = u_mode;
    if (u_split != 0) {
        if (u_split == 1) {
            if (gl_FragCoord.x > u_split_x) mode = 2;
        } else {
            if (gl_FragCoord.x > u_split_x) mode = 4;
        }
    }

    if (mode == 0) {
        return sample_shadow_nearest(projCoords);
    } else if (mode == 1) {
        return sample_stochastic_bilinear_blue(projCoords);
    } else if (mode == 2) {
        return sample_shadow_pcf_4tap(projCoords);
    } else if (mode == 3) {
        return sample_stochastic_bilinear_white(projCoords);
    } else {
        return sample_stochastic_bilinear_blue(projCoords);
    }
}

void main() {
    vec3 color = u_base_color;
    vec3 normal = normalize(Normal);
    vec3 lightDir = normalize(u_light_pos - FragPos);

    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * vec3(1.0);

    vec3 viewDir = normalize(u_view_pos - FragPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), 64.0);
    vec3 specular = spec * vec3(0.3);

    vec3 ambient = 0.15 * color;

    float shadow = ShadowCalculation(FragPosLightSpace);
    vec3 lighting = ambient + (1.0 - shadow) * (diffuse + specular) * color;

    frag_color = vec4(lighting, 1.0);
}
