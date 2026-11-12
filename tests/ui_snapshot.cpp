// Offline UI rendering only: no window, process attachment, or desktop input.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_dx11.h"
#include "resident_ui.h"
#include "resident_glyphs.h"
#include "resident_icon.h"
#include "resident_embedded_icon.h"
using Microsoft::WRL::ComPtr;
static void Check(HRESULT code){if(FAILED(code))throw code;}
int wmain(int argc,wchar_t** argv) {
    if(argc!=5&&argc!=6)return ERROR_INVALID_PARAMETER;
    unsigned width=wcstoul(argv[1],nullptr,10),height=wcstoul(argv[2],nullptr,10);
    if(width<640||height<480||width>3840||height>2160)return ERROR_INVALID_PARAMETER;
    try {
        Check(CoInitializeEx(nullptr,COINIT_MULTITHREADED));
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
        D3D11_TEXTURE2D_DESC desc={};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11RenderTargetView> target;
        Check(device->CreateTexture2D(&desc,nullptr,&texture));Check(device->CreateRenderTargetView(texture.Get(),nullptr,&target));
        ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
        io.DisplaySize=ImVec2((float)width,(float)height);io.DeltaTime=1.f/60.f;
        wchar_t fonts[MAX_PATH];GetWindowsDirectoryW(fonts,MAX_PATH);std::wstring path=std::wstring(fonts)+L"\\Fonts\\msyh.ttc";
        char utf8[2048];WideCharToMultiByte(CP_UTF8,0,path.c_str(),-1,utf8,2048,nullptr,nullptr);
        if(!io.Fonts->AddFontFromFileTTF(utf8,20.f,nullptr,ResidentGlyphs))throw HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        wchar_t own[32768];DWORD own_length=GetModuleFileNameW(nullptr,own,32768);
        std::wstring icon_path=argc==6?argv[5]:L"";
        if(argc==5&&own_length&&own_length<32768) {
            icon_path.assign(own,own_length);icon_path.resize(icon_path.find_last_of(L"\\/")+1);
            icon_path+=L"assets\\istaroth-mark.png";
        }
        ComPtr<ID3D11ShaderResourceView> icon;
        HRESULT icon_status=
#ifdef ISTAROTH_EMBED_ICON
            istaroth::LoadResidentIconWithFallback
#else
            istaroth::LoadResidentIcon
#endif
            (device.Get(),icon_path.c_str(),&icon);
        ImGui::StyleColorsDark();if(!ImGui_ImplDX11_Init(device.Get(),context.Get()))throw E_FAIL;
        istaroth::UiState state;state.initialized=state.actual_known=state.foreground=true;
        state.icon_texture=(uint64_t)(uintptr_t)icon.Get();
        state.display_width=(float)width;state.display_height=(float)height;state.phase=istaroth::UiPhase::Paused;
        strcpy_s(state.reason,u8"已暂停，已确认恢复原速");
        std::wstring mode=argv[3];bool open=true;
        if(mode.rfind(L"hud-",0)==0){open=false;mode.erase(0,4);}
        if(mode==L"active"||mode==L"auto1") {
            state.phase=istaroth::UiPhase::Active;state.enabled=state.dialogue=state.effects_active=true;
            state.advance_active=state.skip_active=true;state.actual=mode==L"active"?4.f:1.f;
            state.target=mode==L"active"?4u:1u;state.speed_active=state.actual>1;
            strcpy_s(state.reason,u8"常规对话控制中");
        } else if(mode==L"waiting"||mode==L"mixed") {
            state.phase=istaroth::UiPhase::Waiting;state.enabled=true;
            if(mode==L"mixed")state.choose_first=state.skip_cooldown=false;
            strcpy_s(state.reason,u8"等待对话，离开对话保持原速");
        } else if(mode==L"fault") {
            state.phase=istaroth::UiPhase::Fault;state.actual_known=false;
            strcpy_s(state.reason,u8"组件异常，已停止请求；请正常重启并加载");
        } else if(mode==L"disabled") {
            state.advance=state.choose_first=state.skip_cooldown=false;
        } else if(mode!=L"paused")return ERROR_INVALID_PARAMETER;
        for(unsigned frame=0;frame<3;frame++) {
            ImGui_ImplDX11_NewFrame();ImGui::NewFrame();
            istaroth::DrawResidentUi(state,open,std::clamp(std::min(width/1920.f,height/1080.f),.65f,2.f),nullptr);
            ImGui::Render();auto rt=target.Get();context->OMSetRenderTargets(1,&rt,nullptr);
            const float clear[4]={.08f,.15f,.24f,1};context->ClearRenderTargetView(rt,clear);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;Check(device->CreateTexture2D(&desc,nullptr,&staging));context->CopyResource(staging.Get(),texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped={};Check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
        ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> image;
        Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
        Check(factory->CreateStream(&stream));Check(stream->InitializeFromFilename(argv[4],GENERIC_WRITE));
        Check(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));Check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
        Check(encoder->CreateNewFrame(&image,nullptr));Check(image->Initialize(nullptr));Check(image->SetSize(width,height));
        WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;Check(image->SetPixelFormat(&format));if(format!=GUID_WICPixelFormat32bppBGRA)throw E_FAIL;
        Check(image->WritePixels(height,mapped.RowPitch,mapped.RowPitch*height,(BYTE*)mapped.pData));Check(image->Commit());Check(encoder->Commit());
        context->Unmap(staging.Get(),0);
        printf("offline UI PNG %ux%u; icon_loaded=%u icon_status=0x%08lx; no window or desktop input",width,height,icon?1u:0u,(unsigned long)icon_status);
        if(ImGuiWindow* hud=ImGui::FindWindowByName("##IstarothHudCapsule")) {
            float font_size=GImGui->FontBaseSize*hud->FontWindowScale;
            ImFont* font=ImGui::GetFont();
            auto text_width=[&](const char* text){return std::floor(font->CalcTextSizeA(font_size,FLT_MAX,0,text).x+0.99999f);};
            printf("; hud_rect=%.3f,%.3f,%.3f,%.3f; font_size=%.3f; text_widths=%.3f,%.3f,%.3f,%.3f,%.3f,%.3f",
                hud->Pos.x,hud->Pos.y,hud->Size.x,hud->Size.y,font_size,text_width(u8"4.0×"),text_width(u8"1.0×"),text_width(u8"推进"),text_width(u8"首选"),text_width(u8"冷却"),text_width("Insert"));
            for(const ImDrawCmd& command:hud->DrawList->CmdBuffer)if(command.ElemCount) {
                printf("; hud_draw_clip=%.3f,%.3f,%.3f,%.3f",command.ClipRect.x,command.ClipRect.y,command.ClipRect.z,command.ClipRect.w);break;
            }
        }
        if(ImGuiWindow* menu=ImGui::FindWindowByName("##IstarothMenuModal"))
            printf("; menu_rect=%.3f,%.3f,%.3f,%.3f",menu->Pos.x,menu->Pos.y,menu->Size.x,menu->Size.y);
        puts("");ImGui_ImplDX11_Shutdown();ImGui::DestroyContext();
        return 0;
    }catch(HRESULT code){fprintf(stderr,"offline UI error %08lx\n",code);return 1;}
}
