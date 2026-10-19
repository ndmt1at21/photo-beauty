# photo-beauty: kế hoạch trình chỉnh ảnh tương đương Photoshop, hiệu năng cao, đa nền tảng

Lập 16/10/2026; sửa cùng ngày: phạm vi là tương đương Photoshop, retouch chân dung là một điểm mạnh riêng. Stack, hạ tầng và một phần thuật toán lấy từ snapone ([plan.md](../snapone/plan.md), [architecture.md](../snapone/docs/architecture.md)); file này chỉ ghi phần riêng của một trình chỉnh theo layer. Tên app, đuôi file là tên tạm.

## Mục tiêu và nguyên tắc

Mục tiêu là một trình chỉnh ảnh tương đương Photoshop cho Windows, macOS, Linux (x64 và ARM64): đủ các nhóm chức năng của Photoshop (công cụ, layer, vùng chọn, mặt nạ, adjustment, bộ lọc, chữ, vector, mode màu, tự động hóa, AI), mở và lưu PSD, tên lệnh và phím tắt như Photoshop để người dùng chuyển sang không phải học lại. Ba điểm mạnh so với Photoshop:

- **Nhanh và nhẹ hơn trên cùng máy:** tài liệu vài trăm MP hay hàng nghìn layer vẫn mượt trên laptop 16 GB (mục Chỉ tiêu hiệu năng).
- **RAW tốt:** mở và chỉnh RAW bằng engine của snapone, cả dạng smart object và Camera Raw Filter.
- **Retouch chân dung:** tách tần số, làm mịn da giữ vân, mặt nạ da, mắt, môi tự động, liquify theo khuôn mặt, retouch cả loạt ảnh.

Danh sách chức năng bên dưới là bảng tương đương: mỗi dòng là một nhóm công cụ, menu hay panel của Photoshop, chia theo mức. Làm chức năng dùng hằng ngày trước (P0–P1), chức năng chuyên nghiệp sau (P2), phần ít dùng và AI tạo sinh cuối (P3). MVP sau khoảng 8 tháng, bản chuyên nghiệp sau khoảng 15 tháng, đủ bảng sau khoảng 24 tháng, với đội 5–8 kỹ sư C++ và QML; đội 3 người thì gấp đôi. P0 ngắn vì build, CI, GPU HAL, scheduler, màu, đóng gói, báo crash đã có trong snapone.

Sáu nguyên tắc:

- **Không chặn luồng giao diện:** đọc ghi file, giải mã, nén, áp bộ lọc full size, lưu đều chạy nền; lưu không bao giờ làm đứng tay.
- **GPU trước, CPU dự phòng:** mỗi kernel có bản CPU cùng công thức để test và để chạy khi không có GPU, như snapone.
- **Chỉ tính thứ cần:** chỉ phần nhìn thấy, ở mức zoom đang xem, chỉ ô bẩn; bản nhanh khi kéo, bản đủ khi thả tay. Không có gì đổi thì không vẽ lại: CPU, GPU về 0 khi người dùng dừng tay.
- **Mọi thứ là ô, chép khi ghi:** layer, mặt nạ, vùng chọn, nét cọ đang vẽ, cache ghép đều là lưới ô; undo, nhân layer, snapshot, lưu nền chỉ chép con trỏ ô.
- **Bộ nhớ có ngân sách:** RAM, VRAM, đĩa mỗi loại một hạn mức; vượt thì nén rồi đẩy ô nguội ra đĩa. Tài liệu lớn hơn RAM vẫn mở, vẽ được; app không bao giờ hết bộ nhớ.
- **Đo trước khi tối ưu, dùng lại trước khi viết:** Tracy và benchmark từ tuần đầu; module nào snapone đã có thì chép; thêm thư viện phải có lý do đo được.

## Cần bạn chốt

Đề xuất in đậm; các bước làm theo đề xuất cho tới khi bạn đổi. Điểm 1 bạn đã chốt ngày 16/10/2026, điểm 2 ngày 17/10/2026.

1. **Phạm vi: tương đương Photoshop ở mọi nhóm chức năng (trừ mục Không làm); retouch chân dung là một điểm mạnh, không phải toàn bộ phạm vi.** Chốt 16/10/2026.
2. **Repo riêng, không phụ thuộc snapone: cần mã của snapone (GPU HAL, jobs, màu, codec, ML…) thì chép sang, từ đó là mã của photo-beauty.** Chốt 17/10/2026, thay cho đề xuất cũ (snapone làm submodule `external/snapone`, link `snapone_core`). Đổi lại: lỗi sửa ở bên này không tự sang bên kia.
3. **Mode màu: MVP có RGB và Grayscale, 8 và 16 bit; CMYK và 32 bit ở P2; Lab, Indexed Color, Duotone, Bitmap, Multichannel ở P3.** CMYK đứng sau vì compositor, bộ lọc, adjustment đều phải chạy trên 4 kênh mực; làm khi đường RGB đã ổn và nhanh.
4. **Định dạng chính là file riêng (`.pbd`). PSD/PSB mở và lưu từ P1: layer, nhóm, mặt nạ, blend mode, clipping, adjustment layer, smart object nhúng, layer style có ở P1, luôn kèm ảnh ghép. Layer chữ của PSD mở được ở P1 (hiện bằng điểm ảnh đã lưu, giữ dữ liệu chữ khi lưu lại), sửa được ở P2 cùng mọi layer style và smart filter.**
5. **Remove tool, Content-Aware Fill dùng LaMa (Apache-2.0) chạy trên máy, như điểm 6 của [ai-plan snapone](../snapone/docs/ai-plan.md); không làm PatchMatch** (bằng sáng chế US 8,285,055 của Adobe, ghi ở [heal-plan snapone](../snapone/docs/heal-plan.md)). Luật sư xem chung đợt với snapone trước khi bán.
6. **Desktop trước (Windows, macOS, Linux; x64, ARM64), bút Wacom và Windows Ink từ P1; iPad sau P3** (Qt cho iOS, Metal, Apple Pencil).
7. **Tên lệnh, cấu trúc menu, phím tắt theo Photoshop; icon, đồ họa, bố cục chi tiết là của mình.** Photopea, Affinity cũng làm vậy; không dùng tên, logo, icon, tài liệu của Adobe (mục Rủi ro).
8. **Script và plugin bằng JavaScript, chạy trên QJSEngine có sẵn trong Qt; không dùng Lua như plan snapone đề xuất.** Người dùng Photoshop quen viết script JavaScript. API là các lệnh JSON của app (mục Lịch sử), không tương thích UXP của Adobe.
9. **Thuật ngữ của giao diện tiếng Việt: giữ tên tiếng Anh cho thuật ngữ người dùng Photoshop đã quen (Layer, Mask, Brush, Blend mode, Smart Object, Curves, Levels, Liquify…), dịch phần hành động và câu chữ (Mở, Lưu, Xuất, Hoàn tác, Tạo mới…); tooltip và ô tìm lệnh nhận cả tên tiếng Anh.** Cách khác: dịch hết như SnapOne (Phơi sáng, Mặt nạ…): dễ đọc với người mới, nhưng người dùng Photoshop phải đoán lại tên lệnh.

## Danh sách chức năng

Mức: P0 nền móng, P1 MVP, P2 chuyên nghiệp, P3 đủ như Photoshop. Ô trống là mức đó không có gì. Ghi "snapone" là chép module của snapone (bảng ở mục Chép từ snapone).

### Thanh công cụ

| Nhóm (phím) | P0–P1 | P2 | P3 |
| --- | --- | --- | --- |
| Move (V) | Move: tự chọn layer, kéo nhân bản (Alt), căn, phân bố | Artboard | |
| Marquee (M) | chữ nhật, elip; cộng, trừ, giao vùng chọn; tỉ lệ, cỡ cố định | Single Row, Single Column | |
| Lasso (L) | Lasso, Polygonal Lasso | | Magnetic Lasso |
| Chọn (W) | Magic Wand, Selection Brush, nút Select Subject | Quick Selection | Object Selection (AI) |
| Crop (C) | Crop (thẳng ảnh, tỉ lệ, giữ hay xóa phần cắt), Perspective Crop | Frame | lấp viền bằng Content-Aware, Generative Expand |
| Eyedropper (I) | Eyedropper (1 điểm, 3 × 3, 5 × 5; layer này hay mọi layer) | Color Sampler, Ruler, Note | Count |
| Healing (J) | Spot Healing Brush, Healing Brush, Patch | Red Eye | Remove, Content-Aware Move |
| Brush (B) | Brush, Pencil | Color Replacement, Adjustment Brush | Mixer Brush |
| Stamp (S) | Clone Stamp | Pattern Stamp | |
| History Brush (Y) | | History Brush | Art History Brush |
| Eraser (E) | Eraser | Background Eraser, Magic Eraser | |
| Gradient (G) | Gradient (tuyến tính, tròn, góc, phản chiếu, kim cương; sửa lại trên canvas), Paint Bucket | | |
| Blur, Sharpen, Smudge | | cả ba | |
| Dodge (O) | Dodge, Burn, Sponge | | |
| Pen (P) | | Pen, Freeform Pen, Curvature Pen; thêm, xóa, đổi điểm neo | |
| Type (T) | chữ ngang | chữ dọc; Type Mask ngang và dọc | |
| Path Selection (A) | | Path Selection, Direct Selection | |
| Shape (U) | Rectangle, Ellipse, Triangle, Polygon, Line (bo góc, viền, sửa bằng thuộc tính) | Custom Shape | |
| Điều hướng | Hand (H), Rotate View (R), Zoom (Z) | | |
| Khác | Quick Mask (Q), Screen Mode (F), màu trước/sau (D, X) | tùy biến thanh công cụ (Edit Toolbar) | |

Cọ ở P0 là cọ tròn có cỡ, độ cứng, opacity, flow, spacing, lực nhấn; P1 thêm làm mượt nét, preset, đầu cọ từ ảnh, cọ lớn tới 5.000 px; nhập cọ `.abr` ở P3.

### Menu File

| Mục | P0–P1 | P2 | P3 |
| --- | --- | --- | --- |
| Tạo, mở | New (preset, cỡ, ppi, mode, bit, hồ sơ màu); Open: JPEG, PNG, TIFF, WebP, HEIC, RAW, PSD/PSB, file riêng; Open Recent | Open as Smart Object; AVIF, JPEG XL, OpenEXR, GIF | PDF, AI (phần PDF trong file), SVG |
| Lưu | Save, Save As, Save a Copy, Revert; tự lưu khôi phục | lịch sử phiên bản (file riêng lưu tăng dần nên giữ sẵn bản cũ) | lưu PDF |
| Đặt ảnh | Place Embedded | Place Linked, Package | |
| Xuất | Export As, Quick Export: JPEG, PNG, WebP, TIFF; cỡ, chất lượng, hồ sơ màu, metadata; nhiều cỡ một lần | Layers to Files, Artboards to Files, Color Lookup Tables; File Info (IPTC, XMP) | path ra SVG, Copy CSS, Generate Image Assets |
| Tự động hóa | | Batch, Image Processor, Load Files into Stack, Photomerge, Merge to HDR Pro | Droplet, Contact Sheet, Fit Image, Crop and Straighten Photos, Conditional Mode Change, Variables, Data Sets |
| In | | Print: quản lý màu, soft proof, cỡ in | |

Mở JPEG, PNG, TIFF (codec của snapone, thêm libtiff, làm chung F1 của [formats-plan snapone](../snapone/docs/formats-plan.md)) và file riêng có từ P0. HEIC qua bộ giải mã của hệ điều hành như quyết định 4 của formats-plan snapone; RAW qua `Engine` của snapone.

### Menu Edit

| Mục | P0–P1 | P2 | P3 |
| --- | --- | --- | --- |
| Lịch sử | Undo, Redo, Step Backward, Step Forward, Fade | | |
| Clipboard | Cut, Copy, Copy Merged, Paste, Paste in Place, Clear | Paste Into, Paste Outside | |
| Tô | Fill (màu, màu trước/sau, lịch sử), Stroke | Fill bằng pattern, Define Pattern | Content-Aware Fill (cả không gian làm việc riêng), Generative Fill |
| Biến đổi | Free Transform; Scale, Rotate, Skew, Distort, Perspective, Warp; xoay 90°, 180°; lật | Auto-Align Layers, Auto-Blend Layers | Puppet Warp, Perspective Warp, Content-Aware Scale, Sky Replacement |
| Preset | Define Brush Preset | Define Custom Shape, Preset Manager, nhập và xuất preset | |
| Màu | Color Settings, Assign Profile, Convert to Profile (sRGB, Display P3, Adobe RGB, ProPhoto…) | | |
| Thiết lập | Preferences; phím tắt mặc định như Photoshop | sửa phím tắt; tìm lệnh (Ctrl F) | sửa menu |

### Menu Image

| Mục | P0–P1 | P2 | P3 |
| --- | --- | --- | --- |
| Mode | RGB, Grayscale; 8, 16 bit | CMYK; 32 bit | Lab, Indexed Color và Color Table, Duotone, Bitmap, Multichannel |
| Adjustments | 16 adjustment (cũng là adjustment layer): Brightness/Contrast, Levels, Curves, Exposure, Vibrance, Hue/Saturation, Color Balance, Black & White, Photo Filter, Channel Mixer, Color Lookup (`.cube`, `.3dl`), Invert, Posterize, Threshold, Gradient Map, Selective Color; lệnh Desaturate, Equalize, Auto Tone, Auto Contrast, Auto Color | Shadows/Highlights, HDR Toning, Match Color, Replace Color; preset adjustment | |
| Kích thước | Image Size (Nearest Neighbor, Bilinear, Bicubic, Bicubic Smoother, Bicubic Sharper; thêm Lanczos), Canvas Size, Image Rotation, Crop, Trim, Reveal All, Duplicate | | Preserve Details (AI) |
| Kênh | | Apply Image, Calculations | Trap |
| Phân tích | | | Measurement, Count, Scale |

### Menu Layer

| Mục | P0–P1 | P2 | P3 |
| --- | --- | --- | --- |
| Layer, nhóm | New, Layer via Copy (Ctrl J), Layer via Cut, Duplicate, Delete, Rename, Background from Layer; nhóm Pass Through và cô lập; ẩn hiện, Arrange, Lock (trong suốt, điểm ảnh, vị trí, tất cả), Link; Merge Down, Merge Visible, Flatten; lọc layer theo loại, tên | Isolate Layers | Layer Comps |
| Blend | 27 blend mode, opacity, fill, Blend If | Knockout và các tùy chọn Advanced Blending | |
| Mặt nạ | mặt nạ layer (Reveal All, Hide All, theo vùng chọn, Apply, tắt, liên kết), clipping mask | mặt nạ vector | Mask All Objects |
| Fill, adjustment layer | Solid Color, Gradient; 16 adjustment layer | Pattern | |
| Smart object | Convert to Smart Object, Edit Contents, Rasterize | smart filter; Linked; New Smart Object via Copy; Replace Contents, Export Contents; Relink; Stack Mode (Median, Mean…) | |
| Layer style | Drop Shadow, Stroke, Color Overlay, Gradient Overlay | Bevel & Emboss, Inner Shadow, Inner Glow, Outer Glow, Satin, Pattern Overlay; nhiều hiệu ứng cùng loại; Global Light; Copy, Paste Layer Style; Scale Effects; Create Layers; panel Styles | |
| Artboard | | Artboard, xuất theo artboard | |
| Matting | | Defringe, Remove Black Matte, Remove White Matte, Color Decontaminate | |
| Căn | Align, Distribute | Combine Shapes | |
| Video | | | Video Layers |

P0 có layer điểm ảnh, nhóm, opacity và Normal cộng 10 blend mode hay dùng (Multiply, Screen, Overlay, Soft Light, Darken, Lighten, Color Dodge, Color Burn, Linear Light, Difference).

### Menu Type

| Mục | P0–P1 | P2 | P3 |
| --- | --- | --- | --- |
| Chữ | chữ điểm, chữ đoạn; gõ tiếng Việt (IME); font thiếu: báo và thay | chữ dọc; Warp Text; chữ theo path; Convert to Shape, Create Work Path; Paste Lorem Ipsum | Match Font (AI) |
| Character | font, kiểu, cỡ, leading, kerning theo font, tracking, co giãn ngang và dọc, baseline shift, màu, gạch chân, gạch ngang, chữ hoa, khử răng cưa | kerning quang học; OpenType (ligature, alternate), panel Glyphs; Character Styles | |
| Paragraph | căn lề, căn đều, thụt lề, khoảng cách đoạn | ngắt từ; Paragraph Styles | |

### Menu Select

| Mục | P0–P1 | P2 | P3 |
| --- | --- | --- | --- |
| Lệnh | All, Deselect, Reselect, Inverse, All Layers; Modify (Border, Smooth, Expand, Contract, Feather); Grow, Similar; Transform Selection; Edit in Quick Mask; Load Selection, Save Selection | Find Layers | |
| Theo nội dung | Color Range (cả Skin Tones, Detect Faces, Highlights, Shadows), Subject (BiRefNet_lite của snapone); da, mắt, môi, răng (điểm mạnh retouch) | Focus Area | Sky; tóc, quần áo |
| Select and Mask | Smooth, Feather, Contrast, Shift Edge; Refine Edge Brush, Refine Hair; xuất ra vùng chọn, mặt nạ, layer mới (guided filter của snapone) | Decontaminate Colors | |

### Menu Filter

| Mục | P0–P1 | P2 | P3 |
| --- | --- | --- | --- |
| Chung | Last Filter; xem trước trên canvas | Convert for Smart Filters; Camera Raw Filter (cần F1, F2 của formats-plan snapone) | Filter Gallery (Artistic, Brush Strokes, Sketch, Texture) |
| Blur | Average, Blur, Blur More, Box, Gaussian, Motion, Surface | Radial, Shape, Smart; Blur Gallery (Field, Iris, Tilt-Shift, Path, Spin) | Lens Blur theo độ sâu |
| Sharpen | Sharpen, Sharpen More, Sharpen Edges, Unsharp Mask | Smart Sharpen | |
| Noise | Add Noise, Despeckle, Dust & Scratches, Median, Reduce Noise (wavelet của snapone) | | |
| Distort | Liquify (cả theo khuôn mặt) | Displace, Pinch, Polar Coordinates, Ripple, Shear, Spherize, Twirl, Wave, ZigZag; Lens Correction | Adaptive Wide Angle, Vanishing Point |
| Pixelate | | Color Halftone, Crystallize, Facet, Fragment, Mezzotint, Mosaic, Pointillize | |
| Stylize | | Diffuse, Emboss, Extrude, Find Edges, Oil Paint, Solarize, Tiles, Trace Contour, Wind | |
| Render | | Clouds, Difference Clouds, Fibers, Lens Flare | Lighting Effects, Flame, Picture Frame, Tree |
| Other | High Pass, Minimum, Maximum, Offset, Custom | HSB/HSL | |
| Video | | | De-Interlace, NTSC Colors |
| Neural Filters | Skin Smoothing (cách cổ điển, thuộc điểm mạnh retouch) | Color Transfer (cách cổ điển) | Smart Portrait, Makeup Transfer, Landscape Mixer, Style Transfer, Harmonization, Colorize, Super Zoom, Depth Blur, JPEG Artifacts Removal, Photo Restoration |

### Menu View

| Mục | P0–P1 | P2 | P3 |
| --- | --- | --- | --- |
| Zoom, màn hình | Zoom In, Zoom Out, Fit on Screen, 100%, Pixel Grid, xoay view, Screen Mode; quản lý màu theo hồ sơ màn hình (snapone) | Print Size; cửa sổ thứ hai; Arrange (Tile, Float, Match Zoom, Match Location) | Pixel Aspect Ratio |
| Căn chỉnh | Rulers; Guides (New, Lock, Clear); Grid; Snap, Snap To | Smart Guides, New Guide Layout, New Guides from Shape | |
| Màu | | Proof Setup, Proof Colors, Gamut Warning (proof của snapone) | |
| Extras | Layer Edges, Selection Edges, Brush Preview | Target Path, Notes, Mesh | |

### Panel và không gian làm việc

| Mục | P0–P1 | P2 | P3 |
| --- | --- | --- | --- |
| Panel | Layers, Properties, Adjustments, History (cả snapshot), Color, Swatches, Gradients, Brushes, Brush Settings, Channels, Navigator, Histogram, Info, Character, Paragraph | Paths, Patterns, Shapes, Styles, Actions, Clone Source, Glyphs, Character Styles, Paragraph Styles, Tool Presets, Notes, Version History | Timeline, Layer Comps, Measurement Log |
| Không gian làm việc | cột panel cố định, thu gọn được; nhiều tài liệu dạng tab; thanh trạng thái có RAM, scratch đang dùng | kéo thả panel, panel nổi, lưu workspace; Contextual Task Bar | |

### Tự động hóa, script, plugin

| Mục | P0–P1 | P2 | P3 |
| --- | --- | --- | --- |
| Actions | | ghi, chạy lại, bộ action, Insert Stop, chạy hàng loạt | nhập `.atn` của Photoshop cho các lệnh có tương đương |
| Script, plugin | | | script JavaScript (điểm 8), plugin có panel QML |

### Hoạt họa và video

Tất cả ở P3: Timeline theo khung hình; xuất GIF, APNG, WebP động; video layer, đọc và xuất video qua bộ giải mã, bộ mã hóa của hệ điều hành (như HEIC, để tránh phí bằng sáng chế codec).

### AI

Mọi model chạy qua `ml.cpp` của snapone (ONNX Runtime nạp lúc chạy), tải khi dùng lần đầu, kiểm SHA-256; giấy phép từng model theo bảng của ai-plan snapone.

| Chức năng | Mức | Cách làm |
| --- | --- | --- |
| Select Subject, Remove Background | P1 | BiRefNet_lite (snapone AI1) |
| Tìm mặt, landmark | P1 | YuNet, Face Landmarker (snapone AI2) |
| Object Selection, Select Sky, chọn tóc, quần áo | P3 | SAM 2.1; mặt nạ trời, người như AI3 của snapone |
| Remove tool, Find Distractions, Content-Aware Fill, Content-Aware Move | P3 | LaMa trên máy (điểm 5) |
| Generative Fill, Generative Expand, Generate Image, Generate Background, Generate Similar, Harmonize | P3 | API bằng khóa của người dùng, theo điểm 8–10 của ai-plan snapone |
| Super Zoom, Preserve Details, Depth Blur, Colorize, JPEG Artifacts Removal, Photo Restoration | P3 | Real-ESRGAN, Depth Anything V2 Small; model khác chọn khi làm, theo giấy phép |
| Content Credentials (C2PA) khi ảnh có phần AI tạo sinh | P3 | |

### Điểm mạnh: retouch chân dung

| Chức năng | Mức | Ghi chú |
| --- | --- | --- |
| Tách tần số một nhấp: nhóm Thấp, Cao, dựng lại khớp ảnh gốc ở 16 bit | P1 | Photoshop làm bằng Apply Image ở 8 bit thì mất một bit |
| Mặt nạ da, mắt, môi, răng tự động | P1 | landmark, màu da trong OKLab, guided filter |
| Làm mịn da giữ vân (lớp Thấp, trong mặt nạ da) | P1 | |
| Dodge & Burn dựng sẵn: cặp Curves sáng, tối có mặt nạ đen, hay layer xám 50% Soft Light | P1 | |
| Làm trắng mắt, răng; làm sáng mắt | P1 | |
| Tự tìm và xóa mụn, vết | P2 | Visualize Spots và Heal của snapone |
| Làm đều màu da | P2 | |
| Áp một công thức retouch cho cả loạt ảnh | P2 | |
| "Chỉnh trong photo-beauty" từ snapone rồi quay về, như Lightroom ↔ Photoshop | P2 | |
| Retouch da bằng model AI, trang điểm, chỉnh dáng người | P3 | xem pháp lý, đạo đức trước |

### Không làm

- **3D:** Adobe đã ngừng 3D trong Photoshop.
- **Dịch vụ đám mây của Adobe** (Creative Cloud Libraries, tài liệu đám mây, Share for Review, Adobe Stock, Adobe Fonts): thay bằng thư viện preset trên máy và lịch sử phiên bản trong file.
- **Slice, Save for Web (Legacy):** Adobe để ở dạng legacy; Export As, Layers to Files, Artboards to Files thay thế.
- **Plugin `.8bf` và UXP của Photoshop:** cần API và SDK của Adobe, phải xem điều khoản trước; plugin của app dùng JavaScript (điểm 8).

## Lộ trình

Bốn mức P0 → P3; chỉ sang mức sau khi qua cổng. Phạm vi mỗi mức là cột tương ứng của Danh sách chức năng; dưới đây chỉ ghi việc lớn.

### P0 · Nền móng (tháng 1–2)

Chứng minh đường bút → ô → GPU → màn hình đạt KPI trên tài liệu lớn, trước khi thêm tính năng. Task cho agent: [docs/p0-tasks.md](docs/p0-tasks.md).

- [ ] Repo: CMake + vcpkg + presets, CI 5 nền tảng với ASan, UBSan, TSan, Tracy, khung bench, Crashpad + Sentry: chép cấu hình snapone (điểm 2).
- [ ] Kho ô: chép khi ghi, ô trống và ô một màu, phiên bản ô, ngân sách RAM, nén, scratch file; chọn ô 128 hay 256 bằng bench.
- [ ] Mô hình tài liệu: cây layer (điểm ảnh, nhóm), thuộc tính JSON có schema version; undo, redo.
- [ ] Ghép v0: Normal và 10 blend mode, opacity, nhóm; bản CPU tham chiếu và kernel Slang; test so khớp.
- [ ] Viewport Qt Quick theo `rawframe.cpp` (texture chia sẻ, không copy về CPU): pan, zoom, xoay view, mip, quản lý màu.
- [ ] Cọ v0: cọ tròn, lực nhấn, spacing, stroke buffer, tẩy; đo độ trễ bằng Tracy.
- [ ] Mở JPEG, PNG, TIFF; file riêng v1.
- [ ] Bộ tài liệu đo D1–D4 (mục Chỉ tiêu hiệu năng); script sinh tài liệu tổng hợp.

**Cổng G1:** trên D1, độ trễ nét cọ 300 px trong app ≤ 8 ms p95, pan và zoom ≥ 60 fps; D2 mở và vẽ được trên laptop 16 GB mà RAM không vượt ngân sách; ảnh ghép CPU và GPU lệch tối đa 1 mức 8 bit. Đo trên Windows và Linux có GPU; macOS khi có máy Mac (chung việc còn lại của snapone).

### P1 · MVP (tháng 3–8)

Người dùng Photoshop làm được việc hằng ngày mà không cần Photoshop: retouch, ghép ảnh, poster, ảnh mạng xã hội, ảnh sản phẩm. Task cho agent: [docs/p1-tasks.md](docs/p1-tasks.md).

- [ ] Compositor đủ: 27 blend mode, Blend If, mặt nạ, clipping, nhóm, khóa; 16 adjustment layer; 4 layer style; smart object nhúng.
- [ ] Cọ đủ và các công cụ vẽ, tô, tẩy; gradient sửa lại được trên canvas.
- [ ] Vùng chọn: công cụ, lệnh Select, Color Range, Select Subject, Select and Mask, Quick Mask.
- [ ] Retouch: Spot Healing, Healing Brush, Clone Stamp, Patch, Dodge, Burn, Sponge; điểm mạnh retouch cột P1.
- [ ] Biến đổi: Free Transform, Warp, Liquify (cả theo khuôn mặt), Crop, Perspective Crop.
- [ ] Bộ lọc cột P0–P1: xem trước trên canvas ở vùng nhìn thấy, áp full size ở nền.
- [ ] Chữ (Character, Paragraph, gõ tiếng Việt), hình.
- [ ] File: PSD/PSB (điểm 4), WebP, HEIC, RAW; Export As, Quick Export; tự lưu khôi phục.
- [ ] Giao diện: panel cột P0–P1, menu và phím tắt như Photoshop (điểm 7); tiếng Việt, tiếng Anh theo cách dịch của snapone.
- [ ] Bút: Windows Ink và Wintab, macOS, Linux (X11, Wayland); lực nhấn, nghiêng.
- [ ] Bộ cài, ký số, báo crash, telemetry khi người dùng đồng ý, khóa bản quyền: `pack/`, `stats.cpp`, `license.cpp` của snapone.

**Cổng G2:** trên laptop tối thiểu, đạt mọi KPI với hai quy trình: retouch trọn D1 (tách tần số, 200 vết heal, liquify, 5 adjustment, chữ) và ghép một poster (Select Subject, smart object, 3 adjustment, chữ có Drop Shadow và Stroke, xuất 3 cỡ). Bộ PSD thử mở rồi lưu lại, ảnh ghép lệch Photoshop ΔE2000 trung bình dưới 1 với các tính năng đã có. 20 người dùng Photoshop (thợ retouch, nhiếp ảnh gia, thiết kế) dùng thử 2 tuần, làm được ít nhất 80% việc hằng ngày không cần Photoshop.

### P2 · Chuyên nghiệp (tháng 9–15)

Task cho agent: [docs/p2-tasks.md](docs/p2-tasks.md) (soạn sớm, soạn kỹ lại khi tới cổng G2).

- [ ] CMYK và 32 bit trên toàn bộ compositor, bộ lọc, adjustment; in, soft proof.
- [ ] Smart filter, smart object liên kết, Stack Mode; Camera Raw Filter, RAW thành smart object.
- [ ] Đủ 10 layer style; Pen, path, mặt nạ vector, Custom Shape; artboard.
- [ ] Actions, Batch, Image Processor; Auto-Align, Auto-Blend, Photomerge, Merge to HDR Pro.
- [ ] Các họ bộ lọc cột P2 (Blur Gallery, Distort, Pixelate, Stylize, Render), Lens Correction.
- [ ] Chữ nâng cao: chữ dọc, OpenType, Glyphs, Character và Paragraph Styles, Warp Text, chữ theo path.
- [ ] PSD: chữ sửa được, mọi layer style, smart filter.
- [ ] Workspace kéo thả, panel nổi, sửa phím tắt, tìm lệnh, Contextual Task Bar.
- [ ] Điểm mạnh retouch cột P2; chỉnh trong photo-beauty từ snapone.
- [ ] Backend Metal (chung snapone); tối ưu sâu: FP16, async compute, zero-copy trên Apple Silicon.

**Cổng G3:** D2 và D4 đạt KPI trên laptop 16 GB; tài liệu CMYK chậm hơn RGB không quá 1,5 lần; bộ PSD thật (có chữ, layer style, smart filter) lệch Photoshop ΔE2000 trung bình dưới 1; nhóm beta làm trọn việc thật (studio cưới, kỷ yếu, ảnh sản phẩm, thiết kế in) trong app.

### P3 · Đủ như Photoshop (tháng 16–24)

Task cho agent: [docs/p3-tasks.md](docs/p3-tasks.md) (soạn sớm, soạn kỹ lại khi tới cổng G3).

- [ ] AI: Object Selection, Remove, Content-Aware Fill, Generative Fill và Expand, siêu phân giải, Depth Blur, Neural Filters, Content Credentials.
- [ ] Mode Lab, Indexed Color, Duotone, Bitmap, Multichannel; Trap.
- [ ] Timeline, video layer; GIF, APNG, WebP động.
- [ ] Puppet Warp, Perspective Warp, Content-Aware Scale, Vanishing Point, Adaptive Wide Angle, Sky Replacement.
- [ ] Filter Gallery, Lighting Effects và các bộ lọc còn lại.
- [ ] Script JavaScript, plugin; nhập `.atn`; Droplet, Variables, Data Sets.
- [ ] Mở, lưu PDF; xuất SVG.

**Cổng G4:** mọi dòng của Danh sách chức năng xong hoặc nằm trong Không làm; 30 người dùng Photoshop làm xong bộ 50 tác vụ chuẩn (mục Kiểm thử và đo) mà không cần Photoshop.

Sau P3: iPad (điểm 6).

## Chỉ tiêu hiệu năng

Thao tác tương tác xong trong một frame (16 ms ở 60 Hz). Bốn tài liệu đo:

- **D1 · chân dung chuẩn:** 8256 × 5504 (45 MP), 16 bit, 15 layer: 3 layer đầy (nền, tách tần số Thấp, Cao), 5 layer vẽ phủ khoảng 20% (heal, clone, dodge & burn), 5 adjustment layer có mặt nạ, 1 chữ, 1 nhóm; khoảng 1,5 GB điểm ảnh.
- **D2 · lớn hơn RAM:** 20.000 × 15.000 (300 MP), 8 bit, 40 layer phủ trung bình 30%; khoảng 14 GB điểm ảnh.
- **D3 · PSD thật:** 1–2 GB từ Photoshop, có nhóm, mặt nạ, adjustment, chữ, smart object, layer style.
- **D4 · thiết kế nhiều layer:** 6000 × 4000, 8 bit, 1.000 layer (300 chữ, 300 hình, 100 smart object, 300 layer điểm ảnh nhỏ), layer style trên 200 layer, nhóm lồng 6 cấp.

Máy chuẩn như snapone: CPU 8 nhân, 32 GB RAM, SSD NVMe, GPU rời tầm trung hoặc Apple Silicon bản Pro. Máy tối thiểu: laptop 4 nhân, GPU tích hợp, 16 GB (máy dev i7-1255U, Iris Xe).

| Thao tác | Mục tiêu | Điều kiện |
| --- | --- | --- |
| Độ trễ nét cọ trong app (sự kiện bút → frame gửi đi) | ≤ 8 ms p95 | D1, cọ 300 px, layer giữa ngăn xếp |
| Cọ lớn | ≥ 60 fps tới 1.000 px; ≥ 30 fps tới 5.000 px | D1 |
| Pan, zoom, xoay view | ≥ 60 fps; chỗ mới lộ ra hiện ngay từ mip thấp, nét sau < 150 ms | D1, D2, D4 |
| Kéo thanh adjustment, layer style, opacity | ≤ 16 ms | D1, D4; vừa màn hình |
| Ẩn hiện layer, đổi blend mode | ≤ 33 ms | D1, D4; vừa màn hình |
| Cuộn panel Layers | ≥ 60 fps | D4 |
| Gõ chữ trên canvas | ≤ 16 ms mỗi phím | chữ đoạn 2.000 ký tự |
| Xem trước bộ lọc (Gaussian r = 100, Unsharp Mask) | ≤ 50 ms | D1, vùng nhìn thấy |
| Áp bộ lọc full size | ≤ 1 s có GPU; ≤ 5 s chỉ CPU | một layer của D1 |
| Kéo Free Transform (cả smart object), cọ Liquify | ≥ 60 fps; áp full size ≤ 1 s | D1, D4 |
| Undo, redo | ≤ 100 ms | mọi thao tác trên D1 |
| Magic Wand, Color Range | ≤ 100 ms | D1 |
| Select Subject | ≤ 8 s từ lần thứ hai | máy tối thiểu; snapone đo 5,2 s trên Iris Xe |
| Mở JPEG 45 MP | frame đầu ≤ 300 ms | |
| Mở file riêng | frame đầu ≤ 1 s, bất kể cỡ | D2 |
| Mở PSD | ≤ 5 s | D3; D4 lưu thành PSD |
| Lưu | không chặn giao diện; tăng dần ≤ 1 s, đầy đủ ≤ 5 s | D1 |
| Tài liệu CMYK | mọi KPI trên chậm hơn RGB không quá 1,5 lần | D1 đổi sang CMYK 8 bit |
| Khởi động | ≤ 1,5 s | |
| RAM | không vượt ngân sách (mặc định 60% RAM máy); D1 nằm trọn trong RAM máy 16 GB | |
| Khi rảnh | CPU, GPU ≈ 0%; RAM ≤ 300 MB khi chưa mở tài liệu | |
| Bộ cài | ≤ 150 MB, không tính model AI | |

## Kiến trúc

### Các tầng

Mỗi tầng chỉ gọi tầng ngay dưới; lõi không biết gì về Qt, như snapone.

| Tầng | Thành phần | Nguồn |
| --- | --- | --- |
| Giao diện | Qt Quick: canvas (QQuickItem nhận texture chia sẻ), công cụ, menu, panel; nhận sự kiện bút; rasterize chữ, hình bằng QtGui; chạy script JavaScript | mới; control QML, `rawframe.cpp` của snapone làm mẫu |
| API lõi | `Document` (cây layer, lệnh, lịch sử), render theo view, nét cọ, vùng chọn, mở, lưu, xuất, lọc | mới |
| Công cụ, ghép | compositor CPU và GPU, layer style, cọ, vùng chọn, biến dạng, bộ lọc, mode màu, retouch | mới; heal, guided filter, wavelet của snapone |
| Kho ô | ô chép khi ghi, mip, ngân sách, nén, scratch | mới |
| GPU HAL, scheduler | `gpu.h` (Vulkan qua volk; Metal sau), `jobs.h`, `lru.h` | snapone |
| Thư viện nền | LibRaw, lcms2, libjpeg-turbo, libspng, libtiff, libwebp, zstd, nlohmann-json, pugixml, volk; ONNX Runtime nạp lúc chạy | vcpkg |

### Mô hình tài liệu

- Tài liệu là cây layer cộng kho ô. Thuộc tính layer (tên, loại, blend, opacity, fill, khóa, Blend If, tham số adjustment, layer style, chữ, biến đổi của smart object) là JSON có schema version, như `EditParams` của snapone.
- Layer điểm ảnh: lưới ô có số kênh theo mode (Grayscale 1, RGB 3, CMYK 4, Lab 3, Multichannel n) cộng alpha, chưa nhân alpha (như PSD); kiểu u8, u16, f32 theo tài liệu. Layer được tràn ra ngoài canvas như Photoshop.
- Mặt nạ layer, vùng chọn, kênh alpha, kênh spot: lưới ô một kênh trong cùng kho. Vì vậy mọi cọ, bộ lọc, adjustment chạy được trên mặt nạ và vùng chọn mà không viết thêm; Quick Mask, Selection Brush chỉ là cách hiển thị và một chế độ của cọ.
- Layer chữ, hình: JSON của chữ hay hình cộng ô đã rasterize. Giao diện rasterize lại khi chữ đổi hay ảnh đổi cỡ; lõi không cần font.
- Adjustment layer, fill layer: chỉ tham số và mặt nạ, không có điểm ảnh; tính khi ghép.
- Smart object (P1): layer trỏ tới một tài liệu con (hay ảnh, hay RAW cộng thiết lập snapone) cộng biến đổi. Render từ nguồn ở cỡ cần, lấy mẫu một lần, cache theo (phiên bản nguồn, biến đổi, mức zoom). Edit Contents mở tài liệu con thành tab; các bản nhân bản dùng chung tài liệu con như Photoshop. Smart filter (P2) là danh sách bộ lọc chạy sau khi render nguồn, có mặt nạ riêng.
- Layer style: tham số JSON; ô hiệu ứng tính từ alpha của layer, cache theo phiên bản ô.
- Artboard (P2): nhóm cấp cao có khung và nền; ghép, xuất theo khung.
- Timeline (P3): mỗi khung hình là bộ thuộc tính layer (ẩn hiện, vị trí, opacity, style) đè lên tài liệu.

### Kho ô

- Ô 256 × 256 (P0 đo cả 128 × 128: bộ nhớ undo của nét mảnh so với chi phí quản lý). Ô bất biến, đếm tham chiếu; ghi vào ô đang dùng chung thì chép trước.
- Ô trống không cấp phát; ô một màu (nền trắng, mặt nạ toàn trắng) chỉ giữ một giá trị.
- Các tầng theo LRU, một ngân sách chung cho mọi tài liệu đang mở: ô nóng trong RAM → ô nguội nén zstd trong RAM → scratch file trên SSD. Với file riêng, ô chưa đổi từ lúc mở thì bỏ khỏi RAM luôn, cần lại thì đọc từ file, không ghi ra scratch.
- Tầng nén chỉ giữ nếu P0 đo thấy lợi: ảnh chụp 16 bit nén kém, còn mặt nạ, layer vẽ, layer xám 50% nén rất tốt.
- Mip theo ô (trung bình 2 × 2), dựng lười khi view cần, dựng lại ô cha ở job nền khi ô con đổi. Mip nhỏ (ở 1/8 chỉ còn 1/64 dữ liệu) nên ưu tiên giữ trong RAM: nhìn toàn ảnh luôn nhanh, kể cả D2.
- Mỗi ô có số phiên bản; cache mip, cache ghép, cache VRAM khóa theo phiên bản nên không bao giờ dùng nhầm dữ liệu cũ.
- Ngân sách tự co khi hệ điều hành báo thiếu bộ nhớ (Windows: memory resource notification; macOS: memory pressure; Linux: PSI).

### Một nét cọ

```text
sự kiện bút (vị trí, lực nhấn, nghiêng)     luồng giao diện: chỉ đẩy điểm vào hàng đợi của nét, không bỏ điểm nào
→ làm mượt, nội suy, đặt chấm theo spacing    job Interactive, đúng thứ tự (không dùng Latest)
→ chấm vào stroke buffer (ô một kênh, CPU, song song theo ô) → đánh dấu ô bẩn
    cọ lớn: chấm thẳng vào mip đang xem để hiện ngay, bản full size đuổi theo ở nền
→ ghép ô bẩn ở mức zoom đang xem              Latest: frame mới nhất thắng
    cache "mọi thứ dưới layer đang vẽ" ⊕ layer ⊕ stroke buffer (opacity, flow, blend) ⊕ các layer trên
    GPU: một kernel cho mỗi ô ra; chỉ upload ô đổi
→ sang hồ sơ màn hình trong cùng kernel → RGBA8 → texture chia sẻ → Qt Quick (không copy về CPU)
nhả bút: stroke buffer gộp vào layer (ô mới) → một bước lịch sử giữ con trỏ ô cũ
nền: dựng lại mip, nén ô nguội, tự lưu từ snapshot
```

### Ghép

- Đầu ra là ô của ảnh ghép ở mức zoom đang xem, chỉ ô nhìn thấy. Pan dùng lại ô đã ghép, chỉ ghép ô mới lộ ra; ô chưa kịp ghép hiện tạm từ mức mip thấp hơn.
- Một kernel cho mỗi ô ra: duyệt các layer có ô chạm ô này (layer trống ở đây bỏ qua, nên 1.000 layer nhỏ không làm chậm vùng không có chúng), đọc từ atlas ô trong VRAM, áp mặt nạ, opacity, Blend If, blend, adjustment, cộng dồn FP32 trong thanh ghi, ghi một lần. Nhóm cô lập ghép trước thành ô tạm; nhóm Pass Through trải phẳng.
- Khi vẽ trên layer k: giữ cache "mọi thứ dưới k" ở mức đang xem; mỗi frame chỉ ghép cache đó với layer k, stroke buffer và các layer trên k, trên ô bẩn. Kéo thanh của adjustment layer, layer style dùng cùng cache.
- Adjustment tính thẳng trong kernel. Gộp chuỗi adjustment không mặt nạ thành LUT (1D 4.096 mức mỗi kênh, 3D 65³ nội suy tứ diện) chỉ khi đo thấy chậm, như snapone hoãn 3D LUT tới khi cần.
- Blend mode theo công thức của W3C Compositing and Blending Level 1 cho 16 mode chuẩn, công thức công khai cho 11 mode riêng của Photoshop (Linear Light, Vivid Light, Pin Light, Hard Mix, Linear Burn, Linear Dodge, Darker Color, Lighter Color, Subtract, Divide, Dissolve). Mọi mode kiểm bằng ảnh ghép từ Photoshop thật, vì vài mode (Soft Light) của Photoshop khác đặc tả W3C.
- Layer style: hiệu ứng tính từ alpha của layer thành ô cache (Drop Shadow, Outer Glow: nới alpha bằng khoảng cách Euclid rồi làm mờ; Stroke: khoảng cách Euclid; Bevel & Emboss: khoảng cách cộng chiếu sáng), ghép như layer ẩn ngay trên hay dưới layer. Kéo thanh của một hiệu ứng chỉ tính lại hiệu ứng đó, ở mức đang xem.
- CMYK, Lab: compositor viết theo số kênh; blend mode trên CMYK, Lab theo cách của Photoshop, kiểm bằng ảnh mẫu; màn hình xem qua LUT dựng bằng lcms2 từ hồ sơ tài liệu sang hồ sơ màn hình.
- Bản CPU cùng công thức dùng khi xuất, làm phẳng, ghi ảnh ghép vào PSD, khi không có GPU, và để test.

### Lọc và biến dạng

- Hơn 100 bộ lọc của Photoshop dùng chung một khung: xem trước, áp theo ô, smart filter, Fade, Last Filter. Thêm một bộ lọc chỉ là viết hàm cho một ô (kernel Slang và bản CPU).
- Xem trước trên GPU ở mức đang xem, chỉ vùng nhìn thấy; bán kính nhân theo tỉ lệ zoom để bản xem khớp bản áp, như snapone.
- Áp full size ở job nền, theo ô có lề bằng bán kính lớn nhất như dải của snapone; hủy được, có tiến độ; xong là một bước lịch sử.
- Transform giữ ảnh gốc tới khi áp; các lần biến đổi trước đó gộp thành một ma trận, lấy mẫu một lần. Smart object không bao giờ mất chất lượng vì luôn render lại từ nguồn.
- Liquify: trường dịch chuyển FP16 ở nửa độ phân giải; cọ sửa trường, GPU warp ảnh ở mức đang xem, áp full size một lần. Theo khuôn mặt: trường dựng từ landmark theo các thanh (cỡ mắt, độ rộng mũi, khuôn mặt, cằm, nụ cười).

### Chữ và vector

- Rasterize ở tầng giao diện bằng QtGui; lõi chỉ nhận ô và JSON.
- QTextLayout lo shaping (HarfBuzz trong Qt), chữ hai chiều, xuống dòng, OpenType. Glyph lấy thành đường viền (QRawFont) rồi tô như hình, nên chữ ra cùng điểm ảnh trên mọi hệ điều hành; Convert to Shape, Create Work Path, Warp Text, chữ theo path chỉ là thao tác trên đường viền.
- Hình, path: QPainterPath, tô và viền bằng QPainter trên QImage 16 bit; các kiểu khử răng cưa của Photoshop kiểm bằng ảnh mẫu.
- Khi kéo, sửa: rasterize ở mức zoom đang xem; thả tay mới làm full size, ở nền.
- PDF, SVG (P3): QtPdf đọc PDF, QPdfWriter ghi PDF, QtSvg ghi SVG; đều là module của Qt.

### Mode màu

- Chuyển mode và hồ sơ bằng lcms2 (rendering intent, black point compensation), như snapone.
- CMYK (P2): 4 kênh mực cộng alpha trên toàn bộ đường ghép, bộ lọc, adjustment; KPI CMYK chậm hơn RGB không quá 1,5 lần.
- Indexed, GIF (P3): lượng tử hóa màu tự viết (median cut, tinh bằng k-means; khuếch tán lỗi Floyd–Steinberg hay pattern); giflib đọc, ghi GIF. Bitmap: ngưỡng, pattern, khuếch tán lỗi, halftone. Duotone: Grayscale cộng đường cong của từng mực.

### Lịch sử

- Lệnh thuộc tính (blend, opacity, tham số adjustment, layer style) là cặp JSON trước, sau, như `History` của snapone. Kéo thanh liên tục gộp thành một bước khi thả tay.
- Lệnh điểm ảnh giữ con trỏ ô cũ của các ô bị đổi: bộ nhớ của một bước bằng đúng phần ô bị sửa.
- Ngân sách lịch sử mặc định 25% ngân sách ô. Bước cũ ra scratch trước; vượt nữa thì bỏ bước cũ nhất.
- Lệnh lưu được dạng JSON: Actions (P2) là ghi và chạy lại các lệnh này; script JavaScript (P3) gọi cùng các lệnh; nhập `.atn` là dịch bộ mô tả (descriptor) của Photoshop sang lệnh của app, dùng chung bộ đọc descriptor với PSD.

### Định dạng file riêng

- Một file: header, các chunk ô nén zstd, chunk JSON của cây layer, ảnh ghép 2048 px cho thumbnail và frame đầu, index ở cuối có checksum.
- Lưu: chụp snapshot (chép con trỏ ô, vài ms) rồi ghi ở job nền; người dùng vẽ tiếp.
- Lưu tăng dần: chỉ nối thêm ô đã đổi và index mới, không ghi đè dữ liệu cũ, nên mất điện giữa chừng vẫn mở được bản trước. Phần thừa quá 50% file thì nén gọn ở nền.
- Lịch sử phiên bản (P2): index cũ vẫn nằm trong file, mở lại bản trước chỉ là đọc index cũ; nén gọn giữ lại các bản người dùng đánh dấu.
- Mở: đọc index và ảnh ghép thu nhỏ, hiện ngay; ô đọc khi cần (mmap). Mở tài liệu 14 GB tốn như mở tài liệu 100 MB.
- Tự lưu khôi phục đi cùng đường, ra file cạnh scratch.

### PSD

- Viết theo đặc tả PSD công khai của Adobe; ag-psd, psd-tools (MIT) để so hành vi. Mã GPL (GIMP, Krita) chỉ đọc ý tưởng, như snapone làm với darktable, RawTherapee.
- P1: layer điểm ảnh, nhóm, mặt nạ, blend, opacity, fill, Blend If, clipping, khóa, adjustment layer, smart object nhúng, 4 layer style của P1; nén Raw, RLE, ZIP; 8, 16 bit; PSB cho cạnh trên 30.000 px. Layer chữ hiện bằng điểm ảnh đã lưu, dữ liệu chữ giữ nguyên khi lưu lại.
- P2: chữ sửa được, mọi layer style, smart filter, smart object liên kết, CMYK, 32 bit, artboard, mặt nạ vector.
- Khối chưa hiểu giữ nguyên để lưu lại không mất; app báo phần nào chưa hiện đúng như Photoshop.
- Lưu: luôn kèm ảnh ghép (Maximize Compatibility) để Lightroom và app khác đọc được.
- Mọi bộ đọc file (PSD, TIFF, WebP, GIF, file riêng) chạy fuzz, như bộ giải mã RAW của snapone.

### Luồng

Dùng `jobs.h` của snapone:

- Interactive: nét cọ (hàng đợi đúng thứ tự), frame (`Latest`), xem trước bộ lọc.
- Visible: ô sắp lộ ra khi pan, mip của vùng đang xem.
- Background: áp full size, nén, scratch, lưu, tự lưu, AI, Actions chạy hàng loạt; chạy dưới luồng giao diện như `lower_priority()` của snapone.

### Giao diện

- QML như snapone, dùng lại Theme, control, CurveBox, Histogram, cách dịch; canvas viết bằng C++ theo `rawframe.cpp`.
- Menu, tên lệnh, phím tắt theo Photoshop (điểm 7). P1 dùng một cột panel cố định, thu gọn được; P2 thêm kéo thả, panel nổi, lưu workspace (QML không có sẵn dock: tự viết khung dock nhỏ).
- Bút: QTabletEvent (lực nhấn, nghiêng, xoay); nhận mọi điểm của bút và chuột, không để Qt gộp sự kiện.
- Viền kiến chạy: tính cạnh từ vùng chọn trong shader của lớp phủ; chỉ lớp phủ vẽ lại 10 lần mỗi giây, ảnh ghép không đổi.
- Panel Layers là danh sách ảo hóa, thumbnail lấy từ mip: 1.000 layer cuộn vẫn mượt.

## Công nghệ

| Hạng mục | Chọn | Từ snapone | Ghi chú |
| --- | --- | --- | --- |
| Ngôn ngữ lõi | C++20 | có | |
| Build | CMake ≥ 3.25, Ninja, vcpkg manifest, CMake presets (default, debug, asan, tsan, profile, ui, mingw) | có | |
| Giao diện | Qt 6.8+ Qt Quick | có | |
| GPU | Vulkan qua volk, HAL `gpu.h`; Metal sau | có | thêm atlas ô, upload từng vùng |
| Shader | Slang → SPIR-V nhúng vào binary | có | |
| Đa luồng | `jobs.h`: một pool, 3 mức ưu tiên, `Latest`, hủy job | có | |
| Màu | lcms2; hồ sơ màn hình, soft proof | có | |
| Codec | libjpeg-turbo, libspng, zlib (có); libtiff, libwebp (thêm ở P0–P1); libavif, libjxl, OpenEXR, giflib (P2); HEIC, video qua hệ điều hành | một phần | zlib cho nén ZIP của PSD |
| Nén | zstd | thêm | file riêng, ô nguội, scratch |
| Chữ, vector | QtGui: QTextLayout, QRawFont, QPainterPath, QPainter trên QImage 16 bit | Qt có sẵn | không thêm HarfBuzz, FreeType, Skia |
| PDF, SVG | QtPdf, QPdfWriter, QtSvg (P3) | Qt có sẵn | |
| Script | JavaScript qua QJSEngine (P3) | Qt có sẵn | điểm 8 |
| RAW | LibRaw, `Engine` | có | |
| AI | ONNX Runtime 1.31 nạp lúc chạy | có | |
| JSON, XML | nlohmann-json, pugixml | có | |
| Dữ liệu app | file JSON (preset, cọ, style, workspace, phím tắt) | | không cần catalog |
| Profiling | Tracy | có | |
| Crash, telemetry | Crashpad + Sentry | có | |
| Đóng gói | MSI, AppImage, DMG; ký số; khóa bản quyền tweetnacl | có | |
| Test | `check.h` tự viết, QML test offscreen, lavapipe trong CI | có | |

## Chép từ snapone

Chép khi cần (điểm 2), không link.

| Module | Dùng cho |
| --- | --- |
| `gpu.h`, `gpu_vulkan.cpp` | GPU HAL; thêm atlas ô, upload từng vùng |
| `jobs.h`, `lru.h`, `image.h` | scheduler, cache theo byte, ảnh planar |
| `color.*`, `proof.*` | hồ sơ màn hình, chuyển hồ sơ, soft proof |
| codec trong `preview.*`, `export.*` | mở, xuất JPEG, PNG, TIFF có EXIF, ICC |
| `raw.*`, `pipeline.*` | mở RAW, smart object RAW, Camera Raw Filter |
| `heal.cpp` | Spot Healing, Healing Brush, Patch: ý tưởng màng Laplace miền log, nguồn tự tìm; viết lại cho ô RGB vì bản snapone chạy trên dữ liệu cảm biến |
| `guided.cpp`, `detail.cpp` | Select and Mask, làm mịn da, Surface Blur, Shadows/Highlights; Reduce Noise, Unsharp Mask |
| dải màu trong `mask.cpp` | Color Range, mặt nạ da |
| `sift.cpp`, `pano.cpp`, `merge.cpp`, `upright.cpp` | Auto-Align, Auto-Blend, Photomerge, Merge to HDR Pro, Lens Correction, thẳng ảnh khi Crop |
| `ml.*`, `ai.*`, `analysis.*` | ONNX Runtime; Select Subject; tìm mặt, landmark |
| `xmp.*`, `license.*` | File Info, metadata; khóa bản quyền offline |
| `ui/qml/*`, `ui/rawframe.cpp`, `ui/stats.*`, `ui/i18n/` | control, viewport chia sẻ texture, telemetry, dịch |
| `pack/`, `.github/workflows/`, `CMakePresets.json`, `bench/`, `tests/check.h` | bộ cài, CI, preset, khung bench, test |

## Thư viện tự viết và thuật toán

| Phần | Vì sao không dùng sẵn | Thuật toán và tối ưu | Mức |
| --- | --- | --- | --- |
| Kho ô | GEGL (của GIMP) theo LGPL, viết bằng C/GObject, không đi cùng GPU HAL; chưa thấy thư viện thoáng nào có sẵn chép khi ghi, ngân sách, đẩy ra đĩa | ô bất biến đếm tham chiếu; ô trống, ô một màu; LRU theo byte; zstd; scratch ghi nối tiếp; mip lười | P0 |
| Compositor | phải khớp Photoshop từng mode, cả CPU và GPU, mọi mode màu | một kernel mỗi ô ra, cộng dồn FP32 trong thanh ghi; danh sách layer theo ô; cache "dưới layer đang vẽ"; nhóm cô lập ghép trước | P0–P2 |
| Layer style | phải khớp Photoshop | khoảng cách Euclid chính xác, Gaussian, chiếu sáng cho Bevel & Emboss; cache theo phiên bản alpha | P1–P2 |
| Cọ | lõi của app, quyết định cảm giác vẽ | nội suy Catmull-Rom; làm mượt kiểu dây kéo; chấm cache theo (cỡ, độ cứng, lệch dưới điểm ảnh); stroke buffer cho opacity và flow; cọ lớn vẽ trên mip trước | P0–P1 |
| Vùng chọn | cần chạy trên ô, cả CPU và GPU | flood fill theo dòng (Magic Wand); biến đổi khoảng cách Euclid chính xác O(n) cho Expand, Contract, Border bo tròn như Photoshop; Feather bằng Gaussian tách trục; tinh cạnh bằng guided filter | P1 |
| Bộ lọc | hơn 100 bộ lọc của Photoshop | chung một khung; mỗi bộ lọc một kernel Slang và bản CPU; Blur Gallery, Lens Blur mô phỏng bokeh theo hình khẩu độ | P1–P3 |
| Biến dạng, Liquify | cần xem trước trên GPU và áp theo ô | ánh xạ ngược, lấy mẫu một lần; trường dịch chuyển FP16 | P1 |
| Heal, Clone, Patch | bản snapone chạy trên dữ liệu cảm biến | màng Laplace miền log giải bằng multigrid; nguồn tự tìm như snapone | P1 |
| File riêng, PSD, ATN | không thư viện nào lưu nền, lưu tăng dần trên ô chép khi ghi; PSD không có thư viện C++ thoáng đủ layer, mặt nạ, style | chunk zstd, ghi nối tiếp, mmap; PSD theo đặc tả; một bộ đọc descriptor cho PSD và `.atn` | P0–P3 |
| Lượng tử hóa màu | cho Indexed Color, GIF | median cut, k-means, Floyd–Steinberg | P3 |
| Retouch chân dung | không có thư viện | tách tần số: Thấp là ảnh làm mờ, Cao = (ảnh − Thấp)/2 + 0,5 với blend Linear Light; mặt nạ da từ landmark (khuôn mặt trừ mắt, mày, môi) giao màu da trong OKLab, tinh bằng guided filter | P1 |

## Tận dụng phần cứng

Như mục cùng tên của plan snapone (băng thông bộ nhớ là nút thắt); phần riêng:

- **GPU:** atlas ô trong VRAM theo LRU, ngân sách theo VK_EXT_memory_budget; chỉ upload ô đổi qua staging buffer vòng; kernel ghép gộp; FP16 cho ô tạm khi đo thấy lợi.
- **CPU:** chấm cọ, flood fill, nén chạy song song theo ô bằng `parallel_for`; tự vector hóa của compiler trước, Highway khi Tracy chỉ ra vòng lặp nóng.
- **Bộ nhớ:** ngân sách ô mặc định 60% RAM, chỉnh trong Thiết lập như Memory Usage của Photoshop; lịch sử 25% của nó; buffer GPU rảnh tự trả sau 10 s như HAL của snapone; thu nhỏ cửa sổ thì nén ô nguội.
- **Đĩa:** scratch trên SSD nhanh nhất (chọn trong Thiết lập), ghi nối tiếp, xóa khi thoát; file riêng đọc bằng mmap.
- **Nhiều layer:** mỗi ô ra chỉ duyệt layer chạm nó; layer ẩn, layer ngoài vùng nhìn không tốn gì; thumbnail panel Layers lấy từ mip.
- **Pin:** job nền chạy mức thấp (EcoQoS, QoS utility); không vẽ lại khi không đổi; viền kiến chỉ vẽ lại lớp phủ.

## Đa nền tảng

Như snapone (Windows và Linux Vulkan, macOS Metal; MSI, AppImage, DMG). Phần riêng:

- Bút: Windows Ink và Wintab (Wacom đời cũ), macOS qua Qt, Linux X11 XInput2 và Wayland tablet protocol; thử bút thật của Wacom, Huion, XP-Pen, Surface Pen.
- Font: đọc font của hệ thống qua Qt; font thiếu khi mở PSD thì báo và thay như Photoshop.
- Tín hiệu thiếu bộ nhớ của từng hệ điều hành cho ngân sách tự co.
- Mở file bằng kéo thả, Open With, và từ snapone.

## Kiểm thử và đo

Ba lớp của snapone (ảnh chuẩn, CPU so GPU, benchmark chặn merge khi chậm hơn 5%), cộng:

- **Bảng tương đương:** mỗi dòng của Danh sách chức năng có kết quả xác định (blend, adjustment, bộ lọc, layer style, chuyển mode) có test so với ảnh mẫu xuất từ Photoshop thật; CPU và GPU lệch tối đa 1 mức 8 bit.
- **Bộ 50 tác vụ chuẩn:** lấy từ các bài học Photoshop phổ biến (tách nền, ghép, poster, retouch, in CMYK, action); mỗi bản beta chạy lại, đo thời gian và số bước so với Photoshop.
- **Chạy lại nét cọ:** ghi sự kiện bút thật (thời điểm, vị trí, lực nhấn), chạy lại ra đúng điểm ảnh (so mã băm trên CPU); bench độ trễ bằng chính bản ghi này.
- **Bộ PSD:** file từ Photoshop, Affinity, Photopea, Krita, GIMP; mở, lưu, mở lại, so ảnh ghép.
- **Bộ nhớ:** D2 với ngân sách 4 GB dưới ASan, đo RSS suốt bench; không bao giờ vượt.
- **An toàn file:** giết tiến trình giữa lúc lưu, nhiều lần; file vẫn mở được bản trước.
- **Fuzz:** PSD, TIFF, WebP, GIF, `.atn`, file riêng bằng libFuzzer.

## Rủi ro

| Rủi ro | Mức độ | Cách giảm |
| --- | --- | --- |
| Phạm vi tương đương Photoshop rất lớn | Cao | làm theo cột P0–P1 → P3 và cổng; bảng tương đương là nguồn sự thật; bộ lọc, công cụ dùng chung khung nên thêm mới rẻ; đủ người theo ước tính |
| Kết quả khác Photoshop (blend, layer style, bộ lọc) | Trung bình | ảnh mẫu từ Photoshop cho từng dòng; chấp nhận khác nhỏ ở bộ lọc nghệ thuật |
| PSD không khớp Photoshop | Cao | giữ khối chưa hiểu; luôn ghi ảnh ghép; bộ PSD thật trong CI; báo rõ phần chưa hỗ trợ |
| Giao diện, tên lệnh giống Photoshop | Thấp | chỉ dùng tên lệnh, phím tắt (Photopea, Affinity cũng vậy); không dùng tên, logo, icon, ảnh giao diện, tài liệu của Adobe; chữ "Photoshop" chỉ để nói tương thích PSD; luật sư xem cùng các mục khác |
| Bằng sáng chế (Content-Aware Fill và công cụ khác của Adobe) | Trung bình | điểm 5; luật sư xem danh sách công cụ trước khi bán, chung đợt với snapone |
| AI tạo sinh: minh bạch nội dung | Trung bình | Content Credentials khi xuất; luật sư xem các quy định như điều 50 của EU AI Act |
| Giấy phép model AI; ảnh khuôn mặt | Trung bình | theo ai-plan snapone; landmark chỉ dùng để chỉnh, tính trên máy, không lưu vector nhận diện |
| Độ trễ bút khác nhau theo driver | Trung bình | đo bằng bản ghi nét cọ trên từng hệ điều hành; thử bút thật của 4 hãng |
| Tài liệu khổng lồ làm hết bộ nhớ | Trung bình | ngân sách và scratch từ P0; D2 trong CI |
| Driver GPU; CPU lệch GPU | Trung bình | như snapone: đường CPU dự phòng, danh sách đen driver, test so khớp |

## 30 ngày đầu

Tháng đầu chỉ chứng minh đường bút → ô → GPU → màn hình nhanh trên tài liệu lớn, chưa làm tính năng.

- [ ] Tuần 1: repo; chép CMake, presets, vcpkg, CI, Tracy từ snapone; kho ô chép khi ghi, kèm test.
- [ ] Tuần 1: script sinh D1, D2, D4; gom 20 PSD thật cho D3.
- [ ] Tuần 2: ghép Normal và 10 blend mode trên CPU, kernel Slang, test so khớp; viewport Qt Quick hiện D1 với pan, zoom, mip.
- [ ] Tuần 3: cọ v0, stroke buffer, undo bằng con trỏ ô; đo độ trễ trên Iris Xe bằng Tracy; ngân sách RAM, nén, scratch.
- [ ] Tuần 4: file riêng v1 (lưu nền, mở lười); mở JPEG, PNG, TIFF; chạy D2 trên laptop 16 GB, D4 với 1.000 layer; tổng hợp số đo, chọn cỡ ô, chốt stack trước phần còn lại của P0.
- [ ] Song song: bạn thiết kế prototype theo mục Danh sách chức năng cho thiết kế giao diện; QML dựng theo prototype như [ui-plan của snapone](../snapone/docs/ui-plan.md), bắt đầu từ khung cửa sổ, canvas, Layers, Brush.

## Danh sách chức năng cho thiết kế giao diện

Để thiết kế prototype như SnapOne: thiết kế trước, rồi dựng QML theo prototype như [ui-plan của snapone](../snapone/docs/ui-plan.md). App có màn hình bắt đầu và một cửa sổ chỉnh kiểu Photoshop, cộng hộp thoại, không gian làm việc toàn màn hình và Thiết lập. Mỗi bảng liệt kê chức năng, điều khiển cần thiết kế và mức ưu tiên; prototype đầu chỉ cần dòng P0, P1, nhưng bố cục phải chừa chỗ cho P2, P3. Khoảng giá trị theo Photoshop để người dùng chuyển sang dễ. Nội dung từng menu ở các bảng Menu của mục Danh sách chức năng.

### Hệ thống thiết kế

Dùng lại hệ thống của SnapOne để hai app cùng một họ và dùng chung control QML.

- **Màu:** token của `ui/qml/Theme.qml` trong snapone (bản QML của `THEMES` trong `snapone.js` của prototype), theme tối và sáng. Theme tối: nền `#161617`; panel `#1c1c1e`, `#252527`, `#323235`; đường kẻ `#2b2b2e`; chữ 4 mức `#ececed`, `#b4b4b7`, `#86868b`, `#5d5d62`; lỗi `#ef5350`, cảnh báo `#e5a33a`. Màu nhấn chọn trong bộ bốn của SnapOne (`#E8664A`, `#E0A53A`, `#2FB5B0`, `#4C8DF6`); mặc định nên khác SnapOne để phân biệt hai app khi mở cạnh nhau.
- **Chữ, icon:** Space Grotesk 12 px, số dùng JetBrains Mono, cỡ chữ Nhỏ, Vừa, Lớn trong Thiết lập; icon Material Symbols Outlined (wght 300, opsz 20), rút gọn bằng `make-icons.sh` của snapone. Công cụ mà bộ Material không có (Clone Stamp, Patch, Dodge, Burn, Sponge, Mixer Brush, Path Selection, Direct Selection, Quick Mask…) vẽ thêm cùng nét.
- **Hình khối, chuyển động:** góc bo 4–10 px; hover, nhấn, chọn đổi màu ngay, không hiệu ứng chuyển (N20 của snapone); chỉ hộp thoại, popup có chuyển động ngắn.
- **Mật độ:** hướng A "Cân bằng" của SnapOne. Lưới đề xuất: thanh trên 44 px như SnapOne, thanh tùy chọn 36 px, thanh công cụ 44 px (một cột; hai cột khi bật trong Thiết lập), dock phải 308 px bằng panel phải của SnapOne, cột icon của panel thu gọn 36 px, tab tài liệu 28 px, thanh trạng thái 24 px.
- **Canvas:** nền xám trung tính (`view` của SnapOne), đổi được 4 mức xám như Photoshop; vùng trong suốt là caro xám trắng.
- **Lớp phủ trên ảnh không dùng màu nhấn**, để rõ trên mọi ảnh: viền kiến trắng đen, tay nắm trắng viền tối, guide xanh lơ, smart guide hồng, Quick Mask đỏ 50% như Photoshop (đổi được trong Thiết lập); con trỏ cọ hai nét trắng đen.
- **Control dùng lại của SnapOne:** Btn, IconButton, Chips, Select, Toggle, TextBox, SliderRow, PanelRow, PanelGroup, PanelHead, PanelSection, PopupMenu (có menu con), DialogBox, Spinner, toast, Histogram, CurveBox, bánh xe màu, FocusRing; thanh trên làm thanh tiêu đề trên Windows (N12), chỉ báo việc chạy nền, hộp thoại AI (N48), khung Thiết lập.
- **Control mới:** menu bar; thanh tùy chọn; thanh công cụ có nhóm bung; tab tài liệu; dock panel có tab, thu gọn thành icon (P2: kéo thả, nổi); dòng layer; ô số có nhãn kéo (scrubby) và đơn vị; ô màu trước/sau; bảng chọn màu; lưới preset (cọ, gradient, pattern, hình, style); thanh gradient có điểm dừng; nét cọ xem trước; ô góc và độ tròn đầu cọ; thanh Blend If (tam giác tách đôi); thanh Levels (ba tam giác trên histogram); thanh dải màu của Hue/Saturation; lưới neo 3 × 3; thước; các lớp phủ trên canvas; Timeline (P3).

```text
┌─ thanh trên 44 ─ logo · File Edit Image Layer Type Select Filter View Window Help · việc nền · ─ □ × ─┐
├─ thanh tùy chọn 36 ─ điều khiển của công cụ đang dùng ───────────────────────────────────────────────┤
│ công cụ │ tab tài liệu 28                          │ icon 36 │ dock 308                              │
│ 44      │ thước                                    │         │  Color · Swatches · Gradients         │
│         │ canvas, nền xám trung tính               │         │  Properties · Adjustments · Retouch   │
│         │                                          │         │  Layers · Channels · Paths            │
├─ thanh trạng thái 24 ─ zoom · cỡ tài liệu · RAM, scratch · thông tin chọn được ──────────────────────┤
```

### Hành vi chung

- **Phản hồi tức thì:** kéo thanh của adjustment, layer style, bộ lọc là ảnh đổi ngay ở bản nhanh, thả tay thì nét; adjustment layer không có nút Áp dụng; hộp thoại bộ lọc có ô Preview, OK, Cancel như Photoshop.
- **Không chặn:** canvas luôn thao tác được; lưu, tự lưu, áp full size, AI, chạy action hiện ở chỉ báo việc chạy nền trên thanh trên như SnapOne (tiến độ, nút hủy), không có hộp thoại chờ.
- **Quy ước của Photoshop:** Shift giữ tỉ lệ, bước góc 15°, cộng vùng chọn; Alt vẽ từ tâm, trừ vùng chọn, lấy màu tạm khi đang vẽ; Ctrl tạm sang Move; Space tạm sang Hand; `[` `]` đổi cỡ cọ, Shift `[` `]` độ cứng; phím số đặt opacity, Shift phím số đặt flow; Alt kéo chuột phải (Windows) hay Ctrl Option kéo (macOS) đổi cỡ và độ cứng cọ ngay trên canvas; Tab ẩn panel; F đổi chế độ màn hình.
- **Ô số:** kéo nhãn để đổi (scrubby); mũi tên lên xuống ±1, Shift ±10; gõ được phép tính đơn giản (`100*2`) và đơn vị (`5cm`, `50%`); nhấp đúp nhãn về mặc định như SnapOne.
- **Hoàn tác:** Ctrl Z nhiều bước, Ctrl Shift Z làm lại; kéo thanh liên tục là một bước.
- **Menu chuột phải:** trên canvas theo công cụ đang dùng, trên layer, panel, tab tài liệu.
- **Mờ và ẩn:** lệnh không dùng được ở ngữ cảnh hiện tại thì mờ; chức năng chưa dựng thì ẩn hẳn, như quyết định 9 của ui-plan snapone.
- **Cảnh báo không chặn:** PSD có phần chưa hỗ trợ, font thiếu, hồ sơ màu khác, bộ nhớ gần hết ngân sách: thanh báo trên canvas có nút Xem chi tiết; chỉ dùng hộp thoại khi người dùng buộc phải chọn.
- **Ngôn ngữ:** tiếng Việt và tiếng Anh từ P1, thuật ngữ theo điểm 9; tooltip có phím tắt và tên tiếng Anh của Photoshop; chừa chỗ cho chuỗi dài nhất của hai thứ tiếng.
- **Bàn phím, trợ năng:** đi được bằng Tab trong panel, hộp thoại (FocusRing của SnapOne); tương phản chữ theo token; cỡ chữ ba mức.

### Màn hình bắt đầu

| Chức năng | Mô tả | Điều khiển và giá trị | Ưu tiên |
| --- | --- | --- | --- |
| Tài liệu gần đây | Mở lại file đã làm | Lưới thumbnail (ảnh ghép trong file) hay danh sách; tên, cỡ, ngày sửa; ô tìm; sắp theo ngày, tên; chuột phải: mở thư mục, bỏ khỏi danh sách | P1 |
| Tạo mới | Tạo tài liệu từ preset | Hàng preset hay dùng (A4 300 ppi, 4K, 1080 × 1350, 1920 × 1080…); nút Tạo mới mở hộp thoại New | P1 |
| Mở file | Mở từ đĩa hay kéo thả | Nút Mở; vùng thả file, thả nhiều file thì mở nhiều tab | P1 |
| Khôi phục | File chưa lưu khi app tắt đột ngột | Thẻ ở đầu màn hình: tên, giờ, Mở, Bỏ | P1 |
| Logo, icon app, màn hình khởi động | Nhận diện của app | Cùng cách làm với SnapOne (wordmark Manrope, icon theo cỡ của từng hệ điều hành) | P1 |

### Khung cửa sổ

| Vùng | Mô tả | Điều khiển và giá trị | Ưu tiên |
| --- | --- | --- | --- |
| Thanh trên | Menu và thanh tiêu đề | Logo; menu File, Edit, Image, Layer, Type, Select, Filter, View, Window, Help, mỗi mục hiện phím tắt; chỉ báo việc chạy nền; nút cửa sổ trên Windows; ô tìm lệnh (Ctrl F) và chọn workspace ở P2 | P0–P1 |
| Thanh tùy chọn | Tùy chọn của công cụ đang dùng | Nội dung theo bảng Công cụ; preset công cụ ở đầu (P2); co gọn khi cửa sổ hẹp | P1 |
| Thanh công cụ | Các nhóm công cụ của Photoshop | Nhóm theo bảng Thanh công cụ; tam giác nhỏ ở nhóm nhiều công cụ, giữ chuột hay chuột phải để bung; Shift + phím đổi công cụ trong nhóm; ô màu trước/sau, đổi (X), mặc định (D); Quick Mask (Q); chế độ màn hình (F); Edit Toolbar ở P2 | P0–P1 |
| Tab tài liệu | Nhiều tài liệu trong một cửa sổ | Tên, chấm chưa lưu, zoom, layer đang chọn, mode và bit ("ảnh.psd @ 33% (Layer 1, RGB/16)"); đóng; menu khi nhiều tab; kéo ra thành cửa sổ nổi ở P2 | P1 |
| Dock panel | Các panel bên phải | Nhóm panel có tab; thu gọn thành cột icon, nhấp icon mở một panel. Mặc định ba nhóm: Color, Swatches, Gradients; Properties, Adjustments, Retouch; Layers, Channels, Paths. P2: kéo thả, panel nổi, ghép nhóm, lưu workspace (Essentials, Photography, Painting, Graphic & Web, Retouch) | P1 |
| Thanh trạng thái | Thông tin tài liệu | Ô zoom gõ được; thông tin chọn được: cỡ tài liệu, hồ sơ màu, kích thước, RAM và scratch đang dùng, thời gian thao tác cuối, công cụ đang dùng | P1 |
| Contextual Task Bar | Thanh nổi dưới vùng chọn hay layer, gợi ý bước kế | Nút theo ngữ cảnh: Select Subject, Remove Background, Select and Mask, Invert, Feather, Fill, Generative Fill (P3); ghim, ẩn | P2 |

### Canvas và lớp phủ

| Chức năng | Mô tả | Điều khiển và giá trị | Ưu tiên |
| --- | --- | --- | --- |
| Zoom, pan, xoay view | Xem từ toàn ảnh tới từng điểm ảnh | Zoom tới 12.800%: con lăn, véo, Ctrl +/−, Z kéo (scrubby zoom); nhấp đúp Hand là vừa màn hình, nhấp đúp Zoom là 100%; R kéo để xoay, Esc về 0° | P0 |
| Thước, guide, lưới | Căn chỉnh | Thước ngang, dọc (chuột phải đổi đơn vị, kéo góc để đặt gốc); kéo từ thước ra guide; lưới; lưới điểm ảnh từ 600%; smart guide và khoảng cách ở P2 | P1 |
| Viền kiến | Vùng chọn đang có | Nét đứt trắng đen chạy; vùng chọn mờ dưới 50% thì báo không hiện viền | P1 |
| Khung biến đổi | Free Transform, Warp | 8 tay nắm, điểm gốc, xoay ngoài khung, Ctrl kéo góc để Distort; HUD W, H, góc; lưới Warp (3 × 3, 4 × 4, 5 × 5, tùy ý) | P1 |
| Con trỏ cọ | Thấy cỡ, độ cứng | Viền theo hình đầu cọ, chữ thập ở giữa, Caps Lock chỉ còn chữ thập; HUD cỡ, độ cứng khi đổi trên canvas | P0–P1 |
| Nguồn Clone, Healing | Thấy chỗ lấy mẫu | Dấu chữ thập ở nguồn; lớp phủ xem trước nguồn trong vòng cọ | P1 |
| Gradient trên canvas | Sửa gradient sau khi kéo | Đường gradient có điểm dừng màu kéo được; nhấp đúp đổi màu, kéo ra ngoài để xóa | P1 |
| Chữ trên canvas | Gõ và sửa chữ | Khung chữ đoạn có tay nắm; con trỏ nhập, bôi chọn; gạch chân khi đang gõ IME; xác nhận, hủy ở thanh tùy chọn | P1 |
| Hình | Live shape | Tay nắm cỡ; chấm bo góc kéo được; điểm neo, tay nắm Bezier ở P2 | P1 |
| Crop | Khung cắt | Khung, tấm che phần bị cắt, lưới phụ (1/3, lưới, đường chéo, tam giác, tỉ lệ vàng, xoắn vàng), xoay ngoài khung | P1 |
| Quick Mask | Vẽ vùng chọn | Lớp phủ đỏ 50% ở phần không chọn | P1 |
| Thanh báo | PSD có phần chưa hỗ trợ, font thiếu, hồ sơ màu khác, bộ nhớ gần hết | Thanh ngang trên canvas, nút Xem chi tiết, Bỏ qua | P1 |
| Tiến độ trên canvas | Áp bộ lọc full size, AI | Thanh mảnh ở mép trên canvas, nút hủy; canvas vẫn dùng được | P1 |
| Bảng màu HUD | Chọn màu ngay khi vẽ | Alt Shift chuột phải: vòng hue hay dải hue cùng ô SV dưới con trỏ | P2 |
| Dấu đo, ghi chú | Color Sampler, Ruler, Note | Dấu đánh số 1–10; đường đo có góc; icon ghi chú | P2 |

### Công cụ

Mỗi dòng là thanh tùy chọn của công cụ; phần trên canvas ở bảng trước.

| Công cụ (phím) | Thanh tùy chọn | Ưu tiên |
| --- | --- | --- |
| Move (V) | Auto-Select (Layer, Group); Show Transform Controls; 6 nút căn (P2 thêm phân bố, căn theo canvas); Alt kéo để nhân bản | P1 |
| Artboard (V) | Cỡ preset; nút + quanh artboard trên canvas; màu nền | P2 |
| Marquee (M) | 4 chế độ (mới, cộng, trừ, giao); Feather 0–1000 px; Anti-alias; Style (Normal, Fixed Ratio, Fixed Size) với W, H; nút Select and Mask | P1 (Single Row, Single Column ở P2) |
| Lasso, Polygonal Lasso (L) | 4 chế độ; Feather; Anti-alias; Select and Mask | P1 |
| Magnetic Lasso (L) | Như Lasso, thêm Width, Contrast, Frequency, lực nhấn bút | P3 |
| Magic Wand (W) | Sample Size (Point tới 101 × 101); Tolerance 0–255; Anti-alias; Contiguous; Sample All Layers; nút Select Subject, Select and Mask | P1 |
| Selection Brush (W) | Cộng, trừ; cỡ, độ cứng; lớp phủ khi vẽ (màu, độ mờ) | P1 |
| Quick Selection (W) | Cộng, trừ; cỡ cọ; Sample All Layers; Enhance Edge; Select Subject | P2 |
| Object Selection (W) | Mode (Rectangle, Lasso); Object Finder (rê chuột thấy vật được tô sẵn); Sample All Layers | P3 |
| Crop (C) | Tỉ lệ (Ratio, W × H × Resolution, Original, 1:1, 4:5, 5:7, 2:3, 16:9, preset riêng); ô W, H, nút đổi chiều, Clear; Straighten (kẻ đường); lưới phụ; bánh răng (Classic Mode, hiện vùng bị cắt, tấm che: màu, độ mờ); Delete Cropped Pixels; Content-Aware ở P3 | P1 |
| Perspective Crop (C) | W, H, Resolution, Front Image, Clear, Show Grid | P1 |
| Frame (K) | Khung chữ nhật, elip; kéo ảnh thả vào khung | P2 |
| Eyedropper (I) | Sample Size (Point, 3 × 3 tới 101 × 101); Sample (Current Layer, Current & Below, All Layers, bỏ qua adjustment); Show Sampling Ring (vòng: màu mới trên, màu cũ dưới) | P1 |
| Color Sampler, Ruler, Note (I) | Color Sampler: Sample Size, Clear All. Ruler: X, Y, W, H, góc, L1, L2, Straighten Layer, Clear. Note: tác giả, màu | P2 |
| Count (I) | Nhóm đếm, màu, cỡ dấu | P3 |
| Spot Healing Brush (J) | Cọ (cỡ, độ cứng); Mode; Type (Proximity Match, Create Texture; Content-Aware ở P3); Sample All Layers | P1 |
| Healing Brush (J) | Cọ; Mode; Source (Sampled; Pattern ở P2); Aligned; Sample (Current Layer, Current & Below, All Layers); Diffusion 1–7; Alt nhấp để đặt nguồn | P1 |
| Patch (J) | Normal (Content-Aware ở P3); Source hay Destination; Transparent; Diffusion | P1 |
| Red Eye (J) | Pupil Size, Darken Amount | P2 |
| Remove, Content-Aware Move (J) | Remove: cỡ cọ, Sample All Layers, xóa sau mỗi nét, Find Distractions (dây điện, người). Content-Aware Move: Move hay Extend, Structure, Color, Transform On Drop | P3 |
| Brush (B) | Bảng preset cọ (cỡ 1–5000 px, độ cứng 0–100%, lưới đầu cọ); nút panel Brush Settings; Mode (blend mode của vẽ, cả Behind, Clear); Opacity 1–100% và Flow 1–100%, mỗi ô có nút lực nhấn; Airbrush; Smoothing 0–100% (bánh răng: Pulled String Mode, Stroke Catch-up, Catch-up on Stroke End, Adjust for Zoom); góc; nút lực nhấn cho cỡ; Symmetry ở P2 | P0–P1 |
| Pencil (B) | Như Brush, thêm Auto Erase | P1 |
| Color Replacement, Adjustment Brush (B) | Color Replacement: Mode, Sampling (Continuous, Once, Background Swatch), Limits, Tolerance. Adjustment Brush: loại adjustment, cộng, trừ, cỡ; vẽ ra adjustment layer có mặt nạ | P2 |
| Mixer Brush (B) | Wet, Load, Mix, Flow; nạp, rửa cọ sau mỗi nét; Sample All Layers | P3 |
| Clone Stamp (S) | Cọ; Mode; Opacity; Flow; Aligned; Sample; nút panel Clone Source (P2) | P1 |
| Pattern Stamp (S) | Pattern; Aligned; Impressionist | P2 |
| History Brush, Art History Brush (Y) | Cọ, Mode, Opacity, Flow; nguồn chọn ở panel History. Art History: Style, Area, Tolerance | P2 · P3 |
| Eraser (E) | Mode (Brush, Pencil, Block); Opacity; Flow; Smoothing; Erase to History ở P2 | P1 |
| Background Eraser, Magic Eraser (E) | Background Eraser: Sampling, Limits, Tolerance, Protect Foreground Color. Magic Eraser: Tolerance, Anti-alias, Contiguous, Sample All Layers, Opacity | P2 |
| Gradient (G) | Bảng preset gradient; Gradient (sửa lại được trên canvas) hay Classic; 5 kiểu (Linear, Radial, Angle, Reflected, Diamond); Mode; Opacity; Reverse; Dither; Method (Perceptual, Linear, Classic, Smooth, Stripes) | P1 |
| Paint Bucket (G) | Fill (màu trước; Pattern ở P2); Mode; Opacity; Tolerance; Anti-alias; Contiguous; All Layers | P1 |
| Blur, Sharpen, Smudge | Cọ; Mode; Strength 1–100%; Sample All Layers; Sharpen: Protect Detail; Smudge: Finger Painting | P2 |
| Dodge, Burn, Sponge (O) | Range (Shadows, Midtones, Highlights); Exposure 1–100%; Airbrush; Protect Tones. Sponge: Desaturate hay Saturate, Flow, Vibrance | P1 |
| Pen, Freeform Pen, Curvature Pen (P) | Shape hay Path; nút Selection, Mask, Shape; thao tác path (Combine, Subtract, Intersect, Exclude); căn path; Rubber Band; Auto Add/Delete; Freeform Pen: Magnetic | P2 |
| Path Selection, Direct Selection (A) | Chọn trong layer đang chọn hay mọi layer; thao tác path; căn, phân bố; Constrain Path Dragging | P2 |
| Type (T) | Font (xem trước bằng chữ tiếng Việt, yêu thích, gần đây, tìm, lọc theo nhóm); kiểu; cỡ (pt); khử răng cưa (None, Sharp, Crisp, Strong, Smooth); căn lề; màu; nút Warp Text (P2); nút panel Character, Paragraph; xác nhận, hủy | P1 (chữ dọc, Type Mask ở P2) |
| Shape (U) | Shape, Path hay Pixels; Fill (không, màu, gradient; pattern ở P2); Stroke (màu, độ dày; nét liền, đứt, chấm; căn trong, giữa, ngoài; đầu nét, góc nét); W, H; thao tác path; bo góc. Polygon: số cạnh, tỉ lệ sao. Line: độ dày, đầu mũi tên | P1 (Custom Shape ở P2) |
| Hand (H), Rotate View (R), Zoom (Z) | Hand: 100%, Fit Screen, Fill Screen, Scroll All Windows (P2). Rotate View: góc, Reset View. Zoom: phóng, thu, Resize Windows to Fit, Scrubby Zoom, 100%, Fit, Fill | P0–P1 |

### Panel

| Panel | Nội dung và điều khiển | Ưu tiên |
| --- | --- | --- |
| Layers | Lọc theo loại (điểm ảnh, adjustment, chữ, hình, smart object) và tên; Blend mode, Opacity, Fill; 4 nút khóa. Dòng layer: mắt (Alt nhấp để chỉ hiện layer này), thumbnail layer, thumbnail mặt nạ có xích, tên (nhấp đúp đổi tên), huy hiệu smart object, fx (mở danh sách hiệu ứng, mỗi hiệu ứng một mắt), mũi tên clipping, nhóm thu gọn và thụt lề, khóa, màu nhãn. Chọn nhiều (Shift, Ctrl), kéo đổi thứ tự, Alt kéo nhân bản. Đáy: Link, fx, mặt nạ, adjustment và fill, nhóm, layer mới, xóa. Cỡ thumbnail chọn được; danh sách ảo hóa, 1.000 layer vẫn cuộn mượt | P0–P1 (artboard ở P2, video ở P3) |
| Properties | Theo ngữ cảnh. Tài liệu: cỡ canvas, độ phân giải, mode; nút nhanh Select Subject, Remove Background. Layer điểm ảnh: X, Y, W, H, góc, căn. Chữ: Character, Paragraph rút gọn. Hình: W, H, X, Y, fill, stroke, bo từng góc, thao tác path. Adjustment layer: điều khiển của nó (bảng Adjustment), clip vào layer dưới, xem trạng thái trước, đặt lại, mắt. Mặt nạ: Density, Feather, Select and Mask, Color Range, Invert. Smart object: nhúng hay liên kết, Edit Contents | P1 |
| Adjustments | Lưới 16 icon adjustment, nhấp là thêm adjustment layer; preset adjustment ở P2 | P1 |
| History | Snapshot có thumbnail ở đầu; các bước có icon công cụ, nhấp để quay về, bước sau mờ đi; tạo snapshot, tài liệu mới từ bước, xóa; cột nguồn của History Brush ở P2 | P1 |
| Color | Ô màu trước/sau; thanh HSB, RGB, Lab (CMYK ở P2) hay vòng màu; hex; cảnh báo ngoài gam ở P2 | P1 |
| Swatches | Hàng màu gần đây; nhóm swatch thu gọn được; thêm, xóa; nhập, xuất `.aco`, `.ase` ở P2 | P1 |
| Gradients | Lưới preset theo nhóm; gần đây; thêm, xóa | P1 |
| Patterns, Shapes, Styles | Lưới preset theo nhóm, cỡ ô, thêm, xóa, nhập, xuất | P2 |
| Brushes | Preset theo nhóm (đầu cọ, tên, nét xem trước); thanh cỡ; tìm; thêm, xóa; nhập `.abr` ở P3 | P1 |
| Brush Settings | Đầu cọ: cỡ, lật X, Y, góc và độ tròn kéo trên elip, độ cứng, spacing 1–1000%. Shape Dynamics: dao động cỡ, góc, độ tròn; điều khiển bằng lực nhấn, độ nghiêng, xoay bút. Transfer: dao động opacity, flow theo lực nhấn. Smoothing, Build-up, Noise, Wet Edges. P2: Scattering, Texture, Color Dynamics, Brush Pose. P3: Dual Brush. Nét xem trước ở đáy | P1 |
| Channels | Kênh tổng và R, G, B (K ở CMYK, P2); kênh alpha; kênh spot ở P3; mắt, thumbnail; đáy: tải làm vùng chọn, lưu vùng chọn, kênh mới, xóa | P1 |
| Paths | Work path, path đã lưu, mặt nạ vector; đáy: tô, viền, tải làm vùng chọn, tạo path từ vùng chọn, thêm mặt nạ, path mới, xóa | P2 |
| Navigator | Ảnh thu nhỏ với khung đang xem (xoay theo view), kéo khung để đi; ô zoom và thanh trượt | P1 |
| Histogram | Dạng gọn như SnapOne (RGB chồng màu). Dạng mở rộng ở P2: kênh (RGB, Luminosity, Colors, R, G, B), nguồn (cả ảnh, layer đang chọn, adjustment), số liệu (Mean, Std Dev, Median, Pixels, Level, Count, Percentile) | P1 · P2 |
| Info | Hai ô đọc màu chọn được (thực, in thử, RGB, CMYK, Lab, HSB, xám, tổng mực, opacity); X, Y; W, H, góc khi biến đổi; dòng thông tin tài liệu; gợi ý của công cụ đang dùng | P1 |
| Character, Paragraph | Điều khiển của bảng Menu Type | P1 (phần P2 như bảng đó) |
| Glyphs, Character Styles, Paragraph Styles | Lưới glyph của font, lọc theo nhóm; danh sách style, tạo từ chữ đang chọn, ghi đè | P2 |
| Actions | Bộ action, action, bước; nút ghi, chạy, dừng; ô bật hộp thoại ở từng bước; chế độ nút ở P3 | P2 |
| Clone Source | 5 nguồn; dời X, Y; W, H (%); góc; Show Overlay (độ mờ, chế độ, Clipped, Auto Hide, Invert) | P2 |
| Tool Presets, Notes | Preset của từng công cụ, lọc theo công cụ đang dùng; ghi chú của tài liệu | P2 |
| Version History | Các bản đã lưu theo giờ; đặt tên, đánh dấu giữ; mở bản cũ, khôi phục, mở thành bản sao | P2 |
| Retouch | Bảng Retouch chân dung bên dưới | P1 |
| Timeline | Khung hình: dải khung, thời gian mỗi khung, lặp, nút chạy. Video: rãnh theo layer, keyframe vị trí, opacity, style; đầu phát; xuất video | P3 |
| Layer Comps, Measurement Log | Danh sách trạng thái layer, áp, cập nhật; bảng số đo, xuất | P3 |

### Adjustment

Điều khiển như nhau ở panel Properties (adjustment layer) và ở hộp thoại của lệnh Image › Adjustments (thêm ô Preview, OK, Cancel).

| Adjustment | Điều khiển và giá trị | Ưu tiên |
| --- | --- | --- |
| Brightness/Contrast | Brightness −150 đến +150; Contrast −50 đến +100; Use Legacy; Auto | P1 |
| Levels | Kênh; preset; histogram với 3 tam giác đầu vào (đen, gamma 0,01–9,99, trắng) và 2 tam giác đầu ra (0–255); 3 ống hút đen, xám, trắng; Auto, Options | P1 |
| Curves | Kênh; preset; ô đường cong trên histogram (CurveBox của SnapOne), thêm điểm, vẽ tay ở P2; bàn tay kéo trên ảnh; 3 ống hút; ô Input, Output; Alt hiện vùng bị cắt; Auto | P1 |
| Exposure | Exposure −20 đến +20; Offset −0,5 đến +0,5; Gamma 0,01–9,99; 3 ống hút | P1 |
| Vibrance | Vibrance, Saturation −100 đến +100 | P1 |
| Hue/Saturation | Preset; dải màu (Master, Reds, Yellows, Greens, Cyans, Blues, Magentas); Hue −180 đến +180; Saturation, Lightness −100 đến +100; Colorize; thanh dải màu có 4 tay nắm; bàn tay kéo trên ảnh | P1 |
| Color Balance | Shadows, Midtones, Highlights; ba thanh Cyan–Red, Magenta–Green, Yellow–Blue −100 đến +100; Preserve Luminosity | P1 |
| Black & White | Preset; 6 thanh màu −200% đến +300%; Tint (màu); Auto; bàn tay kéo trên ảnh | P1 |
| Photo Filter | Bộ lọc có sẵn hay màu; Density 1–100%; Preserve Luminosity | P1 |
| Channel Mixer | Kênh ra; R, G, B −200% đến +200%; Constant; Monochrome; tổng | P1 |
| Color Lookup | File 3DLUT, Abstract, Device Link; Dither | P1 |
| Invert, Posterize, Threshold | Posterize: Levels 2–255. Threshold: 1–255 trên histogram | P1 |
| Gradient Map | Gradient; Dither; Reverse; Method | P1 |
| Selective Color | 9 màu (Reds tới Blacks); Cyan, Magenta, Yellow, Black −100% đến +100%; Relative hay Absolute | P1 |
| Shadows/Highlights | Shadows, Highlights (Amount, Tone, Radius); Color, Midtone, Black Clip, White Clip | P2 |
| HDR Toning, Match Color, Replace Color | Điều khiển như Photoshop | P2 |

### Hộp thoại

| Hộp thoại | Nội dung và điều khiển | Ưu tiên |
| --- | --- | --- |
| New | Preset theo nhóm (Gần đây, Đã lưu, Ảnh, In, Minh họa, Web, Di động, Video); chi tiết: tên, W, H, đơn vị, hướng, độ phân giải, mode, bit, nền (trắng, đen, màu nền sau, trong suốt, tùy chọn), hồ sơ màu; lưu preset; Artboards ở P2 | P1 |
| Lưu, Lưu bản sao, Mở | Hộp thoại file của hệ điều hành; tùy chọn theo định dạng (PSD: Maximize Compatibility; TIFF: nén, giữ layer; JPEG: chất lượng 0–12 như Photoshop; PNG: mức nén); nhúng hồ sơ màu | P1 |
| Export As | Xem trước (phóng, so với gốc); định dạng (JPEG, PNG, WebP, TIFF; GIF, AVIF, JPEG XL ở P2); chất lượng 1–100; cỡ ảnh (W, H, %, cách lấy mẫu); cỡ canvas; nhiều tỉ lệ (0,5×, 2×… kèm hậu tố); metadata (không, bản quyền và liên hệ); đổi sang sRGB, nhúng hồ sơ; dung lượng ước tính; xuất mọi layer, artboard ở P2 | P1 |
| Image Size | Thumbnail xem trước; W, H có xích; đơn vị; độ phân giải; Resample (Automatic, Bicubic, Bicubic Smoother, Bicubic Sharper, Bilinear, Nearest Neighbor, Lanczos; Preserve Details ở P3); Scale Styles; preset Fit To | P1 |
| Canvas Size | Cỡ hiện tại; cỡ mới; Relative; lưới neo 3 × 3; màu phần mở rộng | P1 |
| Color Picker | Ô SV lớn và dải hue; màu mới, màu cũ; ô HSB, RGB, Lab (CMYK ở P2), hex; Add to Swatches; cảnh báo ngoài gam ở P2 | P1 |
| Bộ lọc | Khuôn chung: ô xem trước (phóng, nhấp giữ để so với gốc), ô Preview trên canvas, các thanh, OK, Cancel. Ví dụ: Gaussian Blur (Radius 0,1–1000 px); Unsharp Mask (Amount 1–500%, Radius 0,1–1000 px, Threshold 0–255); Add Noise (Amount, Uniform hay Gaussian, Monochromatic); Reduce Noise (Strength, Preserve Details, Reduce Color Noise, Sharpen Details, Remove JPEG Artifact); Custom (lưới 5 × 5, Scale, Offset) | P1 (các họ P2, P3 như bảng Menu Filter) |
| Fill, Stroke | Fill: nội dung (màu trước, màu sau, màu, lịch sử, đen, xám 50%, trắng; pattern ở P2; Content-Aware ở P3), Mode, Opacity, Preserve Transparency. Stroke: độ dày, màu, vị trí (trong, giữa, ngoài), Mode, Opacity | P1 |
| Layer Style | Danh sách hiệu ứng bên trái có ô bật, nút + để thêm hiệu ứng cùng loại (P2); trang thông số bên phải; Blending Options (Mode, Opacity, Fill, kênh, Blend If với tam giác tách đôi khi Alt kéo; Knockout ở P2); xem trước; New Style, Make Default, Reset to Default | P1 (Blending Options và 4 hiệu ứng) · P2 |
| Color Settings, Assign Profile, Convert to Profile | Không gian làm việc (RGB, Gray; CMYK, Spot ở P2); chính sách khi file có hồ sơ khác hay thiếu hồ sơ; rendering intent; Black Point Compensation; dither | P1 |
| Hồ sơ khác, font thiếu, báo PSD | Dùng hồ sơ nhúng, chuyển, bỏ; thay font thiếu; danh sách phần chưa hỗ trợ theo layer | P1 |
| Lưu khi đóng, khôi phục sau crash | Lưu, Không lưu, Hủy; danh sách file khôi phục (tên, giờ), Mở, Bỏ | P1 |
| Phím tắt | Bảng theo nhóm (menu, panel, công cụ) có ô tìm; P2: bấm để ghi phím, báo trùng, bộ phím, xuất danh sách | P1 · P2 |
| Print | Máy in, số bản, hướng; quản lý màu (máy in hay app quản lý, hồ sơ máy in, rendering intent, BPC); vị trí, cỡ, co vừa giấy; xem trước có cảnh báo ngoài gam | P2 |
| Image Processor, Batch | Nguồn, đích, định dạng, cỡ, action, mẫu tên; chạy nền có tiến độ | P2 |
| AI | Tải model, đồng ý gửi ảnh, khóa API, chi phí, lỗi có nút (như N48 của SnapOne) | P1 (tải model của Select Subject) · P3 |

### Không gian làm việc toàn màn hình

| Không gian | Nội dung và điều khiển | Ưu tiên |
| --- | --- | --- |
| Select and Mask | Chế độ xem (Onion Skin, Marching Ants, Overlay, On Black, On White, Black & White, On Layers) và độ mờ; Show Edge, Show Original, High Quality Preview. Công cụ bên trái: Refine Edge Brush, Brush, Lasso, Hand, Zoom; Quick Selection ở P2, Object Selection ở P3. Bên phải: Refine Mode (Color Aware, Object Aware); Edge Detection (Radius, Smart Radius); Global Refinements (Smooth, Feather, Contrast, Shift Edge); Clear Selection, Invert; nút Select Subject, Refine Hair; Output (Decontaminate Colors ở P2; ra vùng chọn, mặt nạ, layer mới, layer có mặt nạ, tài liệu mới); Remember Settings | P1 |
| Liquify | Công cụ: Forward Warp, Reconstruct, Smooth, Twirl Clockwise, Pucker, Bloat, Push Left, Freeze Mask, Thaw Mask, Face, Hand, Zoom. Cọ: Size, Density, Pressure, Rate, lực nhấn bút, Pin Edges. Face-Aware: chọn mặt; Eyes (cỡ, cao, rộng, nghiêng, khoảng cách; liên kết hai mắt), Nose (cao, rộng), Mouth (cười, môi trên, môi dưới, rộng, cao), Face Shape (trán, cằm, đường hàm, độ rộng mặt). Mask: thay, cộng, trừ, giao, đảo; từ vùng chọn, trong suốt, mặt nạ. View: guide, lớp phủ mặt, lưới (cỡ, màu), mặt nạ, backdrop (layer, chế độ, độ mờ). Reconstruct, Restore All | P1 |
| Camera Raw Filter | Panel chỉnh của SnapOne (Cơ bản, Đường cong, HSL, Phân màu, Chi tiết, Ống kính, Hiệu ứng, Mặt nạ) trong khung toàn màn hình; trước/sau | P2 |
| Blur Gallery | Field, Iris, Tilt-Shift, Path, Spin (bật nhiều cùng lúc); chốt trên canvas (vòng mờ, elip, dải tilt-shift, đường path, vòng spin); Effects (Light Bokeh, Bokeh Color, Light Range); Motion Effects; Noise | P2 |
| Lens Correction | Auto Correction theo profile; Custom: Distortion, Chromatic Aberration, Vignette, Transform; lưới | P2 |
| Content-Aware Fill | Lớp phủ vùng lấy mẫu (vẽ thêm, bớt); Sampling Area (Auto, Rectangular, Custom); Fill Settings (Color Adaptation, Rotation Adaptation, Scale, Mirror); Output (layer hiện tại, layer mới, bản sao layer); ô xem trước | P3 |
| Neural Filters | Danh sách bộ lọc có công tắc; thông số từng bộ lọc; trạng thái tải model; Output | P3 |
| Adaptive Wide Angle, Vanishing Point, Sky Replacement, Puppet Warp, Perspective Warp | Điều khiển như Photoshop; Puppet Warp, Perspective Warp làm ngay trên canvas (lưới tam giác và ghim; chế độ Layout, Warp) | P3 |

### Retouch chân dung

| Chức năng | Mô tả | Điều khiển và giá trị | Ưu tiên |
| --- | --- | --- | --- |
| Panel Retouch | Điểm vào các công cụ chân dung | Danh sách khuôn mặt tìm được (thumbnail, nhấp để chọn mặt); các nút bên dưới; nằm trong dock như mọi panel | P1 |
| Tách tần số | Tạo nhóm Thấp, Cao | Radius (px, gợi ý theo cỡ mặt); xem lớp Thấp hay Cao; Tạo; ra nhóm "Tách tần số" trong Layers | P1 |
| Mặt nạ da, mắt, môi, răng | Vùng chọn tự động từ landmark | Ô chọn vùng; lớp phủ màu xem trước; Feather; ra vùng chọn hay mặt nạ | P1 |
| Làm mịn da | Mịn mà giữ vân | Amount 0–100, Texture 0–100, Radius; chỉ trong mặt nạ da; ra layer riêng có mặt nạ | P1 |
| Dodge & Burn | Dựng sẵn layer để tô sáng, tối | Chọn cách: cặp Curves sáng, tối có mặt nạ đen, hay layer xám 50% Soft Light; tự chuyển sang cọ opacity thấp | P1 |
| Mắt, răng | Làm trắng, làm sáng | Amount từng vùng | P1 |
| Tìm mụn, vết | Tự tìm rồi xóa | Độ nhạy; vết tìm thấy hiện trên ảnh, bật tắt từng vết; Xóa hết | P2 |
| Làm đều màu da | Đều sắc da | Amount; màu da mục tiêu (ống hút) | P2 |
| Retouch cả loạt | Một công thức cho nhiều ảnh | Lưu công thức; chọn file, thư mục đích; chạy nền có tiến độ | P2 |
| Mở từ SnapOne | Chỉnh trong photo-beauty rồi quay về | Thanh báo "Mở từ SnapOne: lưu sẽ cập nhật ảnh trong SnapOne"; nút Lưu và quay lại | P2 |

### Thiết lập

| Mục | Điều khiển và giá trị | Ưu tiên |
| --- | --- | --- |
| Chung | Place luôn tạo smart object; đổi cỡ ảnh khi Place; Free Transform giữ tỉ lệ mặc định (Use Legacy Free Transform); tự tải lại file đang mở khi file đổi trên đĩa | P1 |
| Giao diện | Theme tối, sáng; màu nhấn; màu nền canvas (4 mức xám, tùy chọn) theo chế độ màn hình; viền canvas (bóng, đường, không); cỡ chữ Nhỏ, Vừa, Lớn; ngôn ngữ (Tiếng Việt, English); hiện kênh bằng màu | P1 |
| Workspace | Tự thu gọn panel về icon; mở tài liệu dạng tab; thanh tùy chọn hẹp; cửa sổ tài liệu nổi ở P2 | P1 |
| Công cụ | Tooltip; Shift để đổi công cụ trong nhóm; zoom bằng con lăn; zoom vào chỗ nhấp; zoom có chuyển động; bắt dính lưới điểm ảnh; nhấp đúp mặt nạ mở Select and Mask | P1 |
| Lịch sử, khôi phục | Ngân sách lịch sử (% ngân sách ô); tự lưu khôi phục mỗi N phút; nhật ký lịch sử ở P2 | P1 |
| File, xuất | Maximize PSD Compatibility (Luôn, Hỏi, Không); số file gần đây; Quick Export (định dạng, chỗ lưu, metadata, đổi sang sRGB) | P1 |
| Hiệu năng | Ngân sách RAM (thanh theo % RAM máy, hiện RAM có); ngân sách VRAM; bật tắt GPU, tên GPU; ổ scratch (danh sách ổ, dung lượng trống, thứ tự); mức cache; đồng hồ RAM, scratch đang dùng | P1 |
| Con trỏ | Khi vẽ: Standard, Precise, Normal Brush Tip, Full Size Brush Tip, chữ thập trong đầu cọ, chỉ chữ thập khi vẽ; công cụ khác: Standard, Precise; màu xem trước cọ | P1 |
| Trong suốt, gam | Cỡ ô caro (Không, Nhỏ, Vừa, Lớn), màu ô; màu, độ mờ cảnh báo ngoài gam ở P2 | P1 |
| Đơn vị, thước | Đơn vị thước, chữ; độ phân giải mặc định cho in, màn hình | P1 |
| Guide, lưới | Màu, kiểu guide; màu smart guide; màu, kiểu, khoảng lưới, số ô chia; bật lưới điểm ảnh | P1 |
| Chữ | Ngoặc kép thông minh; bảo vệ glyph thiếu; tên font bằng tiếng Anh; cỡ xem trước font; chữ mẫu khi tạo layer chữ | P1 |
| Bút | Windows Ink hay Wintab (Windows); đường cong lực nhấn ở P2 | P1 |
| Phím tắt | Như hộp thoại Phím tắt | P1 · P2 |
| AI | Model trên máy (tải, xóa, dung lượng); hãng, khóa API, trần chi tiêu, đồng ý gửi ảnh (như N48 của SnapOne) | P1 (model) · P3 |
| Báo lỗi, số liệu, cập nhật, giấy phép | Như SnapOne (N24, N27): hai công tắc, mã cài đặt; dùng thử rồi kích hoạt bằng mã | P1 |
| Plugin, script | Danh sách plugin, bật tắt, nhật ký lỗi; thư mục script | P3 |
