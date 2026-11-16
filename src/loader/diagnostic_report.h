#pragma once
#include "diagnostics.h"
#include <vector>

namespace istaroth::diagnostics {
inline std::string Utf8(const std::wstring& text) {
    if(text.empty())return {};
    int bytes=WideCharToMultiByte(CP_UTF8,0,text.data(),(int)text.size(),nullptr,0,nullptr,nullptr);
    std::string result(bytes,'\0');
    if(bytes)WideCharToMultiByte(CP_UTF8,0,text.data(),(int)text.size(),result.data(),bytes,nullptr,nullptr);
    return result;
}
inline void Replace(std::string& text,const std::string& value,const char* replacement) {
    if(value.empty())return;
    size_t position=0;
    while((position=text.find(value,position))!=std::string::npos) {
        text.replace(position,value.size(),replacement);position+=strlen(replacement);
    }
}
inline void RedactPath(std::string& text,const std::wstring& path,const char* replacement) {
    std::string value=Utf8(path);Replace(text,value,replacement);
    std::string escaped;
    for(char c:value){escaped+=c;if(c=='\\')escaped+='\\';}
    Replace(text,escaped,replacement);
    for(char& c:value)if(c=='\\')c='/';Replace(text,value,replacement);
}
inline std::string ReadTail(const std::wstring& path) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return "[missing or unreadable; error="+std::to_string(GetLastError())+"]\r\n";
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file,&size)){DWORD error=GetLastError();CloseHandle(file);return "[size unavailable; error="+std::to_string(error)+"]\r\n";}
    DWORD length=(DWORD)(size.QuadPart>LogLimit?LogLimit:size.QuadPart);
    if(size.QuadPart>LogLimit) {
        LARGE_INTEGER offset{};offset.QuadPart=-LogLimit;
        if(!SetFilePointerEx(file,offset,nullptr,FILE_END)){CloseHandle(file);return "[seek failed]\r\n";}
    }
    std::string data(length,'\0');DWORD read=0;BOOL ok=ReadFile(file,data.data(),length,&read,nullptr);CloseHandle(file);
    if(!ok)return "[read failed]\r\n";
    data.resize(read);
    if(size.QuadPart>LogLimit) {
        size_t newline=data.find('\n');if(newline!=std::string::npos)data.erase(0,newline+1);
        data="[older bytes omitted]\r\n"+data;
    }
    return data;
}

// A single UTF-8 report; fixed inputs only, with no process discovery or upload.
inline DWORD ExportReport(const std::wstring& directory,const char* game_hash,std::wstring& output) noexcept {
    output.clear();
    try {
        std::wstring reports=directory+L"reports";
        if(!CreateDirectoryW(reports.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return GetLastError();
        SYSTEMTIME time{};GetSystemTime(&time);wchar_t name[150];
        static volatile LONG sequence=0;
        swprintf_s(name,L"\\Istaroth-report-%04u%02u%02u-%02u%02u%02u-%lu-%llu-%ld.txt",time.wYear,time.wMonth,time.wDay,
            time.wHour,time.wMinute,time.wSecond,GetCurrentProcessId(),(unsigned long long)GetTickCount64(),InterlockedIncrement(&sequence));
        std::string report="Istaroth local diagnostic report\r\nversion=" ISTAROTH_BUILD_VERSION
            "\r\nsource=" ISTAROTH_SOURCE_REVISION "\r\nsource_dirty="+std::to_string(ISTAROTH_SOURCE_DIRTY)+
            "\r\nsupported_game_sha256="+game_hash+"\r\n";
        char timestamp[80];sprintf_s(timestamp,"captured_utc=%04u-%02u-%02uT%02u:%02u:%02uZ\r\n",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond);report+=timestamp;
        OSVERSIONINFOW version{};version.dwOSVersionInfoSize=sizeof(version);
        auto query=(LONG(WINAPI*)(OSVERSIONINFOW*))GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion");
        if(query&&query(&version)==0)report+="windows="+std::to_string(version.dwMajorVersion)+"."+std::to_string(version.dwMinorVersion)+"."+std::to_string(version.dwBuildNumber)+"\r\n";
        report+="architecture=x64\r\n\r\n[feature configuration]\r\n";
        auto ini=directory+L"Istaroth.ini";
        UINT multiplier=GetPrivateProfileIntW(L"Settings",L"Multiplier",4,ini.c_str());
        report+="Multiplier="+std::to_string(multiplier>=1&&multiplier<=4?multiplier:4)+"\r\n";
        for(auto key:{L"Advance",L"ChooseFirst",L"SkipCooldown"})
            report+=Utf8(key)+"="+std::to_string(GetPrivateProfileIntW(L"Settings",key,1,ini.c_str())==1?1:0)+"\r\n";
        report+="\r\n[component files]\r\n";
        for(auto component:{L"Istaroth.exe",L"Istaroth.dll",L"time_core.dll",L"story_probe.dll",L"dialogue_actions.dll"}) {
            WIN32_FILE_ATTRIBUTE_DATA data{};bool present=GetFileAttributesExW((directory+component).c_str(),GetFileExInfoStandard,&data)!=FALSE;
            report+=Utf8(component)+(present?" bytes="+std::to_string((uint64_t(data.nFileSizeHigh)<<32)|data.nFileSizeLow):" [missing]")+"\r\n";
        }
        for(auto file:{L"Istaroth-injector.log.1",L"Istaroth-injector.log",L"Istaroth-injector-result.json",L"Istaroth-runtime.log.1",L"Istaroth-runtime.log"})
            report+="\r\n===== "+Utf8(file)+" =====\r\n"+ReadTail(directory+file);
        std::vector<wchar_t> private_path(32768);
        GetPrivateProfileStringW(L"Istaroth",L"GamePath",L"",private_path.data(),(DWORD)private_path.size(),ini.c_str());
        RedactPath(report,private_path.data(),"[GAME_PATH]");
        DWORD length=GetEnvironmentVariableW(L"USERPROFILE",private_path.data(),(DWORD)private_path.size());
        if(length&&length<private_path.size())RedactPath(report,private_path.data(),"[USER_PROFILE]");
        RedactPath(report,directory,"[TOOL_DIRECTORY]/");
        report+="\r\n[user description]\r\nError time and time zone:\r\nPopup text:\r\nScene and last action:\r\n\r\nNo game memory, screenshots, credentials or Windows event logs are collected.\r\n";
        std::wstring path=reports+name;
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return GetLastError();
        const char bom[]="\xef\xbb\xbf";DWORD written=0;bool ok=WriteFile(file,bom,3,&written,nullptr)&&written==3;
        if(ok)ok=WriteFile(file,report.data(),(DWORD)report.size(),&written,nullptr)&&written==report.size();
        DWORD error=ok?ERROR_SUCCESS:GetLastError();CloseHandle(file);
        if(!ok)return error?error:ERROR_WRITE_FAULT;
        output=path;return ERROR_SUCCESS;
    }catch(...){return ERROR_NOT_ENOUGH_MEMORY;}
}
}
