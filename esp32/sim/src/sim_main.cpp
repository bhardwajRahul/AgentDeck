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
  SimDisplay::init(SCREEN_W, SCREEN_H);
  treeCreate();

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
