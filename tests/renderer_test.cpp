// Real WARP swap chains in hidden owned windows. Cursor/focus calls are simulated.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstdlib>
#include "diagnostics.h"
static unsigned checks=0,ticks=0,original_keys=0,global_writes=0,activation_calls=0;
static DWORD renderer_error=0;
static HWND simulated_foreground=nullptr,captured=nullptr;
static RECT clip{10,20,30,40};static POINT cursor_position{15,25};static int cursor_count=-2;
static HWND FakeForeground(){return simulated_foreground;}
static BOOL FakeActivate(HWND){activation_calls++;return FALSE;}
static BOOL FakeClip(const RECT* value){global_writes++;clip=value?*value:RECT{0,0,1920,1080};return TRUE;}
static BOOL FakeGetClip(RECT* value){*value=clip;return TRUE;}
static BOOL FakeGetPosition(POINT* value){*value=cursor_position;return TRUE;}
static BOOL FakeSetPosition(int x,int y){global_writes++;cursor_position={x,y};return TRUE;}
static BOOL FakeCursorInfo(CURSORINFO* value){value->hCursor=(HCURSOR)(ULONG_PTR)1;return TRUE;}
static int FakeShow(BOOL show){cursor_count+=show?1:-1;return cursor_count;}
static HCURSOR FakeCursor(HCURSOR value){return value;}
static BOOL FakeReleaseCapture(){captured=nullptr;return TRUE;}
static HWND FakeCapture(HWND value){HWND old=captured;captured=value;return old;}
#define GetForegroundWindow FakeForeground
#define SetForegroundWindow FakeActivate
#define ClipCursor FakeClip
#define GetClipCursor FakeGetClip
#define GetCursorPos FakeGetPosition
#define SetCursorPos FakeSetPosition
#define GetCursorInfo FakeCursorInfo
#define ShowCursor FakeShow
#define SetCursor FakeCursor
#define ReleaseCapture FakeReleaseCapture
#define SetCapture FakeCapture
#include "resident_renderer.cpp"
#undef SetCapture
#undef ReleaseCapture
#undef SetCursor
#undef ShowCursor
#undef GetCursorInfo
#undef SetCursorPos
#undef GetCursorPos
#undef GetClipCursor
#undef ClipCursor
#undef SetForegroundWindow
#undef GetForegroundWindow
namespace istaroth {
HMODULE ResidentModule=nullptr;HWND GameWindow=nullptr;std::wstring DataDirectory;
volatile LONG MenuVisible=0,RendererReady=0,BackdropReady=0;alignas(8) volatile LONG64 LastPresent=0;
void TraceRuntime(const char* event){diagnostics::Append(DataDirectory,L"owned-renderer.log",event);}
void SetRuntimeError(DWORD error,const char* event){renderer_error=error;TraceRuntime(event);}
void EmitCommand(UiCommand,int){}
UiState GetUiState(){UiState state;state.phase=UiPhase::Paused;state.initialized=true;state.actual_known=true;return state;}
void RendererTick(){ticks++;InterlockedExchange(&RendererReady,1);InterlockedExchange64(&LastPresent,GetTickCount64());}
}
static void Check(bool value){checks++;if(!value){fprintf(stderr,"renderer check %u failed error=0x%08lx\n",checks,renderer_error);exit(1);}}
static LRESULT CALLBACK Host(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_KEYDOWN){original_keys++;return 0;}return DefWindowProcW(window,message,w,l);
}
static void Pump() {
    MSG message;while(PeekMessageW(&message,istaroth::GameWindow,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
}
int main() {
    using namespace istaroth;
    ResidentModule=GetModuleHandleW(nullptr);wchar_t executable[MAX_PATH];GetModuleFileNameW(nullptr,executable,MAX_PATH);
    DataDirectory=executable;DataDirectory.resize(DataDirectory.find_last_of(L"\\/")+1);
    WNDCLASSW type{};type.lpfnWndProc=Host;type.hInstance=ResidentModule;type.lpszClassName=L"Istaroth.OwnedDxgiHost";
    Check(RegisterClassW(&type)!=0);GameWindow=CreateWindowW(type.lpszClassName,L"Owned hidden renderer fixture",WS_OVERLAPPEDWINDOW,0,0,1280,800,nullptr,nullptr,ResidentModule,nullptr);
    Check(GameWindow!=nullptr);simulated_foreground=GameWindow;Check(Install(nullptr)==0);
    DXGI_SWAP_CHAIN_DESC description{};description.BufferDesc.Width=1280;description.BufferDesc.Height=800;
    description.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;description.BufferCount=2;description.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow=GameWindow;description.SampleDesc.Count=1;description.Windowed=TRUE;description.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    ComPtr<IDXGISwapChain> chain;ComPtr<ID3D11Device> host_device;ComPtr<ID3D11DeviceContext> host_context;
    auto create=[&]{Check(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&description,&chain,&host_device,nullptr,&host_context)));};
    create();Check(FindCallbacks(chain.Get())!=nullptr);
    ComPtr<ID3D11Texture2D> buffer;ComPtr<ID3D11RenderTargetView> view;
    Check(SUCCEEDED(chain->GetBuffer(0,IID_PPV_ARGS(&buffer))));Check(SUCCEEDED(host_device->CreateRenderTargetView(buffer.Get(),nullptr,&view)));
    auto render_target=view.Get();host_context->OMSetRenderTargets(1,&render_target,nullptr);
    const char shader_source[]="[numthreads(1,1,1)] void main(uint3 id:SV_DispatchThreadID){}";
    ComPtr<ID3DBlob> shader_blob;Check(SUCCEEDED(D3DCompile(shader_source,sizeof(shader_source)-1,nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&shader_blob,nullptr)));
    ComPtr<ID3D11ComputeShader> compute;Check(SUCCEEDED(host_device->CreateComputeShader(shader_blob->GetBufferPointer(),shader_blob->GetBufferSize(),nullptr,&compute)));host_context->CSSetShader(compute.Get(),nullptr,0);
    D3D11_VIEWPORT viewport{3,4,100,110,0,1};host_context->RSSetViewports(1,&viewport);host_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    Check(SUCCEEDED(chain->Present(0,0)));Check(RendererReady&&ticks==1&&!renderer_error);
    int hud_vertices=ImGui::GetDrawData()->TotalVtxCount;Check(hud_vertices>0);
    ComPtr<ID3D11RenderTargetView> restored;host_context->OMGetRenderTargets(1,&restored,nullptr);Check(restored.Get()==view.Get());
    ComPtr<ID3D11ComputeShader> restored_compute;host_context->CSGetShader(&restored_compute,nullptr,nullptr);Check(restored_compute.Get()==compute.Get());
    D3D11_PRIMITIVE_TOPOLOGY topology{};host_context->IAGetPrimitiveTopology(&topology);Check(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    UINT viewport_count=1;D3D11_VIEWPORT restored_viewport{};host_context->RSGetViewports(&viewport_count,&restored_viewport);Check(restored_viewport.TopLeftX==3&&restored_viewport.Height==110);
    unsigned previous_ticks=ticks;Check(SUCCEEDED(chain->Present(0,DXGI_PRESENT_TEST)));Check(ticks==previous_ticks);
    SendMessageW(GameWindow,WM_KEYDOWN,'A',0);Check(original_keys==1);
    SetMenuVisible(true);Pump();Check(MenuVisible&&cursor_count>=0&&simulated_foreground==GameWindow);Check(activation_calls==0);
    {
        const float background[]={.1f,.2f,.3f,1.f};host_context->ClearRenderTargetView(view.Get(),background);
        Render(chain.Get(),0);Check(!renderer_error&&ImGui::GetDrawData()->TotalVtxCount>hud_vertices);
        D3D11_TEXTURE2D_DESC readback{};buffer->GetDesc(&readback);readback.Usage=D3D11_USAGE_STAGING;
        readback.BindFlags=readback.MiscFlags=0;readback.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> image;Check(SUCCEEDED(host_device->CreateTexture2D(&readback,nullptr,&image)));
        host_context->CopyResource(image.Get(),buffer.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(SUCCEEDED(host_context->Map(image.Get(),0,D3D11_MAP_READ,0,&mapped)));
        auto corner=(const BYTE*)mapped.pData+(readback.Height-1)*mapped.RowPitch;
        Check(abs((int)corner[0]-26)<=1&&abs((int)corner[1]-51)<=1&&abs((int)corner[2]-77)<=1&&corner[3]==255);
        host_context->Unmap(image.Get(),0);Check(SUCCEEDED(chain->Present(0,0)));
    }
    SendMessageW(GameWindow,WM_KEYDOWN,'B',0);Check(original_keys==1);
    SetMenuVisible(false);Pump();Check(!MenuVisible&&cursor_count==-2&&clip.left==10&&cursor_position.x==15);Check(input_count==0);
    SetMenuVisible(true);Pump();Check(MenuVisible);simulated_foreground=(HWND)(ULONG_PTR)123;clip={100,100,200,200};cursor_position={150,150};
    unsigned writes=global_writes;SendMessageW(GameWindow,WM_KILLFOCUS,0,0);Check(!MenuVisible&&cursor_count==-2);Check(global_writes==writes&&clip.left==100&&cursor_position.x==150);
    simulated_foreground=GameWindow;Check(SUCCEEDED(chain->Present(0,0)));Pump();
    host_context->OMSetRenderTargets(0,nullptr,nullptr);restored.Reset();view.Reset();buffer.Reset();
    Check(SUCCEEDED(chain->ResizeBuffers(2,1024,768,DXGI_FORMAT_UNKNOWN,0)));Check(SUCCEEDED(chain->Present(0,0)));Check(RendererReady&&!renderer_error);
    ObservePresent(chain.Get(),DXGI_ERROR_DEVICE_REMOVED);Check(!RendererReady);Pump();
    Check(SUCCEEDED(chain->Present(0,0)));Check(RendererReady&&!renderer_error);
    ComPtr<IDXGISwapChain1> newer;if(SUCCEEDED(chain.As(&newer))) {
        auto entry=FindCallbacks(chain.Get());if(entry&&entry->present1) {DXGI_PRESENT_PARAMETERS parameters{};previous_ticks=ticks;Check(SUCCEEDED(newer->Present1(0,DXGI_PRESENT_TEST,&parameters)));Check(ticks==previous_ticks);}
        newer.Reset();
    }
    chain.Reset();Check(active_chain==nullptr&&!RendererReady);host_context.Reset();host_device.Reset();Pump();
    create();Check(SUCCEEDED(chain->Present(0,0)));Check(RendererReady&&!renderer_error);chain.Reset();host_context.Reset();host_device.Reset();Pump();
    auto slots=(void**)VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);Check(slots!=nullptr);slots[0]=(void*)(ULONG_PTR)1;
    DWORD old=0;Check(VirtualProtect(slots,4096,PAGE_READONLY,&old)!=FALSE);Check(Patch(slots,(void*)(ULONG_PTR)1,(void*)(ULONG_PTR)2)==0);
    Check(Patch(slots,(void*)(ULONG_PTR)1,(void*)(ULONG_PTR)3)==ERROR_BUSY);Check(slots[0]==(void*)(ULONG_PTR)2);
    MEMORY_BASIC_INFORMATION memory{};Check(VirtualQuery(slots,&memory,sizeof(memory))!=0&&memory.Protect==PAGE_READONLY);Check(Patch(nullptr,nullptr,nullptr)!=0);
    Check(VirtualFree(slots,0,MEM_RELEASE)!=FALSE);Check(activation_calls==0);DestroyWindow(GameWindow);GameWindow=nullptr;
    printf("{\"passed\":true,\"checks\":%u,\"desktop_input_sent\":false,\"game_accessed\":false,\"exclusive_display_tested\":false,\"scope\":\"owned WARP frame callbacks, pipeline restore, menu input, cursor, resize, device-loss simulation and chain recreation\"}\n",checks);
}
