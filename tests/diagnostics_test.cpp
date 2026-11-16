// Owned temporary files only; no game process, desktop input or network access.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#include <filesystem>
#include <fstream>
#include "diagnostic_report.h"
using namespace istaroth::diagnostics;
static unsigned checks=0;
static void Check(bool value){checks++;if(!value){fprintf(stderr,"diagnostics check %u failed\n",checks);exit(1);}}
static std::string ReadAll(const std::wstring& path) {
    std::ifstream file(std::filesystem::path(path),std::ios::binary);Check(file.good());
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
static void Write(const std::wstring& path,const std::string& value) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);Check(file!=INVALID_HANDLE_VALUE);
    DWORD written=0;Check(WriteFile(file,value.data(),(DWORD)value.size(),&written,nullptr)&&written==value.size());CloseHandle(file);
}
int main() {
    wchar_t temp[MAX_PATH],name[MAX_PATH];Check(GetTempPathW(MAX_PATH,temp)>0);Check(GetTempFileNameW(temp,L"ist",0,name)!=0);
    Check(DeleteFileW(name)!=FALSE);Check(CreateDirectoryW(name,nullptr)!=FALSE);std::wstring directory=std::wstring(name)+L"\\";
    SetLastError(789);Check(Append(directory,L"Istaroth-runtime.log","event=first"));Check(GetLastError()==789);
    auto data=ReadTail(directory+L"Istaroth-runtime.log");Check(data.find("event=first")!=std::string::npos);Check(data.find("version=" ISTAROTH_BUILD_VERSION)!=std::string::npos);Check(data.find("Z tick=")!=std::string::npos);
    Write(directory+L"Istaroth-runtime.log",std::string(LogLimit,'x'));
    Check(Append(directory,L"Istaroth-runtime.log","event=after_rotation"));
    WIN32_FILE_ATTRIBUTE_DATA attributes{};Check(GetFileAttributesExW((directory+L"Istaroth-runtime.log.1").c_str(),GetFileExInfoStandard,&attributes)!=FALSE);Check(attributes.nFileSizeLow==LogLimit);
    Check(ReadTail(directory+L"Istaroth-runtime.log").find("event=after_rotation")!=std::string::npos);
    std::vector<std::thread> workers;
    for(unsigned i=0;i<4;i++)workers.emplace_back([&]{for(unsigned j=0;j<50;j++)if(!Append(directory,L"concurrent.log","event=concurrent"))abort();});
    for(auto& thread:workers)thread.join();
    data=ReadTail(directory+L"concurrent.log");unsigned count=0;for(size_t p=0;(p=data.find("event=concurrent",p))!=std::string::npos;p++)count++;Check(count==200);
    HANDLE locked=CreateFileW((directory+L"locked.log").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);Check(locked!=INVALID_HANDLE_VALUE);
    SetLastError(654);Check(!Append(directory,L"locked.log","unavailable"));Check(GetLastError()==654);CloseHandle(locked);
    Check(!Append(directory+L"absent\\",L"missing.log","unavailable"));
    auto ini=directory+L"Istaroth.ini";Write(ini,std::string("\xff\xfe",2));
    auto private_game=directory+L"private-account\\YuanShen.exe";
    Check(WritePrivateProfileStringW(L"Istaroth",L"GamePath",private_game.c_str(),ini.c_str())!=FALSE);
    Check(WritePrivateProfileStringW(L"Private",L"Token",L"PRIVATE_TOKEN_SENTINEL",ini.c_str())!=FALSE);
    Check(WritePrivateProfileStringW(L"Settings",L"Multiplier",L"99",ini.c_str())!=FALSE);
    Check(WritePrivateProfileStringW(L"Settings",L"Advance",L"0",ini.c_str())!=FALSE);
    Check(WritePrivateProfileStringW(L"Settings",L"ChooseFirst",L"PRIVATE_CONFIG_SENTINEL",ini.c_str())!=FALSE);
    Write(directory+L"Istaroth-injector.log","full="+Utf8(private_game)+"\r\ntool="+Utf8(directory));
    std::wstring report;Check(ExportReport(directory,"expected-fixture-hash",report)==0);Check(!report.empty());
    data=ReadAll(report);Check(data.size()<4*1024*1024);Check(data.find("PRIVATE_TOKEN_SENTINEL")==std::string::npos);Check(data.find("PRIVATE_CONFIG_SENTINEL")==std::string::npos);
    Check(data.find(Utf8(private_game))==std::string::npos);Check(data.find(Utf8(directory))==std::string::npos);Check(data.find("[GAME_PATH]")!=std::string::npos);
    // Read the complete report header separately; ReadTail intentionally limits input size.
    HANDLE file=CreateFileW(report.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);Check(file!=INVALID_HANDLE_VALUE);
    char header[2048]={};DWORD read=0;Check(ReadFile(file,header,sizeof(header)-1,&read,nullptr)!=FALSE);CloseHandle(file);
    Check(strstr(header,"supported_game_sha256=expected-fixture-hash")!=nullptr);Check(strstr(header,"Multiplier=4")!=nullptr);Check(strstr(header,"Advance=0")!=nullptr);
    std::wstring second;Check(ExportReport(directory,"fixture",second)==0);Check(report!=second);
    auto empty=directory+L"empty\\";Check(CreateDirectoryW(empty.c_str(),nullptr)!=FALSE);Check(ExportReport(empty,"fixture",report)==0);
    data=ReadTail(report);Check(data.find("[missing")!=std::string::npos);
    auto blocked=directory+L"blocked\\";Check(CreateDirectoryW(blocked.c_str(),nullptr)!=FALSE);Write(blocked+L"reports","not a directory");
    Check(ExportReport(blocked,"fixture",report)!=0);Check(report.empty());
    // Remove only this test's explicitly created files and directories.
    for(auto folder:{directory,empty,blocked}) {
        for(auto child:{L"reports\\",L""}) {
            WIN32_FIND_DATAW found{};auto base=folder+child;HANDLE search=FindFirstFileW((base+L"*").c_str(),&found);
            if(search!=INVALID_HANDLE_VALUE){do{if(!(found.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))DeleteFileW((base+found.cFileName).c_str());}while(FindNextFileW(search,&found));FindClose(search);}
            if(*child)RemoveDirectoryW(base.c_str());
        }
    }
    RemoveDirectoryW(empty.c_str());RemoveDirectoryW(blocked.c_str());RemoveDirectoryW(directory.c_str());
    printf("{\"passed\":true,\"checks\":%u,\"scope\":\"owned log rotation, concurrency, export redaction and write failures\"}\n",checks);
}
