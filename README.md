# Unikey-Wayland

Unikey-Wayland là bộ gõ tiếng Việt mã nguồn mở, gồm các backend riêng cho
từng môi trường:

- **Wayland client** cho Linux, được thiết kế trước hết cho KDE Plasma/KWin.
- **IBus engine** tùy chọn cho các phiên GNOME Wayland và X11.
- **Windows Edition** với bộ bắt phím mức thấp của Win32.

Phần xử lý tiếng Việt dùng [Bamboo Engine](wayland-client/src/vendor/github.com/BambooEngine/bamboo-core),
còn giao diện cấu hình dùng Qt 6. Linux biên dịch Bamboo thành C archive qua
CGO; Windows nạp `bamboo.dll` lúc chạy.

> README này mô tả đúng hành vi của mã nguồn hiện tại. Các ghi chú trong
> `CHANGELOG.md` có thể nói về hành vi của những phiên bản cũ.

## Hành vi quan trọng của bản Wayland hiện tại

Đây là phần cần biết trước khi cài:

1. Với ứng dụng văn bản thông thường, Wayland client dùng **direct commit**:
   nó tính phần thay đổi theo ranh giới UTF-8, gửi xóa/chèn qua surrounding
   text rồi commit trực tiếp. Nó **không gửi** các sự kiện
   `preedit_string`, `preedit_styling` hoặc `preedit_cursor`, vì vậy chữ đang
   gõ không bị gạch chân bởi bộ gõ.
2. Khi cửa sổ hiện tại là terminal, hoặc ứng dụng báo
   `content_purpose == terminal` (giá trị 12), Wayland client chuyển sang
   **raw passthrough**: cả phím nhấn, phím nhả và phím lặp được trả thẳng cho
   terminal (ngoại trừ các phím tắt của bộ gõ); Bamboo không xử lý. Vì vậy
   Telex/VNI/VIQR của backend Wayland
   **cố ý không hoạt động trong terminal**. Đây là lựa chọn an toàn để không
   làm hỏng dòng lệnh, autocomplete hoặc Backspace.
3. Danh sách terminal được nhận diện trong source gồm `kitty`, `alacritty`,
   `konsole`, `org.kde.konsole`, `gnome-terminal`, `org.gnome.terminal`,
   `org.gnome.ptyxis`, `ptyxis`, `org.gnome.console`, `kgx`,
   `xfce4-terminal`, `lxterminal`, `foot`, `footclient`, `wezterm` và
   `org.wezfurlong.wezterm`. Với terminal chưa nhận diện nhưng báo đúng
   content purpose, nhánh raw passthrough vẫn được dùng.
4. Chế độ tiếng Anh cũng luôn chuyển phím nguyên bản. Khi đổi cửa sổ hoặc
   KWin reset input context, composition, surrounding text, hàng đợi phím và
   trạng thái modifier được xóa để không dùng lại dữ liệu của cửa sổ trước.

Gạch chân còn nhìn thấy trong terminal có thể đến từ zsh-syntax-highlighting,
autocomplete, lựa chọn văn bản hoặc chính terminal; đó không phải preedit do
Wayland client này gửi. Bộ gõ không thể tắt các lớp hiển thị đó thay cho shell
và terminal.

## Backend và phạm vi hỗ trợ

| Backend | Môi trường | Cách tích hợp | Ghi chú |
| --- | --- | --- | --- |
| Wayland client | Linux Wayland, đặc biệt KDE Plasma/KWin | `zwp_input_method_v1` + Qt event loop | Compositor phải cung cấp protocol này và cho phép Virtual Keyboard. Không mặc định đảm bảo trên mọi compositor. |
| IBus engine | GNOME Wayland hoặc X11 khi IBus được cài | `ibus-engine-unikey-wayland` | Chỉ được tạo khi CMake tìm thấy `ibus-1.0`. Cơ chế preedit của IBus khác Wayland client. |
| Windows client | Windows x64/ARM64 khi có bộ Qt/toolchain tương ứng | Win32 `WH_KEYBOARD_LL` + `SendInput` | CI có job cho x64 và ARM64; artifact cụ thể phụ thuộc release. |

Thư mục `x11-client/` vẫn chứa một frontend X11 riêng lẻ, nhưng không được
đưa vào target cài đặt của `wayland-client/CMakeLists.txt` và script
`install.sh` cũng không build nó. Trên X11, đường cài đặt được hỗ trợ trong
source tree là IBus.

## Tính năng đang được nối vào backend Linux

- Kiểu gõ **Telex**, **Telex giản lược (Telex 2)**, **VNI** và **VIQR**.
- Gõ tự do (free marking), kiểu đặt dấu truyền thống `òa, úy` hoặc kiểu mới
  `oà, uý`.
- Kiểm tra chính tả và tự động khôi phục chuỗi phím gốc khi từ hoàn chỉnh
  không hợp lệ. Việc khôi phục chỉ được xét ở bước commit; nó không phải bộ
  sửa lỗi chính tả tự động.
- Gõ tắt (macro) được hỗ trợ trong luồng Bamboo của ứng dụng không bị
  raw-passthrough; bảng macro được lưu trong file cấu hình JSON. Terminal của
  Wayland client không chạy Bamboo nên cũng không mở rộng macro.
- Wrapper Bamboo có replay raw keys để giữ đúng trạng thái biến đổi (được
  dùng bởi IBus và regression tests). Wayland client chuyển Backspace cho
  client tự xử lý rồi xóa composition nội bộ; phần thay thế chữ vẫn tính diff
  theo byte UTF-8 nhưng chỉ xóa từ đầu ký tự. Vì vậy các trường hợp như `thê`
  → `thể` không bị cắt giữa một codepoint.
- Hàng đợi phím chờ callback surrounding text của client, kiểm tra lại
  snapshot trước khi sửa và có nhánh phục hồi giới hạn cho client áp dụng
  delete/commit lệch transaction.
- `Ctrl + Shift` (mặc định) hoặc `Alt + Z` để đổi E/V. `Ctrl + Shift + F5`
  mở bảng điều khiển; biểu tượng StatusNotifier của KDE hiển thị V/E và tự
  đăng ký lại khi tray hoặc Plasma khởi động lại.
- Khi đổi E/V, KDE OSD được gọi qua `org.kde.plasmashell`/
  `org.kde.osdService`.

### Những ô cấu hình không nên hiểu quá mức

- Chuỗi `charset` (Unicode, TCVN3, VNI Windows, VIQR) được lưu để giữ tương
  thích giao diện, nhưng đường Wayland/IBus hiện tại commit UTF-8 và không có
  bước chuyển đổi sang các bảng mã đó.
- Danh sách `preedit_apps.txt` được giữ để tương thích IBus và cấu hình cũ.
  Wayland client hiện tại không dùng danh sách này để bật preedit; danh sách
  rỗng không làm terminal nhận tiếng Việt vì terminal đã đi qua raw
  passthrough.
- Ô `Bật hội thoại này khi khởi động` được lưu trong cấu hình Wayland nhưng
  main loop Wayland hiện khởi động ẩn; mở giao diện bằng tray,
  `Ctrl + Shift + F5`, `unikey-wayland --setup` hoặc `--exclude`. Windows
  Edition mới dùng giá trị này để tự mở cửa sổ.
- Một số ô giao diện được giữ lại cho tương thích nhưng chưa có đường xử lý
  tương ứng trong backend hiện tại (ví dụ lựa chọn restore hotkey riêng và
  nút mặc định của Wayland). Không nên coi chúng là tính năng đã được đảm
  bảo chỉ vì chúng xuất hiện trên UI.

## IBus có khác Wayland client không?

IBus là target tùy chọn trong CMake. Khi được build và người dùng chọn engine
`unikey-wayland` trong IBus:

- Chế độ bình thường dùng surrounding text và direct diffing.
- IBus vẫn có nhánh preedit để tương thích ứng dụng cần nó. Nó đọc
  `~/UnikeyWayland/preedit_apps.txt` khi file tồn tại; nếu file không tồn tại,
  source IBus có danh sách mặc định cũ. Ngoài ra, `IBUS_INPUT_PURPOSE_TERMINAL`
  vẫn ép nhánh preedit bất kể danh sách ứng dụng.
- Vì vậy câu “không preedit” ở phần trên chỉ áp dụng cho **Wayland client**,
  không được suy diễn thành cam kết giống hệt cho IBus.

`install.sh` tạo file `preedit_apps.txt` rỗng nếu file chưa có và không ghi đè
file người dùng đã tạo. Điều này làm mặc định danh sách ứng dụng rỗng cho bản
cài mới, nhưng không thay đổi nhánh terminal-purpose của IBus.

## Cài đặt trên Linux

### Cách nhanh cho KDE Plasma Wayland

`install.sh` là script cài **Wayland client**, không phải bộ cài cho IBus hay
Windows. Chạy từ thư mục gốc repository:

~~~bash
chmod +x install.sh
./install.sh
~~~

Script kiểm tra Go, GCC/G++, `pkg-config`, `wayland-scanner` và Qt 6 `moc`,
sau đó:

1. build `libbamboo.a` từ mã Go vendored;
2. sinh mã protocol Wayland và Qt MOC;
3. cài binary vào `~/.local/bin/unikey-wayland`, desktop entry và SVG icon vào
   `~/.local/share`;
4. tạo `~/UnikeyWayland/preedit_apps.txt` rỗng nếu chưa tồn tại;
5. nếu có `kwriteconfig6`, ghi desktop entry vào các khóa KWin Virtual
   Keyboard và yêu cầu KWin reconfigure;
6. dừng process `unikey-wayland` cũ, yêu cầu KWin reconfigure và bật lại
   Virtual Keyboard; việc KWin tạo process mới vẫn phụ thuộc session hiện tại.

Script không thể tự chứng minh compositor đã chấp nhận input method. Sau khi
chạy, trên KDE hãy vào **System Settings → Keyboard → Virtual Keyboard** và
chọn desktop entry `Unikey-Wayland` nếu KWin chưa chọn sẵn. Bộ gõ sẽ chạy nền;
mở bảng điều khiển bằng biểu tượng V/E hoặc `Ctrl + Shift + F5`.

### Build và cài bằng CMake

Yêu cầu tối thiểu của target Wayland:

- CMake 3.16 trở lên và compiler C/C++ hỗ trợ C++17;
- Go theo `wayland-client/src/go.mod` (hiện khai báo Go 1.22.0);
- `pkg-config`, `wayland-client`, `wayland-scanner`;
- Qt 6 Core, Gui, Widgets và DBus;
- XML input-method protocol đã được vendored trong repository; gói
  `wayland-protocols` là tùy chọn theo distro.

IBus là tùy chọn; cài thêm development package của IBus nếu muốn CMake tạo
`ibus-engine-unikey-wayland`.

Ví dụ tên gói phổ biến (tên có thể khác theo distro):

~~~bash
# Debian/Ubuntu
sudo apt install cmake build-essential pkg-config golang-go \
  qt6-base-dev qt6-wayland libwayland-dev libwayland-bin wayland-protocols
# Tùy chọn IBus
sudo apt install libibus-1.0-dev

# Fedora
sudo dnf install cmake gcc-c++ pkgconfig golang \
  qt6-qtbase-devel wayland-devel wayland-protocols-devel
# Tùy chọn IBus
sudo dnf install ibus-devel

# Arch Linux
sudo pacman -S cmake gcc pkgconf go qt6-base wayland wayland-protocols
# Tùy chọn IBus
sudo pacman -S ibus
~~~

Build và chạy test:

~~~bash
cmake -S wayland-client -B build \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure

# Regression test trực tiếp cho wrapper Bamboo
(cd wayland-client/src && go test -race -mod=vendor bamboo_wrapper.go bamboo_wrapper_test.go)
~~~

Cài target CMake (mặc định vào prefix của CMake, thường là `/usr/local`):

~~~bash
sudo cmake --install build
~~~

Muốn cài không cần quyền root, chọn prefix khi configure, ví dụ
`-DCMAKE_INSTALL_PREFIX="$HOME/.local"`. CMake chỉ cài các target và asset
được build; nó không tự chọn Virtual Keyboard trong KWin.

### Gỡ lỗi Wayland

Nếu compositor không cung cấp `zwp_input_method_v1`, binary in thông báo
“Running in GUI-only mode” và chỉ còn giao diện cấu hình. Kiểm tra nhanh:

~~~bash
echo "$XDG_SESSION_TYPE"
echo "$WAYLAND_DISPLAY"
pgrep -af unikey-wayland
~~~

Bật log phím/protocol có chủ đích bằng:

~~~bash
UNIKEY_WAYLAND_DEBUG=1 unikey-wayland
~~~

Wayland client ghi log debug chính vào `/tmp/uk_debug.log`; WindowTracker còn
ghi sự kiện cửa sổ vào `/tmp/tracker.log`. Các log này có thể chứa tên ứng
dụng/cửa sổ, nên chỉ gửi khi đã xem lại nội dung.

## Windows Edition

Windows Edition nằm ở `windows-client/` và build bằng Qt 6 + MSVC. Runtime
dùng Win32 low-level keyboard hook và `SendInput`; `bamboo.dll` phải nằm cạnh
file thực thi. Bundle CI chứa `UnikeyWayland.exe`, `bamboo.dll`, Qt runtime,
`setup.bat`, `7z.exe` và `7z.dll`.

`setup.bat` yêu cầu quyền Administrator, giải nén vào
`C:\Program Files\UnikeyWayland`, tạo shortcut Desktop/Start Menu rồi khởi
động chương trình. Nó **không tự bật khởi động cùng Windows**; tùy chọn đó
được ghi vào `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` khi
người dùng bật trong giao diện. Ô “Bật hội thoại này khi khởi động” được
Windows client đọc lúc process bắt đầu.

Source Windows hiện vẫn hiển thị các combo kiểu gõ/bảng mã và lưu chúng vào
JSON, nhưng `applySettings()` của Windows client chưa truyền các giá trị đó
vào `bamboo.dll`. Không nên quảng bá rằng đổi các combo này đã thay đổi engine
Windows trong bản source này.

Windows client tự kiểm tra GitHub Releases sau khoảng ba giây và có nút kiểm
tra thủ công. Việc xử lý phím vẫn cục bộ, nhưng vì có luồng kiểm tra/tải OTA
này nên không được mô tả là “100% offline”.

## Cấu hình và quyền riêng tư

### Linux

- `~/UnikeyWayland/global.json`: kiểu gõ, các cờ Bamboo, macro, E/V và tùy
  chọn giao diện.
- `~/UnikeyWayland/preedit_apps.txt`: danh sách legacy cho IBus; để trống nếu
  không muốn chọn ứng dụng theo tên. Wayland client không dùng nó để bật
  preedit.

### Windows

- `QStandardPaths::AppDataLocation/Unikey/global.json` (thường nằm trong
  `%LOCALAPPDATA%`): cài đặt giao diện, macro và E/V.
- Registry `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`: chỉ
  được dùng khi bật “Khởi động cùng Windows”.

Linux engine không có code gửi nội dung phím lên mạng. Windows dùng mạng cho
GitHub Releases như mô tả ở trên; nó không gửi văn bản đang gõ trong luồng cập
nhật đó.

## Đóng gói và kiến trúc

- `package_arch.sh` tạo gói Arch `.pkg.tar.zst` từ binary đã build và có nhánh
  nhận diện `x86_64`/`aarch64`.
- `debian/` và spec RPM phục vụ pipeline Linux.
- Workflow GitHub Actions hiện có job Linux x86_64/aarch64 và Windows x64/
  ARM64, nhưng việc một release cụ thể có đủ artifact hay không phụ thuộc
  lần chạy pipeline đó.
- `PKGBUILD` trong repository hiện khai báo `x86_64`; đừng suy ra từ script
  đóng gói rằng mọi distro/kiến trúc đều đã có gói nhị phân được kiểm thử.

## Kiểm thử

CTest hiện đăng ký:

- `text-transaction-test`: diff UTF-8, selection/autocomplete, transaction
  delete/commit và Backspace forwarded;
- `bamboo-wrapper-test`: các kiểu Telex/VNI/VIQR/Telex 2, spell-check,
  auto-restore và replay Backspace.

Các test trên không thay thế kiểm thử trong compositor thật. Khi báo lỗi, hãy
ghi rõ session (KDE/GNOME, Wayland/X11), compositor, ứng dụng, kiểu gõ, chuỗi
phím và nội dung log tối thiểu để có thể tái hiện.

## FAQ ngắn

### Vì sao terminal không gõ được VNI/Telex?

Đó là hành vi có chủ đích của Wayland client hiện tại: terminal nhận raw key
để giữ autocomplete, phím lặp và Backspace nguyên vẹn. Muốn thử cơ chế IBus,
phải build IBus target và chọn engine IBus; IBus có semantics preedit riêng và
không được xem là cùng backend.

### Vì sao vẫn thấy gạch chân?

Wayland client không gửi preedit. Hãy phân biệt gạch chân của zsh
syntax-highlighting, autocomplete, spellchecker, selection hoặc terminal với
gạch chân preedit. Kiểm tra thêm `UNIKEY_WAYLAND_DEBUG` và app đang focus.

### Có phải đây là UniKey chính thức không?

Không. Đây là dự án độc lập, không phải bản phát hành chính thức của UniKey.
Tên dự án được giữ theo nguồn gốc Wayland; lõi hiện tại là Bamboo Engine.

### Có thể bật cùng lúc hai bộ gõ không?

Không nên. Chỉ bật một input method tại một thời điểm để tránh hai backend cùng
nhận một phím.

## Bản quyền

Mã nguồn project ở root phát hành theo GNU GPL v3, xem [LICENSE](LICENSE).
Bamboo Engine được vendored tại `wayland-client/src/vendor` và có giấy phép
riêng trong [LICENSE của Bamboo](wayland-client/src/vendor/github.com/BambooEngine/bamboo-core/LICENSE).
Các thành phần bên thứ ba khác vẫn tuân theo license/header tương ứng.

## Liên kết

- [Changelog](CHANGELOG.md)
- [FAQ đầy đủ](FAQ.md)
- [Issues của dự án upstream](https://github.com/ubuntu2310fake/Unikey-Wayland/issues)
- [Releases upstream](https://github.com/ubuntu2310fake/Unikey-Wayland/releases)
