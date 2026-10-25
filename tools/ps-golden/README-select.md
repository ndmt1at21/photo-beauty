# Ảnh mẫu Photoshop: lệnh vùng chọn (S39)

`select.jsx` (cạnh `README.md`, ngoài `p1/`, dùng `p1/lib.jsx` của B30) làm trong Photoshop 79 ảnh vùng chọn cho S30–S34: Feather, Expand, Contract, Border, Smooth (Select › Modify), Inverse, Magic Wand, Grow, Similar, Color Range, Lasso, Elliptical Marquee, Rectangular Marquee, và bốn chế độ kết hợp hai vùng chọn. Bạn chạy một lần ở B33, cùng các script ảnh mẫu khác (mục P1 của `README.md`); khoảng 4 phút.

Mỗi case dựng trong một tài liệu RGB 16 bit sRGB mới như B30 (`PB.run` của `lib.jsx`): layer là ảnh vào (ảnh Magic Wand, Color Range chọn trên đó; với lệnh chỉ đổi vùng chọn thì là chính vùng chọn vào), các bước vùng chọn của case chạy theo thứ tự, rồi vùng chọn lưu thành kênh (Select › Save Selection) và kênh đó thành `<case>.png`: ảnh xám 16 bit, trắng là được chọn (mức 0–32768 của Photoshop, ghi ra 16 bit như ảnh 16 bit của B4). Chưa chạy trên Photoshop thật: chỗ chưa chắc ở mục cuối; phần nào sai với bản Photoshop của bạn thì chỉ case dùng nó hỏng (lý do trong `photoshop.txt` và mục `failed` của `index.json`), các case khác vẫn ra ảnh.

## Các bước

1. Trong WSL, ở `~/photo-beauty`, chép script, `lib.jsx` và ảnh vào sang Windows (cùng thư mục `ps-golden` của B4; `p1\` đã có thì chép lại cũng được):

   ```sh
   mkdir -p /mnt/c/Users/Teko/Downloads/ps-golden/p1/in /mnt/c/Users/Teko/Downloads/ps-golden/select/in
   cp tools/ps-golden/select.jsx /mnt/c/Users/Teko/Downloads/ps-golden/
   cp tools/ps-golden/p1/lib.jsx /mnt/c/Users/Teko/Downloads/ps-golden/p1/
   cp tests/golden/ps/p1/in/*.png /mnt/c/Users/Teko/Downloads/ps-golden/p1/in/
   cp tests/golden/ps/select/in/*.png /mnt/c/Users/Teko/Downloads/ps-golden/select/in/
   ```

   Script đọc `p1\lib.jsx`, `p1\in\` (ảnh của B30) và `select\in\` (vùng chọn vào của S39) cạnh nó: giữ đúng cách xếp thư mục này (như `tests/golden/ps/` trong repo).
2. Photoshop: đóng mọi ảnh, Color Settings như bước 3 mục P1 của `README.md` (script tự tắt các ô hỏi và dither nếu đọc được Color Settings, trả lại khi xong, kiểm trộn RGB không qua gamma 1; ảnh ra 16 bit nên không kiểm dither). Không cần font. Magic Wand: script tự chọn công cụ và Reset Tool (Sample Size về Point Sample), ghi tùy chọn đọc lại vào `photoshop.txt`.
3. File › Scripts › Browse… → `ps-golden\select.jsx` → Open, chờ tới khi hiện "Done", giữa chừng không bấm gì. Thông báo cuối như bước 4 của `README.md` ("Done: N images[, M failed]", "Stopped: …").
4. Chép kết quả vào repo (Photoshop ở máy khác thì trước đó đưa `out\select` về `C:\Users\Teko\Downloads\ps-golden\out\select` của máy này), rồi chạy test và báo lead (gửi `photoshop.txt` nếu có case hỏng):

   ```sh
   cp /mnt/c/Users/Teko/Downloads/ps-golden/out/select/* ~/photo-beauty/tests/golden/ps/select/
   build/default/pb-tests test_ps_select
   ```

   `test_ps_select` kiểm đủ ảnh theo `index.json`, cả bộ ≤ 3 MB, rồi in độ lệch: đường vào ra (`load-*` phải trùng ảnh vào tới mức 16 bit), Feather so S30 (`feather_selection`) kèm σ/Radius ước từ từng ảnh; ảnh của lệnh S31–S34 chưa có thì in số ảnh chờ task đó. Lead ghi độ lệch vào dòng S39; lệch quá 2 mức 8 bit thì kèm task chỉnh hằng số.

## Ảnh vào

Mọi ảnh vào 256 × 256 (trừ `cube.png` 190 × 190), PNG RGB 16 bit không chunk màu, mọi mẫu là mức Photoshop đọc lại đúng (như B30); script gán sRGB, không chuyển. Vùng chọn vào là ảnh xám (R = G = B, độ chọn 0–1); script nạp kênh đỏ của nó làm vùng chọn.

| Ảnh | Nội dung | Cho |
| --- | --- | --- |
| `select/in/hard.png` | điểm ảnh trọn trong hay ngoài: chữ nhật ở góc trên trái (hai mép canvas) x 0–89, y 0–69; chữ nhật rộng 20 px trên mép phải; chữ nhật lớn x 120–249, y 112–247 có lỗ 3 × 3, 1 × 1, 2 × 2 (Contract 50 còn lại phần giữa); hai thanh 40 × 16 cách 4 px; nét cao 1, 2 px; nét rộng 1 px chạm mép dưới; đường chéo điểm ảnh đơn; chấm 1, 2, 3, 5 px; sao năm cánh (góc lồi, lõm) | Feather, Expand, Contract, Border, Smooth |
| `select/in/aa.png` | hình khử răng cưa (16 × 16 mẫu mỗi điểm ảnh): đĩa r 40; tam giác đỉnh lẻ điểm ảnh; chữ nhật 80 × 40 xoay 30°; nét 1,5 px; đĩa r 24 bị mép trái cắt | mép mềm dưới điểm ảnh của S32; Feather |
| `select/in/soft.png` | vùng chọn mềm: Gaussian σ 12; đĩa mép dốc thẳng (1 tới r 30, 0 ở r 50); dải dốc ngang x/255; ba bậc 0,6, 0,5 (đúng nửa: mức 16384), 0,4 | Feather, Expand, Contract của vùng mềm; Inverse |
| `select/in/ramp-x.png`, `ramp-y.png` | x/255 ngang, y/255 dọc | Add, Subtract, Intersect: ảnh là phép kết hợp của mọi cặp mức |
| `p1/in/photo.png` (B30) | ảnh chụp: món ăn trong bát trên bàn gỗ | Magic Wand (trong bát), Grow, Similar, Color Range (ớt đỏ), Skin Tones |
| `p1/in/ramp.png` (B30) | 8 dải dốc ngang: xám, R, G, B, hue ở 4 (S, V) | tầm của Tolerance trên dốc xám và dốc hue; Highlights, Midtones, Shadows; nền của case chỉ có hình |
| `p1/in/cube.png` (B30) | 33³ màu lưới, mỗi điểm ảnh một màu | Magic Wand không Contiguous: những màu nào trong Tolerance (hộp hay cầu RGB); Color Range: độ chọn theo khoảng cách màu; Skin Tones, Highlights, Reds |
| `p1/in/shapes.png` (B30) | hình trên nền trong suốt | Magic Wand với alpha; Sample All Layers (trên `photo.png`) |

`pb-bench ps-select tests/golden/ps` (`bench/ps_select.cpp`) ghi lại `select/in/` bằng bộ sinh `golden::select_in` (`tests/ps_select_inputs.h`, header dùng chung với test); `test_ps_select_inputs` kiểm file trong repo trùng bộ sinh.

## Case

Tên: `<lệnh>-<ảnh vào>[-<tham số>]` (`r` px, `t` Tolerance, `f` Fuzziness; `0_5` là 0,5; `-bounds`: Apply effect at canvas bounds bật; `-noncontig`: Contiguous tắt; `-noaa`: Anti-alias tắt).

| Nhóm | Case | Cho |
| --- | --- | --- |
| Đường vào ra | `load-hard`, `-aa`, `-soft`, `-ramp-x`: nạp rồi lưu, phải ra đúng ảnh vào (kiểm cả đường nạp qua kênh lẫn đường xuất PNG); `inverse-soft` | S39 (test), S31 |
| Feather | `hard` r 1, 5, 20, 100 × bounds tắt, bật; r 0,5, 2,5 (σ ở bán kính nhỏ); `aa` r 5; `soft` r 5 | S30 (so ngay), B38 chốt `kSigmaPerRadius` |
| Expand, Contract | `hard` 5, 50 px × bounds tắt, bật; `aa` 5; `soft` 5 (bậc nào được tính là chọn) | S32 |
| Border, Smooth | Border 10 px `hard`, `aa` (hộp thoại không có ô canvas bounds); Smooth 10 px `hard` bounds tắt, bật, `aa` | S32 |
| Magic Wand | `photo` tại (60, 160) trong bát: Tolerance 0, 32, 100 × Contiguous × Anti-alias (12); `ramp` dốc xám tại (128, 16), dốc hue tại (64, 144) không Contiguous; `cube` tại màu (16, 16, 16), (24, 12, 8) /32, Tolerance 32, 64, không Contiguous, không Anti-alias; `shapes` tại nét 1 px (Contiguous bật, tắt: một hay cả hai nét cùng màu), tại nền trong suốt; Sample All Layers: `shapes.png` trên `photo.png` tại (20, 20) (nơi `shapes.png` trong suốt): ảnh ghép, hay riêng layer trên | S33 |
| Grow, Similar | từ Magic Wand Tolerance 16 trong bát, 8 trên dốc xám, cùng Tolerance | S33 |
| Color Range | Sampled Colors Fuzziness 40, 100 trên `cube` (màu (24, 12, 8)) và ớt của `photo`; Highlights, Midtones, Shadows ở mặc định (Fuzziness 20, Range 190, 105–150, 65) trên `ramp`, Highlights Fuzziness 50 Range 128; Highlights, Skin Tones, Reds trên `cube`; Skin Tones trên `photo` (Detect Faces tắt: ảnh không có mặt) | S34 |
| Hình | Lasso ngôi sao năm cánh vẽ đỉnh nối đỉnh (tự cắt: ngũ giác giữa được tô theo luật non-zero, thủng theo even-odd), Anti-alias bật, tắt; elip 180 × 100, 60 × 60, 7 × 5, 3 × 3 cộng nhau, Anti-alias bật, tắt; chữ nhật cạnh ở phần tư điểm ảnh (Photoshop làm tròn thế nào) và một chữ nhật nguyên | S31 |
| Kết hợp | `add-ramps`, `subtract-ramps`, `intersect-ramps`: `ramp-x` rồi `ramp-y` với chế độ đó | S31 |

## `index.json`

Định dạng của B30 (`tests/golden.h`), thêm khóa `select` của case: danh sách bước chạy theo thứ tự trên tài liệu đã dựng. `"inputs": ".."` (từ `tests/golden/ps/select/` tới `tests/golden/ps/`): `src` của layer và của bước `load` là đường dẫn từ đó (`p1/in/photo.png`, `select/in/hard.png`), nên `golden::Set("select").input(src)` và `layers(e)` đọc được. Ví dụ:

```json
{"name": "feather-hard-r5-bounds", "canvas": [256, 256], "bits": 16,
 "layers": [{"kind": "pixel", "src": "select/in/hard.png", "props": {"version": 1, "name": "hard"}}],
 "select": [{"op": "load", "src": "select/in/hard.png", "mode": "new"}, {"op": "feather", "px": 5, "canvasBounds": true}]}
```

Mỗi bước `{op, …}`, khóa theo thứ tự `S.SPEC` của script (khóa, đơn vị như giao diện: `ModifySelectionDialog.qml`, tùy chọn Magic Wand của `ToolSpec.js`, `ColorRangeDialog.qml`):

| `op` | Khóa | Lệnh Photoshop |
| --- | --- | --- |
| `load` | `src` (ảnh vào xám), `mode` | Load Selection kênh đỏ của ảnh vào |
| `inverse` | | Select › Inverse |
| `feather` | `px` 0,1–1.000, `canvasBounds` | Select › Modify › Feather |
| `expand`, `contract`, `smooth` | `px` 1–500, `canvasBounds` | Select › Modify |
| `border` | `px` 1–200 | Select › Modify › Border |
| `wand` | `at` [x, y], `color` (RGBA 16 bit của điểm đó trong ảnh vào), `tolerance` 0–255, `antiAlias`, `contiguous`, `sampleAll`, `layer` (tên layer lấy mẫu khi không Sample All; không có: layer trên cùng), `sampleSize` (`point`), `mode` | Magic Wand nhấp tại điểm ảnh `at` |
| `grow`, `similar` | `tolerance`, `antiAlias` | Select › Grow, Similar |
| `colorRange` | `select` (`sampledColors`, `reds`…, `highlights`, `midtones`, `shadows`, `skinTones` như `ColorRangeDialog.qml`), `fuzziness` 0–200, `range` [thấp, cao] (mức 8 bit, chỉ ba tông: Highlights [190, 255], Shadows [0, 65]), `at`, `color`, `lab` (Sampled Colors: màu đưa Photoshop, CIELAB D50 tính từ `color` qua ma trận D50 của hồ sơ sRGB IEC61966-2.1, 2 chữ số lẻ), `detectFaces` (Skin Tones), `invert` | Select › Color Range |
| `lasso` | `points` [[x, y], …] (bội 1/8 px), `antiAlias`, `mode` | Lasso, Polygonal Lasso |
| `ellipse` | `rect` [x, y, w, h], `antiAlias`, `mode` | Elliptical Marquee |
| `rect` | `rect` [x, y, w, h] (bội 1/8 px), `mode` | Rectangular Marquee |

`mode`: `new`, `add`, `subtract`, `intersect` (S31). Color Range luôn chọn mới (script bỏ chọn trước: có vùng chọn thì Photoshop chỉ chọn trong đó). Ảnh ra luôn 16 bit (`bits` 16): xám 1 kênh, mẫu s là mức `golden::ps15(s)` của 32768.

Trước B33 test đọc `tests/golden/ps/select/sample/index.json`: 21 dòng của `PB.offline('select', cases(), {bits: 16, repoInputs: '../..'})` (mỗi dạng bước một dòng; `"inputs": "../.."`), trùng từng byte với các dòng cùng tên; ảnh chưa có.

## Cỡ, thời gian

79 ảnh xám 16 bit, ước khoảng 1,2 MB (ngân sách 3 MB của task, câu 6 "Cần bạn chốt"; ước bằng PNG zlib 9 lọc theo hàng như libpng trên ảnh thay thế cùng loại: Feather bằng ba box, Magic Wand, Color Range bằng mô hình đơn giản; vùng chọn cứng, khử răng cưa vài KB, Feather 20 px và Color Range trên ảnh chụp mỗi ảnh tới khoảng 90 KB; Photoshop nén có thể lệch ±15 %, ảnh thật lệch ảnh thay thế thêm). Ảnh vào mới (`select/in/`) 5 file, khoảng 40 KB. Khoảng 4 phút (79 tài liệu, mỗi tài liệu một ảnh xám tạm để xuất).

## Script giữ gì, kiểm gì

- Vùng chọn vào qua kênh: kênh đỏ của ảnh vào chép sang tài liệu của case thành kênh alpha (Duplicate Channel của DOM); không được thì layer của ảnh vào chép lên trên cùng (đục, nên ảnh ghép là nó), nạp kênh đỏ làm vùng chọn (Ctrl nhấp kênh đỏ), lưu thành kênh. Cách nào cũng kiểm histogram của kênh trùng kênh đỏ của ảnh vào. Kênh của mọi bước `load` làm trước bước đầu tiên (cách thứ hai thay vùng chọn).
- Xuất: vùng chọn sau bước cuối lưu thành kênh `selection`; một tài liệu xám 16 bit cỡ canvas, tô đen, rồi tô trắng qua vùng chọn là kênh đó (chép kênh sang rồi nạp; không được thì Load Selection kênh của tài liệu case); tô theo vùng chọn cho mức s của 32768 đúng là s. Histogram ảnh xám phải trùng histogram của kênh, không thì case hỏng; ảnh đầu tiên mở lại kiểm là xám 16 bit. Case `load-*` kiểm cả đường này tới mức 16 bit (test so với ảnh vào).
- Magic Wand: điểm nhấp là tọa độ nguyên của điểm ảnh (`at`), luôn ở vùng đều hay ghi rõ màu (`color`, test kiểm trùng ảnh vào); Reset Tool một lần mỗi lượt chạy; công cụ lấy mẫu layer đang chọn (`layer`) trừ khi `sampleAll`.
- Color Range Sampled Colors nhận màu Lab do script tính (`lab` trong `index.json`, như nhau trong và ngoài Photoshop), không qua ống hút (không phụ thuộc Sample Size, tọa độ điểm hút).
- `photoshop.txt`: phiên bản, Color Settings trước và sau, kiểm trộn RGB, tùy chọn Magic Wand sau Reset Tool, mỗi bước như JSON và descriptor Action Manager đã gửi, biên vùng chọn sau mỗi bước, cách nạp kênh và cách xuất đã dùng, lỗi nếu có.

## Chỗ chưa chắc (Action Manager)

Script dựng các phần sau theo mã ScriptListener quen dùng, nhưng chưa chạy thử. Phần nào sai thì chỉ case dùng nó hỏng (`failed`, lý do trong `photoshop.txt`); gửi lead `photoshop.txt`, lead sửa script, bạn chạy lại riêng script này.

- Apply effect at canvas bounds: khóa `selectionModifyEffectAtCanvasBounds` (boolean) của Feather `Fthr` (`Rds `), Expand `Expn`, Contract `Cntc` (`By  `), Smooth `Smth` (`Rds `); Border `Brdr` (`Wdth`) không có. Photoshop bỏ qua khóa lạ thì hai ảnh tắt, bật trùng nhau: `test_ps_select` báo ("Apply effect at canvas bounds made no difference"); Photoshop từ chối thì các case đó hỏng.
- Duplicate Channel sang tài liệu khác (`Channel.duplicate(doc)` của DOM), Load Selection kênh của tài liệu khác theo tên (`setd` `fsel` tới `Chnl` theo tên trong `Dcmn` theo tên), Ctrl nhấp kênh đỏ (`setd` `fsel` tới `Chnl` `Rd  `): mỗi việc có cách thứ hai; cả hai cách hỏng thì case hỏng, báo cả hai lý do.
- Tô qua vùng chọn trong tài liệu xám đúng từng mức: case `load-*` cho thấy (test in "is its input to the level" hay số điểm ảnh lệch).
- Magic Wand: `setd` (hay `AddT`, `SbtF`, `IntW`) `fsel` tới `Pnt ` (`Hrzn`, `Vrtc` px) với `Tlrn`, `AntA`, `Cntg`, `Mrgd`; Sample Size lấy từ tùy chọn công cụ (Reset Tool: `slct` `magicWandTool`, `Rset` `CrnT`).
- Grow, Similar theo DOM (`selection.grow`, `similar`): Photoshop có thể lấy Sample All Layers từ tùy chọn Magic Wand (tắt sau Reset Tool; các case đó chỉ một layer).
- Color Range `ClrR`: Sampled Colors bằng `Fzns`, `Mnm `, `Mxm ` (`LbCl`: `Lmnc`, `A   `, `B   `); các nhóm bằng `Clrs` (`Rds `, `Hghl`, `Mdtn`, `Shdw`; Skin Tones là stringID `skinTone`), Detect Faces `UseFacesKey`, `colorModel` 0. Fuzziness và Range của ba tông (Photoshop CC): `highlightsFuzziness`, `highlightsLowerLimit`, `midtonesFuzziness`, `midtonesLowerLimit`, `midtonesUpperLimit`, `shadowsFuzziness`, `shadowsUpperLimit`, và mặc định 20, 190, 105–150, 65 đều nhớ từ ScriptListener, chưa kiểm: Photoshop bỏ qua các khóa đó thì nó dùng mặc định của nó, case mặc định vẫn đúng nếu mặc định đúng như trên; `colorRange-ramp-highlights-f50-r128` trùng `colorRange-ramp-highlights` là dấu hiệu khóa bị bỏ qua.
- Marquee qua Action Manager với tọa độ lẻ (`Elps`, `Rctn`: `Top `, `Left`, `Btom`, `Rght` px; `AntA` của elip); Lasso qua DOM `selection.select(điểm, kiểu, 0, antiAlias)`.

## Cho task sau

- Đọc: `golden::Set set("select")`; `if (!set.ready("test_x")) return;`; mỗi `e` của `set.entries()`: `e.json["select"]` (các bước), `set.input(src)` (vùng chọn vào: kênh R; lưới bằng `golden::grid(im, Format{Depth::U16, 1})`), `set.layers(e)` (ảnh Magic Wand, Color Range chọn trên đó: `pixels`), `set.image(e)` (ảnh của Photoshop: xám 16 bit; so mẫu qua `golden::ps15` hay thẳng bằng `golden::compare`, lệch dưới 0,004 mức 8 bit). Trước B33: `golden::Set("select/sample")` (21 dòng, ảnh thiếu).
- `tests/test_ps_select.cpp` để sẵn chỗ: `check_s31()` … `check_s34()` trả false (ảnh chờ task đó); task S31–S34 so ảnh của lệnh mình ở đó (hay trong test riêng, đọc như trên) và trả true. Bước nào là của task nào: bảng `specs()` trong test (`load` có `mode` khác `new` là S31).
- S30, B38: `test_ps_select` in σ/Radius ước từ mỗi ảnh Feather (bình phương tối thiểu trên cả ảnh, cùng mép canvas với `feather_selection`) và trung vị của các ảnh từ 1 px: đổi `kSigmaPerRadius` (`core/gauss.h`) theo đó.
