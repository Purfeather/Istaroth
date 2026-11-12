#pragma once
#include "resident_control.h"
#include <string>

namespace istaroth {
extern HMODULE ResidentModule;
extern HWND GameWindow;
extern volatile LONG MenuVisible;
extern volatile LONG64 LastPresent;
extern volatile LONG RendererReady;
extern volatile LONG BackdropReady;
extern std::wstring DataDirectory;
void EmitCommand(UiCommand,int);
UiState GetUiState();
void RendererTick();
bool InstallRenderer();
bool RendererOwnsForeground();
void SetMenuVisible(bool);
void SetRuntimeError(DWORD,const char*);
void TraceRuntime(const char*);
}
