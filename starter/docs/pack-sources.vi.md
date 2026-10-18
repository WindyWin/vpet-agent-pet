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
Mọi lần cài và cập nhật đều yêu cầu chấp nhận điều khoản.

**Cài từ URL…** nhận liên kết HTTPS tới `.agentpet`. Tệp tải trực tiếp không có mã
băm để đối chiếu với danh mục và được cảnh báo rõ. Giới hạn gói, kiểm tra tương thích
và xác nhận điều khoản vẫn áp dụng. Nhập tệp cục bộ vẫn có sẵn.

**Xóa nguồn** chỉ bỏ nguồn bên thứ ba khỏi danh sách, không xóa gói đã cài. Không thể
xóa nguồn chính thức, nhưng ứng dụng không tự làm mới nền. Chọn gói sẽ tải ảnh xem
trước. Danh mục hợp lệ gần nhất được lưu để duyệt ngoại tuyến; ảnh có thể không tải
được. Làm mới thất bại không ghi đè danh mục đã lưu. Gói biến mất khỏi danh mục mới
không bị xóa; vẫn có thể xóa bằng nút **Xóa gói đã cài…** trong Cài đặt.

Nguồn và bộ nhớ đệm nằm trong `<data>/sources/` của người dùng. Có thể cài gói ngay
cả khi ứng dụng nằm ở nơi chỉ đọc hoặc do trình quản lý gói cài đặt. Giai đoạn này
chỉ cập nhật thủ công; cập nhật tự động và quyền cập nhật sẽ thuộc giai đoạn 6.

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
