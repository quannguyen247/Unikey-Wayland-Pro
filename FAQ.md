# Toàn tập Câu hỏi thường gặp (FAQ) & Chuyện phím

Dưới đây là tập hợp toàn bộ những thắc mắc từ nghiêm túc đến "tấu hài" về Unikey-Wayland trên tất cả các nền tảng.

## Nguồn gốc & Tên gọi

**Vì Unikey-Wayland không được tạo ra ngay từ đầu như một dự án bộ gõ đa nền tảng.**
Dự án sinh ra trên Linux Wayland, sau đó kiến trúc được mở rộng dần để hỗ trợ các môi trường khác. Một cái tên chung chung có thể làm mất dấu nguồn gốc của dự án khi một nền tảng có số lượng người dùng lớn hơn chiếm phần lớn sự chú ý. Vì vậy, tên Unikey-Wayland được giữ nguyên trên mọi nền tảng.

**Windows Edition có phải là phiên bản chính của Unikey-Wayland không?**
Không. Unikey-Wayland là tên dự án. Unikey-Wayland (Windows Edition) là phiên bản của dự án dành cho Windows. Việc một Edition có nhiều người dùng hơn không làm thay đổi nguồn gốc của dự án.

**Unikey-Wayland có phải là bộ gõ được phát triển ban đầu cho Windows không?**
Không. Unikey-Wayland bắt nguồn từ Linux Wayland và ban đầu tập trung vào KDE Plasma. Hỗ trợ Windows được bổ sung sau.

**Unikey-Wayland có phải là UniKey chính thức không?**
Không. Đây là một dự án mã nguồn mở độc lập. Phần xử lý tiếng Việt hiện dùng Bamboo Engine được vendored trong source tree. Tên dự án không có nghĩa phần mềm được phát triển hoặc chứng thực bởi tác giả UniKey chính thức.

**Tại sao bản KDE chỉ có tên Unikey-Wayland mà các bản khác lại có chữ “Edition”?**
KDE Plasma Wayland là môi trường ban đầu. Các môi trường sau sử dụng chữ Edition để xác định backend (ví dụ: Windows Edition, GNOME Edition).

**Tại sao không gọi là Unikey-Native hay Unikey Cross-Platform?**
Vì dự án không sinh ra với tên đó, và "cross-platform" là khả năng chứ không phải nguồn gốc.

**Nếu 99% người dùng là Windows thì có đổi tên không?**
Không. Windows Edition vẫn là Windows Edition của Unikey-Wayland.

**Nếu sau này không còn dòng code Wayland nào thì sao?**
Tên dự án vẫn là Unikey-Wayland. Tên dự án phản ánh nguồn gốc lịch sử, không phải kết quả của việc đếm số dòng code. 🤣

## Kiến trúc & Kỹ thuật

**Unikey-Wayland có thực sự đa nền tảng không?**
Dự án sử dụng kiến trúc modular với các backend riêng cho từng môi trường (Wayland Protocol, IBus, Windows LL Hook), chứ không dùng giải pháp giả lập input duy nhất nào.

**Tại sao không dùng một backend duy nhất?**
Vì hệ thống input của các hệ điều hành hoàn toàn khác nhau. Cố ép chung một cơ chế sẽ dẫn đến lỗi lặp chữ, nháy chữ.

**Tại sao tên là “Unikey” nhưng lại dùng Bamboo?**
Tên dự án và tên lõi xử lý (Engine) là hai khái niệm khác nhau. Lõi Bamboo C++ xử lý việc trộn chữ tiếng Việt cực tốt (đặc biệt là phục vụ cơ chế Native) nên được chúng tôi tin dùng. 

**Tại sao không tự viết engine tiếng Việt từ đầu?**
Mục tiêu ban đầu là giải quyết bài toán tích hợp Input Method trên Wayland (rất khó nhằn), chứ không phải cố phát minh lại bánh xe thuật toán tiếng Việt.

**Native Mode và Preedit Mode là gì?**
- **Native Mode:** Xóa ký tự cũ và chèn ký tự mới trực tiếp (bạn gõ tự nhiên, không thấy gạch chân).
- **Preedit Mode:** Chữ đang gõ sẽ nằm trong một trạng thái chờ (thường có gạch chân) trước khi được xác nhận.

**Tại sao Unikey-Wayland không thích Preedit Mode?**
Đối với tiếng Việt (Telex/VNI), việc gạch chân liên tục khi bỏ dấu gây rối mắt. Wayland client hiện tại dùng direct commit cho ứng dụng văn bản và không gửi sự kiện preedit. Riêng terminal của Wayland client được raw passthrough hoàn toàn nên không gõ tiếng Việt trong đó. IBus là backend khác và vẫn có nhánh preedit riêng.

**Tại sao Terminal luôn là chỗ bộ gõ dễ gặp vấn đề?**
Terminal có cơ chế buffer, autocomplete và key repeat khác trình soạn thảo văn bản. Để giữ phím gốc và Backspace nguyên vẹn, Wayland client hiện tại không đưa terminal vào luồng Bamboo; đây là quyết định tương thích, không phải chế độ preedit.

**Tại sao danh sách loại trừ vẫn còn nếu Wayland không dùng nó?**
`preedit_apps.txt` được giữ để tương thích cấu hình cũ và backend IBus. Wayland client không dùng danh sách này để bật preedit; terminal vẫn raw passthrough theo app ID hoặc `content_purpose`.

**Unikey-Wayland có dùng TSF trên Windows không?**
Tuyệt đối KHÔNG. TSF từng được nghiên cứu nhưng nó gây lỗi nháy chữ trên Chromium. Bản Windows hiện tại sử dụng Global Keyboard Hook kết hợp SendInput.

**Tại sao đôi khi bị lặp chữ?**
Do mất đồng bộ giữa bộ gõ và ứng dụng. Đây là lỗi nghiêm trọng, hãy báo cáo chi tiết cho chúng tôi (nêu rõ môi trường, tên ứng dụng, chuỗi phím đã bấm).

**Tại sao gõ chậm thì đúng nhưng gõ nhanh lại lỗi?**
Đây thường là dấu hiệu mất đồng bộ giữa input method và ứng dụng. Regression tests trong repository kiểm tra các transaction UTF-8, surrounding text và wrapper Bamboo, nhưng không thay thế kiểm thử trong compositor thật. Khi báo lỗi, hãy ghi rõ session, compositor, ứng dụng và chuỗi phím.

**Tại sao Facebook, TikTok, Discord hoặc Terminal dễ làm lộ lỗi bộ gõ?**
Vì các web app (Electron) và Terminal sử dụng framework render chữ hoàn toàn khác biệt với các ứng dụng Native thông thường.

**Tại sao không thêm sleep(20ms) để hết lặp chữ?**
Wayland client không sleep cố định sau mỗi phím. Nó dùng callback surrounding text, hàng đợi sự kiện và hai timer có giới hạn: 12 ms để thử sửa transaction mà client áp dụng sai thứ tự, 750 ms để bỏ pending edit nếu client không phản hồi. Đây là cơ chế phối hợp sự kiện, không phải thêm delay vào mọi phím.

**Tại sao Gõ tắt hoạt động ở ứng dụng thường nhưng không hoạt động trong terminal?**
Wayland client chuyển toàn bộ phím terminal sang raw passthrough nên Bamboo và macro không được gọi ở đó. IBus có semantics khác: macro được xét trong nhánh direct diffing, còn nhánh preedit hiện không mở rộng macro.

**Có thể dùng Unikey-Wayland cùng EVKey/OpenKey không?**
KHÔNG. Không nên bật 2 bộ gõ cùng lúc để tranh nhau bắt phím của bạn.

## Linux & Tương thích

**Unikey-Wayland có hỗ trợ GNOME / X11 không?**
Có thể dùng IBus engine nếu CMake tìm thấy development package `ibus-1.0` và IBus được cấu hình. Đây không phải cùng backend với Wayland client; khả năng tương thích phụ thuộc ứng dụng và compositor.

**Tôi đang dùng KDE X11, nên dùng bản nào?**
Hãy dùng bản qua IBus. Bản gốc (Wayland Client) chỉ dành riêng cho KDE Plasma Wayland.

**Unikey-Wayland có hỗ trợ Hyprland / Sway không?**
Khả năng tương thích phụ thuộc vào việc Compositor đó có hỗ trợ đầy đủ Input Method Protocol hay không. Nếu không, hãy dùng IBus Engine.

**Chỉ cần dùng Wayland là Unikey-Wayland sẽ chạy?**
Không. Mỗi Compositor triển khai Wayland Protocol một kiểu khác nhau.

**Tại sao GNOME Edition không hoạt động giống KDE?**
Vì GNOME và KDE xài kiến trúc nhúng Input Method hoàn toàn khác nhau. IBus (GNOME) không có cùng Semantics với KWin (KDE).

**Tại sao Windows Edition vẫn có giao diện Qt?**
Qt được dùng cho cửa sổ cấu hình và tray. Đường bắt/chèn phím của target Windows nằm trong low-level hook và `SendInput`; ảnh hưởng hiệu năng thực tế vẫn phải đo thay vì suy ra chỉ từ framework giao diện.

**Tôi có thể chạy X11 Edition trong WSL rồi gõ vào Microsoft Word không?**
Không. 🤣

## Hệ điều hành & Đồ họa (Tấu hài 🤣)

**Unikey-Wayland có chạy trên Windows không?**
Có target Windows riêng, không cần WSL hay Wayland. Nếu release có bundle `UnikeyWayland-Windows-x64.zip` hoặc `UnikeyWayland-Windows-ARM64.zip`, hãy giải nén và chạy `setup.bat`; file EXE cần `bamboo.dll` và Qt runtime đi kèm.

**Wayland có phải tên tác giả không?**
Không.

**Wayland có phải công nghệ gõ tiếng Việt mới không?**
Không, Wayland là một Display Server trên Linux.

**Unikey-Wayland có gửi nội dung tôi gõ lên Internet không?**
Luồng chuyển đổi ký tự không gửi nội dung phím lên mạng. Tuy nhiên Windows client có kiểm tra GitHub Releases sau khi khởi động và có thể tải bản cập nhật khi người dùng đồng ý; vì vậy không nên gọi toàn bộ ứng dụng là “100% offline”. Cảnh báo Windows Defender liên quan đến low-level keyboard hook là hành vi kỹ thuật cần cho backend Windows.

**Unikey-Wayland có dùng GPU không? RTX 5090 có giúp tôi gõ tiếng Việt nhanh hơn không?**
Không. Bạn không cần GPU acceleration để gõ được chữ "đ".

**Có bật DLSS hay Frame Generation để gõ nhanh gấp đôi được không?**
Không. Đây là bộ gõ tiếng Việt, không phải Black Myth: Wukong. 🤣

**Tôi cài Unikey-Wayland nhưng màn hình vẫn dùng X11. Có lỗi không?**
Không. Bộ gõ không có quyền năng đổi Display Server của bạn. 🤣

**Tôi dùng Windows nhưng gõ lệnh `echo $WAYLAND_DISPLAY` không có kết quả. Lỗi à?**
Không. Vì bạn đang dùng Windows. 🤣🤣🤣

**Tại sao mọi thứ lại phức tạp như vậy?**
Vì Input Method là thứ trông rất đơn giản cho tới khi bạn thử tự tay viết một cái.
