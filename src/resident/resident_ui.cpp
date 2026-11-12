#include "resident_ui.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <cmath>
#include <cfloat>
#include <cstdio>

namespace istaroth {

// -----------------------------------------------------------------------------
// Scheme C: Astral Azure on Frosted Glass (Strict Two-Color Discipline)
// -----------------------------------------------------------------------------
static constexpr ImU32 kColGlassModal   = IM_COL32(14, 20, 32, 184); // rgba(14, 20, 32, 0.72)
static constexpr ImU32 kColGlassHud     = IM_COL32(10, 16, 26, 204); // rgba(10, 16, 26, 0.80)
static constexpr ImU32 kColCardBg       = IM_COL32(255, 255, 255, 10); // rgba(255, 255, 255, 0.04)
static constexpr ImU32 kColCardBgHover  = IM_COL32(255, 255, 255, 18); // rgba(255, 255, 255, 0.07)
static constexpr ImU32 kColBorderSubtle = IM_COL32(255, 255, 255, 26); // rgba(255, 255, 255, 0.10)
static constexpr ImU32 kColBorderActive = IM_COL32(80, 170, 255, 166); // rgba(80, 170, 255, 0.65)
static constexpr ImU32 kColBlueAccent   = IM_COL32(80, 170, 255, 255); // #50AAFF
static constexpr ImU32 kColBlueGlow     = IM_COL32(80, 170, 255, 97);  // rgba(80, 170, 255, 0.38)
static constexpr ImU32 kColBlueDim      = IM_COL32(80, 170, 255, 45);  // rgba(80, 170, 255, 0.18)
static constexpr ImU32 kColTextPrimary  = IM_COL32(255, 255, 255, 255);
static constexpr ImU32 kColTextMuted    = IM_COL32(255, 255, 255, 153); // rgba(255, 255, 255, 0.60)
static constexpr ImU32 kColTextDim      = IM_COL32(255, 255, 255, 82);  // rgba(255, 255, 255, 0.32)
static constexpr ImU32 kColWhite        = IM_COL32(255, 255, 255, 255);
static constexpr ImU32 kColDarkText     = IM_COL32(6, 9, 14, 255);

static ImVec4 TextInkBounds(const char* text) {
    ImFont* font=ImGui::GetFont();float advance=0;
    ImVec4 bounds(FLT_MAX,FLT_MAX,-FLT_MAX,-FLT_MAX);
    for(const char* p=text;*p;) {
        unsigned code=0;int count=ImTextCharFromUtf8(&code,p,nullptr);if(count<=0)break;p+=count;
        const ImFontGlyph* glyph=font->FindGlyph((ImWchar)code);if(!glyph)continue;
        if(glyph->Visible) {
            bounds.x=ImMin(bounds.x,advance+glyph->X0);bounds.z=ImMax(bounds.z,advance+glyph->X1);
            bounds.y=ImMin(bounds.y,glyph->Y0);bounds.w=ImMax(bounds.w,glyph->Y1);
        }
        advance+=glyph->AdvanceX;
    }
    if(bounds.x==FLT_MAX)return ImVec4(0,0,0,ImGui::GetFontSize());
    float factor=ImGui::GetFontSize()/font->FontSize;
    return ImVec4(bounds.x*factor,bounds.y*factor,bounds.z*factor,bounds.w*factor);
}
static void DrawBoxedText(ImDrawList* draw,const ImVec2& min_pos,const ImVec2& max_pos,ImU32 color,const char* text) {
    ImVec4 ink=TextInkBounds(text);
    ImVec2 origin(std::floor((min_pos.x+max_pos.x-ink.x-ink.z)*0.5f+0.5f),std::floor((min_pos.y+max_pos.y-ink.y-ink.w)*0.5f+0.5f));
    ImVec4 clip(min_pos.x,min_pos.y,max_pos.x,max_pos.y);
    draw->AddText(nullptr,0,origin,color,text,nullptr,0,&clip);
}
static bool BoxedButton(const char* label,const ImVec2& size) {
    ImU32 color=ImGui::GetColorU32(ImGuiCol_Text);
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(0,0,0,0));
    bool pressed=ImGui::Button(label,size);ImGui::PopStyleColor();
    DrawBoxedText(ImGui::GetWindowDrawList(),ImGui::GetItemRectMin(),ImGui::GetItemRectMax(),color,label);
    return pressed;
}

// -----------------------------------------------------------------------------
// Helper: Blurred DX11 Backdrop Underlay
// -----------------------------------------------------------------------------
static void DrawBackdropUnderlay(ImDrawList* draw, uint64_t backdrop, float display_w, float display_h,
                                const ImVec2& min_pos, const ImVec2& max_pos, float rounding, ImU32 tint_glass) {
    if (backdrop != 0 && display_w > 0.0f && display_h > 0.0f) {
        ImVec2 uv0(min_pos.x / display_w, min_pos.y / display_h);
        ImVec2 uv1(max_pos.x / display_w, max_pos.y / display_h);
        uv0.x = ImClamp(uv0.x, 0.0f, 1.0f); uv0.y = ImClamp(uv0.y, 0.0f, 1.0f);
        uv1.x = ImClamp(uv1.x, 0.0f, 1.0f); uv1.y = ImClamp(uv1.y, 0.0f, 1.0f);
        draw->AddImageRounded((ImTextureID)backdrop, min_pos, max_pos, uv0, uv1, IM_COL32_WHITE, rounding);
    }
    draw->AddRectFilled(min_pos, max_pos, tint_glass, rounding);
    draw->AddRect(min_pos, max_pos, kColBorderSubtle, rounding, 0, 1.0f);
}

// -----------------------------------------------------------------------------
// Static PNG icon; a simple ring remains available if the optional asset fails.
// -----------------------------------------------------------------------------
static void DrawStaticIcon(ImDrawList* draw,uint64_t texture,const ImVec2& center,float size,ImU32 color) {
    float half=size*0.5f;
    if(texture)draw->AddImage((ImTextureID)texture,ImVec2(center.x-half,center.y-half),ImVec2(center.x+half,center.y+half),ImVec2(0,0),ImVec2(1,1),color);
    else draw->AddCircle(center,half*0.7f,color,24,size*0.065f);
}

// -----------------------------------------------------------------------------
// Custom Widget: Clean Flat Switch
// -----------------------------------------------------------------------------
static bool DrawCleanSwitch(const char* id, bool* v, float scale) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID imgui_id = window->GetID(id);

    const float width  = 34.0f * scale;
    const float height = 18.0f * scale;
    const ImVec2 pos   = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + width, pos.y + height));

    ImGui::ItemSize(bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, imgui_id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, imgui_id, &hovered, &held);
    if (pressed) {
        *v = !(*v);
        ImGui::MarkItemEdited(imgui_id);
    }

    ImDrawList* draw = window->DrawList;
    float rounding = height * 0.5f;
    ImU32 rail_col = (*v) ? kColBlueAccent : IM_COL32(255, 255, 255, 31);
    draw->AddRectFilled(bb.Min, bb.Max, rail_col, rounding);

    float knob_radius = 6.0f * scale;
    float knob_offset_x = (*v) ? (width - 3.0f * scale - knob_radius) : (3.0f * scale + knob_radius);
    ImVec2 knob_center(bb.Min.x + knob_offset_x, bb.Min.y + height * 0.5f);
    ImU32 knob_col = (*v) ? kColDarkText : kColWhite;
    draw->AddCircleFilled(knob_center, knob_radius, knob_col, 16);

    return pressed;
}

// -----------------------------------------------------------------------------
// Custom Widget: Discrete 4-Step Slider (16px Track, 20px White Thumb, Anti-Box Rounding)
// -----------------------------------------------------------------------------
static bool DrawDiscreteSlider(const char* id, unsigned* target_val, float scale, float x_start, float x_end, float card_y_mid) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID imgui_id = window->GetID(id);

    const float track_h = 16.0f * scale;
    const float thumb_r = 10.0f * scale;
    const float widget_h = 28.0f * scale;
    const float track_y_mid = card_y_mid;

    const ImRect bb(ImVec2(x_start - thumb_r, track_y_mid - widget_h * 0.5f),
                    ImVec2(x_end + thumb_r,   track_y_mid + widget_h * 0.5f));

    ImGui::ItemSize(bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, imgui_id)) return false;

    bool hovered, held;
    ImGui::ButtonBehavior(bb, imgui_id, &hovered, &held);

    bool changed = false;
    const float active_len = x_end - x_start;

    if (held && active_len > 0.0f) {
        float mouse_x = g.IO.MousePos.x;
        float norm = ImClamp((mouse_x - x_start) / active_len, 0.0f, 1.0f);
        unsigned snapped = (unsigned)ImClamp((int)std::round(norm * 3.0f) + 1, 1, 4);
        if (snapped != *target_val) {
            *target_val = snapped;
            changed = true;
            ImGui::MarkItemEdited(imgui_id);
        }
    }

    unsigned step = *target_val;
    if (step < 1) step = 1;
    if (step > 4) step = 4;

    ImDrawList* draw = window->DrawList;

    // 1. Base track pill
    ImVec2 track_min(x_start - thumb_r, track_y_mid - track_h * 0.5f);
    ImVec2 track_max(x_end + thumb_r,   track_y_mid + track_h * 0.5f);
    float rounding = track_h * 0.5f;
    draw->AddRectFilled(track_min, track_max, IM_COL32(255, 255, 255, 28), rounding);

    // 2. Active fill: fully rounded ends, preventing square edges at all steps
    float fill_norm = (float)(step - 1) / 3.0f;
    float thumb_center_x = x_start + fill_norm * active_len;
    ImVec2 fill_min = track_min;
    ImVec2 fill_max(thumb_center_x + thumb_r, track_max.y);

    if (step == 4) {
        // True rounded geometry colored by vertex gradient (no rectangular clipping artifacts)
        const int vtx_start = draw->VtxBuffer.Size;
        draw->AddRectFilled(track_min, track_max, IM_COL32_WHITE, rounding);
        const int vtx_end = draw->VtxBuffer.Size;
        ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw, vtx_start, vtx_end,
                                                     track_min, ImVec2(track_max.x, track_min.y),
                                                     IM_COL32(50, 114, 230, 255), IM_COL32(136, 194, 255, 255));

        // Stardust micro specks within track
        draw->AddCircleFilled(ImVec2(x_start + active_len * 0.22f, track_y_mid), 1.5f * scale, IM_COL32(255, 255, 255, 180));
        draw->AddCircleFilled(ImVec2(x_start + active_len * 0.52f, track_y_mid - 2.0f * scale), 1.3f * scale, IM_COL32(255, 255, 255, 160));
        draw->AddCircleFilled(ImVec2(x_start + active_len * 0.78f, track_y_mid + 1.5f * scale), 1.6f * scale, IM_COL32(255, 255, 255, 200));
    } else {
        draw->AddRectFilled(fill_min, fill_max, kColBlueAccent, rounding);
    }

    // 3. Embedded notch dots exactly at step centers
    for (int i = 0; i < 4; ++i) {
        float notch_x = x_start + ((float)i / 3.0f) * active_len;
        draw->AddCircleFilled(ImVec2(notch_x, track_y_mid), 2.2f * scale, IM_COL32(255, 255, 255, 120));
    }

    // 4. Prominent 20px white circular draggable thumb knob
    ImVec2 thumb_center(thumb_center_x, track_y_mid);
    draw->AddCircleFilled(ImVec2(thumb_center.x, thumb_center.y + 1.5f * scale), thumb_r + 1.0f * scale, IM_COL32(0, 0, 0, 110), 24);
    if (step == 4) {
        draw->AddCircleFilled(thumb_center, thumb_r + 3.5f * scale, kColBlueGlow, 24);
    }
    draw->AddCircleFilled(thumb_center, thumb_r, kColWhite, 24);
    draw->AddCircle(thumb_center, thumb_r, IM_COL32(0, 0, 0, 20), 24, 1.0f);

    return changed;
}

// -----------------------------------------------------------------------------
// Top-Center Full-Information HUD Overlay
// -----------------------------------------------------------------------------
static void DrawTopCenterHud(const UiState& s, bool& menu_open, float scale) {
    const float display_w = (s.display_width > 0.0f) ? s.display_width : ImGui::GetIO().DisplaySize.x;
    const float display_h = (s.display_height > 0.0f) ? s.display_height : ImGui::GetIO().DisplaySize.y;
    bool is_enabled = s.enabled;
    struct HudFunc { const char* name; bool configured; };
    HudFunc funcs[3]={{u8"推进",s.advance},{u8"首选",s.choose_first},{u8"冷却",s.skip_cooldown}};
    char speed_buf[16];
    if(s.actual_known)std::snprintf(speed_buf,sizeof(speed_buf),u8"%.1f×",s.actual);
    else std::snprintf(speed_buf,sizeof(speed_buf),u8"—");
    ImFont* font=ImGui::GetFont();float font_size=GImGui->FontBaseSize*0.8f*scale;
    auto measure=[&](const char* text) {
        ImVec2 size=font->CalcTextSizeA(font_size,FLT_MAX,0,text);
        size.x=std::floor(size.x+0.99999f);return size;
    };
    float speed_slot=ImMax(measure(u8"4.0×").x,measure(speed_buf).x);
    const char* ins_str="Insert";ImVec2 ins_sz=measure(ins_str);
    ImVec2 tag_sizes[3];float tags_width=0;
    for(int i=0;i<3;i++){tag_sizes[i]=measure(funcs[i].name);tags_width+=tag_sizes[i].x+20.0f*scale;}
    float dot_offset=35.0f*scale+speed_slot+6.0f*scale;
    float tags_offset=dot_offset+22.0f*scale;
    float insert_offset=tags_offset+tags_width+14.0f*scale;
    const float hud_h=std::ceil(38.0f*scale);
    const float hud_w=std::ceil(insert_offset+ins_sz.x+24.0f*scale);
    const float hud_x=std::floor((display_w-hud_w)*0.5f);
    const float hud_y=std::floor(16.0f*scale);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize,ImVec2(1,1));
    ImGui::SetNextWindowPos(ImVec2(hud_x, hud_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(hud_w, hud_h), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("##IstarothHudCapsule", nullptr, flags)) {
        ImGui::SetWindowFontScale(scale * 0.8f);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 p_min = ImGui::GetWindowPos();
        ImVec2 window_size=ImGui::GetWindowSize();
        ImVec2 p_max(p_min.x+window_size.x,p_min.y+window_size.y);
        float rounding = hud_h * 0.5f;
        // Begin() clips to the inner content rectangle; the capsule stroke needs its AA fringe too.
        draw->PushClipRect(ImVec2(p_min.x-2,p_min.y-2),ImVec2(p_max.x+2,p_max.y+2),false);

        DrawBackdropUnderlay(draw, s.backdrop, display_w, display_h, p_min, p_max, rounding, kColGlassHud);
        if (is_enabled) {
            draw->AddRect(p_min, p_max, kColBorderActive, rounding, 0, 1.2f);
        }

        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            menu_open = !menu_open;
        }

        // 1. Left Icon: Mini Compass
        ImVec2 compass_center(p_min.x + 20.0f * scale, p_min.y + hud_h * 0.5f);
        DrawStaticIcon(draw, s.icon_texture, compass_center, 26.0f * scale, is_enabled ? kColBlueAccent : kColTextMuted);

        // 2. Speed readout
        ImVec2 text_pos(p_min.x+35.0f*scale,p_min.y);
        ImU32 text_col = is_enabled ? kColBlueAccent : kColTextPrimary;
        DrawBoxedText(draw,text_pos,ImVec2(text_pos.x+speed_slot,p_max.y),text_col,speed_buf);

        // Live status dot
        ImVec2 dot_pos(p_min.x+dot_offset,p_min.y+hud_h*0.5f);
        ImU32 dot_col = is_enabled ? kColBlueAccent : kColTextDim;
        draw->AddCircleFilled(dot_pos, 3.0f * scale, dot_col, 12);
        if (is_enabled) {
            draw->AddCircleFilled(dot_pos, 5.0f * scale, kColBlueGlow, 12);
        }

        // Separator 1
        float sep1_x = dot_pos.x + 12.0f * scale;
        draw->AddLine(ImVec2(sep1_x, p_min.y + 11.0f * scale), ImVec2(sep1_x, p_max.y - 11.0f * scale), IM_COL32(255, 255, 255, 38), 1.0f);

        // 3. Three Chinese Function Tags: 推进, 首选, 冷却
        float tag_x = p_min.x+tags_offset;
        const float tag_h = 20.0f * scale;
        const float tag_y = p_min.y + (hud_h - tag_h) * 0.5f;

        for (int i = 0; i < 3; ++i) {
            ImVec2 tsz = tag_sizes[i];
            float tag_w = tsz.x + 14.0f * scale;
            ImVec2 t_min(tag_x, tag_y);
            ImVec2 t_max(tag_x + tag_w, tag_y + tag_h);

            bool lit = s.enabled && funcs[i].configured;
            ImU32 bg_col = lit ? kColBlueDim : IM_COL32(255, 255, 255, 8);
            ImU32 fg_col = lit ? kColBlueAccent : kColTextDim;
            draw->AddRectFilled(t_min, t_max, bg_col, 4.0f * scale);
            if (lit) {
                draw->AddRect(t_min, t_max, kColBorderActive, 4.0f * scale, 0, 1.0f);
            }
            DrawBoxedText(draw,t_min,t_max,fg_col,funcs[i].name);
            tag_x += tag_w + 6.0f * scale;
        }

        // Separator 2 & Insert hotkey badge
        draw->AddLine(ImVec2(tag_x + 4.0f * scale, p_min.y + 11.0f * scale), ImVec2(tag_x + 4.0f * scale, p_max.y - 11.0f * scale), IM_COL32(255, 255, 255, 38), 1.0f);

        ImVec2 ins_min(p_min.x+insert_offset,p_min.y+(hud_h-ins_sz.y-6.0f*scale)*0.5f);
        ImVec2 ins_max(ins_min.x + ins_sz.x + 12.0f * scale, ins_min.y + ins_sz.y + 6.0f * scale);
        draw->AddRectFilled(ins_min, ins_max, IM_COL32(255, 255, 255, 18), 4.0f * scale);
        DrawBoxedText(draw,ins_min,ins_max,kColTextMuted,ins_str);
        draw->PopClipRect();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// -----------------------------------------------------------------------------
// In-Game Menu Modal Window
// -----------------------------------------------------------------------------
static void DrawMenuModal(const UiState& s, bool& menu_open, float scale, UiEmit emit) {
    const float display_w = (s.display_width > 0.0f) ? s.display_width : ImGui::GetIO().DisplaySize.x;
    const float display_h = (s.display_height > 0.0f) ? s.display_height : ImGui::GetIO().DisplaySize.y;

    const float modal_w = 500.0f * scale; // 保持既定基准尺寸
    const float modal_x = (display_w - modal_w) * 0.5f;
    const float modal_y = display_h * 0.14f;

    ImGui::SetNextWindowPos(ImVec2(modal_x, modal_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(modal_w, 0.0f), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("##IstarothMenuModal", &menu_open, flags)) {
        ImGui::SetWindowFontScale(scale * 0.8f);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 p_min = ImGui::GetWindowPos();
        ImVec2 p_max = ImVec2(p_min.x + ImGui::GetWindowWidth(), p_min.y + ImGui::GetWindowHeight());
        float rounding = 18.0f * scale;

        DrawBackdropUnderlay(draw, s.backdrop, display_w, display_h, p_min, p_max, rounding, kColGlassModal);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * scale, 12.0f * scale));

        const float font_h = ImGui::GetFontSize();

        // ================= Header =================
        {
            const float header_h = 46.0f * scale;
            ImVec2 header_min = p_min;
            ImVec2 header_max(p_max.x, p_min.y + header_h);
            draw->AddLine(ImVec2(header_min.x, header_max.y), ImVec2(header_max.x, header_max.y), kColBorderSubtle, 1.0f);

            // Logo
            ImVec2 logo_center(header_min.x + 26.0f * scale, header_min.y + header_h * 0.5f);
            DrawStaticIcon(draw, s.icon_texture, logo_center, 30.0f * scale, kColBlueAccent);

            // Title Text
            draw->AddText(ImVec2(logo_center.x + 16.0f * scale, header_min.y + (header_h - font_h) * 0.5f), kColTextPrimary, u8"时之执政-伊斯塔露");

            ImGui::SetCursorScreenPos(ImVec2(p_min.x + 18.0f * scale, header_max.y + 14.0f * scale));
        }

        // ================= Card 1: Master Control =================
        {
            float card_w = modal_w - 36.0f * scale;
            const float card_h = 72.0f * scale;
            ImVec2 c_min = ImGui::GetCursorScreenPos();
            ImVec2 c_max(c_min.x + card_w, c_min.y + card_h);
            draw->AddRectFilled(c_min, c_max, kColCardBg, 12.0f * scale);
            draw->AddRect(c_min, c_max, kColBorderSubtle, 12.0f * scale, 0, 1.0f);

            float btn_w1 = 84.0f * scale;
            float btn_w2 = btn_w1;
            float btn_h  = 32.0f * scale;
            float btns_right = c_max.x - 14.0f * scale;
            float btn_y = c_min.y + (card_h - btn_h) * 0.5f;
            float text_max_x = btns_right - btn_w2 - 8.0f * scale - btn_w1 - 12.0f * scale;

            // Phase Title (大白话极简文案)
            const char* title_text = (s.phase == UiPhase::Active)     ? u8"剧情极速加速中" :
                                     (s.phase == UiPhase::Paused)     ? u8"已暂停：手动待机" :
                                     (s.phase == UiPhase::Waiting)    ? u8"等待常规对话就绪" :
                                     (s.phase == UiPhase::Confirming) ? u8"对话状态重验中" :
                                     (s.phase == UiPhase::Restoring)  ? u8"恢复基准原速中" :
                                     (s.phase == UiPhase::Starting)   ? u8"系统初始化就绪" :
                                                                        u8"状态待确认";
            float line1_y = c_min.y + 14.0f * scale;
            ImVec2 dot_pos(c_min.x + 16.0f * scale, line1_y + font_h * 0.5f);
            ImU32 dot_col = (s.phase == UiPhase::Active) ? kColBlueAccent : kColTextDim;
            draw->AddCircleFilled(dot_pos, 3.5f * scale, dot_col, 12);
            if (s.phase == UiPhase::Active) {
                draw->AddCircleFilled(dot_pos, 6.0f * scale, kColBlueGlow, 12);
            }

            ImVec4 clip_rect(c_min.x + 28.0f * scale, c_min.y, text_max_x, c_max.y);
            draw->AddText(nullptr, 0.0f, ImVec2(c_min.x + 28.0f * scale, line1_y), kColTextPrimary, title_text, nullptr, 0.0f, &clip_rect);

            // Line 2: Reason text (极简大白话)
            const char* hint_text = (s.reason[0] != 0) ? s.reason :
                                    (s.phase == UiPhase::Active) ? u8"对话加速进行中 · 按 F10 紧急停机" :
                                    (s.phase == UiPhase::Paused) ? u8"点击启用开始加速 · 按 F10 紧急停机" :
                                    u8"离开对话保持原速，进入对话自动加速";
            float line2_y = line1_y + font_h + 8.0f * scale;
            draw->AddText(nullptr, 0.0f, ImVec2(c_min.x + 28.0f * scale, line2_y), kColTextMuted, hint_text, nullptr, 0.0f, &clip_rect);

            // Stop F10 button
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,6.0f * scale);
            ImGui::SetCursorScreenPos(ImVec2(btns_right - btn_w2, btn_y));
            if (BoxedButton(u8"停机 F10", ImVec2(btn_w2, btn_h))) {
                if (emit) emit(UiCommand::Emergency, 0);
            }

            // Run / Pause button
            ImGui::SetCursorScreenPos(ImVec2(btns_right - btn_w2 - 8.0f * scale - btn_w1, btn_y));
            bool is_on = s.enabled;
            if (is_on) {
                ImGui::PushStyleColor(ImGuiCol_Button, kColBlueAccent);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(95, 180, 255, 255));
                ImGui::PushStyleColor(ImGuiCol_Text, kColDarkText);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 20));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 255, 255, 36));
                ImGui::PushStyleColor(ImGuiCol_Text, kColTextPrimary);
            }
            if (BoxedButton(is_on ? u8"暂停" : u8"启用", ImVec2(btn_w1, btn_h))) {
                if (emit) emit(UiCommand::Enabled, is_on ? 0 : 1);
            }
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();

            ImGui::SetCursorScreenPos(ImVec2(p_min.x + 18.0f * scale, c_max.y + 12.0f * scale));
        }

        // ================= Card 2: 4-Step Multiplier & Discrete Slider =================
        {
            float card_w = modal_w - 36.0f * scale;
            const float card_h = 124.0f * scale;
            ImVec2 c_min = ImGui::GetCursorScreenPos();
            ImVec2 c_max(c_min.x + card_w, c_min.y + card_h);
            draw->AddRectFilled(c_min, c_max, kColCardBg, 12.0f * scale);
            draw->AddRect(c_min, c_max, kColBorderSubtle, 12.0f * scale, 0, 1.0f);

            draw->AddText(ImVec2(c_min.x + 14.0f * scale, c_min.y + 14.0f * scale), kColTextPrimary, u8"剧情推进倍率 (4 档步进)");

            char actual_str[32];
            if (s.actual_known) {
                std::snprintf(actual_str, sizeof(actual_str), u8"实测：%.2f×", s.actual);
            } else {
                std::snprintf(actual_str, sizeof(actual_str), u8"实测：—");
            }

            ImVec2 act_sz = ImGui::CalcTextSize(actual_str);
            float readout_right = c_max.x - 14.0f * scale;
            ImVec2 pill_min(readout_right - act_sz.x - 12.0f * scale, c_min.y + 12.0f * scale);
            ImVec2 pill_max(readout_right, pill_min.y + act_sz.y + 4.0f * scale);
            draw->AddRectFilled(pill_min, pill_max, IM_COL32(255, 255, 255, 15), 4.0f * scale);
            DrawBoxedText(draw,pill_min,pill_max,(s.speed_active&&s.actual_known)?kColBlueAccent:kColTextMuted,actual_str);

            char target_str[16];
            std::snprintf(target_str, sizeof(target_str), u8"%u×", s.target);
            ImVec2 tgt_sz = ImGui::CalcTextSize(target_str);
            draw->AddText(ImVec2(pill_min.x - tgt_sz.x - 10.0f * scale, c_min.y + 12.0f * scale), kColBlueAccent, target_str);

            // Interactive Slider Widget (16px track, 20px white knob, 4x stardust)
            // Shared alignment baseline for slider, notches, and step labels
            const float thumb_r = 10.0f * scale;
            const float x_start = c_min.x + 18.0f * scale + thumb_r;
            const float x_end   = c_max.x - 18.0f * scale - thumb_r;
            const float track_y_mid = c_min.y + 54.0f * scale;
            const float step_w  = (x_end - x_start) / 3.0f;

            // Interactive Slider Widget (16px track, 20px white knob, strictly within card)
            ImGui::SetCursorScreenPos(ImVec2(x_start - thumb_r, track_y_mid - 14.0f * scale));
            unsigned cur_target = s.target;
            if (DrawDiscreteSlider("##StepSlider", &cur_target, scale, x_start, x_end, track_y_mid)) {
                if (emit && cur_target >= 1 && cur_target <= 4) {
                    emit(UiCommand::Multiplier, (int)cur_target);
                }
            }

            // 4 discrete step labels [1×] [2×] [3×] [4×] sharing exact notch centers cx
            const float notch_y = c_min.y + 82.0f * scale;
            const char* step_labels[4] = { "1×", "2×", "3×", "4×" };

            for (int i = 0; i < 4; ++i) {
                float cx = x_start + i * step_w;
                bool is_sel = (s.target == (unsigned)(i + 1));
                ImU32 g_col = is_sel ? kColBlueAccent : kColTextMuted;

                ImRect nbb(ImVec2(cx - 20.0f * scale, notch_y - 4.0f * scale),
                           ImVec2(cx + 20.0f * scale, notch_y + 24.0f * scale));
                ImGuiID nid = ImGui::GetID(step_labels[i]);
                bool nhov, nheld;
                if (ImGui::ButtonBehavior(nbb, nid, &nhov, &nheld) && emit) {
                    emit(UiCommand::Multiplier, i + 1);
                }

                ImVec2 tsz = ImGui::CalcTextSize(step_labels[i]);
                draw->AddText(ImVec2(cx - tsz.x * 0.5f, notch_y + 4.0f * scale), g_col, step_labels[i]);
            }

            ImGui::SetCursorScreenPos(ImVec2(p_min.x + 18.0f * scale, c_max.y + 12.0f * scale));
        }

        // ================= Card 3: Three Independent Capabilities =================
        {
            float card_w = modal_w - 36.0f * scale;
            struct CapRow {
                const char* title;
                const char* desc;
                const char* switch_id;
                bool configured;
                bool active;
                UiCommand cmd;
            };
            CapRow rows[3] = {
                { u8"自动推进对话", u8"单句输出完成后自动进入下一句", "##SwAdv", s.advance, s.advance_active, UiCommand::Advance },
                { u8"自动选择首项", u8"出现对话分支时自动选择第一个选项", "##SwFirst", s.choose_first, s.choose_active, UiCommand::ChooseFirst },
                { u8"跳过对话冷却", u8"解除文本硬直，无冷却快速跳过", "##SwSkip", s.skip_cooldown, s.skip_active, UiCommand::SkipCooldown }
            };

            const float sw_w = 34.0f * scale;
            const float sw_h = 18.0f * scale;
            const float cap_card_h = (font_h * 2.0f) + (26.0f * scale);

            for (int i = 0; i < 3; ++i) {
                ImVec2 c_min = ImGui::GetCursorScreenPos();
                ImVec2 c_max(c_min.x + card_w, c_min.y + cap_card_h);
                draw->AddRectFilled(c_min, c_max, kColCardBg, 8.0f * scale);
                draw->AddRect(c_min, c_max, kColBorderSubtle, 8.0f * scale, 0, 1.0f);

                float text_left = c_min.x + 14.0f * scale;
                float line1_y = c_min.y + 8.0f * scale;

                draw->AddText(ImVec2(text_left, line1_y), kColTextPrimary, rows[i].title);
                float tw = ImGui::CalcTextSize(rows[i].title).x;

                const char* tag_str = (!rows[i].configured) ? u8"关闭" :
                                      (rows[i].active)      ? u8"生效中" : u8"待机";
                ImU32 tag_bg = (!rows[i].configured) ? IM_COL32(255, 255, 255, 10) :
                               (rows[i].active)      ? kColBlueDim : IM_COL32(255, 255, 255, 15);
                ImU32 tag_fg = (!rows[i].configured) ? kColTextDim :
                               (rows[i].active)      ? kColBlueAccent : kColTextMuted;

                ImVec2 tag_sz = ImGui::CalcTextSize(tag_str);
                ImVec2 tg_min(text_left + tw + 8.0f * scale, line1_y);
                ImVec2 tg_max(tg_min.x + tag_sz.x + 8.0f * scale, tg_min.y + tag_sz.y + 2.0f * scale);
                draw->AddRectFilled(tg_min, tg_max, tag_bg, 3.0f * scale);
                DrawBoxedText(draw,tg_min,tg_max,tag_fg,tag_str);

                float line2_y = line1_y + font_h + 4.0f * scale;
                ImVec4 cap_clip(text_left, line2_y, c_max.x - sw_w - 20.0f * scale, c_max.y);
                draw->AddText(nullptr, 0.0f, ImVec2(text_left, line2_y), kColTextMuted, rows[i].desc, nullptr, 0.0f, &cap_clip);

                ImGui::SetCursorScreenPos(ImVec2(c_max.x - sw_w - 14.0f * scale, c_min.y + (cap_card_h - sw_h) * 0.5f));
                bool cur_cfg = rows[i].configured;
                if (DrawCleanSwitch(rows[i].switch_id, &cur_cfg, scale)) {
                    if (emit) emit(rows[i].cmd, cur_cfg ? 1 : 0);
                }

                ImGui::SetCursorScreenPos(ImVec2(p_min.x + 18.0f * scale, c_max.y + 8.0f * scale));
            }
        }

        // ================= Footer =================
        {
            ImVec2 f_min(p_min.x, ImGui::GetCursorScreenPos().y + 4.0f * scale);
            ImVec2 f_max(p_max.x, f_min.y + 36.0f * scale);
            draw->AddLine(f_min, ImVec2(f_max.x, f_min.y), kColBorderSubtle, 1.0f);

            draw->AddText(ImVec2(f_min.x + 18.0f * scale, f_min.y + (36.0f * scale - font_h) * 0.5f), kColTextMuted,
                          u8"按 Insert 打开或关闭面板      按 F10 紧急停机");

            ImGui::SetCursorScreenPos(ImVec2(p_min.x, f_max.y));
        }

        ImGui::PopStyleVar();
    }
    ImGui::End();
}

// -----------------------------------------------------------------------------
// Public Exported Entry Point
// -----------------------------------------------------------------------------
void DrawResidentUi(const UiState& s, bool& menu_open, float scale, UiEmit emit) {
    if (scale <= 0.0f) scale = 1.0f;

    // 1. Always render the Top-Center Full-Information HUD Overlay
    DrawTopCenterHud(s, menu_open, scale);

    // 2. Conditionally render the In-Game Menu Modal Window
    if (menu_open) {
        DrawMenuModal(s, menu_open, scale, emit);
    }
}

} // namespace istaroth
