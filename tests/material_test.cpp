// Native GPU material checks and previews over a generated scene, never a game capture.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <vector>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "resident_material.h"
#include "resident_ui.h"
#include "resident_icon.h"
#include "resident_embedded_icon.h"
#include "resident_glyphs.h"
using Microsoft::WRL::ComPtr;
using namespace istaroth;
static unsigned checks=0;
static void Check(bool result){checks++;if(!result){fprintf(stderr,"material check %u failed\n",checks);exit(1);}}
static void Hr(HRESULT result){if(FAILED(result)){fprintf(stderr,"material HRESULT=0x%08lx\n",result);exit(1);}checks++;}
static ComPtr<ID3D11Device> device;static ComPtr<ID3D11DeviceContext> context;
static ComPtr<ID3D11Texture2D> Texture(UINT w,UINT h,DXGI_FORMAT format,const std::vector<BYTE>* pixels=nullptr) {
    D3D11_TEXTURE2D_DESC description{};description.Width=w;description.Height=h;description.MipLevels=description.ArraySize=description.SampleDesc.Count=1;
    description.Format=format;description.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{};if(pixels){data.pSysMem=pixels->data();data.SysMemPitch=w*4;}
    ComPtr<ID3D11Texture2D> texture;Hr(device->CreateTexture2D(&description,pixels?&data:nullptr,&texture));return texture;
}
static ComPtr<ID3D11Texture2D> Resource(ID3D11ShaderResourceView* view) {
    ComPtr<ID3D11Resource> resource;view->GetResource(&resource);ComPtr<ID3D11Texture2D> texture;Hr(resource.As(&texture));return texture;
}
static std::vector<BYTE> Read(ID3D11Texture2D* texture) {
    D3D11_TEXTURE2D_DESC description{};texture->GetDesc(&description);description.BindFlags=description.MiscFlags=0;
    description.Usage=D3D11_USAGE_STAGING;description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;Hr(device->CreateTexture2D(&description,nullptr,&staging));context->CopyResource(staging.Get(),texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};Hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));std::vector<BYTE> data(description.Width*description.Height*4);
    for(UINT y=0;y<description.Height;y++)memcpy(data.data()+y*description.Width*4,(BYTE*)mapped.pData+y*mapped.RowPitch,description.Width*4);
    context->Unmap(staging.Get(),0);return data;
}
static void Save(ID3D11Texture2D* texture,const std::filesystem::path& path) {
    auto pixels=Read(texture);D3D11_TEXTURE2D_DESC description{};texture->GetDesc(&description);
    UINT width=1120,height=1440,x=(description.Width-width)/2;
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> image;
    Hr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));Hr(factory->CreateStream(&stream));Hr(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE));
    Hr(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));Hr(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));Hr(encoder->CreateNewFrame(&image,nullptr));
    Hr(image->Initialize(nullptr));Hr(image->SetSize(width,height));WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;Hr(image->SetPixelFormat(&format));Check(format==GUID_WICPixelFormat32bppBGRA);
    Hr(image->WritePixels(height,description.Width*4,(UINT)pixels.size()-x*4,pixels.data()+x*4));Hr(image->Commit());Hr(encoder->Commit());
}
static std::vector<BYTE> Fill(UINT w,UINT h,BYTE r,BYTE g,BYTE b,bool bgra=false) {
    std::vector<BYTE> data(w*h*4);for(size_t p=0;p<data.size();p+=4){data[p]=bgra?b:r;data[p+1]=g;data[p+2]=bgra?r:b;data[p+3]=255;}return data;
}
static void ColorProbe() {
    auto canvas=Texture(64,64,DXGI_FORMAT_R8G8B8A8_TYPELESS);ComPtr<ID3D11RenderTargetView> legacy,corrected;
    D3D11_RENDER_TARGET_VIEW_DESC view{};view.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    Hr(device->CreateRenderTargetView(canvas.Get(),&view,&legacy));Hr(CreateUiTarget(device.Get(),canvas.Get(),&corrected));
    for(unsigned mode=0;mode<2;mode++) {
        auto& io=ImGui::GetIO();io.DisplaySize=ImVec2(64,64);io.DeltaTime=1.f/60;
        ImGui_ImplDX11_NewFrame();ImGui::NewFrame();ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(4,4),ImVec2(24,24),IM_COL32(80,170,255,255));ImGui::Render();
        auto target=mode?corrected.Get():legacy.Get();context->OMSetRenderTargets(1,&target,nullptr);const float clear[]={0,0,0,1};context->ClearRenderTargetView(target,clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());auto pixels=Read(canvas.Get());size_t p=(10*64+10)*4;
        Check(abs((int)pixels[p]-(mode?80:152))<=1&&abs((int)pixels[p+1]-(mode?170:213))<=1&&pixels[p+2]==255);
    }
}
static void MipmapProbe() {
    D3D11_TEXTURE2D_DESC description{};description.Width=64;description.Height=64;description.MipLevels=7;description.ArraySize=1;
    description.SampleDesc.Count=1;description.Format=DXGI_FORMAT_R8G8B8A8_UNORM;description.Usage=D3D11_USAGE_IMMUTABLE;
    description.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    std::vector<std::vector<BYTE>> levels(7);std::vector<D3D11_SUBRESOURCE_DATA> initial(7);
    for(UINT level=0,size=64;level<7;++level,size=(std::max)(1u,size>>1)) {
        BYTE r=level==0?0:(level==1?40:80),g=level==0?0:(level==1?85:170),b=level==0?0:(level==1?128:255);
        levels[level]=Fill(size,size,r,g,b);initial[level]={levels[level].data(),size*4,0};
    }
    ComPtr<ID3D11Texture2D> mip_texture;Hr(device->CreateTexture2D(&description,initial.data(),&mip_texture));
    ComPtr<ID3D11ShaderResourceView> mip_view;Hr(device->CreateShaderResourceView(mip_texture.Get(),nullptr,&mip_view));
    auto canvas=Texture(64,64,DXGI_FORMAT_R8G8B8A8_UNORM);ComPtr<ID3D11RenderTargetView> rt;Hr(device->CreateRenderTargetView(canvas.Get(),nullptr,&rt));
    auto& io=ImGui::GetIO();io.DisplaySize=ImVec2(64,64);io.DeltaTime=1.f/60;
    ImGui_ImplDX11_NewFrame();ImGui::NewFrame();
    ImGui::GetBackgroundDrawList()->AddImage((ImTextureID)(uintptr_t)mip_view.Get(),ImVec2(4,4),ImVec2(20,20));
    ImGui::Render();auto target=rt.Get();context->OMSetRenderTargets(1,&target,nullptr);
    const float clear[]={0,0,0,1};context->ClearRenderTargetView(target,clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());auto pixels=Read(canvas.Get());size_t p=(12*64+12)*4;
    Check(abs((int)pixels[p]-80)<=2&&abs((int)pixels[p+1]-170)<=2&&pixels[p+2]>=253);
}
static void BlurChecks() {
    for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB}) {
        SceneBlur blur;auto data=Fill(128,128,80,170,255,format==DXGI_FORMAT_B8G8R8A8_UNORM||format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
        auto input=Texture(128,128,format,&data);ID3D11ShaderResourceView* output=nullptr;
        Check(blur.Draw(device.Get(),context.Get(),input.Get(),{-1,0,128,128},1.5f,&output)==E_INVALIDARG);
        Hr(blur.Draw(device.Get(),context.Get(),input.Get(),{0,0,128,128},1.5f,&output));Check(output!=nullptr);
        auto texture=Resource(output);auto pixels=Read(texture.Get());size_t center=(16*32+16)*4;
        Check(abs((int)pixels[center]-80)<=1&&abs((int)pixels[center+1]-170)<=1&&pixels[center+2]>=254);
        auto previous=output;Hr(blur.Draw(device.Get(),context.Get(),input.Get(),{0,0,128,128},2.f,&output));Check(output==previous);
    }
    auto data=Fill(512,256,0,0,0);for(UINT y=0;y<256;y++)for(UINT x=0;x<512;x++){BYTE value=((x/4+y/4)&1)?224:32;size_t p=(y*512+x)*4;data[p]=data[p+1]=data[p+2]=value;}
    auto input=Texture(512,256,DXGI_FORMAT_R8G8B8A8_UNORM,&data);SceneBlur full,partial;ID3D11ShaderResourceView* output=nullptr;
    Hr(full.Draw(device.Get(),context.Get(),input.Get(),{0,0,512,256},1.5f,&output));auto all=Read(Resource(output).Get());
    BYTE minimum=255,maximum=0;for(unsigned x=40;x<88;x++){BYTE value=all[(32*128+x)*4];minimum=std::min(minimum,value);maximum=std::max(maximum,value);}
    if(maximum-minimum>=48)fprintf(stderr,"blur contrast min=%u max=%u samples=%u,%u,%u,%u\n",minimum,maximum,all[(32*128+60)*4],all[(32*128+61)*4],all[(32*128+62)*4],all[(32*128+63)*4]);Check(maximum-minimum<48);
    Hr(partial.Draw(device.Get(),context.Get(),input.Get(),{200,100,312,164},1.5f,&output));auto region=Read(Resource(output).Get());
    for(UINT y:{25u,32u,40u})for(UINT x:{50u,64u,77u})Check(abs((int)region[(y*128+x)*4]-all[(y*128+x)*4])<=2);
    Check(full.Draw(device.Get(),context.Get(),input.Get(),{0,0,512,256},0,nullptr)==E_POINTER);
    Check(full.Draw(nullptr,context.Get(),input.Get(),{0,0,512,256},1,&output)==E_INVALIDARG);
    UINT quality=0;Hr(device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM,4,&quality));
    if(quality) {
        D3D11_TEXTURE2D_DESC description{};description.Width=description.Height=64;description.MipLevels=description.ArraySize=1;description.SampleDesc.Count=4;
        description.Format=DXGI_FORMAT_R8G8B8A8_UNORM;description.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> msaa;ComPtr<ID3D11RenderTargetView> target;Hr(device->CreateTexture2D(&description,nullptr,&msaa));Hr(device->CreateRenderTargetView(msaa.Get(),nullptr,&target));
        const float clear[]={80.f/255,170.f/255,1,1};context->ClearRenderTargetView(target.Get(),clear);SceneBlur blur;
        Hr(blur.Draw(device.Get(),context.Get(),msaa.Get(),{0,0,64,64},1.5f,&output));auto pixels=Read(Resource(output).Get());Check(abs((int)pixels[0]-80)<=1&&abs((int)pixels[1]-170)<=1&&pixels[2]>=254);
    }
    context->ClearState();
}
static void Previews() {
    constexpr UINT width=3840,height=2160;auto pixels=Fill(width,height,0,0,0,true);
    for(UINT y=0;y<height;y++)for(UINT x=0;x<width;x++) {
        float u=x/(float)width,v=y/(float)height;float r=62+v*45,g=151+v*45,b=210+v*30;
        if(u>.50f-.15f*v) {float grass=12*sinf(x*.19f+y*.13f)+7*sinf(x*.047f-y*.21f);r=108+grass;g=168+grass;b=54+grass*.35f;}
        if(u>.35f+.08f*v&&u<.58f+.12f*v) {float edge=10*sinf(x*.021f+y*.037f);r=112+edge;g=126+edge;b=137+edge;}
        if((x/72)%8==0&&u>.52f-.15f*v&&((y+3*x)%190)<110){r=57;g=111; b=46;}
        size_t p=(y*width+x)*4;pixels[p]=(BYTE)std::clamp(b,0.f,255.f);pixels[p+1]=(BYTE)std::clamp(g,0.f,255.f);pixels[p+2]=(BYTE)std::clamp(r,0.f,255.f);
    }
    auto canvas=Texture(width,height,DXGI_FORMAT_B8G8R8A8_TYPELESS,&pixels);ComPtr<ID3D11RenderTargetView> legacy,corrected;
    D3D11_RENDER_TARGET_VIEW_DESC view{};view.Format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    Hr(device->CreateRenderTargetView(canvas.Get(),&view,&legacy));Hr(CreateUiTarget(device.Get(),canvas.Get(),&corrected));
    ComPtr<ID3D11ShaderResourceView> icon;
#ifdef ISTAROTH_EMBED_ICON
    Hr(LoadResidentIconWithFallback(device.Get(),L"not-an-existing-icon.png",&icon));
#endif
    SceneBlur blur;std::filesystem::path folder=L"material-previews";std::filesystem::create_directories(folder);
    const wchar_t* names[]={L"00-dev1-srgb.png",L"01-corrected-color.png",L"02-glass-balanced.png",L"03-glass-strong.png"};
    for(unsigned mode=0;mode<4;mode++) {
        for(unsigned frame=0;frame<3;frame++) {
            context->ClearState();context->UpdateSubresource(canvas.Get(),0,nullptr,pixels.data(),width*4,0);ID3D11ShaderResourceView* backdrop=nullptr;
            if(mode>=2)Hr(blur.Draw(device.Get(),context.Get(),canvas.Get(),{0,0,(LONG)width,(LONG)height},mode==2?3.f:4.8f,&backdrop));
            auto& io=ImGui::GetIO();io.DisplaySize=ImVec2((float)width,(float)height);io.DeltaTime=1.f/60;
            ImGui_ImplDX11_NewFrame();ImGui::NewFrame();UiState state;state.initialized=state.enabled=state.foreground=state.actual_known=true;
            state.phase=UiPhase::Waiting;strcpy_s(state.reason,u8"当前保持原速；关闭面板后进入对话自动加速");state.icon_texture=(uint64_t)(uintptr_t)icon.Get();state.backdrop=(uint64_t)(uintptr_t)backdrop;
            state.display_width=width;state.display_height=height;bool open=true;DrawResidentUi(state,open,2.f,nullptr);ImGui::Render();
            auto target=mode?corrected.Get():legacy.Get();context->OMSetRenderTargets(1,&target,nullptr);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
        Save(canvas.Get(),folder/names[mode]);
    }
}
int main() {
    Hr(CoInitializeEx(nullptr,COINIT_MULTITHREADED));Hr(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;wchar_t windows[MAX_PATH];GetWindowsDirectoryW(windows,MAX_PATH);
    auto font=std::wstring(windows)+L"\\Fonts\\msyh.ttc";char path[2048];Check(WideCharToMultiByte(CP_UTF8,0,font.c_str(),-1,path,2048,nullptr,nullptr)!=0);
    ImFontConfig font_config;font_config.OversampleH=2;font_config.OversampleV=2;font_config.PixelSnapH=true;
    Check(io.Fonts->AddFontFromFileTTF(path,32.f,&font_config,ResidentGlyphs)!=nullptr);Check(ImGui_ImplDX11_Init(device.Get(),context.Get()));
    ColorProbe();MipmapProbe();BlurChecks();Previews();ImGui_ImplDX11_Shutdown();ImGui::DestroyContext();context->ClearState();CoUninitialize();
    printf("{\"passed\":true,\"checks\":%u,\"game_accessed\":false,\"legacy_blue\":[152,213,255],\"corrected_blue\":[80,170,255],\"previews\":4,\"scope\":\"WARP sRGB palette, mipmap sampling, RGBA/BGRA blur, ROI, MSAA and generated-scene PNGs\"}\n",checks);
}
