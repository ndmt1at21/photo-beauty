# Ảnh mẫu Photoshop: Image Size, Free Transform, Warp, Image Rotation, Canvas Size (T38)

`resample.jsx` (cạnh `README.md`, dùng `p1/lib.jsx` của B30) làm trong Photoshop các ảnh mà test của T33 (Image Size, `resample()`), X30 (lấy mẫu theo ánh xạ ngược, `warp_tile()`), X31 (Free Transform), X32 (Warp) và T34 (Canvas Size, Image Rotation) so với. Bạn chạy một lần ở B33 cùng các script ảnh mẫu khác. 177 case, khoảng 2,2 MB, khoảng 12 phút.

Mỗi case dựng trong tài liệu RGB 16 bit sRGB mới (như B30): một layer chép từ ảnh vào, rồi các lệnh của case (`ops`), rồi lưu ảnh ghép. Ảnh ra 16 bit, trừ ảnh chụp (`photo*`) và Warp trên ô cờ: 8 bit (ảnh ghép 16 bit làm tròn, không dither: câu 6 của "Cần bạn chốt"), vì chúng so theo ΔE2000 hay một mức 8 bit, còn nhân nội suy và ánh xạ đọc từ ô cờ, dốc 16 bit. Chưa chạy trên Photoshop thật: chỗ chưa chắc ở mục cuối; phần nào sai với bản Photoshop của bạn thì chỉ case dùng nó hỏng (lý do trong `photoshop.txt` và mục `failed` của `index.json`), các case khác vẫn ra ảnh.

## Các bước

1. Trong WSL, ở `~/photo-beauty`, chép script và ảnh vào sang Windows, cạnh `blend.jsx` của B4 (script đọc `p1\lib.jsx`: thư mục `p1` đã chép ở mục P1 của `README.md`; chưa thì chép cả nó):

   ```sh
   mkdir -p /mnt/c/Users/Teko/Downloads/ps-golden/p1 /mnt/c/Users/Teko/Downloads/ps-golden/resample-in
   cp tools/ps-golden/resample.jsx /mnt/c/Users/Teko/Downloads/ps-golden/
   cp tools/ps-golden/p1/lib.jsx /mnt/c/Users/Teko/Downloads/ps-golden/p1/
   cp tests/golden/ps/resample/in/*.png /mnt/c/Users/Teko/Downloads/ps-golden/resample-in/
   ```

2. Photoshop: đóng mọi ảnh, Color Settings như bước 2 của B4 trong `README.md` (bốn điểm, nhất là Use Dither tắt: ảnh 8 bit lưu không dither). Script tự tắt các ô đó nếu đọc được Color Settings, trả lại khi xong, và kiểm trộn RGB không qua gamma 1, đổi 16 sang 8 bit không dither; kiểm không qua thì dừng ngay với câu báo.
   - Image Interpolation (Preferences › General) và màu nền (Background Color của thanh công cụ): script đặt theo từng case trong lúc chạy, xong trả lại; `photoshop.txt` ghi giá trị trước đó.
   - Đừng mở bảng Properties hay Free Transform dở dang: script làm Free Transform, Warp bằng Action Manager.
3. File › Scripts › Browse… → `ps-golden\resample.jsx` → Open. Chờ tới khi hiện "Done", giữa chừng không bấm gì (Photoshop mở, đóng tài liệu liên tục; mỗi Free Transform, Warp còn tạo rồi xóa một bản chép smart object để đọc lại phép biến đổi).
   - "Done: 177 images": xong. "Done: N images, M failed": M case không làm được (lý do trong `photoshop.txt`, mục `failed` của `index.json`), các case còn lại vẫn đúng: chép như bước 4 và gửi lead `photoshop.txt`.
   - "Stopped: …": kiểm thiết lập không qua: làm theo câu báo rồi chạy lại.
4. Chép kết quả vào repo (Photoshop ở máy khác thì trước đó đưa `ps-golden\out\resample` về `C:\Users\Teko\Downloads\ps-golden\out\resample` của máy này), rồi chạy test và báo lead:

   ```sh
   cp /mnt/c/Users/Teko/Downloads/ps-golden/out/resample/* ~/photo-beauty/tests/golden/ps/resample/
   build/default/pb-tests test_ps_resample_set test_ps_resample_maps
   ```

   `test_ps_resample_set` kiểm đủ ảnh theo `index.json`, đúng cỡ và độ sâu, mọi cảnh và lệnh đọc được, cả `tests/golden/ps/resample/` ≤ 3 MB; `test_ps_resample_maps` in Photoshop đọc lại ma trận, lưới có khớp với điểm ảnh của chính nó không. Lead chạy test so của T33, X30–X32, T34 khi các task đó có mã.

## Ảnh vào (`tests/golden/ps/resample/in/`)

`pb-bench ps-resample tests/golden/ps/resample/in` làm (`tests/ps_resample_inputs.h`, `golden::resample`), như ảnh vào của B30: PNG 16 bit không gắn profile, mọi mẫu là mức Photoshop đọc lại đúng (`golden::ps_level`); script gán sRGB (Assign, không Convert). `test_ps_resample_inputs` kiểm file trong repo trùng bộ sinh từng byte.

| File | Cỡ | Nội dung | Để làm gì |
| --- | --- | --- | --- |
| `checker.png` | 100 × 70, RGB | ô cờ 10 px hai màu (sáng 0,80 0,72 0,60; tối 0,20 0,28 0,42, ô trên trái sáng); ba cột ô từ x = 70 chia thành ô 1 px (hai hàng ô đầu), 2 px (hàng 2, 3), 5 px (hàng 4–6) | ô to: đáp ứng bước của nhân ở mọi tỉ lệ (hai màu xa đen, trắng nên vòng gợn không bị cắt); ô nhỏ: thu nhỏ có lọc trước (xám đều) hay răng cưa |
| `ramp.png` | 100 × 70, RGB | R = (x + 0,5)/100, G = (y + 0,5)/70 (tâm điểm ảnh), B = (R² + G²)/2 | sau khi lấy mẫu lại, xoay, warp, mỗi điểm ảnh cho biết nó lấy từ đâu của ảnh vào (nội suy giữ đúng mặt phẳng, xa mép); B: độ lệch bậc hai của nhân (Keys chỉ đúng ở a = −0,5) |
| `photo.png` | 100 × 70, RGB | `photo.png` của B30 cắt từ (96, 120): thịt, ớt đỏ, vòng hành mảnh, lá | chi tiết thật |
| `checker-pad.png`, `ramp-pad.png`, `photo-pad.png` | 160 × 128, RGBA | ảnh trên đặt ở (30, 29) trên nền trong suốt đen; `ramp-pad.png` có B = 0,5 (warp một đường cong tốn gấp ba byte, không thêm gì về ánh xạ) | khung của Free Transform, Warp là biên nội dung (30, 29)–(130, 99); chừa chỗ cho hình sau biến đổi, mép alpha của nội dung |

## Case

Tên: `<lệnh>-<ảnh vào>-<tham số>-<nội suy>`; `m50` là −50, `200`/`50`/`37` là phần trăm của Image Size.

| Nhóm | Case | Ảnh | Cho |
| --- | --- | --- | --- |
| Image Size | `size-<checker\|ramp\|photo>-<cách>-<200\|50\|37>`: 7 cách (Nearest Neighbor, Bilinear, Bicubic, Bicubic Smoother, Bicubic Sharper, Automatic, Preserve Details ở Reduce Noise 0) × 200 % (200 × 140), 50 % (50 × 35), 37 % (37 × 26: 25,9 làm tròn; 37 % của bội số của 8 lại là 3/8 đúng, nên ảnh vào 100 × 70) | 63 (ảnh chụp 8 bit) | T33 |
| | `size-checker-pad-<cách>-<200\|37>`: 6 cách (trừ Preserve Details) × 200 % (320 × 256), 37 % (59 × 47): mép alpha của nội dung (trọng số alpha) | 12 | T33 |
| | `size-ramp-bg-bicubic-<200\|37>`: dốc là Background (mép ảnh không có trong suốt bên ngoài) | 2 | T33 |
| Free Transform | `transform-checker-<rotate30\|scale37\|skew20\|perspective>-<nội suy>`: xoay 30° theo chiều kim đồng hồ, co 37 %, nghiêng ngang 20°, Perspective (cạnh trên hẹp lại 16 px mỗi bên), quanh tâm khung; × 6 nội suy của thanh tùy chọn (Nearest Neighbor, Bilinear, Bicubic, Bicubic Smoother, Bicubic Sharper, Bicubic Automatic) | 24 | X30, X31 |
| | `transform-ramp-<…\|distort\|combo>-bilinear`: bốn phép trên, Distort (bốn góc tùy ý: cả hai số hạng phối cảnh), mọi ô cùng lúc (W 80 %, H 60 %, góc 30°, nghiêng 15° và −10°: Photoshop ghép chúng theo thứ tự nào) | 6 | X31 (ma trận), X30 |
| | `transform-photo-<…>-automatic`: bốn phép, Bicubic Automatic (mặc định của Photoshop) | 4 (8 bit) | X30 |
| | `transform-checker-<distort\|combo>-bicubic` | 2 | X30 |
| Warp | `warp-ramp-<kiểu>-<50\|m50>`: 15 kiểu (Arc, Arc Lower, Arc Upper, Arch, Bulge, Shell Lower, Shell Upper, Flag, Wave, Fish, Rise, Fisheye, Inflate, Squeeze, Twist) ở Bend 50 %, −50 % | 30 | X32 (chỉnh hình theo ánh xạ của dốc) |
| | `warp-ramp-arc-50-h50`, `-v50` (H, V Distortion 50 %), `warp-ramp-arc-50-vertical`, `warp-ramp-wave-50-vertical` (hướng dọc) | 4 | X32 |
| | `warp-ramp-custom-a`, `-b`: lưới Custom một mảnh 16 điểm biết trước (nhẹ: góc giữ, cạnh và trong cong; mạnh: cả góc dời) | 2 | X30 (lưới Bezier), X32 |
| | `warp-checker-<kiểu>-50`, `warp-checker-custom-a`: để nhìn hình | 16 (8 bit) | X32 |
| Image Rotation | `rotate-checker-15-bicubic`, `-automatic` (Image Interpolation Bicubic, Bicubic Automatic), `rotate-ramp-15-bicubic`, `rotate-ramp-m15-bicubic` (ngược chiều), `rotate-photo-15-bicubic`, `rotate-photo-bg-15-bicubic` (Background: góc lộ ra tô màu nền `#2b6cb0`) | 6 (ảnh chụp 8 bit) | T34 |
| Canvas Size | trên `ramp.png` (mỗi điểm ảnh cho biết nó ở đâu): `canvas-ramp-141x101-center` (lề lẻ: điểm lẻ về phía nào), `canvas-ramp-bg-140x100-topleft` (Background, neo trên trái, màu Other `#e9c7b3`), `canvas-ramp-bg-131x99-bottom` (neo giữa dưới, Gray `#808080`), `canvas-ramp-63x45-topright` (thu, neo trên phải), `canvas-ramp-61x41-back` (thu rồi nới lại: layer thường giữ điểm ảnh ra ngoài canvas), `canvas-ramp-bg-61x41-back` (Background: bị cắt, nới lại tô White) | 6 | T34 |

177 case: 134 ảnh 16 bit, 43 ảnh 8 bit.

## `index.json`

Như của B30 (`build/orch/api/b30.md`, `tests/golden.h`): một ảnh một dòng; `canvas` là cỡ tài liệu lúc dựng (cỡ ảnh vào), `bits` của PNG, `layers` một layer (`props` theo `to_json()` của A30; `"background": true` là Background, tên "Background": khóa của A40). Thêm hai khóa của case:

- `ops`: các lệnh làm trên tài liệu sau khi có layer, theo thứ tự, mỗi lệnh đúng dạng giao diện gửi `Docs.run()` (`ImageSizeDialog.qml`, `CanvasSizeDialog.qml`, `RotateCanvasDialog.qml`), thêm phần lõi cần:
  - `{"op": "imageSize", "w", "h" (px), "method"}`: `method` theo khóa `FilterSpec.RESAMPLE` (`nearestNeighbor`, `bilinear`, `bicubic`, `bicubicSmoother`, `bicubicSharper`, `automatic`: Automatic của hộp Image Size), thêm `preserveDetails` (Preserve Details của Photoshop ở Reduce Noise 0; Preserve Details 2.0 là việc của P3). Tỉ lệ của T33 là `w / canvas[0]`, `h / canvas[1]`.
  - `{"op": "rotateCanvas", "angle" (độ, chiều kim đồng hồ), "interp", "color"}`: Image Rotation › Arbitrary; `interp` là Image Interpolation lúc chạy; `color` `#rrggbb` là màu nền Photoshop tô vào góc lộ ra của Background, `null` khi layer không phải Background.
  - `{"op": "canvasSize", "w", "h" (px), "anchor" (0–8 theo hàng từ trên trái, như `AnchorGrid`), "color"}`: `color` phần mở rộng của Background, `null` khi không có Background (như giao diện).
  - `{"op": "freeTransform", "w", "h" (%), "angle" (độ, chiều kim đồng hồ), "skew" [ngang, dọc] (độ), "corners" (Perspective, Distort: bốn góc của khung tới đâu, trên trái, trên phải, dưới phải, dưới trái, x rồi y), "interp", "transform", "warp"}`: các ô của thanh tùy chọn, quanh tâm khung (Reference Point giữa). Khóa nào case không dùng thì không có.
  - `{"op": "warp", "style", "bend", "h", "v" (%: Bend, H Distortion, V Distortion), "orientation" (`horizontal`, `vertical`), "interp", "warp"}`; `style` là tên camelCase của kiểu (`arc`, `arcLower`, `arcUpper`, `arch`, `bulge`, `shellLower`, `shellUpper`, `flag`, `wave`, `fish`, `rise`, `fisheye`, `inflate`, `squeeze`, `twist`) hay `custom` (khi đó không có `bend`, `h`, `v`, `orientation`).
  - `interp` như `Interp` của `render.h`: `nearestNeighbor`, `bilinear`, `bicubic`, `bicubicSmoother`, `bicubicSharper`, `automatic` (Bicubic Automatic).
  - `transform`: ma trận của A31, 9 số theo hàng từ điểm ảnh của layer (như trước khi biến đổi) sang tài liệu, x' = (m0 x + m1 y + m2) / (m6 x + m7 y + m8), góc điểm ảnh ở số nguyên (như `props.transform` của F31).
  - `warp`: `LayerWarp` của A30 để X30 đọc thẳng: `{"matrix" (tài liệu → nội dung: nghịch đảo của `transform`; Warp: đơn vị), "mesh" (`BezierMesh` của A30: `cols`, `rows`, `xs`, `ys`, `points` [x, y] theo hàng, điểm ảnh tài liệu), "interp"}`. `mesh` có ở Warp Custom (lưới của case), và ở kiểu có sẵn khi Photoshop đưa lưới của nó (đọc lại từ bản chép smart object); không có thì X32 tự dựng lưới từ `style`, `bend`, `h`, `v`, `orientation`.
  - Offline (`PB.offline`, mẫu viết tay) `transform`, `warp` là phỏng đoán của script: co W, H; nghiêng dọc (y dời tan v lần x) rồi ngang (x dời tan h lần y); xoay theo chiều kim đồng hồ (y hướng xuống); quanh tâm khung; `corners`: phép chiếu đưa góc khung tới đó. Khi chạy, script làm cùng phép biến đổi trên một bản chép smart object của layer, đọc bốn góc (`smartObjectMore.transform`) và thay bằng ma trận của Photoshop (lệch quá 0,01 px thì `photoshop.txt` ghi).
- `size`: `[w, h]` của ảnh sau các lệnh. Offline: Image Rotation lấy khung bao trọn, làm tròn lên (114,7 × 93,5 thành 115 × 94); sau khi chạy là cỡ thật của Photoshop (`photoshop.txt` ghi khi khác).

Mẫu viết tay: `tests/golden/ps/resample/sample/index.json` (21 dòng, mỗi dạng ở trên một dòng, `"inputs": "../in"`; ở `sample/` vì `test_ps_resample_set` coi `tests/golden/ps/resample/` là bộ thật và đòi đủ PNG). Mỗi dòng trùng từng byte với dòng cùng tên của `PB.offline` (`jscheck` của B30, `build/orch/api/b30.md`):

```sh
jscheck --eval "PB.offline('resample', cases(), {bits: 8, repoInputs: '../in'})" out.json tools/ps-golden/resample.jsx
```

rồi so từng dòng của mẫu với dòng cùng `name` trong `out.json`. Sửa case trong script thì sửa dòng mẫu tương ứng. Chạy thử cả `PB.run` không Photoshop: giả lập `build/orch/api/t38-psmock.jsx` (của F31 thêm Image Size, Canvas Size, Image Rotation, Free Transform, Warp, smart object có warp; nạp sau script): `jscheck --eval "HERE = new Folder('C:/x'); MOCK.run(function () { R.run(); })" out.txt tools/ps-golden/resample.jsx t38-psmock.jsx`; `MOCK.reject = ['Intr', 'quadCorner0:Ofst']`, `MOCK.noMethods = ['AUTOMATIC']`, `MOCK.keepsMesh = false`, `MOCK.ignoreMeshList = true`, `MOCK.failEvents = ['newPlacedLayer']` thử các cách khác của script.

Đọc trong test (`tests/test_ps_resample.cpp`): `golden::Set ps("resample")` (trước B33: `golden::Set("resample/sample")`); `ps.layers(e)[0].pixels` là ảnh vào; `e.json["ops"]` các lệnh; `warp_of(op["warp"])` ra `LayerWarp`, `matrix_of(op["transform"])`, `interp_of(tên)` ra `Interp` (hàm trong test, chép sang test của mình); ảnh 16 bit của Photoshop so qua `golden::ps15`; điểm ảnh của dốc: nguồn là (30 + R · 100, 29 + G · 70) với R, G = `ps15(mẫu) / 32768`.

## Cỡ ước tính

PNG đo như B30 (zlib 9, bộ lọc dòng thích ứng như spng; cách lưu của Photoshop lệch ±15 %) trên phép biến đổi gần đúng viết bằng Python (nhân Keys, song tuyến, gần nhất; warp bằng ánh xạ trơn): ô cờ 16 bit 1–7 KB ở Image Size, 4–36 KB khi xoay, phối cảnh; dốc 16 bit 2–28 KB (B cong ở 200 %), 1–10 KB khi biến đổi, 4–25 KB khi warp (Twist nhiều nhất); ảnh chụp 8 bit 3–50 KB.

| Nhóm | Ảnh | Cỡ |
| --- | --- | --- |
| Ảnh vào (6 file), `sample/index.json` | | khoảng 0,1 MB |
| Image Size | 77 | khoảng 0,76 MB (ảnh chụp 0,37) |
| Free Transform | 36 | khoảng 0,66 MB (ô cờ × 6 nội suy 0,55) |
| Warp | 52 | khoảng 0,36 MB |
| Image Rotation, Canvas Size | 12 | khoảng 0,21 MB |
| `index.json`, `photoshop.txt` | | khoảng 0,15 MB |

Cả bộ khoảng 2,2 MB (1,9–2,6 MB), dưới 3 MB của T38 (`test_ps_resample_set` kiểm). Mọi ảnh 16 bit thì khoảng 2,7 MB: ảnh chụp, Warp trên ô cờ 8 bit chừa chỗ cho cách lưu của Photoshop.

## Chỗ chưa chắc (Action Manager)

Script dựng các phần sau theo mã ScriptListener quen dùng hay DOM, chưa chạy thử. Phần nào sai chỉ làm hỏng case dùng nó; gửi lead `photoshop.txt`, lead sửa script, bạn chạy lại.

- Image Size: `resizeImage(w, h, undefined, ResampleMethod.X)` của DOM (`AUTOMATIC` là Automatic của hộp Image Size, `PRESERVEDETAILS` với Reduce Noise 0); thiếu tên đó thì sự kiện `ImgS` (`Wdth`, `Hght` px, `Intr` = `Nrst`, `Blnr`, `Bcbc`, `bicubicSmoother`, `bicubicSharper`, `automaticInterpolation`, `preserveDetailsUpscale`). `photoshop.txt` ghi cách đã chạy; cỡ ra khác `w × h` thì case hỏng.
- Free Transform: sự kiện `Trnf` trên layer đang chọn, `FTcs` = `Qcsa` (quanh tâm khung), `Ofst` 0, `Wdth`, `Hght` (%), `Angl` (độ), `Skew` (`Pnt ` hai góc), `Intr` (nội suy: `Nrst`, `Blnr`, `Bcbc`, `bicubicSmoother`, `bicubicSharper`, `bicubicAutomatic`); Photoshop không nhận `Intr` thì chạy lại không có nó (khi đó Image Interpolation script đặt cho case quyết định; `photoshop.txt` ghi "without Intr").
- Perspective, Distort (10 case), chưa chắc nhất: bốn góc `quadCorner0`–`3` (trên trái, trên phải, dưới phải, dưới trái), trước là độ dời của từng góc (`Ofst`), rồi là điểm (`Pnt `); bản chép smart object cho biết mỗi cách đưa góc tới đâu, script dùng cách ra đúng chỗ. Không cách nào chạy thì các case đó hỏng: khi đó ghi một lần Perspective bằng ScriptListener (hay Actions) gửi lead. Góc không đổi (Photoshop bỏ qua khóa) thì case hỏng ("the corners stayed").
- Đọc lại phép biến đổi: Convert to Smart Object (`newPlacedLayer`) một bản chép của layer, cùng sự kiện, bốn góc ở `smartObjectMore.transform` (như smart object của F31), rồi xóa bản chép. Photoshop không cho đọc (hay khung của bản chép khác biên nội dung) thì `index.json` giữ ma trận script tính (`photoshop.txt` ghi "nothing read back"); `test_ps_resample_maps` dù sao cũng so ma trận với điểm ảnh của dốc.
- Warp: `Trnf` với khóa `warp` (`warpStyle` = `warpArc`…, `warpValue` Bend, `warpPerspective` H, `warpPerspectiveOther` V, `warpRotate` `Hrzn`/`Vrtc`). Custom thêm `bounds` (khung, điểm ảnh tài liệu), `uOrder`, `vOrder` 4, `customEnvelopeWarp` với `meshPoints` 16 điểm theo hàng, trước là danh sách điểm `rationalPoint` (`Hrzn`, `Vrtc`), rồi là một `rationalPoint` hai danh sách; biên layer không đổi sau đó thì coi như Photoshop không đọc lưới, thử cách kia.
- Lưới của kiểu có sẵn: `smartObjectMore.warp.customEnvelopeWarp.meshPoints` của bản chép smart object đã warp; không có thì đổi warp của bản chép sang Custom (`warpStyle` = `warpCustom`, không lưới) rồi đọc lại; vẫn không thì `index.json` chỉ có kiểu và Bend (lead đã nhận: "nếu lấy được"). `photoshop.txt` ghi nguyên descriptor đọc được.
- Image Rotation › Arbitrary: `rotateCanvas(góc)` của DOM (dương là chiều kim đồng hồ), nội suy theo Image Interpolation (script đặt `ResampleMethod.BICUBIC`, `BICUBICAUTOMATIC`); Background tô `app.backgroundColor` (script đặt theo `color`).
- Canvas Size: `resizeCanvas(w, h, AnchorPosition.X)` của DOM; phần mở rộng của Background là màu nền (script đặt theo `color`), layer thường trong suốt.
- Background: `isBackgroundLayer = true` trên layer duy nhất, sau props.
