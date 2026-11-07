# AGENTS.md

## Project

XiaoZhi is an ESP-IDF C/C++ voice-assistant firmware supporting many chips, boards, displays, audio devices, and network transports. A build selects exactly one board implementation.

Use the ESP-IDF `release/v6.1` branch when possible. The minimum supported SDK is ESP-IDF v6.0.1. IDF 5.x is not supported.

## Architecture

- `main/application.*`: main event loop, protocol lifecycle, and high-level behavior.
- `main/device_state_machine.*`: legal runtime state transitions.
- `main/boards/common/`: board interfaces and reusable hardware/network helpers.
- `main/boards/**/`: board-specific pins, initialization, and build variants.
- `main/audio/`: codecs, audio tasks, engines, wake words, and queues.
- `main/protocols/`: transport-neutral API plus WebSocket and MQTT/UDP.
- `main/display/` and `main/led/`: reusable UI implementations.
- `main/mcp_server.*`: common device-side MCP tools and dispatch.
- `main/Kconfig.projbuild`: board and feature configuration.
- `main/CMakeLists.txt`: source, board, locale, font, and asset selection.
- `scripts/build.py`: canonical board/variant build entry point.

Read the closest existing implementation before adding a new one. Prefer the narrowest owning layer; do not put board-specific behavior into core modules.

## Required Rules

- Preserve unrelated worktree changes and keep patches focused.
- A build must export exactly one board factory through `DECLARE_BOARD(...)`.
- Never alter an existing board's pins to support different hardware. Add a uniquely named board or release variant; board identity affects OTA compatibility.
- Core code depends on `Board` interfaces, never a concrete board class or board `config.h`.
- Treat camera, backlight, display, LED, battery, and similar capabilities as optional.
- Change runtime state through `Application::SetDeviceState()` and the state machine.
- Callbacks may run outside the main task. Schedule application mutations with `Application::Schedule()` or event bits.
- Do not block the main event loop or audio tasks. Avoid unbounded queues and repeated large allocations in audio paths.
- Keep shared message semantics in `Protocol`; verify both transports when changing its contract.
- Validate network input and preserve `cJSON` ownership. NVS keys are persistent API and require migration when changed.
- Guard target-specific features with Kconfig/component rules. Do not assume every target has PSRAM or S3/P4 resources.
- Do not manually edit generated/vendor output: `build/`, `releases/`, `managed_components/`, `components/`, `sdkconfig*`, `main/assets/lang_config.h`, or generated mmap headers.
- Format only touched C/C++ files with the repository `.clang-format`; avoid unrelated mass formatting.

## Boards and Configuration

Board selection is a coupled chain:

`config.json` -> `scripts/build.py` -> `main/Kconfig.projbuild` -> `main/CMakeLists.txt` -> board source and `config.h`.

When adding a board or variant, update every relevant link in that chain. Include a unique board identity, correct chip target, flash/partition settings, exactly one `DECLARE_BOARD`, and board documentation. Follow `docs/custom-board.md`.

## Commands

Source the intended ESP-IDF environment first:

```sh
source /path/to/esp-idf/export.sh
idf.py --version
```

```sh
# Discover exact board and variant names
python3 scripts/build.py --list-boards

# Canonical variant build
python3 scripts/build.py <board-directory> --name <variant-name>

# Host-side build tests
python3 -m unittest discover -s scripts/tests -v

# Format/check touched files
clang-format -i <files>
clang-format --dry-run -Werror <files>
```

The build script changes local `sdkconfig` and build state. Do not assume the build directory still represents a previous target.

## Validation

- Board-only change: build affected variants and smoke-test changed hardware.
- Core, common-board, audio, protocol, display, dependency, Kconfig, or CMake change: run host tests and build representative affected chip/network paths.
- Protocol changes: verify WebSocket and MQTT/UDP when shared behavior changes.
- Audio changes: verify capture, playback, wake/VAD, interruption, reconnect, and applicable AEC modes.
- UI/assets changes: verify applicable no-display/OLED/LVGL paths and partition size.
- Always report what was tested and what still needs physical hardware. A successful build is not hardware validation.

## Authoritative Documentation

- Overview and SDK policy: `README.md`
- Board guide: `docs/custom-board.md`
- Audio design: `main/audio/README.md`
- Code style: `docs/code_style.md`
- Protocols: `docs/websocket.md`, `docs/mqtt-udp.md`, `docs/mcp-protocol.md`
- CI matrix: `.github/workflows/build.yml`

Keep detailed or fast-changing information in those files, not here. Add a nested `AGENTS.md` only when a subsystem needs specialized instructions.


## Workspace Customizations (Active Project History)

### Hardware Target
- **Board**: `freenove-esp32s3-display-2.8-lcd` (ESP32-S3 with 8MB PSRAM, 16MB Flash).
- **Display**: 2.8-inch LCD (ILI9341, 320x240 landscape orientation).
- **SD Card**: SDMMC 4-bit bus mode on GPIO pins 38 (CLK), 40 (CMD), 39 (D0), 41 (D1), 48 (D2), 47 (D3).
- **Primary Upload Port**: COM30.

### Implemented Features & Custom Architecture
1. **Dual Wallpaper System**:
   - `boot_landscape.bin` (in `main/assets/`): Displays during boot and WiFi connection ("XIAOZHI_DZ AI SMART ASSISTANT" robot banner).
   - `wallpaper_landscape.bin` (in `main/assets/`): Displays when WiFi is connected and system enters `kDeviceStateIdle`.
   - Handled via `main/display/custom_wallpaper.h`, `custom_wallpaper.cc`, and `main/display/lcd_display.cc`.
2. **SD Card Auto-Fit Wallpaper**:
   - `main/boards/freenove-esp32s3-display-2.8-lcd/freenove-esp32s3-display-2.8-lcd.cc` mounts `/sdcard` and reads JPEG/PNG wallpapers.
   - Images are automatically zoomed and centered using LVGL scaling to fit 320x240 perfectly.
3. **Top-Left Solar & Vietnamese Lunar Calendar**:
   - `main/display/vietnamese_lunar_calendar.h`: Pure C++ astronomical algorithm computing exact Vietnamese Lunar calendar dates (taking timezone +7:00 into account).
   - Displayed at top-left 1/4 area using font `font_noto_sans_basic_20_4` in white with frosted dark pill background: `DD/MM/YYYY\nÂm lịch: DD/MM`.
4. **Center Big Digital Clock**:
   - Large digital clock at `LV_ALIGN_CENTER, 0, 10` using Montserrat 40px with real-time seconds (`HH:MM:SS`), updated every second in `LcdDisplay::UpdateStatusBar`.
5. **Giao diện màn hình chờ nâng cao (Standby UI Redesign - Ngang 320x240 & Dọc 240x320)**:
   - Nền màu tối slate (#0f172a) hiện đại, tự động hiển thị khi vào chế độ chờ (idle) và ẩn đi khi đang trò chuyện/lắng nghe.
   - **Toàn bộ chữ trên màn hình chờ đều để màu TRẮNG tinh khôi (`lv_color_white()`)**.
   - **Góc trên bên trái**:
     - Đồng hồ số lớn font Montserrat 40 (`HH:MM`) và **số đếm giây nhỏ hơn** (`:SS`, font 20px trắng) được đẩy ra xa hơn tự động bám sát chữ số phút với khoảng cách đúng 3px (cw + 3), hoàn toàn cách biệt và không bị đè dấu `:`.
     - **Thêm hàng Thứ theo thời gian thực**: `Thứ Tư` (hoặc `Thứ Hai`, `Thứ Ba`...) nằm ngay phía trên hàng lịch dương.
     - Dương lịch: Chuẩn định dạng `dd/mm/yyyy` (ví dụ: `04/11/2026`).
     - Âm lịch: `Âm lịch: 24 / 9` hoặc `Âm lịch: Mùng X / Y` (đã bỏ chữ "Ngày").
     - Đã bỏ dòng Năm 2026 cho thoáng màn hình.
     - Bố cục màn hình dọc (Portrait) đối xứng: Nửa trên (Trái: Thứ, Dương lịch, Âm lịch rải đều; Phải: Đồng hồ số ở trên, số giây đếm :SS nằm dưới phút và được căn sát lề phải thẳng hàng với phút). Nửa giữa (Trái: Icon mây ở trên, Nhiệt độ lớn ở dưới căn giữa; Phải: Thành phố, thời tiết, độ ẩm, gió rải đều). Nửa dưới: 4 thẻ dự báo.
   - **Góc trên bên phải**:
     - **Biểu tượng thời tiết và nhiệt độ lớn (`25°C`) nằm NGANG HÀNG nhau**.
     - Biểu tượng thời tiết động thông minh: trời nắng/mây sẽ có icon mặt trời hoặc mặt trời ló sau mây (`sun_cloud`), trời mưa có giọt mưa (`rain`), sấm sét (`thunder`).
     - **Tên địa phương (ví dụ: `TP. Hồ Chí Minh`) được XUỐNG DÒNG riêng biệt** ngay bên dưới cụm icon và nhiệt độ.
     - Dưới cùng là trạng thái thời tiết (`Trời có mây`), `Độ ẩm: XX%` (đã sửa triệt để lỗi mất dấu thành "Độ m" bằng glyph substitution) và `Gió: X km/h`.
   - **Phía dưới**:
     - 4 thẻ card bo góc hiện đại hiển thị dự báo thời tiết 4 ngày kế tiếp (`T4 04`, `T5 05`, `T6 06`, `T7 07`).
     - Mỗi thẻ gồm: Thứ & ngày, icon thời tiết 24x24 px, nhiệt độ cao nhất trong ngày (`25°C`).
   - Tự động lấy dữ liệu thời gian thực từ Open-Meteo API. Định vị vị trí động theo mạng WiFi kết nối: Sử dụng ưu tiên ipwho.is (độ chính xác cao cho các dải IP mạng Việt Nam) và dự phòng ip-api.com. Khi chuyển sang WiFi mới, hệ thống tự động cập nhật lại vị trí và thời tiết của nơi đó.
      - Hỗ trợ công cụ MCP self.weather.set_location và self.weather.refresh cho phép AI hoặc người dùng đổi vị trí thời tiết linh hoạt khi cần.
      - Phân biệt thời tiết ngày/đêm thông qua tham số is_day:
        + Ban ngày: Hiển thị icon Mặt Trời / Mây Mặt Trời (weather_icon_sun, weather_icon_sun_cloud), mô tả 'Trời nắng', 'Nắng nhẹ'.
        + Ban đêm (is_day == 0): Hiển thị icon Mặt Trăng khuyết / Mây Mặt Trăng (weather_icon_moon, weather_icon_moon_cloud), mô tả chuẩn xác 'Trời quang', 'Ít mây' thay vì hiển thị nắng.
6. **Touch Gestures & Voice Interaction Timing**:
   - **Chế độ Chatbot**: Chỉ kích hoạt khi người dùng nhấn vào icon "Trợ lý AI" trong Menu ứng dụng hoặc nút "Trợ lý ảo" trong Cài đặt nhanh (đã vô hiệu hóa kích hoạt bằng 2 lần chạm).
   - **Single-tap & Double-tap**: Đã được vô hiệu hóa để tránh kích hoạt nhầm khi thao tác vuốt.
   - **Triple-tap (Chạm 3 lần)**: Đổi chiều xoay màn hình (ngang / dọc).
   - **4 taps (Chạm 4 lần)**: Đổi hình nền tiếp theo từ thẻ nhớ SD.
   - **VAD Silence Timeout**: Thời gian dừng nói để AI phản hồi được cấu hình là 2 giây (`2,000,000 us`).
7. **Hệ thống Cử chỉ vuốt, Màn hình 14 Icon (2 trang) & Cài đặt nhanh (Quick Settings)**:
   - **Đồng bộ cảm ứng Ngang (320x240) và Dọc (240x320)**: Tần số lấy mẫu 20ms, thuật toán nhận diện vuốt delta trực quan độc lập góc xoay, giải mã nghịch đảo ma trận toạ độ của LVGL 9 khi xoay 90 độ, đảm bảo chạm và vuốt mượt mà 100%.
   - **Kịch bản vuốt linh hoạt**:
     - Tại màn hình Chatbot (đang trò chuyện / lắng nghe / nói): Vuốt từ dưới lên -> Thoát ngay chế độ chatbot, ngắt âm thanh và trở về màn hình chờ ban đầu.
     - Tại màn hình chờ (Standby): Vuốt dưới lên -> Mở màn hình Menu icon. Vuốt trên xuống -> Mở Cài đặt nhanh (Quick Settings).
     - Tại màn hình Menu icon: Vuốt sang trái -> Sang Trang 2 (5 icon). Vuốt sang phải -> Quay lại Trang 1 (9 icon). Vuốt dưới lên -> Quay về màn hình chờ ban đầu. Vuốt trên xuống -> Mở Cài đặt nhanh.
     - Tại màn hình Cài đặt nhanh: Vuốt dưới lên (hoặc nút đóng X) -> Quay về màn hình chờ.
   - **Giao diện Menu 14 Icon phong cách iPhone**:
     - Chia 2 trang (Trang 1: 9 icon, Trang 2: 5 icon) kèm 2 chấm tròn chỉ báo trang đang xem ở cạnh dưới.
     - Tỷ lệ icon squircle cân đối, chữ tiếng Việt có dấu đầy đủ (`font_noto_sans_custom_14_1`), không che lấp chữ.
   - **Màn hình Cài đặt nhanh (Quick Settings)**:
     - Thanh kéo độ sáng màn hình (Backlight 5-100%).
     - Thanh kéo âm lượng (Volume 0-100%).
     - Bộ chọn 5 màu chữ màn hình (Trắng, Vàng, Xanh dương, Xanh lá, Cam) lưu trực tiếp vào NVS.
     - Nút hành động nhanh: Trợ lý ảo (bắt đầu hội thoại AI), Xoay màn hình (chuyển ngang/dọc), Khởi động lại thiết bị.
