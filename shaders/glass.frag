#version 440
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 panelSize;
    float cornerRadius;
    vec4 glassTint;
};
layout(binding = 1) uniform sampler2D source;
void main() {
    vec2 size = max(panelSize, vec2(1.0));
    vec2 p = (qt_TexCoord0 - 0.5) * size;
    vec2 q = abs(p) - size * 0.5 + cornerRadius;
    float distanceToEdge = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - cornerRadius;
    float mask = 1.0 - smoothstep(-1.0, 0.5, distanceToEdge);
    vec2 normal = normalize(max(q, vec2(0.01))) * sign(p);
    float rim = 1.0 - smoothstep(0.0, 18.0, -distanceToEdge);
    vec2 refracted = clamp(qt_TexCoord0 - normal * rim * 5.0 / size, 0.0, 1.0);
    vec3 scene = texture(source, refracted).rgb;
    // QML QColor uniforms already contain premultiplied RGB. Apply alpha once.
    vec3 color = scene * (1.0 - glassTint.a) + glassTint.rgb;
    float edge = 1.0 - smoothstep(0.5, 1.8, -distanceToEdge);
    float light = 0.09 + 0.16 * max(0.0, dot(normal, normalize(vec2(-0.6, -1.0))));
    color += edge * light + pow(rim, 4.0) * 0.025;
    fragColor = vec4(color * mask, mask) * qt_Opacity;
}
