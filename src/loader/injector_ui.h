#pragma once
#include <windows.h>
#include <string>

namespace istaroth {

enum class InjectorStage { Checking, WaitingGame, CheckingGame, Terminating, Starting, Waiting, Complete, Failed, Cancelled };

constexpr int kInjectorDesignWidth = 640;
constexpr int kInjectorDesignHeight = 220;

struct InjectorUiState {
    InjectorStage stage = InjectorStage::Checking;
    std::wstring main_status = L"正在校验组件文件";
    std::wstring detail_hint;
    DWORD error_code = 0;
    bool load_started = false;
};

HWND CreateInjectorWindow(HINSTANCE instance, WNDPROC window_proc);

class InjectorUiRenderer {
public:
    InjectorUiRenderer() = default;
    InjectorUiRenderer(const InjectorUiRenderer&) = delete;
    InjectorUiRenderer& operator=(const InjectorUiRenderer&) = delete;
    ~InjectorUiRenderer();
    void Paint(HDC destination, const RECT& client, const InjectorUiState& state);
    void Reset();

private:
    HDC surface = nullptr;
    HBITMAP bitmap = nullptr;
    HICON icon = nullptr;
    SIZE size{};
    InjectorUiState previous;
};

} // namespace istaroth
