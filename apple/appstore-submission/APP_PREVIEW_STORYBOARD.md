# App Preview capture record

Upload-ready App Previews are in `previews/<platform>/`. The videos use only actual AgentDeck UI and deterministic sample data.

## Delivered storyboards

| Platform | Time | Visual |
|---|---:|---|
| macOS | 0–2.5s | Cold open — a connected daemon with no sessions yet |
| macOS | 2.5–11s | Claude then Codex appear; the Codex weekly gauge lights up |
| macOS | 11–20s | OpenCode joins, Claude enters the amber attention state, then clears |
| macOS | 20–28s | All three complete and the creatures settle to the floor |
| iPhone | 0–8s | Three sample agents in processing/idle states; animated creatures and timeline |
| iPhone | 8–15.5s | Claude attention state with a display-only “respond in terminal” prompt |
| iPad | 0–8.8s | Full dashboard, topology, animated aquarium, timeline, and detail pane |
| iPad | 8.8–17.2s | Focused attention state while the remaining agents stay visible |

All three platforms record the live dashboard against the deterministic feed in `scripts/appstore-demo-orchestrator.mjs`, reached through the Debug-only `-AgentDeckScreenshotURL` argument; the mobile takes run on iOS Simulators. Neither the orchestrator nor `scripts/appstore-screenshot-mock.mjs` ships in a Release/App Store build. Every session name, project, task line and device row in the footage comes from that feed, so no real local session or hardware state is on screen — the earlier note here describing the macOS preview as a slideshow of three screenshots described an approach that was replaced and was left behind by the change.

## Capture rules

- Duration: 15–30 seconds.
- H.264 High Profile Level 4.0, progressive, 30 fps, 11 Mbps.
- Maximum file size: 500 MB.
- Accepted containers: `.mov`, `.m4v`, or `.mp4` for H.264.
- macOS preview must be landscape.
- Set a deliberate poster frame near 5 seconds.
- The delivered videos are silent and depend only on visible in-app labels; no narration is required.
- Use an anonymized demo workspace such as `agentdeck-demo`; never show a real project, terminal, token, IP address, home path, USB path, or notification containing private text.
- Record separate iPhone/iPad variants only if they add product value. Do not stretch or crop a macOS recording into portrait.

## Poster frames

- macOS: use a frame around 5 seconds so Device Preview is fully visible.
- iPhone: use a frame around 5 seconds with the three sample sessions visible.
- iPad: use a frame around 5 seconds with the full aquarium and timeline visible.

App Store Connect defaults to a poster frame near 5 seconds; verify it after processing rather than relying on the default blindly.
