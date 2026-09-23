#pragma once
#include <lvgl.h>
namespace IPS10Workspace {
// All widgets and bounded text stores live for the screen lifetime.
lv_obj_t* init(lv_obj_t* parent, const lv_image_dsc_t* (*glyph)(const char*));
void update();
struct Diagnostics {
    uint32_t updates, lastUpdateUs, maxUpdateUs;
    uint16_t width, height;
    uint8_t sessions, visibleSessions, filter, eventCount;
    bool connected, history, voiceOpen;
};
Diagnostics diagnostics();
const char* selectedSession();
}
