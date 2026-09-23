#include "config.h"
#if defined(BOARD_IPS10)
#include "ips10_workspace.h"
#include "ips10_ocean_generated.h"
#include "ips10_relief_claude_generated.h"
#include "ips10_relief_codex_generated.h"
#include "ips10_relief_openclaw_generated.h"
#include "../assets/logo.h"
#include "audio/mic_capture.h"
#include "../theme.h"
#include "../display.h"
#include "../../state/agent_state.h"
#include "../../util/utf8.h"
#include "../../util/usage_format.h"
#include <Arduino.h>
#include "net/serial_client.h"
#include <cstdio>
#include <cstring>

LV_FONT_DECLARE(font_workspace_20);
LV_FONT_DECLARE(font_workspace_36);
LV_FONT_DECLARE(font_studio_28);
LV_FONT_DECLARE(font_studio_20);
LV_FONT_DECLARE(font_studio_16);

namespace IPS10Workspace {
namespace {
// IPS10 layout rhythm: outer 24, inter-panel 16, content inset 16.
namespace Grid { constexpr int Outer=24, Gap=16, Inset=16, Header=96, AgentRow=120; }
// Fixed stores avoid label heap churn while live state changes. LVGL owns the
// widgets; these stores are reused across orientation rebuilds, never allocated
// in update(). Only the selected session's eight newest events are copied.
template<size_t N> struct Text {
    lv_obj_t* obj = nullptr;
    char value[N] = {};
    void set(const char* s) {
        char normalized[N];
        snprintf(normalized, N, "%s", s?s:"");
        Utf8::utf8TrimEnd(normalized);
        Utf8::sanitizeLvglText(normalized);
        if (!strcmp(value, normalized)) return;
        snprintf(value,N,"%s",normalized);
        lv_label_set_text_static(obj, value);
    }
};
struct Row {
    char id[32], name[40], agent[16], state[20], tool[40], project[40];
    char activity[160], latest[120], hm[6];
    uint16_t children, latestRank; bool childrenKnown;
    uint32_t elapsed;
};
static Row rows[10];
static SessionInfo selected;
static TimelineEntry events[8];
static int count, eventCount, filter;
static char selectedId[32] = "";
static char presentedId[32] = "";
static bool history, voiceOpen, connected, overviewMode=true;
static uint32_t lastUpdate;
static lv_obj_t *root, *rail, *detail, *voicePane, *filters[4], *cards[10], *marks[10], *eventBox, *activityScroll;
static lv_obj_t *stateNodes[3], *eventCards[8], *historyButton, *focusGlyph;
static Text<12> censusValue[3];
static Text<80> link, summary, rosterTitle, heading, identity, stateText, usage, census, empty;
static Text<200> activity, instruction;
static Text<56> rowTitle[10], rowState[10];
static Text<96> rowTool[10];
static Text<240> eventText[8];
static Text<64> eventMeta[8];
static Text<32> filterText[4], filterNumber[4], historyLabel;
static const lv_image_dsc_t* (*glyphFor)(const char*);
static int detailW, sceneW;
// Bounded, device-lifetime widget pools: ten projects / ten creatures total.
// No canvases or per-frame heap allocations are needed for the living overview.
static lv_obj_t *overview, *viewButton, *resourcePane, *quotaCards[6], *quotaBars[6], *pods[10], *seats[10], *creatures[10];
static Text<40> viewLabel, quotaValue[6], quotaReset[6], podName[10], seatState[10];
static Text<160> tokenLine;
static Text<100> podStatus[10], overviewEmpty;
static Text<200> podLatest[10], ambientVoice;
static Text<120> attentionLine;
static Text<128> pageLabel;
static Text<160> agentActivity[10];
static Text<40> cohortLabel[10], childLabel[10];
static bool gatewayReady=false, retainedDetail=false;
static lv_obj_t* recentCaption;
static Text<32> quotaWindow[6];
static lv_obj_t* providerNames[3];
static lv_obj_t* voiceButton;
// One fixed subscription chip; raw Antigravity credits never enter the view.
static lv_obj_t* planChip;
static Text<48> planLabel;
static int displayedSlot[10];
static lv_obj_t *voiceStatusPane, *attentionPane;
static int page=0, pageCount=1;
static uint32_t pageSince=0;
static bool pageHeld=false;
static char projectKeys[10][40];
struct UsageSnapshot { float percent[6]; char reset[6][20]; char plan[48]; bool mcp; uint32_t input, output, calls; float cost; };
static UsageSnapshot quota;
static int podFor[10], memberSlot[10], podMembers[10], projectCount;
static uint32_t brandColor(const char* agent) {
    if(!strcmp(agent,"claude-code") || !strcmp(agent,"claude")) return Theme::ClaudeBody;
    if(!strcmp(agent,"codex") || !strcmp(agent,"codex-cli") || !strcmp(agent,"codex-app")) return Theme::CloudBody;
    if(!strcmp(agent,"openclaw")) return Theme::CrayfishShell;
    if(!strncmp(agent,"kiro",4)) return Theme::KiroMark;
    if(!strcmp(agent,"antigravity")) return Theme::AntigravityMark;
    return Theme::OpenCodeOuter;
}
// Three flash-resident 112px reliefs (110.25 KiB total), shared by all seats.
// Unsupported agents retain their canonical glyph; unknown agents remain hidden.
static const lv_image_dsc_t* reliefFor(const char* agent) {
    if(!strcmp(agent,"claude-code") || !strcmp(agent,"claude"))return &IPS10Relief_claude::image;
    if(!strcmp(agent,"codex") || !strcmp(agent,"codex-cli") || !strcmp(agent,"codex-app"))return &IPS10Relief_codex::image;
    if(!strcmp(agent,"openclaw"))return &IPS10Relief_openclaw::image;
    return nullptr;
}
static portMUX_TYPE diagMux=portMUX_INITIALIZER_UNLOCKED;
static Diagnostics diag{};

static lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color) {
    auto* o = lv_obj_create(parent);
    lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);
    lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(o,0,0);
    lv_obj_set_style_radius(o,16,0); lv_obj_set_style_pad_all(o,0,0);
    lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
template<size_t N> static void label(Text<N>& t, lv_obj_t* p, int x,int y,int w,const lv_font_t* font,uint32_t color) {
    t.value[0]=0; t.obj=lv_label_create(p);
    lv_obj_set_pos(t.obj,x,y); lv_obj_set_width(t.obj,w);
    lv_obj_set_style_text_font(t.obj,font,0);
    lv_obj_set_style_text_color(t.obj,lv_color_hex(color),0);
    lv_label_set_long_mode(t.obj,LV_LABEL_LONG_DOT);
    lv_label_set_text_static(t.obj,t.value);
}
static void caption(lv_obj_t* p,const char* s,int x,int y,int w,uint32_t color=Theme::HUDDim) {
    auto* l=lv_label_create(p); lv_label_set_text_static(l,s);
    lv_obj_set_pos(l,x,y);lv_obj_set_width(l,w);
    lv_obj_set_style_text_font(l,&font_studio_20,0);
    lv_obj_set_style_text_color(l,lv_color_hex(color),0);
}
static void visible(lv_obj_t* o,bool v) {
    if(v) lv_obj_clear_flag(o,LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);
}
static int category(const char* s) {
    if(!strncmp(s,"awaiting",8) || !strcmp(s,"error")) return 1;
    if(!strcmp(s,"processing")) return 2;
    return 3;
}
static uint32_t colorFor(int c) { return c==1?Theme::StatusAmber:c==2?Theme::StatusBlue:Theme::HUDDim; }
static const char* nameFor(int c) { return c==1?"Attention":c==2?"Working":"Idle"; }
static bool matches(const Row& r) { return !filter || category(r.state)==filter; }
static void selectCb(lv_event_t* e) {
    const int i=static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if(i<0 || i>=count) return;
    snprintf(selectedId,sizeof(selectedId),"%s",rows[i].id);
    overviewMode=false; retainedDetail=false; history=false; lastUpdate=0; update();
}
static void filterCb(lv_event_t* e) {
    filter=static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    page=0;pageSince=millis();
    lastUpdate=0; update();
    lv_obj_scroll_to_y(rail,0,LV_ANIM_OFF);
}
static void historyCb(lv_event_t*) { history=!history;lastUpdate=0;update();lv_obj_scroll_to_y(eventBox,0,LV_ANIM_OFF); }
static void viewCb(lv_event_t*) { overviewMode=!overviewMode;if(overviewMode)filter=0;lastUpdate=0;update(); }
static void pageCb(lv_event_t*) { pageHeld=!pageHeld;lastUpdate=0;update(); }
static void voiceCb(lv_event_t*) { voiceOpen=!voiceOpen;visible(voicePane,voiceOpen); }
}

lv_obj_t* init(lv_obj_t* parent,const lv_image_dsc_t* (*glyph)(const char*)) {
    presentedId[0]=0;
    glyphFor=glyph; count=0; filter=0; history=false; voiceOpen=false;lastUpdate=0;
    overviewMode=true;page=0;pageSince=millis();pageHeld=false;
    const int w=g_screenW,h=g_screenH, railW=w>=1100?320:248;
    root=box(parent,0,0,w,h,Theme::DeepSea);lv_obj_set_style_radius(root,0,0);
    auto* ocean=lv_image_create(root);lv_image_set_src(ocean,&IPS10Ocean::image);
    const int oceanScale=w*400>=h*640?w*256/640:h*256/400;
    lv_image_set_pivot(ocean,0,0);lv_image_set_scale(ocean,oceanScale);lv_obj_set_x(ocean,-(640*oceanScale/256-w)/2);
    lv_obj_set_style_image_opa(ocean,LV_OPA_30,0);lv_obj_clear_flag(ocean,LV_OBJ_FLAG_CLICKABLE);
    auto* logo=lv_image_create(root);lv_image_set_src(logo,&img_logo_48);lv_obj_set_pos(logo,24,16);lv_obj_set_style_image_recolor_opa(logo,LV_OPA_COVER,0);lv_obj_set_style_image_recolor(logo,lv_color_hex(Theme::StatusCyan),0);
    caption(root,"AgentDeck",84,23,240,Theme::HUDText);
    auto* wordmark=lv_obj_get_child(root,-1);lv_obj_set_style_text_font(wordmark,&font_studio_28,0);
    viewButton=box(root,w-152,16,128,44,Theme::ShallowWater);
    label(viewLabel,viewButton,16,10,100,&font_studio_20,Theme::HUDText);
    lv_obj_add_event_cb(viewButton,viewCb,LV_EVENT_CLICKED,nullptr);
    label(link,root,330,26,350,&font_studio_20,Theme::HUDDim);
    lv_obj_set_style_text_align(link.obj,LV_TEXT_ALIGN_LEFT,0);
    if(w<1100)visible(link.obj,false);
    label(summary,root,24,76,w-248,&font_studio_20,Theme::HUDText);
    const int gap=12, fw=(w-48-gap*3)/4;
    for(int i=0;i<4;++i) {
        filters[i]=box(root,24+i*(fw+gap),118,fw,64,Theme::MidWater);
        lv_obj_add_flag(filters[i],LV_OBJ_FLAG_CLICKABLE);
        label(filterText[i],filters[i],16,24,fw-84,&font_studio_20,Theme::HUDDim);
        label(filterNumber[i],filters[i],fw-66,12,56,&font_workspace_36,Theme::HUDText);
        lv_obj_add_event_cb(filters[i],filterCb,LV_EVENT_CLICKED,reinterpret_cast<void*>(static_cast<intptr_t>(i)));
    }
    label(rosterTitle,root,24,205,railW,&font_studio_20,Theme::HUDDim);
    rail=box(root,24,238,railW,h-358,Theme::DeepSea);
    lv_obj_add_flag(rail,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(rail,LV_DIR_VER);
    lv_obj_set_style_pad_bottom(rail,8,0);
    for(int i=0;i<10;++i) {
        cards[i]=box(rail,0,i*110,railW,100,Theme::MidWater);
        lv_obj_set_style_border_width(cards[i],2,0);
        marks[i]=lv_image_create(cards[i]);lv_obj_set_pos(marks[i],0,4);
        lv_obj_set_style_image_recolor_opa(marks[i],LV_OPA_COVER,0);
        label(rowTitle[i],cards[i],64,16,railW-80,&font_studio_20,Theme::HUDText);
        label(rowState[i],cards[i],64,44,railW-80,&font_studio_16,Theme::HUDDim);
        label(rowTool[i],cards[i],16,72,railW-32,&font_studio_16,Theme::HUDDim);
        lv_obj_set_height(rowTool[i].obj,18);
        lv_obj_add_event_cb(cards[i],selectCb,LV_EVENT_CLICKED,reinterpret_cast<void*>(static_cast<intptr_t>(i)));
    }
    detailW=w-railW-72;
    detail=box(root,48+railW,204,detailW,h-324,Theme::MidWater);
    label(heading,detail,24,16,detailW-124,&font_studio_20,Theme::HUDText);
    label(identity,detail,24,54,detailW-124,&font_studio_16,Theme::HUDDim);
    focusGlyph=lv_image_create(detail);lv_obj_set_pos(focusGlyph,detailW-88,8);
    lv_obj_set_style_image_recolor_opa(focusGlyph,LV_OPA_COVER,0);
    // These are reported collaboration counts, never inferred progress. The
    // raised tiles provide a glanceable map of active/completed/background work.
    const int nw=(detailW-64)/3;
    for(int i=0;i<3;++i) {
        stateNodes[i]=box(detail,24+i*(nw+8),90,nw,58,Theme::DeepSea);
        caption(stateNodes[i],i==0?"Subagents":i==1?"Completed":"Background",12,8,nw-20);
        label(censusValue[i],stateNodes[i],12,30,nw-20,&font_studio_20,Theme::HUDText);
    }
    label(stateText,detail,24,164,detailW-48,&font_studio_20,Theme::HUDText);
    activityScroll=box(detail,24,192,detailW-48,54,Theme::MidWater);
    lv_obj_add_flag(activityScroll,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(activityScroll,LV_DIR_VER);
    label(activity,activityScroll,0,0,detailW-56,&font_workspace_20,Theme::HUDText);
    lv_label_set_long_mode(activity.obj,LV_LABEL_LONG_WRAP);
    label(census,detail,24,252,detailW-48,&font_studio_16,Theme::HUDDim);
    label(instruction,detail,24,276,detailW-48,&font_workspace_20,Theme::HUDDim);
    lv_obj_set_height(instruction.obj,44);
    historyButton=box(detail,detailW-166,324,142,48,Theme::ShallowWater);
    label(historyLabel,historyButton,14,16,120,&font_studio_20,Theme::HUDText);
    lv_obj_add_event_cb(historyButton,historyCb,LV_EVENT_CLICKED,nullptr);
    caption(detail,"Recent activity",24,340,detailW-212);recentCaption=lv_obj_get_child(detail,-1);
    eventBox=box(detail,24,386,detailW-48,h-324-402,Theme::MidWater);
    lv_obj_add_flag(eventBox,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(eventBox,LV_DIR_VER);
    for(int i=0;i<8;++i) {
        eventCards[i]=box(eventBox,0,i*112,detailW-48,104,Theme::DeepSea);
        label(eventMeta[i],eventCards[i],14,12,detailW-76,&font_studio_16,Theme::StatusCyan);
        label(eventText[i],eventCards[i],14,36,detailW-76,&font_workspace_20,Theme::HUDText);
        lv_label_set_long_mode(eventText[i].obj,LV_LABEL_LONG_WRAP);
        lv_obj_set_height(eventText[i].obj,62);
    }
    sceneW=w-324;
    overview=box(root,24,96,sceneW,h-174,Theme::DeepSea);lv_obj_set_style_bg_opa(overview,LV_OPA_TRANSP,0);
    resourcePane=box(root,w-276,96,252,h-176,Theme::DeepSea);lv_obj_set_style_bg_opa(resourcePane,LV_OPA_70,0);
    caption(resourcePane,"Usage quota",16,16,220,Theme::HUDText);
    const char* providers[]={"Claude","Codex","z.ai"};
    for(int i=0;i<3;++i){caption(resourcePane,providers[i],16,48,220,Theme::HUDText);providerNames[i]=lv_obj_get_child(resourcePane,-1);lv_obj_set_style_text_align(providerNames[i],LV_TEXT_ALIGN_LEFT,0);}
    for(int i=0;i<6;++i) {
        quotaCards[i]=box(resourcePane,8,72,116,150,Theme::DeepSea);lv_obj_set_style_bg_opa(quotaCards[i],LV_OPA_TRANSP,0);
        quotaBars[i]=lv_arc_create(quotaCards[i]);lv_obj_set_pos(quotaBars[i],7,0);lv_obj_set_size(quotaBars[i],100,100);
        lv_arc_set_rotation(quotaBars[i],270);lv_arc_set_bg_angles(quotaBars[i],0,360);lv_arc_set_range(quotaBars[i],0,100);
        lv_obj_remove_style(quotaBars[i],nullptr,LV_PART_KNOB);lv_obj_clear_flag(quotaBars[i],LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_width(quotaBars[i],8,LV_PART_MAIN);lv_obj_set_style_arc_width(quotaBars[i],8,LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(quotaBars[i],lv_color_hex(Theme::ShallowWater),LV_PART_MAIN);
        label(quotaValue[i],quotaCards[i],4,33,108,&font_studio_28,Theme::HUDText);lv_obj_set_style_text_align(quotaValue[i].obj,LV_TEXT_ALIGN_CENTER,0);
        label(quotaWindow[i],quotaCards[i],4,103,108,&font_studio_16,Theme::HUDDim);lv_obj_set_style_text_align(quotaWindow[i].obj,LV_TEXT_ALIGN_CENTER,0);
        label(quotaReset[i],quotaCards[i],4,124,108,&font_studio_16,Theme::HUDDim);lv_obj_set_style_text_align(quotaReset[i].obj,LV_TEXT_ALIGN_CENTER,0);
    }
    planChip=box(root,w>=1100?500:350,18,160,40,Theme::ShallowWater);
    label(planLabel,planChip,12,8,136,&font_studio_20,Theme::HUDDim);
    lv_label_set_long_mode(planLabel.obj,LV_LABEL_LONG_DOT);lv_obj_set_height(planLabel.obj,font_studio_20.line_height);
    label(tokenLine,root,24,h-64,w-260,&font_workspace_20,Theme::HUDText);
    label(pageLabel,overview,0,0,sceneW,&font_studio_16,Theme::HUDDim);
    lv_obj_add_flag(pageLabel.obj,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(pageLabel.obj,pageCb,LV_EVENT_CLICKED,nullptr);
    for(int i=0;i<10;++i) {
        pods[i]=box(overview,0,24,sceneW,560,Theme::MidWater);lv_obj_set_style_bg_opa(pods[i],LV_OPA_80,0);
        lv_obj_set_style_border_width(pods[i],0,0);lv_obj_set_style_border_side(pods[i],LV_BORDER_SIDE_RIGHT,0);lv_obj_set_style_border_color(pods[i],lv_color_hex(Theme::ShallowWater),0);
        label(podName[i],pods[i],16,16,sceneW-32,&font_studio_20,Theme::HUDText);
        label(podStatus[i],pods[i],16,44,sceneW-32,&font_studio_16,Theme::HUDDim);
        label(cohortLabel[i],pods[i],16,72,sceneW-32,&font_studio_16,Theme::HUDDim);
        label(podLatest[i],pods[i],16,480,sceneW-32,&font_workspace_20,Theme::HUDDim);
        lv_label_set_long_mode(podLatest[i].obj,LV_LABEL_LONG_WRAP);lv_obj_set_height(podLatest[i].obj,72);
        seats[i]=box(overview,0,0,100,126,Theme::DeepSea);lv_obj_set_style_bg_opa(seats[i],LV_OPA_TRANSP,0);
        lv_obj_add_event_cb(seats[i],selectCb,LV_EVENT_CLICKED,reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        creatures[i]=lv_image_create(seats[i]);lv_image_set_pivot(creatures[i],0,0);lv_obj_clear_flag(creatures[i],LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_image_recolor_opa(creatures[i],LV_OPA_COVER,0);
        label(seatState[i],seats[i],0,104,100,&font_studio_16,Theme::HUDText);
        label(childLabel[i],seats[i],20,124,90,&font_studio_16,Theme::StatusCyan);
        label(agentActivity[i],overview,0,0,300,&font_workspace_20,Theme::HUDText);
        lv_label_set_long_mode(agentActivity[i].obj,LV_LABEL_LONG_WRAP);lv_obj_set_height(agentActivity[i].obj,68);
    }
    label(overviewEmpty,overview,16,180,sceneW-32,&font_studio_28,Theme::HUDDim);
    attentionPane=box(root,24,76,w-48,48,Theme::DeepSea);lv_obj_set_style_border_width(attentionPane,1,0);lv_obj_set_style_border_color(attentionPane,lv_color_hex(Theme::StatusAmber),0);
    label(attentionLine,attentionPane,14,12,w-76,&font_studio_20,Theme::StatusAmber);
    voiceStatusPane=box(root,w-522,15,350,48,Theme::DeepSea);lv_obj_set_style_bg_opa(voiceStatusPane,LV_OPA_TRANSP,0);
    label(ambientVoice,voiceStatusPane,8,11,334,&font_workspace_20,Theme::StatusCyan);
    label(empty,rail,12,12,railW-24,&font_studio_20,Theme::HUDDim);
    lv_label_set_long_mode(empty.obj,LV_LABEL_LONG_WRAP);
    label(usage,root,24,h-32,w-48,&font_studio_16,Theme::HUDDim);
    auto* voice=box(root,w-200,h-58,176,44,Theme::ShallowWater);voiceButton=voice;
    caption(voice,"Voice / speaker",14,10,156,Theme::HUDText);
    lv_obj_add_event_cb(voice,voiceCb,LV_EVENT_CLICKED,nullptr);
    // A separate drawer preserves direct voice controls without consuming the
    // working surface while closed. Width is always the full screen minus 48.
    voicePane=box(root,w-584,h-302,560,178,Theme::ShallowWater);
    lv_obj_set_style_pad_all(voicePane,10,0);lv_obj_set_style_pad_row(voicePane,0,0);
    lv_obj_set_flex_flow(voicePane,LV_FLEX_FLOW_COLUMN);
    visible(voicePane,false);
    update();return voicePane;
}

void update() {
    if(!root) return;
    const uint32_t now=millis();
    if(lastUpdate && now-lastUpdate<250) return;
    lastUpdate=now?now:1;
    const uint32_t started=micros();
    int totals[4]={}; float p5,p7,c5,c7; bool stale; uint16_t rosterTotal;bool rosterRotating;
    lockState();
    connected=g_state.wsConnected || Net::serialConnected();
    gatewayReady=connected && g_state.gatewayConnected && !g_state.gatewayHasError;
    count=0;
    for(int i=0;i<g_state.sessionCount && i<10;++i) {
        const auto& s=g_state.sessions[i]; if(!s.alive || !s.id[0]) continue;
        auto& r=rows[count++];
        snprintf(r.id,sizeof(r.id),"%s",s.id);snprintf(r.name,sizeof(r.name),"%s",s.lastEventTask[0]?s.lastEventTask:s.projectName[0]?s.projectName:s.agentType);
        snprintf(r.project,sizeof(r.project),"%s",s.projectName);
        snprintf(r.agent,sizeof(r.agent),"%s",s.agentType);snprintf(r.state,sizeof(r.state),"%s",s.state);
        snprintf(r.tool,sizeof(r.tool),"%s",s.currentTool[0]?s.currentTool:s.activity[0]?s.activity:s.lastEventText);r.elapsed=s.elapsedSec;
        snprintf(r.activity,sizeof(r.activity),"%s",category(s.state)==1 && s.question[0]?s.question:s.activity[0]?s.activity:s.lastEventTask[0]?s.lastEventTask:s.currentTool);
        snprintf(r.latest,sizeof(r.latest),"%s",s.lastEventText);snprintf(r.hm,sizeof(r.hm),"%s",s.lastEventHm);
        r.latestRank=TIMELINE_MAX_ENTRIES;
        // Ring order, not HH:MM string order, remains correct across midnight.
        for(int e=0;e<g_state.timelineCount;++e) {
            const auto& item=g_state.timeline[(g_state.timelineHead+g_state.timelineCount-1-e+TIMELINE_MAX_ENTRIES)%TIMELINE_MAX_ENTRIES];
            if(strcmp(item.sessionId,s.id))continue;
            r.latestRank=e;
            snprintf(r.latest,sizeof(r.latest),"%s",item.detail[0]?item.detail:item.raw);
            snprintf(r.hm,sizeof(r.hm),"%s",item.hm);break;
        }
        r.children=s.childrenActive;r.childrenKnown=s.childrenKnown;
        ++totals[category(r.state)];
    }
    // Priority ordering is deterministic; selection still follows the ID.
    for(int i=1;i<count;++i) {
        static Row r; r=rows[i];int j=i; // Reused bounded scratch; keep the UI stack small.
        while(j>0 && category(rows[j-1].state)>category(r.state)) {rows[j]=rows[j-1];--j;}
        rows[j]=r;
    }
    totals[0]=count;rosterTotal=g_state.sessionsTotal;rosterRotating=g_state.sessionsRotating;
    int chosen=-1;
    for(int i=0;i<count;++i) if(matches(rows[i]) && !strcmp(rows[i].id,selectedId)) chosen=i;
    retainedDetail=chosen<0 && !overviewMode && selectedId[0] && !strcmp(selected.id,selectedId) && rosterTotal>count;
    if(chosen<0 && !retainedDetail) {
        for(int i=0;i<count;++i) if(matches(rows[i]) && (chosen<0 || category(rows[i].state)<category(rows[chosen].state))) chosen=i;
        snprintf(selectedId,sizeof(selectedId),"%s",chosen>=0?rows[chosen].id:"");history=false;
    }
    if(!retainedDetail)selected={};eventCount=0;
    for(int i=0;i<g_state.sessionCount && i<10;++i) if(selectedId[0] && !strcmp(g_state.sessions[i].id,selectedId)) selected=g_state.sessions[i];
    // No global/unattributed events in a session detail, even when project names
    // happen to match. A stable session ID is the sole join key.
    for(int i=0;i<g_state.timelineCount && eventCount<8 && selectedId[0];++i) {
        const int idx=(g_state.timelineHead+g_state.timelineCount-1-i+TIMELINE_MAX_ENTRIES)%TIMELINE_MAX_ENTRIES;
        const auto& e=g_state.timeline[idx];
        if(!strcmp(e.sessionId,selectedId)) events[eventCount++]=e;
    }
    p5=g_state.fiveHourPercent;p7=g_state.sevenDayPercent;c5=g_state.codexPrimaryPercent;c7=g_state.codexSecondaryPercent;stale=g_state.usageStale;
    quota.percent[0]=stale?-1:p5;quota.percent[1]=stale?-1:p7;quota.percent[2]=c5;quota.percent[3]=c7;
    quota.percent[4]=g_state.zaiPrimaryPercent;quota.percent[5]=g_state.zaiSecondaryPercent;quota.mcp=g_state.zaiSecondaryIsMcp;
    const char* resets[]={g_state.fiveHourReset,g_state.sevenDayReset,g_state.codexPrimaryReset,g_state.codexSecondaryReset,g_state.zaiPrimaryReset,g_state.zaiSecondaryReset};
    for(int i=0;i<6;++i) snprintf(quota.reset[i],20,"%s",resets[i]);
    snprintf(quota.plan,sizeof(quota.plan),"%s",g_state.antigravityPlan);
    quota.input=g_state.inputTokens;quota.output=g_state.outputTokens;quota.calls=g_state.toolCalls;quota.cost=g_state.estimatedCostUsd;
    unlockState();
    char text[240];
    const bool hasQuota=quota.percent[0]>=0 || quota.percent[1]>=0 || quota.percent[2]>=0 || quota.percent[3]>=0 || quota.percent[4]>=0 || quota.percent[5]>=0;
    sceneW=g_screenW-(hasQuota?316:48);
    visible(overview,overviewMode);visible(resourcePane,hasQuota);visible(rail,!overviewMode);visible(rosterTitle.obj,!overviewMode);
    const bool needsAttention=totals[1]>0 || !connected;
    visible(attentionPane,overviewMode && needsAttention);visible(summary.obj,!overviewMode);
    lv_obj_set_pos(overview,24,needsAttention?138:90);lv_obj_set_size(overview,sceneW,g_screenH-(needsAttention?216:168)-(g_screenW<1100?54:0));
    for(int i=0;i<4;++i)visible(filters[i],!overviewMode);
    viewLabel.set(overviewMode?"Details":"Aquarium");
    const char* vs=Audio::voiceState();
    const char* voiceText=!Audio::micReady()?"":!strcmp(vs,"listening")?"Listening...":!strcmp(vs,"sending")?"Sending voice...":!strcmp(vs,"waiting")?"Waiting for reply...":!strcmp(vs,"speaking")?"Speaking...":!strcmp(vs,"error")?"Voice connection error":!strcmp(vs,"muted")?"":gatewayReady?"OpenClaw ready":connected?"Wake ready · Agent offline":"Wake ready · Disconnected";
    ambientVoice.set(voiceText);visible(voiceStatusPane,voiceText[0]);
    if(g_screenW<1100){lv_obj_set_pos(voiceStatusPane,24,g_screenH-112);lv_obj_set_width(voiceStatusPane,g_screenW-48);}
    visible(voiceButton,Audio::micReady());
    lv_obj_set_y(resourcePane,overviewMode?(needsAttention?178:130):204);
    // Six reused rings, with fixed geometry regardless of how many providers exist.
    // Window labels explain the measure; reset text is secondary and optional.
    constexpr int gaugeSize=80;
    int quotaY=48;
    for(int provider=0;provider<3;++provider) {
        const int begin=provider*2,end=begin+2;
        int knownCount=0;for(int i=begin;i<end;++i)knownCount+=quota.percent[i]>=0;
        visible(providerNames[provider],knownCount>0);
        if(knownCount)lv_obj_set_y(providerNames[provider],quotaY);
        bool hasReset=false;
        int col=0;
        for(int i=begin;i<end;++i) {
            const float p=quota.percent[i];const bool known=p>=0;visible(quotaCards[i],known);if(!known)continue;
            lv_obj_set_pos(quotaCards[i],knownCount==1?68:8+col++*120,quotaY+30);
            lv_obj_set_size(quotaCards[i],116,136);
            lv_obj_set_size(quotaBars[i],gaugeSize,gaugeSize);lv_obj_set_x(quotaBars[i],(116-gaugeSize)/2);
            lv_obj_set_style_arc_width(quotaBars[i],6,LV_PART_MAIN);lv_obj_set_style_arc_width(quotaBars[i],6,LV_PART_INDICATOR);
            lv_obj_set_y(quotaValue[i].obj,23);
            lv_obj_set_y(quotaWindow[i].obj,84);lv_obj_set_y(quotaReset[i].obj,104);
            snprintf(text,sizeof(text),"%.0f%%",p);quotaValue[i].set(text);
            lv_arc_set_value(quotaBars[i],p>100?100:static_cast<int>(p));
            // Same meaning, same color across providers and both time windows.
            lv_obj_set_style_arc_color(quotaBars[i],lv_color_hex(p>=90?Theme::StatusAmber:Theme::StatusCyan),LV_PART_INDICATOR);
            quotaWindow[i].set(i==5 && quota.mcp?"MCP used":i%2?"7d used":"5h used");
            snprintf(text,sizeof(text),"Reset %s",quota.reset[i]);quotaReset[i].set(text);
            visible(quotaReset[i].obj,quota.reset[i][0]);hasReset|=quota.reset[i][0]!=0;
        }
        if(knownCount)quotaY+=hasReset?164:148;
    }
    // Content-sized rail avoids a tall empty panel when only one window is known.
    lv_obj_set_height(resourcePane,quotaY+4);
    const char* plan=quota.plan;while(*plan==' ')++plan;
    visible(planChip,*plan);
    if(*plan){UsageFormat::formatAgyPlan(plan,text,sizeof(text));planLabel.set(text);}
    // These are the daemon's usage snapshot, not selected-session or project totals.
    if(quota.input || quota.output || quota.calls) snprintf(text,sizeof(text),"Tokens  IN %lu / OUT %lu   ·   Tools %lu",static_cast<unsigned long>(quota.input),static_cast<unsigned long>(quota.output),static_cast<unsigned long>(quota.calls));
    else snprintf(text,sizeof(text),"");
    tokenLine.set(text);visible(tokenLine.obj,quota.input || quota.output || quota.calls);
    projectCount=0;memset(podMembers,0,sizeof(podMembers));
    for(int i=0;i<count;++i) {
        podFor[i]=-1;if(!matches(rows[i]))continue;
        int group=-1;
        // Exact reported project identity only. Unnamed sessions remain separate.
        for(int j=0;j<i;++j) if(podFor[j]>=0 && rows[i].project[0] && !strcmp(rows[i].project,rows[j].project)) {group=podFor[j];break;}
        if(group<0) group=projectCount++;
        podFor[i]=group;memberSlot[i]=podMembers[group]++;
    }
    // Sort project spaces by identity, never by their changing activity state.
    int order[10], remap[10];
    for(int g=0;g<projectCount;++g) {
        order[g]=g;
        for(int i=0;i<count;++i)if(podFor[i]==g) {snprintf(projectKeys[g],40,"%s",rows[i].project[0]?rows[i].project:rows[i].id);break;}
    }
    for(int i=1;i<projectCount;++i){int key=order[i],j=i;while(j>0 && strcmp(projectKeys[order[j-1]],projectKeys[key])>0){order[j]=order[j-1];--j;}order[j]=key;}
    for(int i=0;i<projectCount;++i)remap[order[i]]=i;
    for(int i=0;i<count;++i)if(podFor[i]>=0)podFor[i]=remap[podFor[i]];
    memset(podMembers,0,sizeof(podMembers));
    for(int i=0;i<count;++i)if(podFor[i]>=0){++podMembers[podFor[i]];memberSlot[i]=0;for(int j=0;j<count;++j)if(podFor[j]==podFor[i] && strcmp(rows[j].id,rows[i].id)<0)++memberSlot[i];}
    const int capacity=g_screenW>=1100?3:1;
    pageCount=(projectCount+capacity-1)/capacity;if(pageCount<1)pageCount=1;
    if(page>=pageCount)page=0;
    if(overviewMode && !pageHeld && now-pageSince>=12000){page=(page+1)%pageCount;pageSince=now;}
    if(rosterTotal>count)snprintf(text,sizeof(text),"%d of %u agents · %s · Projects %d/%d · %s",count,rosterTotal,rosterRotating?"Roster rotates 60s":"Priority view",page+1,pageCount,pageHeld?"Paused":"12s");
    else snprintf(text,sizeof(text),"%d agents · %d projects%s",count,projectCount,pageCount>1?(pageHeld?" · Paused":" · Auto rotate 12s"):"");
    if(rosterTotal>count && pageCount==1)snprintf(text,sizeof(text),"%d of %u agents · %s",count,rosterTotal,rosterRotating?"Roster rotates 60s":"Priority view");
    pageLabel.set(text);visible(pageLabel.obj,count>0);
    const int columns=projectCount<capacity?(projectCount?projectCount:1):capacity;
    const int projectWidth=(sceneW-(columns-1)*Grid::Gap)/columns;
    int maxPeers=0;bool pageHasLatest=false;
    for(int g=page*capacity;g<projectCount && g<(page+1)*capacity;++g){const int peers=podMembers[g]<3?podMembers[g]:3;if(peers>maxPeers)maxPeers=peers;}
    for(int i=0;i<count;++i)if(podFor[i]>=page*capacity && podFor[i]<(page+1)*capacity && rows[i].latest[0])pageHasLatest=true;
    int projectX=0;
    const int peerRows=projectWidth>=600?1:maxPeers;
    const int ph=Grid::Header+peerRows*Grid::AgentRow+(pageHasLatest?88:16);
    for(int i=0;i<10;++i)displayedSlot[i]=-1;
    for(int g=0;g<10;++g) {
        const bool show=g<projectCount && g/capacity==page;visible(pods[g],show);if(!show)continue;
        const int pw=projectWidth;
        lv_obj_set_pos(pods[g],projectX,40);projectX+=pw+Grid::Gap;lv_obj_set_size(pods[g],pw,ph);
        int first=-1,working=0,attention=0;
        for(int i=0;i<count;++i)if(podFor[i]==g){if(first<0)first=i;working+=category(rows[i].state)==2;attention+=category(rows[i].state)==1;}
        lv_obj_set_width(podName[g].obj,pw-32);lv_label_set_long_mode(podName[g].obj,LV_LABEL_LONG_DOT);lv_obj_set_height(podName[g].obj,font_studio_20.line_height);lv_obj_set_width(podStatus[g].obj,pw-32);
        podName[g].set(rows[first].project[0]?rows[first].project:"Unnamed project");
        snprintf(text,sizeof(text),"%d %s · %d working",podMembers[g],podMembers[g]==1?"agent":"agents",working);podStatus[g].set(text);
        int members[10],nMembers=0;
        for(int i=0;i<count;++i)if(podFor[i]==g)members[nMembers++]=i;
        // Attention stays in view; reserve one slot for fair rotation of others.
        for(int i=1;i<nMembers;++i){int v=members[i],j=i;while(j>0 && (category(rows[members[j-1]].state)>category(rows[v].state) || (category(rows[members[j-1]].state)==category(rows[v].state) && strcmp(rows[members[j-1]].id,rows[v].id)>0))){members[j]=members[j-1];--j;}members[j]=v;}
        const int pinned=attention<2?attention:2;
        const int slots=nMembers<3?nMembers:3,rotating=slots-pinned,remaining=nMembers-pinned;
        for(int j=0;j<pinned;++j)displayedSlot[members[j]]=j;
        const int offset=remaining?((now/8000)*rotating)%remaining:0;
        for(int j=0;j<rotating;++j)displayedSlot[members[pinned+(offset+j)%remaining]]=pinned+j;
        snprintf(text,sizeof(text),"Showing %d of %d · Rotate 8s",slots,nMembers);cohortLabel[g].set(text);visible(cohortLabel[g].obj,nMembers>3);lv_obj_set_width(cohortLabel[g].obj,pw-44);
        int recent=first;for(int i=0;i<count;++i)if(podFor[i]==g && rows[i].latestRank<rows[recent].latestRank)recent=i;
        visible(podLatest[g].obj,rows[recent].latest[0]);
        lv_obj_set_pos(podLatest[g].obj,16,Grid::Header+peerRows*Grid::AgentRow+16);lv_obj_set_width(podLatest[g].obj,pw-32);
        snprintf(text,sizeof(text),"Latest %s\n%s",rows[recent].hm,rows[recent].latest);podLatest[g].set(text);
    }
    for(int i=0;i<10;++i) {
        const bool show=i<count && displayedSlot[i]>=0;visible(seats[i],show);visible(agentActivity[i].obj,show);if(!show)continue;
        if(lv_obj_get_parent(seats[i])!=pods[podFor[i]])lv_obj_set_parent(seats[i],pods[podFor[i]]);
        if(lv_obj_get_parent(agentActivity[i].obj)!=pods[podFor[i]])lv_obj_set_parent(agentActivity[i].obj,pods[podFor[i]]);
        const int pw=projectWidth; // LVGL may not have resolved this frame's resized bounds yet.
        const int cat=category(rows[i].state),slot=displayedSlot[i];
        const int peers=podMembers[podFor[i]]<3?podMembers[podFor[i]]:3;
        const bool across=pw>=600;
        const int cellW=across?(pw-32-(peers-1)*16)/peers:pw-32;
        const int rowX=16+(across?slot*(cellW+16):0),rowY=Grid::Header+(across?0:slot*Grid::AgentRow);
        // One aligned agent row: illustration left, state and work right.
        lv_obj_set_pos(seats[i],rowX,rowY);lv_obj_set_size(seats[i],cellW,112);
        const auto* relief=reliefFor(rows[i].agent);const auto* glyph=relief?relief:glyphFor?glyphFor(rows[i].agent):nullptr;visible(creatures[i],glyph);
        lv_obj_set_style_image_recolor_opa(creatures[i],relief?LV_OPA_TRANSP:LV_OPA_COVER,0);
        if(glyph){lv_image_set_src(creatures[i],glyph);lv_image_set_scale(creatures[i],relief?219:320);lv_obj_set_style_image_opa(creatures[i],cat==3?LV_OPA_60:LV_OPA_COVER,0);lv_obj_set_style_image_recolor(creatures[i],lv_color_hex(brandColor(rows[i].agent)),0);}
        lv_obj_set_pos(creatures[i],0,cat==1 && overviewMode && connected?((now/300+i)%2?0:4):4);
        snprintf(text,sizeof(text),"#%d %s",memberSlot[i]+1,cat==1?"! Attention":cat==2?"Working":"Idle");seatState[i].set(text);
        lv_obj_set_pos(seatState[i].obj,96,0);lv_obj_set_width(seatState[i].obj,cellW-96);
        lv_obj_set_pos(childLabel[i].obj,96,88);lv_obj_set_width(childLabel[i].obj,cellW-96);
        snprintf(text,sizeof(text),"%u workers",rows[i].children);childLabel[i].set(text);visible(childLabel[i].obj,rows[i].childrenKnown && rows[i].children>0);
        lv_obj_set_style_text_color(seatState[i].obj,lv_color_hex(cat==1?Theme::StatusAmber:Theme::HUDDim),0);
        // The canonical creature is already the graphical anchor; no disconnected icon column.
        lv_obj_set_pos(agentActivity[i].obj,rowX+96,rowY+24);lv_obj_set_width(agentActivity[i].obj,cellW-96);lv_obj_set_height(agentActivity[i].obj,60);
        agentActivity[i].set(rows[i].activity);visible(agentActivity[i].obj,rows[i].activity[0]);
        lv_obj_set_style_text_color(agentActivity[i].obj,lv_color_hex(cat==1?Theme::StatusAmber:Theme::HUDText),0);

    }
    visible(overviewEmpty.obj,!projectCount);overviewEmpty.set(count?"No sessions match this filter.":"Waiting for agents");
    if(!connected)attentionLine.set("Disconnected · Last received state");
    else if(totals[1]) {int first=0;while(first<count && category(rows[first].state)!=1)++first;snprintf(text,sizeof(text),"Attention %d · %s · %s",totals[1],rows[first].project,rows[first].activity);attentionLine.set(text);}
    else {snprintf(text,sizeof(text),"%d projects · %d working · %d idle",projectCount,totals[2],totals[3]);attentionLine.set(text);}
    lv_obj_set_style_text_color(attentionLine.obj,lv_color_hex(totals[1]?Theme::StatusAmber:Theme::HUDText),0);
    link.set(connected?"Connected":"Disconnected");
    lv_obj_set_style_text_color(link.obj,lv_color_hex(connected?Theme::HUDDim:Theme::StatusAmber),0);
    if(!connected) snprintf(text,sizeof(text),"Disconnected · Reconnecting");
    else if(totals[1]) snprintf(text,sizeof(text),"%d %s",totals[1],totals[1]==1?"session needs attention":"sessions need attention");
    else if(totals[2]) snprintf(text,sizeof(text),"%d agents working",totals[2]);
    else snprintf(text,sizeof(text),"%s",count?"Ready for the next task":"Waiting for agents");
    summary.set(text);
    for(int i=0;i<4;++i) {
        filterText[i].set(i?nameFor(i):"All");
        snprintf(text,sizeof(text),"%d",totals[i]);filterNumber[i].set(text);
        lv_obj_set_style_bg_color(filters[i],lv_color_hex(filter==i?Theme::ShallowWater:Theme::MidWater),0);
        lv_obj_set_style_border_width(filters[i],filter==i?2:0,0);
        lv_obj_set_style_border_color(filters[i],lv_color_hex(i?colorFor(i):Theme::StatusCyan),0);
    }
    if(rosterTotal>count) snprintf(text,sizeof(text),"Sessions · %d of %u",count,rosterTotal);
    else snprintf(text,sizeof(text),g_screenW<1100?"Sessions":"Sessions / inspect");
    rosterTitle.set(text);
    int shown=0;
    for(int i=0;i<10;++i) {
        const bool show=i<count && matches(rows[i]);visible(cards[i],show);if(!show)continue;
        const auto& r=rows[i];const int cat=category(r.state);
        lv_obj_set_y(cards[i],shown++*110);
        lv_obj_set_style_border_color(cards[i],lv_color_hex(!strcmp(r.id,selectedId)?Theme::StatusCyan:Theme::MidWater),0);
        rowTitle[i].set(r.name);
        snprintf(text,sizeof(text),"%s  /  %s",r.agent,nameFor(cat));rowState[i].set(text);
        lv_obj_set_style_text_color(rowState[i].obj,lv_color_hex(colorFor(cat)),0);
        rowTool[i].set(r.tool[0]?r.tool:"No active tool");
        const auto* glyph=glyphFor?glyphFor(r.agent):nullptr;visible(marks[i],glyph);
        if(glyph) {lv_image_set_src(marks[i],glyph);lv_image_set_scale(marks[i],128);lv_obj_set_style_image_recolor(marks[i],lv_color_hex(brandColor(r.agent)),0);}
    }
    visible(empty.obj,!shown);empty.set(count?"No matching sessions":"Waiting for agents");
    // Keep known quotas beside every view. Detail prioritizes actual content.
    lv_obj_set_x(voicePane,sceneW>584?sceneW-536:24);
    const bool portrait=g_screenW<1100;
    const int rw=portrait?sceneW:hasQuota?224:280;
    detailW=portrait?sceneW:sceneW-rw-24;
    lv_obj_set_y(rosterTitle.obj,220);lv_obj_set_size(rail,rw,portrait?112:g_screenH-310);lv_obj_set_scroll_dir(rail,portrait?LV_DIR_HOR:LV_DIR_VER);lv_obj_set_width(rosterTitle.obj,rw);
    lv_obj_set_pos(detail,portrait?24:48+rw,portrait?370:204);lv_obj_set_y(resourcePane,overviewMode?(needsAttention?178:130):portrait?370:204);lv_obj_set_size(detail,detailW,portrait?g_screenH-530:g_screenH-282);
    for(int i=0;i<4;++i){const int fw=(sceneW-36)/4;lv_obj_set_x(filters[i],24+i*(fw+12));lv_obj_set_width(filters[i],fw);lv_obj_set_width(filterText[i].obj,portrait?fw-16:fw-70);lv_obj_set_pos(filterText[i].obj,portrait?8:16,portrait?5:24);lv_obj_set_style_text_font(filterText[i].obj,portrait?&font_studio_16:&font_studio_20,0);lv_obj_set_pos(filterNumber[i].obj,fw-52,portrait?22:12);}
    for(int i=0;i<10;++i){const int cardW=portrait?224:rw;lv_obj_set_width(cards[i],cardW);if(portrait)lv_obj_set_pos(cards[i],i*236,0);else lv_obj_set_x(cards[i],0);lv_obj_set_width(rowTitle[i].obj,cardW-80);lv_obj_set_height(rowTitle[i].obj,font_studio_20.line_height);lv_obj_set_width(rowState[i].obj,cardW-32);lv_obj_set_x(rowState[i].obj,16);lv_obj_set_y(rowState[i].obj,48);lv_obj_set_width(rowTool[i].obj,cardW-32);}
    lv_obj_set_width(heading.obj,detailW-112);lv_obj_set_width(identity.obj,detailW-48);lv_obj_set_x(focusGlyph,detailW-80);
    visible(detail,!overviewMode && (chosen>=0 || retainedDetail));
    if(strcmp(presentedId,selectedId)) {
        snprintf(presentedId,sizeof(presentedId),"%s",selectedId);
        lv_obj_scroll_to_y(eventBox,0,LV_ANIM_OFF);
        lv_obj_scroll_to_y(activityScroll,0,LV_ANIM_OFF);
    }
    if(chosen>=0 || retainedDetail) {
        heading.set(selected.projectName[0]?selected.projectName:selected.agentType);
        snprintf(text,sizeof(text),"%s  /  %s  /  %lus",selected.agentType,selected.modelName[0]?selected.modelName:"Model unavailable",static_cast<unsigned long>(selected.elapsedSec));identity.set(text);
        const int cat=category(selected.state);
        const auto* fg=glyphFor?glyphFor(selected.agentType):nullptr;visible(focusGlyph,fg);
        if(fg){lv_image_set_src(focusGlyph,fg);lv_obj_set_style_image_recolor(focusGlyph,lv_color_hex(brandColor(selected.agentType)),0);}
        int knownNodes=0;
        for(int i=0;i<3;++i)knownNodes+=(i<2?selected.childrenKnown:selected.coordinationKnown);
        int shownNodes=0;
        for(int i=0;i<3;++i) {
            const bool known=i<2?selected.childrenKnown:selected.coordinationKnown;
            visible(stateNodes[i],known);
            if(!known)continue;
            const int nw=(detailW-48-(knownNodes-1)*8)/knownNodes;
            lv_obj_set_pos(stateNodes[i],24+shownNodes++*(nw+8),82);lv_obj_set_width(stateNodes[i],nw);
            const unsigned value=i==0?selected.childrenActive:i==1?selected.childrenCompleted:selected.backgroundJobs;
            snprintf(text,sizeof(text),"%u",value);censusValue[i].set(text);
        }
        const int y=knownNodes?154:82;
        snprintf(text,sizeof(text),"%s%s%s",nameFor(cat),selected.currentTool[0]?" · ":"",selected.currentTool);stateText.set(text);lv_obj_set_y(stateText.obj,y);lv_obj_set_width(stateText.obj,detailW-48);
        activity.set(cat==1 && selected.question[0]?selected.question:selected.activity[0]?selected.activity:selected.lastEventText);
        lv_obj_set_pos(activityScroll,24,y+32);lv_obj_set_width(activityScroll,detailW-48);lv_obj_set_width(activity.obj,detailW-56);lv_obj_set_height(activity.obj,LV_SIZE_CONTENT);lv_obj_update_layout(activity.obj);
        const int maxActivity=history?54:96,measured=lv_obj_get_height(activity.obj);
        const int activityH=activity.value[0]?(measured>maxActivity?maxActivity:measured<28?28:measured):0;
        lv_obj_set_height(activityScroll,activityH);visible(activityScroll,activityH>0);
        visible(census.obj,false);
        instruction.set(retainedDetail?"Saved detail · Agent is on another roster page":cat==1?"Review this request in the agent terminal.":"");visible(instruction.obj,retainedDetail || cat==1);
        const int after=y+activityH+40;
        lv_obj_set_y(instruction.obj,after);lv_obj_set_width(instruction.obj,detailW-48);
        const int eventStart=after+((retainedDetail || cat==1)?52:8);
        lv_obj_set_pos(historyButton,detailW-150,eventStart);lv_obj_set_size(historyButton,126,36);lv_obj_set_pos(historyLabel.obj,10,7);
        historyLabel.set(history?"Summary":"History");visible(historyButton,eventCount>0);
        lv_obj_set_pos(recentCaption,24,eventStart+6);lv_obj_set_width(recentCaption,detailW-190);visible(recentCaption,eventCount>0);
        lv_obj_set_pos(eventBox,24,eventStart+48);lv_obj_set_size(eventBox,detailW-48,lv_obj_get_height(detail)-eventStart-60);
        for(int i=0;i<8;++i){lv_obj_set_width(eventCards[i],detailW-48);lv_obj_set_width(eventText[i].obj,detailW-76);lv_obj_set_width(eventMeta[i].obj,detailW-76);}
        int eventY=0;
        for(int i=0;i<8;++i) {
            const bool show=i<(history?8:2) && i<eventCount;visible(eventCards[i],show);if(!show)continue;
            const auto& e=events[i];
            const char* kind=!strcmp(e.type,"chat_response")?"Response":!strcmp(e.type,"chat_start")?"Request":!strcmp(e.type,"tool_request")?"Tool call":!strcmp(e.type,"tool_result")?"Tool result":!strcmp(e.type,"error")?"Error":!strcmp(e.type,"task_start")?"Task started":"Activity";
            snprintf(text,sizeof(text),"%s  /  %s%s",e.hm,kind,i==0?" / latest":"");eventMeta[i].set(text);
            eventText[i].set(e.detail[0]?e.detail:e.raw);
            lv_obj_set_height(eventText[i].obj,history?LV_SIZE_CONTENT:62);
            lv_obj_update_layout(eventText[i].obj);
            int eh=history?lv_obj_get_height(eventText[i].obj)+50:104;if(eh<104)eh=104;
            lv_obj_set_y(eventCards[i],eventY);lv_obj_set_height(eventCards[i],eh);eventY+=eh+8;
        }
        if(!eventCount) {
            visible(eventCards[0],true);eventMeta[0].set("Recent activity");eventText[0].set("No events received for this session.");
        }
    }
    if(quota.cost>=0) snprintf(text,sizeof(text),"Daemon totals · Estimated $%.2f",quota.cost);
    else snprintf(text,sizeof(text),"Daemon totals");
    usage.set(text);visible(usage.obj,quota.cost>=0 || quota.input || quota.output || quota.calls);
    const uint32_t elapsed=micros()-started;
    portENTER_CRITICAL(&diagMux);
    ++diag.updates;diag.lastUpdateUs=elapsed;
    if(elapsed>diag.maxUpdateUs)diag.maxUpdateUs=elapsed;
    diag.width=g_screenW;diag.height=g_screenH;diag.sessions=count;diag.visibleSessions=shown;
    diag.filter=filter;diag.eventCount=eventCount;diag.connected=connected;
    diag.usageVisible=hasQuota;diag.quotaWindows=0;for(int i=0;i<6;++i)diag.quotaWindows+=quota.percent[i]>=0;diag.rosterTotal=rosterTotal;diag.rosterRotating=rosterRotating;
    diag.history=history;diag.voiceOpen=voiceOpen;diag.overview=overviewMode;diag.projects=projectCount;
    portEXIT_CRITICAL(&diagMux);
}
Diagnostics diagnostics() {
    portENTER_CRITICAL(&diagMux);const Diagnostics out=diag;portEXIT_CRITICAL(&diagMux);
    return out;
}
const char* selectedSession() { return selectedId; }
}
#endif
