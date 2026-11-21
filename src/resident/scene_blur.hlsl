Texture2D image : register(t0);
SamplerState filtering : register(s0);

// Matches SceneBlur::Constants in resident_material.h.
cbuffer Parameters : register(b0) {
    float2 offset;
    float phase;
    float centerWeight;
    float4 taps[16]; // x = distance, y = weight
};

struct Vertex {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

Vertex vertex(uint id : SV_VertexID) {
    Vertex result;
    result.uv = float2((id << 1) & 2, id & 2);
    result.position = float4(result.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return result;
}

float3 EncodeSrgb(float3 color) {
    color = max(color, 0);
    return lerp(1.055 * pow(color, 1.0 / 2.4) - .055, color * 12.92, step(color, .0031308));
}

float4 pixel(Vertex v) : SV_TARGET {
    if (phase < .5) {
        float3 color = image.Sample(filtering, v.uv + offset).rgb + image.Sample(filtering, v.uv - offset).rgb;
        color += image.Sample(filtering, v.uv + float2(offset.x, -offset.y)).rgb
               + image.Sample(filtering, v.uv + float2(-offset.x, offset.y)).rgb;
        return float4(color * .25, 1);
    }

    float3 color = image.Sample(filtering, v.uv).rgb * centerWeight;
    [loop] for (int i = 0; i < 16; ++i) {
        if (taps[i].y <= 0) break;
        float2 delta = offset * taps[i].x;
        color += (image.Sample(filtering, v.uv + delta).rgb + image.Sample(filtering, v.uv - delta).rgb) * taps[i].y;
    }
    return float4(phase > 1.5 ? EncodeSrgb(color) : color, 1);
}
