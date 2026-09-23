# IPS10 tactile keycap

`keycap.py` is the authored Blender 5.2 source. It creates a separate studio scene, reads material colors from `design/tokens.css`, renders `keycap.png` at 128×96 with transparency, then restores the previously active scene. It does not modify agent/brand geometry or bake text into the image.

`encode.py` converts that PNG to a shared, uncompressed RGB565+A8 image in `esp32/src/ui/widgets/ips10_keycap_generated.h`. The 36 KiB pixel payload stays in flash; LVGL overlays the existing canonical creature and live state. The touch target is 108×108 and shifts three pixels while pressed. All ten seats reuse the same image.

Regenerate the image using Blender's Python execution with this script's `__file__` set, then run `python3 design/ips10/encode.py` from the repository root. `python3 design/ips10/encode.py --check` verifies PNG-to-consumer drift. Render the native IPS10 interaction suite to review the complete UI rather than the isolated asset alone.
