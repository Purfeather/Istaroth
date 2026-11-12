#include "injector_ui.h"
#include <cmath>
#include <vector>

namespace istaroth {

// -----------------------------------------------------------------------------
// Scheme C: Astral Azure & Deep Frosted Obsidian (Strict GDI Palette)
// -----------------------------------------------------------------------------
static constexpr COLORREF kClrBgDark       = RGB(10, 14, 22);   // #0A0E16 深渊曜黑底色
static constexpr COLORREF kClrCardBg       = RGB(18, 25, 38);   // #121926 内容卡片底色
static constexpr COLORREF kClrBorderSubtle = RGB(36, 48, 68);   // #243044 极细分割与边框
static constexpr COLORREF kClrBorderActive = RGB(80, 170, 255); // #50AAFF 穹极星蓝边框
static constexpr COLORREF kClrBlueAccent   = RGB(80, 170, 255); // #50AAFF 唯一主高亮
static constexpr COLORREF kClrTextPrimary  = RGB(245, 250, 255); // 纯白高亮文字
static constexpr COLORREF kClrTextMuted    = RGB(150, 170, 195); // 中性透白次级文字
static constexpr COLORREF kClrTextDim      = RGB(90, 110, 135);  // 暗灰注释文字
static constexpr COLORREF kClrStagePending = RGB(45, 60, 85);   // 未开始节点深灰
static constexpr COLORREF kClrStageDone    = RGB(80, 170, 255); // 已完成节点星蓝
static constexpr COLORREF kClrErrorRed     = RGB(235, 80, 80);   // 失败异常红

// -----------------------------------------------------------------------------
// Scheme C: Celestial Astrolabe Emblem (Octagram Star in Compass Ring, No Needle)
// -----------------------------------------------------------------------------
static void DrawEmblemOctagram(HDC hdc, int cx, int cy, int radius, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HBRUSH brush = CreateSolidBrush(color);
    auto old_pen = SelectObject(hdc, pen);
    auto old_brush = SelectObject(hdc, brush);

    // Outer ring
    SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    Ellipse(hdc, cx - radius, cy - radius, cx + radius, cy + radius);
    SelectObject(hdc, brush);

    // 4 Cardinal Star Points (North, South, East, West)
    int tip_len = radius + (int)(radius * 0.35f);
    int inner_w = (int)(radius * 0.22f);

    POINT ptN[3] = { { cx, cy - tip_len }, { cx + inner_w, cy - radius + 2 }, { cx - inner_w, cy - radius + 2 } };
    Polygon(hdc, ptN, 3);
    POINT ptS[3] = { { cx, cy + tip_len }, { cx + inner_w, cy + radius - 2 }, { cx - inner_w, cy + radius - 2 } };
    Polygon(hdc, ptS, 3);
    POINT ptE[3] = { { cx + tip_len, cy }, { cx + radius - 2, cy + inner_w }, { cx + radius - 2, cy - inner_w } };
    Polygon(hdc, ptE, 3);
    POINT ptW[3] = { { cx - tip_len, cy }, { cx - radius + 2, cy + inner_w }, { cx - radius + 2, cy - inner_w } };
    Polygon(hdc, ptW, 3);

    // Central 8-Pointed Primogem / Octagram Star (Candidate C, No Clock Hand)
    int r_outer = (int)(radius * 0.65f);
    int r_inner = (int)(radius * 0.24f);

    POINT star[16];
    for (int i = 0; i < 16; ++i) {
        float angle = i * (3.14159265f / 8.0f) - (3.14159265f / 2.0f);
        int r = (i % 2 == 0) ? r_outer : r_inner;
        star[i].x = cx + (int)(cosf(angle) * r);
        star[i].y = cy + (int)(sinf(angle) * r);
    }
    Polygon(hdc, star, 16);

    // Small central core
    int hub_r = (int)(radius * 0.16f);
    if (hub_r < 2) hub_r = 2;
    Ellipse(hdc, cx - hub_r, cy - hub_r, cx + hub_r, cy + hub_r);

    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
}

// -----------------------------------------------------------------------------
// Draw Stepper Pipeline Indicator (Checking -> Launching -> Injecting -> Ready)
// -----------------------------------------------------------------------------
static void DrawStagePipeline(HDC hdc, int left, int top, int right, InjectorStage current_stage, float scale) {
    struct StageStep {
        const wchar_t* name;
        InjectorStage target;
    };
    StageStep steps[4] = {
        { L"环境校验", InjectorStage::Checking },
        { L"启动游戏", InjectorStage::Starting },
        { L"挂载组件", InjectorStage::Waiting },
        { L"初始化完成", InjectorStage::Complete }
    };

    int active_idx = 0;
    if (current_stage == InjectorStage::WaitingGame) active_idx = 1; // 首次启动等待原神进程
    else if (current_stage == InjectorStage::Terminating || current_stage == InjectorStage::Starting) active_idx = 1;
    else if (current_stage == InjectorStage::Waiting) active_idx = 2;
    else if (current_stage == InjectorStage::Complete) active_idx = 3;
    else if (current_stage == InjectorStage::Failed || current_stage == InjectorStage::Cancelled) active_idx = -1;

    int count = 4;
    int total_w = right - left;
    int step_w = total_w / count;

    int font_sz = (int)(-11.0f * scale);
    if (font_sz > -10) font_sz = -10;
    HFONT stage_font = CreateFontW(font_sz, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    auto old_font = SelectObject(hdc, stage_font);

    int cy = top + (int)(8.0f * scale);

    for (int i = 0; i < count; ++i) {
        int cx = left + i * step_w + step_w / 2;
        bool is_done = (active_idx >= 0 && i < active_idx) || (active_idx == 3 && i == 3);
        bool is_curr = (active_idx >= 0 && i == active_idx);
        bool is_err  = (current_stage == InjectorStage::Failed && i == 0);

        COLORREF dot_clr = is_err ? kClrErrorRed : (is_done || is_curr ? kClrBlueAccent : kClrStagePending);
        HBRUSH dot_brush = CreateSolidBrush(dot_clr);
        HPEN dot_pen = CreatePen(PS_SOLID, 1, is_curr ? kClrTextPrimary : dot_clr);
        auto ob = SelectObject(hdc, dot_brush);
        auto op = SelectObject(hdc, dot_pen);

        int dot_r = (int)((is_curr ? 5.0f : 4.0f) * scale);
        Ellipse(hdc, cx - dot_r, cy - dot_r, cx + dot_r, cy + dot_r);

        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        DeleteObject(dot_brush);
        DeleteObject(dot_pen);

        // Connecting line to next stage
        if (i < count - 1) {
            int next_cx = left + (i + 1) * step_w + step_w / 2;
            HPEN line_pen = CreatePen(PS_SOLID, 1, is_done ? kClrBlueAccent : kClrStagePending);
            auto ol = SelectObject(hdc, line_pen);
            MoveToEx(hdc, cx + (int)(8.0f * scale), cy, nullptr);
            LineTo(hdc, next_cx - (int)(8.0f * scale), cy);
            SelectObject(hdc, ol);
            DeleteObject(line_pen);
        }

        // Label
        RECT text_rc = { cx - (int)(40.0f * scale), cy + (int)(6.0f * scale), cx + (int)(40.0f * scale), cy + (int)(26.0f * scale) };
        SetTextColor(hdc, is_curr ? kClrTextPrimary : (is_done ? kClrBlueAccent : kClrTextDim));
        DrawTextW(hdc, steps[i].name, -1, &text_rc, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    SelectObject(hdc, old_font);
    DeleteObject(stage_font);
}

// -----------------------------------------------------------------------------
// Main Public Render Function: Double-Buffered, DPI-Scaled Injector Workplace
// -----------------------------------------------------------------------------
void RenderInjectorUi(HDC hdc, const RECT& client_rect, const InjectorUiState& state) {
    const int w = client_rect.right - client_rect.left;
    const int h = client_rect.bottom - client_rect.top;
    if (w <= 0 || h <= 0) return;

    // Compute dynamic DPI scale factor based on 640x220 design baseline
    float scale = state.dpi_scale;
    if (scale <= 0.0f) {
        float scale_w = (float)w / (float)kInjectorDesignWidth;
        float scale_h = (float)h / (float)kInjectorDesignHeight;
        scale = (scale_w < scale_h) ? scale_w : scale_h;
        if (scale < 0.65f) scale = 0.65f;
    }

    // Double buffering in memory
    HDC mem_dc = CreateCompatibleDC(hdc);
    HBITMAP mem_bm = CreateCompatibleBitmap(hdc, w, h);
    auto old_bm = SelectObject(mem_dc, mem_bm);

    SetBkMode(mem_dc, TRANSPARENT);

    // 1. Fill background (深渊曜黑)
    HBRUSH bg_brush = CreateSolidBrush(kClrBgDark);
    FillRect(mem_dc, &client_rect, bg_brush);
    DeleteObject(bg_brush);

    // 2. Content Card (深石蓝毛玻璃卡片矩形)
    int pad_x = (int)(16.0f * scale);
    int pad_y = (int)(12.0f * scale);
    RECT card_rc = { pad_x, pad_y, w - pad_x, h - pad_y };

    HBRUSH card_brush = CreateSolidBrush(kClrCardBg);
    HPEN card_pen = CreatePen(PS_SOLID, 1, kClrBorderSubtle);
    auto ob = SelectObject(mem_dc, card_brush);
    auto op = SelectObject(mem_dc, card_pen);
    int round_d = (int)(14.0f * scale);
    RoundRect(mem_dc, card_rc.left, card_rc.top, card_rc.right, card_rc.bottom, round_d, round_d);
    SelectObject(mem_dc, ob);
    SelectObject(mem_dc, op);
    DeleteObject(card_brush);
    DeleteObject(card_pen);

    // 3. Draw Brand Emblem (Candidate C: Octagram Star in Compass, No Hand)
    int emblem_r = (int)(13.0f * scale);
    int emblem_cx = card_rc.left + (int)(30.0f * scale);
    int emblem_cy = card_rc.top + (int)(30.0f * scale);
    COLORREF emblem_clr = (state.stage == InjectorStage::Failed) ? kClrErrorRed : kClrBlueAccent;
    DrawEmblemOctagram(mem_dc, emblem_cx, emblem_cy, emblem_r, emblem_clr);

    // 4. Header title: "伊斯塔露加载器" (用户最新精确命名，无中点无额外术语)
    int title_font_sz = (int)(-15.0f * scale);
    HFONT title_font = CreateFontW(title_font_sz, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    auto old_font = SelectObject(mem_dc, title_font);
    SetTextColor(mem_dc, kClrTextPrimary);
    RECT title_rc = { emblem_cx + (int)(22.0f * scale), card_rc.top + (int)(14.0f * scale),
                      emblem_cx + (int)(220.0f * scale), card_rc.top + (int)(42.0f * scale) };
    DrawTextW(mem_dc, L"伊斯塔露加载器", -1, &title_rc, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(mem_dc, old_font);
    DeleteObject(title_font);

    // 5. Stepper Pipeline on upper right
    int pipeline_left = emblem_cx + (int)(180.0f * scale);
    int pipeline_right = card_rc.right - (int)(16.0f * scale);
    DrawStagePipeline(mem_dc, pipeline_left, card_rc.top + (int)(10.0f * scale), pipeline_right, state.stage, scale);

    // Horizontal hairline divider
    HPEN div_pen = CreatePen(PS_SOLID, 1, kClrBorderSubtle);
    auto old_dp = SelectObject(mem_dc, div_pen);
    int div_y = card_rc.top + (int)(48.0f * scale);
    MoveToEx(mem_dc, card_rc.left + (int)(16.0f * scale), div_y, nullptr);
    LineTo(mem_dc, card_rc.right - (int)(16.0f * scale), div_y);
    SelectObject(mem_dc, old_dp);
    DeleteObject(div_pen);

    // 6. Main Status text (支持自动折行，纯白高亮，适度行间距)
    int status_font_sz = (int)(-14.0f * scale);
    HFONT status_font = CreateFontW(status_font_sz, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    SelectObject(mem_dc, status_font);
    SetTextColor(mem_dc, (state.stage == InjectorStage::Failed) ? kClrErrorRed : kClrTextPrimary);

    RECT status_rc = { card_rc.left + (int)(22.0f * scale),
                       div_y + (int)(10.0f * scale),
                       card_rc.right - (int)(22.0f * scale),
                       div_y + (int)(68.0f * scale) };
    std::wstring main_msg = state.main_status.empty() ? L"正在准备加载环境..." : state.main_status;
    DrawTextW(mem_dc, main_msg.c_str(), -1, &status_rc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

    // 7. Detail hint text (辅助说明，区分加载器关闭与游戏内F10停用，透白中性色)
    int hint_font_sz = (int)(-12.0f * scale);
    HFONT hint_font = CreateFontW(hint_font_sz, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    SelectObject(mem_dc, hint_font);
    SetTextColor(mem_dc, kClrTextMuted);

    RECT hint_rc = { card_rc.left + (int)(22.0f * scale),
                     status_rc.bottom + (int)(4.0f * scale),
                     card_rc.right - (int)(22.0f * scale),
                     card_rc.bottom - (int)(10.0f * scale) };
    std::wstring detail_msg = state.detail_hint.empty() ?
        L"提示：初始化确认后本加载器将自动退出。关闭本窗口不会卸载已注入组件或结束游戏；游戏内按 F10 停用。" :
        state.detail_hint;
    if (state.detail_hint.empty() && state.stage == InjectorStage::WaitingGame) {
        detail_msg = L"首次使用：未配置游戏路径，请手动启动原神。检测到游戏后将自动校验并记住路径，等待期间可点击右上角 [×] 取消。";
    }
    DrawTextW(mem_dc, detail_msg.c_str(), -1, &hint_rc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

    SelectObject(mem_dc, old_font);
    DeleteObject(status_font);
    DeleteObject(hint_font);

    // Blit to screen
    BitBlt(hdc, 0, 0, w, h, mem_dc, 0, 0, SRCCOPY);

    SelectObject(mem_dc, old_bm);
    DeleteObject(mem_bm);
    DeleteDC(mem_dc);
}

} // namespace istaroth
