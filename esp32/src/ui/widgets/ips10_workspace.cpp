#include "config.h"
#if defined(BOARD_IPS10)
#include "ips10_workspace.h"
#include "ips10_keycap_generated.h"
#include "../theme.h"
#include "../display.h"
#include "../../state/agent_state.h"
#include "../../util/utf8.h"
#include <Arduino.h>
#include "net/serial_client.h"
#include <cstdio>
#include <cstring>

LV_FONT_DECLARE(font_workspace_20);
LV_FONT_DECLARE(font_workspace_36);

namespace IPS10Workspace {
namespace {
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
static lv_obj_t *overview, *viewButton, *quotaCards[4], *quotaBars[4], *pods[10], *seats[10], *creatures[10];
static Text<40> viewLabel, quotaValue[4], quotaReset[4], podName[10], seatState[10];
static Text<160> tokenLine;
static Text<100> podStatus[10], overviewEmpty;
static Text<48> seatTool[10];
struct UsageSnapshot { float percent[4]; char reset[4][20]; uint32_t input, output, calls; float cost; };
static UsageSnapshot quota;
static int podFor[10], memberSlot[10], podMembers[10], projectCount;
static uint32_t brandColor(const char* agent) {
    if(!strcmp(agent,"claude-code") || !strcmp(agent,"claude")) return Theme::ClaudeBody;
    if(!strcmp(agent,"codex") || !strcmp(agent,"codex-cli")) return Theme::CloudBody;
    if(!strcmp(agent,"openclaw")) return Theme::CrayfishShell;
    if(!strcmp(agent,"kiro")) return Theme::KiroMark;
    if(!strcmp(agent,"antigravity")) return Theme::AntigravityMark;
    return Theme::OpenCodeOuter;
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
    lv_obj_set_style_text_font(l,&font_workspace_20,0);
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
static const char* nameFor(int c) { return c==1?"확인 필요":c==2?"작업 중":"대기"; }
static bool matches(const Row& r) { return !filter || category(r.state)==filter; }
static void selectCb(lv_event_t* e) {
    const int i=static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if(i<0 || i>=count) return;
    snprintf(selectedId,sizeof(selectedId),"%s",rows[i].id);
    overviewMode=false; history=false; lastUpdate=0; update();
}
static void filterCb(lv_event_t* e) {
    filter=static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    lastUpdate=0; update();
    lv_obj_scroll_to_y(rail,0,LV_ANIM_OFF);
}
static void historyCb(lv_event_t*) { history=!history;lastUpdate=0;update();lv_obj_scroll_to_y(eventBox,0,LV_ANIM_OFF); }
static void viewCb(lv_event_t*) { overviewMode=!overviewMode;lastUpdate=0;update(); }
static void voiceCb(lv_event_t*) { voiceOpen=!voiceOpen;visible(voicePane,voiceOpen); }
}

lv_obj_t* init(lv_obj_t* parent,const lv_image_dsc_t* (*glyph)(const char*)) {
    presentedId[0]=0;
    glyphFor=glyph; count=0; filter=0; history=false; voiceOpen=false;lastUpdate=0;
    overviewMode=true;
    const int w=g_screenW,h=g_screenH, railW=w>=1100?320:248;
    root=box(parent,0,0,w,h,Theme::DeepSea);lv_obj_set_style_radius(root,0,0);
    caption(root,"AgentDeck  /  PROJECT DECK",24,22,380,Theme::HUDText);
    viewButton=box(root,420,12,174,48,Theme::ShallowWater);
    label(viewLabel,viewButton,16,14,148,&font_workspace_20,Theme::HUDText);
    lv_obj_add_event_cb(viewButton,viewCb,LV_EVENT_CLICKED,nullptr);
    label(link,root,w-370,24,346,&font_workspace_20,Theme::HUDDim);
    lv_obj_set_style_text_align(link.obj,LV_TEXT_ALIGN_RIGHT,0);
    if(w<1100) lv_obj_set_pos(viewButton,w-198,62);
    label(summary,root,24,76,w-248,&font_workspace_20,Theme::HUDText);
    const int gap=12, fw=(w-48-gap*3)/4;
    for(int i=0;i<4;++i) {
        filters[i]=box(root,24+i*(fw+gap),118,fw,64,Theme::MidWater);
        lv_obj_add_flag(filters[i],LV_OBJ_FLAG_CLICKABLE);
        label(filterText[i],filters[i],16,24,fw-84,&font_workspace_20,Theme::HUDDim);
        label(filterNumber[i],filters[i],fw-66,12,56,&font_workspace_36,Theme::HUDText);
        lv_obj_add_event_cb(filters[i],filterCb,LV_EVENT_CLICKED,reinterpret_cast<void*>(static_cast<intptr_t>(i)));
    }
    label(rosterTitle,root,24,205,railW,&font_workspace_20,Theme::HUDDim);
    rail=box(root,24,238,railW,h-318,Theme::DeepSea);
    lv_obj_add_flag(rail,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(rail,LV_DIR_VER);
    lv_obj_set_style_pad_bottom(rail,8,0);
    for(int i=0;i<10;++i) {
        cards[i]=box(rail,0,i*110,railW,100,Theme::MidWater);
        lv_obj_set_style_border_width(cards[i],2,0);
        marks[i]=lv_image_create(cards[i]);lv_obj_set_pos(marks[i],0,4);
        lv_obj_set_style_image_recolor_opa(marks[i],LV_OPA_COVER,0);
        label(rowTitle[i],cards[i],64,16,railW-80,&font_workspace_20,Theme::HUDText);
        label(rowState[i],cards[i],64,44,railW-80,&font_kr_12,Theme::HUDDim);
        label(rowTool[i],cards[i],16,72,railW-32,&font_kr_12,Theme::HUDDim);
        lv_obj_set_height(rowTool[i].obj,18);
        lv_obj_add_event_cb(cards[i],selectCb,LV_EVENT_CLICKED,reinterpret_cast<void*>(static_cast<intptr_t>(i)));
    }
    detailW=w-railW-72;
    detail=box(root,48+railW,204,detailW,h-284,Theme::MidWater);
    label(heading,detail,24,22,detailW-124,&font_workspace_20,Theme::HUDText);
    label(identity,detail,24,54,detailW-124,&font_kr_12,Theme::HUDDim);
    focusGlyph=lv_image_create(detail);lv_obj_set_pos(focusGlyph,detailW-88,8);
    lv_obj_set_style_image_recolor_opa(focusGlyph,LV_OPA_COVER,0);
    // These are reported collaboration counts, never inferred progress. The
    // raised tiles provide a glanceable map of active/completed/background work.
    const int nw=(detailW-64)/3;
    for(int i=0;i<3;++i) {
        stateNodes[i]=box(detail,24+i*(nw+8),90,nw,58,Theme::DeepSea);
        caption(stateNodes[i],i==0?"하위 작업":i==1?"하위 완료":"백그라운드",12,8,nw-20);
        label(censusValue[i],stateNodes[i],12,30,nw-20,&font_workspace_20,Theme::HUDText);
    }
    label(stateText,detail,24,164,detailW-48,&font_workspace_20,Theme::HUDText);
    activityScroll=box(detail,24,192,detailW-48,54,Theme::MidWater);
    lv_obj_add_flag(activityScroll,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(activityScroll,LV_DIR_VER);
    label(activity,activityScroll,0,0,detailW-56,&font_workspace_20,Theme::HUDText);
    lv_label_set_long_mode(activity.obj,LV_LABEL_LONG_WRAP);
    label(census,detail,24,252,detailW-48,&font_kr_12,Theme::HUDDim);
    label(instruction,detail,24,276,detailW-48,&font_workspace_20,Theme::HUDDim);
    lv_obj_set_height(instruction.obj,44);
    historyButton=box(detail,detailW-166,324,142,48,Theme::ShallowWater);
    label(historyLabel,historyButton,14,16,120,&font_workspace_20,Theme::HUDText);
    lv_obj_add_event_cb(historyButton,historyCb,LV_EVENT_CLICKED,nullptr);
    caption(detail,"최근 활동",24,340,detailW-212);
    eventBox=box(detail,24,386,detailW-48,h-284-402,Theme::MidWater);
    lv_obj_add_flag(eventBox,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(eventBox,LV_DIR_VER);
    for(int i=0;i<8;++i) {
        eventCards[i]=box(eventBox,0,i*112,detailW-48,104,Theme::DeepSea);
        label(eventMeta[i],eventCards[i],14,12,detailW-76,&font_kr_12,Theme::StatusCyan);
        label(eventText[i],eventCards[i],14,36,detailW-76,&font_workspace_20,Theme::HUDText);
        lv_label_set_long_mode(eventText[i].obj,LV_LABEL_LONG_WRAP);
        lv_obj_set_height(eventText[i].obj,62);
    }
    sceneW=w-48;
    overview=box(root,24,204,sceneW,h-284,Theme::DeepSea);
    lv_obj_add_flag(overview,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(overview,LV_DIR_VER);
    lv_obj_set_style_pad_bottom(overview,12,0);
    const int qw=(sceneW-24)/4;
    const char* quotaNames[]={"Claude 5h","Claude 7d","Codex 기본","Codex 보조"};
    for(int i=0;i<4;++i) {
        quotaCards[i]=box(overview,i*(qw+8),0,qw,110,Theme::MidWater);
        caption(quotaCards[i],quotaNames[i],12,10,qw-24);
        label(quotaValue[i],quotaCards[i],12,36,qw-24,&font_workspace_20,Theme::HUDText);
        quotaBars[i]=lv_bar_create(quotaCards[i]);lv_obj_set_pos(quotaBars[i],12,66);lv_obj_set_size(quotaBars[i],qw-24,8);
        lv_obj_set_style_bg_color(quotaBars[i],lv_color_hex(Theme::ShallowWater),LV_PART_MAIN);
        label(quotaReset[i],quotaCards[i],12,82,qw-24,&font_kr_12,Theme::HUDDim);
    }
    label(tokenLine,root,24,h-54,w-238,&font_workspace_20,Theme::HUDText);
    caption(overview,"프로젝트 데크  /  에이전트 키를 눌러 살펴보기",12,124,sceneW-24);
    for(int i=0;i<10;++i) {
        pods[i]=box(overview,0,160,sceneW,168,Theme::MidWater);
        label(podName[i],pods[i],16,12,detailW-32,&font_workspace_20,Theme::HUDText);
        label(podStatus[i],pods[i],16,40,detailW-32,&font_kr_12,Theme::HUDDim);
        seats[i]=box(overview,0,0,108,108,Theme::ShallowWater);
        lv_obj_set_style_bg_opa(seats[i],LV_OPA_TRANSP,0);
        // Baked Blender relief + LVGL press state: depth without a 3D render loop.
        auto* keycap=lv_image_create(seats[i]);lv_image_set_src(keycap,&IPS10Keycap::image);
        lv_image_set_scale(keycap,208);lv_obj_set_pos(keycap,-10,-8);
        lv_obj_clear_flag(keycap,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_translate_y(seats[i],3,LV_STATE_PRESSED);
        lv_obj_add_event_cb(seats[i],selectCb,LV_EVENT_CLICKED,reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        creatures[i]=lv_image_create(seats[i]);lv_obj_set_pos(creatures[i],16,0);
        lv_obj_clear_flag(creatures[i],LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_image_recolor_opa(creatures[i],LV_OPA_COVER,0);
        label(seatState[i],seats[i],8,74,92,&font_kr_12,Theme::HUDText);
        label(seatTool[i],seats[i],8,92,92,&font_kr_12,Theme::HUDDim);
        lv_obj_set_height(seatTool[i].obj,16);
    }
    label(overviewEmpty,overview,16,174,sceneW-32,&font_workspace_20,Theme::HUDDim);
    label(empty,rail,12,12,railW-24,&font_workspace_20,Theme::HUDDim);
    lv_label_set_long_mode(empty.obj,LV_LABEL_LONG_WRAP);
    label(usage,root,24,h-26,w-230,&font_kr_12,Theme::HUDDim);
    auto* voice=box(root,w-186,h-64,162,48,Theme::ShallowWater);
    caption(voice,"음성 / 스피커",18,15,138,Theme::HUDText);
    lv_obj_add_event_cb(voice,voiceCb,LV_EVENT_CLICKED,nullptr);
    // A separate drawer preserves direct voice controls without consuming the
    // working surface while closed. Width is always the full screen minus 48.
    voicePane=box(root,24,h-252,w-48,178,Theme::ShallowWater);
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
    int totals[4]={}; float p5,p7,c5,c7; bool stale; uint16_t rosterTotal;
    lockState();
    connected=g_state.wsConnected || Net::serialConnected();
    count=0;
    for(int i=0;i<g_state.sessionCount && i<10;++i) {
        const auto& s=g_state.sessions[i]; if(!s.alive || !s.id[0]) continue;
        auto& r=rows[count++];
        snprintf(r.id,sizeof(r.id),"%s",s.id);snprintf(r.name,sizeof(r.name),"%s",s.lastEventTask[0]?s.lastEventTask:s.projectName[0]?s.projectName:s.agentType);
        snprintf(r.project,sizeof(r.project),"%s",s.projectName);
        snprintf(r.agent,sizeof(r.agent),"%s",s.agentType);snprintf(r.state,sizeof(r.state),"%s",s.state);
        snprintf(r.tool,sizeof(r.tool),"%s",s.currentTool[0]?s.currentTool:s.activity[0]?s.activity:s.lastEventText);r.elapsed=s.elapsedSec;
        ++totals[category(r.state)];
    }
    // Priority ordering is deterministic; selection still follows the ID.
    for(int i=1;i<count;++i) {
        Row r=rows[i];int j=i;
        while(j>0 && category(rows[j-1].state)>category(r.state)) {rows[j]=rows[j-1];--j;}
        rows[j]=r;
    }
    totals[0]=count;rosterTotal=g_state.sessionsTotal;
    int chosen=-1;
    for(int i=0;i<count;++i) if(matches(rows[i]) && !strcmp(rows[i].id,selectedId)) chosen=i;
    if(chosen<0) {
        for(int i=0;i<count;++i) if(matches(rows[i]) && (chosen<0 || category(rows[i].state)<category(rows[chosen].state))) chosen=i;
        snprintf(selectedId,sizeof(selectedId),"%s",chosen>=0?rows[chosen].id:"");history=false;
    }
    selected={};eventCount=0;
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
    const char* resets[]={g_state.fiveHourReset,g_state.sevenDayReset,g_state.codexPrimaryReset,g_state.codexSecondaryReset};
    for(int i=0;i<4;++i) snprintf(quota.reset[i],20,"%s",resets[i]);
    quota.input=g_state.inputTokens;quota.output=g_state.outputTokens;quota.calls=g_state.toolCalls;quota.cost=g_state.estimatedCostUsd;
    unlockState();
    char text[240];
    visible(overview,overviewMode);visible(rail,!overviewMode);visible(rosterTitle.obj,!overviewMode);viewLabel.set(overviewMode?"작업 상세":"프로젝트 모임");
    for(int i=0;i<4;++i) {
        const float p=quota.percent[i];const bool known=p>=0;
        if(known) snprintf(text,sizeof(text),"%.0f%% 사용",p);else snprintf(text,sizeof(text),"정보 없음");
        quotaValue[i].set(text);visible(quotaBars[i],known);
        lv_bar_set_value(quotaBars[i],known?(p>100?100:static_cast<int>(p)):0,LV_ANIM_OFF);
        lv_obj_set_style_bg_color(quotaBars[i],lv_color_hex(p>=90?Theme::StatusAmber:Theme::StatusCyan),LV_PART_INDICATOR);
        if(known && quota.reset[i][0])snprintf(text,sizeof(text),"리셋 %s",quota.reset[i]);else snprintf(text,sizeof(text),"%s",known?"리셋 정보 없음":"다음 수신 대기");
        quotaReset[i].set(text);
    }
    // These are the daemon's usage snapshot, not selected-session or project totals.
    if(quota.input || quota.output || quota.calls) snprintf(text,sizeof(text),"수신 토큰  IN %lu / OUT %lu   ·   도구 %lu",static_cast<unsigned long>(quota.input),static_cast<unsigned long>(quota.output),static_cast<unsigned long>(quota.calls));
    else snprintf(text,sizeof(text),"토큰 집계 대기  ·  계정 한도와 별도로 수신됩니다");
    tokenLine.set(text);
    projectCount=0;memset(podMembers,0,sizeof(podMembers));
    for(int i=0;i<count;++i) {
        podFor[i]=-1;if(!matches(rows[i]))continue;
        int group=-1;
        // Exact reported project identity only. Unnamed sessions remain separate.
        for(int j=0;j<i;++j) if(podFor[j]>=0 && rows[i].project[0] && !strcmp(rows[i].project,rows[j].project)) {group=podFor[j];break;}
        if(group<0) group=projectCount++;
        podFor[i]=group;memberSlot[i]=podMembers[group]++;
    }
    const int columns=sceneW>=700?2:1, pw=(sceneW-(columns-1)*12)/columns;
    const int seatsPerRow=(pw-24)/112;
    int columnY[2]={160,160};
    for(int g=0;g<10;++g) {
        visible(pods[g],g<projectCount);if(g>=projectCount)continue;
        const int col=g%columns, ph=60+((podMembers[g]+seatsPerRow-1)/seatsPerRow)*112;
        lv_obj_set_pos(pods[g],col*(pw+12),columnY[col]);lv_obj_set_size(pods[g],pw,ph);columnY[col]+=ph+12;
        int first=-1,working=0,attention=0;
        for(int i=0;i<count;++i)if(podFor[i]==g) {if(first<0)first=i;working+=category(rows[i].state)==2;attention+=category(rows[i].state)==1;}
        lv_obj_set_width(podName[g].obj,pw-32);lv_obj_set_width(podStatus[g].obj,pw-32);
        podName[g].set(rows[first].project[0]?rows[first].project:"프로젝트 미지정");
        snprintf(text,sizeof(text),"%d명  ·  작업 %d  ·  확인 %d",podMembers[g],working,attention);podStatus[g].set(text);
    }
    for(int i=0;i<10;++i) {
        const bool show=i<count && podFor[i]>=0;visible(seats[i],show);if(!show)continue;
        if(lv_obj_get_parent(seats[i])!=pods[podFor[i]]) lv_obj_set_parent(seats[i],pods[podFor[i]]);
        const int slot=memberSlot[i],cat=category(rows[i].state);
        lv_obj_set_pos(seats[i],12+(slot%seatsPerRow)*112,56+(slot/seatsPerRow)*112);
        const auto* glyph=glyphFor?glyphFor(rows[i].agent):nullptr;visible(creatures[i],glyph);
        if(glyph) {lv_image_set_src(creatures[i],glyph);lv_image_set_scale(creatures[i],192);lv_obj_set_style_image_opa(creatures[i],cat==3?LV_OPA_60:LV_OPA_COVER,0);lv_obj_set_style_image_recolor(creatures[i],lv_color_hex(brandColor(rows[i].agent)),0);}
        // Only awaiting animates. Brand identity stays intact; status is a separate badge.
        lv_obj_set_y(creatures[i],cat==1 && overviewMode && connected?((now/300+i)%2?0:4):4);
        snprintf(text,sizeof(text),"%s %s",cat==1?"!":cat==2?">":"z",nameFor(cat));seatState[i].set(text);
        lv_obj_set_style_text_color(seatState[i].obj,lv_color_hex(colorFor(cat)),0);
        seatTool[i].set(rows[i].tool[0]?rows[i].tool:rows[i].agent);
    }
    visible(overviewEmpty.obj,!projectCount);overviewEmpty.set(count?"이 상태의 작업이 없습니다. 다른 상태를 선택하세요.":"에이전트가 연결되면 프로젝트 모임이 나타납니다.");
    link.set(connected?"연결됨  ·  실시간 작업 현황":"연결 끊김  ·  마지막 수신 내용");
    lv_obj_set_style_text_color(link.obj,lv_color_hex(connected?Theme::HUDDim:Theme::StatusAmber),0);
    if(!connected) snprintf(text,sizeof(text),"연결을 복구하고 있습니다. 아래는 마지막 수신 상태입니다.");
    else if(totals[1]) snprintf(text,sizeof(text),"%d개의 작업에 확인이 필요합니다",totals[1]);
    else if(totals[2]) snprintf(text,sizeof(text),"%d개의 에이전트가 작업하고 있습니다",totals[2]);
    else snprintf(text,sizeof(text),"%s",count?"다음 작업을 기다리고 있습니다":"연결된 에이전트를 기다리고 있습니다");
    summary.set(text);
    for(int i=0;i<4;++i) {
        filterText[i].set(i?nameFor(i):"전체");
        snprintf(text,sizeof(text),"%d",totals[i]);filterNumber[i].set(text);
        lv_obj_set_style_bg_color(filters[i],lv_color_hex(filter==i?Theme::ShallowWater:Theme::MidWater),0);
        lv_obj_set_style_border_width(filters[i],filter==i?2:0,0);
        lv_obj_set_style_border_color(filters[i],lv_color_hex(i?colorFor(i):Theme::StatusCyan),0);
    }
    if(rosterTotal>count) snprintf(text,sizeof(text),"작업 목록  ·  %d / %u 표시",count,rosterTotal);
    else snprintf(text,sizeof(text),g_screenW<1100?"작업 목록 / 선택":"작업 목록 / 터치하여 살펴보기");
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
        rowTool[i].set(r.tool[0]?r.tool:"현재 도구 없음");
        const auto* glyph=glyphFor?glyphFor(r.agent):nullptr;visible(marks[i],glyph);
        if(glyph) {lv_image_set_src(marks[i],glyph);lv_image_set_scale(marks[i],128);lv_obj_set_style_image_recolor(marks[i],lv_color_hex(brandColor(r.agent)),0);}
    }
    visible(empty.obj,!shown);empty.set(count?"이 상태의 작업이 없습니다.\n다른 상태를 선택하세요.":"에이전트가 연결되면\n작업이 여기에 나타납니다.");
    visible(detail,!overviewMode && chosen>=0);
    if(strcmp(presentedId,selectedId)) {
        snprintf(presentedId,sizeof(presentedId),"%s",selectedId);
        lv_obj_scroll_to_y(eventBox,0,LV_ANIM_OFF);
        lv_obj_scroll_to_y(activityScroll,0,LV_ANIM_OFF);
    }
    if(chosen>=0) {
        heading.set(selected.projectName[0]?selected.projectName:selected.agentType);
        snprintf(text,sizeof(text),"%s  /  %s  /  %lus",selected.agentType,selected.modelName[0]?selected.modelName:"모델 정보 없음",static_cast<unsigned long>(selected.elapsedSec));identity.set(text);
        const int cat=category(selected.state);
        const auto* fg=glyphFor?glyphFor(selected.agentType):nullptr;visible(focusGlyph,fg);
        if(fg){lv_image_set_src(focusGlyph,fg);lv_obj_set_style_image_recolor(focusGlyph,lv_color_hex(brandColor(selected.agentType)),0);}
        for(int i=0;i<3;++i) {
            const bool known=i==2?selected.coordinationKnown:selected.childrenKnown;
            const unsigned value=i==0?selected.childrenActive:i==1?selected.childrenCompleted:selected.backgroundJobs;
            if(known)snprintf(text,sizeof(text),"%u",value);else snprintf(text,sizeof(text),"-");
            censusValue[i].set(text);
        }
        snprintf(text,sizeof(text),"%s  ·  %s",nameFor(cat),selected.currentTool[0]?selected.currentTool:"현재 도구 없음");stateText.set(text);
        activity.set(cat==1 && selected.question[0]?selected.question:selected.activity[0]?selected.activity:selected.lastEventText[0]?selected.lastEventText:"아직 작업 내용이 도착하지 않았습니다");
        if(selected.lastEventTask[0]) snprintf(text,sizeof(text),"%s / %s",selected.lastEventHm,selected.lastEventTask);
        else snprintf(text,sizeof(text),"%s",selected.childrenKnown?"에이전트가 보고한 협업 현황":"협업 정보가 없는 항목은 - 로 표시합니다");
        census.set(text);
        instruction.set(cat==1?"위 내용을 확인하고 해당 에이전트의 터미널에서 응답하세요.":cat==2?"실제 수신된 활동과 최근 결과를 아래에서 확인할 수 있습니다.":"대기 상태는 작업 완료를 의미하지 않습니다.");
        historyLabel.set(history?"요약 보기":"기록 보기");
        int eventY=0;
        for(int i=0;i<8;++i) {
            const bool show=i<(history?8:2) && i<eventCount;visible(eventCards[i],show);if(!show)continue;
            const auto& e=events[i];
            const char* kind=!strcmp(e.type,"chat_response")?"응답":!strcmp(e.type,"chat_start")?"요청":!strcmp(e.type,"tool_request")?"도구 실행":!strcmp(e.type,"tool_result")?"도구 결과":!strcmp(e.type,"error")?"오류":!strcmp(e.type,"task_start")?"작업 시작":"활동";
            snprintf(text,sizeof(text),"%s  /  %s%s",e.hm,kind,i==0?"  /  최근":"");eventMeta[i].set(text);
            eventText[i].set(e.detail[0]?e.detail:e.raw);
            lv_obj_set_height(eventText[i].obj,history?LV_SIZE_CONTENT:62);
            lv_obj_update_layout(eventText[i].obj);
            int eh=history?lv_obj_get_height(eventText[i].obj)+50:104;if(eh<104)eh=104;
            lv_obj_set_y(eventCards[i],eventY);lv_obj_set_height(eventCards[i],eh);eventY+=eh+8;
        }
        if(!eventCount) {
            visible(eventCards[0],true);eventMeta[0].set("최근 기록");eventText[0].set("이 작업에서 수신된 기록이 없습니다.");
        }
    }
    if(quota.cost>=0) snprintf(text,sizeof(text),"데몬 수신 집계 · 추정 비용 $%.2f · 선택 작업의 개별 합계가 아닙니다",quota.cost);
    else snprintf(text,sizeof(text),"데몬 수신 집계 · 선택 작업의 개별 합계가 아닙니다");
    usage.set(text);
    const uint32_t elapsed=micros()-started;
    portENTER_CRITICAL(&diagMux);
    ++diag.updates;diag.lastUpdateUs=elapsed;
    if(elapsed>diag.maxUpdateUs)diag.maxUpdateUs=elapsed;
    diag.width=g_screenW;diag.height=g_screenH;diag.sessions=count;diag.visibleSessions=shown;
    diag.filter=filter;diag.eventCount=eventCount;diag.connected=connected;
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
