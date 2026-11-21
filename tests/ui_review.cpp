// Standalone UI host. All commands update the sample shown in this window.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include "resident_ui.h"
#include "resident_glyphs.h"
#include "resident_embedded_icon.h"

using namespace istaroth;
using Microsoft::WRL::ComPtr;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static UiState sample;
static bool open = true;
static unsigned command_count;
static UiCommand last_command;
static int last_value;
static constexpr int width = 960, height = 720;

static void Apply(UiCommand command, int value) {
    ++command_count;
    last_command = command;
    last_value = value;
    switch (command) {
    case UiCommand::Enabled: sample.enabled = value != 0; break;
    case UiCommand::Emergency: sample.enabled = false; break;
    case UiCommand::Multiplier: sample.target = value; break;
    case UiCommand::Advance: sample.advance = value != 0; break;
    case UiCommand::ChooseFirst: sample.choose_first = value != 0; break;
    case UiCommand::SkipCooldown: sample.skip_cooldown = value != 0; break;
    }
    sample.phase = sample.enabled ? UiPhase::Waiting : UiPhase::Paused;
}

static void Frame(float opacity = -1.f) {
    ImGui::NewFrame();
    DrawResidentUi(sample, open, 1.f, Apply, opacity);
    ImGui::Render();
}

static void Click(float x, float y) {
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(x,y);
    Frame();
    io.AddMouseButtonEvent(0,true);
    Frame();
    io.AddMouseButtonEvent(0,false);
    Frame();
}

static bool CheckControls() {
    Frame(); Frame();
    auto menu = ImGui::FindWindowByName("##IstarothMenuModal");
    if (!menu) return false;
    const ImVec2 p = menu->Pos;
    auto check = [&](float x, float y, UiCommand command, int value) {
        unsigned before = command_count;
        Click(p.x+x,p.y+y);
        bool ok = command_count==before+1 && last_command==command && last_value==value;
        if (!ok) fprintf(stderr,"control at %.0f,%.0f expected (%d,%d), got (%d,%d), events=%u\n",
                         x,y,int(command),value,int(last_command),last_value,command_count-before);
        return ok;
    };
    if (!check(422,96,UiCommand::Enabled,1) || !check(422,96,UiCommand::Enabled,0)) return false;
    for (int i=0; i<4; ++i)
        if (!check(46+i*136.f,230,UiCommand::Multiplier,i+1)) return false;
    for (int i=0; i<4; ++i)
        if (!check(46+i*136.f,200,UiCommand::Multiplier,i+1)) return false;
    const UiCommand rows[] = {UiCommand::Advance,UiCommand::ChooseFirst,UiCommand::SkipCooldown};
    for (int i=0; i<3; ++i) {
        if (!check(250,301+i*66.f,rows[i],0) || !check(250,301+i*66.f,rows[i],1)) return false;
    }
    auto before = command_count;
    Click(p.x+473,p.y+23);
    if (open || command_count!=before) return false;
    Frame();
    auto hud = ImGui::FindWindowByName("##IstarothHudCapsule");
    if (!hud) return false;
    Click(hud->Pos.x+20,hud->Pos.y+18);
    return open && command_count==before;
}

static void Measure(bool menu_open) {
    open = menu_open;
    using Clock = std::chrono::steady_clock;
    double samples[19];
    for (int round=-1; round<19; ++round) {
        auto start = Clock::now();
        for (int frame=0; frame<100; ++frame) Frame();
        if (round>=0) samples[round]=std::chrono::duration<double,std::milli>(Clock::now()-start).count()/100;
    }
    std::sort(samples,samples+19);
    printf("%s_ms=%.6f\n",open?"menu":"hud",samples[9]);
}

static LRESULT CALLBACK WindowProc(HWND window, UINT msg, WPARAM key, LPARAM data) {
    if (msg==WM_KEYDOWN && !(data & (1L<<30))) {
        if (key==VK_INSERT) { open=!open; return 0; }
        if (key==VK_F10) { Apply(UiCommand::Emergency,0); return 0; }
    }
    if (ImGui_ImplWin32_WndProcHandler(window,msg,key,data)) return 1;
    if (msg==WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window,msg,key,data);
}

static HRESULT Preview(bool smoke) {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW type{};
    type.hInstance=instance;
    type.lpszClassName=L"Istaroth.OfflineMenuReview";
    type.lpfnWndProc=WindowProc;
    type.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if (!RegisterClassW(&type)) return HRESULT_FROM_WIN32(GetLastError());
    const DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
    RECT rect{0,0,width,height};
    AdjustWindowRect(&rect,style,FALSE);
    HWND window=CreateWindowW(type.lpszClassName,L"伊斯塔露 · 离线菜单验收",style,CW_USEDEFAULT,CW_USEDEFAULT,
                              rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,nullptr);
    if (!window) return HRESULT_FROM_WIN32(GetLastError());
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount=1; desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.SampleDesc.Count=1;
    desc.Windowed=TRUE; desc.OutputWindow=window;
    ComPtr<IDXGISwapChain> chain;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> buffer;
    ComPtr<ID3D11RenderTargetView> target;
    HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
                            D3D11_SDK_VERSION,&desc,&chain,&device,nullptr,&context);
    if (SUCCEEDED(hr)) hr=chain->GetBuffer(0,IID_PPV_ARGS(&buffer));
    if (SUCCEEDED(hr)) hr=device->CreateRenderTargetView(buffer.Get(),nullptr,&target);
    if (FAILED(hr)) { DestroyWindow(window); return hr; }
    bool platform=ImGui_ImplWin32_Init(window);
    bool graphics=platform && ImGui_ImplDX11_Init(device.Get(),context.Get());
    if (!graphics) hr=E_FAIL;
    ComPtr<ID3D11ShaderResourceView> icon;
#ifdef ISTAROTH_EMBED_ICON
    if (graphics) hr=LoadResidentIconWithFallback(device.Get(),L"assets\\istaroth-mark.png",&icon);
#endif
    sample.icon_texture=(uint64_t)(uintptr_t)icon.Get();
    if (!smoke && SUCCEEDED(hr)) ShowWindow(window,SW_SHOWNORMAL);
    float opacity=1.f;
    MSG message{};
    while (SUCCEEDED(hr) && message.message!=WM_QUIT) {
        if (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message); continue;
        }
        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame();
        opacity=StepMenuOpacity(opacity,open,ImGui::GetIO().DeltaTime);
        Frame(opacity);
        auto view=target.Get();
        const float clear[]={.08f,.15f,.24f,1};
        context->OMSetRenderTargets(1,&view,nullptr);
        context->ClearRenderTargetView(view,clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        hr=chain->Present(1,0);
        if (smoke) break;
        if (hr==DXGI_STATUS_OCCLUDED) Sleep(50);
    }
    if (graphics) ImGui_ImplDX11_Shutdown();
    if (platform) ImGui_ImplWin32_Shutdown();
    if (IsWindow(window)) DestroyWindow(window);
    return hr;
}

int wmain(int argc,wchar_t** argv) {
    std::wstring mode=argc>1?argv[1]:L"";
    if (argc>2 || (!mode.empty() && mode!=L"--test" && mode!=L"--bench" && mode!=L"--smoke")) return 2;
    if (FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED))) return 1;
    ImGui::CreateContext();
    auto& io=ImGui::GetIO();
    io.IniFilename=io.LogFilename=nullptr;
    io.DisplaySize=ImVec2(width,height); io.DeltaTime=1.f/60;
    wchar_t windows[MAX_PATH];
    GetWindowsDirectoryW(windows,MAX_PATH);
    std::wstring font=std::wstring(windows)+L"\\Fonts\\msyh.ttc";
    char utf8[2048];
    ImFontConfig config;
    config.OversampleH=2; config.OversampleV=2; config.PixelSnapH=true;
    bool ok=WideCharToMultiByte(CP_UTF8,0,font.c_str(),-1,utf8,sizeof(utf8),nullptr,nullptr)>0 &&
            io.Fonts->AddFontFromFileTTF(utf8,16.f,&config,ResidentGlyphs) && io.Fonts->Build();
    sample.phase=UiPhase::Paused;
    sample.actual_known=sample.initialized=sample.foreground=true;
    if (ok) {
        if (mode==L"--test") ok=CheckControls();
        else if (mode==L"--bench") { Measure(false); Measure(true); }
        else ok=SUCCEEDED(Preview(mode==L"--smoke"));
    }
    ImGui::DestroyContext(); CoUninitialize();
    if (mode==L"--test") printf("menu controls: %s\n",ok?"passed":"failed");
    return ok?0:1;
}
