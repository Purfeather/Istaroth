#include "resident_ui.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <cmath>
#include <cfloat>
#include <cstdio>

namespace istaroth {

// -----------------------------------------------------------------------------
// Scheme C: Astral Azure on Frosted Glass + Semantic Phase Palette
// -----------------------------------------------------------------------------
static constexpr ImU32 kColGlassModal   = IM_COL32(12, 18, 30, 200); // rgba(12, 18, 30, 0.78)
static constexpr ImU32 kColGlassHud     = IM_COL32(10, 16, 26, 214); // rgba(10, 16, 26, 0.84)
static constexpr ImU32 kColCardBg       = IM_COL32(255, 255, 255, 10); // rgba(255, 255, 255, 0.04)
static constexpr ImU32 kColCardBgHover  = IM_COL32(255, 255, 255, 18); // rgba(255, 255, 255, 0.07)
static constexpr ImU32 kColBorderSubtle = IM_COL32(255, 255, 255, 38); // rgba(255, 255, 255, 0.15)
static constexpr ImU32 kColBorderRim    = IM_COL32(0, 0, 0, 72);       // subtle outer contrast rim
static constexpr ImU32 kColBorderActive = IM_COL32(80, 170, 255, 166); // rgba(80, 170, 255, 0.65)
static constexpr ImU32 kColBlueAccent   = IM_COL32(80, 170, 255, 255); // #50AAFF
static constexpr ImU32 kColBlueGlow     = IM_COL32(80, 170, 255, 97);  // rgba(80, 170, 255, 0.38)
static constexpr ImU32 kColBlueDim      = IM_COL32(80, 170, 255, 45);  // rgba(80, 170, 255, 0.18)
static constexpr ImU32 kColFaultAccent  = IM_COL32(248, 81, 73, 255);  // #F85149 (Fault)
static constexpr ImU32 kColTextPrimary  = IM_COL32(255, 255, 255, 255);
static constexpr ImU32 kColTextMuted    = IM_COL32(255, 255, 255, 178); // rgba(255, 255, 255, 0.70)
static constexpr ImU32 kColTextDim      = IM_COL32(255, 255, 255, 108); // rgba(255, 255, 255, 0.42)
static constexpr ImU32 kColWhite        = IM_COL32(255, 255, 255, 255);
static constexpr ImU32 kColDarkText     = IM_COL32(6, 9, 14, 255);
static constexpr ImU32 kColStopFill     = IM_COL32(248, 81, 73, 30);
static constexpr ImU32 kColStopHover    = IM_COL32(248, 81, 73, 54);
static constexpr ImU32 kColStopBorder   = IM_COL32(248, 81, 73, 108);
static constexpr ImU32 kColStopText     = IM_COL32(255, 176, 172, 255);

static ImU32 WithAlpha(ImU32 col, int alpha) {
    ImU32 a = (ImU32)ImClamp(alpha, 0, 255);
    return (col & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

static ImU32 LerpColor(ImU32 a, ImU32 b, float t) {
    t = ImClamp(t, 0.0f, 1.0f);
    int ar = (int)((a >> IM_COL32_R_SHIFT) & 0xFF), ag = (int)((a >> IM_COL32_G_SHIFT) & 0xFF);
    int ab = (int)((a >> IM_COL32_B_SHIFT) & 0xFF), aa = (int)((a >> IM_COL32_A_SHIFT) & 0xFF);
    int br = (int)((b >> IM_COL32_R_SHIFT) & 0xFF), bg = (int)((b >> IM_COL32_G_SHIFT) & 0xFF);
    int bb = (int)((b >> IM_COL32_B_SHIFT) & 0xFF), ba = (int)((b >> IM_COL32_A_SHIFT) & 0xFF);
    return IM_COL32(
        (int)std::round(ar + (br - ar) * t),
        (int)std::round(ag + (bg - ag) * t),
        (int)std::round(ab + (bb - ab) * t),
        (int)std::round(aa + (ba - aa) * t));
}

static float AnimateFloat(ImGuiID id, float target, float speed) {
    ImGuiStorage* storage = ImGui::GetStateStorage();
    float cur = storage->GetFloat(id, -1.0f);
    if (cur < 0.0f) {
        storage->SetFloat(id, target);
        return target;
    }
    float dt = ImClamp(ImGui::GetIO().DeltaTime, 0.0f, 0.05f);
    if (dt > 0.0f && std::fabs(target - cur) > 0.001f) {
        cur += (target - cur) * (1.0f - std::exp(-speed * dt));
    } else if (std::fabs(target - cur) <= 0.001f) {
        cur = target;
    }
    storage->SetFloat(id, cur);
    return cur;
}

struct PhaseStyle {
    ImU32 accent;
    ImU32 text;
    ImU32 hud_border;
    ImU32 card_border;
    ImU32 tag_bg;
    ImU32 tag_fg;
    ImU32 tag_border;
    bool has_halo;
};

static PhaseStyle GetPhaseStyle(const UiState& s) {
    if (s.phase == UiPhase::Fault) {
        return { kColFaultAccent, kColFaultAccent, WithAlpha(kColFaultAccent, 150), WithAlpha(kColFaultAccent, 108),
                 IM_COL32(255, 255, 255, 8), kColTextDim, 0, true };
    }
    if (s.enabled && s.phase == UiPhase::Active) {
        return { kColBlueAccent, kColBlueAccent, kColBorderActive, WithAlpha(kColBlueAccent, 96),
                 kColBlueDim, kColBlueAccent, kColBorderActive, true };
    }
    if (s.enabled && s.phase != UiPhase::Paused) {
        return { kColBlueAccent, kColTextPrimary, IM_COL32(255, 255, 255, 38), WithAlpha(kColBlueAccent, 96),
                 IM_COL32(255, 255, 255, 18), kColTextPrimary, IM_COL32(255, 255, 255, 38), false };
    }
    return { kColTextDim, kColTextDim, IM_COL32(255, 255, 255, 18), kColBorderSubtle,
             IM_COL32(255, 255, 255, 8), kColTextDim, 0, false };
}

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
    draw->AddRect(ImVec2(min_pos.x - 1.0f, min_pos.y - 1.0f), ImVec2(max_pos.x + 1.0f, max_pos.y + 1.0f), kColBorderRim, rounding + 1.0f, 0, 1.0f);
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
static void DrawSwitchVisual(ImDrawList* draw, ImGuiID anim_seed, const ImRect& bb, bool on, bool hovered, float scale) {
    float width = bb.Max.x - bb.Min.x;
    float height = bb.Max.y - bb.Min.y;
    float rounding = height * 0.5f;
    float t = AnimateFloat(anim_seed, on ? 1.0f : 0.0f, 18.0f);
    float ht = AnimateFloat(anim_seed + 1u, hovered ? 1.0f : 0.0f, 16.0f);

    ImU32 rail_off = LerpColor(IM_COL32(255, 255, 255, 36), IM_COL32(255, 255, 255, 54), ht);
    ImU32 rail_on  = LerpColor(kColBlueAccent, IM_COL32(104, 186, 255, 255), ht);
    ImU32 rail_col = LerpColor(rail_off, rail_on, t);
    draw->AddRectFilled(bb.Min, bb.Max, rail_col, rounding);
    if (t < 0.99f) {
        draw->AddRect(bb.Min, bb.Max, WithAlpha(IM_COL32(255, 255, 255, 34), (int)std::round(34.0f * (1.0f - t))), rounding, 0, 1.0f);
    }

    float knob_radius = 6.0f * scale;
    float x_off = 3.0f * scale + knob_radius;
    float x_on  = width - 3.0f * scale - knob_radius;
    float knob_offset_x = x_off + (x_on - x_off) * t;
    ImVec2 knob_center(bb.Min.x + knob_offset_x, bb.Min.y + height * 0.5f);
    draw->AddCircleFilled(ImVec2(knob_center.x, knob_center.y + 1.0f * scale), knob_radius + 0.5f * scale, IM_COL32(0, 0, 0, 64), 16);
    draw->AddCircleFilled(knob_center, knob_radius, LerpColor(IM_COL32(228, 234, 242, 255), kColWhite, t), 16);
}

// -----------------------------------------------------------------------------
// Helper: Keycap Badge
// -----------------------------------------------------------------------------
static float DrawKeycap(ImDrawList* draw, const ImVec2& pos, const char* label, float scale, ImU32 fg, ImU32 bg, ImU32 border) {
    ImVec2 sz = ImGui::CalcTextSize(label);
    ImVec2 k_min(pos.x, pos.y);
    ImVec2 k_max(pos.x + sz.x + 12.0f * scale, pos.y + sz.y + 4.0f * scale);
    draw->AddRectFilled(k_min, k_max, bg, 4.0f * scale);
    draw->AddRect(k_min, k_max, border, 4.0f * scale, 0, 1.0f);
    DrawBoxedText(draw, k_min, k_max, fg, label);
    return k_max.x - k_min.x;
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
    float target_norm = (float)(step - 1) / 3.0f;
    float fill_norm = AnimateFloat(imgui_id + 1u, target_norm, held ? 32.0f : 20.0f);
    float halo_t    = AnimateFloat(imgui_id + 2u, (step == 4 || hovered || held) ? 1.0f : 0.0f, 14.0f);

    float thumb_center_x = x_start + fill_norm * active_len;
    ImVec2 fill_min = track_min;
    ImVec2 fill_max(thumb_center_x + thumb_r, track_max.y);

    if (step == 4 && fill_norm > 0.995f) {
        // True rounded geometry colored by vertex gradient (no rectangular clipping artifacts)
        const int vtx_start = draw->VtxBuffer.Size;
        draw->AddRectFilled(track_min, track_max, IM_COL32_WHITE, rounding);
        const int vtx_end = draw->VtxBuffer.Size;
        ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw, vtx_start, vtx_end,
                                                     track_min, ImVec2(track_max.x, track_min.y),
                                                     IM_COL32(50, 114, 230, 255), IM_COL32(136, 194, 255, 255));
    } else {
        draw->AddRectFilled(fill_min, fill_max, kColBlueAccent, rounding);
    }

    // 3. Embedded notch dots exactly at step centers
    for (int i = 0; i < 4; ++i) {
        float notch_x = x_start + ((float)i / 3.0f) * active_len;
        bool reached = (unsigned)(i + 1) <= step;
        ImU32 notch_col = reached ? IM_COL32(255, 255, 255, 190) : IM_COL32(255, 255, 255, 115);
        draw->AddCircleFilled(ImVec2(notch_x, track_y_mid), 2.1f * scale, notch_col, 12);
    }

    // 4. Prominent 20px white circular draggable thumb knob
    ImVec2 thumb_center(thumb_center_x, track_y_mid);
    draw->AddCircleFilled(ImVec2(thumb_center.x, thumb_center.y + 1.5f * scale), thumb_r + 1.0f * scale, IM_COL32(0, 0, 0, 110), 24);
    if (halo_t > 0.01f) {
        draw->AddCircleFilled(thumb_center, thumb_r + 3.5f * scale * halo_t, WithAlpha(kColBlueAccent, (int)std::round(97.0f * halo_t)), 24);
    }
    draw->AddCircleFilled(thumb_center, thumb_r, kColWhite, 24);
    draw->AddCircle(thumb_center, thumb_r, IM_COL32(0, 0, 0, 20), 24, 1.0f);
    if (step == 4) {
        draw->AddCircleFilled(thumb_center, 3.0f * scale, kColBlueAccent, 16);
    }

    return changed;
}

// -----------------------------------------------------------------------------
// Top-Center Full-Information HUD Overlay
// -----------------------------------------------------------------------------
static void DrawTopCenterHud(const UiState& s, bool& menu_open, float scale) {
    const float display_w = (s.display_width > 0.0f) ? s.display_width : ImGui::GetIO().DisplaySize.x;
    const float display_h = (s.display_height > 0.0f) ? s.display_height : ImGui::GetIO().DisplaySize.y;
    const PhaseStyle ps = GetPhaseStyle(s);
    const bool show_live_tags = s.enabled && s.phase != UiPhase::Paused && s.phase != UiPhase::Fault;
    struct HudFunc { const char* name; bool configured; };
    HudFunc funcs[3]={{u8"推进",s.advance},{u8"首选",s.choose_first},{u8"冷却",s.skip_cooldown}};
    char speed_buf[16];
    if(s.phase!=UiPhase::Fault&&s.actual_known)std::snprintf(speed_buf,sizeof(speed_buf),u8"%.1f×",s.actual);
    else std::snprintf(speed_buf,sizeof(speed_buf),u8"—");
    ImFont* font=ImGui::GetFont();float font_size=16.0f*scale;
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
        ImGui::SetWindowFontScale(font_size / GImGui->FontBaseSize);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 p_min = ImGui::GetWindowPos();
        ImVec2 window_size=ImGui::GetWindowSize();
        ImVec2 p_max(p_min.x+window_size.x,p_min.y+window_size.y);
        float rounding = hud_h * 0.5f;
        // Begin() clips to the inner content rectangle; the capsule stroke needs its AA fringe too.
        draw->PushClipRect(ImVec2(p_min.x-2,p_min.y-2),ImVec2(p_max.x+2,p_max.y+2),false);

        bool hud_hovered = ImGui::IsWindowHovered();
        float hud_ht = AnimateFloat(ImGui::GetID("##HudHoverAnim"), hud_hovered ? 1.0f : 0.0f, 16.0f);
        DrawBackdropUnderlay(draw, s.backdrop, display_w, display_h, p_min, p_max, rounding, kColGlassHud);
        ImU32 bd_col = LerpColor(ps.hud_border, WithAlpha(ps.text, 150), hud_ht * 0.5f);
        draw->AddRect(p_min, p_max, bd_col, rounding, 0, 1.2f);

        if (hud_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            menu_open = !menu_open;
        }

        // 1. Left Icon: Mini Compass
        ImVec2 compass_center(p_min.x + 20.0f * scale, p_min.y + hud_h * 0.5f);
        DrawStaticIcon(draw, s.icon_texture, compass_center, 26.0f * scale, ps.text);

        // 2. Speed readout
        ImVec2 text_pos(p_min.x+35.0f*scale,p_min.y);
        ImU32 text_col = (s.phase == UiPhase::Fault) ? kColFaultAccent :
                         (s.enabled && s.phase == UiPhase::Active && s.speed_active && s.actual_known) ? kColBlueAccent : kColTextPrimary;
        DrawBoxedText(draw,text_pos,ImVec2(text_pos.x+speed_slot,p_max.y),text_col,speed_buf);

        // Live status dot
        ImVec2 dot_pos(p_min.x+dot_offset,p_min.y+hud_h*0.5f);
        if (ps.has_halo) draw->AddCircleFilled(dot_pos, 5.0f * scale, WithAlpha(ps.accent, 80), 12);
        draw->AddCircleFilled(dot_pos, 3.0f * scale, ps.accent, 12);

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

            bool lit = show_live_tags && funcs[i].configured;
            ImU32 bg_col = lit ? ps.tag_bg : IM_COL32(255, 255, 255, 8);
            ImU32 fg_col = lit ? ps.tag_fg : kColTextDim;
            draw->AddRectFilled(t_min, t_max, bg_col, 4.0f * scale);
            if (lit && ps.tag_border != 0) {
                draw->AddRect(t_min, t_max, ps.tag_border, 4.0f * scale, 0, 1.0f);
            }
            DrawBoxedText(draw,t_min,t_max,fg_col,funcs[i].name);
            tag_x += tag_w + 6.0f * scale;
        }

        // Separator 2 & Insert hotkey badge
        draw->AddLine(ImVec2(tag_x + 4.0f * scale, p_min.y + 11.0f * scale), ImVec2(tag_x + 4.0f * scale, p_max.y - 11.0f * scale), IM_COL32(255, 255, 255, 38), 1.0f);

        ImVec2 ins_min(p_min.x+insert_offset,p_min.y+(hud_h-ins_sz.y-6.0f*scale)*0.5f);
        ImVec2 ins_max(ins_min.x + ins_sz.x + 12.0f * scale, ins_min.y + ins_sz.y + 6.0f * scale);
        draw->AddRectFilled(ins_min, ins_max, LerpColor(IM_COL32(255, 255, 255, 18), IM_COL32(255, 255, 255, 30), hud_ht), 4.0f * scale);
        draw->AddRect(ins_min, ins_max, IM_COL32(255, 255, 255, 34), 4.0f * scale, 0, 1.0f);
        DrawBoxedText(draw,ins_min,ins_max,kColTextMuted,ins_str);
        draw->PopClipRect();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// -----------------------------------------------------------------------------
// In-Game Menu Modal Window
// -----------------------------------------------------------------------------
static void DrawMenuModal(const UiState& snapshot, bool& menu_open, float scale, UiEmit emit, float opacity) {
    UiState s = snapshot;
    if (s.phase == UiPhase::Active) s.phase = UiPhase::Waiting;
    const float display_w = (s.display_width > 0.0f) ? s.display_width : ImGui::GetIO().DisplaySize.x;
    const float display_h = (s.display_height > 0.0f) ? s.display_height : ImGui::GetIO().DisplaySize.y;
    const PhaseStyle ps = GetPhaseStyle(s);

    const float modal_w = 500.0f * scale; // 保持既定基准尺寸
    const float modal_x = (display_w - modal_w) * 0.5f;
    const float modal_y = display_h * 0.14f;

    ImGui::SetNextWindowPos(ImVec2(modal_x, modal_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(modal_w, 0.0f), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBackground;

    if (!menu_open) flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing;

    if (ImGui::Begin("##IstarothMenuModal", &menu_open, flags)) {
        ImGui::SetWindowFontScale((16.0f * scale) / GImGui->FontBaseSize);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 p_min = ImGui::GetWindowPos();
        ImVec2 p_max = ImVec2(p_min.x + ImGui::GetWindowWidth(), p_min.y + ImGui::GetWindowHeight());
        float rounding = 18.0f * scale;

        draw->PushClipRect(ImVec2(p_min.x - 2.0f, p_min.y - 2.0f), ImVec2(p_max.x + 2.0f, p_max.y + 2.0f), false);
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

            // Subtle right-side close button
            float close_sz = 26.0f * scale;
            ImVec2 close_min(header_max.x - 14.0f * scale - close_sz, header_min.y + (header_h - close_sz) * 0.5f);
            ImRect close_bb(close_min, ImVec2(close_min.x + close_sz, close_min.y + close_sz));
            ImGuiID close_id = ImGui::GetID("##MenuCloseBtn");
            bool close_hov = false, close_held = false;
            if (ImGui::ButtonBehavior(close_bb, close_id, &close_hov, &close_held)) {
                menu_open = false;
            }
            if (close_hov) {
                draw->AddRectFilled(close_bb.Min, close_bb.Max, kColCardBgHover, 6.0f * scale);
            }
            ImVec2 cc = close_bb.GetCenter();
            float cr = 4.5f * scale;
            ImU32 close_col = close_hov ? kColTextPrimary : kColTextMuted;
            draw->AddLine(ImVec2(cc.x - cr, cc.y - cr), ImVec2(cc.x + cr, cc.y + cr), close_col, 1.4f * scale);
            draw->AddLine(ImVec2(cc.x + cr, cc.y - cr), ImVec2(cc.x - cr, cc.y + cr), close_col, 1.4f * scale);

            ImGui::SetCursorScreenPos(ImVec2(p_min.x + 18.0f * scale, header_max.y + 14.0f * scale));
        }

        // ================= Card 1: Master Control =================
        {
            float card_w = modal_w - 36.0f * scale;
            const char* title_text = (s.phase == UiPhase::Paused)     ? u8"已暂停" :
                                     (s.phase == UiPhase::Waiting)    ? (s.dialogue_ready ? u8"已启用 · 面板暂挂中" : u8"已启用 · 监听对话中") :
                                     (s.phase == UiPhase::Confirming) ? u8"正在校验状态" :
                                     (s.phase == UiPhase::Restoring)  ? u8"正在恢复原速" :
                                     (s.phase == UiPhase::Starting)   ? u8"正在初始化" :
                                     (s.phase == UiPhase::Fault)      ? u8"组件异常 · 已停止" :
                                                                        u8"状态待确认";
            const char* hint_text = (s.reason[0] != 0) ? s.reason :
                                    (s.phase == UiPhase::Paused) ? u8"当前保持 1× 原速，点击「启用」开始" :
                                    u8"当前保持原速；关闭面板后进入对话自动加速";
            const float text_width = card_w - 148.0f * scale;
            float hint_height = ImGui::CalcTextSize(hint_text, nullptr, false, text_width).y;
            const float card_h = ImMax(72.0f * scale, font_h + hint_height + 36.0f * scale);
            ImVec2 c_min = ImGui::GetCursorScreenPos();
            ImVec2 c_max(c_min.x + card_w, c_min.y + card_h);
            draw->AddRectFilled(c_min, c_max, kColCardBg, 12.0f * scale);
            draw->AddRect(c_min, c_max, ps.card_border, 12.0f * scale, 0, 1.0f);

            float btn_w = 92.0f * scale;
            float btn_h  = 32.0f * scale;
            float btns_right = c_max.x - 14.0f * scale;
            float btn_y = c_min.y + (card_h - btn_h) * 0.5f;
            float text_max_x = btns_right - btn_w - 14.0f * scale;

            float line1_y = c_min.y + 14.0f * scale;
            ImVec2 dot_pos(c_min.x + 16.0f * scale, line1_y + font_h * 0.5f);
            draw->AddCircleFilled(dot_pos, 3.5f * scale, ps.accent, 12);
            if ((s.enabled && s.phase == UiPhase::Waiting) || s.phase == UiPhase::Fault)
                draw->AddCircleFilled(dot_pos, 6.0f * scale, WithAlpha(ps.accent, 80), 12);

            ImVec4 clip_rect(c_min.x + 28.0f * scale, c_min.y, text_max_x, c_max.y);
            draw->AddText(nullptr, 0.0f, ImVec2(c_min.x + 28.0f * scale, line1_y), kColTextPrimary, title_text, nullptr, 0.0f, &clip_rect);

            float line2_y = line1_y + font_h + 8.0f * scale;
            draw->AddText(nullptr, 0.0f, ImVec2(c_min.x + 28.0f * scale, line2_y), kColTextMuted, hint_text, nullptr, text_width, &clip_rect);

            // Merged Start / Stop button
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f * scale);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            ImGui::SetCursorScreenPos(ImVec2(btns_right - btn_w, btn_y));
            bool is_on = s.enabled;
            if (is_on) {
                ImGui::PushStyleColor(ImGuiCol_Button, kColBlueAccent);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(95, 180, 255, 255));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70, 156, 240, 255));
                ImGui::PushStyleColor(ImGuiCol_Border, kColBlueAccent);
                ImGui::PushStyleColor(ImGuiCol_Text, kColDarkText);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 20));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 255, 255, 36));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(255, 255, 255, 48));
                ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 255, 255, 34));
                ImGui::PushStyleColor(ImGuiCol_Text, kColTextPrimary);
            }
            if (BoxedButton(is_on ? u8"停止" : u8"启用", ImVec2(btn_w, btn_h))) {
                if (emit) emit(UiCommand::Enabled, is_on ? 0 : 1);
            }
            ImGui::PopStyleColor(5);
            ImGui::PopStyleVar(2);

            ImGui::SetCursorScreenPos(ImVec2(p_min.x + 18.0f * scale, c_max.y + 12.0f * scale));
        }

        // ================= Card 2: 4-Step Multiplier & Discrete Slider =================
        {
            float card_w = modal_w - 36.0f * scale;
            const float card_h = 116.0f * scale;
            ImVec2 c_min = ImGui::GetCursorScreenPos();
            ImVec2 c_max(c_min.x + card_w, c_min.y + card_h);
            draw->AddRectFilled(c_min, c_max, kColCardBg, 12.0f * scale);
            draw->AddRect(c_min, c_max, kColBorderSubtle, 12.0f * scale, 0, 1.0f);

            float row_top_y = c_min.y + 13.0f * scale;
            draw->AddText(ImVec2(c_min.x + 14.0f * scale, row_top_y), kColTextPrimary, u8"对话加速倍率");

            char rule_str[64];
            if (!s.enabled) std::snprintf(rule_str, sizeof(rule_str), u8"当前：1× 原速");
            else if (s.target == 1) std::snprintf(rule_str, sizeof(rule_str), u8"原速执行 · 面板暂挂");
            else std::snprintf(rule_str, sizeof(rule_str), u8"目标 %u× · 面板暂挂 1×", s.target);

            ImVec2 act_sz = ImGui::CalcTextSize(rule_str);
            float readout_right = c_max.x - 14.0f * scale;
            bool act_lit = s.enabled;
            ImVec2 pill_min(readout_right - act_sz.x - 14.0f * scale, row_top_y - 2.0f * scale);
            ImVec2 pill_max(readout_right, row_top_y + font_h + 2.0f * scale);
            draw->AddRectFilled(pill_min, pill_max, act_lit ? IM_COL32(80, 170, 255, 24) : IM_COL32(255, 255, 255, 18), 4.0f * scale);
            draw->AddRect(pill_min, pill_max, act_lit ? IM_COL32(80, 170, 255, 86) : IM_COL32(255, 255, 255, 32), 4.0f * scale, 0, 1.0f);
            DrawBoxedText(draw,pill_min,pill_max,act_lit?kColBlueAccent:kColTextMuted,rule_str);

            if (!s.enabled) {
                char target_str[32];std::snprintf(target_str, sizeof(target_str), u8"目标 %u×", s.target);
                ImVec2 tgt_sz = ImGui::CalcTextSize(target_str);
                draw->AddText(ImVec2(pill_min.x - tgt_sz.x - 10.0f * scale, row_top_y), kColTextMuted, target_str);
            }

            // Interactive Slider Widget (16px track, 20px white knob, 4x stardust)
            // Shared alignment baseline for slider, notches, and step labels
            const float thumb_r = 10.0f * scale;
            const float x_start = c_min.x + 18.0f * scale + thumb_r;
            const float x_end   = c_max.x - 18.0f * scale - thumb_r;
            const float track_y_mid = c_min.y + 56.0f * scale;
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
            const float notch_y = c_min.y + 76.0f * scale;
            const char* step_labels[4] = { "1×", "2×", "3×", "4×" };

            for (int i = 0; i < 4; ++i) {
                float cx = x_start + i * step_w;
                bool is_sel = (s.target == (unsigned)(i + 1));

                ImRect nbb(ImVec2(cx - 20.0f * scale, notch_y - 4.0f * scale),
                           ImVec2(cx + 20.0f * scale, notch_y + 24.0f * scale));
                ImGuiID nid = ImGui::GetID(step_labels[i]);
                bool nhov, nheld;
                if (ImGui::ButtonBehavior(nbb, nid, &nhov, &nheld) && emit) {
                    emit(UiCommand::Multiplier, i + 1);
                }
                ImU32 g_col = is_sel ? kColBlueAccent : (nhov ? kColTextPrimary : kColTextMuted);

                ImVec2 tsz = ImGui::CalcTextSize(step_labels[i]);
                draw->AddText(ImVec2(cx - tsz.x * 0.5f, notch_y + 4.0f * scale), g_col, step_labels[i]);
                if (is_sel) {
                    float ul_y = notch_y + 4.0f * scale + font_h + 2.0f * scale;
                    draw->AddLine(ImVec2(cx - 8.0f * scale, ul_y), ImVec2(cx + 8.0f * scale, ul_y), kColBlueAccent, 2.0f * scale);
                }
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
                UiCommand cmd;
            };
            CapRow rows[3] = {
                { u8"自动推进对话", u8"单句输出完成后自动进入下一句", "##SwAdv", s.advance, UiCommand::Advance },
                { u8"自动选择首项", u8"出现对话分支时自动选择第一个选项", "##SwFirst", s.choose_first, UiCommand::ChooseFirst },
                { u8"跳过对话冷却", u8"跳过对话切换冷却，连续推进不卡顿", "##SwSkip", s.skip_cooldown, UiCommand::SkipCooldown }
            };

            const float sw_w = 34.0f * scale;
            const float sw_h = 18.0f * scale;
            const float cap_card_h = (font_h * 2.0f) + (26.0f * scale);

            for (int i = 0; i < 3; ++i) {
                ImVec2 c_min = ImGui::GetCursorScreenPos();
                ImVec2 c_max(c_min.x + card_w, c_min.y + cap_card_h);
                ImRect card_bb(c_min, c_max);
                ImGui::ItemSize(card_bb, 0.0f);
                ImGuiID row_id = ImGui::GetID(rows[i].switch_id);
                bool row_hov = false, row_held = false;
                bool row_pressed = ImGui::ItemAdd(card_bb, row_id) && ImGui::ButtonBehavior(card_bb, row_id, &row_hov, &row_held);
                float row_ht = AnimateFloat(row_id + 10u, row_hov ? 1.0f : 0.0f, 16.0f);

                draw->AddRectFilled(c_min, c_max, LerpColor(kColCardBg, kColCardBgHover, row_ht), 8.0f * scale);
                draw->AddRect(c_min, c_max, LerpColor(kColBorderSubtle, IM_COL32(255, 255, 255, 58), row_ht), 8.0f * scale, 0, 1.0f);

                float text_left = c_min.x + 14.0f * scale;
                float line1_y = c_min.y + 8.0f * scale;
                float line1_mid = line1_y + font_h * 0.5f;

                draw->AddText(ImVec2(text_left, line1_y), kColTextPrimary, rows[i].title);
                float tw = ImGui::CalcTextSize(rows[i].title).x;

                bool ready = rows[i].configured && s.enabled;
                const char* tag_str = (!rows[i].configured) ? u8"已关闭" :
                                      ready ? u8"已就绪" : u8"待启用";
                ImU32 tag_bg = (!rows[i].configured) ? IM_COL32(255, 255, 255, 10) :
                               ready ? kColBlueDim : IM_COL32(255, 255, 255, 18);
                ImU32 tag_fg = (!rows[i].configured) ? kColTextDim :
                               ready ? kColBlueAccent : kColTextMuted;
                ImU32 tag_bd = (!rows[i].configured) ? IM_COL32(255, 255, 255, 24) :
                               ready ? IM_COL32(80, 170, 255, 92) : IM_COL32(255, 255, 255, 36);

                ImVec2 tag_sz = ImGui::CalcTextSize(tag_str);
                float tag_h = tag_sz.y + 2.0f * scale;
                ImVec2 tg_min(text_left + tw + 8.0f * scale, line1_mid - tag_h * 0.5f);
                ImVec2 tg_max(tg_min.x + tag_sz.x + 10.0f * scale, line1_mid + tag_h * 0.5f);
                draw->AddRectFilled(tg_min, tg_max, tag_bg, 3.0f * scale);
                draw->AddRect(tg_min, tg_max, tag_bd, 3.0f * scale, 0, 1.0f);
                DrawBoxedText(draw,tg_min,tg_max,tag_fg,tag_str);

                float line2_y = line1_y + font_h + 4.0f * scale;
                ImVec4 cap_clip(text_left, line2_y, c_max.x - sw_w - 20.0f * scale, c_max.y);
                draw->AddText(nullptr, 0.0f, ImVec2(text_left, line2_y), kColTextMuted, rows[i].desc, nullptr, 0.0f, &cap_clip);

                ImVec2 sw_min(c_max.x - sw_w - 14.0f * scale, c_min.y + (cap_card_h - sw_h) * 0.5f);
                DrawSwitchVisual(draw, row_id + 1u, ImRect(sw_min, ImVec2(sw_min.x + sw_w, sw_min.y + sw_h)), rows[i].configured, row_hov, scale);
                if (row_pressed && emit) {
                    emit(rows[i].cmd, rows[i].configured ? 0 : 1);
                }

                ImGui::SetCursorScreenPos(ImVec2(p_min.x + 18.0f * scale, c_max.y + 8.0f * scale));
            }
        }

        // ================= Footer =================
        {
            ImVec2 f_min(p_min.x, ImGui::GetCursorScreenPos().y + 4.0f * scale);
            ImVec2 f_max(p_max.x, f_min.y + 36.0f * scale);
            draw->AddLine(f_min, ImVec2(f_max.x, f_min.y), kColBorderSubtle, 1.0f);

            float fx = f_min.x + 18.0f * scale;
            float fy = f_min.y + (36.0f * scale - font_h) * 0.5f;
            float ky = fy - 2.0f * scale;
            const float gap = 5.0f * scale;
            ImU32 cap_bg = IM_COL32(255, 255, 255, 18);
            ImU32 cap_bd = IM_COL32(255, 255, 255, 34);
            auto push_text = [&](const char* txt) {
                draw->AddText(ImVec2(fx, fy), kColTextMuted, txt);
                fx += ImGui::CalcTextSize(txt).x;
            };
            push_text(u8"按");
            fx += gap;
            fx += DrawKeycap(draw, ImVec2(fx, ky), "Insert", scale, kColTextPrimary, cap_bg, cap_bd) + gap;
            push_text(u8"开关面板    按");
            fx += gap;
            fx += DrawKeycap(draw, ImVec2(fx, ky), "F10", scale, kColStopText, kColStopFill, kColStopBorder) + gap;
            push_text(u8"立即停止并恢复原速");

            ImGui::SetCursorScreenPos(ImVec2(p_min.x, f_max.y));
        }

        ImGui::PopStyleVar();
        draw->PopClipRect();
        // Fade every vertex, including custom colors and the blurred backdrop.
        if (opacity < 1.f) for (auto& vertex : draw->VtxBuffer) {
            int alpha = (int)((vertex.col >> IM_COL32_A_SHIFT) & 0xff);
            vertex.col = WithAlpha(vertex.col, (int)std::round(alpha * opacity));
        }
    }
    ImGui::End();
}

// -----------------------------------------------------------------------------
// Public Exported Entry Point
// -----------------------------------------------------------------------------
float StepMenuOpacity(float current, bool menu_open, float delta_time) {
    float target = menu_open ? 1.f : 0.f;
    float dt = std::isfinite(delta_time) ? ImClamp(delta_time, 0.f, .05f) : 0.f;
    current += (target - current) * (1.f - std::exp(-20.f * dt));
    return std::fabs(target - current) < .01f ? target : current;
}

void DrawResidentUi(const UiState& s, bool& menu_open, float scale, UiEmit emit, float menu_opacity) {
    if (scale <= 0.0f) scale = 1.0f;

    // 1. Always render the Top-Center Full-Information HUD Overlay
    DrawTopCenterHud(s, menu_open, scale);

    // Visual fade-out continues after input ownership has returned to the game.
    if (menu_opacity < 0.f) menu_opacity = menu_open ? 1.f : 0.f;
    if (menu_open || menu_opacity > .01f) {
        DrawMenuModal(s, menu_open, scale, menu_open ? emit : nullptr, ImClamp(menu_opacity, 0.f, 1.f));
    }
}

} // namespace istaroth
