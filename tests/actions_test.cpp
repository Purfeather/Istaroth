#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <string>
int wmain() {
    wchar_t own[32768]; DWORD length=GetModuleFileNameW(nullptr,own,32768);
    if(!length||length>=32768)return 1;
    std::wstring path(own,length);path.resize(path.find_last_of(L"\\/")+1);path+=L"dialogue_actions_test.dll";
    HMODULE module=LoadLibraryW(path.c_str());if(!module)return 2;
    struct Case { const char* name; DWORD expected; } cases[]={{"ActionsSelfTest",32767},{"CombinedGuardSelfTest",511},{"CooldownSelfTest",63},{"OuterWaitSelfTest",3}};
    bool passed=true;
    for(auto item:cases) {
        auto run=(DWORD(*)())GetProcAddress(module,item.name);
        DWORD result=run?run():0;printf("%s=%lu expected=%lu\n",item.name,result,item.expected);passed=passed&&result==item.expected;
    }
    FreeLibrary(module);return passed?0:3;
}
