#include "injector_ui.h"
#include <dwmapi.h>
#include <shellscalingapi.h>
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

int ProgressStep(InjectorStage stage) {
    switch (stage) {
    case InjectorStage::Checking: return 0;
    case InjectorStage::WaitingGame:
    case InjectorStage::CheckingGame:
    case InjectorStage::Terminating:
    case InjectorStage::Starting: return 1;
    case InjectorStage::Waiting:
    case InjectorStage::Complete: return 2;
    default: return -1;
    }
}

const wchar_t* Hint(const InjectorUiState& state) {
    if (!state.detail_hint.empty()) return state.detail_hint.c_str();
    if (state.stage == InjectorStage::WaitingGame)
        return L"游戏启动后，伊斯塔露会自动加载。关闭此窗口可取消启动，不影响游戏。";
    if (state.stage == InjectorStage::Waiting)
        return L"菜单准备好后，此窗口会自动关闭。功能默认暂停，进入游戏后按 Insert 打开菜单，再点“启用”。";
    if (state.stage == InjectorStage::Terminating)
        return L"当前游戏关闭后，加载器会重新启动《原神》。";
    if (state.load_started)
        return L"关闭此窗口只会停止等待。游戏会继续运行，已加载的功能可在游戏内按 F10 停用。";
    if (state.stage == InjectorStage::CheckingGame)
        return L"加载器正在确认这个版本的《原神》是否受支持。";
    if (state.stage == InjectorStage::Starting)
        return L"请保持游戏和此窗口打开，加载器会自动完成后续步骤。";
    return L"加载器会先确认所需文件完整，再检查游戏。";
}

void Fill(HDC dc, RECT rect, COLORREF color) {
    SetDCBrushColor(dc, color);
    FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void Text(HDC dc, RECT rect, const wchar_t* value, COLORREF color, UINT flags) {
    SetTextColor(dc, color);
    DrawTextW(dc, value, -1, &rect, flags | DT_NOPREFIX);
}

void Draw(HDC dc, int width, int height, HICON portrait, const InjectorUiState& state) {
    const double scale = (std::min)(double(width) / kInjectorDesignWidth,
                                    double(height) / kInjectorDesignHeight);
    auto px = [scale](int value) { return static_cast<int>(std::lround(value * scale)); };
    auto font = [&](int height_px, int weight) {
        return CreateFontW(-px(height_px), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    };
    const int left = px(26), right = width - left, bottom_band = height - px(58);
    const bool failed = state.stage == InjectorStage::Failed;
    const int step = ProgressStep(state.stage);
    const int saved = SaveDC(dc);
    SetBkMode(dc, TRANSPARENT);
    Fill(dc, {0, 0, width, bottom_band}, background);
    Fill(dc, {0, bottom_band, width, height}, footer);

    int text_right = right;
    if (portrait && !failed) {
        const int side = px(72);
        const int x = right - side, y = (bottom_band - side) / 2;
        HRGN circle = CreateEllipticRgn(x, y, right, y + side);
        const int layer = SaveDC(dc);
        if (circle && layer && ExtSelectClipRgn(dc, circle, RGN_AND) != ERROR &&
            DrawIconEx(dc, x, y, portrait, side, side, 0, nullptr, DI_NORMAL))
            text_right = x - px(18);
        if (layer) RestoreDC(dc, layer);
        if (circle) DeleteObject(circle);
    }

    HFONT regular = font(14, FW_NORMAL);
    HFONT heading = font(23, FW_SEMIBOLD);
    auto original_font = SelectObject(dc, regular);
    SelectObject(dc, heading);
    RECT title{left, px(19), text_right, bottom_band};
    RECT measured = title;
    DrawTextW(dc, state.main_status.c_str(), -1, &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    title.bottom = (std::min)(measured.bottom, static_cast<LONG>(bottom_band - px(46)));
    Text(dc, title, state.main_status.c_str(), failed ? fault : primary, DT_WORDBREAK | DT_END_ELLIPSIS);
    SelectObject(dc, regular);
    Text(dc, {left, title.bottom + px(6), text_right, bottom_band - px(4)}, Hint(state), secondary, DT_WORDBREAK);

    if (step >= 0) {
        const wchar_t* labels[] = {L"检查文件", L"确认游戏", L"加载游戏内功能"};
        if (state.stage == InjectorStage::WaitingGame) labels[1] = L"等待游戏启动";
        if (state.stage == InjectorStage::CheckingGame) labels[1] = L"检查游戏版本";
        int gap = px(10), available = right - left + gap;
        for (int i = 0; i < 3; ++i) {
            int x = left + available * i / 3, end = left + available * (i + 1) / 3 - gap;
            Fill(dc, {x, bottom_band + px(12), end, bottom_band + px(i == step ? 16 : 14)},
                 i <= step ? accent : track);
            Text(dc, {x, bottom_band + px(21), end, height - px(4)}, labels[i],
                 i == step ? primary : secondary, DT_SINGLELINE);
        }
    } else if (failed) {
        Text(dc, {left, bottom_band, right, height}, L"加载日志   Istaroth-injector.log",
             secondary, DT_SINGLELINE | DT_VCENTER);
        if (state.error_code) {
            const auto code = L"错误 " + std::to_wstring(state.error_code);
            Text(dc, {left, bottom_band, right, height}, code.c_str(), fault, DT_SINGLELINE | DT_RIGHT | DT_VCENTER);
        }
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
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) return nullptr;
    UINT dpi = 0, dpi_y = 0;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi, &dpi_y))) dpi = GetDpiForSystem();
    RECT bounds{0, 0, MulDiv(kInjectorDesignWidth, dpi, 96), MulDiv(kInjectorDesignHeight, dpi, 96)};
    if (!AdjustWindowRectExForDpi(&bounds, style, FALSE, 0, dpi)) return nullptr;
    const RECT& work = info.rcWork;
    const LONG work_width = work.right - work.left, work_height = work.bottom - work.top;
    const LONG width = (std::min)(bounds.right - bounds.left, work_width);
    const LONG height = (std::min)(bounds.bottom - bounds.top, work_height);
    HWND window = CreateWindowW(wc.lpszClassName, L"伊斯塔露加载器", style,
                                work.left + (work_width - width) / 2, work.top + (work_height - height) / 2,
                                width, height, nullptr, nullptr, instance, nullptr);
    if (window) {
        HIGHCONTRASTW contrast{sizeof(contrast)};
        if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
            !(contrast.dwFlags & HCF_HIGHCONTRASTON)) {
            const BOOL dark = TRUE;
            DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
            DwmSetWindowAttribute(window, DWMWA_CAPTION_COLOR, &background, sizeof(background));
            DwmSetWindowAttribute(window, DWMWA_TEXT_COLOR, &primary, sizeof(primary));
        }
    }
    return window;
}

InjectorUiRenderer::~InjectorUiRenderer() { Reset(); }

void InjectorUiRenderer::Reset() {
    if (surface) DeleteDC(surface);
    if (bitmap) DeleteObject(bitmap);
    if (portrait) DestroyIcon(portrait);
    portrait = nullptr;
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
            Draw(dc, width, height, nullptr, state);
            return;
        }
        const double scale = (std::min)(double(width) / kInjectorDesignWidth, double(height) / kInjectorDesignHeight);
        int side = (std::max)(1L, std::lround(72 * scale));
        portrait = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), IMAGE_ICON, side, side, 0));
        size = {width, height};
    }
    if (resized || previous.stage != state.stage || previous.main_status != state.main_status ||
        previous.detail_hint != state.detail_hint || previous.error_code != state.error_code ||
        previous.load_started != state.load_started) {
        Draw(surface, width, height, portrait, state);
        previous = state;
    }
    BitBlt(dc, client.left, client.top, width, height, surface, 0, 0, SRCCOPY);
}

} // namespace istaroth
