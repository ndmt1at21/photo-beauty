# Ảnh mẫu Photoshop: adjustment, gradient (D32)

Hai script P1 trong `p1/`, dùng `p1/lib.jsx` của B30 (mục P1 của `README.md`), bạn chạy một lần ở B33 cùng các script ảnh mẫu khác:

- `p1/adjust.jsx`: 16 adjustment layer của P1 trên ảnh vào của B30 (`cube.png` lưới 33³, `ramp.png` dốc 256 mức, `photo.png` ảnh chụp), mỗi adjustment 3–10 bộ tham số (Brightness/Contrast 17: bản mới và Use Legacy); Image › Adjustments và các lệnh Desaturate, Equalize, Auto Tone, Auto Contrast, Auto Color; adjustment layer ở Luminosity và opacity 50 %; nút Auto của Levels, Curves, Brightness/Contrast; `presets.json` (preset của Photoshop). Cho D33–D41, D40. 137 case, khoảng 2,1 MB, khoảng 7 phút.
- `p1/gradient.jsx`: fill layer Gradient: 5 hình × 5 method; đường của fill trên khung không vuông (góc, Scale, Offset, Reverse, Align with Layer trên mask); điểm giữa; Smoothness; điểm độ mờ; Dither ở tài liệu 16 bit và 8 bit; fill trên điểm ảnh, Solid Color. Cho D30 (`test_gradient_ps`), D40, B38. 73 case, khoảng 0,5 MB, khoảng 3 phút.

Mỗi case dựng trong tài liệu RGB 16 bit sRGB mới (như B30), ảnh ra 8 bit (ảnh ghép 16 bit làm tròn, không dither: câu 6 của "Cần bạn chốt"), trừ ba ảnh `dither16*` của `gradient.jsx`: 16 bit, vì nhiễu dither của tài liệu 16 bit có thể dưới một mức 8 bit (ba ảnh nhỏ, 256 × 64). Chưa chạy trên Photoshop thật: chỗ chưa chắc ở mục cuối; phần nào sai với bản Photoshop của bạn thì chỉ case dùng nó hỏng (lý do trong `photoshop.txt` và mục `failed` của `index.json`), các case khác vẫn ra ảnh.

## Các bước

1. Chép script và ảnh vào sang Windows như bước 1 mục P1 của `README.md` (cả thư mục `p1`: `gradient.jsx` đọc `adjust.jsx`, cả hai đọc `lib.jsx` cạnh nó), thêm LUT thử của Color Lookup vào `p1\lut\`:

   ```sh
   mkdir -p /mnt/c/Users/Teko/Downloads/ps-golden/p1/lut
   cp tests/data/lut/* /mnt/c/Users/Teko/Downloads/ps-golden/p1/lut/
   ```

2. Photoshop 2023 (24) trở đi (Method của gradient: Perceptual, Linear, Classic; Smooth, Stripes ở bản mới hơn), bản 2025 (26) như B30. Color Settings như bước 3 mục P1 của `README.md` (script tự kiểm). Thêm:
   - Auto Options của Levels, Curves (nút Options… trong hộp Levels hay Curves) để mặc định của Photoshop (Enhance Brightness and Contrast, Snap Neutral Midtones tắt, Clip 0,1 %; nếu từng đổi và bấm Save as defaults thì đặt lại): ba case `auto-*` và `cmd-auto-*` dùng Auto Options như đang đặt.
   - Không cần font, không cần preset riêng: `presets.json` đọc preset có sẵn trong thư mục `Presets` cạnh Photoshop.
3. File › Scripts › Browse… → `ps-golden\p1\adjust.jsx` → Open, chờ "Done" (ghi `presets.json` trước, rồi các case); rồi `gradient.jsx`. Thông báo cuối như bước 4 của `README.md` ("Done: N images[, M failed]", "Stopped: …").
4. Chép kết quả như bước 5 của `README.md`: `out\adjust\` (cả `presets.json`), `out\gradient\` thành `tests/golden/ps/p1/adjust/`, `tests/golden/ps/p1/gradient/`; `build/default/pb-tests test_golden_ps` (đủ ảnh theo `index.json`, mọi cảnh đọc được, cả `tests/golden/ps/p1/` ≤ 30 MB), `build/default/pb-tests test_gradient_ps` (in độ lệch của D30 từng ảnh, chưa có ngưỡng). Case hỏng thì gửi lead `photoshop.txt` của script đó; lead sửa script, bạn chạy lại riêng script đó.

## Case của `adjust.jsx`

Mỗi case một layer ảnh vào và một adjustment layer ở trên (Normal 100 %, không mask), hay các bước Image › Adjustments, lệnh trên chính layer ảnh vào. Tên case: `<adjustment>-<tham số>-<ảnh vào>` (`m` là dấu trừ: `bm150` là b −150; `nopl` là Preserve Luminosity tắt; `abs` là Absolute).

| Adjustment | Case | Cho |
| --- | --- | --- |
| Brightness/Contrast | trên `ramp`: bản mới, b −150, −50, 50, 150; c −50, 50, 100; b 50 c 50; Use Legacy cùng các bộ đó (hộp thoại Legacy dừng ở ±100: b ±150 thành ±100) và c −100; b 50 c 50 bản mới trên `cube`, `photo`, Legacy trên `cube`; c 100 trên `photo` (bản mới có nhìn ảnh không) | D34 |
| Levels | trên `ramp`: gamma 0,5, 2; vào 20–230 gamma 1,3 ra 10–240; ra đảo 255–0; trên `ramp`, `cube`: từng kênh (Red in0 40; Green gamma 1,8; Blue in1 180, out0 30), RGB (in0 15, gamma 0,6) cùng Red (in0 60, gamma 1,5, out1 230: thứ tự áp) | D33 |
| Curves | trên `ramp`: chữ S; điểm sát biên (8, 32), (247, 223); đầu mút ở trong (30, 15) … (220, 245); 8 điểm; trên `ramp`, `cube`: Red, Blue riêng; RGB cùng Green (thứ tự áp) | D33 |
| Exposure | trên `ramp`: exposure +1, −1,5; offset +0,1, −0,05; gamma 0,5, 2; cả ba (0,7; −0,02; 1,3) cả trên `cube` | D34 |
| Vibrance | trên `cube`: vibrance 50, −50, 100; saturation 50, −50; vibrance 60 với saturation −30 | D35 |
| Hue/Saturation | trên `cube`: Master hue 60, saturation 50, −100, lightness 40, −40; Reds (30, 40, −10); ba dải Yellows, Cyans, Blues (cả `photo`); Greens với span riêng [60, 90, 150, 200]; Colorize (200, 40), (30, 70, −20) | D35 |
| Color Balance | trên `cube`: Shadows, Midtones (cyan–red +100, có và không Preserve Luminosity), Highlights; cả ba vùng (cả `photo`), có và không Preserve Luminosity | D36 |
| Black & White | trên `cube`: mặc định; giá trị preset Red Filter; quá biên hộp thoại (300, −200…); Tint màu mặc định của AdjustSpec; Tint `#3a6ea5` | D37 |
| Photo Filter | trên `cube`: bộ lọc mặc định (script không đưa màu: Photoshop tự chọn, màu đọc lại vào `vals` nếu khác `#ec8a00`); Underwater Density 60; màu tự chọn `#006dff` 50, `#ea1a1a` 75 Preserve Luminosity tắt, `#19c919` 100, `#ac7a33` 40 tắt | D37 |
| Channel Mixer | trên `cube`: đổi Red, Green; trộn có số âm; Constant +30, −40; quá biên (200, −200); Monochrome; Monochrome có Constant | D37 |
| Color Lookup | trên `cube`: `identity-17.cube`, `identity-17.3dl`, `invert-17.cube`, `film-9.cube` (cả `photo`), `film-9.3dl`, `film-9.cube` có Dither | D39 |
| Invert | trên `cube` | D38 |
| Posterize | 2, 4, 7 trên `ramp` (biên các bậc), `cube` | D38 |
| Threshold | 64, 128, 200 trên `ramp`, `cube` (hệ số Lum) | D38 |
| Gradient Map | ba điểm của AdjustSpec (`#1b1020`, `#9e2f45`, `#e9c7b3`) × 5 method trên `ramp` (hàng xám là gradient ở 256 điểm; Perceptual cả trên `cube`: hệ số Lum); Reverse; Dither; điểm độ mờ (Gradient Map có dùng độ mờ không); điểm giữa riêng từng điểm, cả điểm cuối (Classic, Smoothness 0) | D38, D30 |
| Selective Color | trên `cube`: Reds Relative, Absolute; Neutrals; Whites, Blacks Absolute; năm màu Yellows…Magentas Relative (cả `photo`), Absolute | D36 |
| Lệnh (`cmd-*`, `apply-*`) | Desaturate (`cube`); Equalize (`cube`, `photo`); Auto Tone, Auto Color (`photo`: lục, lam chưa tới trắng); Auto Contrast trên `photo` đã nén bằng Levels ra 40–200 (đỏ của `photo.png` đã đủ 0–255); Image › Adjustments › Hue/Saturation hue 60 (`cube`: so với `hue-h60-cube`) | D41, D31 |
| Mode, opacity (`lum-*`, `opacity50-*`) | adjustment layer ở Luminosity: Curves chữ S trên `photo`, Hue 60, Levels gamma 0,5 trên `cube`; Curves chữ S ở opacity 50 % trên `cube` | D40 |
| Auto (`auto-*`) | nút Auto của Levels, Curves, Brightness/Contrast trên `photo` (Auto Options như đang đặt); `vals` sau khi chạy là giá trị Photoshop tính (offline không có) | D41 |

## Case của `gradient.jsx`

Mỗi case một fill layer trên tài liệu trong suốt (không có layer nào dưới, trừ hai case cuối), khung 256 × 160 (không vuông: độ dài của đường theo góc mới hiện ra) trừ chỗ ghi. Ba điểm `G3`: đỏ (230, 40, 30) ở 0, vàng (250, 220, 40) ở 0,5, lam (20, 60, 220) ở 1, Smoothness 100 % (mặc định của Photoshop); case hình học dùng đen → trắng Classic Smoothness 0 (giá trị chính là t, đọc hình học thẳng từ ảnh). Tên: `<hình>-<method>`, `-a<góc>` (`m` là âm), `-s<Scale>`.

| Nhóm | Case | Chốt |
| --- | --- | --- |
| 5 hình × 5 method | `linear`, `radial`, `angle`, `reflected`, `diamond` × `perceptual`, `linear`, `classic`, `smooth`, `stripes`, ba điểm `G3`, góc 90°, Scale 100 % | trộn của từng method (D30: Perceptual trong Oklab, Linear trong ánh sáng tuyến tính, Smooth trọng smoothstep, Stripes đổi ở `kStripesAt` 0,5); Smoothness 100 % có tác dụng ngoài Classic không |
| Góc | `linear-a0`, `-a30`, `-a45`, `-a135`, `-a150`, `-am60`; `radial-a30`; `angle-a30`, `angle-am120`; `reflected-a45`; `diamond-a30` | `fill_span`: độ dài \|w cos a\| + \|h sin a\|; Linear từ đầu tới cuối, hình khác từ tâm, nửa độ dài; chiều của Angle (ngược kim đồng hồ); Radial, Diamond có xoay theo góc không |
| Scale | `linear-s50`, `-s150`; `radial-s50`, `-s150`; `diamond-s150` | Scale nhân độ dài |
| Offset | `linear-a30-offset` (20 %, −10 %), `radial-offset` (−25 %, 15 %), `angle-offset` (10 %, 10 %) | Offset theo % khung, phải và xuống |
| Reverse | `linear-a30-reverse`, `radial-reverse`, `angle-reverse` | Reverse là t → 1 − t |
| Align with Layer | `align-on`, `align-off`: Radial trên mask từ alpha của `shapes.png`, khung 256² | D40: khung là biên của mask hay canvas |
| Điểm giữa | `mid-bw` (đen m 0,25, trắng m 0,8); `mid3-classic`, `-perceptual`, `-linear` (m 0,2; 0,75; 0,35); `mid3-stripes` (0,3; 0,7; 0,5); `mid-extremes` (0,05; 0,95; 0,5); `mid-alpha` (điểm độ mờ m 0,3; 0,7) | `Mdpn` của điểm dừng thuộc đoạn nào (ghi chú lead 25/10), `midpoint_weight` |
| Smoothness | `smoothness-classic-0`, `-classic-50`, `-perceptual-0`, `-linear-0`, `-smooth-0` (ba điểm `G3`; so với ô của lưới ở 100 %) | `smoothness_weight` chỉ cho Classic |
| Điểm độ mờ | `alpha-<method>` × 5: `G3` với độ mờ đục, 20 %, đục | độ mờ trộn theo method (D30: tuyến tính, Classic theo Smoothness, Smooth smoothstep, Stripes giữ) |
| Dither | `dither16`, `dither16-off` (256 × 64, Linear 30° đen → trắng, ảnh 16 bit có và không Dither: tách nhiễu khỏi làm tròn), `dither16-radial` (ba điểm, 16 bit); `dither8` (tài liệu 16 bit, ảnh 8 bit); `dither8-doc` (tài liệu đổi sang 8 bit trước khi lưu: fill dither ở 8 bit) | `kDitherLevels8`, `kDitherLevels16` (D30: ±0,5 mức 8 bit ở cả hai độ sâu) |
| Trên điểm ảnh | `over-ramp-multiply` (ba điểm, 30°, Multiply 60 % trên `ramp.png`, 256²); `solid-ramp` (Solid Color `#3a7bd5` 70 % trên `ramp.png`) | D40: fill layer ghép như layer điểm ảnh |

## `index.json`

Như của B30 (`build/orch/api/b30.md`, `tests/golden.h`): một ảnh một dòng, `canvas`, `bits` của PNG, `layers` dưới lên, `props` theo `to_json()` của A30 (`PB.normalise` xếp khóa). Thêm:

- **Adjustment layer**: `kind` `adj`, `props.adj` (tên `AdjustSpec.ORDER`), `props.vals` theo khóa của `AdjustSpec.js` (D31 đọc khóa thiếu là mặc định của AdjustSpec; case chỉ ghi khóa nó đặt; không có `vals` là mặc định cả). Nơi Photoshop giữ một bộ giá trị cho mỗi kênh, dải, vùng, màu mà AdjustSpec chỉ có một bộ và một ô chọn, `vals` có một object cho mỗi cái theo tên trong ô chọn (như `psd.jsx` của E33, `README-psd.md`; D33 ghi dạng Levels):
  - Levels `{RGB, Red, Green, Blue: {in0, gam, in1, out0, out1}}` (mức 0–255); Curves `{pts: {RGB, Red, Green, Blue: [[vào, ra], …]}}` (0–255, tăng dần);
  - Hue/Saturation `{Master, Reds, Yellows, Greens, Cyans, Blues, Magentas: {h, s, l, span}, colorize}` (`span` [a, b, c, d] độ, thiếu là `AdjustSpec.SPANS`; Colorize: `h` 0–360, `s` 0–100 của Master là màu);
  - Color Balance `{Shadows, Midtones, Highlights: {cr, mg, yb}, pl}`; Channel Mixer `{Red, Green, Blue: {r, g, b, k}}` (% ; `k` Constant), Monochrome là `{mono: true, Gray: {r, g, b, k}}`; Selective Color `{Reds, Yellows, Greens, Cyans, Blues, Magentas, Whites, Neutrals, Blacks: {c, m, y, k}, rel}` (`rel` theo ô SEG của AdjustSpec: 0 Relative, 1 Absolute);
  - còn lại như AdjustSpec: Brightness/Contrast `b`, `c`, `legacy`; Exposure `e`, `o`, `g`; Vibrance `v`, `s`; Black & White `r`, `y`, `g`, `c`, `b`, `m` (%), `tint`, `tintColor`; Photo Filter `pf` (0: bộ lọc `pf0` theo tên của `AdjustSpec.LISTS.pfilter`, 1: màu `pf1`), `d` Density %, `pl`; Color Lookup `lf0` (tên file trong `tests/data/lut/`; D31 giữ nội dung file ở khóa `lut` của `vals`: test đọc file rồi đặt `lut`), `dither`; Posterize `lv`; Threshold `t`; Gradient Map `gradient` (JSON gradient của D30), `method` (chữ như AdjustSpec: `Perceptual`…), `dither`, `rev`.
  - Photo Filter theo tên: màu công bố của bộ lọc (`presets.json` › `Photo Filter`), trừ bộ lọc mặc định (`pfilter-default-cube`: script không đưa màu, Photoshop tự chọn; màu đọc lại khác `#ec8a00` quá 2 mức thì `vals` của case thành `{pf: 1, pf1: màu đọc lại}`).
  - Layer có `"auto": true` (`auto-*`): script bấm Auto rồi ghi giá trị Photoshop tính vào `vals` (offline không có `vals`).
- **Case có `apply`** (`cmd-*`, `apply-*`): `[{adj, vals} | {command}]`, các bước theo thứ tự trên layer ảnh vào (layer trên cùng): Image › Adjustments › `adj` với `vals` như trên, hay lệnh `desaturate`, `equalize`, `autoTone`, `autoContrast`, `autoColor` (Image › Auto Tone…, Auto Options như đang đặt). Mỗi bước phải đổi ảnh (script so histogram).
- **Fill layer** (`gradient.jsx`): `kind` `fill`, `props.content` là `GradientFill` của D30 đủ mọi khóa (`gradient` JSON của D30, `method` chữ thường, `style`, `angle`, `scale`, `reverse`, `dither`, `align`, `offset`: `gradient_fill_from()` đọc), hay `{color: "#rrggbb"}` cho Solid Color (D40). `mask` như B30 (`align-*`: alpha của ảnh vào làm mask, script xóa mask trắng Photoshop tự thêm). Case có `"depth": 8` (`dither8-doc`): tài liệu đổi sang 8 bit sau khi dựng, trước khi lưu. `bits` 16: `dither16*`.
- Điểm giữa: `m` của mỗi điểm theo nghĩa của D30 (đoạn từ điểm đó tới điểm sau); script ghi `Mdpn` của mỗi điểm bằng `m` của chính nó như `psd.jsx` (E33). Ảnh của các case điểm giữa cho biết Photoshop dùng `Mdpn` của điểm nào cho mỗi đoạn: khớp `render_gradient()` của D30 thì D30, E33 cùng đúng; khớp khi mỗi đoạn lấy `m` của điểm cuối đoạn (với Photoshop `Mdpn` của một điểm là điểm giữa của đoạn từ điểm trước tới nó) thì sửa bên sai như ghi chú lead (`photoshop.txt` ghi `Mdpn` Photoshop đọc lại, có thể khác ở điểm đầu hay cuối, chỉ ghi chứ không hỏng case).
- Mẫu viết tay: `tests/golden/ps/p1/sample/adjust/index.json` (23 dòng: mỗi adjustment, mỗi dạng `vals`, `apply`, `auto`, Luminosity, opacity), `tests/golden/ps/p1/sample/gradient/index.json` (14 dòng: lưới, góc, Scale, Offset, Reverse, Align trên mask, điểm giữa, điểm độ mờ, Smoothness, `bits` 16, `depth`, fill trên điểm ảnh, Solid Color), `"inputs": "../../in"` (ở `sample/` như F31, vì `test_golden_ps` coi `p1/adjust`, `p1/gradient` là bộ thật và báo thiếu PNG): test đọc bằng `golden::Set set("p1/sample/adjust")` khi chưa có ảnh Photoshop (`image()` rỗng, `skipped()` báo). Mỗi dòng trùng từng byte với dòng cùng tên của `PB.offline`:

  ```sh
  jscheck --eval "PB.offline('adjust', cases(), {bits: 8, repoInputs: '../../in'})" out.json tools/ps-golden/p1/adjust.jsx
  jscheck --eval "PB.offline('gradient', cases(), {bits: 8, repoInputs: '../../in'})" out.json tools/ps-golden/p1/gradient.jsx
  ```

  rồi so từng dòng của mẫu với dòng cùng `name` trong `out.json` (dòng đầu cũng trùng). Sửa case trong script thì sửa dòng mẫu tương ứng.

## `presets.json` (`out/adjust/`, ghi trước các case)

```json
{"photoshop": "26.x", "folder": "C:\\Program Files\\Adobe\\Adobe Photoshop 2025\\Presets",
 "Levels": {"Darker": {...}, ...}, "Curves": {...}, "Hue/Saturation": {...}, "Black & White": {...}, "Exposure": {...},
 "defaults": {"Brightness/Contrast": {...}, ..., "Color Lookup": null, "Invert": null, "Gradient Map": null},
 "Photo Filter": {"Warming Filter (85)": "#ec8a00", ...},
 "failed": [{"preset": "...", "error": "..."}]}
```

- Mỗi file trong `Presets\Levels`, `Curves`, `Hue Sat`, `Black and White`, `Exposure` cạnh Photoshop, theo tên file bỏ đuôi: nạp vào một adjustment layer mới, đọc lại, ghi `vals` dạng `index.json` (khóa của AdjustSpec, object theo kênh, dải), bỏ phần bằng mặc định của AdjustSpec.
- `defaults`: mỗi adjustment Photoshop tạo khi không đưa giá trị nào, mọi giá trị đọc lại (Photo Filter: màu bộ lọc mặc định của Photoshop ở `pf1`; null cho adjustment không đọc giá trị).
- `Photo Filter`: màu công bố của 16 bộ lọc (`AdjustSpec.LISTS.pfilter`), không đọc từ Photoshop (script không chọn bộ lọc theo tên được); màu bộ lọc mặc định thật ở `defaults`.
- Giao diện thay `AdjustSpec.PRESET_VALS` bằng các bộ này (dạng khác bản giả: Levels theo kênh `{RGB: {...}}` thay một bộ, Hue/Saturation `{Reds: {s}}` thay `{rng, s}`: N37 dịch); D33 lấy preset Levels, Curves; D37 màu Photo Filter. Thư mục hay file không đọc được vào `failed`, các case vẫn chạy.

## LUT thử (`tests/data/lut/`)

Năm file cho Color Lookup (`adjust.jsx` đọc ở `p1\lut\`) và D39 (đọc file):

| File | Nội dung | Định dạng |
| --- | --- | --- |
| `identity-17.cube` | đơn vị, 17 điểm mỗi chiều | `.cube` (Adobe, Resolve): chú thích `#`, `TITLE "…"`, `LUT_3D_SIZE 17`, 17³ dòng `R G B` (6 chữ số lẻ), đỏ đổi nhanh nhất |
| `invert-17.cube` | 1 − v mỗi kênh, 17 điểm | như trên |
| `identity-17.3dl` | đơn vị, 17 điểm | `.3dl`: dòng đầu là lưới vào 10 bit `0 64 128 … 960 1023`, rồi 17³ dòng số nguyên 12 bit (0–4095), lam đổi nhanh nhất |
| `film-9.cube` | "phim" tự làm, 9 điểm | `.cube` có `DOMAIN_MIN 0.0 0.0 0.0`, `DOMAIN_MAX 1.0 1.0 1.0`; mọi giá trị là mức 12 bit n/4095 |
| `film-9.3dl` | cùng số với `film-9.cube` | `.3dl` lưới `0 128 … 896 1023`, 12 bit |

Công thức phim (r, g, b của điểm lưới, 0–1; các kênh khác nhau để LUT đọc sai thứ tự là thấy): y = 0,2126 r + 0,7152 g + 0,0722 b; s(v) = 0,75 v + 0,25 v² (3 − 2v); c = 0,035 + 0,93 s(v) mỗi kênh; giảm bão hòa c = Y(c) + 0,85 (c − Y(c)) (Y cùng hệ số); rồi R += 0,05 y − 0,04 (1 − y), G += 0,015 y + 0,01 (1 − y), B += −0,05 y + 0,045 (1 − y); kẹp 0–1, làm tròn tới n/4095. Đã đọc lại bằng một bộ đọc viết riêng: hai đơn vị đúng tới 0 (`.3dl` tới 1/4095 của làm tròn 12 bit), đảo đúng tới 0, hai file phim trùng nhau tới 5·10⁻⁷.

## Cỡ, thời gian ước tính

PNG 8 bit đo như B30 (zlib 9, bộ lọc dòng thích ứng như spng; cách lưu của Photoshop lệch ±15 %) trên adjustment, gradient gần đúng viết bằng Python: `cube.png` sau adjustment 1,4–6,6 KB, `ramp.png` 0,5–1,3 KB, `photo.png` 68–134 KB; có Dither (nhiễu một mức) thêm khoảng 15–25 KB một ảnh; gradient vẽ gần đúng như D30 từ index offline (đường `fill_span`, trộn sRGB): 256 × 160 Linear, Reflected, Diamond, Stripes 0,4–2,5 KB, Radial 5–13 KB, Angle 6–15 KB, Radial trên mask 256² 16 KB, Dither 8 bit khoảng 25 KB, 256 × 64 16 bit có Dither khoảng 75 KB (không Dither 1,3 KB). Vẽ từng case bằng `render_gradient()` của D30 (ghép `composite_tile()`, lưu như `golden::write_png`): `gradient.jsx` 0,47 MB.

| Script | Ảnh | Cỡ | Thời gian |
| --- | --- | --- | --- |
| `adjust.jsx` | 137: 72 trên `cube`, 51 trên `ramp`, 14 trên `photo` (khoảng 1,8 MB) | khoảng 2,1 MB | khoảng 7 phút |
| `gradient.jsx` | 73: 70 ảnh 8 bit, 3 ảnh 16 bit 256 × 64 | khoảng 0,5 MB | khoảng 3 phút |

Cả hai khoảng 2,6 MB, cộng `index.json`, `photoshop.txt`, `presets.json` khoảng 0,15 MB: trong phần 4 MB của D32 (20 MB còn lại của lô 2, câu 6 của "Cần bạn chốt"; B30 khoảng 9,2 MB).

## Giả thuyết: case nào chốt

Hằng giả thuyết của D30 (đầu `core/gradient.cpp`, `test_gradient_ps` in độ lệch từng ảnh) và của task D (dòng task trong `docs/p1-tasks.md`), B38 chốt:

| Giả thuyết | Task | Case |
| --- | --- | --- |
| `Mdpn` của một điểm là điểm giữa của đoạn từ nó tới điểm sau (cách D30 đọc) hay từ điểm trước tới nó (khi đó `psd.jsx` ghi `m` của chính điểm vào `Mdpn` là lệch một đoạn); `midpoint_weight` tuyến tính từng đoạn qua (m; 0,5) | D30, E33, M43 | `mid-*`, `mid3-*`, `gmap-midpoints-ramp` |
| `smoothness_weight`: Smoothness chỉ cho Classic | D30 | `smoothness-*`, lưới 5 × 5 (100 %) |
| Perceptual trong Oklab, Linear trong ánh sáng tuyến tính, Smooth trọng smoothstep, Stripes ở `kStripesAt` 0,5; độ mờ theo method | D30, D38 | lưới 5 × 5, `alpha-*`, `mid3-stripes`, `gmap-<method>-ramp` |
| `fill_span` (độ dài, gốc của từng hình), Angle ngược kim đồng hồ, Offset, Scale, Reverse | D30, D40 | `*-a*`, `*-s*`, `*-offset`, `*-reverse` |
| Align with Layer: khung là biên mask | D40 | `align-on`, `align-off` |
| `kDitherLevels8`, `kDitherLevels16` | D30, D38, D39 | `dither16*`, `dither8*`, `gmap-dither-ramp`, `lookup-film9-dither-cube` |
| Levels: công thức, kênh riêng áp trước RGB; Curves: spline bậc ba tự nhiên, hằng ngoài hai đầu mút | D33 | `levels-*`, `curves-*` (`-order-*`: thứ tự; `-edges`, `-ends`: kẹp và đầu mút) |
| Brightness/Contrast Legacy (v + b/255, hệ số k của c), bản mới (đường cong không công khai); Exposure trong ánh sáng tuyến tính theo hồ sơ (hay gamma 2,2) | D34 | `bc-*`, `exposure-*` |
| Hue/Saturation theo mô hình GIMP (trọng số dải, saturation, lightness, Colorize); Vibrance | D35 | `hue-*`, `vibrance-*`, `lum-hue-h60-cube` |
| Color Balance theo GIMP, Preserve Luminosity giữ L; Selective Color (trọng số max − mid…, Relative nhân với kênh) | D36 | `cbal-*`, `selective-*` |
| Black & White (xám = min + …), Tint Color; Channel Mixer, đơn vị Constant; Photo Filter Multiply theo Density, Preserve Luminosity | D37 | `bw-*`, `mixer-*`, `pfilter-*` |
| Posterize min(⌊v n⌋, n − 1)/(n − 1); hệ số Lum của Threshold, Gradient Map (0,299/0,587/0,114 hay 0,3/0,59/0,11); Gradient Map có dùng độ mờ không | D38 | `posterize-*`, `threshold-*`, `gmap-*` |
| Nội suy tứ diện; `.cube` và `.3dl` cùng số ra cùng ảnh; LUT trên ảnh 16 bit | D39 | `lookup-*` |
| Adjustment layer ghép theo mode, opacity | D40 | `lum-*`, `opacity50-curves-s-cube` |
| Auto (Enhance Brightness and Contrast, cắt 0,1 %), Auto Tone, Auto Contrast, Auto Color, Equalize (LUT từ histogram độ sáng), Desaturate (saturation −100) | D41 | `auto-*`, `cmd-*` |

## Chỗ chưa chắc (Action Manager)

Script dựng các phần sau theo mã ScriptListener quen dùng (descriptor của 16 adjustment, fill layer Gradient chép từ `psd.jsx` của E33), chưa chạy thử. Mỗi adjustment layer, fill layer đọc lại sau khi tạo: giá trị Photoshop giữ khác giá trị đã đưa thì case hỏng (ảnh sẽ không phải của giá trị đó); khóa Photoshop không trả lại, tên, dữ liệu file chỉ ghi vào `photoshop.txt` ("not read back as given").

- Method của gradient (fill layer, Gradient Map): khóa `gradientsInterpolationMethod`, kiểu `gradientInterpolationMethodType`; giá trị là stringID `perceptual`, `linear`, `classic`, `smooth`, `stripes` hay charID `Perc`, `Lnr `, `Gcls`, `Smoo`, `Stri`: trước các case script thử cả hai trên một fill layer tạm, giữ cách Photoshop đọc lại đúng (`photoshop.txt`: "gradient method …"); không cách nào được thì case của method đó hỏng ("no id of it worked").
- Điểm giữa `Mdpn` (số nguyên %) ghi theo `m` của chính điểm đó; Photoshop có thể đặt lại `Mdpn` của điểm không dùng (đầu hay cuối): chỉ ghi vào `photoshop.txt`. Smoothness là `Intr` (0–4096).
- Fill layer Gradient: `Mk` lớp `contentLayer` kiểu `gradientLayer` (`Grad`, `Type` `GrdT`, `Angl`, `Scl `, `Rvrs`, `Dthr`, `Algn`, `Ofst` {`Hrzn`, `Vrtc`} %) như `psd.jsx`; Photoshop có thể giữ góc âm thành 0–360 (so theo modulo 360). Solid Color: `solidColorLayer`, `Clr ` RGBC. Align with Layer trên mask: Photoshop lấy khung theo biên mask hay không là điều case cho biết.
- `dither8-doc`: đổi tài liệu có fill layer sang 8 bit (`bitsPerChannel`); Photoshop giữ layer.
- Photo Filter: màu đưa bằng RGBC (Photoshop có thể giữ Lab: so qua đổi màu); bộ lọc theo tên không chọn được bằng script, nên bộ lọc khác mặc định dùng màu công bố. Selective Color: `Mthd` `CrcM` `Absl`/`Rltv`. Hue/Saturation Colorize: hue 0–360, saturation 0–100 trong mục Master (`Hst2`) của `Adjs`. Black & White `tintColor` RGBC. Brightness/Contrast `useLegacy`. Channel Mixer `Cnst` %.
- Color Lookup: descriptor như Properties ghi khi nạp file 3DLUT (`lookupType` `3DLUT`, `Nm `, `LUTFormat` `LUTFormatCUBE`/`LUTFormat3DL`, `LUT3DFileData` là byte của file, `LUT3DFileName`). Tạo xong mà Photoshop không giữ LUT (không `LUT3DFileData`, không `profile`) thì script đặt lại trên layer như Properties; vẫn không thì case hỏng. LUT không phải đơn vị phải đổi ảnh (so histogram).
- Auto (`auto-*`): Levels, Curves đặt `Auto` true ở mục kênh tổng của `Adjs`, Brightness/Contrast `Auto` true, như bấm nút Auto (Photoshop có thể ghi khóa khác, như `autoBrightnessContrast`); đọc lại vẫn là mặc định thì case hỏng ("Auto left the defaults"): báo lead.
- Image › Adjustments (`apply-*`, bước đầu của `cmd-auto-contrast-flat-photo`): sự kiện là lớp của adjustment với cùng descriptor (`Lvls`, `HStr`). Auto Color: `Lvls` với `autoBlackWhite`, `autoNeutrals`; Desaturate, Equalize, Auto Tone (`autoLevels`), Auto Contrast bằng DOM.
- Preset: thư mục `Presets` cạnh Photoshop (`app.path`), thư mục con `Levels`, `Curves`, `Hue Sat`, `Black and White`, `Exposure` (tìm theo tên); mỗi file nạp vào layer bằng `setd` với `presetKind` `presetKindUserDefined` và `Usng` là file. Không được thì `failed` của `presets.json` ghi, các case vẫn chạy; khi đó gửi lead `presets.json`, `photoshop.txt`.
- Đổi ảnh hay không (LUT, lệnh, Auto) so bằng histogram R, G, B (256 mức) của bản ghép: thay đổi dưới một mức 8 bit không thấy (chỉ hai LUT đơn vị bỏ phép kiểm này).

## Kiểm không có Photoshop

- `jscheck` (B30, `build/orch/api/b30.md`): `jscheck tools/ps-golden/p1/adjust.jsx`, `jscheck tools/ps-golden/p1/gradient.jsx`: 0 problem.
- Chế độ offline: lệnh ở mục `index.json` ra 137 và 73 dòng; mẫu viết tay trùng từng byte.
- `golden::Set` của `tests/golden.h` (một test tạm, không commit) đọc hai mẫu và hai index offline (`"inputs"` tuyệt đối): mọi cảnh đọc được (`props_from_json`, ảnh vào, mask), không problem nào ngoài PNG chưa có; `gradient_fill_from()` đọc cả 72 `content` gradient; `adjustment()` của D31 dựng được mọi adjustment layer và bước `apply` có `adj` (offline 132, mẫu 23; chỉ các case ở mặc định của AdjustSpec, như `posterize-4-*`, `threshold-128-*`, ra `vals()` "{}").
- Chạy cả `PB.run` không Photoshop: giả lập của F31 `build/orch/api/f31-psmock.jsx` cộng phần của D32 `build/orch/api/d32-psmock.jsx` (nạp sau: adjustment, fill layer giữ descriptor đã tạo và trả lại ở `adjustment`; Image › Adjustments, lệnh, Auto, preset đổi "điểm ảnh" của layer nên histogram của bản ghép đổi; thư mục `lut\`, `Presets\` giả). Script nạp trước để không tự chạy:

  ```sh
  M=build/orch/api  # của thư mục chính
  jscheck --eval "HERE = new Folder('C:/x'); MOCK.run(function () { A.prepare(true); PB.run('adjust', cases(), {bits: 8, dir: HERE}); })" out.txt tools/ps-golden/p1/adjust.jsx $M/f31-psmock.jsx $M/d32-psmock.jsx
  jscheck --eval "HERE = new Folder('C:/x'); MOCK.run(function () { A.prepare(false); PB.run('gradient', cases(), {bits: 8, dir: HERE}); })" out.txt tools/ps-golden/p1/gradient.jsx $M/f31-psmock.jsx $M/d32-psmock.jsx
  ```

  "Done: 137 images", "Done: 73 images"; `index.json` trùng offline trừ chỗ cố ý (Auto, bộ lọc mặc định của Photo Filter). Thử cách khác của script: `D32M.badMethods = ['stripes']` (Photoshop đọc lại Perceptual), `D32M.throwMethods = ['smooth']` (từ chối) cho phép thử Method; `D32M.quirks = true` (góc giữ 0–360, `Mdpn` của điểm cuối thành 50, tên gradient khác); `D32M.quirks2 = true` (LUT chỉ giữ sau khi đặt lại; Threshold lệch 5: 6 case hỏng riêng); `MOCK.reject = ['autoNeutrals']` (Auto Color hỏng riêng). Không có điểm ảnh: chỉ bắt lỗi của mã và dòng chảy.

## Đọc trong test

- `golden::Set set("p1/adjust")`; `if (!set.ready("test_adj_levels")) return;`; với mỗi `e` có layer `adj` của mình (`set.layers(e)`: ảnh vào ở `pixels`, props với `adj`, `vals`) hay bước `apply` của mình (`(*e.json)["apply"]`): dựng adjustment từ `vals` (D31), áp lên ảnh vào (adjustment layer Normal 100 % trên ảnh vào đục là adjustment áp lên ảnh vào), `compare()` với `set.image(e)`, `print()`; ngưỡng theo quy ước đầu mục Task. Trước B33: `golden::Set("p1/sample/adjust")`.
- Color Lookup: `lf0` là tên file trong `tests/data/lut/` (đọc file, đặt nội dung vào khóa `lut` của `vals`, như D31 giữ LUT). Auto: `vals` sau B33 là giá trị Photoshop tính (D41 so số của mình với số đó và ảnh). Preset: `presets.json` cạnh `index.json` của `p1/adjust`.
- Gradient: `test_gradient_ps` của D30 đọc `p1/gradient` (fill layer `props.content` qua `gradient_fill_from()`; case có `mask` để D40); `dither8-doc` (khóa `depth`: Photoshop dither ở 8 bit) cần dựng ở U8 mới so được (`test_gradient_ps` hiện dựng mọi case ở U16); ảnh 16 bit (`dither16*`) so qua `golden::ps15`.
