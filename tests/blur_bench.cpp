#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include "resident_material.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
int main() {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &device, nullptr, &context))) return 1;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 640; desc.Height = 360; desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> scene;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &scene))) return 2;
    std::vector<double> samples;
    for (int round = 0; round < 20; ++round) {
        istaroth::SceneBlur blur;
        ID3D11ShaderResourceView* result = nullptr;
        auto start = Clock::now();
        HRESULT hr = blur.Draw(device.Get(), context.Get(), scene.Get(), {0,0,640,360}, 3.f, &result);
        double ms = std::chrono::duration<double, std::milli>(Clock::now()-start).count();
        if (FAILED(hr) || !result) return 3;
        if (round) samples.push_back(ms);
        context->ClearState();
        context->Flush();
    }
    std::sort(samples.begin(), samples.end());
    printf("first_draw_cpu_median_ms=%.3f n=%zu\n",samples[samples.size()/2],samples.size());
}
