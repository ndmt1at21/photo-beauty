# Ảnh mẫu Photoshop: công cụ vẽ và retouch (C41)

`strokes.jsx` (cạnh `README.md`, ngoài `p1/`, dùng `p1/lib.jsx` của B30) làm trong Photoshop 59 ảnh cho C30–C38 và H30–H35: Brush (độ cứng, Flow, Opacity, mode, Wet Edges, Noise, đầu cọ elip), Pencil, Eraser ba mode, Clone Stamp, Healing Brush (Diffusion), Spot Healing Brush (Proximity Match, Create Texture), Dodge, Burn (ba Range, Protect Tones), Sponge (Saturate, Desaturate, Vibrance), Gradient (năm kiểu, năm Method), Paint Bucket. Bạn chạy một lần ở B33, cùng các script ảnh mẫu khác (mục P1 của `README.md`); khoảng 4 phút.

Mỗi case dựng trong một tài liệu RGB 16 bit sRGB mới như B30 (`PB.run` của `lib.jsx`): một layer pixel là ảnh vào; script chọn công cụ, Reset Tool, đặt tùy chọn qua Action Manager rồi đọc lại, rồi vẽ từng path của case thành một nét bằng `PathItem.strokePath(ToolType.…)` (không Simulate Pressure); Gradient kéo theo một đoạn, Paint Bucket tô từ một điểm. Ảnh ghép lưu thành `<case>.png`, RGB (hay RGBA khi có trong suốt) 16 bit. Chưa chạy trên Photoshop thật: chỗ chưa chắc ở mục cuối; phần nào sai với bản Photoshop của bạn thì chỉ case dùng nó hỏng (lý do trong `photoshop.txt` và mục `failed` của `index.json`), các case khác vẫn ra ảnh.

## Các bước

1. Trong WSL, ở `~/photo-beauty`, chép script, `lib.jsx` và ảnh vào sang Windows (cùng thư mục `ps-golden` của B4; `p1\` đã có thì chép lại cũng được):

   ```sh
   mkdir -p /mnt/c/Users/Teko/Downloads/ps-golden/p1 /mnt/c/Users/Teko/Downloads/ps-golden/strokes/in
   cp tools/ps-golden/strokes.jsx /mnt/c/Users/Teko/Downloads/ps-golden/
   cp tools/ps-golden/p1/lib.jsx /mnt/c/Users/Teko/Downloads/ps-golden/p1/
   cp tests/golden/ps/strokes/in/* /mnt/c/Users/Teko/Downloads/ps-golden/strokes/in/
   ```

   Script đọc `p1\lib.jsx` và `strokes\in\` (bốn ảnh vào và `paths.json`) cạnh nó: giữ đúng cách xếp thư mục này (như `tests/golden/ps/` trong repo). Không dùng `p1\in\`.
2. Photoshop: đóng mọi ảnh, Color Settings như bước 3 mục P1 của `README.md` (script tự tắt các ô hỏi và dither nếu đọc được Color Settings, trả lại khi xong, kiểm trộn RGB không qua gamma 1; ảnh ra 16 bit nên không kiểm dither). Không cần font. Script đổi công cụ đang chọn, tùy chọn của từng công cụ (Reset Tool), màu tiền cảnh và View (100 % cho case Eraser Block): chạy xong thì các công cụ đó ở tùy chọn mặc định. Clone Stamp và Healing Brush dùng pattern tạm tên `c41 …`, script xóa sau mỗi case; sót lại (`photoshop.txt` ghi "not deleted") thì xóa ở Window › Patterns.
3. File › Scripts › Browse… → `ps-golden\strokes.jsx` → Open, chờ tới khi hiện "Done", giữa chừng không bấm gì. Thông báo cuối như bước 4 của `README.md` ("Done: N images[, M failed]", "Stopped: …").
4. Chép kết quả vào repo (Photoshop ở máy khác thì trước đó đưa `out\strokes` về `C:\Users\Teko\Downloads\ps-golden\out\strokes` của máy này), rồi chạy test và báo lead (gửi `photoshop.txt` nếu có case hỏng):

   ```sh
   cp /mnt/c/Users/Teko/Downloads/ps-golden/out/strokes/* ~/photo-beauty/tests/golden/ps/strokes/
   build/default/pb-tests test_ps_strokes
   ```

   `test_ps_strokes` kiểm đủ ảnh theo `index.json`, cả bộ ≤ 3 MB, mỗi `paint` đúng (khóa, path, vùng), rồi in: độ lệch của Brush cơ bản (đầu tròn, Normal, không Wet Edges, Noise) và Eraser mode Brush so với cọ P0 (`core/dab.h`, `core/stroke.h`), ΔE2000 và mức 8 bit; tâm chấm của path `dot` mà Photoshop vẽ (điểm của path là (28,25; 64,75)); thống kê từng vùng của Clone Stamp, Healing Brush, Spot Healing Brush; với công cụ chưa có thì số ảnh chờ từng task (C30, C31, C33, C36, C38, H30, H32, H33, H35). Lead ghi độ lệch của từng công cụ vào dòng C41; lệch quá 2 mức 8 bit thì kèm task chỉnh hằng số.

## Ảnh vào

`pb-bench ps-strokes tests/golden/ps` ghi chúng (bộ sinh `tests/ps_strokes_inputs.h`, `golden::strokes_in`); `test_ps_strokes_inputs` kiểm file trong repo trùng bộ sinh. PNG 16 bit không gắn profile, mọi mẫu là mức Photoshop đọc lại đúng (`golden::ps_level`), như ảnh vào của B30.

| Ảnh | Nội dung | Cho |
| --- | --- | --- |
| `strokes/in/white.png` | 256 × 160 RGB trắng | Brush, Pencil, Gradient |
| `strokes/in/patches.png` | 256 × 160 RGBA, ô 32 px: hàng 0–2 (y 0–95) đục, 24 màu (xám, cơ bản, phụ, da, tối, pastel); hàng 3 (y 96–127) màu hàng 0 với alpha x/255; hàng 4 (y 128–159) trong suốt | mode của Brush (Multiply, Screen, Behind, Clear), Eraser, Gradient Multiply |
| `strokes/in/tones.png` | 256 × 128 RGB: 8 dải 16 hàng, mỗi dải dốc ngang t = x/255 như `ramp.png` của B30: xám; R, G, B; hue ở (S, V) (1, 1), (1, 0,5), (0,5, 1), (0,25, 0,75) | Dodge, Burn, Sponge (mọi mức dưới nét), Paint Bucket |
| `strokes/in/skin.png` | 128 × 128 RGB: da sáng, tối dần về góc dưới phải, vân value noise (ô 9 và 3,5 px, khoảng ±4 %), bốn vết ở x < 52: nốt ruồi tối a (20, 24), vệt tối b (36, 60), chấm tối c (18, 98), vết đỏ d (40, 104); da sạch cách 64 px bên phải | Clone Stamp, Healing Brush, Spot Healing Brush |
| `strokes/in/paths.json` | các path, mục dưới | mọi nét |

## Path

Tọa độ điểm ảnh của tài liệu, điểm ảnh (i, j) phủ [i, i + 1) × [j, j + 1) (như `PenEvent` của `core/stroke.h`), bội 1/8. Mỗi path một subpath mở, vẽ thành một nét riêng; giữa hai điểm là đường Bezier bậc ba qua tay nắm ra của điểm trước và tay nắm vào của điểm sau (đoạn thẳng giữa hai điểm góc); path một điểm là một chấm.

| Path | Điểm | Cho |
| --- | --- | --- |
| `line` | (16, 20) → (240, 20) | nét ngang: khoảng chấm, mép nét |
| `cross` | (224, 4) → (224, 44), cắt ngang cuối `line` | hai nét chồng (Opacity mỗi nét) |
| `dot` | (28,25; 64,75), một điểm | một chấm lệch phần tư điểm ảnh: Photoshop đặt tâm chấm ở đâu |
| `diagonal` | (60, 48) → (116, 104) | nét 45° |
| `curve` | (136, 48) tay nắm ra (200, 48) → (240, 104) tay nắm vào (176, 104): hình S | nét cong theo độ dài cung |
| `loop` | (16, 112) → (88, 156) → (88, 112) → (16, 156), tự cắt | nét tự chồng |
| `shallow` | (104, 128) → (248, 150), dốc 22/144 | Pencil: điểm ảnh của đường dốc thấp |
| `h16`, `h48`, `h80`, `h112`, `h144` | (−8, y) → (264, y), giữa mỗi hàng ô của `patches.png`, quá hai mép | mode, Eraser trên mọi màu, alpha |
| `band0` … `band7` | (−8, y) → (264, y), giữa mỗi dải của `tones.png` | Dodge, Burn, Sponge |
| `spot-a` … `spot-d` | chấm (20, 24); đoạn (28, 60) → (44, 60); chấm (18, 98); chấm (40, 104): qua bốn vết của `skin.png` | Clone, Healing, Spot Healing |

Nguồn sự thật là `strokes_in::paths()`. Bench ghi `paths.json`, một path một dòng, điểm `[x, y]` (điểm góc) hay `[x, y, vàoX, vàoY, raX, raY]`; script đọc nó (`eval`: ExtendScript không có JSON) và dựng `PathItem`. `index.json` chỉ ghi tên path (`paint.paths`): task sau lấy điểm bằng `strokes_in::path(tên)`, nét thành sự kiện bút bằng `strokes_in::events(path)` (đi dọc path, mỗi ≤ 0,5 px, 200 Hz, pressure 1), hay đọc `paths.json`.

## Case

| Nhóm | Case | Cho |
| --- | --- | --- |
| Brush cơ bản | `brush-h0`, `-h50`, `-h100`: 24 px, đen trên `white`, path `line`, `cross`, `dot`, `diagonal`, `curve`, `loop`; `brush-h50-flow30`, `brush-h50-o50`, `brush-h0-flow30-o50` | P0: so ngay với cọ P0 (`kFalloffPower`, flow, opacity, khoảng chấm); C30–C35 chỉnh hằng |
| Brush trên mảng màu | `brush-normal`, `-multiply`, `-screen`, `-behind`, `-clear`: #3399cc, h50, theo `h16` … `h144` trên `patches` | P0 (Normal), C33 |
| Wet Edges, Noise | `brush-wet-h100`, `brush-wet-h0` (#3399cc), `brush-noise-h0` (đen) | C31 |
| Đầu cọ elip | `brush-ellipse`: 32 px, góc 30°, độ tròn 40 % | C30 |
| Pencil | `pencil-1` (`shallow`, `diagonal`, `curve`, `dot`), `pencil-9` | C30 |
| Eraser | `eraser-brush` (h50, 24 px), `eraser-brush-o50`, `eraser-pencil` (9 px), `eraser-block`, trên `patches` | P0 (Brush), C33 |
| Clone Stamp | `clone-h100`, `clone-h0`: 20 px, nguồn lệch (64, 0), Aligned, qua bốn vết của `skin` | H30 |
| Healing Brush | `heal-d1`, `-d5`, `-d7`: Diffusion 1, 5, 7, nguồn như Clone Stamp | H32 |
| Spot Healing Brush | `spot-proximity`, `spot-texture` | H33 |
| Dodge, Burn | `dodge-`, `burn-` × `shadows`, `midtones`, `highlights` × Protect Tones bật, tắt (`-noprotect`): 14 px, Exposure 50 %, dọc tám dải của `tones` | H35 |
| Sponge | `sponge-desaturate`, `-saturate` × Vibrance tắt, bật (`-vibrance`): Flow 50 % | H35 |
| Gradient | `gradient-linear`, `-radial`, `-angle`, `-reflected`, `-diamond` (Perceptual, ba màu); `gradient-linear-linear`, `-classic`, `-smooth`, `-stripes` (Method); `gradient-linear-transparent` (alpha 1 → 0); `gradient-linear-multiply` (trên `patches`); `gradient-radial-reverse-o50` | C36 (C37) |
| Paint Bucket | `bucket-t32` (#00a0ff, Tolerance 32 tại (128, 8) trên dải xám), `-noncontig`, `-noaa` | C38 |

## `index.json`

Định dạng của B30 (`tests/golden.h`), thêm khóa `paint` của case: công cụ và mọi tùy chọn của nó (mặc định cũng ghi). `"inputs": ".."` (từ `tests/golden/ps/strokes/` tới `tests/golden/ps/`): `src` của layer là `strokes/in/<ảnh>.png`, nên `golden::Set("strokes").layers(e)` đọc được. Ví dụ:

```json
{"name": "brush-h50-o50", "canvas": [256, 160], "bits": 16,
 "layers": [{"kind": "pixel", "src": "strokes/in/white.png", "props": {"version": 1, "name": "white"}}],
 "paint": {"tool": "brush", "size": 24, "hardness": 0.5, "spacing": 0.25, "angle": 0, "roundness": 1, "mode": "normal",
           "opacity": 0.5, "flow": 1, "color": "#000000", "wetEdges": false, "noise": false,
           "paths": ["line", "cross", "dot", "diagonal", "curve", "loop"]}}
```

Khóa theo thứ tự `C.SPEC` của script; đơn vị như photo-beauty (`BrushParams` của `core/stroke.h`, `ui/qml/ToolSpec.js`): `size` đường kính px, `hardness`, `roundness`, `opacity`, `flow`, `exposure` 0..1, `spacing` phần của `size`, `angle` độ. Công cụ có đầu cọ (mọi công cụ trừ `gradient`, `bucket`) có trước `size`, `hardness`, `spacing`, `angle`, `roundness`.

| `tool` | Khóa | Công cụ Photoshop |
| --- | --- | --- |
| `brush` | `mode`, `opacity`, `flow`, `color`, `wetEdges`, `noise`, `paths` | Brush |
| `pencil` | `mode`, `opacity`, `color`, `paths` | Pencil |
| `eraser` | `eraser` (`brush`, `pencil`, `block`), `opacity`, `flow`, `paths` | Eraser |
| `clone` | `mode`, `opacity`, `flow`, `source`, `aligned`, `paths`, `areas` | Clone Stamp (làm bằng Pattern Stamp: mục cuối) |
| `heal` | `mode` (`normal`, `replace`, `multiply`, `screen`, `darken`, `lighten`, `color`, `luminosity`), `source`, `aligned`, `diffusion` 1–7, `paths`, `areas` | Healing Brush (Source Pattern) |
| `spot` | `mode`, `type` (`proximity`, `texture`), `paths`, `areas` | Spot Healing Brush |
| `dodge`, `burn` | `range` (`shadows`, `midtones`, `highlights`), `exposure`, `protectTones`, `paths` | Dodge, Burn |
| `sponge` | `sponge` (`desaturate`, `saturate`), `flow`, `vibrance`, `paths` | Sponge |
| `gradient` | `gradient` (JSON của D30: `colors` [{p, c [r, g, b] 0..255, m}], `alphas` [{p, a, m}], `smoothness`), `method` (`perceptual`, `linear`, `classic`, `smooth`, `stripes`), `style` (`linear`, `radial`, `angle`, `reflected`, `diamond`), `mode`, `opacity`, `reverse`, `dither`, `transparency`, `line` [[x0, y0], [x1, y1]] | Gradient cổ điển, kéo từ `line[0]` tới `line[1]` |
| `bucket` | `color`, `mode`, `opacity`, `tolerance` 0–255, `antiAlias`, `contiguous`, `allLayers`, `at` [x, y] | Paint Bucket, nhấp tại `at` |

`mode` của Brush, Pencil, Clone, Gradient, Bucket: tên `kBlendNames` cùng `behind`, `clear` (danh sách `paint` của `ToolSpec.js`). `source` [dx, dy]: nguồn của điểm ảnh (x, y) là (x + dx, y + dy). `areas` (clone, heal, spot): mỗi path một vùng [x, y, w, h] = `strokes_in::area(path, size, canvas)` (hộp bao các điểm và tay nắm, nới size/2 + 1 px, cắt theo canvas): Healing, Spot Healing ra điểm ảnh theo thuật toán riêng, nên H30, H32, H33 so thống kê trong các vùng đó (test in trung bình, độ lệch chuẩn từng kênh của ảnh Photoshop, ảnh vào và vùng nguồn). Ảnh ra luôn 16 bit (`bits` 16).

Trước B33 test đọc `tests/golden/ps/strokes/sample/index.json`: 17 dòng của `PB.offline('strokes', cases(), {bits: 16, inputs: '.', repoInputs: '../..'})` (mỗi công cụ, mỗi task một dòng ít nhất; có bốn case P0 `brush-h100`, `brush-h50-o50`, `brush-h50-flow30`, `eraser-brush`), `"inputs": "../.."`, trùng từng byte với các dòng cùng tên; ảnh chưa có. Mẫu ở thư mục riêng vì test của bộ thật đòi đủ PNG cạnh `index.json`.

## Cỡ, thời gian

59 ảnh RGB(A) 16 bit, ước khoảng 1,0 MB (ngân sách 3 MB của task, câu 6 "Cần bạn chốt"; ước bằng PNG zlib 9 của spng trên ảnh vẽ thô cùng loại: Brush 0,29 MB, Gradient 0,28, Healing 0,17, Clone 0,12, Spot Healing 0,11, còn lại nhỏ; Photoshop nén khác và ảnh thật khác ảnh thô, gấp đôi vẫn dưới ngân sách). Ảnh vào 4 PNG và `paths.json`, khoảng 65 KB (`skin.png` 62 KB). Khoảng 4 phút (59 tài liệu, mỗi tài liệu vài nét).

## Script làm gì, kiểm gì

- Công cụ: `slct` lớp của công cụ, Reset Tool (`Rset` tới `CrnT`); tùy chọn sau Reset ghi vào `photoshop.txt` một lần mỗi công cụ; tắt Shape Dynamics, Scatter, Texture, Dual Brush, Color Dynamics, Transfer, Brush Pose, Noise, Wet Edges, Airbrush (`repeat`) còn bật (như B4). Đầu cọ đặt bằng `setd` `computedBrush` (`Dmtr`, `Hrdn`, `Angl`, `Rndn`, `Spcn`, như chấm cọ của B4) và đọc lại từ `brush` của tùy chọn: lệch quá 0,5 thì case hỏng.
- Mỗi tùy chọn của case (`C.OPTIONS`): tìm khóa trong tùy chọn của công cụ theo tên (các bản Photoshop đặt tên khác nhau), với giá trị liệt kê thì theo kiểu liệt kê hay giá trị hiện có; đặt, đọc lại, phải trùng. Không tìm được hay đọc lại khác thì case hỏng, lý do kèm danh sách khóa của công cụ.
- Nét: mỗi path một `PathItem` một subpath, `strokePath(ToolType.X, false)`; công cụ không có trong `ToolType` thì `Strk` của Action Manager (`null` là path theo tên, `Usng` là lớp công cụ). Sau mỗi nét kiểm histogram của ảnh ghép đã đổi (Eraser, Sponge, Behind, Clear có thể không đổi: chỉ ghi chú); không đổi thì case hỏng.
- Gradient: `Grdn` với `From`, `T   `, `Type`, `Dthr`, `UsMs` (Transparency), `Rvrs`, `Md  `, `Opct`, `Grad` dựng như `E.gradient` của `psd.jsx`, Method `gradientsInterpolationMethod` bằng char ID rồi string ID; kiểm số layer không đổi (Gradient mới của Photoshop 2023 tạo layer fill: phải là Classic).
- Paint Bucket: `Fl  ` với `From`, `Tlrn`, `AntA`, `Cntg`, `Usng` màu tiền cảnh, `Md  `, `Opct` (`Mrgd` khi `allLayers`).
- Eraser Block: View › 100 % trước (khối của Block là 16 điểm ảnh màn hình).
- `photoshop.txt`: phiên bản, Color Settings trước và sau, kiểm trộn RGB, tùy chọn sau Reset của mỗi công cụ, `paint` của mỗi case như JSON, tùy chọn đã đặt và đọc lại, cách vẽ mỗi nét, lỗi nếu có.

## Chỗ chưa chắc (Action Manager)

Script dựng các phần sau theo mã ScriptListener quen dùng, nhưng chưa chạy thử. Phần nào sai thì chỉ case dùng nó hỏng (`failed`, lý do trong `photoshop.txt`); gửi lead `photoshop.txt`, lead sửa script, bạn chạy lại riêng script này.

- Clone Stamp và nguồn của Healing Brush (chỗ chưa chắc nhất): script không Alt-nhấp được điểm nguồn, nên Clone Stamp là Pattern Stamp (Aligned, Impressionist tắt) với pattern là ảnh vào dịch (−dx, −dy) có quấn mép (Filter › Other › Offset, Wrap Around; Define Pattern bằng `Mk  ` `Ptrn` theo tên), Healing Brush là Source Pattern với cùng pattern. Pattern lát từ góc trên trái tài liệu, nên điểm ảnh (x, y) lấy (x + dx, y + dy) của ảnh vào, đúng như Clone Stamp với nguồn lệch (dx, dy) khi nguồn nằm trong canvas (nguồn (64, 0), vết ở x < 52: luôn trong). H30, H32 biết điều này khi so: Healing Brush với Source Sampled có thể khác Source Pattern ở phép hòa biên.
- Pattern theo tên không có `Idnt` (bản cũ không trả id), xóa bằng `Dlt ` theo tên; hỏng thì ghi chú, không hỏng case.
- Khóa trong `CrnT`: mode của Eraser, Range, Protect Tones, mode của Sponge, Vibrance, Diffusion, kiểu của Spot Healing, Source của Healing Brush, Aligned, Impressionist; Wet Edges và Noise là khóa boolean `wetEdges`, `noise` của Brush. Tên và giá trị liệt kê là ứng viên (`C.OPTIONS`): bản Photoshop của bạn đặt khác thì case đó hỏng kèm danh sách khóa thật, lead thêm ứng viên.
- Đầu cọ elip (Angle 30°, Roundness 40 %) qua `computedBrush`: Photoshop có thể bỏ qua `Angl`, `Rndn` với đầu cọ tròn mẫu; đọc lại bắt được.
- Spot Healing Brush qua `Strk` (nếu `ToolType` không có `SPOTHEALINGBRUSH`): chưa biết Photoshop nhận `Usng` là lớp công cụ đó.
- `Grdn` cùng Method; `Fl  ` của Paint Bucket.
- Eraser Block cần View 100 %: cỡ khối theo màn hình, không theo `size`.

## Cho task sau

- Đọc: `golden::Set set("strokes")`; `if (!set.ready("test_x")) return;`; mỗi `e` của `set.entries()`: `e.json["paint"]` (công cụ, tùy chọn), `set.layers(e)[0].pixels` (ảnh vào), `set.image(e)` (ảnh của Photoshop, 16 bit; `golden::compare`). Path: `golden::strokes_in::path(tên)`, sự kiện bút `strokes_in::events(path)` (`tests/ps_strokes_inputs.h`), hay `strokes/in/paths.json`. Trước B33: `golden::Set("strokes/sample")` (17 dòng, ảnh thiếu).
- `tests/test_ps_strokes.cpp`: `render_p0()` dựng ảnh của một case bằng `Stroke` (mỗi path một nét, `SetPixels`, `composite_tile`); `task_of()` nói case nào chờ task nào; `check_c30()` … `check_h35()` trả false (ảnh chờ task đó): task điền chỗ của mình (hay viết test riêng đọc như trên) và trả true. H30, H32, H33 so thống kê trong `areas` (`area_stats()`).
- C30–C35: `test_ps_strokes` in độ lệch của cọ P0 với từng ảnh Brush cơ bản và tâm chấm `dot`; đổi `kFalloffPower` (`core/dab.cpp`) và các hằng khác theo đó.
