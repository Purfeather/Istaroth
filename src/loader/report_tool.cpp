// Standalone collector: reads fixed local files, never starts or opens a game process.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <algorithm>
#include "diagnostic_report.h"
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
using Microsoft::WRL::ComPtr;
namespace report_tool {
constexpr int Choose=101,Export=102,Open=103,About=104;
static HWND path_box=nullptr,status_box=nullptr,export_button=nullptr,open_button=nullptr;
static HFONT font=nullptr,title_font=nullptr;static HBRUSH background=nullptr;
static std::wstring source,output,report;static DWORD last_error=0;
static int Scale(HWND window,int value){return MulDiv(value,GetDpiForWindow(window),96);}
static std::wstring Folder(std::wstring path){std::replace(path.begin(),path.end(),L'/',L'\\');if(!path.empty()&&path.back()!=L'\\')path+=L'\\';return path;}
static bool IsSource(const std::wstring& path) {
    DWORD attributes=GetFileAttributesW(path.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY))return false;
    for(auto name:{L"Istaroth.exe",L"Istaroth.ini",L"Istaroth-runtime.log",L"Istaroth-runtime.log.1",L"Istaroth-injector.log",L"Istaroth-injector.log.1",L"Istaroth-injector-result.json"}) {
        DWORD file_attributes=GetFileAttributesW((Folder(path)+name).c_str());if(file_attributes!=INVALID_FILE_ATTRIBUTES&&!(file_attributes&FILE_ATTRIBUTE_DIRECTORY))return true;
    }
    return false;
}
static void SetSource(const std::wstring& path) {
    source=IsSource(path)?Folder(path):L"";SetWindowTextW(path_box,source.c_str());EnableWindow(export_button,!source.empty());
    report.clear();EnableWindow(open_button,FALSE);
    SetWindowTextW(status_box,source.empty()?L"请选择包含 Istaroth.exe 或 Istaroth 日志的文件夹。":L"已选好文件夹。点击“导出报告”即可收集现有日志。");
}
static void Browse(HWND window) {
    ComPtr<IFileOpenDialog> dialog;if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))) {SetWindowTextW(status_box,L"无法打开文件夹选择窗口，请稍后重试。");return;}
    DWORD options=0;dialog->GetOptions(&options);dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"选择 Istaroth 工具所在文件夹（不是原神游戏目录）");
    if(!source.empty()){ComPtr<IShellItem> initial;if(SUCCEEDED(SHCreateItemFromParsingName(source.c_str(),nullptr,IID_PPV_ARGS(&initial))))dialog->SetFolder(initial.Get());}
    HRESULT result=dialog->Show(window);if(result==HRESULT_FROM_WIN32(ERROR_CANCELLED))return;
    ComPtr<IShellItem> item;PWSTR selected=nullptr;
    if(FAILED(result)||FAILED(dialog->GetResult(&item))||FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&selected))) {SetWindowTextW(status_box,L"读取所选文件夹失败。");return;}
    SetSource(selected);CoTaskMemFree(selected);
}
static void Collect(HWND window) {
    if(!IsSource(source)){SetSource(L"");return;}
    EnableWindow(export_button,FALSE);SetWindowTextW(status_box,L"正在整理日志，请稍候……");UpdateWindow(window);
    report.clear();EnableWindow(open_button,FALSE);
    last_error=istaroth::diagnostics::ExportReport(source,"not checked by standalone collector",report,true,output);
    if(last_error) {
        std::wstring message=L"导出失败，错误码 "+std::to_wstring(last_error)+L"。请确认桌面可写；如日志目录受保护，可右键以管理员身份运行。";
        SetWindowTextW(status_box,message.c_str());
    } else {
        std::wstring message=L"报告已保存：\r\n"+report+L"\r\n请将这份 TXT 与报错截图一起发送，并注明报错时间和当时操作。";
        SetWindowTextW(status_box,message.c_str());EnableWindow(open_button,TRUE);
    }
    EnableWindow(export_button,TRUE);
}
static void ShowReport(HWND window) {
    if(report.empty())return;
    std::wstring arguments=L"/select,\""+report+L"\"";
    auto result=ShellExecuteW(window,L"open",L"explorer.exe",arguments.c_str(),nullptr,SW_SHOWNORMAL);
    if((INT_PTR)result<=32)SetWindowTextW(status_box,(L"无法打开资源管理器，请手动打开：\r\n"+report).c_str());
}
static std::wstring License() {
    std::wstring text=L"伊斯塔露报告工具 0.1.0\r\nCopyright 2026 Istaroth contributors\r\n\r\n";
    for(int id:{201,202}) {HMODULE module=GetModuleHandleW(nullptr);HRSRC resource=FindResourceW(module,MAKEINTRESOURCEW(id),RT_RCDATA);if(!resource)continue;
        auto data=(const char*)LockResource(LoadResource(module,resource));int size=(int)SizeofResource(module,resource);if(!data||!size)continue;
        int length=MultiByteToWideChar(CP_UTF8,0,data,size,nullptr,0);std::wstring section(length,L'\0');MultiByteToWideChar(CP_UTF8,0,data,size,section.data(),length);text+=section+L"\r\n\r\n";
    }return text;
}
static LRESULT CALLBACK LicenseWindow(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_CREATE){auto text=License();HWND edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",text.c_str(),WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_READONLY,0,0,1,1,window,nullptr,GetModuleHandleW(nullptr),nullptr);SendMessageW(edit,WM_SETFONT,(WPARAM)font,TRUE);return 0;}
    if(message==WM_SIZE){MoveWindow(GetWindow(window,GW_CHILD),0,0,LOWORD(l),HIWORD(l),TRUE);return 0;}
    return DefWindowProcW(window,message,w,l);
}
static void Layout(HWND window) {
    RECT rect{};GetClientRect(window,&rect);int width=MulDiv(rect.right,96,GetDpiForWindow(window));
    struct Place{int id,x,y,w,h;};Place positions[]={{1,24,16,width-48,34},{2,24,58,width-48,42},{3,24,108,width-48,24},
        {4,24,138,width-204,38},{Choose,width-164,136,140,42},{Export,24,196,172,46},{Open,212,196,172,46},{About,width-124,204,100,32},{5,24,260,width-48,104}};
    for(auto p:positions)MoveWindow(GetDlgItem(window,p.id),Scale(window,p.x),Scale(window,p.y),Scale(window,p.w),Scale(window,p.h),TRUE);
    if(font)DeleteObject(font);if(title_font)DeleteObject(title_font);
    font=CreateFontW(-Scale(window,16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
    title_font=CreateFontW(-Scale(window,23),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
    for(auto p:positions)SendMessageW(GetDlgItem(window,p.id),WM_SETFONT,(WPARAM)(p.id==1?title_font:font),TRUE);
}
static LRESULT CALLBACK Window(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_CREATE) {
        auto control=[&](const wchar_t* type,const wchar_t* text,DWORD style,int id){return CreateWindowExW(id==4?WS_EX_CLIENTEDGE:0,type,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,window,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);};
        control(L"STATIC",L"伊斯塔露报告工具",0,1);control(L"STATIC",L"报告包含已有日志与配置摘要，保存到桌面的 Istaroth-reports 文件夹。\r\n不会自动上传；你可以先查看报告，再将 TXT 文件发送给开发者。",0,2);
        control(L"STATIC",L"Istaroth 所在文件夹",0,3);path_box=control(L"EDIT",L"",ES_READONLY|ES_AUTOHSCROLL,4);
        control(L"BUTTON",L"选择文件夹…",WS_TABSTOP,Choose);export_button=control(L"BUTTON",L"导出报告",WS_TABSTOP|BS_DEFPUSHBUTTON,Export);
        open_button=control(L"BUTTON",L"打开报告位置",WS_TABSTOP,Open);control(L"BUTTON",L"许可声明",WS_TABSTOP,About);status_box=control(L"STATIC",L"请选择工具文件夹。",SS_NOPREFIX,5);
        EnableWindow(export_button,FALSE);EnableWindow(open_button,FALSE);Layout(window);return 0;
    }
    if(message==WM_COMMAND){switch(LOWORD(w)){case Choose:Browse(window);break;case Export:Collect(window);break;case Open:ShowReport(window);break;case About:{
        HWND license=CreateWindowExW(WS_EX_DLGMODALFRAME,L"Istaroth.ReportLicense",L"许可证与版权",WS_OVERLAPPEDWINDOW,100,100,760,540,window,nullptr,GetModuleHandleW(nullptr),nullptr);ShowWindow(license,SW_SHOW);break;} }return 0;}
    if(message==WM_CTLCOLORSTATIC){SetTextColor((HDC)w,RGB(28,48,72));SetBkColor((HDC)w,RGB(244,247,251));return (LRESULT)background;}
    if(message==WM_DPICHANGED){auto rect=(RECT*)l;SetWindowPos(window,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);Layout(window);return 0;}
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(window,message,w,l);
}
static HWND Create(HINSTANCE instance,int show) {
    background=CreateSolidBrush(RGB(244,247,251));WNDCLASSW type{};type.hInstance=instance;type.lpfnWndProc=Window;type.hbrBackground=background;
    type.hCursor=LoadCursorW(nullptr,IDC_ARROW);type.hIcon=LoadIconW(nullptr,IDI_INFORMATION);type.lpszClassName=L"Istaroth.ReportCollector";RegisterClassW(&type);
    type.lpfnWndProc=LicenseWindow;type.lpszClassName=L"Istaroth.ReportLicense";RegisterClassW(&type);
    UINT dpi=GetDpiForSystem();RECT rect{0,0,MulDiv(760,dpi,96),MulDiv(388,dpi,96)};DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
    AdjustWindowRectExForDpi(&rect,style,FALSE,0,dpi);HWND window=CreateWindowW(L"Istaroth.ReportCollector",L"伊斯塔露报告工具",style,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,nullptr);
    if(window)ShowWindow(window,show);return window;
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);HRESULT apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(apartment))return (int)apartment;
    PWSTR desktop=nullptr;if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop,0,nullptr,&desktop))){report_tool::output=std::wstring(desktop)+L"\\Istaroth-reports";CoTaskMemFree(desktop);}
    else {MessageBoxW(nullptr,L"无法获取桌面位置，请稍后重试。",L"伊斯塔露报告工具",MB_OK|MB_ICONERROR);CoUninitialize();return ERROR_PATH_NOT_FOUND;}
    HWND window=report_tool::Create(instance,show);if(!window){CoUninitialize();return (int)GetLastError();}
    std::vector<wchar_t> own(32768);DWORD length=GetModuleFileNameW(nullptr,own.data(),(DWORD)own.size());
    if(length&&length<own.size()){std::wstring folder(own.data());folder.resize(folder.find_last_of(L"\\/")+1);report_tool::SetSource(folder);}
    MSG message;while(GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
    if(report_tool::font)DeleteObject(report_tool::font);if(report_tool::title_font)DeleteObject(report_tool::title_font);if(report_tool::background)DeleteObject(report_tool::background);
    CoUninitialize();return 0;
}
