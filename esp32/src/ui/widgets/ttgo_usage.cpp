#include "ttgo_usage.h"
#if defined(BOARD_TTGO)
#include "ttgo_usage_layout.h"
#include "../display.h"
#include "../theme.h"
#include "../../state/agent_state.h"
#include <cstdio>
#include <cstring>

LV_FONT_DECLARE(font_ttgo_plex_12);
LV_FONT_DECLARE(font_ttgo_plex_28);

namespace TTGO { namespace Usage {
static bool selected = true;
static lv_obj_t* root;
static lv_obj_t* empty;
struct Card {
    lv_obj_t *panel, *title, *value, *reset, *bar;
    char titleText[20], valueText[8], resetText[28];
};
// Five bounded cards are allocated by LVGL once per screen lifetime (Claude
// 5h/7d + Codex 5h/7d + z.ai 5h — #350). z.ai shows the credits window only;
// the MCP quota is hidden on this 135px screen. Label storage is static, so
// changing telemetry never allocates/free text.
static Card cards[5];
static int previousMask = -1;

bool active() { return selected; }
void toggle() { selected = !selected; }

static lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, uint32_t color) {
    auto* obj = lv_label_create(parent);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    return obj;
}
template<size_t N> static void text(lv_obj_t* obj, char (&storage)[N], const char* value) {
    if (strcmp(storage, value) == 0) return;
    snprintf(storage, N, "%s", value);
    lv_label_set_text_static(obj, storage);
}
void create(lv_obj_t* parent) {
    previousMask = -1;
    root = lv_obj_create(parent);
    lv_obj_set_size(root, g_screenW, g_screenH);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(Theme::DeepSea), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    auto* heading = label(root, &font_ttgo_plex_12, Theme::HUDDim);
    lv_label_set_text_static(heading, "USAGE / USED");
    lv_obj_align(heading, LV_ALIGN_TOP_LEFT, 6, 3);
    auto* hint = label(root, &font_ttgo_plex_12, Theme::HUDFaint);
    lv_label_set_text_static(hint, "MODE   /   ROTATE");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -3);
    empty = label(root, &font_ttgo_plex_12, Theme::HUDDim);
    lv_label_set_text_static(empty, "No usage data\nWaiting for limits");
    lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(empty);
    for (auto& c : cards) {
        c.titleText[0] = c.valueText[0] = c.resetText[0] = '\0';
        c.panel = lv_obj_create(root);
        lv_obj_set_style_bg_color(c.panel, lv_color_hex(Theme::MidWater), 0);
        lv_obj_set_style_bg_opa(c.panel, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(c.panel, 0, 0);
        lv_obj_set_style_radius(c.panel, 4, 0);
        lv_obj_set_style_pad_all(c.panel, 0, 0);
        lv_obj_clear_flag(c.panel, LV_OBJ_FLAG_SCROLLABLE);
        c.title = label(c.panel, &font_ttgo_plex_12, Theme::HUDText);
        c.value = label(c.panel, &font_ttgo_plex_28, Theme::HUDText);
        c.reset = label(c.panel, &font_ttgo_plex_12, Theme::HUDDim);
        c.bar = lv_bar_create(c.panel);
        lv_bar_set_range(c.bar, 0, 100);
        lv_obj_set_style_bg_color(c.bar, lv_color_hex(Theme::ShallowWater), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(c.bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(c.bar, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_add_flag(c.panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (!selected) lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
}
void update() {
    if (!root) return;
    if (!selected) { lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
    // Bounded snapshot; never retain the large dashboard on stack. z.ai shows
    // the 5h credits window only (#348) — the MCP quota is secondary at glance
    // distance on this 135px screen.
    float values[5]; char resets[5][20];
    lockState();
    values[0] = g_state.usageStale ? -1 : g_state.fiveHourPercent;
    values[1] = g_state.usageStale ? -1 : g_state.sevenDayPercent;
    values[2] = g_state.codexPrimaryPercent;
    values[3] = g_state.codexSecondaryPercent;
    values[4] = g_state.zaiPrimaryPercent;
    memcpy(resets[0], g_state.fiveHourReset, 20);
    memcpy(resets[1], g_state.sevenDayReset, 20);
    memcpy(resets[2], g_state.codexPrimaryReset, 20);
    memcpy(resets[3], g_state.codexSecondaryReset, 20);
    memcpy(resets[4], g_state.zaiPrimaryReset, 20);
    unlockState();
    const char* names[5] = {"CLAUDE 5H", "CLAUDE 7D", "CODEX 5H", "CODEX 7D", "Z.AI 5H"};
    int mask = 0, count = 0;
    for (int i = 0; i < 5; ++i) if (hasWindow(values[i])) { mask |= 1 << i; ++count; }
    if (count) lv_obj_add_flag(empty, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(empty, LV_OBJ_FLAG_HIDDEN);
    int slot = 0;
    for (int i = 0; i < 5; ++i) {
        if (!(mask & (1 << i))) continue;
        auto& c = cards[slot];
        if (mask != previousMask) {
            const auto r = cardRect(g_screenW, g_screenH, count, slot);
            lv_obj_set_pos(c.panel, r.x, r.y); lv_obj_set_size(c.panel, r.w, r.h);
            const bool compact = r.h < 66;
            lv_obj_set_style_text_font(c.value, compact ? &font_ttgo_plex_12 : &font_ttgo_plex_28, 0);
            lv_obj_align(c.title, LV_ALIGN_TOP_LEFT, 5, 3);
            lv_obj_align(c.value, compact ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_CENTER, compact ? -5 : 0, compact ? 3 : -2);
            lv_obj_set_width(c.reset, r.w - 10);
            lv_obj_align(c.reset, LV_ALIGN_BOTTOM_LEFT, 5, -10);
            lv_obj_set_size(c.bar, r.w - 10, 3);
            lv_obj_align(c.bar, LV_ALIGN_BOTTOM_MID, 0, -4);
            lv_obj_clear_flag(c.panel, LV_OBJ_FLAG_HIDDEN);
        }
        char buf[28]; text(c.title, c.titleText, names[i]);
        snprintf(buf, sizeof(buf), "%d%%", boundedPercent(values[i])); text(c.value, c.valueText, buf);
        resets[i][19] = '\0';
        snprintf(buf, sizeof(buf), "Reset %s", resets[i][0] ? resets[i] : "--"); text(c.reset, c.resetText, buf);
        lv_obj_set_style_bg_color(c.bar, lv_color_hex(values[i] >= 90 ? Theme::StatusAmber :
            (i < 2 ? Theme::ClaudeBody : i < 4 ? Theme::CloudBody : Theme::ZaiBlue)), LV_PART_INDICATOR);
        lv_bar_set_value(c.bar, boundedPercent(values[i]), LV_ANIM_OFF);
        ++slot;
    }
    for (; slot < 5; ++slot) lv_obj_add_flag(cards[slot].panel, LV_OBJ_FLAG_HIDDEN);
    previousMask = mask;
}
} }
#endif
