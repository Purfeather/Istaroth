// Hidden owned window and temporary files only; no game, desktop input or network.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
static unsigned shell_calls=0;static std::wstring shell_file,shell_args;
static HINSTANCE WINAPI TestShellExecute(HWND,LPCWSTR,LPCWSTR file,LPCWSTR arguments,LPCWSTR,INT) {
    shell_calls++;shell_file=file;shell_args=arguments;return (HINSTANCE)33;
}
#define ShellExecuteW TestShellExecute
#define wWinMain CollectorUnusedEntry
#include "../src/loader/report_tool.cpp"
#undef wWinMain
#undef ShellExecuteW
static unsigned checks=0;
static void Check(bool value){checks++;if(!value){fprintf(stderr,"report tool check %u failed\n",checks);exit(1);}}
static void Write(const std::wstring& path,const std::string& value) {
    std::ofstream file(std::filesystem::path(path),std::ios::binary);Check(file.good());file<<value;file.close();Check(!file.fail());
}
static std::string Read(const std::wstring& path) {
    std::ifstream file(std::filesystem::path(path),std::ios::binary);Check(file.good());return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
static BOOL CALLBACK FindCollector(HWND window,LPARAM context) {
    auto values=(std::pair<DWORD,HWND>*)context;DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    wchar_t type[80];GetClassNameW(window,type,80);
    if(pid==values->first&&wcscmp(type,L"Istaroth.ReportCollector")==0){values->second=window;return FALSE;}return TRUE;
}
static void SmokeExecutable() {
    std::vector<wchar_t> own(32768);Check(GetModuleFileNameW(nullptr,own.data(),(DWORD)own.size())!=0);
    auto executable=std::filesystem::path(own.data()).parent_path()/L"伊斯塔露报告工具.exe";std::wstring command=L"\""+executable.wstring()+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
    Check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process)!=FALSE);
    WaitForInputIdle(process.hProcess,10000);std::pair<DWORD,HWND> found{process.dwProcessId,nullptr};EnumWindows(FindCollector,(LPARAM)&found);
    bool started=found.second&&!IsWindowVisible(found.second);
    if(found.second)PostMessageW(found.second,WM_CLOSE,0,0);
    DWORD waited=WaitForSingleObject(process.hProcess,10000),code=1;
    if(waited==WAIT_TIMEOUT){TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,10000);}
    GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hThread);CloseHandle(process.hProcess);
    Check(started);Check(waited==WAIT_OBJECT_0);Check(code==0);
}
static void Snapshot(HWND window,const wchar_t* path) {
    // Paint only our window outside the desktop; hidden windows can return black pixels.
    SetWindowPos(window,nullptr,-30000,-30000,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);ShowWindow(window,SW_SHOWNOACTIVATE);UpdateWindow(window);
    RECT rect{};GetWindowRect(window,&rect);int width=rect.right-rect.left,height=rect.bottom-rect.top;
    HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,width,height);auto old=SelectObject(dc,bitmap);
    Check(PrintWindow(window,dc,0)!=FALSE);SelectObject(dc,old);
    BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB};
    std::vector<char> pixels(size_t(width)*height*4);Check(GetDIBits(dc,bitmap,0,height,pixels.data(),&info,DIB_RGB_COLORS)!=0);
    BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);header.bfSize=header.bfOffBits+(DWORD)pixels.size();
    std::ofstream file(std::filesystem::path(path),std::ios::binary);file.write((char*)&header,sizeof(header));file.write((char*)&info.bmiHeader,sizeof(info.bmiHeader));file.write(pixels.data(),pixels.size());Check(file.good());
    DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);
}
int wmain(int argc,wchar_t** argv) {
    SmokeExecutable();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)));
    wchar_t temp[MAX_PATH],name[MAX_PATH];Check(GetTempPathW(MAX_PATH,temp)>0);Check(GetTempFileNameW(temp,L"irc",0,name)!=0);
    Check(DeleteFileW(name)!=FALSE);Check(CreateDirectoryW(name,nullptr)!=FALSE);
    std::wstring base=std::wstring(name)+L"\\",input=base+L"中文 测试\\",destination=base+L"导出报告";
    Check(CreateDirectoryW(input.c_str(),nullptr)!=FALSE);
    HWND window=report_tool::Create(GetModuleHandleW(nullptr),SW_HIDE);Check(window!=nullptr);Check(!IsWindowVisible(window));
    Check(!IsWindowEnabled(report_tool::export_button));Check(!IsWindowEnabled(report_tool::open_button));
    report_tool::SetSource(input);Check(report_tool::source.empty());Check(!IsWindowEnabled(report_tool::export_button));
    auto log=input+L"Istaroth-injector.log.1";Write(log,"version=old-fixture\r\nfixture-event=ready\r\n");
    report_tool::SetSource(input);Check(IsWindowEnabled(report_tool::export_button)!=FALSE); // Rotated log alone is enough.
    auto ini=input+L"Istaroth.ini";Write(ini,"[Private]\r\nToken=SECRET_SENTINEL\r\n[Settings]\r\nMultiplier=3\r\nAdvance=0\r\n");auto original=Read(ini),original_log=Read(log);
    report_tool::output=destination;
    SendMessageW(window,WM_COMMAND,report_tool::Export,0);Check(report_tool::last_error==0);Check(IsWindowEnabled(report_tool::open_button)!=FALSE);Check(shell_calls==0);
    auto first=report_tool::report;Check(first.find(destination)==0);auto contents=Read(first);
    Check(contents.find("collector_version=0.1.0")!=std::string::npos);Check(contents.find("\r\nversion=0.1.0")==std::string::npos);
    Check(contents.find("version=old-fixture")!=std::string::npos);Check(contents.find("fixture-event=ready")!=std::string::npos);
    Check(contents.find("SECRET_SENTINEL")==std::string::npos);Check(contents.find("Multiplier=3")!=std::string::npos);Check(contents.find("[missing")!=std::string::npos);
    Check(Read(ini)==original);Check(Read(log)==original_log);Check(!std::filesystem::exists(input+L"reports"));
    SendMessageW(window,WM_COMMAND,report_tool::Open,0);Check(shell_calls==1);Check(shell_file==L"explorer.exe");Check(shell_args==L"/select,\""+first+L"\"");
    SendMessageW(window,WM_COMMAND,report_tool::Export,0);Check(report_tool::last_error==0);Check(first!=report_tool::report);
    report_tool::output=base+L"blocked";Write(report_tool::output,"not a directory");
    SendMessageW(window,WM_COMMAND,report_tool::Export,0);Check(report_tool::last_error!=0);Check(report_tool::report.empty());Check(!IsWindowEnabled(report_tool::open_button));Check(IsWindowEnabled(report_tool::export_button)!=FALSE);
    SendMessageW(window,WM_COMMAND,report_tool::Open,0);Check(shell_calls==1);
    report_tool::SetSource(base+L"absent");Check(report_tool::source.empty());Check(!IsWindowEnabled(report_tool::export_button));
    report_tool::SetSource(input);report_tool::output=destination;
    HANDLE locked=CreateFileW(log.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);Check(locked!=INVALID_HANDLE_VALUE);
    SendMessageW(window,WM_COMMAND,report_tool::Export,0);Check(report_tool::last_error==0);Check(Read(report_tool::report).find("missing or unreadable; error=32")!=std::string::npos);CloseHandle(locked);
    auto license=report_tool::License();Check(license.find(L"Apache License")!=std::wstring::npos);Check(license.find(L"Istaroth contributors")!=std::wstring::npos);
    if(argc>1){report_tool::SetSource(L"");Snapshot(window,argv[1]);}
    DestroyWindow(window);DeleteObject(report_tool::font);DeleteObject(report_tool::title_font);DeleteObject(report_tool::background);CoUninitialize();
    // Only the uniquely created test directory is removed.
    std::filesystem::remove_all(std::filesystem::path(name));
    printf("{\"passed\":true,\"checks\":%u,\"scope\":\"hidden collector GUI, unicode paths, separate destination, read-only inputs, missing files, export failures and embedded license\"}\n",checks);
}
