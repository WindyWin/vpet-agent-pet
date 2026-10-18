# Nguồn gói

Mở **Cài đặt → Thú cưng** (hoặc **Tiện ích**) → **Duyệt nguồn gói…**. Thư viện
chính thức được cấu hình sẵn. Chọn **Làm mới**, rồi chọn một gói để xem ảnh, tác giả,
giấy phép, khả năng tương thích và hướng dẫn thiết lập tích hợp nếu có.

**Cài đặt…** hoặc **Cập nhật…** tải phiên bản đã chọn. Ứng dụng kiểm tra đúng dung
lượng và mã SHA-256, kiểm tra toàn bộ gói bằng quy trình nhập tệp, rồi đối chiếu
thông tin với danh mục. Đọc điều khoản và chọn **Chấp nhận điều khoản và cài đặt**.
Khởi động lại Agent Pet để áp dụng. Lỗi tải, lỗi kiểm tra hoặc hủy xác nhận không
thay đổi phiên bản đang dùng. **Hủy tải xuống** hoặc đóng trình duyệt sẽ bỏ tệp tạm.

**Xóa gói…** ghi nhận việc xóa cho lần khởi động tiếp theo. Xóa thú cưng đang chọn
sẽ chuyển về VPet; xóa tiện ích sẽ bỏ trạng thái bật của tiện ích đó. Cài tiện ích
mới không tự bật tiện ích. Bạn cần bật riêng trong Cài đặt và thiết lập tiện ích
trình duyệt hoặc cầu nối bên ngoài theo hướng dẫn; cài dữ liệu không tự kết nối.

## Nguồn bên thứ ba và URL trực tiếp

Chọn **Thêm nguồn…**, nhập URL HTTPS của danh mục và đọc cảnh báo: người vận hành
nguồn kiểm soát nội dung và cập nhật. Nguồn này được ghi rõ là bên thứ ba. Mã băm
chỉ xác nhận tệp khớp danh mục, không chứng minh tác giả hoặc quyền phân phối.
Cài đặt thủ công và đổi nguồn đều yêu cầu chấp nhận điều khoản. Cập nhật tự động
cần quyền riêng theo từng nguồn và điều khoản không thay đổi (xem bên dưới).

**Cài từ URL…** nhận liên kết HTTPS tới `.agentpet`. Tệp tải trực tiếp không có mã
băm để đối chiếu với danh mục và được cảnh báo rõ. Giới hạn gói, kiểm tra tương thích
và xác nhận điều khoản vẫn áp dụng. Nhập tệp cục bộ vẫn có sẵn.

**Xóa nguồn** chỉ bỏ nguồn bên thứ ba khỏi danh sách, không xóa gói đã cài. Không thể
xóa nguồn chính thức. Khi bật kiểm tra tự động, ứng dụng chỉ kiểm tra nền các nguồn
của gói đã cài (xem bên dưới). Chọn gói sẽ tải ảnh xem
trước. Danh mục hợp lệ gần nhất được lưu để duyệt ngoại tuyến; ảnh có thể không tải
được. Làm mới thất bại không ghi đè danh mục đã lưu. Gói biến mất khỏi danh mục mới
không bị xóa; vẫn có thể xóa bằng nút **Xóa gói đã cài…** trong Cài đặt.

Nguồn và bộ nhớ đệm nằm trong `<data>/sources/` của người dùng. Có thể cài gói ngay
cả khi ứng dụng nằm ở nơi chỉ đọc hoặc do trình quản lý gói cài đặt. Trình duyệt hỗ
trợ cập nhật thủ công và tự động theo từng nguồn; xem “Cập nhật gói đã cài”.

## Xuất bản nguồn

Đưa `index.json` lên HTTPS theo [định dạng và ví dụ](pack-sources.md#publishing-a-source).
Dùng `schema_version: 1`, mỗi cặp `(kind, id)` chỉ xuất hiện một lần. Xuất bản tệp
`.agentpet` và ảnh PNG theo phiên bản cố định, tính chính xác dung lượng cùng SHA-256,
rồi cập nhật danh mục. Người dùng chỉ cần Làm mới, không phải cập nhật ứng dụng.

Các trường tên, tác giả, giấy phép, phiên bản, cấu trúc và phiên bản ứng dụng tối
thiểu phải khớp `pack.json`. Tiện ích cần `kind: "plugin"` và `pet` là mã thú cưng
đích. Trường `setup` tùy chọn mô tả phần mở rộng hoặc cầu nối cần cài riêng. Không
trường nào được thực thi hoặc tự bật tiện ích.

Danh mục tối đa 2 MiB và 1000 gói; tối đa 32 nguồn. URL phải là HTTPS, không có
thông tin đăng nhập hoặc phần neo. Ảnh PNG tối đa 4 MiB và 512 × 512 pixel. Tệp tải
xuống tối đa 544 MiB; quy trình nhập áp dụng thêm giới hạn riêng cho từng loại gói.
Danh mục sai, trùng mã, thiếu mã băm hoặc chứa trường không hỗ trợ sẽ bị từ chối mà
không ảnh hưởng dữ liệu đã cài. Xem tài liệu tiếng Anh để biết toàn bộ giới hạn.

## Cập nhật gói đã cài

Khi cài từ danh mục, ứng dụng lưu URL nguồn, phiên bản, SHA-256 của tệp và điều
khoản đã chấp nhận bên ngoài nội dung gói. Gói nhập từ tệp hoặc URL trực tiếp
**không có nguồn** và không được tự động cập nhật. Các gói được cài bằng phiên
bản ứng dụng trước cũng không có nguồn; hãy cài rõ ràng một phiên bản mới hơn từ
danh mục và chấp nhận điều khoản để thiết lập nguồn.

Khi bật **Tự động kiểm tra mỗi ngày** trong phần Cập nhật, Agent Pet kiểm tra các
nguồn của gói đã cài sau khi khởi động nếu đến hạn, rồi mỗi ngày. Thời điểm thử
kiểm tra được lưu qua các lần khởi động. Tắt tùy chọn này cũng dừng kiểm tra và
tải gói trong nền. Chế độ tải/cài bản cập nhật ứng dụng không cấp quyền cập nhật
gói. Bạn vẫn có thể chủ động làm mới danh mục và cài thủ công. Khi ngoại tuyến
hoặc kiểm tra thất bại, danh mục hợp lệ gần nhất và gói đang hoạt động được giữ lại.

Trong **Nguồn gói**, chọn một nguồn và bật **Tự động cập nhật gói đã cài từ nguồn
này**. Mặc định tùy chọn này tắt cho mọi nguồn, kể cả thư viện chính thức. Chỉ
phiên bản mới hơn của gói đã cài từ chính nguồn đó mới đủ điều kiện. Các lượt tải
chạy lần lượt, kiểm tra kích thước, SHA-256, nội dung và tính tương thích. Cửa sổ
hiển thị tiến độ và cho phép hủy. Bản cập nhật hợp lệ được chuẩn bị để áp dụng
ở lần khởi động sau, giữ lựa chọn thú cưng và trạng thái bật plugin. Tải lỗi hoặc
bị hủy không làm mất phiên bản đang hoạt động. Giao dịch gói và bản ghi nguồn
được khôi phục nếu ứng dụng bị gián đoạn.

Nếu tác giả, giấy phép hoặc toàn bộ điều khoản hiển thị thay đổi, bạn phải chấp
nhận thủ công. Bản cập nhật vẫn có trong danh mục nhưng không tự động áp dụng.
Phiên bản yêu cầu ứng dụng mới hơn hoặc lược đồ chưa hỗ trợ cũng hiển thị lý do
không tương thích và không được tải tự động. Gói biến mất khỏi danh mục không bị xóa.

Xóa nguồn giữ lại gói đã cài nhưng thu hồi nguồn gốc và quyền tự động cập nhật.
Gói hiển thị **Không có nguồn (chỉ cập nhật thủ công)**. Bản cập nhật tự động đã
chuẩn bị từ nguồn bị xóa sẽ bị bỏ ở lần khởi động sau. Thêm lại cùng URL không
khôi phục nguồn gốc hoặc quyền cũ; cần cài thủ công phiên bản mới hơn từ danh mục.
Tắt cập nhật gói tự động dừng các lượt tải tiếp theo; bản cập nhật đã xác minh và
chuẩn bị vẫn chờ lần khởi động sau.
