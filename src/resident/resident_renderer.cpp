// In-process layered UI. Rendering and input belong to its own Win32 thread.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "resident_internal.h"
#include "resident_glyphs.h"
#include "resident_icon.h"
#include "resident_embedded_icon.h"
#include "resident_cursor.h"
using Microsoft::WRL::ComPtr;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);

namespace istaroth {
static HWND volatile overlay=nullptr;
static HWND owner=nullptr;
static DWORD ui_thread=0;
static MenuCursor menu_cursor;
constexpr UINT MenuMessage=WM_APP+0x4a1;
bool RendererOwnsForeground(){return overlay&&GetForegroundWindow()==overlay;}
static void ApplyMenu(bool open) {
    if(!overlay||open==(MenuVisible!=0))return;
    if(open&&GetForegroundWindow()!=owner&&GetForegroundWindow()!=overlay)return;
    if(open&&!menu_cursor.Save()){TraceRuntime("menu cursor snapshot failed; menu stays closed");return;}
    InterlockedExchange(&MenuVisible,open?1:0);
    LONG_PTR style=WS_EX_LAYERED|WS_EX_TOOLWINDOW;
    if(!open)style|=WS_EX_TRANSPARENT|WS_EX_NOACTIVATE;
    SetWindowLongPtrW(overlay,GWL_EXSTYLE,style);
    if(open) {
        // Release the owner's capture on its own thread before activating our window.
        DWORD_PTR ignored=0;
        SendMessageTimeoutW(owner,WM_CANCELMODE,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,100,&ignored);
        ShowWindow(overlay,SW_SHOWNORMAL);SetForegroundWindow(overlay);SetFocus(overlay);
        ReleaseCapture();
        if(GetForegroundWindow()!=overlay||!menu_cursor.Show()) {
            TraceRuntime("menu cursor takeover failed; restoring previous state");ApplyMenu(false);
        }
    } else {
        ReleaseCapture();
        if(ImGui::GetCurrentContext())ImGui::GetIO().ClearInputKeys();
        if(GetForegroundWindow()==overlay) {
            ClipCursor(nullptr);
            if(IsWindow(owner))SetForegroundWindow(owner);
        }
        menu_cursor.Restore(GetForegroundWindow()==owner&&IsWindow(owner)&&!IsIconic(owner));
    }
}
void SetMenuVisible(bool open) {
    HWND window=overlay;if(!window)return;
    if(GetCurrentThreadId()==ui_thread)ApplyMenu(open);else PostMessageW(window,MenuMessage,open?1:0,0);
}
static LRESULT CALLBACK Window(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(message==MenuMessage){ApplyMenu(w!=0);return 0;}
    if(message==WM_KILLFOCUS||message==WM_CLOSE){ApplyMenu(false);return 0;}
    if((message==WM_KEYDOWN||message==WM_SYSKEYDOWN)&&w==VK_F10){EmitCommand(UiCommand::Emergency,0);return 0;}
    if((message==WM_KEYDOWN||message==WM_SYSKEYDOWN)&&w==VK_ESCAPE){ApplyMenu(false);return 0;}
    // The resident samples Insert once, including while the owner has focus.
    if((message==WM_KEYDOWN||message==WM_SYSKEYDOWN||message==WM_KEYUP||message==WM_SYSKEYUP)&&w==VK_INSERT)return 0;
    if(ImGui::GetCurrentContext())ImGui_ImplWin32_WndProcHandler(window,message,w,l);
    if(message==WM_SETCURSOR&&MenuVisible){SetCursor(LoadCursorW(nullptr,IDC_ARROW));return TRUE;}
    if(message==WM_ERASEBKGND)return 1;
    return DefWindowProcW(window,message,w,l);
}
static void Check(HRESULT value){if(FAILED(value))throw value;}
struct Surface {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> texture,staging;ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11ShaderResourceView> icon;
    unsigned width=0,height=0,crop_width=0,crop_height=0;
    HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;void* pixels=nullptr;
    ~Surface(){ResetBitmap();}
    void ResetBitmap(){if(dc){if(previous)SelectObject(dc,previous);if(bitmap)DeleteObject(bitmap);DeleteDC(dc);}dc=nullptr;bitmap=nullptr;previous=nullptr;pixels=nullptr;}
    void Initialize() {
        HRESULT result=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
        if(FAILED(result))result=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
        Check(result);
    }
    void Resize(unsigned w,unsigned h,unsigned cw,unsigned ch) {
        if(width!=w||height!=h) {
            context->OMSetRenderTargets(0,nullptr,nullptr);target.Reset();texture.Reset();
            D3D11_TEXTURE2D_DESC d={};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.SampleDesc.Count=1;
            d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.BindFlags=D3D11_BIND_RENDER_TARGET;
            Check(device->CreateTexture2D(&d,nullptr,&texture));Check(device->CreateRenderTargetView(texture.Get(),nullptr,&target));width=w;height=h;
        }
        if(crop_width!=cw||crop_height!=ch) {
            staging.Reset();ResetBitmap();D3D11_TEXTURE2D_DESC d={};texture->GetDesc(&d);
            d.Width=cw;d.Height=ch;d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            Check(device->CreateTexture2D(&d,nullptr,&staging));
            BITMAPINFO info={};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=(LONG)cw;info.bmiHeader.biHeight=-(LONG)ch;
            info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            dc=CreateCompatibleDC(nullptr);bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
            if(!dc||!bitmap||!pixels)throw HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY);
            previous=SelectObject(dc,bitmap);crop_width=cw;crop_height=ch;
        }
    }
    void Present(unsigned x,unsigned y,POINT position) {
        D3D11_BOX box={x,y,0,x+crop_width,y+crop_height,1};context->CopySubresourceRegion(staging.Get(),0,0,0,0,texture.Get(),0,&box);
        D3D11_MAPPED_SUBRESOURCE mapped={};Check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
        for(unsigned row=0;row<crop_height;row++)memcpy((BYTE*)pixels+row*crop_width*4,(BYTE*)mapped.pData+row*mapped.RowPitch,crop_width*4);
        context->Unmap(staging.Get(),0);
        POINT source={};SIZE size={(LONG)crop_width,(LONG)crop_height};BLENDFUNCTION blend={AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        if(!UpdateLayeredWindow(overlay,nullptr,&position,&size,dc,&source,0,&blend,ULW_ALPHA)) {
            DWORD error=GetLastError();throw HRESULT_FROM_WIN32(error?error:ERROR_GEN_FAILURE);
        }
    }
};
static DWORD WINAPI RunUi(void* value) {
    owner=(HWND)value;ui_thread=GetCurrentThreadId();SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HRESULT com_status=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    bool platform=false,graphics=false;
    try {
        WNDCLASSW wc={};wc.lpfnWndProc=Window;wc.hInstance=ResidentModule;wc.lpszClassName=L"Istaroth.ResidentMenu";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw HRESULT_FROM_WIN32(GetLastError());
        overlay=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,wc.lpszClassName,L"Istaroth · 游戏内菜单",WS_POPUP,0,0,1,1,owner,nullptr,ResidentModule,nullptr);
        if(!overlay)throw HRESULT_FROM_WIN32(GetLastError());
        Surface surface;surface.Initialize();
        HRESULT icon_status=
#ifdef ISTAROTH_EMBED_ICON
            LoadResidentIconWithFallback
#else
            LoadResidentIcon
#endif
            (surface.device.Get(),(DataDirectory+L"assets\\istaroth-mark.png").c_str(),&surface.icon);
        if(SUCCEEDED(icon_status))TraceRuntime("static PNG icon loaded on private UI device");
        else TraceRuntime("static PNG icon missing or invalid; using static fallback");
        ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
        io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NoMouseCursorChange;ImGui::StyleColorsDark();
        wchar_t fonts[MAX_PATH];GetWindowsDirectoryW(fonts,MAX_PATH);std::wstring directory=std::wstring(fonts)+L"\\Fonts\\";
        bool loaded=false;for(auto name:{L"msyh.ttc",L"simhei.ttf",L"msyh.ttf"}) {
            auto path=directory+name;if(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES)continue;
            char utf8[2048];if(!WideCharToMultiByte(CP_UTF8,0,path.c_str(),-1,utf8,2048,nullptr,nullptr))continue;
            if(io.Fonts->AddFontFromFileTTF(utf8,20.f,nullptr,ResidentGlyphs)){loaded=true;break;}
        }
        if(!loaded)throw HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        platform=ImGui_ImplWin32_Init(overlay);if(!platform)throw E_FAIL;
        graphics=ImGui_ImplDX11_Init(surface.device.Get(),surface.context.Get());if(!graphics)throw E_FAIL;
        TraceRuntime("layered UI initialized in game process; private DX11 surface and Win32 window ready");
        while(IsWindow(owner)) {
            MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
            HWND foreground=GetForegroundWindow();bool visible=IsWindowVisible(owner)&&!IsIconic(owner)&&(foreground==owner||foreground==overlay);
            if(!visible){ApplyMenu(false);ShowWindow(overlay,SW_HIDE);Sleep(20);continue;}
            if(MenuVisible&&foreground==overlay&&!menu_cursor.Show()) {
                TraceRuntime("menu cursor refresh failed; restoring previous state");ApplyMenu(false);
            }
            RECT client;POINT origin={};if(!GetClientRect(owner,&client)||!ClientToScreen(owner,&origin)||client.right<=0||client.bottom<=0){Sleep(20);continue;}
            unsigned w=(unsigned)client.right,h=(unsigned)client.bottom;if(w>8192||h>8192)throw HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
            float scale=std::clamp(std::min(w/1920.f,h/1080.f),.65f,2.f);bool open=MenuVisible!=0;
            // Only the small HUD rectangle crosses the GPU/CPU boundary while closed.
            unsigned crop_w=open?w:std::min(w,(unsigned)(340*scale+8));
            unsigned crop_h=open?h:std::min(h,(unsigned)(80*scale+8));unsigned crop_x=(w-crop_w)/2;
            surface.Resize(w,h,crop_w,crop_h);
            ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();io.DisplaySize=ImVec2((float)w,(float)h);io.DisplayFramebufferScale=ImVec2(1,1);
            if(open){POINT point;if(GetCursorPos(&point))io.AddMousePosEvent((float)(point.x-origin.x),(float)(point.y-origin.y));}
            ImGui::NewFrame();UiState snapshot=GetUiState();snapshot.display_width=(float)w;snapshot.display_height=(float)h;snapshot.backdrop=0;
            snapshot.icon_texture=(uint64_t)(uintptr_t)surface.icon.Get();
            DrawResidentUi(snapshot,open,scale,EmitCommand);if(open!=(MenuVisible!=0))ApplyMenu(open);
            ImGui::Render();auto target=surface.target.Get();surface.context->OMSetRenderTargets(1,&target,nullptr);
            // Alpha 1 captures clicks outside the open panel; the closed HUD is click-through.
            const float clear[4]={0,0,0,MenuVisible?1.f/255.f:0};surface.context->ClearRenderTargetView(target,clear);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());POINT position={origin.x+(LONG)crop_x,origin.y};surface.Present(crop_x,0,position);
            ShowWindow(overlay,SW_SHOWNOACTIVATE);RendererTick();Sleep(20);
        }
    }catch(HRESULT error) {
        char text[160];sprintf_s(text,"layered UI failed HRESULT=0x%08lx",error);SetRuntimeError((DWORD)error,text);
    }catch(...){SetRuntimeError(ERROR_UNHANDLED_EXCEPTION,"layered UI exception");}
    ApplyMenu(false);InterlockedExchange(&RendererReady,0);
    if(graphics)ImGui_ImplDX11_Shutdown();if(platform)ImGui_ImplWin32_Shutdown();if(ImGui::GetCurrentContext())ImGui::DestroyContext();
    if(overlay)DestroyWindow(overlay);overlay=nullptr;
    if(SUCCEEDED(com_status))CoUninitialize();return 0;
}
bool InstallRenderer() {
    HANDLE thread=CreateThread(nullptr,0,RunUi,GameWindow,0,nullptr);if(!thread)return false;CloseHandle(thread);return true;
}
}
