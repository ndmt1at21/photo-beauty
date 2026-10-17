# photo-beauty: kiến trúc lõi

## Hợp đồng lõi

Bốn header `core/tile.h`, `core/doc.h`, `core/render.h`, `core/stroke.h` là hợp đồng giữa các task lõi. A1 chỉ cài `tile.*`; phần còn lại là khai báo cho T1 (`doc.cpp`), B1 (`composite.cpp`), V1 (`view.cpp`), C2 (`stroke.cpp`) cài. Thêm hàm thì cứ thêm; đổi chữ ký hay ý nghĩa thì báo lead (mục Cách phối hợp của `docs/p0-tasks.md`).

**Điểm ảnh.** Mẫu là số nguyên theo mã hóa của tài liệu (không tuyến tính), như Photoshop ở 8 và 16 bit: `U8` 0–255, `U16` 0–65535 (`uint16_t` theo thứ tự byte của máy). Mọi phép tính dùng float 0–1 = giá trị / `max_value()`, ghi lại bằng `from_unit()` (làm tròn gần nhất, kẹp). Kênh xen kẽ, alpha ở cuối, không nhân trước: 4 là RGBA, 2 là xám + alpha, 1 là một giá trị không alpha (mặt nạ, vùng chọn, stroke buffer).

**Ô.** Cạnh `kTileSize = 1 << kTileLog2` (256; E4 đo cả 128). Ba loại: trống (không có `Tile` nào, đọc ra `fill()` của lưới), một màu (một giá trị, không cấp phát điểm ảnh), có dữ liệu. `id` duy nhất trong tiến trình, mới mỗi lần nội dung đổi, không bao giờ là 0 (cache dùng 0 cho ô trống), nên cache khóa theo `id` không bao giờ trả dữ liệu cũ. Đọc, ghi điểm ảnh chỉ qua `Pin`, `WritePin` (RAII; người giữ pin giữ ô sống): A1 để thô trong RAM, K1 thêm nén và scratch sau cùng API.

**Lưới (`TileGrid`).** Map thưa `TileXY` → `TilePtr` (`shared_ptr<const Tile>`), duyệt theo hàng, tọa độ âm được, có `fill()` (trong suốt cho layer, trắng hay đen cho mặt nạ) và `bounds()`. Chép lưới là chép con trỏ. Một lưới chỉ một luồng ghi; luồng khác chỉ đọc bản chép nó đã công bố (`shared_ptr<const TileGrid>`), không bao giờ đọc lưới nó đang ghi. `write(tx, ty)` ghi tại chỗ khi chỉ lưới này từng giữ ô (chưa bị chép theo lưới, chưa qua `set()`, không `TilePtr` nào ngoài lưới giữ), không thì chép trước; cả hai lấy `id` mới. Dấu "đã chia sẻ" đặt khi chép lưới thay cho chỉ dựa `use_count()`: `use_count()` đọc relaxed, luồng đọc vừa bỏ bản chép không làm các lần đọc của nó xảy ra trước lần ghi tại chỗ (TSan báo race, test bắt được). `write_rect()` gộp các ô nó chạm; `compact()`: ô toàn `fill` thành trống (khi có alpha, mọi điểm trong suốt coi như nhau bất kể màu), toàn một giá trị thành một màu.

**Tài liệu.** `DocState` bất biến khi đã chia sẻ: cỡ, mode (RGB, xám), bit, ICC, cây `Layer` dùng chung nút (`shared_ptr<const Layer>`, con xếp từ dưới lên), lựa chọn, `next_id`. Layer có `id` cố định (0 là gốc), loại (điểm ảnh, nhóm), `LayerProps` ↔ JSON có `kLayerSchemaVersion`, khóa lạ giữ trong `extra`; enum `Blend` đủ 27 mode của Photoshop cộng Pass Through, tên JSON ở `kBlendNames`. Lệnh là giá trị (`Command`, một `std::variant`): `run()` ra `DocState` mới dùng chung phần không đổi, phạm bất biến thì ném `std::invalid_argument`; `to_json()` ghi lệnh cho lịch sử và Actions. Hàm tên `run` chứ không `apply`: ADL với `std::variant` tìm thấy `std::apply`.

**Ghép.** `composite_tile(input, level, tx, ty)`: ô RGBA U16, alpha không nhân trước, các layer hiện từ dưới lên trên nền trong suốt, ngoài canvas trong suốt; ra null khi toàn trong suốt, ô một màu khi đều màu; người gọi giữ kết quả. `RenderInput` = `DocPtr` cộng lớp phủ nét cọ tùy chọn. Ghép GPU (B2) cùng nghĩa. `View`: tọa độ canvas theo điểm ảnh logic của Qt, `zoom` là điểm ảnh thiết bị trên một điểm ảnh tài liệu (1 là 100% của Photoshop), `angle` độ theo chiều kim đồng hồ, `level()` = ⌊log2(1/zoom)⌋, không âm.

**Nét cọ.** Luồng giao diện tạo `Stroke`, gọi `add()` (không bỏ, không gộp sự kiện), `finish()` khi nhả bút. Job Interactive áp sự kiện đúng thứ tự, là nơi duy nhất ghi stroke buffer, mỗi đợt công bố `StrokeState` (bản chép buffer, số sự kiện đã áp; sau khi gộp thêm lưới điểm ảnh mới của layer) rồi gọi `on_publish`; frame tìm ô đổi bằng cách so `id` ô của buffer; giao diện đưa lưới đó vào lệnh `SetPixels`, thành một bước lịch sử.

**Luồng** (theo `jobs.h`):

| Ai | Việc | Đọc, ghi |
| --- | --- | --- |
| Luồng giao diện | giữ `DocState` hiện tại, lịch sử, `View`; tạo `Stroke` | chạy lệnh, giao `DocPtr` cho job |
| Interactive | job nét cọ (đúng thứ tự, không `Latest`); frame (`Latest`); xem trước bộ lọc | nét cọ ghi stroke buffer; frame chỉ đọc bản chụp |
| Visible | ô sắp lộ ra khi pan, mip vùng đang xem | đọc bản chụp, ghi cache riêng |
| Background | áp full size, nén, scratch, lưu | đọc bản chụp; chạy dưới luồng giao diện (`lower_priority()`) |

**Bộ nhớ.** Ô đếm tham chiếu, người giữ cuối cùng giải phóng; lưới nằm trong `Layer` qua `shared_ptr<const TileGrid>`; `DocState` dùng chung giữa lịch sử và các job; undo, nhân layer, snapshot, lưu nền chỉ chép con trỏ. `tile_stats()` đếm byte điểm ảnh thô (K1 thêm nén, scratch, đang pin).
