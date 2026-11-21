#include "injector_ui.h"
#include <algorithm>
#include <cmath>

namespace istaroth {
namespace {

constexpr COLORREF background = RGB(17, 25, 39);
constexpr COLORREF footer = RGB(24, 35, 51);
constexpr COLORREF track = RGB(69, 87, 108);
constexpr COLORREF primary = RGB(240, 246, 251);
constexpr COLORREF secondary = RGB(174, 191, 208);
constexpr COLORREF accent = RGB(127, 212, 248);
constexpr COLORREF fault = RGB(255, 166, 155);

struct Phase {
    const wchar_t* label;
    int step;
};

Phase Describe(InjectorStage stage) {
    switch (stage) {
    case InjectorStage::Checking:    return {L"正在校验", 0};
    case InjectorStage::WaitingGame: return {L"等待操作", 1};
    case InjectorStage::CheckingGame: return {L"正在校验", 1};
    case InjectorStage::Terminating: return {L"正在重启", 1};
    case InjectorStage::Starting:    return {L"正在加载", 1};
    case InjectorStage::Waiting:     return {L"等待就绪", 2};
    case InjectorStage::Complete:    return {L"已就绪", 2};
    case InjectorStage::Failed:      return {L"加载未完成", -1};
    case InjectorStage::Cancelled:   return {L"已取消", -1};
    }
    return {L"", -1};
}

const wchar_t* Hint(const InjectorUiState& state) {
    if (!state.detail_hint.empty()) return state.detail_hint.c_str();
    if (state.stage == InjectorStage::WaitingGame)
        return L"检测到游戏后会自动继续，初始化成功后记住路径。关闭窗口可取消等待。";
    if (state.stage == InjectorStage::Waiting)
        return L"游戏内功能默认暂停。就绪后此窗口自动关闭，按 Insert 打开菜单，按 F10 停用。";
    if (state.load_started)
        return L"关闭窗口后游戏和组件继续运行。游戏内按 Insert 打开菜单，按 F10 停用。";
    return L"就绪后加载器会自动退出。关闭窗口可取消后续步骤，已启动的游戏会继续运行。";
}

void Fill(HDC dc, RECT rect, COLORREF color) {
    SetDCBrushColor(dc, color);
    FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void Text(HDC dc, RECT rect, const wchar_t* value, COLORREF color, UINT flags) {
    SetTextColor(dc, color);
    DrawTextW(dc, value, -1, &rect, flags | DT_NOPREFIX);
}

void Draw(HDC dc, int width, int height, HICON icon, const InjectorUiState& state) {
    const double scale = (std::min)(double(width) / kInjectorDesignWidth,
                                    double(height) / kInjectorDesignHeight);
    auto px = [scale](int value) { return static_cast<int>(std::lround(value * scale)); };
    auto font = [&](int height_px, int weight) {
        return CreateFontW(-px(height_px), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    };
    const int left = px(26), right = width - left, bottom_band = height - px(58);
    const bool failed = state.stage == InjectorStage::Failed;
    const Phase phase = Describe(state.stage);
    const COLORREF highlight = failed ? fault : accent;
    const int saved = SaveDC(dc);
    SetBkMode(dc, TRANSPARENT);
    Fill(dc, {0, 0, width, bottom_band}, background);
    Fill(dc, {0, bottom_band, width, height}, footer);

    HFONT regular = font(14, FW_NORMAL);
    HFONT heading = font(23, FW_SEMIBOLD);
    auto original_font = SelectObject(dc, regular);
    if (icon) DrawIconEx(dc, left, px(14), icon, px(32), px(32), 0, nullptr, DI_NORMAL);
    Text(dc, {left + px(42), px(12), right, px(49)}, L"伊斯塔露加载器", primary, DT_SINGLELINE | DT_VCENTER);
    std::wstring error_label;
    if (failed && state.error_code) error_label = L"错误 " + std::to_wstring(state.error_code);
    Text(dc, {left, px(12), right, px(49)}, error_label.empty() ? phase.label : error_label.c_str(),
         highlight, DT_SINGLELINE | DT_RIGHT | DT_VCENTER);

    SelectObject(dc, heading);
    RECT title{left, px(59), right, bottom_band};
    RECT measured = title;
    DrawTextW(dc, state.main_status.c_str(), -1, &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    title.bottom = (std::min)(measured.bottom, static_cast<LONG>(bottom_band - px(46)));
    Text(dc, title, state.main_status.c_str(), primary, DT_WORDBREAK | DT_END_ELLIPSIS);
    SelectObject(dc, regular);
    Text(dc, {left, title.bottom + px(6), right, bottom_band - px(4)}, Hint(state), secondary, DT_WORDBREAK);

    if (phase.step >= 0) {
        constexpr const wchar_t* labels[] = {L"检查组件", L"准备游戏", L"确认就绪"};
        int gap = px(10), available = right - left + gap;
        for (int i = 0; i < 3; ++i) {
            int x = left + available * i / 3, end = left + available * (i + 1) / 3 - gap;
            Fill(dc, {x, bottom_band + px(12), end, bottom_band + px(i == phase.step ? 16 : 14)},
                 i <= phase.step ? accent : track);
            Text(dc, {x, bottom_band + px(21), end, height - px(4)}, labels[i],
                 i == phase.step ? primary : secondary, DT_SINGLELINE);
        }
    } else if (failed) {
        Text(dc, {left, bottom_band, right, height}, L"加载日志   Istaroth-injector.log",
             secondary, DT_SINGLELINE | DT_VCENTER);
    }

    if (original_font) SelectObject(dc, original_font);
    if (saved) RestoreDC(dc, saved);
    DeleteObject(regular);
    DeleteObject(heading);
}

} // namespace

HWND CreateInjectorWindow(HINSTANCE instance, WNDPROC window_proc) {
    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.lpfnWndProc = window_proc;
    wc.lpszClassName = L"Istaroth.Injector";
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;

    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    const UINT dpi = GetDpiForSystem();
    RECT bounds{0, 0, MulDiv(kInjectorDesignWidth, dpi, 96), MulDiv(kInjectorDesignHeight, dpi, 96)};
    if (!AdjustWindowRectExForDpi(&bounds, style, FALSE, 0, dpi)) return nullptr;
    return CreateWindowW(wc.lpszClassName, L"伊斯塔露加载器", style, CW_USEDEFAULT, CW_USEDEFAULT,
                         bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, instance, nullptr);
}

InjectorUiRenderer::~InjectorUiRenderer() { Reset(); }

void InjectorUiRenderer::Reset() {
    if (surface) DeleteDC(surface);
    if (bitmap) DeleteObject(bitmap);
    if (icon) DestroyIcon(icon);
    icon = nullptr;
    surface = nullptr;
    bitmap = nullptr;
    size = {};
}

void InjectorUiRenderer::Paint(HDC dc, const RECT& client, const InjectorUiState& state) {
    const int width = client.right - client.left, height = client.bottom - client.top;
    if (width <= 0 || height <= 0) return;
    const bool resized = width != size.cx || height != size.cy;
    if (resized) {
        Reset();
        surface = CreateCompatibleDC(dc);
        bitmap = CreateCompatibleBitmap(dc, width, height);
        if (!surface || !bitmap || !SelectObject(surface, bitmap)) {
            Reset();
            Draw(dc, width, height, LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101)), state);
            return;
        }
        const double scale = (std::min)(double(width) / kInjectorDesignWidth, double(height) / kInjectorDesignHeight);
        const int icon_size = (std::max)(1L, std::lround(32 * scale));
        icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), IMAGE_ICON,
                                             icon_size, icon_size, 0));
        size = {width, height};
    }
    if (resized || previous.stage != state.stage || previous.main_status != state.main_status ||
        previous.detail_hint != state.detail_hint || previous.error_code != state.error_code ||
        previous.load_started != state.load_started) {
        Draw(surface, width, height, icon, state);
        previous = state;
    }
    BitBlt(dc, client.left, client.top, width, height, surface, 0, 0, SRCCOPY);
}

} // namespace istaroth
