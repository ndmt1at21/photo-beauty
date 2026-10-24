# Ảnh mẫu Photoshop

Script ExtendScript làm trong Photoshop các ảnh mà test của photo-beauty so với: B4 (P0) là `blend.jsx` ở đây; P1 là các script trong `p1/` (B30, D32, F31), cùng `strokes.jsx` (C41), `select.jsx` (S39), `resample.jsx` (T38), `psd.jsx` (E33) có README riêng (`README-<mảng>.md`). Nhắm Photoshop 2025 (26.x) trên Windows; script là ExtendScript nên các bản từ CC 2019 (20.x) trở đi cũng chạy. Script chưa chạy thử trên Photoshop thật: có lỗi thì gửi lead file `photoshop.txt` của script đó.

Bạn chạy mọi script một lần ở B33 (sau lô 2 của P1, khoảng 2–3 giờ cho tất cả), cùng lần với B4 còn treo: làm mục B4 rồi mục P1, rồi README của các mảng khác.

## B4: 11 blend mode, chấm cọ

`blend.jsx` làm các ảnh mà `tests/test_ps_golden.cpp` so với photo-beauty: hai layer `base.png`, `top.png` ghép ở 11 blend mode × opacity 100% và 50%, và chấm cọ tròn ở 3 cỡ × 4 độ cứng.

### Các bước

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

### Script giữ gì

- Không chuyển profile: `base.png`, `top.png` mang giá trị sRGB nhưng không gắn profile, nên Photoshop mở ra không chuyển gì; script gán (Assign Profile, không phải Convert) `sRGB IEC61966-2.1` cho cả hai rồi chép layer của `top` sang tài liệu `base`. Photoshop vẫn hỏi về profile khi mở thì chọn Leave as is (don't color manage). Có chuyển thì `test_ps_golden_blend` thấy ngay ở `normal-100`.
- 16 bit: dừng nếu ảnh vào không mở thành RGB 16 bit, hay `normal-100.png` lưu ra không còn 16 bit. Photoshop giữ mẫu 16 bit dưới dạng 0–32768; `pb-bench ps-inputs` chỉ ghi những mức Photoshop đọc lại đúng.
- Chấm cọ: công cụ Brush, Reset Tool, tắt mọi dynamics còn bật, cọ tròn computed, đen trên nền trắng, opacity và flow 100%; mỗi chấm là Stroke Path bằng Brush trên một path một điểm, ở giữa ô 128 px.
- `photoshop.txt`: phiên bản Photoshop, Color Settings trước và sau, kết quả kiểm cách trộn, thiết lập cọ.

## P1: `p1/*.jsx`

Mỗi script trong `p1/` dựng từng ảnh mẫu (một "case": ảnh vào, layer, tham số theo khóa JSON của photo-beauty) trong một tài liệu RGB 16 bit sRGB mới, lưu ảnh ghép, rồi ghi `out\<script>\` cạnh nó: các ảnh `<case>.png`, `index.json` (mỗi case như photo-beauty đọc, `tests/golden.h`), `photoshop.txt` (phiên bản, Color Settings, từng layer như Photoshop có). Ảnh vào ở `p1\in\` (`tests/golden/ps/p1/in/`, do `pb-bench ps-inputs` làm). Các script:

| Script | Task | Ảnh | Thời gian ước tính |
| --- | --- | --- | --- |
| `blend.jsx` | B30 | 174 ảnh: 27 blend mode × opacity 100, 50%; Fill của 8 mode đặc biệt; trong suốt trên trong suốt; nhóm cô lập, Pass Through, có mask; mask Density; clipping; Blend If; kênh; adjustment layer Invert; mỗi ảnh vào như Photoshop đọc (`in-*`). 16 bit cho 16 mode B4 chưa có (32 ảnh) và `in-*`, còn lại 8 bit; khoảng 9 MB | khoảng 8 phút |
| `adjust.jsx`, `gradient.jsx` | D32 | adjustment, gradient, 8 bit | xem đầu script |
| `filters.jsx`, `styles.jsx` | F31 | bộ lọc, layer style, chữ, hình, smart object, 8 bit | xem đầu script |

### Các bước

1. Trong WSL, ở `~/photo-beauty`, chép script và ảnh vào sang Windows (cùng thư mục `ps-golden` của B4):

   ```sh
   mkdir -p /mnt/c/Users/Teko/Downloads/ps-golden/p1/in
   cp tools/ps-golden/p1/*.jsx /mnt/c/Users/Teko/Downloads/ps-golden/p1/
   cp tests/golden/ps/p1/in/*.png /mnt/c/Users/Teko/Downloads/ps-golden/p1/in/
   ```

   Script đọc `lib.jsx` cạnh nó: chép cả thư mục `p1`, đừng chép riêng một script.
2. Font cho ảnh chữ của `filters.jsx`, `styles.jsx` (F31): cài Space Grotesk trên Windows, từ `ui/fonts/SpaceGrotesk.ttf` và `ui/fonts/SpaceGrotesk-SemiBold.ttf` (chép sang Windows, nhấp phải › Install for all users), rồi mở lại Photoshop. `blend.jsx` không cần font.
3. Photoshop: đóng mọi ảnh, Color Settings như bước 2 của B4 (bốn điểm, nhất là Use Dither tắt: ảnh 8 bit lưu không dither). Script tự tắt các ô đó nếu đọc được Color Settings, trả lại khi xong, và kiểm: trộn RGB không qua gamma 1 (mọi script), đổi 16 sang 8 bit không dither (script có ảnh 8 bit). Kiểm không qua thì script dừng ngay với câu báo, chưa làm ảnh nào.
4. File > Scripts > Browse… → `ps-golden\p1\blend.jsx` → Open. Chờ tới khi hiện "Done", giữa chừng không bấm gì (Photoshop mở, đóng tài liệu liên tục). Rồi lần lượt `adjust.jsx`, `gradient.jsx`, `filters.jsx`, `styles.jsx` (những script đã có).
   - "Done: N images": xong. "Done: N images, M failed": M case không làm được (lý do trong `photoshop.txt` và mục `failed` của `index.json`), các case còn lại vẫn đúng: chép như bước 5 và gửi lead `photoshop.txt` của script đó.
   - "Stopped: …": kiểm thiết lập không qua (thường là Color Settings): làm theo câu báo rồi chạy lại script đó.
5. Chép kết quả vào repo (Photoshop ở máy khác thì trước đó đưa `p1\out` về `C:\Users\Teko\Downloads\ps-golden\p1\out` của máy này):

   ```sh
   cp -r /mnt/c/Users/Teko/Downloads/ps-golden/p1/out/* ~/photo-beauty/tests/golden/ps/p1/
   build/default/pb-tests test_golden_ps
   ```

   Mỗi script thành một thư mục `tests/golden/ps/p1/<script>/`. `test_golden_ps` kiểm mỗi thư mục đủ ảnh theo `index.json`, Photoshop đọc ảnh vào đúng mức (`in-*`), cả `tests/golden/ps/p1/` không quá 30 MB; rồi báo lead (lead chạy test so của từng task, ghi độ lệch vào dòng task).

### Script giữ gì

- Ảnh vào như B4: PNG 16 bit không gắn profile, mức mẫu Photoshop đọc lại đúng; script gán `sRGB IEC61966-2.1` (Assign, không Convert), không chuyển gì. `blend.jsx` lưu lại từng ảnh vào như Photoshop đọc (`in-<tên>.png`) để test kiểm.
- Tài liệu luôn 16 bit (Photoshop tính trên 0–32768). Ảnh 8 bit: ảnh ghép 16 bit lưu PNG, mở lại, đổi sang 8 bit (Use Dither tắt nên làm tròn, script kiểm), lưu, xóa bản 16 bit. `index.json` ghi `bits` của từng ảnh; `tests/golden.h` đọc cả hai.
- Blend 16 bit, mà vẫn giữ `tests/golden/ps/p1/` ≤ 30 MB (câu 6 của "Cần bạn chốt" trong `docs/p1-tasks.md`): để mọi ảnh của `blend.jsx` 16 bit thì riêng nó khoảng 32 MB, nên lead chốt 24/10 (sẽ báo bạn): 16 bit như B4 cho 16 mode B4 chưa có (opacity 100, 50%) và 8 ảnh vào `in-*`; mọi case blend khác 8 bit, như ảnh ra của adjustment, bộ lọc, style. Mode nào cũng có một ảnh mẫu 16 bit (11 mode P0 ở bộ của B4, `tests/golden/ps/`); các case về cấu trúc (Fill, alpha, nhóm, mask, clip, Blend If, kênh, Invert) so tới 1 mức 8 bit vẫn có nghĩa, vì làm tròn 8 bit của Photoshop chỉ chiếm tối đa nửa mức.
- Mỗi case dựng trong tài liệu mới bằng kích thước canvas của nó, layer chép từ ảnh vào (Duplicate Layer), xếp đúng thứ tự rồi mới đặt tham số; dựng xong script so cây layer với case (tên, thứ tự, nhóm) và đọc lại blend mode, opacity, Fill, clipping, có mask hay không: lệch thì case đó hỏng và có lý do.
- `photoshop.txt`: phiên bản, Color Settings trước và sau, kết quả các phép kiểm, từng case và từng layer như Photoshop báo (mode, opacity, fill, clipping, mask, Density, Blend If, kênh), lỗi nếu có.

### Cỡ ảnh (cho script lô 2)

Ngân sách: cả `tests/golden/ps/p1/` ≤ 30 MB (`test_golden_ps` kiểm). Đo bằng PNG của spng (cùng cách lưu của `golden::write_png`; Photoshop nén có thể lệch ±15%):

| Ảnh | 16 bit | 8 bit |
| --- | --- | --- |
| Ghép `top.png` trên `base.png`, 256 × 384 | ~168 KB | ~21 KB |
| `alpha.png` trên chính nó xoay, 256² (dốc alpha hai chiều) | ~328 KB | ~40 KB |
| `shapes.png`, `alpha.png` trên `ramp.png`, 256² | ~156 KB | ~24 KB |
| Dissolve (nửa điểm ảnh ngẫu nhiên), 256 × 384 | ~203 KB | ~151 KB |
| `photo.png` (ảnh chụp 256², RGB) | 343 KB | ~132 KB |
| Ảnh vào `p1/in/` (8 file) | 0,38 MB | |

`blend.jsx` ước khoảng 9 MB: 31 ảnh mode mới 16 bit 5,1 MB, `dissolve-50` 0,2 MB, `in-*` 0,4 MB, 85 ảnh 256 × 384 8 bit 1,7 MB, 26 `alpha-*` 1 MB, 21 ảnh trên `ramp.png` 0,5 MB, 2 dissolve 8 bit 0,3 MB. Còn khoảng 20 MB cho D32, F31 (và C41, S39, T38 nếu ảnh của họ tính vào cùng ngân sách): một ảnh chụp 8 bit 256² ~130 KB, nên 100 ảnh bộ lọc, adjustment trên `photo.png` đã khoảng 13 MB; ảnh trên `ramp.png`, `shapes.png` rẻ hơn nhiều.

### Chỗ chưa chắc (Action Manager)

Script dựng các phần sau bằng Action Manager theo mã ScriptListener quen dùng, nhưng chưa chạy thử. Phần nào không đúng với bản Photoshop của bạn thì chỉ các case dùng nó hỏng (báo trong `photoshop.txt`), các case khác vẫn làm; gửi lead `photoshop.txt`, lead sửa `lib.jsx` rồi bạn chạy lại riêng script đó.

- Mask từ alpha của một ảnh vào (case `mask*`, `group-*-mask`, `clip-multiply-mask`): chép layer ảnh vào sang tạm, nạp độ trong suốt làm vùng chọn (`setd` vùng chọn tới kênh `Trsp`), xóa layer tạm, Add Layer Mask › Reveal Selection (`Mk` kênh `Msk` dùng `RvlS`). Báo "no layer mask made" là bước này hỏng. Chưa chắc: vùng chọn từ alpha 16 bit có giữ đủ 16 bit không (test thấy ở các case có mask).
- Mask Density (`mask-density50`): khóa `userMaskDensity` (phần trăm) của layer. Khóa sai thì `setd` báo lỗi, hay ảnh ra như Density 100% (test so thấy ngay). Cùng cách: Feather `userMaskFeather` (px), tắt mask `userMaskEnabled`, bỏ liên kết `userMaskLinked` (chưa case nào dùng hai khóa sau).
- Blend If (`blendif-*`): danh sách `Blnd` của layer, mỗi kênh (`gray`, `red`, `green`, `blue` của `index.json` là `Gry`, `Rd`, `Grn`, `Bl`) tám số `SrcB`, `Srcl`, `SrcW`, `Srcm` (This Layer: đen trái, đen phải, trắng trái, trắng phải) và `DstB`, `Dstl`, `DstW`, `Dstt` (Underlying Layer).
- Kênh của Advanced Blending (`channels-*`): danh sách `channelRestrictions` các kênh còn bật.
- Adjustment layer Invert (`invert*`): `Mk` lớp `AdjL` kiểu `Invr`; script xóa mask trắng Photoshop tự thêm.
- Clipping vào một nhóm (`clip-base-group`): cần Photoshop cho clip vào nhóm (CC 2015 trở đi).
- Blend mode theo tên DOM (`BlendMode.SATURATIONBLEND`, `COLORBLEND`, `SUBTRACT`, `DIVIDE`…): thiếu tên nào thì script dừng ngay, báo tên đó.
- Xếp nhóm bằng DOM `move` (`PLACEBEFORE`, `INSIDE`); sai thứ tự thì case báo "the layers came out …".

## Cho người viết script (D32, F31, C41, S39, T38)

- Script P1 `#include "lib.jsx"`, liệt kê case trong `cases()` rồi `if (typeof app != 'undefined') PB.run('<tên>', cases(), {bits: 8});` (`bits` của ảnh ra; `inputs`: thư mục ảnh vào cạnh script, mặc định `in`; `repoInputs`: khóa `"inputs"` của `index.json`, chỗ ảnh vào trong repo tính từ thư mục ảnh ra, mặc định `../in`). Case là dữ liệu: `{name, canvas: [w, h], bits, layers: [...]}`, layer `{kind: 'pixel' | 'group' | 'adj', src, rotate, mask, props, children}`, `props` là JSON props của A30 (`to_json()` trong `core/doc.cpp`; `PB.normalise` xếp khóa theo thứ tự của nó và báo khóa lạ): `blend` theo `kBlendNames`; `opacity`, `fill`, `maskDensity` 0–1; `maskFeather` px; `visible`, `clip`, `maskOff`, `maskLinked`; `blendIf` `{gray|red|green|blue: {layer: [b0, b1, w0, w1], under: [...]}}` 0–255, chỉ kênh khác 0, 0, 255, 255; `channels` `{r|g|b: false}`, chỉ ô tắt; `adj` (tên `AdjustSpec.ORDER`), `vals`. Đầu `lib.jsx` ghi đủ. Thêm điều `lib.jsx` chưa biết mà không sửa nó: `PB.adjustments[tên]` (descriptor của một adjustment), `PB.kinds[kind]`, `PB.layerKeys[khóa]` (như `fx`), `PB.steps[khóa]` (khóa của case, như một bộ lọc áp lên layer đã dựng).
- Script ngoài `p1/` (`strokes.jsx`…) `#include "p1/lib.jsx"`: hoặc cũng dựng case bằng `PB.run('strokes', cases(), {bits: 16, inputs: 'strokes-in', repoInputs: '.'})` (ảnh ra ở `out\strokes\`), hoặc dùng phần thấp: Color Settings (`PB.colorSettings`, `PB.checkBlending`, `PB.checkDither`), `PB.input`, `PB.save`, `PB.json`, `PB.indexText`.
- Kiểm cú pháp không có Photoshop: chương trình `jscheck` (Qt, xem `build/orch/api/b30.md`): `jscheck <script>.jsx` báo lỗi cú pháp và thứ ES3 (ExtendScript) không có; `jscheck --eval "PB.offline('<tên>', cases(), {bits: 8})" <ra.json> <script>.jsx` in `index.json` script sẽ ghi, để so với `index.json` mẫu viết tay của test.
- Test đọc ảnh mẫu bằng `tests/golden.h`: `golden::Set("p1/<tên>")`, `ready()`, `entries()`, `layers()`, `image()`, `compare()`, `print()`; bỏ qua có lý do khi thiếu ảnh.
