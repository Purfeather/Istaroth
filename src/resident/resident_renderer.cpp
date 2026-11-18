// Draw into the owner's DX11 swap chain; opening the menu never activates a window.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <memory>
#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "resident_internal.h"
#include "resident_glyphs.h"
#include "resident_icon.h"
#include "resident_embedded_icon.h"
#include "resident_cursor.h"
#include "resident_material.h"
using Microsoft::WRL::ComPtr;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);

namespace istaroth {
using PresentFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
using Present1Fn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*,UINT,UINT,const DXGI_PRESENT_PARAMETERS*);
using ResizeFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT,UINT,DXGI_FORMAT,UINT);
using ReleaseFn=ULONG(STDMETHODCALLTYPE*)(IDXGISwapChain*);
struct Callbacks {void** table=nullptr;PresentFn present=nullptr;Present1Fn present1=nullptr;ResizeFn resize=nullptr;ReleaseFn release=nullptr;};
static Callbacks callbacks[8];static volatile LONG callback_count=0;
static std::recursive_mutex frame_lock;
static std::atomic<HWND> owner{nullptr};
static WNDPROC previous_window=nullptr;
static MenuCursor menu_cursor;
constexpr UINT MenuMessage=WM_APP+0x4a1,CursorMessage=WM_APP+0x4a2;
static volatile LONG cursor_pending=0,clear_input=0;
struct Input {UINT message;WPARAM w;LPARAM l;};
static SRWLOCK input_lock=SRWLOCK_INIT;
static Input inputs[256];static unsigned input_count=0;
static IDXGISwapChain* active_chain=nullptr; // Borrowed; Release callback clears it before returning zero.
static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;
static ComPtr<ID3D11RenderTargetView> target;
static ComPtr<ID3D11ShaderResourceView> icon;
static SceneBlur scene_blur;
static bool blur_failure_logged=false;
static float menu_opacity=0;
static bool platform=false,graphics=false;
static int last_fullscreen=-1;static unsigned last_width=0,last_height=0;
static thread_local bool in_present=false;

static void Check(HRESULT result){if(FAILED(result))throw result;}
static void TraceCode(const char* event,HRESULT code) {char line[160];sprintf_s(line,"event=%s hresult=0x%08lx",event,code);TraceRuntime(line);}
bool RendererOwnsForeground(){return false;}
static void ApplyMenu(bool open) {
    HWND window=owner.load();if(!window||open==(MenuVisible!=0))return;
    if(open&&(GetForegroundWindow()!=window||!RendererReady||IsIconic(window)))return;
    if(open&&!menu_cursor.Save()){TraceRuntime("event=menu_cursor_snapshot_failed");return;}
    InterlockedExchange(&MenuVisible,open?1:0);
    if(open) {
        if(previous_window)CallWindowProcW(previous_window,window,WM_CANCELMODE,0,0);
        ReleaseCapture();
        if(!menu_cursor.Show()){TraceRuntime("event=menu_cursor_takeover_failed");ApplyMenu(false);return;}
        TraceRuntime("event=menu_opened backend=dxgi focus=game_window");
    } else {
        ReleaseCapture();InterlockedExchange(&clear_input,1);
        AcquireSRWLockExclusive(&input_lock);input_count=0;ReleaseSRWLockExclusive(&input_lock);
        menu_cursor.Restore(IsWindow(window)&&GetForegroundWindow()==window&&!IsIconic(window));
        TraceRuntime("event=menu_closed backend=dxgi");
    }
}
void SetMenuVisible(bool open) {if(HWND window=owner.load())PostMessageW(window,MenuMessage,open?1:0,0);}
static bool IsInput(UINT message) {
    return (message>=WM_MOUSEFIRST&&message<=WM_MOUSELAST)||message==WM_MOUSELEAVE||
        message==WM_KEYDOWN||message==WM_KEYUP||message==WM_SYSKEYDOWN||message==WM_SYSKEYUP||message==WM_CHAR||
        message==WM_SETFOCUS||message==WM_KILLFOCUS;
}
static LRESULT CALLBACK Window(HWND window,UINT message,WPARAM w,LPARAM l) {
    WNDPROC original=previous_window;
    if(message==MenuMessage){ApplyMenu(w!=0);return 0;}
    if(message==CursorMessage) {
        InterlockedExchange(&cursor_pending,0);
        if(MenuVisible&&GetForegroundWindow()==window&&!menu_cursor.Show())ApplyMenu(false);
        return 0;
    }
    if(message==WM_KILLFOCUS||message==WM_CLOSE||message==WM_NCDESTROY||(message==WM_ACTIVATEAPP&&!w))ApplyMenu(false);
    if((message==WM_KEYDOWN||message==WM_SYSKEYDOWN)&&w==VK_F10){EmitCommand(UiCommand::Emergency,0);return 0;}
    if((message==WM_KEYDOWN||message==WM_SYSKEYDOWN)&&w==VK_ESCAPE&&MenuVisible){ApplyMenu(false);return 0;}
    if((message==WM_KEYDOWN||message==WM_KEYUP||message==WM_SYSKEYDOWN||message==WM_SYSKEYUP)&&w==VK_INSERT)return 0;
    if((MenuVisible||message==WM_KILLFOCUS||message==WM_SETFOCUS)&&IsInput(message)) {
        AcquireSRWLockExclusive(&input_lock);
        bool overflow=input_count==256;if(!overflow)inputs[input_count++]={message,w,l};
        ReleaseSRWLockExclusive(&input_lock);
        if(overflow){TraceRuntime("event=menu_input_overflow; closing menu");ApplyMenu(false);}
    }
    if(MenuVisible) {
        if(message==WM_SETCURSOR){SetCursor(LoadCursorW(nullptr,IDC_ARROW));return TRUE;}
        if(message==WM_INPUT)return DefWindowProcW(window,message,w,l);
        if(IsInput(message)) {
            if(message==WM_LBUTTONDOWN||message==WM_RBUTTONDOWN||message==WM_MBUTTONDOWN)SetCapture(window);
            if(message==WM_LBUTTONUP||message==WM_RBUTTONUP||message==WM_MBUTTONUP)ReleaseCapture();
            return 0;
        }
    }
    LRESULT result=original?CallWindowProcW(original,window,message,w,l):DefWindowProcW(window,message,w,l);
    if(message==WM_NCDESTROY){owner.store(nullptr);previous_window=nullptr;}
    return result;
}
static void InstallInput(HWND window) {
    if(owner.load()==window&&previous_window)return;
    if(owner.load()&&IsWindow(owner.load()))throw HRESULT_FROM_WIN32(ERROR_BUSY);
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid!=GetCurrentProcessId())throw E_ACCESSDENIED;
    previous_window=(WNDPROC)GetWindowLongPtrW(window,GWLP_WNDPROC);if(!previous_window)throw E_FAIL;
    owner.store(window);SetLastError(0);
    auto previous=(WNDPROC)SetWindowLongPtrW(window,GWLP_WNDPROC,(LONG_PTR)Window);
    if(!previous){DWORD error=GetLastError();owner.store(nullptr);previous_window=nullptr;throw HRESULT_FROM_WIN32(error?error:ERROR_GEN_FAILURE);}
    previous_window=previous;
}
static void ResetGraphics() {
    active_chain=nullptr;target.Reset();icon.Reset();scene_blur.Reset();blur_failure_logged=false;menu_opacity=0;InterlockedExchange(&BackdropReady,0);
    if(graphics)ImGui_ImplDX11_Shutdown();if(platform)ImGui_ImplWin32_Shutdown();
    graphics=platform=false;if(ImGui::GetCurrentContext())ImGui::DestroyContext();
    context.Reset();device.Reset();InterlockedExchange(&RendererReady,0);
    last_fullscreen=-1;last_width=last_height=0;
    InterlockedExchange(&clear_input,1);SetMenuVisible(false);
}
static void Initialize(IDXGISwapChain* chain,HWND window) {
    ResetGraphics();Check(chain->GetDevice(IID_PPV_ARGS(&device)));device->GetImmediateContext(&context);
    if(!context)throw E_FAIL;
    InstallInput(window);ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NoMouseCursorChange;ImGui::StyleColorsDark();
    wchar_t windows[MAX_PATH];GetWindowsDirectoryW(windows,MAX_PATH);bool loaded=false;
    DXGI_SWAP_CHAIN_DESC swap_desc{};
    float init_scale=1.0f;
    if(SUCCEEDED(chain->GetDesc(&swap_desc))&&swap_desc.BufferDesc.Width&&swap_desc.BufferDesc.Height) {
        init_scale=std::clamp(std::min(swap_desc.BufferDesc.Width/1920.f,swap_desc.BufferDesc.Height/1080.f),.65f,2.f);
    }
    ImFontConfig font_config;font_config.OversampleH=2;font_config.OversampleV=2;font_config.PixelSnapH=true;
    for(auto name:{L"msyh.ttc",L"simhei.ttf",L"msyh.ttf"}) {
        auto path=std::wstring(windows)+L"\\Fonts\\"+name;char utf8[2048];
        if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES&&WideCharToMultiByte(CP_UTF8,0,path.c_str(),-1,utf8,2048,nullptr,nullptr)&&
           io.Fonts->AddFontFromFileTTF(utf8,16.f*init_scale,&font_config,ResidentGlyphs)){loaded=true;break;}
    }
    if(!loaded)throw HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    platform=ImGui_ImplWin32_Init(window);if(!platform)throw E_FAIL;
    graphics=ImGui_ImplDX11_Init(device.Get(),context.Get());if(!graphics)throw E_FAIL;
    struct Apartment {HRESULT value=CoInitializeEx(nullptr,COINIT_MULTITHREADED);~Apartment(){if(SUCCEEDED(value))CoUninitialize();}} apartment;
    HRESULT image=
#ifdef ISTAROTH_EMBED_ICON
        LoadResidentIconWithFallback
#else
        LoadResidentIcon
#endif
        (device.Get(),(DataDirectory+L"assets\\istaroth-mark.png").c_str(),&icon);
    TraceCode("dxgi_icon",image);active_chain=chain;
    ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC gpu{};
    if(SUCCEEDED(device.As(&dxgi))&&SUCCEEDED(dxgi->GetAdapter(&adapter))&&SUCCEEDED(adapter->GetDesc(&gpu))) {
        char line[160];sprintf_s(line,"event=dxgi_adapter vendor=0x%04x device=0x%04x feature_level=0x%x",gpu.VendorId,gpu.DeviceId,(unsigned)device->GetFeatureLevel());TraceRuntime(line);
    }
    TraceRuntime("event=dxgi_initialized; game swap chain and original game window input ready");
}
// ImGui restores its own states; save the MRT and HS/DS/CS states it omits.
struct Pipeline {
    ID3D11DeviceContext* c;ID3D11RenderTargetView* rt[8]{};ID3D11DepthStencilView* depth=nullptr;
    ID3D11UnorderedAccessView* uav[64]{};UINT uav_count=8;
    ID3D11HullShader* hs=nullptr;ID3D11DomainShader* ds=nullptr;ID3D11ComputeShader* cs=nullptr;
    ID3D11ClassInstance *hi[256]{},*di[256]{},*ci[256]{};UINT hn=256,dn=256,cn=256;
    ID3D11VertexShader* vs=nullptr;ID3D11PixelShader* ps=nullptr;ID3D11GeometryShader* gs=nullptr;
    ID3D11ClassInstance *vi[256]{},*pi[256]{},*gi[256]{};UINT vn=256,pn=256,gn=256;
    ID3D11ShaderResourceView* image=nullptr;ID3D11SamplerState* sampler=nullptr;ID3D11Buffer* constants=nullptr;
    ID3D11BlendState* blend=nullptr;FLOAT blend_factor[4]{};UINT mask=0,reference=0;
    ID3D11DepthStencilState* depth_state=nullptr;ID3D11RasterizerState* raster=nullptr;ID3D11InputLayout* layout=nullptr;
    D3D11_PRIMITIVE_TOPOLOGY topology{};D3D11_VIEWPORT viewports[16]{};UINT viewport_count=16;
    D3D11_RECT scissors[16]{};UINT scissor_count=16;
    explicit Pipeline(ID3D11DeviceContext* value):c(value) {
        uav_count=device->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1?64u:8u;
        c->OMGetRenderTargets(8,rt,&depth);c->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,uav_count,uav);
        c->HSGetShader(&hs,hi,&hn);c->DSGetShader(&ds,di,&dn);c->CSGetShader(&cs,ci,&cn);
        c->VSGetShader(&vs,vi,&vn);c->PSGetShader(&ps,pi,&pn);c->GSGetShader(&gs,gi,&gn);
        c->PSGetShaderResources(0,1,&image);c->PSGetSamplers(0,1,&sampler);c->PSGetConstantBuffers(0,1,&constants);
        c->OMGetBlendState(&blend,blend_factor,&mask);c->OMGetDepthStencilState(&depth_state,&reference);
        c->RSGetState(&raster);c->RSGetViewports(&viewport_count,viewports);c->RSGetScissorRects(&scissor_count,scissors);
        c->IAGetInputLayout(&layout);c->IAGetPrimitiveTopology(&topology);
    }
    ~Pipeline() {
        UINT render_count=0;for(UINT i=0;i<8;i++)if(rt[i])render_count=i+1;
        UINT counts[64];for(auto& count:counts)count=UINT(-1);
        c->OMSetRenderTargetsAndUnorderedAccessViews(render_count,rt,depth,render_count,uav_count-render_count,uav+render_count,counts);
        c->HSSetShader(hs,hi,hn);c->DSSetShader(ds,di,dn);c->CSSetShader(cs,ci,cn);
        c->VSSetShader(vs,vi,vn);c->PSSetShader(ps,pi,pn);c->GSSetShader(gs,gi,gn);
        c->PSSetShaderResources(0,1,&image);c->PSSetSamplers(0,1,&sampler);c->PSSetConstantBuffers(0,1,&constants);
        c->OMSetBlendState(blend,blend_factor,mask);c->OMSetDepthStencilState(depth_state,reference);
        c->RSSetState(raster);c->RSSetViewports(viewport_count,viewports);c->RSSetScissorRects(scissor_count,scissors);
        c->IASetInputLayout(layout);c->IASetPrimitiveTopology(topology);
        for(auto value:rt)if(value)value->Release();if(depth)depth->Release();if(hs)hs->Release();if(ds)ds->Release();if(cs)cs->Release();
        for(UINT i=0;i<uav_count;i++)if(uav[i])uav[i]->Release();
        for(UINT i=0;i<hn;i++)if(hi[i])hi[i]->Release();for(UINT i=0;i<dn;i++)if(di[i])di[i]->Release();for(UINT i=0;i<cn;i++)if(ci[i])ci[i]->Release();
        if(vs)vs->Release();if(ps)ps->Release();if(gs)gs->Release();for(UINT i=0;i<vn;i++)if(vi[i])vi[i]->Release();for(UINT i=0;i<pn;i++)if(pi[i])pi[i]->Release();for(UINT i=0;i<gn;i++)if(gi[i])gi[i]->Release();
        if(image)image->Release();if(sampler)sampler->Release();if(constants)constants->Release();if(blend)blend->Release();if(depth_state)depth_state->Release();if(raster)raster->Release();if(layout)layout->Release();
    }
};
static void Render(IDXGISwapChain* chain,UINT flags) {
    if(flags&DXGI_PRESENT_TEST)return;
    std::lock_guard<std::recursive_mutex> lock(frame_lock);
    try {
        DXGI_SWAP_CHAIN_DESC description{};Check(chain->GetDesc(&description));
        if(!GameWindow||description.OutputWindow!=GameWindow)return;
        ComPtr<ID3D11Device> current;Check(chain->GetDevice(IID_PPV_ARGS(&current)));
        if(chain!=active_chain||current.Get()!=device.Get())Initialize(chain,description.OutputWindow);
        ComPtr<ID3D11Texture2D> buffer;Check(chain->GetBuffer(0,IID_PPV_ARGS(&buffer)));
        D3D11_TEXTURE2D_DESC dimensions{};buffer->GetDesc(&dimensions);
        BOOL fullscreen=FALSE;HRESULT mode=chain->GetFullscreenState(&fullscreen,nullptr);int exclusive=SUCCEEDED(mode)?(int)fullscreen:-1;
        if(exclusive!=last_fullscreen||dimensions.Width!=last_width||dimensions.Height!=last_height) {
            char line[160];sprintf_s(line,"event=dxgi_display exclusive=%d width=%u height=%u format=%u",exclusive,dimensions.Width,dimensions.Height,(unsigned)dimensions.Format);TraceRuntime(line);
            last_fullscreen=exclusive;last_width=dimensions.Width;last_height=dimensions.Height;
        }
        if(!target) {
            Check(CreateUiTarget(device.Get(),buffer.Get(),&target));D3D11_RENDER_TARGET_VIEW_DESC view{};target->GetDesc(&view);
            char line[128];sprintf_s(line,"event=ui_color_target source_format=%u ui_format=%u",(unsigned)dimensions.Format,(unsigned)view.Format);TraceRuntime(line);
        }
        if(!dimensions.Width||!dimensions.Height)return;
        auto pipeline=std::make_unique<Pipeline>(context.Get());
        auto& io=ImGui::GetIO();if(InterlockedExchange(&clear_input,0))io.ClearInputKeys();
        Input pending[256];unsigned count=0;
        AcquireSRWLockExclusive(&input_lock);count=input_count;memcpy(pending,inputs,count*sizeof(Input));input_count=0;ReleaseSRWLockExclusive(&input_lock);
        for(unsigned i=0;i<count;i++)ImGui_ImplWin32_WndProcHandler(owner.load(),pending[i].message,pending[i].w,pending[i].l);
        ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();
        if(io.DisplaySize.x<=0||io.DisplaySize.y<=0)return;
        io.DisplayFramebufferScale=ImVec2(1,1);
        bool open=MenuVisible!=0;float scale=std::clamp(std::min(io.DisplaySize.x/1920.f,io.DisplaySize.y/1080.f),.65f,2.f);
        menu_opacity=StepMenuOpacity(menu_opacity,open,io.DeltaTime);
        D3D11_RECT area{0,0,(LONG)dimensions.Width,(LONG)dimensions.Height};
        if(!open&&menu_opacity<=.01f) {
            LONG half=(LONG)ceilf(210*scale*dimensions.Width/io.DisplaySize.x);
            area.left=std::max(0L,(LONG)dimensions.Width/2-half);area.right=std::min((LONG)dimensions.Width,(LONG)dimensions.Width/2+half);
            area.bottom=std::min((LONG)dimensions.Height,(LONG)ceilf(100*scale*dimensions.Height/io.DisplaySize.y));
        }
        ID3D11ShaderResourceView* backdrop=nullptr;
        HRESULT blurred=scene_blur.Draw(device.Get(),context.Get(),buffer.Get(),area,1.5f*scale,&backdrop);
        InterlockedExchange(&BackdropReady,SUCCEEDED(blurred)&&backdrop?1:0);
        if(FAILED(blurred)&&!blur_failure_logged){TraceCode("blur_unavailable; using translucent tint",blurred);blur_failure_logged=true;}
        ImGui::NewFrame();
        auto snapshot=GetUiState();snapshot.display_width=io.DisplaySize.x;snapshot.display_height=io.DisplaySize.y;
        snapshot.icon_texture=(uint64_t)(uintptr_t)icon.Get();snapshot.backdrop=(uint64_t)(uintptr_t)backdrop;
        DrawResidentUi(snapshot,open,scale,EmitCommand,menu_opacity);if(open!=(MenuVisible!=0))SetMenuVisible(open);
        ImGui::Render();auto draw=ImGui::GetDrawData();
        // Keep mouse/UI coordinates in client space. The pinned DX11 backend does
        // not scale viewports, so adapt only its drawing data to the back buffer.
        ImVec2 factor(dimensions.Width/io.DisplaySize.x,dimensions.Height/io.DisplaySize.y);
        if(factor.x!=1.f||factor.y!=1.f) {
            for(auto list:draw->CmdLists)for(auto& vertex:list->VtxBuffer){vertex.pos.x*=factor.x;vertex.pos.y*=factor.y;}
            draw->ScaleClipRects(factor);draw->DisplayPos.x*=factor.x;draw->DisplayPos.y*=factor.y;
            draw->DisplaySize=ImVec2((float)dimensions.Width,(float)dimensions.Height);
        }
        auto view=target.Get();context->OMSetRenderTargets(1,&view,nullptr);ImGui_ImplDX11_RenderDrawData(draw);
        RendererTick();
        if(MenuVisible&&!InterlockedCompareExchange(&cursor_pending,1,0)&&!PostMessageW(owner.load(),CursorMessage,0,0))InterlockedExchange(&cursor_pending,0);
    }catch(HRESULT error) {TraceCode("dxgi_frame_failed",error);ResetGraphics();SetRuntimeError((DWORD)error,"DXGI frame initialization or drawing failed");}
    catch(...){ResetGraphics();SetRuntimeError(ERROR_UNHANDLED_EXCEPTION,"DXGI frame exception");}
}
static Callbacks* FindCallbacks(IDXGISwapChain* chain) {
    void** table=*(void***)chain;LONG count=InterlockedCompareExchange(&callback_count,0,0);
    for(LONG i=0;i<count;i++)if(callbacks[i].table==table)return &callbacks[i];return nullptr;
}
static void ObservePresent(IDXGISwapChain* chain,HRESULT result) {
    if(result==DXGI_ERROR_DEVICE_REMOVED||result==DXGI_ERROR_DEVICE_RESET||result==DXGI_ERROR_DEVICE_HUNG) {
        std::lock_guard<std::recursive_mutex> lock(frame_lock);
        if(chain==active_chain){TraceCode("dxgi_device_lost",result);if(device)TraceCode("dxgi_removed_reason",device->GetDeviceRemovedReason());ResetGraphics();}
    }
}
static HRESULT STDMETHODCALLTYPE Present(IDXGISwapChain* chain,UINT sync,UINT flags) {
    auto hook=FindCallbacks(chain);if(!hook||!hook->present)return DXGI_ERROR_INVALID_CALL;
    if(in_present)return hook->present(chain,sync,flags);
    in_present=true;Render(chain,flags);HRESULT result=hook->present(chain,sync,flags);in_present=false;ObservePresent(chain,result);return result;
}
static HRESULT STDMETHODCALLTYPE Present1(IDXGISwapChain1* chain,UINT sync,UINT flags,const DXGI_PRESENT_PARAMETERS* parameters) {
    auto hook=FindCallbacks(chain);if(!hook||!hook->present1)return DXGI_ERROR_INVALID_CALL;
    if(in_present)return hook->present1(chain,sync,flags,parameters);
    in_present=true;Render(chain,flags);HRESULT result=hook->present1(chain,sync,flags,parameters);in_present=false;ObservePresent(chain,result);return result;
}
static HRESULT STDMETHODCALLTYPE Resize(IDXGISwapChain* chain,UINT count,UINT width,UINT height,DXGI_FORMAT format,UINT flags) {
    auto hook=FindCallbacks(chain);if(!hook||!hook->resize)return DXGI_ERROR_INVALID_CALL;
    std::unique_lock<std::recursive_mutex> lock(frame_lock);
    if(chain!=active_chain){lock.unlock();return hook->resize(chain,count,width,height,format,flags);}
    context->OMSetRenderTargets(0,nullptr,nullptr);target.Reset();TraceRuntime("event=dxgi_resize; releasing UI back buffer");
    HRESULT result=hook->resize(chain,count,width,height,format,flags);if(FAILED(result))TraceCode("dxgi_resize_failed",result);return result;
}
static ULONG STDMETHODCALLTYPE Release(IDXGISwapChain* chain) {
    auto hook=FindCallbacks(chain);if(!hook||!hook->release)return 0;
    std::unique_lock<std::recursive_mutex> lock(frame_lock);
    if(chain!=active_chain){lock.unlock();return hook->release(chain);}
    ULONG remaining=hook->release(chain);if(!remaining){TraceRuntime("event=dxgi_chain_released");ResetGraphics();}return remaining;
}
// Only aligned interface pointers change; no instruction bytes or thread contexts change.
static DWORD Patch(void** slot,void* expected,void* replacement) {
    DWORD protection=0;if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&protection))return GetLastError();
    void* found=InterlockedCompareExchangePointer((PVOID volatile*)slot,replacement,expected);
    DWORD ignored=0;BOOL restored=VirtualProtect(slot,sizeof(void*),protection,&ignored);
    if(!restored) {
        DWORD error=GetLastError();
        if(found==expected)InterlockedCompareExchangePointer((PVOID volatile*)slot,expected,replacement);
        if(!VirtualProtect(slot,sizeof(void*),protection,&ignored))TraceCode("dxgi_pointer_protection_restore_failed",HRESULT_FROM_WIN32(GetLastError()));
        return error;
    }
    return found==expected?ERROR_SUCCESS:ERROR_BUSY;
}
static void Register(IDXGISwapChain* chain) {
    void** table=*(void***)chain;LONG index=InterlockedCompareExchange(&callback_count,0,0);
    for(LONG i=0;i<index;i++)if(callbacks[i].table==table)return;
    if(index==8)throw HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
    auto& entry=callbacks[index];entry.table=table;entry.release=(ReleaseFn)table[2];entry.present=(PresentFn)table[8];entry.resize=(ResizeFn)table[13];
    ComPtr<IDXGISwapChain1> newer;
    if(SUCCEEDED(chain->QueryInterface(IID_PPV_ARGS(&newer)))&&*(void***)newer.Get()==table)entry.present1=(Present1Fn)table[22];
    InterlockedIncrement(&callback_count); // Publish originals before exposing a callback.
    struct Change {unsigned index;void* old;void* next;};
    Change changes[]={{2,(void*)entry.release,(void*)Release},{13,(void*)entry.resize,(void*)Resize},{22,(void*)entry.present1,(void*)Present1},{8,(void*)entry.present,(void*)Present}};
    unsigned applied=0;
    for(unsigned i=0;i<4;i++) {
        if(!changes[i].old){applied++;continue;}
        DWORD error=Patch(table+changes[i].index,changes[i].old,changes[i].next);
        if(error) {
            // A protection-restore failure may already have exchanged this slot.
            for(unsigned j=0;j<=applied;j++)if(changes[j].old) {
                DWORD rollback=Patch(table+changes[j].index,changes[j].next,changes[j].old);
                if(rollback&&rollback!=ERROR_BUSY)TraceCode("dxgi_callback_rollback_failed",HRESULT_FROM_WIN32(rollback));
            }
            throw HRESULT_FROM_WIN32(error);
        }
        applied++;
    }
    TraceRuntime("event=dxgi_callbacks_installed; checked atomic interface callbacks");
}
static DWORD WINAPI Install(void*) {
    try {
        WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=ResidentModule;type.lpszClassName=L"Istaroth.DxgiProbe";
        if(!RegisterClassW(&type)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw HRESULT_FROM_WIN32(GetLastError());
        HWND window=CreateWindowW(type.lpszClassName,L"",WS_POPUP,0,0,32,32,nullptr,nullptr,ResidentModule,nullptr);
        if(!window)throw HRESULT_FROM_WIN32(GetLastError());
        unsigned prepared=0;
        for(auto driver:{D3D_DRIVER_TYPE_HARDWARE,D3D_DRIVER_TYPE_WARP})for(auto effect:{DXGI_SWAP_EFFECT_DISCARD,DXGI_SWAP_EFFECT_FLIP_DISCARD}) {
            DXGI_SWAP_CHAIN_DESC description{};description.BufferDesc.Width=description.BufferDesc.Height=32;
            description.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;description.SampleDesc.Count=1;
            description.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;description.BufferCount=2;description.OutputWindow=window;description.Windowed=TRUE;description.SwapEffect=effect;
            ComPtr<IDXGISwapChain> chain;ComPtr<ID3D11Device> probe;
            HRESULT result=D3D11CreateDeviceAndSwapChain(nullptr,driver,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&description,&chain,&probe,nullptr,nullptr);
            if(FAILED(result)){TraceCode("dxgi_probe_unavailable",result);continue;}
            try {Register(chain.Get());prepared++;}catch(...){DestroyWindow(window);throw;}
        }
        DestroyWindow(window);if(!prepared)throw DXGI_ERROR_UNSUPPORTED;
        TraceRuntime("event=dxgi_waiting_first_game_frame");return 0;
    }catch(HRESULT error){TraceCode("dxgi_install_failed",error);SetRuntimeError((DWORD)error,"DXGI callback setup failed");}
    catch(...){SetRuntimeError(ERROR_UNHANDLED_EXCEPTION,"DXGI setup exception");}return 1;
}
bool InstallRenderer() {HANDLE worker=CreateThread(nullptr,0,Install,nullptr,0,nullptr);if(!worker)return false;CloseHandle(worker);return true;}
}
