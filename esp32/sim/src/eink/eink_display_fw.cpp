// Unity-include wrapper for the real TRMNL 7.5" e-ink render tree (direct-draw
// GxEPD2, no LVGL). Self-gated on BOARD_TRMNL_75. See fw/renderer.cpp for the
// per-env compilation rationale.
//
// The SimEink render entry point lives HERE (after the include) rather than in a
// separate file because the firmware's `display` object sits in eink_display.cpp's
// anonymous namespace — only code in this same translation unit can read the host
// framebuffer the GxEPD2_BW shim accumulated.
#include "../../../src/ui/eink/eink_display.cpp"

#ifdef BOARD_TRMNL_75
#include "../sim.h"
#include <Arduino.h>
#include <cstdlib>

// Panel bus instance (declared extern in the SPI shim). No real transfer on host.
SimSPIClass SPI;

bool SimEink::renderToPng(const char* scene, const char* path) {
  const bool simulateDecision = std::strcmp(scene, "decision") == 0;
  if (!SimScenes::apply(simulateDecision ? "permission" : scene)) return false;
  Eink::init();
#if defined(BOARD_SIM_PULL)
  if (simulateDecision) {
    // Pixel-exact post-primary-action state: the real pull boards open an
    // eight-minute interactive lease before DECISION becomes eligible.
    interactiveLeaseUntilMs = g_sim_millis + FACE_HOLD_MS;
    faceHoldUntilMs = 0;
    suppressedDecisionHash = 0;
  }
#endif
  // render() is content-hash + min-refresh-interval gated. In an --all run these
  // statics persist across scenes with millis() otherwise frozen, so advance the
  // virtual clock past the coalesce window and render twice to force a fresh draw.
  g_sim_millis += 3600;
  Eink::render();
  g_sim_millis += 3600;
  Eink::render();

  const uint8_t* buf = display.hostBuffer();   // anon-namespace global, visible here
  const int W = display.hostWidth(), H = display.hostHeight();
  if (!buf) return false;

  // Host ink codes (0..15 grayscale level, 16 = red) → RGB565 for the shared
  // PNG writer. Red appears only on the tri-color NM face and intermediate
  // levels only on the EPD47; the other panels emit 0/15 alone, so this is a
  // no-op for them.
  uint16_t* img = static_cast<uint16_t*>(std::malloc((size_t)W * H * sizeof(uint16_t)));
  if (!img) return false;
  for (int i = 0; i < W * H; i++) {
    if (buf[i] == 16) { img[i] = 0xF800; continue; }
    const uint8_t g = (uint8_t)(buf[i] * 17);         // 0..15 → 0..255
    img[i] = (uint16_t)(((g & 0xF8) << 8) | ((g & 0xFC) << 3) | (g >> 3));
  }
  bool ok = SimPng::writeRgb565(path, img, W, H);
  std::free(img);
  return ok;
}
#endif  // BOARD_TRMNL_75
