# IPS10 project studio assets

`room.py` is the authored Blender 5.2 source for the shared project workbench. It creates a separate scene, reads material colors from `design/tokens.css`, renders `room.png` at 448×120 with transparency, then restores the previous scene. It does not change the original scene or bake agent geometry, state or text into the image.

`encode_room.py` converts the PNG to a shared, uncompressed RGB565+A8 image in `esp32/src/ui/widgets/ips10_room_generated.h`. Its 157.5 KiB pixel payload remains in flash. All ten project containers reuse it; live canonical creatures, activity, attention and recent messages are independent LVGL objects. No runtime canvas or decompression is needed.

Execute the Blender source with its `__file__` set, then run `python3 design/ips10/encode_room.py`. Add `--check` to verify PNG-to-consumer drift. Review the complete UI with the native IPS10 interaction suite in landscape and portrait.

The earlier `keycap.py`, `keycap.png`, `encode.py` and generated keycap header are retained as the previous tactile-deck asset; the ambient studio does not compile that image into firmware.
