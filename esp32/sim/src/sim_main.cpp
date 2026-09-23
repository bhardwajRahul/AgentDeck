extern const char* g_simVoiceState;
// AgentDeck ESP32 host simulator — entry point.
//
// Drives the real firmware render surface against a headless host backend and
// dumps board-accurate PNG frames. Because the render sources are compiled
// verbatim with the target board's defines (SCREEN_W/H + BOARD_*), the output is
// pixel-exact with what the physical panel shows — not a hand-drawn approximation.
//
// LCD/terrarium boards render via a headless LVGL display; the TC001 matrix board
// (BOARD_LED8X32) is LVGL-free and renders its CRGB pages upscaled instead.
//
// Usage (LCD):    sim [--scene NAME] [--frames N] [--out PATH] [--label NAME]
//                 T-Display Pro also accepts --page focus|usage|sessions
//                 TTGO accepts --page usage|terrarium and --landscape
//                 sim --all [--frames N] [--outdir DIR] [--label NAME]
// Usage (matrix): sim [--scene NAME] [--page usage|agents] [--scale N] [--out PATH]
//                 sim --all [--outdir DIR] [--scale N]
#include "sim.h"
#include "config.h"
#include "state/agent_state.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

namespace {
const char* arg(int argc, char** argv, const char* key, const char* def) {
  for (int i = 1; i < argc - 1; i++)
    if (std::strcmp(argv[i], key) == 0) return argv[i + 1];
  return def;
}
bool flag(int argc, char** argv, const char* key) {
  for (int i = 1; i < argc; i++)
    if (std::strcmp(argv[i], key) == 0) return true;
  return false;
}
const char* SCENES[] = {"empty", "offline", "idle", "display-off", "working", "multi", "crowd", "dense", "permission", "attention", "decision"};
}  // namespace

#if defined(BOARD_LED8X32)
// ── TC001 8×32 LED matrix ────────────────────────────────────────────────────
int main(int argc, char** argv) {
  const char* label = arg(argc, argv, "--label", "led8x32");
  int frames = std::atoi(arg(argc, argv, "--frames", "60"));
  int scale = std::atoi(arg(argc, argv, "--scale", "16"));
  if (frames < 1) frames = 1;

  auto one = [&](const char* scene, const char* page, const char* path) {
    bool ok = SimMatrix::renderToPng(scene, page, frames, scale, path);
    std::fprintf(stderr, "[sim] %-11s %-6s → %s %s\n", scene, page, path, ok ? "ok" : "FAILED");
    return ok;
  };

  if (flag(argc, argv, "--all")) {
    const char* outdir = arg(argc, argv, "--outdir", "sim-out");
    const char* pages[] = {"usage", "agents"};
    bool allOk = true;
    for (const char* s : SCENES)
      for (const char* p : pages) {
        std::string path = std::string(outdir) + "/" + label + "-" + s + "-" + p + ".png";
        allOk &= one(s, p, path.c_str());
      }
    return allOk ? 0 : 1;
  }

  const char* scene = arg(argc, argv, "--scene", "working");
  const char* page = arg(argc, argv, "--page", "usage");
  std::string def = std::string("sim-out/") + label + "-" + scene + "-" + page + ".png";
  const char* out = arg(argc, argv, "--out", def.c_str());
  return one(scene, page, out) ? 0 : 1;
}

#elif defined(BOARD_TRMNL_75)
// ── TRMNL 7.5" 800×480 1-bit e-ink ──────────────────────────────────────────────
int main(int argc, char** argv) {
  const char* label = arg(argc, argv, "--label", "trmnl_75");
  auto one = [&](const char* scene, const char* path) {
    bool ok = SimEink::renderToPng(scene, path);
    std::fprintf(stderr, "[sim] %-11s → %s (800x480 e-ink) %s\n", scene, path, ok ? "ok" : "FAILED");
    return ok;
  };
  if (flag(argc, argv, "--all")) {
    const char* outdir = arg(argc, argv, "--outdir", "sim-out");
    bool allOk = true;
    for (const char* s : SCENES) {
      std::string path = std::string(outdir) + "/" + label + "-" + s + ".png";
      allOk &= one(s, path.c_str());
    }
    return allOk ? 0 : 1;
  }
  const char* scene = arg(argc, argv, "--scene", "working");
  std::string def = std::string("sim-out/") + label + "-" + scene + ".png";
  const char* out = arg(argc, argv, "--out", def.c_str());
  return one(scene, out) ? 0 : 1;
}

#else
// ── LCD boards (headless LVGL, real per-board screen composition) ─────────────
// Three render trees share this path because all three are LVGL screens driven
// by create()/update(dt): the terrarium+HUD aquarium, the T-Embed knob, and the
// T-Display-S3-Pro ticker. Only the create/update pair differs per board.
#if defined(BOARD_T_EMBED)
#include "ui/knob/knob_ui.h"
#elif defined(BOARD_T_DISPLAY_PRO)
#include "ui/ticker/ticker_ui.h"
#else
#include "ui/screens/aquarium.h"
#if defined(BOARD_TTGO)
#include "ui/widgets/ttgo_usage.h"
#include "ui/display.h"
#endif
#endif

#if defined(BOARD_IPS10)
#include "../../src/audio/wake_word.h"
#include "../../src/ui/widgets/ips10_workspace.h"
extern bool g_simSerialConnected;
#endif

#if defined(BOARD_T_EMBED) || defined(BOARD_T_DISPLAY_PRO)
#include "companion_checks.h"
#endif

namespace {
constexpr uint32_t FRAME_MS = 33;                 // ~30fps
constexpr float    FRAME_DT = FRAME_MS / 1000.0f;

// Board render tree: build the screen once, then advance it per frame.
#if defined(BOARD_T_EMBED)
void treeCreate() { Knob::create(); }             // loads its own screen
void treeUpdate(float dt) { Knob::update(dt); }
#elif defined(BOARD_T_DISPLAY_PRO)
void treeCreate() { Ticker::create(); }
void treeUpdate(float dt) { Ticker::update(dt); }
#else
void treeCreate() { SimDisplay::loadScreen(Screens::aquariumCreate()); }
void treeUpdate(float dt) { Screens::aquariumUpdate(dt); }
#endif

bool renderScene(const char* scene, const char* path, int frames, const char* page) {
  if (!SimScenes::apply(scene)) {
    std::fprintf(stderr, "[sim] unknown scene '%s' (have: %s)\n", scene, SimScenes::catalog());
    return false;
  }
  randomSeed(0xA6E7DECC);  // deterministic frames per run
  g_sim_millis = 0;
#if defined(BOARD_T_DISPLAY_PRO)
  // Physical rocker navigation, exercised without a hand-authored preview.
  // Attention scenes still correctly snap back to Focus inside update().
  if (std::strcmp(page, "usage") == 0) Ticker::nextPage();
  else if (std::strcmp(page, "sessions") == 0) {
    Ticker::nextPage();
    Ticker::nextPage();
  }
#else
  (void)page;
#endif
  for (int i = 0; i < frames; i++) {
    SimDisplay::tick(FRAME_MS);
    treeUpdate(FRAME_DT);
    SimDisplay::refresh();
  }
#if defined(BOARD_T_EMBED)
  if (std::strcmp(scene, "attention") == 0 && Knob::selectedSessionIdx() != 1) {
    std::fprintf(stderr, "[sim] attention regression: selected=%d, expected awaiting session 1\n",
                 Knob::selectedSessionIdx());
    return false;
  }
  if (std::strcmp(scene, "attention") == 0) {
    // The daemon's roster is not a stable array. Automatic pager focus must
    // follow the awaiting session ID when its index changes, instead of
    // falling back to the idle OpenClaw card now occupying the old index.
    SessionInfo swap = g_state.sessions[0];
    g_state.sessions[0] = g_state.sessions[1];
    g_state.sessions[1] = swap;
    Knob::update(FRAME_DT);
    if (Knob::selectedSessionIdx() != 0) {
      std::fprintf(stderr,
                   "[sim] attention reorder regression: selected=%d, expected awaiting session 0\n",
                   Knob::selectedSessionIdx());
      return false;
    }
  }
#endif
  bool ok = SimPng::writeRgb565(path, SimDisplay::framebuffer(),
                                SimDisplay::width(), SimDisplay::height());
  std::fprintf(stderr, "[sim] %-11s → %s (%dx%d, %d frames) %s\n",
               scene, path, SimDisplay::width(), SimDisplay::height(), frames,
               ok ? "ok" : "FAILED");
  return ok;
}
#if defined(BOARD_IPS10)
lv_obj_t* ipsLabel(lv_obj_t* obj, const char* text) {
  if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return nullptr;
  if (lv_obj_check_type(obj, &lv_label_class) && strstr(lv_label_get_text(obj), text)) return obj;
  for(uint32_t i=0;i<lv_obj_get_child_count(obj);++i)
    if(auto* found=ipsLabel(lv_obj_get_child(obj,i),text)) return found;
  return nullptr;
}
bool ipsFailure(int line) {
  std::fprintf(stderr,"[sim] IPS10 check failed at line %d\n",line);
  return false;
}
bool verifyIpsInteractions(const char* outdir) {
  auto save=[&](const char* name) {
    const std::string path=std::string(outdir)+"/"+name+".png";
    return SimPng::writeRgb565(path.c_str(),SimDisplay::framebuffer(),SimDisplay::width(),SimDisplay::height());
  };
  auto advance=[] { SimDisplay::tick(300);treeUpdate(.3f);SimDisplay::refresh(); };
  auto click=[&](const char* text) {
    auto* l=ipsLabel(lv_screen_active(),text);
    if(!l) {std::fprintf(stderr,"missing control: %s\n",text);return ipsFailure(__LINE__);}
    lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,nullptr);advance();return true;
  };
  const std::string overview=std::string(outdir)+"/ips10-overview.png";
  if(!renderScene("crowd",overview.c_str(),90,"focus")) return ipsFailure(__LINE__);
  if(std::strcmp(IPS10Workspace::selectedSession(),"s4-AgentDeck")) return ipsFailure(__LINE__);
  if(!ipsLabel(lv_screen_active(),"5명  -  작업 3  -  확인 1"))return ipsFailure(__LINE__);
  if(!ipsLabel(lv_screen_active(),"IN 128000 / OUT 41000"))return ipsFailure(__LINE__);
  if(!IPS10Workspace::diagnostics().overview || IPS10Workspace::diagnostics().projects!=3)return ipsFailure(__LINE__);
  if(!ipsLabel(lv_screen_active(),"Bash 명령 실행") || !ipsLabel(lv_screen_active(),"권한 요청:"))return ipsFailure(__LINE__);
  const char* voiceStates[]={"listening","sending","waiting","speaking","error","muted","wake"};
  const char* voiceLabels[]={"듣고 있습니다","보내고 있습니다","응답을 기다리고","읽어드리고","음성 연결","음성 호출 꺼짐","음성 호출 대기"};
  for(int i=0;i<7;++i){g_simVoiceState=voiceStates[i];advance();if(!ipsLabel(lv_screen_active(),voiceLabels[i]) || IPS10Workspace::diagnostics().voiceOpen)return ipsFailure(__LINE__);}
  if(g_state.zaiPrimaryPercent!=-1 || g_state.zaiSecondaryPercent!=-1)return ipsFailure(__LINE__);
  auto* projectName=ipsLabel(lv_screen_active(),"5명  -  작업 3");
  lv_area_t before;lv_obj_get_coords(lv_obj_get_parent(projectName),&before);
  const auto saved=g_state.sessions[0];std::snprintf(g_state.sessions[0].state,sizeof(g_state.sessions[0].state),"awaiting_permission");advance();
  auto* changedProject=ipsLabel(lv_screen_active(),"5명  -  작업 2");if(!changedProject)return ipsFailure(__LINE__);
  lv_area_t after;lv_obj_get_coords(lv_obj_get_parent(changedProject),&after);
  if(before.x1!=after.x1 || before.y1!=after.y1)return false;
  g_state.sessions[0]=saved;advance();
  // Clicking an actual creature seat opens its exact session, not just its project.
  static lv_point_t point{};static bool pressed=false;
  auto* pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(pointer,[](lv_indev_t*,lv_indev_data_t* data) {data->point=point;data->state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;});
  auto* attention=ipsLabel(lv_screen_active(),"! 확인 필요");if(!attention)return ipsFailure(__LINE__);
  lv_area_t key;lv_obj_get_coords(lv_obj_get_parent(attention),&key);
  point.x=(key.x1+key.x2)/2;point.y=key.y1+30;
  pressed=true;lv_indev_read(pointer);SimDisplay::refresh();
  if(!lv_obj_has_state(lv_obj_get_parent(attention),LV_STATE_PRESSED))return ipsFailure(__LINE__);
  if(!save("ips10-key-pressed"))return ipsFailure(__LINE__);
  pressed=false;lv_indev_read(pointer);advance();
  lv_indev_delete(pointer);
  if(IPS10Workspace::diagnostics().overview)return ipsFailure(__LINE__);
  if(!ipsLabel(lv_screen_active(),"권한 요청:")) return ipsFailure(__LINE__); // ring head is oldest
  if(!click("ips10 카드 개선")) return ipsFailure(__LINE__);
  if(std::strcmp(IPS10Workspace::selectedSession(),"s2-AgentDeck")) return ipsFailure(__LINE__);
  if(!ipsLabel(lv_screen_active(),"Fixed the treemap")) return ipsFailure(__LINE__);
  if(ipsLabel(lv_screen_active(),"권한 요청:")) return ipsFailure(__LINE__);
  if(!save("ips10-selected")) return ipsFailure(__LINE__);
  // Reordering the daemon roster must not retarget detail or voice.
  std::swap(g_state.sessions[0],g_state.sessions[2]);advance();
  if(std::strcmp(IPS10Workspace::selectedSession(),"s2-AgentDeck")) return ipsFailure(__LINE__);
  TimelineEntry entry{};
  std::snprintf(entry.raw,sizeof(entry.raw),"GLOBAL-UNATTRIBUTED");g_state.addTimelineEntry(entry);
  std::snprintf(entry.sessionId,sizeof(entry.sessionId),"s4-AgentDeck");
  std::snprintf(entry.raw,sizeof(entry.raw),"OTHER-SESSION-ONLY");g_state.addTimelineEntry(entry);
  // Exercise wrapped rings, newest-first ordering, and the eight-row bound.
  std::snprintf(entry.sessionId,sizeof(entry.sessionId),"s2-AgentDeck");
  for(int i=0;i<TIMELINE_MAX_ENTRIES+3;++i) {
    std::snprintf(entry.raw,sizeof(entry.raw),"SELECTED-EVENT-%03d",i);g_state.addTimelineEntry(entry);
  }
  advance();if(!click("기록 보기")) return ipsFailure(__LINE__);
  char latest[40];std::snprintf(latest,sizeof(latest),"SELECTED-EVENT-%03d",TIMELINE_MAX_ENTRIES+2);
  if(!ipsLabel(lv_screen_active(),latest)) return ipsFailure(__LINE__);
  if(ipsLabel(lv_screen_active(),"GLOBAL-UNATTRIBUTED") || ipsLabel(lv_screen_active(),"OTHER-SESSION-ONLY")) return ipsFailure(__LINE__);
  if(!save("ips10-history")) return ipsFailure(__LINE__);
  auto* logLabel=ipsLabel(lv_screen_active(),latest);
  auto* logPane=lv_obj_get_parent(lv_obj_get_parent(logLabel));
  lv_obj_scroll_to_y(logPane,500,LV_ANIM_OFF);
  std::snprintf(entry.sessionId,sizeof(entry.sessionId),"s1-AgentDeck");
  std::snprintf(entry.raw,sizeof(entry.raw),"NEW-SESSION-EVENT");
  g_state.addTimelineEntry(entry);g_state.addTimelineEntry(entry);advance();
  if(!click("TRMNL timeline"))return ipsFailure(__LINE__);
  if(lv_obj_get_scroll_y(logPane)!=0)return ipsFailure(__LINE__); // new task starts at newest event
  if(!click("ips10 카드 개선"))return ipsFailure(__LINE__);
  g_state.sessions[0].alive=false;advance();
  if(!std::strcmp(IPS10Workspace::selectedSession(),"s2-AgentDeck")) return ipsFailure(__LINE__);
  g_state.sessions[4].alive=false;advance();
  if(!click("확인 필요")) return ipsFailure(__LINE__);
  if(IPS10Workspace::selectedSession()[0] || !ipsLabel(lv_screen_active(),"이 상태의 작업이 없습니다")) return ipsFailure(__LINE__);
  if(!click("전체")) return ipsFailure(__LINE__);
  if(!click("음성 / 스피커")) return ipsFailure(__LINE__);
  auto* talk=ipsLabel(lv_screen_active(),"Hold to talk");if(!talk)return ipsFailure(__LINE__);
  lv_area_t bounds;lv_obj_get_coords(lv_obj_get_parent(talk),&bounds);
  if(bounds.x1<0 || bounds.x2>=g_screenW || bounds.y1<0 || bounds.y2>=g_screenH)return ipsFailure(__LINE__);
  if(!save("ips10-voice-controls") || !click("음성 / 스피커"))return ipsFailure(__LINE__);
  g_state.wsConnected=false;g_simSerialConnected=false;advance();
  if(!ipsLabel(lv_screen_active(),"연결 끊김"))return ipsFailure(__LINE__);
  if(!save("ips10-offline"))return ipsFailure(__LINE__);
  g_state.sessionCount=0;advance();
  if(IPS10Workspace::selectedSession()[0] || !ipsLabel(lv_screen_active(),"에이전트가 연결되면"))return ipsFailure(__LINE__);
  if(!save("ips10-empty"))return ipsFailure(__LINE__);
  g_simSerialConnected=true;
  if(!click("프로젝트 모임"))return ipsFailure(__LINE__);
  g_state.usageStale=false;g_state.fiveHourPercent=0;g_state.codexPrimaryPercent=23;advance();
  if(!ipsLabel(lv_screen_active(),"0%"))return ipsFailure(__LINE__);
  g_state.usageStale=true;advance();
  if(ipsLabel(lv_screen_active(),"0%") || !ipsLabel(lv_screen_active(),"23%"))return ipsFailure(__LINE__);
  if(!save("ips10-stale-usage"))return ipsFailure(__LINE__);
  // Ten distinct projects must remain ten pods; similarly named worktrees are
  // not evidence of a shared project or an actual delegation relationship.
  g_state.sessionCount=10;
  for(int i=0;i<10;++i) {
    auto& session=g_state.sessions[i];session={};session.alive=true;
    std::snprintf(session.id,sizeof(session.id),"stress-%d",i);
    std::snprintf(session.projectName,sizeof(session.projectName),"shared-prefix-project-%d",i);
    std::snprintf(session.agentType,sizeof(session.agentType),"codex-cli");
    std::snprintf(session.state,sizeof(session.state),"processing");
  }
  advance();if(IPS10Workspace::diagnostics().projects!=10)return ipsFailure(__LINE__);
  if(!save("ips10-ten-projects"))return ipsFailure(__LINE__);
  SimDisplay::tick(31000);treeUpdate(31);SimDisplay::refresh();
  if(!ipsLabel(lv_screen_active(),"shared-prefix-project-4") || ipsLabel(lv_screen_active(),"shared-prefix-project-0"))return ipsFailure(__LINE__);
  // Attention is visible even when its project is on another page.
  std::snprintf(g_state.sessions[0].state,sizeof(g_state.sessions[0].state),"awaiting_permission");
  std::snprintf(g_state.sessions[0].question,sizeof(g_state.sessions[0].question),"OFF-PAGE-ATTENTION");advance();
  if(!ipsLabel(lv_screen_active(),"OFF-PAGE-ATTENTION"))return ipsFailure(__LINE__);
  auto* pager=ipsLabel(lv_screen_active(),"30초");if(!pager)return ipsFailure(__LINE__);
  lv_obj_send_event(pager,LV_EVENT_CLICKED,nullptr);advance();
  SimDisplay::tick(31000);treeUpdate(31);SimDisplay::refresh();
  if(!ipsLabel(lv_screen_active(),"shared-prefix-project-4"))return ipsFailure(__LINE__);
  lv_obj_send_event(pager,LV_EVENT_CLICKED,nullptr);advance();
  std::snprintf(g_state.sessions[0].state,sizeof(g_state.sessions[0].state),"processing");
  g_state.zaiPrimaryPercent=12;g_state.zaiSecondaryPercent=7;g_state.zaiSecondaryIsMcp=true;g_state.antigravityCredits=812;advance();
  if(!ipsLabel(lv_screen_active(),"z.ai MCP") || !ipsLabel(lv_screen_active(),"812") || ipsLabel(lv_screen_active(),"812%"))return ipsFailure(__LINE__);
  for(int i=0;i<10;++i) std::snprintf(g_state.sessions[i].projectName,sizeof(g_state.sessions[i].projectName),"Shared project");
  advance();if(IPS10Workspace::diagnostics().projects!=1)return ipsFailure(__LINE__);
  if(!ipsLabel(lv_screen_active(),"10명  -  작업 10"))return ipsFailure(__LINE__);
  if(!save("ips10-ten-peers"))return ipsFailure(__LINE__);
  for(int i=0;i<10;++i) g_state.sessions[i].projectName[0]=0;
  advance();if(IPS10Workspace::diagnostics().projects!=10)return ipsFailure(__LINE__);
  g_state.markBridgeDisconnected();
  if(g_state.zaiPrimaryPercent!=-1 || g_state.zaiSecondaryPercent!=-1 || g_state.zaiSecondaryIsMcp)return false;
  std::fprintf(stderr,"[sim] IPS10 observation, voice states, stable placement, pages, quotas, selection, attribution, drawer, offline: ok\n");
  return true;
}
#endif
}  // namespace

int main(int argc, char** argv) {
  const char* label = arg(argc, argv, "--label", "board");
  const char* page = arg(argc, argv, "--page", "focus");
  int frames = std::atoi(arg(argc, argv, "--frames", "90"));  // 3s settle
  if (frames < 1) frames = 1;

  // Display resolution is fixed at compile time by the board's SCREEN_W/H build
  // flags — the sim IS that board minus hardware I/O. The tree builds the real
  // per-board composed screen (Terrarium+HUD / Office / TTGO overlay / knob /
  // ticker).
#if defined(BOARD_TTGO)
  if (flag(argc, argv, "--landscape")) {
    g_screenW = SCREEN_H;
    g_screenH = SCREEN_W;
  }
  if (flag(argc, argv, "--verify-mode")) {
    // Startup default, repeated physical-mode transitions and screen lifetime:
    // rotating rebuilds the tree without resetting the operator's mode.
    if (!TTGO::Usage::active()) return 1;
    SimDisplay::init(g_screenW, g_screenH);
    treeCreate();
    for (int i = 0; i < 12; ++i) {
      TTGO::Usage::toggle();
      const bool expected = (i % 2) != 0;
      if (TTGO::Usage::active() != expected) return 1;
      auto* old = lv_screen_active();
      treeCreate();
      lv_obj_delete(old);
      if (TTGO::Usage::active() != expected) return 1;
      SimScenes::apply("multi");
      treeUpdate(FRAME_DT);
      SimDisplay::refresh();
    }
    std::fprintf(stderr, "[sim] TTGO mode + 12 screen rebuilds: ok\n");
    return 0;
  }
  if (std::strcmp(page, "terrarium") == 0) TTGO::Usage::toggle();
  SimDisplay::init(g_screenW, g_screenH);
#elif defined(BOARD_IPS10)
  if(flag(argc,argv,"--portrait")){g_screenW=800;g_screenH=1280;}
  SimDisplay::init(g_screenW,g_screenH);
#else
  SimDisplay::init(SCREEN_W, SCREEN_H);
#endif
  treeCreate();
#if defined(BOARD_IPS10)
  if(flag(argc,argv,"--verify-interactions")) return verifyIpsInteractions(arg(argc,argv,"--outdir","sim-out")) ? 0 : 1;
#endif
#if defined(BOARD_T_EMBED) || defined(BOARD_T_DISPLAY_PRO)
  if (flag(argc, argv, "--verify-interactions")) return verifyCompanionInteractions(arg(argc, argv, "--outdir", "sim-out")) ? 0 : 1;
#endif

  if (flag(argc, argv, "--all")) {
    const char* outdir = arg(argc, argv, "--outdir", "sim-out");
    bool allOk = true;
    for (const char* s : SCENES) {
      std::string path = std::string(outdir) + "/" + label + "-" + s + ".png";
      allOk &= renderScene(s, path.c_str(), frames, "focus");
    }
    return allOk ? 0 : 1;
  }

  const char* scene = arg(argc, argv, "--scene", "working");
  std::string def = std::string("sim-out/") + label + "-" + scene;
#if defined(BOARD_T_DISPLAY_PRO)
  if (std::strcmp(page, "focus") != 0) def += std::string("-") + page;
#endif
  def += ".png";
  const char* out = arg(argc, argv, "--out", def.c_str());
  return renderScene(scene, out, frames, page) ? 0 : 1;
}
#endif
