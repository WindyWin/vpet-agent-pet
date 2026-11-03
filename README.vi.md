# Agent Pet

[English](README.md) | **Tiếng Việt**

**Một thú cưng nhỏ trên màn hình, đồng hành cùng bạn khi Claude Code và Codex làm việc.**

Tích hợp OpenCode và Antigravity đang ở giai đoạn thử nghiệm, có trong **Cài đặt → Khởi động và agent**. Xem [hướng dẫn và phạm vi hỗ trợ](starter/docs/integrations.md#opencode-and-antigravity-experimental).

Thú cưng luôn ở bên bạn trên màn hình: suy nghĩ cùng agent, bận rộn khi agent chạy công cụ và vẫy tay khi cần bạn phê duyệt. Bạn có thể rời mắt khỏi terminal mà vẫn biết lúc nào cần quay lại.

<p align="center">
  <img src="docs/media/thinking.gif" width="120" alt="Đang suy nghĩ">
  <img src="docs/media/working.gif" width="120" alt="Đang làm việc">
  <img src="docs/media/needs-input.gif" width="120" alt="Cần bạn phản hồi">
  <img src="docs/media/turn-finished.gif" width="120" alt="Lượt đã hoàn thành">
</p>

- **Nhìn là biết trạng thái**: theo dõi được các phiên agent đang làm gì.
- **Không bỏ lỡ yêu cầu**: khi agent chờ bạn sẽ có thông báo nhỏ, nhấp một cái là đến đúng terminal.
- **Trả lời ngay tại chỗ** (mặc định tắt): trả lời agent trong khung chat nhỏ cạnh thú cưng; xem [trò chuyện với agent](starter/docs/interactions.md).
- **Chơi cùng thú cưng**: kéo, vuốt ve, tung lên, ngắm nó đi dạo và ngủ.
- **Nhắc bạn chăm sóc bản thân**: nghỉ mắt, uống nước, ăn trưa, vận động cùng Break Buddy và xem lại hoạt động trong ngày.
- **Riêng tư theo mặc định**: phần theo dõi trạng thái chỉ nhận biết *loại hoạt động*, không đọc prompt, mã nguồn hay câu lệnh của bạn; trò chuyện với agent là tính năng tùy chọn.

Hỗ trợ **Linux** x86_64 (X11 hoặc desktop Wayland có XWayland như GNOME và KDE), **macOS 11+** (Apple silicon và Intel), và **Windows 10 phiên bản 1809 trở lên / Windows 11** (x64).

## Nhập thú cưng hoặc plugin của bạn

Trong **Cài đặt → Thú cưng / Plugin**, chọn **Nhập…** hoặc kéo tệp `.agentpet` vào
thú cưng hay cửa sổ Cài đặt. Gói hiện bản xem trước trước khi bạn chấp nhận điều khoản;
khởi động lại để áp dụng. Có thể dùng
`agent-pet import <file.agentpet>` hoặc
`agent-pet remove <pet|plugin> <id>` mà không cần giao diện. Cài đặt cũng có nút gỡ
gói; nếu kiểm tra gói thay thế thất bại, gói cũ được giữ nguyên.

**Cài đặt → Thú cưng / Plugin → Duyệt nguồn gói…** mở thư viện chính thức, có bản
xem trước trước khi bấm **Cài đặt…** (Install…) hoặc **Cập nhật…** (Update…). Xem
[hướng dẫn tạo thú cưng](starter/docs/pets.md#importing-and-removing-packs),
[gói plugin](starter/docs/plugins.md#importing-a-agentpet-file) và
[nguồn gói](starter/docs/pack-sources.vi.md).

## Bắt đầu

Tải phiên bản mới nhất từ [trang Releases](https://github.com/WindyWin/vpet-agent-pet/releases/latest). Chọn hệ điều hành trong bảng **Download / Tải xuống** ở đầu trang phát hành; mỗi gói đều có sẵn hình ảnh thú cưng.

**Linux**

1. Tải `agent-pet-<version>-linux-x86_64.tar.gz` và giải nén.
2. Trong thư mục vừa giải nén, chạy `./install.sh`. Trình cài đặt cho bạn chọn thư mục cài đặt, thêm ứng dụng vào menu, kết nối với Claude Code hoặc Codex và tự khởi động thú cưng khi phiên agent bắt đầu.
3. Khởi động lại Claude Code hoặc Codex và gửi một prompt. Thú cưng sẽ phản ứng.

<img src="docs/media/installer.png" width="480" alt="Danh sách tùy chọn cài đặt trong terminal">

**macOS**

1. Tải và giải nén tệp `.zip` cho macOS, rồi chuyển **Agent Pet** vào **Applications**.
2. Mở ứng dụng. Lần đầu, macOS yêu cầu xác nhận: vào **System Settings → Privacy & Security → Open Anyway** (trên macOS 14 trở xuống, giữ Control rồi nhấp ứng dụng → **Open**).
3. Nhấp chuột phải vào thú cưng → **Cài đặt → Khởi động và agent** (Settings → Startup and agents), chọn **Bật** (Enable) cạnh Claude Code hoặc Codex, rồi khởi động lại ứng dụng agent.

**Windows**

1. Tải và chạy `agent-pet-<version>-windows-x86_64-setup.exe`. Ứng dụng được cài cho người dùng hiện tại, không cần quyền quản trị và xuất hiện trong menu Start với tên **Agent Pet**. Qt, Visual C++ runtime và hình ảnh thú cưng đã được đóng gói sẵn.
2. Trong bộ cài, chọn kết nối Claude Code hoặc Codex và tự khởi động thú cưng khi đăng nhập Windows hoặc khi phiên agent bắt đầu. Bạn cũng có thể kết nối sau tại **Cài đặt → Khởi động và agent** (Settings → Startup and agents).
3. Khởi động lại Claude Code hoặc Codex và gửi một prompt. Hook của Claude Code cần phiên bản **2.1.139 trở lên**; với Codex, xác nhận tin cậy hook trong `/hooks`.

Bản Windows chưa được ký mã. Nếu SmartScreen cảnh báo, chọn **More info → Run anyway**. Các phiên bản mới cung cấp bộ cài Setup; nếu đang dùng bản portable, hãy chạy Setup để chuyển sang bản được cài đặt. Xem [hướng dẫn cài Windows](starter/docs/install.md#windows) để biết chi tiết.

Cần hướng dẫn kỹ hơn hoặc gặp vấn đề? Xem [hướng dẫn cài đặt](starter/docs/install.md) và [bảng xử lý sự cố](starter/docs/install.md#troubleshooting) (tiếng Anh).

## Các tính năng

*Hình ảnh đều chụp từ ứng dụng thật; tên dự án chỉ là ví dụ.*

### Hiển thị hoạt động của agent

Bạn có thể chạy nhiều phiên cùng lúc. Thú cưng luôn hiển thị phiên cần chú ý nhất: yêu cầu đang chờ bạn được ưu tiên hơn lỗi, lỗi được ưu tiên hơn lượt vừa hoàn thành, v.v.

Khi một lượt Claude Code dừng vì chạm giới hạn sử dụng hoặc hết tín dụng, thú cưng lên giường với nhiệt kế và cơn ho, kèm ghi chú **Đã đạt giới hạn sử dụng** hoặc **Đã hết tín dụng** cho đến khi phiên đó hoạt động lại.

<table>
  <tr>
    <td align="center"><img src="docs/media/thinking.gif" width="140" alt="Đang suy nghĩ"><br>Đang suy nghĩ</td>
    <td align="center"><img src="docs/media/reading.gif" width="140" alt="Đang đọc"><br>Đang đọc tệp</td>
    <td align="center"><img src="docs/media/working.gif" width="140" alt="Đang làm việc"><br>Đang làm việc</td>
    <td align="center"><img src="docs/media/needs-input.gif" width="140" alt="Cần phản hồi"><br>Đang chờ bạn</td>
  </tr>
  <tr>
    <td align="center"><img src="docs/media/tool-error.gif" width="140" alt="Lỗi công cụ"><br>Có thao tác thất bại</td>
    <td align="center"><img src="docs/media/turn-finished.gif" width="140" alt="Hoàn thành lượt"><br>Xong rồi!</td>
    <td align="center"><img src="docs/media/sleeping.gif" width="140" alt="Đang ngủ"><br>Ngủ sau 10 phút yên tĩnh</td>
    <td align="center"><img src="docs/media/startled.gif" width="140" alt="Giật mình"><br>Giật mình trước lệnh nguy hiểm như <code>rm -rf</code></td>
  </tr>
</table>

Khi làm việc, thú cưng cũng thay đổi động tác: đặt sách xuống, cầm bút lên ngay tại bàn, xoay bút và thỉnh thoảng có những phản ứng nhỏ. Trong **Cài đặt**, bạn có thể chọn **Nhẹ nhàng** (Subtle) hoặc **Cổ điển** (Classic, lặp một hoạt ảnh cho mỗi hoạt động); chế độ mặc định là **Tinh nghịch** (Playful).

<table>
  <tr>
    <td align="center"><img src="docs/media/reading-to-working.gif" width="180" alt="Thú cưng đổi sách lấy bút tại bàn"><br>Từ đọc sang viết</td>
    <td align="center"><img src="docs/media/pen-spin.gif" width="180" alt="Thú cưng xoay bút tại bàn"><br>Xoay bút</td>
  </tr>
</table>

### Báo khi agent cần bạn

Khi agent cần phê duyệt, cần bạn nhập thông tin hoặc gặp lỗi công cụ, một thông báo ngắn xuất hiện cạnh thú cưng. Chọn **Mở** (Open) để chuyển đến terminal hoặc trình soạn thảo của phiên đó, hoặc **×** để đóng thông báo. Dấu hiệu màu cam vẫn hiển thị trên thú cưng cho đến khi bạn phản hồi. Bạn có thể tắt thông báo hoặc bật âm thanh khi có thông báo mới.

<img src="docs/media/alert-toast.png" width="420" alt="Thông báo cần phê duyệt cạnh thú cưng, có nút Open và nút đóng">

### Tất cả phiên trong một danh sách

Nhấp vào vùng ngoài đầu và bụng của thú cưng để xem các phiên đang chạy, sắp xếp theo mức độ cần chú ý. Nhấp vào một phiên để chuyển đến phiên đó.

Cuối cửa sổ đó có **Giới hạn gói** (Plan limits): mức dùng và giờ đặt lại của tài khoản Claude Code và Codex; xem [giới hạn gói](starter/README.md#plan-limits).

<img src="docs/media/session-list.png" width="420" alt="Danh sách bốn phiên: cần phê duyệt, đang làm việc, đang suy nghĩ và rảnh">

### Chơi cùng thú cưng

Kéo thú cưng đến bất kỳ đâu, nó sẽ đung đưa theo con trỏ. Chạm đầu để vuốt ve, chạm bụng để chọc nhẹ; nhấn giữ để tương tác liên tục hoặc giữ vùng má để bẹo má. Thả giữa lúc đang đung đưa, nó sẽ ngã xuống rồi đứng dậy. Đẩy ra mép màn hình, nó sẽ trốn ở đó và ló đầu ra cho đến khi agent bận rộn trở lại.

Đừng quá tay: tung nhiều quá, vuốt ve liên tục hoặc giữ quá lâu thì nó sẽ giận và bỏ đi.

<table>
  <tr>
    <td align="center"><img src="docs/media/drag-throw.gif" width="420" alt="Kéo và tung thú cưng"><br>Kéo và tung</td>
    <td align="center"><img src="docs/media/edge-hide.gif" width="240" alt="Thú cưng trốn ở mép màn hình"><br>Trốn ở mép màn hình</td>
  </tr>
  <tr>
    <td align="center"><img src="docs/media/touch-head.gif" width="160" alt="Xoa đầu"><br>Xoa đầu</td>
    <td align="center"><img src="docs/media/touch-body.gif" width="160" alt="Chạm bụng"><br>Chạm bụng</td>
  </tr>
  <tr>
    <td align="center" colspan="2"><img src="docs/media/angry-leave.gif" width="340" alt="Thú cưng giận vì bị vuốt ve quá nhiều, rồi nhảy đi"><br>Vuốt ve quá nhiều: giận rồi bỏ đi</td>
  </tr>
</table>

### Có nhịp sống riêng

Khi không có việc cần chú ý, thú cưng lặp lại một chuỗi động tác: vài vòng rảnh, rồi liếc nhìn, ngáp, vẻ chán, ngồi xổm hoặc kêu meo. Thỉnh thoảng nó cũng đi dạo, bò trên màn hình hoặc leo mép màn hình. Nó cũng có tâm trạng: nhiều lượt hoàn thành khiến nó vui, nhiều lỗi khiến nó buồn, còn làm việc liên tục quá lâu thì nó đói, cũng là lời nhắc bạn nên nghỉ một chút.

Khi một lượt hoàn thành, VPet chọn ngẫu nhiên cách ăn mừng: vẫy tay, reo vui lấp lánh, cười ngượng hoặc một trong hai động tác chân mèo vui vẻ. Vẫy tay có xác suất gấp đôi từng phản ứng còn lại; dịp đặc biệt và phần thưởng tích lũy có thể mang đến cách ăn mừng khác.

<table>
  <tr>
    <td align="center"><img src="docs/media/walk-left.gif" width="240" alt="Đi bộ"><br>Đi bộ</td>
    <td align="center"><img src="docs/media/crawl-left.gif" width="240" alt="Bò"><br>Bò</td>
    <td align="center"><img src="docs/media/climb-up-right.gif" width="240" alt="Leo mép màn hình"><br>Leo</td>
  </tr>
  <tr>
    <td align="center"><img src="docs/media/fidget-yawn.gif" width="160" alt="Ngáp"><br>Động tác khi rảnh</td>
    <td align="center"><img src="docs/media/cheer-shining.gif" width="160" alt="Ăn mừng vui vẻ"><br>Vui vẻ</td>
    <td align="center"><img src="docs/media/snack-hungry.gif" width="160" alt="Đói"><br>Đến lúc nghỉ rồi?</td>
  </tr>
</table>

### Những bất ngờ nhỏ

Tối thứ Sáu, sinh nhật bạn, đêm khuya, công việc kéo dài và mỗi lượt hoàn thành thứ 100 đều có phản ứng riêng. Ngày thường, thú cưng nhắc bạn chuẩn bị về nhà; ban đêm, nó nhắc bạn đi ngủ. Vẫn còn ít nhất một bất ngờ khác để bạn khám phá.

<table>
  <tr>
    <td align="center"><img src="docs/media/dance.gif" width="160" alt="Nhảy múa"><br>Điệu nhảy tối thứ Sáu</td>
    <td align="center"><img src="docs/media/birthday.gif" width="160" alt="Sinh nhật"><br>Sinh nhật bạn</td>
    <td align="center"><img src="docs/media/love-520.gif" width="160" alt="Trái tim ngày 20 tháng 5"><br>Ngày 20 tháng 5</td>
    <td align="center"><img src="docs/media/milestone.gif" width="160" alt="Cột mốc"><br>Mỗi lượt thứ 100</td>
  </tr>
</table>

### Nhắc bạn chăm sóc bản thân

Cứ sau 20 phút làm việc, thú cưng nhắc bạn nhìn ra xa; nhấp vào lời nhắc để có 20 giây yên tĩnh. Mỗi giờ, VPet nâng cốc uống nước để nhắc bạn uống cùng. Đến giờ ăn trưa (mặc định 12:00), nó ăn bánh mì và nhắc bạn nghỉ trưa. Bạn có thể chọn **Xong**, **Để sau** (nhắc lại sau 10 phút) hoặc **Bỏ qua hôm nay**.

Hoàn thành lời nhắc, VPet sẽ chọn ngẫu nhiên một trong **sáu phản ứng vui vẻ**: cười ngượng, reo vui lấp lánh, vẫy tay, nhảy múa, thả tim hoặc ăn mừng cột mốc. Với lời nhắc uống nước, ăn trưa và về nhà, phản ứng xuất hiện khi bạn chọn **Xong**; với nghỉ mắt và nghỉ vận động, VPet ăn mừng khi đếm ngược kết thúc. Sau đó, VPet trở về trạng thái nghỉ; một phản ứng có thể xuất hiện hai lần liên tiếp.

**Break Buddy** mời bạn dành 60 giây để vận động sau mỗi giờ làm việc. Chọn **Bắt đầu nghỉ** để VPet nhảy dây cùng bạn trong lúc bạn vận động hoặc giãn cơ. Bạn cũng có thể bắt đầu bất cứ lúc nào từ menu chuột phải → **Nghỉ vận động**, hoặc nhấp vào lời nhắc để kết thúc sớm. Vào **Cài đặt → Lời nhắc → Nghỉ vận động** để tắt lời nhắc hoặc chọn khoảng cách giữa các lần nhắc: **30**, **60** hoặc **90 phút**.

Thú cưng chỉ nhắc khi bạn đang dùng máy tính, giữ yên lặng khi agent cần bạn và không làm phiền vào ban đêm. Khoảng nghỉ vận động sẽ kết thúc nếu có yêu cầu phê duyệt hoặc lỗi cần bạn xử lý, hoặc khi bạn khóa màn hình, tạm hoãn hay tắt thông báo, ẩn thú cưng hoặc nhấc nó lên. Các thú cưng khác có hoạt ảnh riêng khi nhắc bạn.

<table>
  <tr>
    <td align="center"><img src="docs/media/break-buddy.gif" width="200" alt="VPet nhảy dây trong lúc nghỉ vận động"><br>Break Buddy</td>
    <td align="center"><img src="docs/media/lunch.gif" width="200" alt="VPet ăn bánh mì cùng lời nhắc nghỉ trưa"><br>Cùng ăn trưa</td>
    <td align="center"><img src="docs/media/drinking.gif" width="200" alt="VPet uống nước từ cốc cùng lời nhắc uống nước"><br>Cùng uống nước</td>
  </tr>
</table>

<table>
  <tr>
    <td align="center"><img src="docs/media/eye-break.png" width="300" alt="Thông báo nghỉ mắt cạnh thú cưng"><br>Nghỉ mắt</td>
    <td align="center"><img src="docs/media/water.png" width="300" alt="Thông báo uống nước cạnh thú cưng"><br>Đến giờ uống nước</td>
  </tr>
</table>

### Tổng kết ngày làm việc

Nhấp chuột phải → **Tổng kết hôm nay** (Today's recap) để xem tóm tắt một dòng, ví dụ *“Hôm nay: 38 lượt trên 3 dự án · lượt dài nhất: 22 phút”*. Nhấp vào bản tóm tắt để xem chi tiết từng dự án. Lời nhắc về nhà vào các ngày trong tuần cũng kèm tổng kết này.

<table>
  <tr>
    <td align="center"><img src="docs/media/recap.png" width="300" alt="Tóm tắt ngày làm việc"><br>Tóm tắt</td>
    <td align="center"><img src="docs/media/recap-breakdown.png" width="300" alt="Chi tiết số lượt theo dự án"><br>Chi tiết</td>
  </tr>
</table>

### Tùy chỉnh theo ý bạn

Nhấp chuột phải vào thú cưng (hoặc biểu tượng ở khay hệ thống / thanh menu) để xem danh sách phiên và tổng kết ngày, bắt đầu nghỉ vận động, tắt thông báo, giữ thú cưng luôn ở trên cùng, mở **Cài đặt** (Settings) hoặc **Thoát** (Quit). Trong **Cài đặt**, bạn có thể:

- đổi kích thước, chọn những thông báo được hiển thị và chọn nhân vật (thú cưng khác được tải về khi chọn);
- chọn mức độ sinh động khi làm việc, tắt chuỗi hoạt ảnh khi rảnh, hoặc tắt đi dạo, tâm trạng, tương tác và bất ngờ;
- bật [gói plugin](starter/docs/plugins.md) để thêm hoạt ảnh cho thú cưng, và để chúng phản ứng khi script hoặc CI của bạn gửi [một sự kiện tùy chỉnh](starter/docs/events.md#custom-events);
- chỉnh hoặc tắt lời nhắc nghỉ mắt, uống nước và nghỉ vận động; đặt giờ nhắc ăn trưa (mặc định 12:00, có thể nhắc đến 13:15), nhắc thứ Hai, về nhà và đi ngủ; nhập ngày sinh nhật;
- đổi hoạt ảnh theo giờ đi ngủ: VPet làm việc với vẻ buồn ngủ, ngáp khi hoàn thành và ngủ khi rảnh từ giờ đi ngủ đến 06:00. Bật hoặc tắt trong **Cài đặt → Thú cưng**, độc lập với lời nhắc và tâm trạng;
- bật trò chuyện với agent, kết nối Claude Code, Codex, OpenCode hoặc Antigravity bằng một cú nhấp, tự khởi động cùng agent hoặc khi đăng nhập;
- chọn cách cài đặt bản cập nhật và sửa chữa bản cài đặt bị hỏng.

Giao diện hỗ trợ **tiếng Anh và tiếng Việt**. Mặc định, ứng dụng dùng ngôn ngữ hệ thống; vào **Cài đặt → Chung → Ngôn ngữ** (Settings → General → Language) để đổi ngôn ngữ ngay.

Nếu thú cưng che mất nội dung, nhấp biểu tượng ở khay hệ thống để ẩn nó. Nó vẫn theo dõi các phiên và hiển thị dấu hiệu cần chú ý trên biểu tượng.

<table>
  <tr>
    <td align="center" valign="top"><img src="docs/media/menus/after-menu.png" width="380" alt="Menu chuột phải"><br>Menu chuột phải</td>
    <td align="center" valign="top"><img src="docs/media/menus/after-settings.png" width="340" alt="Cửa sổ cài đặt"><br>Cài đặt</td>
  </tr>
</table>

### Cập nhật phiên bản

Trên Linux, mặc định thú cưng tự cập nhật trong nền, chỉ tải những phần đã thay đổi và giữ nguyên cài đặt. Nếu bản mới không khởi động được, ứng dụng quay lại bản trước.

Trên Windows, phiên bản được cài bằng bộ cài hỗ trợ tự động cập nhật tại **Cài đặt → Cập nhật** (Settings → Updates). Ứng dụng tải bộ cài đã được xác minh bằng SHA-256, lưu các phiên đang theo dõi, nâng cấp ngay trong thư mục cài đặt rồi khởi động lại thú cưng. Cài đặt, hook và tùy chọn khởi động được giữ nguyên. Windows chưa hỗ trợ tự động quay lại phiên bản trước; nếu cập nhật thất bại, hãy chạy bộ cài thủ công để sửa chữa. Các bản Windows cũ cần nâng cấp thủ công một lần để có trình cập nhật mới.

Trên macOS và bản Windows portable, thú cưng thông báo khi có phiên bản mới để bạn cài thủ công: thay ứng dụng trên macOS hoặc chạy bộ cài Windows mới hơn. Xem [hướng dẫn cập nhật](starter/docs/install.md#update-notifications-and-automatic-updates) để biết các chế độ cập nhật và cách khôi phục.

**Cài đặt → Cập nhật → Sửa chữa bản cài đặt** (Repair installation) tải lại gói đã kiểm tra của đúng phiên bản đang chạy và thay các tệp bị thiếu hoặc hỏng; xem [hướng dẫn sửa chữa](starter/docs/install.md#repair-an-existing-installation).

## Quyền riêng tư

- Mọi dữ liệu hoạt động đều nằm trên máy bạn. Thú cưng chỉ nhận sự kiện từ agent qua một kênh cục bộ riêng.
- Nó chỉ biết *loại hoạt động* (suy nghĩ, làm việc, chờ, hoàn thành), không nhận prompt, mã nguồn, nội dung tệp hay câu lệnh. Với lệnh nguy hiểm, nó chỉ nhận một giá trị có/không cho biết “lệnh này có vẻ nguy hiểm”. Ngoại lệ là trò chuyện với agent, mặc định tắt.
- Khi bật trò chuyện với agent, tin nhắn giữa bạn và agent chỉ đi qua thú cưng trên máy này và không được lưu.
- Dữ liệu lưu lại chỉ gồm cài đặt, gói đã tải và số liệu tổng kết mỗi ngày (có tên thư mục dự án); số liệu này giữ trong hai tuần.
- Khi kết nối, ứng dụng thêm các mục hook riêng vào cài đặt Claude Code hoặc Codex và giữ nguyên các mục khác. Gỡ cài đặt chỉ xóa những mục của Agent Pet.

## Lưu ý khi sử dụng

- **Thú cưng không phản ứng?** Khởi động lại Claude Code hoặc Codex sau khi kết nối, rồi gửi prompt mới. Trong Codex, phê duyệt hook mới tại `/hooks`.
- **Không thấy thú cưng?** Nhấp biểu tượng ở khay hệ thống, hoặc nhấp chuột phải → **Thêm → Khôi phục vị trí và điều khiển thú cưng** (More → Recover pet position and input).
- **macOS:** chưa hỗ trợ đưa cửa sổ terminal lên trước và tự động cài cập nhật. **Mở** (Open) vẫn chuyển được khung terminal trong tmux và herdr.
- **Dùng Herdr?** Plugin [herdr-agent-pet](https://github.com/WindyWin/herdr-agent-pet) khởi động thú cưng cùng máy chủ Herdr và cài đặt hoặc cập nhật ngay trong Herdr: `herdr plugin install WindyWin/herdr-agent-pet`. Plugin hỗ trợ Linux x86_64 và macOS, yêu cầu Herdr 0.7.5 trở lên. Xem [hướng dẫn plugin Herdr](starter/docs/install.md#herdr-plugin): cách khởi động này không hỗ trợ điều hướng **Open** của Herdr.
- **Windows:** **Mở** (Open) đưa cửa sổ Windows Terminal hoặc console cổ điển của agent lên trước; nếu không tìm được, ứng dụng tìm trình soạn thảo hoặc terminal theo cây tiến trình cha. Phiên bản được cài bằng bộ cài hỗ trợ tự động cập nhật; bản portable cần nâng cấp thủ công. Dùng `agent-pet-cli.exe` cho lệnh hook và thao tác cần xuất kết quả ra console.
- **Linux trên Wayland:** thú cưng chạy qua XWayland, có sẵn mặc định trên GNOME và KDE. Chưa hỗ trợ Wayland thuần.

## Dành cho lập trình viên

### Tổng quan kiến trúc

Kiến trúc đầu-cuối. Số issue cho biết mỗi lớp đến từ phần việc nào. Mũi tên thể hiện luồng chạy, không phải thứ tự biên dịch.

```mermaid
flowchart TB
  agents["Claude Code / Codex"] --> input["Adapter provider · hook / emit không giao diện"]
  input --> ipc["IPC cục bộ riêng · sự kiện đã kiểm tra"]
  ipc --> sessions["Sessions · tổng hợp hoạt động / yêu cầu chú ý"]
  sessions --> runtime["#67 Bộ điều phối hành vi<br/>Ý định → điều kiện / ưu tiên / ngắt"]
  life["Tương tác / đi dạo / tâm trạng / sức khỏe<br/>Bất ngờ / khởi động / thoát"] --> runtime
  custom["#43 Sự kiện / quy tắc tùy chỉnh"] --> runtime
  runtime --> cues["#64 Ánh xạ tín hiệu<br/>Tín hiệu ngữ nghĩa → trạng thái / phản ứng"]
  cues --> player["Player · giai đoạn / khung hình / chuyển tiếp"]
  player --> window["PetWindow · thú cưng / di chuyển / đầu vào"]
  player -. kết quả hiển thị .-> runtime
  window --> life

  sessions --> alerts["Thông báo / danh sách phiên / khay hệ thống"]
  alerts --> focus["Mở phiên · adapter môi trường chạy"]
  focus --> platform["Dịch vụ nền tảng · Linux / macOS / Windows"]
  window --> platform
  sessions --> recap["Tổng kết ngày / lưu trạng thái phiên"]

  packs["#62 PetLibrary / Catalog<br/>Thú cưng đang dùng · hình ảnh / tác giả / ánh xạ riêng"] --> cues
  packs --> player
  plugins["#43 Gói plugin<br/>Phần bổ sung catalog · quy tắc sự kiện"] --> packs
  plugins --> custom
  updates["Cập nhật ứng dụng / đóng gói"] --> packs
  downloads["#62 Phần 2<br/>Tải thú cưng có kiểm tra tính toàn vẹn"] --> packs
  settings["Cài đặt / tùy chọn"] -. cấu hình .-> life
  settings -. chọn thú cưng / plugin .-> packs
```

`Sessions` xác định thứ tự ưu tiên giữa các phiên; #67 chọn ý định hành vi; #64 quyết định cách thú cưng thể hiện tín hiệu đã chọn. Thông báo được xử lý theo luồng riêng. Việc di chuyển và thao tác với cửa sổ hệ điều hành vẫn do `PetWindow` và các dịch vụ nền tảng đảm nhiệm. Xem [bản đồ mã nguồn](starter/docs/architecture.md#code-map) để tìm các tệp liên quan và các quyết định thiết kế.

### Biên dịch và đóng góp

Mã ứng dụng nằm trong [`starter/`](starter/README.md): hướng dẫn biên dịch từ mã nguồn, chạy kiểm thử, tùy chọn dòng lệnh, gửi sự kiện demo và đóng gói. Tài liệu đọc thêm (tiếng Anh):

- [Tổng quan kiến trúc](starter/docs/architecture.md) và [các quyết định thiết kế](starter/docs/adr/README.md)
- [Giao thức sự kiện](starter/docs/events.md) và [tích hợp Claude Code / Codex](starter/docs/integrations.md)
- [Issue đang mở](https://github.com/WindyWin/vpet-agent-pet/issues) cho lỗi, yêu cầu tính năng và công việc dự kiến
- [Quy tắc commit cho CI](starter/README.md#ci-commit-rules): dùng `docs: ...` hoặc `docs(scope): ...` khi toàn bộ thay đổi chỉ là tài liệu. Commit cuối được push lên `main` có tiền tố `docs` sẽ bỏ qua các job build/test/package; GitHub vẫn có thể hiển thị workflow với các job bị bỏ qua. Tag phiên bản và chạy workflow thủ công vẫn build.

Thư mục gốc còn giữ bộ hình ảnh VPet nguyên bản (khoảng 5.500 khung hình, 735 MiB), để bổ sung hoạt ảnh. Ứng dụng không cần bộ này để chạy. Kiểm tra bằng `python3 scripts/assets.py verify`, hoặc liệt kê các chuỗi bằng `python3 scripts/assets.py catalog`.

## Ghi nhận tác giả và giấy phép

Hình ảnh nhân vật do **đội ngũ VUP-Simulator** thực hiện, từ [LorisYounger/VPet](https://github.com/LorisYounger/VPet). Hình ảnh tuân theo [điều khoản riêng](licenses/VPET-ARTWORK-TERMS.md); xem [thông báo về bên thứ ba](THIRD_PARTY_NOTICES.md).

Mã nguồn riêng của Agent Pet được cấp phép theo [Apache-2.0](starter/LICENSE). Giấy phép đó không áp dụng cho hình ảnh nhân vật.

Ứng dụng chỉ đóng gói sẵn VPet. Duyệt [thư viện thú cưng](https://github.com/WindyWin/agent-pet-gallery/releases) trong **Cài đặt → Thú cưng → Duyệt nguồn gói…** (Browse pack sources…), hoặc nhập tệp `.agentpet`.

Danh mục gói: [duyệt, cài đặt và xuất bản nguồn](starter/docs/pack-sources.vi.md).
