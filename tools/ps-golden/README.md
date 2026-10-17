# Ảnh mẫu Photoshop (B4)

`blend.jsx` làm trong Photoshop các ảnh mà `tests/test_ps_golden.cpp` so với photo-beauty: hai layer `base.png`, `top.png` ghép ở 11 blend mode × opacity 100% và 50%, và chấm cọ tròn ở 3 cỡ × 4 độ cứng. Nhắm Photoshop 2025 (26.x) trên Windows; script là ExtendScript nên các bản từ CC 2019 (20.x) trở đi cũng chạy. Script chưa chạy thử trên Photoshop thật: có lỗi thì gửi lead file `out\photoshop.txt`.

## Các bước

1. Trong WSL, ở `~/photo-beauty`, chép ảnh vào và script sang Windows:

   ```sh
   mkdir -p /mnt/c/Users/Teko/Downloads/ps-golden
   cp tests/golden/ps/base.png tests/golden/ps/top.png tools/ps-golden/blend.jsx /mnt/c/Users/Teko/Downloads/ps-golden/
   ```

   Photoshop ở máy khác thì chép thư mục `ps-golden` sang máy đó.
2. Mở Photoshop, đóng mọi ảnh. Edit > Color Settings (Ctrl Shift K), bấm More Options nếu có nút đó:
   - Working Spaces, RGB: `sRGB IEC61966-2.1`;
   - Color Management Policies: RGB là Preserve Embedded Profiles; bỏ chọn ba ô Ask When Opening, Ask When Pasting (Profile Mismatches) và Ask When Opening (Missing Profiles);
   - Conversion Options: bỏ chọn Use Dither (8-bit/channel images);
   - Advanced Controls: bỏ chọn Blend RGB Colors Using Gamma (Blend Text Colors để nguyên).

   Bấm OK. Script cũng tự tắt dither, gamma và các ô hỏi nếu đọc được Color Settings, rồi trả lại thiết lập cũ khi xong; nó tự kiểm Photoshop trộn RGB không qua gamma 1, còn qua gamma 1 thì dừng và báo.
3. File > Scripts > Browse… → `ps-golden\blend.jsx` → Open. Chờ khoảng một phút tới khi hiện "Done", giữa chừng không bấm gì. Thư mục `ps-golden\out` khi đó có 24 file: 22 ảnh ghép `<mode>-<opacity>.png`, `dabs.png`, `photoshop.txt`. Hiện "Stopped" thì làm theo câu báo rồi chạy lại, hay gửi lead `out\photoshop.txt`.
4. Chép kết quả vào repo (Photoshop ở máy khác thì trước đó đưa thư mục `out` về `C:\Users\Teko\Downloads\ps-golden\out` của máy này), rồi báo lead:

   ```sh
   cp /mnt/c/Users/Teko/Downloads/ps-golden/out/* ~/photo-beauty/tests/golden/ps/
   ```

   `build/default/pb-tests test_ps_golden_blend test_ps_golden_dabs` khi đó in độ lệch của từng mode, từng chấm.

## Script giữ gì

- Không chuyển profile: `base.png`, `top.png` mang giá trị sRGB nhưng không gắn profile, nên Photoshop mở ra không chuyển gì; script gán (Assign Profile, không phải Convert) `sRGB IEC61966-2.1` cho cả hai rồi chép layer của `top` sang tài liệu `base`. Photoshop vẫn hỏi về profile khi mở thì chọn Leave as is (don't color manage). Có chuyển thì `test_ps_golden_blend` thấy ngay ở `normal-100`.
- 16 bit: dừng nếu ảnh vào không mở thành RGB 16 bit, hay `normal-100.png` lưu ra không còn 16 bit. Photoshop giữ mẫu 16 bit dưới dạng 0–32768; `pb-bench ps-inputs` chỉ ghi những mức Photoshop đọc lại đúng.
- Chấm cọ: công cụ Brush, Reset Tool, tắt mọi dynamics còn bật, cọ tròn computed, đen trên nền trắng, opacity và flow 100%; mỗi chấm là Stroke Path bằng Brush trên một path một điểm, ở giữa ô 128 px.
- `photoshop.txt`: phiên bản Photoshop, Color Settings trước và sau, kết quả kiểm cách trộn, thiết lập cọ.
