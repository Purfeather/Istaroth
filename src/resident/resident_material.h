#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace istaroth {
inline DXGI_FORMAT UiUnormFormat(DXGI_FORMAT format) {
    switch(format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:case DXGI_FORMAT_R8G8B8A8_UNORM:case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:case DXGI_FORMAT_B8G8R8A8_UNORM:case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:return DXGI_FORMAT_B8G8R8A8_UNORM;
    default:return DXGI_FORMAT_UNKNOWN;
    }
}
inline HRESULT CreateUiTarget(ID3D11Device* device,ID3D11Texture2D* buffer,ID3D11RenderTargetView** target) {
    D3D11_TEXTURE2D_DESC texture{};buffer->GetDesc(&texture);
    D3D11_RENDER_TARGET_VIEW_DESC view{};view.Format=UiUnormFormat(texture.Format);
    view.ViewDimension=texture.SampleDesc.Count>1?D3D11_RTV_DIMENSION_TEXTURE2DMS:D3D11_RTV_DIMENSION_TEXTURE2D;
    // ImGui's palette and PNGs already contain sRGB values. An UNORM view avoids
    // encoding them again on an sRGB back buffer. HDR keeps its existing path.
    return device->CreateRenderTargetView(buffer,view.Format==DXGI_FORMAT_UNKNOWN?nullptr:&view,target);
}

class SceneBlur {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Texture2D> copy,linear[2],encoded;
    Ptr<ID3D11ShaderResourceView> source,views[2],result;
    Ptr<ID3D11RenderTargetView> targets[3];
    Ptr<ID3D11VertexShader> vs;Ptr<ID3D11PixelShader> ps;
    Ptr<ID3D11Buffer> parameters;Ptr<ID3D11SamplerState> sampler;
    Ptr<ID3D11RasterizerState> raster;Ptr<ID3D11DepthStencilState> depth;
    UINT width=0,height=0,samples=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    HRESULT failure=S_OK;
    static void Check(HRESULT value){if(FAILED(value))throw value;}
    void ResetTextures() {
        copy.Reset();source.Reset();result.Reset();encoded.Reset();
        for(unsigned i=0;i<2;i++){linear[i].Reset();views[i].Reset();}for(auto& target:targets)target.Reset();width=height=samples=0;
    }
    void PrepareShaders(ID3D11Device* device) {
        if(vs)return;
        const char shader[]=R"(
Texture2D image:register(t0);SamplerState filtering:register(s0);
cbuffer Parameters:register(b0){float2 offset;float phase;float unused;};
struct Vertex {float4 position:SV_POSITION;float2 uv:TEXCOORD0;};
Vertex vertex(uint id:SV_VertexID){Vertex v;v.uv=float2((id<<1)&2,id&2);v.position=float4(v.uv*float2(2,-2)+float2(-1,1),0,1);return v;}
float3 encode(float3 c){c=max(c,0);return lerp(1.055*pow(c,1.0/2.4)-.055,c*12.92,step(c,.0031308));}
float4 pixel(Vertex v):SV_TARGET {
    if(phase<.5){float3 c=image.Sample(filtering,v.uv+offset).rgb+image.Sample(filtering,v.uv-offset).rgb;
        c+=image.Sample(filtering,v.uv+float2(offset.x,-offset.y)).rgb+image.Sample(filtering,v.uv+float2(-offset.x,offset.y)).rgb;return float4(c*.25,1);}
    float3 color=image.Sample(filtering,v.uv).rgb*.227027;
    color+=(image.Sample(filtering,v.uv+offset*1.384615).rgb+image.Sample(filtering,v.uv-offset*1.384615).rgb)*.316216;
    color+=(image.Sample(filtering,v.uv+offset*3.230769).rgb+image.Sample(filtering,v.uv-offset*3.230769).rgb)*.070270;
    return float4(phase>1.5?encode(color):color,1);
})";
        Ptr<ID3DBlob> vertex,pixel,error;
        Check(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"vertex","vs_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vertex,&error));
        Check(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"pixel","ps_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&pixel,&error));
        Check(device->CreateVertexShader(vertex->GetBufferPointer(),vertex->GetBufferSize(),nullptr,&vs));
        Check(device->CreatePixelShader(pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,&ps));
        D3D11_BUFFER_DESC constant{};constant.ByteWidth=16;constant.BindFlags=D3D11_BIND_CONSTANT_BUFFER;Check(device->CreateBuffer(&constant,nullptr,&parameters));
        D3D11_SAMPLER_DESC sampling{};sampling.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sampling.AddressU=sampling.AddressV=sampling.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampling.MaxLOD=D3D11_FLOAT32_MAX;
        sampling.ComparisonFunc=D3D11_COMPARISON_ALWAYS;sampling.MaxAnisotropy=1;Check(device->CreateSamplerState(&sampling,&sampler));
        D3D11_RASTERIZER_DESC drawing{};drawing.FillMode=D3D11_FILL_SOLID;drawing.CullMode=D3D11_CULL_NONE;drawing.DepthClipEnable=TRUE;drawing.ScissorEnable=TRUE;
        Check(device->CreateRasterizerState(&drawing,&raster));D3D11_DEPTH_STENCIL_DESC testing{};testing.DepthFunc=D3D11_COMPARISON_ALWAYS;
        testing.FrontFace.StencilFailOp=testing.FrontFace.StencilDepthFailOp=testing.FrontFace.StencilPassOp=D3D11_STENCIL_OP_KEEP;testing.FrontFace.StencilFunc=D3D11_COMPARISON_ALWAYS;testing.BackFace=testing.FrontFace;
        Check(device->CreateDepthStencilState(&testing,&depth));
    }
    void Prepare(ID3D11Device* device,const D3D11_TEXTURE2D_DESC& input) {
        if(copy&&width==input.Width&&height==input.Height&&format==input.Format&&samples==input.SampleDesc.Count)return;
        ResetTextures();DXGI_FORMAT unorm=UiUnormFormat(input.Format);if(unorm==DXGI_FORMAT_UNKNOWN)throw DXGI_ERROR_UNSUPPORTED;
        PrepareShaders(device);
        D3D11_TEXTURE2D_DESC texture{};texture.Width=input.Width;texture.Height=input.Height;texture.MipLevels=texture.ArraySize=1;texture.SampleDesc.Count=1;
        texture.Format=unorm==DXGI_FORMAT_R8G8B8A8_UNORM?DXGI_FORMAT_R8G8B8A8_TYPELESS:DXGI_FORMAT_B8G8R8A8_TYPELESS;
        texture.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        Check(device->CreateTexture2D(&texture,nullptr,&copy));
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=unorm==DXGI_FORMAT_R8G8B8A8_UNORM?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;
        Check(device->CreateShaderResourceView(copy.Get(),&view,&source));
        texture.Width=std::max(1u,(input.Width+3)/4);texture.Height=std::max(1u,(input.Height+3)/4);
        texture.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;texture.BindFlags|=D3D11_BIND_RENDER_TARGET;
        for(unsigned i=0;i<2;i++) {
            Check(device->CreateTexture2D(&texture,nullptr,&linear[i]));Check(device->CreateShaderResourceView(linear[i].Get(),nullptr,&views[i]));Check(device->CreateRenderTargetView(linear[i].Get(),nullptr,&targets[i]));
        }
        texture.Format=DXGI_FORMAT_R8G8B8A8_UNORM;Check(device->CreateTexture2D(&texture,nullptr,&encoded));
        Check(device->CreateShaderResourceView(encoded.Get(),nullptr,&result));Check(device->CreateRenderTargetView(encoded.Get(),nullptr,&targets[2]));
        width=input.Width;height=input.Height;format=input.Format;samples=input.SampleDesc.Count;
    }
public:
    void Reset() {
        ResetTextures();vs.Reset();ps.Reset();parameters.Reset();sampler.Reset();raster.Reset();depth.Reset();failure=S_OK;
    }
    // The caller saves/restores pipeline state. Copy before drawing any UI.
    HRESULT Draw(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* input,D3D11_RECT area,float strength,ID3D11ShaderResourceView** output) noexcept {
        if(!output)return E_POINTER;*output=nullptr;
        if(!device||!context||!input)return E_INVALIDARG;
        if(FAILED(failure))return failure;
        try {
            D3D11_TEXTURE2D_DESC description{};input->GetDesc(&description);
            if(!description.Width||!description.Height||!std::isfinite(strength)||strength<=0||strength>8||
               area.left<0||area.top<0||area.right>(LONG)description.Width||area.bottom>(LONG)description.Height||area.left>=area.right||area.top>=area.bottom)return E_INVALIDARG;
            Prepare(device,description);
            LONG margin=(LONG)ceilf(16*strength+20);
            D3D11_RECT region{std::max(0L,area.left-margin),std::max(0L,area.top-margin),std::min((LONG)width,area.right+margin),std::min((LONG)height,area.bottom+margin)};
            context->OMSetRenderTargets(0,nullptr,nullptr);ID3D11ShaderResourceView* empty=nullptr;context->PSSetShaderResources(0,1,&empty);
            if(samples>1)context->ResolveSubresource(copy.Get(),0,input,0,description.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS||description.Format==DXGI_FORMAT_B8G8R8A8_TYPELESS?UiUnormFormat(description.Format):description.Format);
            else {D3D11_BOX box{(UINT)region.left,(UINT)region.top,0,(UINT)region.right,(UINT)region.bottom,1};context->CopySubresourceRegion(copy.Get(),0,box.left,box.top,0,input,0,&box);}
            D3D11_TEXTURE2D_DESC reduced{};encoded->GetDesc(&reduced);
            D3D11_VIEWPORT viewport{0,0,(float)reduced.Width,(float)reduced.Height,0,1};context->RSSetViewports(1,&viewport);
            D3D11_RECT scissor{region.left/4,region.top/4,(region.right+3)/4,(region.bottom+3)/4};context->RSSetScissorRects(1,&scissor);
            context->RSSetState(raster.Get());context->OMSetBlendState(nullptr,nullptr,UINT(-1));context->OMSetDepthStencilState(depth.Get(),0);
            context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);context->GSSetShader(nullptr,nullptr,0);
            context->HSSetShader(nullptr,nullptr,0);context->DSSetShader(nullptr,nullptr,0);context->CSSetShader(nullptr,nullptr,0);
            auto constant=parameters.Get();auto filtering=sampler.Get();context->PSSetConstantBuffers(0,1,&constant);context->PSSetSamplers(0,1,&filtering);
            ID3D11ShaderResourceView* images[]={source.Get(),views[0].Get(),views[1].Get()};
            float values[3][4]={{1.f/width,1.f/height,0,0},{strength/reduced.Width,0,1,0},{0,strength/reduced.Height,2,0}};
            for(unsigned i=0;i<3;i++) {
                auto target=targets[i].Get();context->OMSetRenderTargets(1,&target,nullptr);context->PSSetShaderResources(0,1,&images[i]);
                context->UpdateSubresource(constant,0,nullptr,values[i],0,0);context->Draw(3,0);context->PSSetShaderResources(0,1,&empty);
            }
            Check(device->GetDeviceRemovedReason());*output=result.Get();return S_OK;
        }catch(HRESULT error){failure=error;return error;}catch(...){failure=E_OUTOFMEMORY;return failure;}
    }
};
}
