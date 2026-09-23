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
bool verifyIpsInteractions(const char* outdir) {
  auto save=[&](const char* name) {
    const std::string path=std::string(outdir)+"/"+name+".png";
    return SimPng::writeRgb565(path.c_str(),SimDisplay::framebuffer(),SimDisplay::width(),SimDisplay::height());
  };
  auto advance=[] { SimDisplay::tick(300);treeUpdate(.3f);SimDisplay::refresh(); };
  auto click=[&](const char* text) {
    auto* l=ipsLabel(lv_screen_active(),text);
    if(!l) {std::fprintf(stderr,"missing control: %s\n",text);return false;}
    lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,nullptr);advance();return true;
  };
  const std::string overview=std::string(outdir)+"/ips10-overview.png";
  if(!renderScene("crowd",overview.c_str(),90,"focus")) return false;
  if(std::strcmp(IPS10Workspace::selectedSession(),"s4-AgentDeck")) return false;
  if(!ipsLabel(lv_screen_active(),"권한 요청:")) return false; // ring head is oldest
  if(!click("ips10 카드 개선")) return false;
  if(std::strcmp(IPS10Workspace::selectedSession(),"s2-AgentDeck")) return false;
  if(!ipsLabel(lv_screen_active(),"Fixed the treemap")) return false;
  if(ipsLabel(lv_screen_active(),"권한 요청:")) return false;
  if(!save("ips10-selected")) return false;
  // Reordering the daemon roster must not retarget detail or voice.
  std::swap(g_state.sessions[0],g_state.sessions[2]);advance();
  if(std::strcmp(IPS10Workspace::selectedSession(),"s2-AgentDeck")) return false;
  TimelineEntry entry{};
  std::snprintf(entry.raw,sizeof(entry.raw),"GLOBAL-UNATTRIBUTED");g_state.addTimelineEntry(entry);
  std::snprintf(entry.sessionId,sizeof(entry.sessionId),"s4-AgentDeck");
  std::snprintf(entry.raw,sizeof(entry.raw),"OTHER-SESSION-ONLY");g_state.addTimelineEntry(entry);
  // Exercise wrapped rings, newest-first ordering, and the eight-row bound.
  std::snprintf(entry.sessionId,sizeof(entry.sessionId),"s2-AgentDeck");
  for(int i=0;i<TIMELINE_MAX_ENTRIES+3;++i) {
    std::snprintf(entry.raw,sizeof(entry.raw),"SELECTED-EVENT-%03d",i);g_state.addTimelineEntry(entry);
  }
  advance();if(!click("기록 보기")) return false;
  char latest[40];std::snprintf(latest,sizeof(latest),"SELECTED-EVENT-%03d",TIMELINE_MAX_ENTRIES+2);
  if(!ipsLabel(lv_screen_active(),latest)) return false;
  if(ipsLabel(lv_screen_active(),"GLOBAL-UNATTRIBUTED") || ipsLabel(lv_screen_active(),"OTHER-SESSION-ONLY")) return false;
  if(!save("ips10-history")) return false;
  auto* logLabel=ipsLabel(lv_screen_active(),latest);
  auto* logPane=lv_obj_get_parent(lv_obj_get_parent(logLabel));
  lv_obj_scroll_to_y(logPane,500,LV_ANIM_OFF);
  std::snprintf(entry.sessionId,sizeof(entry.sessionId),"s1-AgentDeck");
  std::snprintf(entry.raw,sizeof(entry.raw),"NEW-SESSION-EVENT");
  g_state.addTimelineEntry(entry);g_state.addTimelineEntry(entry);advance();
  if(!click("TRMNL timeline"))return false;
  if(lv_obj_get_scroll_y(logPane)!=0)return false; // new task starts at newest event
  if(!click("ips10 카드 개선"))return false;
  g_state.sessions[0].alive=false;advance();
  if(!std::strcmp(IPS10Workspace::selectedSession(),"s2-AgentDeck")) return false;
  g_state.sessions[4].alive=false;advance();
  if(!click("확인 필요")) return false;
  if(IPS10Workspace::selectedSession()[0] || !ipsLabel(lv_screen_active(),"이 상태의 작업이 없습니다")) return false;
  if(!click("전체")) return false;
  if(!click("음성 / 스피커")) return false;
  auto* talk=ipsLabel(lv_screen_active(),"Hold to talk");if(!talk)return false;
  lv_area_t bounds;lv_obj_get_coords(lv_obj_get_parent(talk),&bounds);
  if(bounds.x1<0 || bounds.x2>=g_screenW || bounds.y1<0 || bounds.y2>=g_screenH)return false;
  if(!save("ips10-voice-controls") || !click("음성 / 스피커"))return false;
  g_state.wsConnected=false;g_simSerialConnected=false;advance();
  if(!ipsLabel(lv_screen_active(),"연결 끊김"))return false;
  if(!save("ips10-offline"))return false;
  g_state.sessionCount=0;advance();
  if(IPS10Workspace::selectedSession()[0] || !ipsLabel(lv_screen_active(),"에이전트가 연결되면"))return false;
  if(!save("ips10-empty"))return false;
  g_simSerialConnected=true;
  std::fprintf(stderr,"[sim] IPS10 selection, priority, ring wrap, attribution, removal, filters, drawer bounds, offline and empty: ok\n");
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
