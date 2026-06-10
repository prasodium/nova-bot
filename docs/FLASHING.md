# Flashing the firmware with esptool-js

There are **two ESP32 boards**, they are **different chips**, and each gets its own
binary:

| Board | Chip | Binary the CI produces |
|-------|------|------------------------|
| ESP32 DevKit v1 (main controller) | ESP32 | `main_controller-esp32-merged.bin` |
| XIAO ESP32-S3 Sense (vision node) | ESP32-S3 | `vision_node-esp32s3-merged.bin` |

Both are **merged** images — flash each at **address `0x0`** and you're done (no
need to juggle bootloader/partition offsets).

---

## Step 1 — Get the .bin files (build them on GitHub, free)

You can't compile these in a browser, and they must be built against the exact
board/chip. The repo includes a GitHub Actions workflow that builds them for you:

1. Edit your settings:
   - `firmware/main_controller/include/config.h` → `WIFI_SSID`, `WIFI_PASS`, `BACKEND_HOST`
   - `firmware/vision_node/include/config.h` → `WIFI_SSID`, `WIFI_PASS`
   *(Or skip editing and instead set repo **Secrets** `WIFI_SSID`, `WIFI_PASS`,
   `BACKEND_HOST` — see note below — so credentials never get committed.)*
2. Commit and push to GitHub.
3. Open the **Actions** tab → the "Build firmware" run → download the
   **`firmware-bin`** artifact. It contains both `.bin` files.
   *(Or push a tag like `git tag v1.0 && git push --tags` to also get a Release
   with the binaries attached.)*

> 🔒 **Don't commit your Wi-Fi password to a public repo.** Either keep the repo
> private, or use the Secrets path: Settings → Secrets and variables → Actions →
> add `WIFI_SSID`, `WIFI_PASS`, `BACKEND_HOST`. The workflow injects them at build
> time and the defaults in `config.h` stay as placeholders.

### Prefer to build locally?
If you have [PlatformIO](https://platformio.org/) installed:
```bash
# esptool is pinned to 4.x — v5 renamed merge_bin to merge-bin
pip install platformio "esptool~=4.8"

pio run -d firmware/main_controller
pio run -d firmware/vision_node

BOOTAPP=$(find ~/.platformio/packages -name boot_app0.bin | head -1)

# ESP32 main controller (bootloader at 0x1000)
MC=firmware/main_controller/.pio/build/esp32dev
python -m esptool --chip esp32 merge_bin -o main_controller-esp32-merged.bin \
  --flash_mode keep --flash_freq keep --flash_size 4MB \
  0x1000 "$MC/bootloader.bin" 0x8000 "$MC/partitions.bin" \
  0xe000 "$BOOTAPP" 0x10000 "$MC/firmware.bin"

# ESP32-S3 vision node (bootloader at 0x0)
VN=firmware/vision_node/.pio/build/seeed_xiao_esp32s3
python -m esptool --chip esp32s3 merge_bin -o vision_node-esp32s3-merged.bin \
  --flash_mode keep --flash_freq keep --flash_size 8MB \
  0x0 "$VN/bootloader.bin" 0x8000 "$VN/partitions.bin" \
  0xe000 "$BOOTAPP" 0x10000 "$VN/firmware.bin"
```

---

## Step 2 — Flash with esptool-js

Do this **once per board** (plug in one board at a time).

1. Use a USB **data** cable (not charge-only) and Chrome/Edge (Web Serial).
2. Go to <https://espressif.github.io/esptool-js/>.
3. Set **Baudrate** to `921600` (or `115200` if it's flaky) → click **Connect**
   and pick the serial port. On some boards hold **BOOT** while connecting.
4. In the **Program** section, add **one** file:
   - **File**: the merged `.bin` for the board you have plugged in.
   - **Flash Address**: `0x0`
5. (Optional but recommended for a clean state) click **Erase Flash** first.
6. Click **Program**. Wait for "Hash of data verified" / completion.
7. Press the board's **EN/RST** button. Open the esptool-js **console** (or any
   115200 serial monitor) to watch it boot, join Wi-Fi, and print its IP.

Repeat for the second board with its own `.bin`.

---

## Which file goes on which board?
- `main_controller-esp32-merged.bin` → the **ESP32 DevKit v1** (the one wired to
  motors, IMU, audio, IR sensor).
- `vision_node-esp32s3-merged.bin` → the **XIAO ESP32-S3 Sense** (the camera).

Flashing the wrong file to the wrong chip won't boot — they're different CPUs.

## Troubleshooting
| Symptom | Fix |
|---------|-----|
| No port appears | Use a data cable; install CP210x/CH340 USB-serial driver; try another port |
| "Failed to connect" | Hold **BOOT**, tap **EN/RST**, release **EN**, then Connect; lower baud to 115200 |
| Flashes but reboots in a loop | Wrong chip's binary, or wrong flash size; rebuild for the correct board |
| Boots but no Wi-Fi | You flashed before setting SSID/PASS — rebuild with correct credentials |
