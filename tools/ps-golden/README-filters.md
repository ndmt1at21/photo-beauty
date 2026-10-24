# Ảnh mẫu Photoshop: bộ lọc, style, chữ, hình, smart object (F31)

Hai script P1 trong `p1/`, dùng `p1/lib.jsx` của B30 (mục P1 của `README.md`), bạn chạy một lần ở B33 cùng các script ảnh mẫu khác:

- `p1/filters.jsx`: mọi bộ lọc cột P0–P1 của menu Filter (Blur, Sharpen, Noise, Other) với 2–5 bộ tham số, cho F32–F38. 81 case, 82 ảnh (cả ảnh nhiễu giữ lại cho Reduce Noise), khoảng 2,3 MB, khoảng 6 phút.
- `p1/styles.jsx`: bốn layer style của P1, chữ Space Grotesk, hình, smart object, mặt nạ Feather, cho L30–L35, Y30, Y32, B36. 74 case, khoảng 1,0 MB, khoảng 5 phút.

Mỗi case dựng trong tài liệu RGB 16 bit sRGB mới (như B30), ảnh ra 8 bit (ảnh ghép 16 bit làm tròn, không dither: câu 6 của "Cần bạn chốt"), trừ ảnh bộ lọc trên `impulse.png`: 16 bit, vì đuôi của nhân dưới một mức 8 bit và các ảnh đó chỉ vài KB (ngoại lệ có chủ ý của câu 6, lead đã nhận). Chưa chạy trên Photoshop thật: chỗ chưa chắc ở mục cuối; phần nào sai với bản Photoshop của bạn thì chỉ case dùng nó hỏng (lý do trong `photoshop.txt` và mục `failed` của `index.json`), các case khác vẫn ra ảnh.

## Các bước

1. Chép script và ảnh vào sang Windows như bước 1 mục P1 của `README.md` (cả thư mục `p1`: script đọc `lib.jsx` cạnh nó).
2. Font cho ảnh chữ của `styles.jsx`: chép `ui/fonts/SpaceGrotesk.ttf` và `ui/fonts/SpaceGrotesk-SemiBold.ttf` sang Windows (`cp ui/fonts/SpaceGrotesk*.ttf /mnt/c/Users/Teko/Downloads/ps-golden/`), nhấp phải từng file › Install for all users, rồi mở lại Photoshop.
   - `SpaceGrotesk.ttf` là font biến thiên (trục `wght` 300–700; bản có tên: Light, Regular, Medium, Bold). Tên PostScript ghi trong file (`name` ID 6) là `SpaceGrotesk-Light`, của bản mặc định; các bản có tên không có tên PostScript riêng (bảng `fvar` không ghi), nên Photoshop tự đặt, thường là `SpaceGrotesk-Regular` cho Regular. `SpaceGrotesk-SemiBold.ttf` là font tĩnh, tên PostScript `SpaceGrotesk-SemiBold`, họ Space Grotesk (ID 16), kiểu SemiBold (ID 17; ID 1, 2 là "Space Grotesk SemiBold", "Regular").
   - Script tìm font theo tên PostScript (`SpaceGrotesk-Regular`, `SpaceGrotesk-SemiBold`); không thấy thì theo họ "Space Grotesk" và kiểu ("Regular", "SemiBold"), hay họ "Space Grotesk SemiBold"; tên Photoshop dùng vào `photoshop.txt` và `index.json` (khóa `font` của `source`). Không có Space Grotesk thì case chữ hỏng, báo các font Grotesk Photoshop thấy: script không thay font khác (ảnh của font khác vô dụng cho Y30).
3. Photoshop: Color Settings như bước 3 mục P1 của `README.md` (script tự kiểm, như mọi script P1). Thêm cho `styles.jsx`:
   - Nếu từng bấm Make Default trong hộp Layer Style, mở lại hộp đó, bấm Reset to Default ở từng hiệu ứng (Drop Shadow, Stroke, Color Overlay, Gradient Overlay): bốn case `defaults-*` đọc mặc định của Photoshop.
   - Image Interpolation (Preferences › General): script đặt Bicubic Automatic (mặc định của Photoshop) trong lúc chạy, xong trả lại; `photoshop.txt` ghi giá trị trước và trong lúc chạy.
4. File › Scripts › Browse… → `ps-golden\p1\filters.jsx` → Open, chờ "Done"; rồi `styles.jsx`. Thông báo cuối như bước 4 của `README.md` ("Done: N images[, M failed]", "Stopped: …").
5. Chép kết quả như bước 5 của `README.md`: `out\filters\`, `out\styles\` thành `tests/golden/ps/p1/filters/`, `tests/golden/ps/p1/styles/`; `build/default/pb-tests test_golden_ps` (đủ ảnh theo `index.json`, mọi cảnh đọc được, cả `tests/golden/ps/p1/` ≤ 30 MB). Case hỏng thì gửi lead `photoshop.txt` của script đó; lead sửa script, bạn chạy lại riêng script đó.

## Case của `filters.jsx`

Mỗi case một layer (ảnh vào của B30, `tests/golden/ps/p1/in/`) và các bước lọc của nó. Tên case: `<bộ lọc>-<ảnh vào>[-<tham số>]` (`r` radius, `t` threshold, `a` angle hay amount, `d` distance; `0_5` là 0,5).

| Bộ lọc | Case | Cho |
| --- | --- | --- |
| Average | `photo`; `ramp` trong chữ nhật (vùng chọn); `shapes` (alpha vào trung bình) | F33 |
| Blur, Blur More, Sharpen, Sharpen More | `impulse` (nhân, 16 bit); `shapes` (mép nội dung) | F33 |
| Box Blur | `impulse` r 1, 4 (16 bit); `photo` r 10; `shapes` r 6 | F33 |
| Gaussian Blur | `impulse` r 0,5, 1, 2, 5 (16 bit: k của `sigma_of_radius`); `photo` r 10, 50, 250 (mép canvas); `shapes` r 4; `alpha` r 8; `shapes` r 3 trong vùng chọn mềm từ alpha của `alpha.png` (F30: trộn theo vùng chọn) | F32, F30 |
| Motion Blur | `impulse` góc 0 dài 10 và 9, 30/20, −45/15, 90/12 (16 bit); `photo` 30/25; `shapes` 0/20 | F33 |
| Surface Blur | `photo` r 5 t 15, r 12 t 40; `impulse` r 6 t 255 (16 bit: hình, trọng số của cửa sổ); `shapes` r 8 t 60; `ramp` r 10 t 20 | F34 |
| Sharpen Edges | `impulse` (16 bit), `photo`, `ramp` | F32 |
| Unsharp Mask | `photo` 120 % 1,2 3 (mặc định), 300 % 3 0; `ramp` 200 % 4 8 (ngưỡng); `impulse` 100 % 2 0 (16 bit); `shapes` 150 % 4 0 | F32 |
| Add Noise | trên xám 50 % của `impulse.png` giữa hai hàng chấm (vùng chọn [0, 145, 256, 31], không chấm nào): Gaussian 10 % Monochromatic, Gaussian 10 % màu, Uniform 10 % Monochromatic, Gaussian 25 % Monochromatic | F37 (thống kê) |
| Despeckle | `photo`, `impulse` (16 bit), `ramp` | F35 |
| Dust & Scratches | `photo` r 3 t 20; `impulse` r 2 t 0 (16 bit); `ramp` r 4 t 10 | F35 |
| Median | `photo` r 3, 10; `shapes` r 6 (hình cửa sổ ở góc) | F35 |
| Reduce Noise | `photo` mặc định; `photo` Strength 10, Preserve Details 0, Reduce Color Noise 100, Sharpen Details 0, Remove JPEG Artifact; `ramp-noisy`: Add Noise Gaussian 8 % màu vào giữa `ramp.png` rồi Reduce Noise mặc định, ảnh nhiễu giữ ở `reduceNoise-ramp-noisy-in.png` | F37 |
| High Pass | `photo` r 10; `impulse` r 2 (16 bit); `shapes` r 4 | F32 |
| Maximum, Minimum | `impulse` Maximum r 4 vuông, tròn (16 bit); `photo` Maximum r 3 tròn, Minimum r 3 vuông; `shapes` Maximum r 6 vuông, Minimum r 6 vuông, tròn; `alpha` Maximum r 8 vuông | F36 |
| Offset | `ramp` (64, 32) Wrap, (−100, 50) Repeat, (300, 0) Wrap; `shapes` (40, −30) Transparent; `shapes` dời (96, 0) ra ngoài canvas trước rồi (−64, 0) Wrap | F38 |
| Custom | `impulse` một số 1 ở góc trên phải của 5 × 5 (chiều áp nhân: tương quan dời chấm xuống trái), nhân mặc định (16 bit); `ramp` đạo hàm ngang, Offset 128; `shapes` hộp 3 × 3, Scale 9 | F38 |

## Case của `styles.jsx`

| Nhóm | Case | Cho |
| --- | --- | --- |
| Mặc định của Photoshop | `defaults-dropShadow`, `-stroke`, `-colorOverlay`, `-gradientOverlay`: hiệu ứng chỉ bật, mọi giá trị là của Photoshop, đọc lại vào `index.json` (Drop Shadow thêm `globalLight`, `globalAngle` khi mặc định dùng Global Light) | L30 |
| Drop Shadow | trên `shapes.png`, canvas trong suốt (alpha là độ phủ), đen Multiply 75 %: góc 120 dài 12 cỡ 24; 30/20/10; −90/8/4; 45/10/0 (bóng cứng); Spread 50 % cỡ 20, Spread 100 % cỡ 12; dài 0 cỡ 50; mặc định của giao diện giả 60 % trên `ramp.png` | L32 |
| Stroke | Size 1, 3, 10, 50 × Outside, Inside, Center; Center 6 Multiply 50 % trên `ramp.png` | L31 |
| Color Overlay | trên `ramp.png`: Normal, Multiply, Screen, Overlay, Difference, Luminosity; Normal 60 % | L34 |
| Gradient Overlay | trên `ramp.png`: Linear, Radial, Angle, Reflected, Diamond; góc 30, Scale 50 %, Reverse; Radial không Align with Layer, Scale 80 %, Offset (20, −10); Multiply 60 %; Perceptual với Smoothness 100 %. Gradient ba màu, opacity 100 → 40 %, điểm giữa 50 % (không vướng nghĩa `Mdpn`), Classic Smoothness 0 trừ case cuối | L34 |
| Fill, knockout, mask, tổ hợp | Fill 0 với Drop Shadow có và không Layer Knocks Out Drop Shadow; Fill 0 với Stroke và Color Overlay (Blend Interior Effects as Group tắt: overlay vẫn đủ); Opacity 50 %; Fill 50 % ở Color Dodge (luật 8 mode của B31) trên `ramp.png`; mặt nạ từ alpha của `alpha.png` (Layer Mask Hides Effects tắt); cả bốn hiệu ứng | L34 |
| Chữ | Space Grotesk Regular 32 pt, năm kiểu khử răng cưa (None, Sharp, Crisp, Strong, Smooth); 12 pt, 72 pt; tracking 200, −60; tiếng Việt "Tiếng Việt: ố ữ ặ ỷ" (hai dấu chồng, dấu dưới); SemiBold. Màu đen trên canvas trong suốt, mỗi case canvas vừa chữ | Y30 |
| Hình | chữ nhật bo 24, bo từng góc (0, 12, 40, 6); elip; lục giác; sao 5 cánh Star Ratio 50 %; Line dày 8; Stroke 10 px Inside, Center, Outside trên chữ nhật bo góc; elip chỉ Stroke 6 px | Y32 |
| Smart object | layer `photo.png` Convert to Smart Object, rồi thu 50 %, xoay 30° theo chiều kim đồng hồ, cả hai, quanh tâm canvas; Bicubic Automatic | L35 |
| Mặt nạ Feather | `ramp.png` với mặt nạ từ alpha của `shapes.png` (cạnh cứng, nét 1 px: chính nhân), Feather 10 và 3 px | B36 |

## `index.json`

Như của B30 (`build/orch/api/b30.md`, `tests/golden.h`): một ảnh một dòng, `canvas`, `bits` của PNG, `layers` dưới lên, `props` theo `to_json()` của A30. Thêm:

- **Bộ lọc** (`filters.jsx`): layer có `filters`, danh sách bước chạy theo thứ tự trên layer đó sau khi đặt props: `{name, vals, selection, keep}`.
  - `name`: khóa của `FilterSpec.FILTERS` cho bộ lọc có hộp thoại (`boxBlur`, `gaussianBlur`, `motionBlur`, `surfaceBlur`, `addNoise`, `dustScratches`, `median`, `reduceNoise`, `unsharpMask`, `custom`, `highPass`, `maximum`, `minimum`, `offset`); bộ lọc không hộp thoại theo tên mục menu viết camelCase (mục `@step:` của `MenuSpec.js`): `average`, `blur`, `blurMore`, `sharpen`, `sharpenMore`, `sharpenEdges`, `despeckle` (lead đã nhận: F30 dùng các tên này).
  - `vals`: mọi khóa của bộ lọc theo `FilterSpec.js` (khóa, đơn vị, khoảng, thứ tự hàng; mặc định của FilterSpec cho khóa case không nói): `radius` px, `threshold` mức 0–255, `amount` %, `angle` độ, `distance` px, `distribution` `uniform`/`gaussian`, `monochromatic`, `strength`, `preserveDetails`, `reduceColorNoise`, `sharpenDetails`, `removeJpegArtifact`, `kernel` 25 số nguyên theo hàng, `scale`, `offset`, `preserve` `squareness`/`roundness`, `horizontal`, `vertical`, `undefinedAreas` `transparent`/`repeat`/`wrap`. Bộ lọc không tham số: `{}`.
  - `selection` (có thể không có): vùng chọn của riêng bước đó, `[x, y, w, h]` (chữ nhật) hay tên ảnh vào (alpha của nó nạp làm vùng chọn mềm, cách của `PB.maskFrom`).
  - `keep` (có thể không có): ảnh ghép sau bước đó lưu thêm thành `<keep>.png`, 8 bit, cạnh `index.json`: đầu vào của bước sau (chỉ `reduceNoise-ramp-noisy`, vì nhiễu của Photoshop là ngẫu nhiên: F37 so Reduce Noise của mình từ ảnh nhiễu này).
  - `move` `[dx, dy]` của layer (có thể không có): layer dời trước các bộ lọc, phần ra ngoài canvas Photoshop vẫn giữ.
- **Layer style**: `props.fx` (khóa `fx` của A30) `{dropShadow, stroke, colorOverlay, gradientOverlay}`, mỗi hiệu ứng đủ mọi khóa, khóa và đơn vị như `LayerStyleDialog.qml` và L30: `on`, `blend` (`kBlendNames`), `color` `#rrggbb`, `opacity` %, Drop Shadow `angle` độ (120: sáng từ trên trái), `distance` px, `spread` %, `size` px, `noise` %, `knockOut`; Stroke `size` px, `position` `outside`/`inside`/`center`; Gradient Overlay `gradient` (JSON gradient của D30: `colors` `[{p, c: [r, g, b]}]`, `alphas` `[{p, a}]`, `smoothness`, `name`), `method` (`perceptual`, `linear`, `classic`, `smooth`, `stripes`), `style` (`linear`, `radial`, `angle`, `reflected`, `diamond`), `angle`, `scale` %, `reverse`, `dither`, `align`, `offset` [x %, y %]. Giao diện giả giữ gradient là chỉ số preset `[nhóm, chỉ số]`: ở đây là JSON của D30 như L30 ghi. Không Global Light (P2: script tắt `uglg`), Contour Linear, không anti-alias, noise 0. Case `defaults-*`: offline `{"on": true}`, sau khi chạy là giá trị Photoshop.
- **Chữ**: `kind` `text`, `props.source` `{text, font (tên PostScript Photoshop đã dùng), family, style, size (pt = px ở 72 ppi), color, tracking (phần nghìn em), aa (none, sharp, crisp, strong, smooth), at [x, y] (đầu đường chân chữ của point text)}`, dạng của `psd.jsx` (E33) thêm `family`, `style` (lead đã nhận; Y30 chưa chốt dạng của mình: test Y30 dịch nếu khác). Mọi thiết lập Character khác ở mặc định (kerning Metrics, leading Auto, co 100 %, baseline 0, không faux, không gạch, căn trái).
- **Hình**: `kind` `shape`, `props.source` dạng của `psd.jsx`: `{type (rectangle, ellipse, polygon, line), box [trái, trên, phải, dưới], radii [tl, tr, br, bl], sides, star %, from, to, weight, fill (#rrggbb hay null), stroke ({width, color, align, cap, join} hay null)}`.
- **Smart object**: `kind` `so` (`Mock.KINDS`, A31), `src` là ảnh vào làm nội dung (layer của nó Convert to Smart Object, lead đã nhận: nội dung đúng các điểm ảnh đó, sRGB 16 bit; không Place vì ảnh vào không gắn profile, Place hiểu nó theo không gian làm việc của Color Settings và có thể co theo Resize Image During Place), `place` `{scale %, rotate (độ, chiều kim đồng hồ)}` quanh tâm canvas, `props.transform` (A31: ma trận 3 × 3 theo hàng từ điểm ảnh của nội dung sang tài liệu, x' = m0 u + m1 v + m2, y' = m3 u + m4 v + m5, góc điểm ảnh ở số nguyên, 6 chữ số lẻ). Offline là ma trận script tính (T(tâm) · R · S · T(−tâm)); sau khi chạy là ma trận đọc lại từ bốn góc Photoshop báo (lệch quá 0,01 px thì `photoshop.txt` ghi). Trước A31, `props_from_json` để `transform` vào `extra`.
- **Mặt nạ Feather**: khóa của B30 (`mask` là ảnh vào lấy alpha, `props.maskFeather` px).
- Mẫu viết tay: `tests/golden/ps/p1/sample/filters/index.json` (11 dòng), `tests/golden/ps/p1/sample/styles/index.json` (13 dòng), mỗi dạng ở trên một dòng, `"inputs": "../../in"` (ở `sample/`, không ở `filters/`, `styles/`: `test_golden_ps` coi hai thư mục đó là bộ thật và báo thiếu PNG; lead đã nhận): test đọc bằng `golden::Set set("p1/sample/filters")` khi chưa có ảnh Photoshop (thiếu PNG nên `image()` rỗng, `skipped()` báo). Mỗi dòng trùng từng byte với dòng cùng tên của `PB.offline` (kiểm bằng `jscheck`, xem `build/orch/api/b30.md`):

  ```sh
  jscheck --eval "PB.offline('filters', cases(), {bits: 8, repoInputs: '../../in'})" out.json tools/ps-golden/p1/filters.jsx
  ```

  rồi so từng dòng của mẫu với dòng cùng `name` trong `out.json`. Sửa case trong script thì sửa dòng mẫu tương ứng.

  Chạy thử cả `PB.run` không Photoshop: giả lập `build/orch/api/f31-psmock.jsx` (F31; nạp sau script để script không tự chạy) giữ cây layer, mặt nạ, style, biến đổi smart object, ghi `index.json`, `photoshop.txt` vào bộ nhớ: `jscheck --eval "HERE = new Folder('C:/x'); MOCK.run(function () { S.run(); })" out.txt tools/ps-golden/p1/styles.jsx f31-psmock.jsx` (`filters.jsx`: `PB.run('filters', cases(), {bits: 8, dir: HERE})`); `MOCK.reject = ['EdgF']` cho một khóa bị từ chối để thử cách khác của script. Không có điểm ảnh: chỉ bắt lỗi của mã và dòng chảy, không thay được Photoshop.

Đọc trong test: `golden::Set set("p1/filters")`; `set.layers(e)` cho ảnh vào (`pixels`, `mask`) và props; khóa riêng của F31 qua `layer.json` (`(*l.json)["filters"]`, `["move"]`, `["place"]`); ảnh 16 bit của Photoshop so qua `golden::ps15` (`tests/golden.h`); ảnh `keep` đọc bằng `golden::read_png(set.dir() / "reduceNoise-ramp-noisy-in.png")`.

## Cỡ ước tính

PNG 8 bit đo như B30 (zlib 9, bộ lọc dòng thích ứng như spng; cách lưu của Photoshop lệch ±15 %) trên bộ lọc gần đúng viết bằng Python: ảnh chụp sau bộ lọc làm nét, Median, Despeckle, High Pass 95–175 KB mỗi ảnh; Gaussian r 10 khoảng 45 KB, r 50 khoảng 10 KB; Add Noise trên dải 31 hàng 11–21 KB; ảnh `impulse.png` 16 bit 3–6 KB; `shapes.png` 8 bit 7–15 KB; `ramp.png` 1–3 KB; `shapes.png` trên `ramp.png` khoảng 10 KB, có bóng mềm khoảng 20 KB; smart object xoay 30° khoảng 125 KB, thu 50 % khoảng 45 KB; chữ, hình vài KB.

| Script | Ảnh | Cỡ |
| --- | --- | --- |
| `filters.jsx` | 82 (20 trên ảnh chụp: khoảng 1,7 MB) | khoảng 2,3 MB |
| `styles.jsx` | 74 | khoảng 1,0 MB |

Cả hai khoảng 3,3 MB, cộng `index.json`, `photoshop.txt` khoảng 0,1 MB: trong phần khoảng 4 MB của F31 trong 20 MB còn lại của lô 2 (câu 6 của "Cần bạn chốt"; B30 đã dùng khoảng 9,2 MB).

## Chỗ chưa chắc (Action Manager)

Script dựng các phần sau theo mã ScriptListener quen dùng hay DOM, chưa chạy thử. Phần nào sai chỉ làm hỏng case dùng nó.

- Bộ lọc DOM có sẵn (`applyAverage`, `applyBlur`, `applyMotionBlur`, `applyUnSharpMask`, `applyAddNoise`, `applyDespeckle`, `applyDustAndScratches`, `applyMedianNoise`, `applyHighPass`, `applyCustomFilter`, `applyOffset`…) chắc nhất. Gaussian Blur qua `GsnB` (`Rds ` px) vì `applyGaussianBlur` của DOM dừng ở 250.
- Box Blur `boxblur`, Surface Blur `surfaceBlur` (`Rds ` px, `Thsh`): tên sự kiện theo ScriptListener, chưa thử.
- Maximum, Minimum (`Mxm `, `Mnm `): ô Preserve (Photoshop 2019 trở đi) là `preserveShape` = `squareness`/`roundness`; hỏng thì Squareness thử lại không có khóa đó (hộp thoại cũ), Roundness hỏng hẳn. `photoshop.txt` ghi cách đã chạy.
- Reduce Noise (`denoise`): `ClNs` (Reduce Color Noise), `Shrp` (Sharpen Details), `removeJPEGArtifact`, danh sách `channelDenoise` một mục cho kênh tổng (`Amnt` Strength, `EdgF` Preserve Details); thử thêm `edgeFidelity` thay `EdgF`, rồi có `preset` "Default". Chế độ Basic. Chưa chắc preset có đè giá trị không: case `reduceNoise-photo-s10-…` khác hẳn `reduceNoise-photo` là đúng.
- Custom trên tài liệu 16 bit: theo tài liệu của Adobe Custom có ở 16 bit; nếu không, ba case `custom-*` hỏng (báo lead: chạy riêng chúng ở 8 bit).
- Add Noise dùng số ngẫu nhiên của Photoshop: chỉ so thống kê; `keep` giữ ảnh nhiễu cho Reduce Noise.
- Vùng chọn từ alpha 16 bit (`gaussianBlur-shapes-r3-softsel`, mặt nạ của `mask-*`): như chỗ chưa chắc của B30.
- Offset của layer có phần ngoài canvas (`offset-shapes-moved-m64-0-wrap`): ảnh cho biết Photoshop bỏ hay giữ phần đó.
- Mặc định của style (`defaults-*`): đặt hiệu ứng chỉ với `enab`, `present`, `showInDialog`, mong Photoshop điền phần còn lại bằng mặc định của nó. Nếu `setd` từ chối, bốn case đó hỏng; khi đó mở hộp Layer Style, bấm Reset to Default ở từng hiệu ứng và chép các giá trị cho lead.
- Gradient Overlay: Method `gradientsInterpolationMethod` (Photoshop 2023 trở đi); bản cũ hơn bỏ qua hay báo lỗi (case gradient hỏng, hay `photoshop.txt` ghi "not read back as set").
- Hình: Polygon (`polygon`, rồi `Plgn`), Line (`Ln ` với `Strt`, `End `, `Wdth`; rồi `line`) như `psd.jsx`, chưa chắc nhất; chữ nhật, elip chắc hơn.
- Chữ: `textItem.position` là đầu đường chân chữ của point text; Character đặt lại từng khóa (một khóa không đặt được thì `photoshop.txt` ghi, case vẫn chạy); tên PostScript của bản Regular trong font biến thiên (bước 2).
- Smart object: Convert to Smart Object (`newPlacedLayer`), `resize`, `rotate` của DOM quanh tâm khung của layer (tâm canvas); `rotate` dương là chiều kim đồng hồ (ma trận đọc lại cho biết); bốn góc đọc từ `smartObjectMore.transform`. Nội suy lấy từ Image Interpolation (script đặt Bicubic Automatic).
