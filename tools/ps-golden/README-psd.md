# Bộ PSD thử từ Photoshop (E33)

`psd.jsx` dựng trong Photoshop 57 file PSD nhỏ (256 × 256, mỗi file một tính năng P1) và một PSB rộng 31.000 px, cho bộ đọc, ghi PSD của photo-beauty (M40–M47) và E34 (ΔE2000 so Photoshop, Photoshop mở file của ta). Hai lượt:

- **Lượt một** (bạn chạy ở B33, cùng lần với các script P1 khác): mỗi case một tài liệu, script dựng, đọc lại và kiểm Photoshop làm đúng điều case nói, lưu PSD, ảnh ghép PNG, `index.json`, `photoshop.txt`. 57 file vào `tests/golden/psd/` (trong git), PSB vào `samples/psd/` (ngoài git).
- **Lượt hai** (sau E34): Photoshop mở các file photo-beauty lưu, xuất ảnh ghép và cây layer như nó đọc, vào `tests/golden/psd/read/`.

ExtendScript (ES3) như `p1/blend.jsx` của B30, dùng `p1/lib.jsx`. Chưa chạy trên Photoshop thật: chỗ Action Manager chưa chắc ở mục cuối; phần nào sai với bản Photoshop của bạn thì chỉ case dùng nó hỏng (lý do trong `photoshop.txt`, mục `failed` của `index.json`), các case khác vẫn ra file.

## Các file

Cỡ là ước tính (KB; cách ước ở cuối mục). Mỗi tài liệu RGB 8 bit sRGB 256 × 256 trừ khi ghi khác. Ảnh vào là của B30 (`tests/golden/ps/p1/in/`): `ramp.png` xoay 90° (dải dọc, các hàng giống nhau) làm nền, `ramp.png` xoay 270° cắt ô, `shapes.png` (hình trên nền trong suốt), ô tô màu; mặt nạ từ alpha của `shapes.png` hay hình chữ nhật.

| File | Tính năng | Task | PSD | PNG |
| --- | --- | --- | ---: | ---: |
| `modes` | 27 blend mode của layer, mỗi mode một ô 64 × 32; Dissolve 50% | M40, E34 | 63 | 1 |
| `opacity` | Opacity, Fill riêng và cùng nhau; Fill trên Multiply và 6 trong 8 mode có Fill khác Opacity | M40 | 64 | 2 |
| `groups` | nhóm lồng 4 cấp (Pass Through, Normal cô lập, Multiply 75%, Pass Through); nhóm ẩn, nhóm rỗng, nhóm có mặt nạ | M40, M42 | 104 | 7 |
| `masks` | mặt nạ: alpha của `shapes.png`, Density 50%, Feather 10 px, tắt, không liên kết, Reveal All, Hide All, trên layer có alpha | M42 | 98 | 7 |
| `clip` | chuỗi clipping 4 layer trên `shapes.png` (Multiply 50%, Screen có mặt nạ, adjustment Invert); layer clip vào nhóm | M42 | 108 | 7 |
| `blend-if` | Blend If tách đôi (This Layer, Underlying Layer; xám, R, B); ô G của Advanced Blending tắt | M42 | 53 | 1 |
| `locks-labels` | Background; 4 khóa riêng và chung; 7 nhãn màu và none, nhãn trên nhóm | M40 (A40) | 51 | 2 |
| `edges` | layer rỗng; tràn và nằm hẳn ngoài canvas (điểm ảnh giữ); 1 px; một màu; ẩn | M40, M41 | 83 | 5 |
| `names` | tên Unicode (tiếng Việt, Nhật, Hy Lạp, Kirin, emoji ngoài BMP), tên dài hơn 31 ký tự (`luni`) | M40 | 32 | 1 |
| `advanced` | Advanced Blending của P2 khác mặc định (Knockout Shallow, bốn ô): giữ, notice | M42 | 79 | 13 |
| `channels-guides` | 2 kênh alpha (alpha của `shapes.png`; chữ nhật, Selected Areas, xanh 70%), 4 guide, 2 cái ở giữa điểm ảnh | M42 | 80 | 13 |
| `spot` | kênh spot (P3): giữ, notice; PNG không có mực spot | M42 | 39 | 1 |
| `resources` | 300 ppi (0x03ED); XMP, IPTC (tiêu đề, tác giả, chú thích tiếng Việt, bản quyền, từ khóa); layer đang chọn | M40, M41 | 73 | 12 |
| `no-composite` | Maximize Compatibility tắt: không có ảnh ghép thật (0x0421); PNG vẫn là ảnh ghép của Photoshop | M40, M41 | 47 | 13 |
| `adj-brightness-contrast` | Brightness/Contrast mới (trái), Use Legacy (phải) | M43 (D33–D39) | 42 | 1 |
| `adj-levels` | Levels RGB, Red, Blue; mặt nạ từ alpha của `shapes.png` | M43 | 47 | 1 |
| `adj-curves` | Curves chữ S trên RGB, hai đầu Green dời, điểm sát hai đầu trên Blue | M43 | 40 | 1 |
| `adj-exposure`, `adj-vibrance` | khác mặc định, mặt nạ chữ nhật | M43 | 40 + 40 | 1 + 1 |
| `adj-hue-saturation` | Master, Reds, Blues (dải riêng) bên trái; Colorize bên phải | M43 | 42 | 1 |
| `adj-color-balance` | ba tông, Preserve Luminosity tắt | M43 | 40 | 1 |
| `adj-black-white` | sáu thanh, Tint | M43 | 40 | 1 |
| `adj-photo-filter` | màu riêng, Density 60%, Preserve Luminosity tắt | M43 | 40 | 1 |
| `adj-channel-mixer` | ba kênh ra có Constant (trái), Monochrome (phải) | M43 | 42 | 1 |
| `adj-color-lookup` | LUT `.cube` 5 × 5 × 5 nhúng (`e33-warm.cube`, script ghi cạnh các file) | M43 | 43 | 1 |
| `adj-invert`, `adj-posterize`, `adj-threshold` | mặt nạ chữ nhật; Posterize 5, Threshold 100 | M43 | 40 × 3 | 1 × 3 |
| `adj-gradient-map` | ba điểm dừng, một điểm giữa, độ mờ; Method Smooth, Dither | M43 | 40 | 1 |
| `adj-selective-color` | Relative (trái), Absolute (phải) | M43 | 42 | 1 |
| `fill-solid` | Solid Color Multiply 80% có mặt nạ; 30% giữ mặt nạ trắng Photoshop tạo | M43 (D40) | 66 | 12 |
| `fill-gradient` | Gradient fill theo dải: 5 kiểu × 5 Method, Reverse, Scale, Dither, Align tắt, Offset | M43 (D30, D40) | 101 | 8 |
| `fx-drop-shadow` | Drop Shadow: Global Light (120°, 30°), Spread, Noise, không knock out, tắt | M44 (L30–L34) | 79 | 25 |
| `fx-stroke` | Stroke: ba vị trí, cỡ 3, 10, 6, 50; Multiply 60% | M44 | 79 | 25 |
| `fx-color-overlay` | Color Overlay bốn mode; trên layer Fill 0 | M44 | 69 | 21 |
| `fx-gradient-overlay` | Gradient Overlay bốn kiểu, Scale, Reverse, Align tắt, Dither, Offset, Multiply 70% | M44 | 79 | 25 |
| `fx-p2` | hiệu ứng P2 (Inner Shadow, Outer Glow, Bevel & Emboss, Satin, Inner Glow), 2 Drop Shadow (`lmfx`), Stroke gradient, cả bộ ẩn: giữ, notice | M44 | 79 | 25 |
| `so-placed` | `shapes.png` đặt nhúng, co 50%, xoay 30° | M45 (A31) | 55 | 4 |
| `so-layers` | hai layer thành smart object (PSB nhúng), co 75%; bản sao dùng chung nội dung, xoay −20° | M45 | 128 | 11 |
| `so-filter` | smart filter Gaussian Blur 3 px (P2): giữ, notice | M45 | 79 | 7 |
| `so-linked` | đặt liên kết (P2; đường dẫn của máy làm ra nó): giữ, notice | M45 | 43 | 5 |
| `text` | chữ Space Grotesk: point text có tracking; paragraph text căn giữa, tiếng Việt; Anti-aliasing None | M46 (Y30) | 81 | 29 |
| `shape-rectangle` | Rectangle live, mỗi góc một bán kính, Stroke 4 px trong | M46 (Y32) | 29 | 1 |
| `shape-ellipse` | Ellipse, Stroke 6 px giữa | M46 | 28 | 1 |
| `shape-triangle` | Triangle (Photoshop 2021), góc bo 6 px, Stroke 3 px ngoài | M46 | 29 | 1 |
| `shape-polygon` | Polygon 6 cạnh, Star Ratio 60%, không stroke | M46 | 29 | 1 |
| `shape-line` | Line 6 px | M46 | 27 | 1 |
| `shape-path` | shape layer từ path, không phải live shape (P2): notice | M46 | 27 | 1 |
| `vector-mask` | mặt nạ vector trên layer điểm ảnh (P2): giữ, notice | M46 | 51 | 1 |
| `depth16` | RGB 16 bit (layer ZIP có dự đoán), cắt còn 128 × 128 | M40, M41 | 127 | 6 |
| `gray8`, `gray16` | xám 8 bit; xám 16 bit cắt 128 × 128 | M40, M41 | 45 + 54 | 5 + 3 |
| `cmyk` | CMYK 8 bit, hai layer, hồ sơ CMYK nhúng (khoảng 560 KB của 622); PNG sRGB 16 bit | M47 | 622 | 36 |
| `lab` | Lab 8 bit, hai layer; PNG sRGB 16 bit | M47 | 70 | 38 |
| `rgb32` | 32 bit (`Lr32`), cắt 64 × 64; ảnh ghép TIFF số thực 32 bit (~50 KB) và PNG qua Exposure and Gamma | M47 | 65 | 1 |
| `indexed` | Indexed 64 màu không dither (đã gộp): không layer, chỉ ảnh ghép | M47 | 23 | 5 |
| `bitmap` | Bitmap (50% Threshold, đã gộp): 1 bit | M47 | 14 | 5 |
| `psb-wide` (`samples/psd/`) | PSB 31.000 × 256: fill layer, dải ngang hết chiều rộng, `shapes.png` ở hai đầu (một cái quá 30.000 px), nhóm ở giữa | M40, M41 | ~750 | TIFF |

Vào git (`tests/golden/psd/`): 57 PSD khoảng 3,7 MB, PNG 0,4 MB, `rgb32.tif` 0,05 MB, `index.json`, `cases.json`, `photoshop.txt`, `e33-warm.cube` khoảng 0,4 MB: **tổng khoảng 4,5 MB** (sai số ±50%: 3–6 MB). Riêng `cmyk.psd` khoảng 0,6 MB vì hồ sơ CMYK làm việc nhúng trong file (U.S. Web Coated (SWOP) v2 557 KB, Coated FOGRA39 654 KB): giữ, vì M47 và E34 cần đúng hồ sơ Photoshop đã dùng khi chuyển ra PNG; nặng quá thì chuyển riêng file đó sang `samples/psd/`. Ngoài git (`samples/psd/`): `psb-wide.psb` khoảng 0,75 MB và ảnh ghép của nó (PNG nếu Photoshop ghi được ở 31.000 px, không thì TIFF LZW, 0,1–1 MB). Cỡ thật của mỗi file có trong `index.json` sau lượt một (`psd.bytes`, `pngInfo.bytes`).

Cách ước (`build/est/` của worktree E33, không vào git): dựng gần đúng điểm ảnh từng layer theo case (ảnh vào xoay, ô, tô, dời; mặt nạ), nén như Photoshop (PackBits ở 8 bit, đếm byte hàng 2 byte ở PSD, 4 ở PSB; ZIP có dự đoán ở 16, 32 bit), ảnh ghép Normal (bỏ qua adjustment, style) thành ảnh ghép của file (RLE ở 8 bit, thô ở 16, 32 bit) và PNG (zlib 9, bộ lọc thích nghi); cộng phần cố định mỗi file khoảng 13 KB (thumbnail JPEG, XMP, ICC sRGB 3 KB), mỗi layer 0,3 KB, descriptor adjustment khoảng 1 KB, style 5 KB, chữ 14 KB, smart object 3 KB cộng nội dung nhúng. Vì vậy nền là `ramp.png` xoay (PackBits mỗi hàng chỉ 8 đoạn), không có `photo.png` (một layer ảnh chụp khoảng 200 KB), và tài liệu 16, 32 bit cắt nhỏ (ảnh ghép 16 bit 256² không nén là 512 KB).

## Lượt một: các bước

1. Trong WSL, ở `~/photo-beauty`, chép script, `lib.jsx` và ảnh vào sang thư mục `ps-golden` của B4 (đã làm mục P1 của `README.md` thì `p1\` đã có, chỉ cần dòng `psd.jsx`):

   ```sh
   mkdir -p /mnt/c/Users/Teko/Downloads/ps-golden/p1/in
   cp tools/ps-golden/psd.jsx /mnt/c/Users/Teko/Downloads/ps-golden/
   cp tools/ps-golden/p1/lib.jsx /mnt/c/Users/Teko/Downloads/ps-golden/p1/
   cp tests/golden/ps/p1/in/*.png /mnt/c/Users/Teko/Downloads/ps-golden/p1/in/
   ```

   `psd.jsx` đọc `p1\lib.jsx` và ảnh vào ở `p1\in\` cạnh nó; ghi ra `out\psd\`, `out\psd-big\` (lượt hai: `out\psd-read\`), không đụng các file B4 ở `out\`.
2. Font cho `text`: cài Space Grotesk trên Windows từ cả hai file `ui/fonts/SpaceGrotesk.ttf` (font biến thiên: Light tới Bold) và `ui/fonts/SpaceGrotesk-SemiBold.ttf` (chép sang Windows, nhấp phải › Install for all users), rồi mở lại Photoshop. Thiếu font nào thì script dùng font Space Grotesk khác (không có thì Arial), ghi vào `photoshop.txt`, và `index.json` ghi font đã dùng.
3. Photoshop: đóng mọi ảnh; Color Settings như bước 2 của B4 (sRGB, Use Dither tắt, Blend RGB Colors Using Gamma tắt, ba ô Ask tắt). Working CMYK, Gray để mặc định (`photoshop.txt` ghi tên; `cmyk.psd` nhúng hồ sơ CMYK, `gray8.psd`, `gray16.psd` hồ sơ Gray). Script tự tắt các ô đó nếu đọc được Color Settings, tự đặt preference Maximize PSD and PSB File Compatibility cho từng file, trả lại mọi thứ khi xong; nó kiểm trộn RGB không qua gamma 1 và đổi 16 sang 8 bit không dither, không qua thì dừng ngay, chưa làm file nào.
4. File > Scripts > Browse… → `ps-golden\psd.jsx` → Open. Không có thư mục `ps-golden\ours\` (hay nó không có PSD) thì lượt một chạy luôn; có thì script hỏi: No là lượt một. Chờ tới khi hiện "Done", giữa chừng không bấm gì (Photoshop mở, đóng tài liệu liên tục): khoảng 10–15 phút cho 57 file, PSB thêm vài phút.
   - "Done: N PSD files": xong. "Done: N PSD files, M failed": M case không làm được (lý do trong `out\psd\photoshop.txt` và mục `failed` của `index.json`), các file còn lại vẫn đúng: chép như bước 5 và gửi lead `photoshop.txt`; lead sửa `psd.jsx`, bạn chạy lại cả script (ghi đè `out\psd\`).
   - "Stopped: …": kiểm thiết lập không qua (thường là Color Settings): làm theo câu báo rồi chạy lại.
5. Chép kết quả vào repo (Photoshop ở máy khác thì trước đó đưa `ps-golden\out\psd*` về `C:\Users\Teko\Downloads\ps-golden\out\` của máy này):

   ```sh
   mkdir -p ~/photo-beauty/tests/golden/psd ~/photo-beauty/samples/psd
   cp /mnt/c/Users/Teko/Downloads/ps-golden/out/psd/* ~/photo-beauty/tests/golden/psd/
   cp /mnt/c/Users/Teko/Downloads/ps-golden/out/psd-big/* ~/photo-beauty/samples/psd/
   du -sh ~/photo-beauty/tests/golden/psd
   ```

   Rồi báo lead (số file, số case hỏng, cỡ): M40–M47 và E34 đọc các file đó khi có, thiếu thì bỏ qua có lý do.

## Lượt hai (sau E34)

1. E34 ghi bản photo-beauty lưu của từng file ra một thư mục (đề xuất `build/psd-ours/`; E34 chốt và ghi lệnh). Chép sang Windows:

   ```sh
   mkdir -p /mnt/c/Users/Teko/Downloads/ps-golden/ours
   cp build/psd-ours/*.ps[db] /mnt/c/Users/Teko/Downloads/ps-golden/ours/
   ```

2. Photoshop: File > Scripts > Browse… → `ps-golden\psd.jsx`; script thấy file trong `ours\` và hỏi: **Yes** là lượt hai. Vài giây mỗi file. Script mở từng file như người dùng mở, ghi `out\psd-read\`: `<file>.png` (ảnh ghép như lượt một), `index.json` (canvas, mode, độ sâu, hồ sơ, ppi, cây layer như Photoshop đọc, số layer, kênh alpha, guide, `crc32` và `bytes` của file), `photoshop.txt`.
3. Chép về:

   ```sh
   mkdir -p ~/photo-beauty/tests/golden/psd/read
   cp /mnt/c/Users/Teko/Downloads/ps-golden/out/psd-read/* ~/photo-beauty/tests/golden/psd/read/
   ```

   E34 so cây layer, ảnh ghép với bản của nó; `crc32` khác file hiện tại của E34 nghĩa là file đã đổi từ lần chạy đó (chạy lại lượt hai).

## Script giữ gì, kiểm gì

- Tài liệu: mỗi case một tài liệu mới 72 ppi (point là pixel), trong suốt, mặc định RGB 8 bit gán `sRGB IEC61966-2.1` (Assign, không Convert); `mode`, `depth` của case đổi chúng (xám, CMYK, Lab theo working space). Khác B30 (tài liệu luôn 16 bit): ở đây 8 bit là mặc định vì đó là PSD thường gặp, Photoshop ghi layer 8 bit bằng RLE, 16 bit bằng ZIP có dự đoán, M41 phải ghi như thế. Ảnh vào 16 bit chép sang tài liệu 8 bit thì Photoshop làm tròn (Use Dither tắt).
- Dựng (như `PB.build` của `lib.jsx` cộng các khóa mới): layer chép từ ảnh vào (Duplicate Layer, xoay), giữ trong ô `cell`, tô `paint`, dời `move`; nhóm; adjustment, fill (bỏ mặt nạ trắng Photoshop tự thêm, trừ khi case có mặt nạ), chữ, hình, smart object; rồi thuộc tính, mặt nạ, Blend If, kênh, nhãn màu, Advanced Blending, mặt nạ vector, style; Background và khóa sau cùng (khóa chặn các sửa trước nó); rồi các khóa của tài liệu (crop, kênh alpha, spot, guide, metadata, ppi, đổi mode, layer chọn).
- Kiểm, lệch là case hỏng có lý do: cây layer (tên, thứ tự, nhóm); từng layer qua DOM và Action Manager: kind, blend, Opacity, Fill (theo byte, ±1), hiện, clipping, 4 khóa, nhãn màu, Background, mặt nạ (Density, Feather, tắt, liên kết), Blend If, kênh, loại adjustment, mặt nạ vector, có style, chữ (nội dung, font); descriptor của adjustment, fill, fill của hình và style đọc lại so từng khóa script đặt (px ±0,05; %, độ ±0,5; màu ±0,6; danh sách object khớp không theo thứ tự). Khóa Photoshop không trả lại, và chuỗi khác (chỉ là tên: tên gradient, file LUT) chỉ ghi "not read back as set" vào `photoshop.txt`, không hỏng. Smart object: liên kết hay nhúng đúng như case, biến đổi đọc lại. Tài liệu: canvas, ppi, kênh alpha và spot (tên, kiểu), guide (lệch ≤ 0,5 px thì ghi chú và `index.json` lấy giá trị đọc lại), tác giả.
- Lưu: Save As PSD (PSB khi case có `psb`) bằng Action Manager với `maximizeCompatibility` (bật, trừ `compat: false`), preference cùng giá trị; rồi đọc header của file: version 1 hay 2, cỡ, độ sâu, mode, ID các image resource theo thứ tự, hasRealMergedData của 0x0421 (phải khớp Maximize Compatibility), nén ảnh ghép; vào `psd` của `index.json`.
- Ảnh ghép `<case>.png` theo độ sâu tài liệu (lead chốt 25/10): tài liệu 8 bit ra PNG 8 bit, 16 và 32 bit ra 16 bit; CMYK, Lab ra bản sao 16 bit chuyển sang sRGB (Relative Colorimetric, Black Point Compensation, không dither; cách M47 sẽ làm); 32 bit còn TIFF số thực 32 bit `<case>.tif` (ảnh ghép đúng của tài liệu), PNG qua HDR Toning, Exposure and Gamma (0, 1); Bitmap ra bản sao xám; PNG không ghi được thì TIFF LZW. `bits` của `index.json` là của PNG thật (script đọc IHDR, lệch dự kiến thì ghi chú).
- `photoshop.txt`: phiên bản, hệ điều hành, Color Settings trước và sau, kết quả kiểm trộn và dither, preference Maximize Compatibility, từng case: từng layer như Photoshop có (descriptor rút gọn), biến thể Action Manager đã dùng (hình), khóa không đọc lại, header của file, PNG; lỗi.

### Vì sao

- PNG theo độ sâu, không luôn 16 bit như dòng E33: ảnh ghép của tài liệu 8 bit trong Photoshop đã là 8 bit, PNG 16 bit của nó chỉ là v × 257, gấp đôi cỡ mà không thêm gì. Lead nhận 25/10.
- 57 file chứ không khoảng 40: mỗi file một tính năng để mỗi task M40–M47 tìm thấy file của mình và một case hỏng chỉ mất một tính năng; phần thêm (adjustment mỗi loại một file, 6 hình, mode màu) cho M43, M46, M47. Lead nhận 25/10.
- Không có Duotone, Multichannel: dựng chúng bằng Action Manager chưa chắc (Duotone cần mực và đường cong; Multichannel bỏ layer), mà M47 đọc chúng như Indexed, Bitmap (một layer từ ảnh ghép, có notice), hai mode đó đã có file. Lead nhận 25/10.

## `index.json`, `cases.json`

`index.json` theo định dạng của B30 (`tests/golden.h` đọc được: `script`, `photoshop`, `os`, `date`, `complete`, `inputs`, `failed`, `images`), thêm `pass` (1, 2). `tests/golden/psd/cases.json` là index của lượt một trước khi có Photoshop (các case như script định làm, chưa có kết quả), sinh bằng `jscheck` (`build/orch/api/b30.md`):

```sh
jscheck --eval "E.offline('psd')" tests/golden/psd/cases.json tools/ps-golden/psd.jsx   # 'psd-big' cho PSB
```

Sửa case trong `psd.jsx` thì sinh lại. So `cases.json` với `index.json` của lần chạy thật thấy Photoshop đổi gì (font thay, guide làm tròn, biến đổi smart object, cây layer sau đổi mode).

Mỗi case trong `images`: `name`, `canvas` (`index.json`: sau crop; `cases.json`: trước), `bits` (của PNG), `note` (tiếng Anh: file có gì), `layers` (dưới lên), các khóa của case, và sau lượt một các kết quả. Layer như B30 (`kind` `pixel`, `group`, `adj`; `src`, `rotate`, `mask`, `props`, `children`), thêm kind `fill`, `text`, `shape`, `so` (A30, A31) và khóa:

- `cell` [x, y, w, h]: layer chỉ giữ điểm ảnh trong ô đó; `move` [dx, dy]: rồi dời (điểm ảnh ra ngoài canvas vẫn giữ); `paint` [[x, y, w, h, '#rrggbb'], …]: chữ nhật tô màu (layer không `src` là layer mới rỗng rồi tô).
- `maskRect` [x, y, w, h]: mặt nạ chỉ lộ chữ nhật đó; `maskAll` `white` | `black`: Reveal All, Hide All; `mask`: như B30 (alpha của một ảnh vào).
- `smart` {`id`, `file` (ảnh vào đặt vào) | `layers` (layer của chính nó, Convert to Smart Object) | chỉ `id` (bản sao của smart object trước, dùng chung nội dung), `linked`}; `place` {`scale` %, `rotate` độ theo chiều kim đồng hồ, `move`}; sau lượt một `smart.canvas` (cỡ nguồn), `smart.fileName` và `props.transform` (A31: ma trận 3 × 3 theo hàng từ điểm ảnh của nguồn sang tài liệu, x' = m0 u + m1 v + m2, y' = m3 u + m4 v + m5, từ ba góc Photoshop báo); `smartFilters` [{`filter`: `gaussianBlur`, `radius`}].
- `advanced` {`knockout` `shallow` | `deep`, `blendInterior`, `blendClipped`, `transparencyShapes`, `maskHidesEffects`, `vectorMaskHidesEffects`} (P2); `vectorMask` [[x, y], …] (đa giác).

`props` như B30 (khóa A30, thứ tự `to_json()`), thêm `lockTransparency`, `lockPixels`, `lockPosition`, `lockAll`, `background`, `color` (nhãn màu, A40: `red`, `orange`, `yellow`, `green`, `blue`, `violet`, `gray`), `fx`, `content`, `source`, `transform`:

- `vals` của adjustment theo khóa của `AdjustSpec`; chỗ Photoshop giữ một bộ giá trị cho mỗi kênh, dải, tông, màu mà AdjustSpec có một bộ cùng ô chọn thì mỗi bộ là một object theo tên của ô chọn (bỏ khóa ô chọn): Levels {`RGB`, `Red`, `Green`, `Blue`: {`in0`, `gam`, `in1`, `out0`, `out1`}}; Curves `pts` {`RGB`, …: [[vào, ra], …]}; Hue/Saturation {`Master`, `Reds` … `Magentas`: {`h`, `s`, `l`, `span` [4 độ] (dải màu)}, `colorize`} (Colorize: `h` 0–360, `s` 0–100 của Master là màu); Color Balance {`Shadows`, `Midtones`, `Highlights`: {`cr`, `mg`, `yb`}, `pl`}; Channel Mixer {`Red`, `Green`, `Blue`: {`r`, `g`, `b`, `k`}} hay `mono` cùng {`Gray`: …}; Selective Color {`Reds` … `Blacks`: {`c`, `m`, `y`, `k`}, `rel` (0 Relative, 1 Absolute)}. Còn lại như AdjustSpec: Brightness/Contrast `b`, `c`, `legacy`; Exposure `e`, `o`, `g`; Vibrance `v`, `s`; Black & White `r`, `y`, `g`, `c`, `b`, `m`, `tint`, `tintColor`; Photo Filter `pf` 1 (màu riêng), `pf1`, `d`, `pl`; Color Lookup `lf0` (tên file), `lut` (nội dung `.cube`), `dither`; Posterize `lv`; Threshold `t`; Gradient Map `gradient` (JSON gradient của D30), `method`, `dither`, `rev`.
- `content` của fill (D40): {`color`} hay GradientFill của D30 {`gradient` {`colors` [{`p`, `c` [r, g, b], `m`}], `alphas` [{`p`, `a`, `m`}], `smoothness`}, `method`, `style`, `angle`, `scale`, `reverse`, `dither`, `align`, `offset` [x %, y %]}.
- `fx` (khóa của LayerStyleDialog, L30): `dropShadow` {`on`, `blend`, `color`, `opacity` %, `angle`, `distance`, `spread`, `size`, `noise`, `knockOut`, `globalLight`, `antiAliased`}, `stroke` {`size`, `position`, `blend`, `opacity`, `color`; `fillType` `gradient` cùng khóa GradientFill}, `colorOverlay`, `gradientOverlay`; P2 (tên của script): `innerShadow`, `outerGlow`, `innerGlow` (`range`), `bevelEmboss` (`angle`, `altitude`, `depth`, `size`), `satin`, `dropShadowMulti` [...]; `on: false` ẩn cả bộ; `scale` (Scale Effects). Case có `globalLight` [góc, độ cao].
- `source` của chữ (Y30 chưa chốt; dạng của script): {`text`, `font` (tên PostScript), `size` (pt, bằng px ở 72 ppi), `color`, `tracking`, `aa` (`none`, `sharp`, `crisp`, `strong`, `smooth`), `at` [x, y] (đầu đường chân chữ, point text) | `box` [x, y, w, h] (paragraph text), `align`}.
- `source` của hình (Y32 chưa chốt; dạng của script): {`type` `rectangle` | `ellipse` | `triangle` | `polygon` | `line` | `path`, `box` [trái, trên, phải, dưới], `radii` [tl, tr, br, bl], `radius`, `sides`, `star` %, `from`, `to`, `weight`, `points`, `fill` '#rrggbb' | null, `stroke` {`width`, `color`, `align` `inside` | `center` | `outside`, `cap`, `join`} | null}.

Khóa của case: `mode` (`rgb`, `gray`, `cmyk`, `lab`), `depth` (8, 16, 32), `big` (vào `psd-big`), `psb`, `compat` (Maximize Compatibility, mặc định true), `globalLight`, `crop` [x, y, w, h] (sau các layer, cắt bỏ điểm ảnh ngoài), `alphas` [{`name`, `from` (alpha của ảnh vào) | `rect`, `kind` `masked` | `selected`, `color`, `opacity`}], `spots` [{`name`, `rect`, `color`, `solidity`}], `guides` [[`v` | `h`, px], …], `meta` {`title`, `author`, `caption`, `copyright`, `keywords`}, `ppi`, `convert` (`indexed`, `bitmap`: gộp rồi đổi mode; `layers` khi đó là cây đọc lại, `madeOf` là cây trước khi gộp), `selected` (tên layer đang chọn khi lưu). Kết quả của lượt một: `file`, `psd` {`version`, `channels`, `height`, `width`, `depth`, `mode`, `resources`, `merged`, `compression`, `bytes`}, `png`, `pngInfo` {`width`, `height`, `bits`, `color`, `bytes`}, `tif`, `profile`.

Lượt hai, mỗi file một mục: `name`, `file`, `bytes`, `crc32`, `canvas`, `mode`, `depth`, `profile`, `ppi`, `layers` [{`kind`, `props` (chỉ khóa khác mặc định, như `to_json()`; nhóm luôn có `blend`), `mask`, `vectorMask`, `fx` (tên các hiệu ứng, `hidden` khi ẩn cả bộ), `smart`, `text`, `children`}], `count`, `alphas`, `guides`, `png`, `pngInfo`, `tif`.

## Chỗ chưa chắc (Action Manager)

Script dựng các phần sau theo mã ScriptListener quen dùng, chưa chạy thử. Sai thì chỉ case dùng nó hỏng; gửi lead `photoshop.txt`.

- Live shape Triangle, Polygon (Photoshop 2021): lớp `triangle` hay `polygon` với `sides`, `polygonCornerRadius`, `starRatio`; Line: `Ln  ` (Line cũ) rồi `line`. Script thử lần lượt, biến thể đã dùng ghi trong `photoshop.txt` (`shape-*`). Cũng chưa chắc: `strokeStyle` của hình; shape layer từ path (Solid Color fill tạo khi path đang chọn, `shape-path`); mặt nạ vector (`Mk` `Pth` At `vectorMask` Using Target, `vector-mask`).
- Method của gradient (Photoshop 2023: `gradientsInterpolationMethod`, kiểu `gradientInterpolationMethodType`, mã `Perc`, `Lnr `, `Gcls`, `Smoo`, `Stri`) trong fill, Gradient Overlay, Stroke, Gradient Map. `Mdpn` (điểm giữa) script đặt theo `m` của chính điểm dừng đó; D30 hiểu `m` là đoạn tới điểm dừng sau: E34 sẽ thấy nếu lệch một đoạn.
- Color Lookup: `LUT3DFileData` (byte của `.cube`), `LUTFormatCUBE`, tên file ở `Nm  ` và `LUT3DFileName`, không có hồ sơ. Hue/Saturation Colorize: giá trị ở mục Master của `Hst2`. Channel Mixer Monochrome: kênh ra `Gry `. Photo Filter màu RGB: Photoshop có thể giữ màu dạng Lab (đọc lại không so được, chỉ ghi "not read back as set").
- Global Light (`gblA` với `gagl`, `globalAltitude`); hiệu ứng P2, nhiều Drop Shadow (`dropShadowMulti`); ẩn cả bộ hiệu ứng (`Hd  ` trên `Lefx`).
- Advanced Blending (`knockout`, `blendInterior`, `blendClipped`, `transparencyShapesLayer`, `layerMaskAsGlobalMask`, `vectorMaskAsGlobalMask`); nhãn màu (`Clr `); khóa đọc lại qua `layerLocking`.
- Smart object: Place (`Plc `, `Lnkd` cho liên kết), Convert to Smart Object (`newPlacedLayer`), bản sao dùng chung nội dung (Duplicate), smart filter (Gaussian Blur của DOM trên smart object); đọc lại `smartObjectMore` (`transform` 8 số, `size`) và `smartObject` (`fileReference`, `linked`): thiếu thì case báo "no smart object transform read back".
- Lưu: PSB (`largeDocumentFormat`, rồi `Pht8`); preference `maximizeCompatibility`; PNG ở 31.000 px (không được thì TIFF).
- 32 bit xuống 16 bit qua HDR Toning (`CnvM` với `hdrOptions` Vrsn 6, Mthd 0, Exposure 0, Gamma 1): rất chưa chắc; hỏng thì `rgb32` chỉ có TIFF (ghi chú, không hỏng case).
- Layer 8 bit chép từ ảnh vào 16 bit: Photoshop làm tròn (Use Dither tắt) hay dither; kênh alpha kiểu Selected Areas có thể lưu giá trị đảo (`channels-guides`).
- Font của biến thể font biến thiên (`SpaceGrotesk-Regular` của `SpaceGrotesk.ttf`): Photoshop có thể đặt tên PostScript khác; script dùng font Space Grotesk khác và ghi lại.
- `$.fileName`: script lấy thư mục của chính nó ở dòng đầu (`var HERE = …`, trước `#include`), không đọc trong `lib.jsx`.
