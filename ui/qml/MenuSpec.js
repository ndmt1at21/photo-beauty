.pragma library
// The menu bar (docs/ui-tasks.md, U4): MENUS and SUBS of build/proto/Main.dc.html (copied by a script, not retyped),
// their actions, requirements and keys, as PopupMenu items. One source for the menus, the keys Main registers and the
// Phím tắt dialog (U20). An item:
//   l the label; k its keys as shown ("Ctrl ⇧ Z", Main registers them, see sequences()); disabled always greyed;
//   act "@name[:arg]", the prototype's exec(): Docs.exec() runs it, but for Main's own (openFile, quit, close,
//       closeAll, clearRecent, rename, noop);
//   dialog "<Name>" (+ params): Main.openDialog() opens <Name>Dialog.qml; space "<Name>" (+ params): Main.openSpace()
//       opens <Name>Space.qml; either greyed while that file is not in the module (Main.can());
//   req what it needs, a key of Doc.can() ("doc", "sel", "!bgl"), else its action's REQ (Mock.js);
//   sub a submenu's items ("recent": the recent files, Docs.recent), w its width;
//   free: runs with no document open too (Start.dc.html's menus: Window, Help, Tạo mới, Mở, Thoát, Thiết lập…);
//       anything else needs one;
//   fade: the label names the step Hoàn tác would take back ("Fade Brush…").
// Prototype artboards become dialogs: FillStroke → Fill, Stroke; ImageSize → ImageSize, CanvasSize; ColorSettings →
// ColorSettings, AssignProfile, ConvertProfile; Filters → Filter { filter }; LayerStyle(Blend) → LayerStyle { page };
// Notices → MissingFonts (Thoát asks SaveChanges through Main); @dlg:<adjustment> → Adjust { adjust }. A param's value
// is the item's English name in camelCase ("Gaussian Blur…" → "gaussianBlur", "Hue/Saturation" → "hueSaturation").

var SEP = { t: "sep" }

var MENUS = [
    { title: qsTr("&File"), w: 250, items: [
        { l: qsTr("Tạo mới…"), k: "Ctrl N", dialog: "NewDoc", free: true },
        { l: qsTr("Mở…"), k: "Ctrl O", act: "@openFile", free: true },
        { l: qsTr("Mở gần đây"), sub: "recent", w: 260, free: true },
        SEP,
        { l: qsTr("Đóng"), k: "Ctrl W", act: "@close" },
        { l: qsTr("Đóng tất cả"), k: "Ctrl Alt W", act: "@closeAll" },
        SEP,
        { l: qsTr("Lưu"), k: "Ctrl S", act: "@save" },
        { l: qsTr("Lưu thành…"), k: "Ctrl ⇧ S", dialog: "SaveOptions", req: "doc" },
        { l: qsTr("Lưu bản sao…"), k: "Ctrl Alt S", dialog: "SaveOptions", params: { copy: true }, req: "doc" },
        { l: qsTr("Hoàn nguyên"), k: "F12", act: "@revert" },
        SEP,
        { l: qsTr("Place Embedded…"), act: "@place" },
        { l: qsTr("Xuất"), w: 230, sub: [
            { l: qsTr("Xuất nhanh PNG"), act: "@quickExport" },
            { l: qsTr("Xuất thành…"), k: "Ctrl ⇧ Alt W", dialog: "ExportAs", req: "doc" }
        ] },
        SEP,
        { l: qsTr("Thoát"), k: "Ctrl Q", act: "@quit", free: true }
    ] },
    { title: qsTr("&Edit"), w: 270, items: [
        { l: qsTr("Hoàn tác"), k: "Ctrl Z", act: "@undo" },
        { l: qsTr("Làm lại"), k: "Ctrl ⇧ Z", act: "@redo" },
        { l: qsTr("Đổi qua lại bước cuối"), k: "Ctrl Alt Z", act: "@toggleLast" },
        { l: qsTr("Fade…"), k: "Ctrl ⇧ F", act: "@noop", req: "undo", fade: true },
        SEP,
        { l: qsTr("Cắt"), k: "Ctrl X", act: "@noop", req: "sel" },
        { l: qsTr("Sao chép"), k: "Ctrl C", act: "@noop", req: "sel" },
        { l: qsTr("Sao chép hợp nhất"), k: "Ctrl ⇧ C", act: "@noop", req: "sel" },
        { l: qsTr("Dán"), k: "Ctrl V", act: "@paste" },
        { l: qsTr("Dán tại chỗ"), k: "Ctrl ⇧ V", act: "@paste" },
        { l: qsTr("Xóa"), k: "Del", act: "@noop", req: "sel" },
        SEP,
        { l: qsTr("Fill…"), k: "⇧ F5", dialog: "Fill", req: "doc" },
        { l: qsTr("Stroke…"), dialog: "Stroke", req: "sel" },
        SEP,
        { l: qsTr("Free Transform"), k: "Ctrl T", act: "@xf" },
        { l: qsTr("Transform"), w: 300, sub: [
            { l: qsTr("Lặp lại"), k: "Ctrl ⇧ T", act: "@noop" },
            SEP,
            { l: qsTr("Scale"), act: "@xf" },
            { l: qsTr("Rotate"), act: "@xf" },
            { l: qsTr("Skew"), act: "@xf" },
            { l: qsTr("Distort"), act: "@xf" },
            { l: qsTr("Perspective"), act: "@xf" },
            { l: qsTr("Warp"), act: "@warpOn" },
            SEP,
            { l: qsTr("Xoay 180°"), act: "@rot:180" },
            { l: qsTr("Xoay 90° theo chiều kim đồng hồ"), act: "@rot:90" },
            { l: qsTr("Xoay 90° ngược chiều kim đồng hồ"), act: "@rot:270" },
            SEP,
            { l: qsTr("Lật ngang"), act: "@flip:h" },
            { l: qsTr("Lật dọc"), act: "@flip:v" }
        ] },
        SEP,
        { l: qsTr("Define Brush Preset…"), act: "@noop" },
        SEP,
        { l: qsTr("Color Settings…"), k: "Ctrl ⇧ K", dialog: "ColorSettings", free: true },
        { l: qsTr("Assign Profile…"), dialog: "AssignProfile", req: "doc" },
        { l: qsTr("Convert to Profile…"), dialog: "ConvertProfile", req: "doc" },
        SEP,
        { l: qsTr("Phím tắt…"), k: "Ctrl ⇧ Alt K", dialog: "Shortcuts", free: true },
        { l: qsTr("Thiết lập…"), k: "Ctrl K", dialog: "Settings", free: true }
    ] },
    { title: qsTr("&Image"), w: 236, items: [
        { l: qsTr("Mode"), w: 190, sub: [
            { l: qsTr("RGB Color"), act: "@mode:RGB" },
            { l: qsTr("Grayscale"), act: "@mode:Gray" },
            SEP,
            { l: qsTr("8 Bits/Channel"), act: "@bits:8" },
            { l: qsTr("16 Bits/Channel"), act: "@bits:16" }
        ] },
        SEP,
        { l: qsTr("Adjustments"), w: 268, sub: [
            { l: qsTr("Brightness/Contrast…"), dialog: "Adjust", params: { adjust: "brightnessContrast" }, req: "doc" },
            { l: qsTr("Levels…"), k: "Ctrl L", dialog: "Adjust", params: { adjust: "levels" }, req: "doc" },
            { l: qsTr("Curves…"), k: "Ctrl M", dialog: "Adjust", params: { adjust: "curves" }, req: "doc" },
            { l: qsTr("Exposure…"), dialog: "Adjust", params: { adjust: "exposure" }, req: "doc" },
            SEP,
            { l: qsTr("Vibrance…"), dialog: "Adjust", params: { adjust: "vibrance" }, req: "doc" },
            { l: qsTr("Hue/Saturation…"), k: "Ctrl U", dialog: "Adjust", params: { adjust: "hueSaturation" }, req: "doc" },
            { l: qsTr("Color Balance…"), k: "Ctrl B", dialog: "Adjust", params: { adjust: "colorBalance" }, req: "doc" },
            { l: qsTr("Black & White…"), k: "Ctrl ⇧ Alt B", dialog: "Adjust", params: { adjust: "blackWhite" }, req: "doc" },
            { l: qsTr("Photo Filter…"), dialog: "Adjust", params: { adjust: "photoFilter" }, req: "doc" },
            { l: qsTr("Channel Mixer…"), dialog: "Adjust", params: { adjust: "channelMixer" }, req: "doc" },
            { l: qsTr("Color Lookup…"), dialog: "Adjust", params: { adjust: "colorLookup" }, req: "doc" },
            SEP,
            { l: qsTr("Invert"), k: "Ctrl I", act: "@invert" },
            { l: qsTr("Posterize…"), dialog: "Adjust", params: { adjust: "posterize" }, req: "doc" },
            { l: qsTr("Threshold…"), dialog: "Adjust", params: { adjust: "threshold" }, req: "doc" },
            { l: qsTr("Gradient Map…"), dialog: "Adjust", params: { adjust: "gradientMap" }, req: "doc" },
            { l: qsTr("Selective Color…"), dialog: "Adjust", params: { adjust: "selectiveColor" }, req: "doc" },
            SEP,
            { l: qsTr("Desaturate"), k: "Ctrl ⇧ U", act: "@step:Desaturate" },
            { l: qsTr("Equalize"), act: "@step:Equalize" }
        ] },
        SEP,
        { l: qsTr("Auto Tone"), k: "Ctrl ⇧ L", act: "@step:Auto Tone" },
        { l: qsTr("Auto Contrast"), k: "Ctrl ⇧ Alt L", act: "@step:Auto Contrast" },
        { l: qsTr("Auto Color"), k: "Ctrl ⇧ B", act: "@step:Auto Color" },
        SEP,
        { l: qsTr("Image Size…"), k: "Ctrl Alt I", dialog: "ImageSize", req: "doc" },
        { l: qsTr("Canvas Size…"), k: "Ctrl Alt C", dialog: "CanvasSize", req: "doc" },
        { l: qsTr("Image Rotation"), w: 280, sub: [
            { l: qsTr("180°"), act: "@crot:180" },
            { l: qsTr("90° theo chiều kim đồng hồ"), act: "@crot:90" },
            { l: qsTr("90° ngược chiều kim đồng hồ"), act: "@crot:270" },
            { l: qsTr("Tùy ý…"), act: "@noop" },
            SEP,
            { l: qsTr("Lật canvas ngang"), act: "@cflip:h" },
            { l: qsTr("Lật canvas dọc"), act: "@cflip:v" }
        ] },
        { l: qsTr("Crop"), act: "@cropSel", req: "sel" },
        { l: qsTr("Trim…"), act: "@noop" },
        { l: qsTr("Reveal All"), act: "@noop" },
        SEP,
        { l: qsTr("Nhân bản…"), act: "@dupDoc" }
    ] },
    { title: qsTr("&Layer"), w: 272, items: [
        { l: qsTr("Mới"), w: 250, sub: [
            { l: qsTr("Layer…"), k: "Ctrl ⇧ N", act: "@newLayer" },
            { l: qsTr("Nhóm…"), act: "@newGroup" },
            { l: qsTr("Chuyển thành Background"), act: "@noop", req: "!bgl" },
            SEP,
            { l: qsTr("Sao chép sang layer mới"), k: "Ctrl J", act: "@dupLayer" },
            { l: qsTr("Cắt sang layer mới"), k: "Ctrl ⇧ J", act: "@cutLayer", req: "sel" }
        ] },
        { l: qsTr("Nhân bản layer…"), act: "@dupLayer" },
        { l: qsTr("Đổi tên layer…"), act: "@rename" },
        { l: qsTr("Xóa"), w: 200, sub: [
            { l: qsTr("Layer"), act: "@delLayer" },
            { l: qsTr("Layer đang ẩn"), act: "@delHidden" }
        ] },
        SEP,
        { l: qsTr("Layer Style"), w: 220, sub: [
            { l: qsTr("Blending Options…"), dialog: "LayerStyle", params: { page: "blendingOptions" }, req: "doc" },
            SEP,
            { l: qsTr("Drop Shadow…"), dialog: "LayerStyle", params: { page: "dropShadow" }, req: "doc" },
            { l: qsTr("Stroke…"), dialog: "LayerStyle", params: { page: "stroke" }, req: "doc" },
            { l: qsTr("Color Overlay…"), dialog: "LayerStyle", params: { page: "colorOverlay" }, req: "doc" },
            { l: qsTr("Gradient Overlay…"), dialog: "LayerStyle", params: { page: "gradientOverlay" }, req: "doc" }
        ] },
        SEP,
        { l: qsTr("Fill Layer mới"), w: 180, sub: [
            { l: qsTr("Solid Color…"), act: "@fill:Solid Color" },
            { l: qsTr("Gradient…"), act: "@fill:Gradient" }
        ] },
        { l: qsTr("Adjustment Layer mới"), w: 230, sub: [
            { l: qsTr("Brightness/Contrast…"), act: "@adj:Brightness/Contrast" },
            { l: qsTr("Levels…"), act: "@adj:Levels" },
            { l: qsTr("Curves…"), act: "@adj:Curves" },
            { l: qsTr("Exposure…"), act: "@adj:Exposure" },
            { l: qsTr("Vibrance…"), act: "@adj:Vibrance" },
            { l: qsTr("Hue/Saturation…"), act: "@adj:Hue/Saturation" },
            { l: qsTr("Color Balance…"), act: "@adj:Color Balance" },
            { l: qsTr("Black & White…"), act: "@adj:Black & White" },
            { l: qsTr("Photo Filter…"), act: "@adj:Photo Filter" },
            { l: qsTr("Channel Mixer…"), act: "@adj:Channel Mixer" },
            { l: qsTr("Color Lookup…"), act: "@adj:Color Lookup" },
            { l: qsTr("Invert…"), act: "@adj:Invert" },
            { l: qsTr("Posterize…"), act: "@adj:Posterize" },
            { l: qsTr("Threshold…"), act: "@adj:Threshold" },
            { l: qsTr("Gradient Map…"), act: "@adj:Gradient Map" },
            { l: qsTr("Selective Color…"), act: "@adj:Selective Color" }
        ] },
        SEP,
        { l: qsTr("Layer Mask"), w: 220, sub: [
            { l: qsTr("Hiện tất cả"), act: "@mask:reveal", req: "layer" },
            { l: qsTr("Ẩn tất cả"), act: "@mask:hide", req: "layer" },
            { l: qsTr("Hiện vùng chọn"), act: "@mask:sel", req: "sel" },
            { l: qsTr("Ẩn vùng chọn"), act: "@mask:selInv", req: "sel" },
            SEP,
            { l: qsTr("Xóa"), act: "@mask:del", req: "mask" },
            { l: qsTr("Áp dụng"), act: "@mask:apply", req: "mask" },
            SEP,
            { l: qsTr("Tắt"), act: "@mask:off", req: "mask" },
            { l: qsTr("Bỏ liên kết"), act: "@noop", req: "mask" }
        ] },
        { l: qsTr("Tạo Clipping Mask"), k: "Ctrl Alt G", act: "@clip" },
        SEP,
        { l: qsTr("Smart Objects"), w: 240, sub: [
            { l: qsTr("Chuyển thành Smart Object"), act: "@toSO", req: "layer" },
            { l: qsTr("Sửa nội dung"), act: "@noop", req: "so" },
            { l: qsTr("Rasterize"), act: "@rast", req: "so" }
        ] },
        { l: qsTr("Rasterize"), w: 180, sub: [
            { l: qsTr("Layer chữ"), act: "@rast", req: "text" },
            { l: qsTr("Hình"), act: "@rast", req: "shape" },
            { l: qsTr("Smart Object"), act: "@rast", req: "so" },
            { l: qsTr("Layer"), act: "@rast", req: "layer" }
        ] },
        SEP,
        { l: qsTr("Nhóm layer"), k: "Ctrl G", act: "@groupLayer" },
        { l: qsTr("Bỏ nhóm"), k: "Ctrl ⇧ G", act: "@ungroup" },
        { l: qsTr("Ẩn layer"), k: "Ctrl ,", act: "@hideLayer" },
        SEP,
        { l: qsTr("Sắp xếp"), w: 270, sub: [
            { l: qsTr("Đưa lên trên cùng"), k: "Ctrl ⇧ ]", act: "@arr:top" },
            { l: qsTr("Đưa lên một bậc"), k: "Ctrl ]", act: "@arr:up" },
            { l: qsTr("Đưa xuống một bậc"), k: "Ctrl [", act: "@arr:down" },
            { l: qsTr("Đưa xuống dưới cùng"), k: "Ctrl ⇧ [", act: "@arr:bottom" }
        ] },
        { l: qsTr("Căn"), w: 220, sub: [
            { l: qsTr("Mép trên"), act: "@alignSel", req: "sel" },
            { l: qsTr("Giữa theo chiều dọc"), act: "@alignSel", req: "sel" },
            { l: qsTr("Mép dưới"), act: "@alignSel", req: "sel" },
            SEP,
            { l: qsTr("Mép trái"), act: "@alignSel", req: "sel" },
            { l: qsTr("Giữa theo chiều ngang"), act: "@alignSel", req: "sel" },
            { l: qsTr("Mép phải"), act: "@alignSel", req: "sel" }
        ] },
        { l: qsTr("Phân bố"), w: 220, sub: [
            { l: qsTr("Mép trên"), disabled: true },
            { l: qsTr("Giữa theo chiều dọc"), disabled: true },
            { l: qsTr("Mép dưới"), disabled: true },
            SEP,
            { l: qsTr("Mép trái"), disabled: true },
            { l: qsTr("Giữa theo chiều ngang"), disabled: true },
            { l: qsTr("Mép phải"), disabled: true }
        ] },
        SEP,
        { l: qsTr("Khóa layer…"), k: "Ctrl /", act: "@lockAll" },
        { l: qsTr("Liên kết layer"), disabled: true },
        SEP,
        { l: qsTr("Gộp xuống"), k: "Ctrl E", act: "@merge:down" },
        { l: qsTr("Gộp layer đang hiện"), k: "Ctrl ⇧ E", act: "@merge:visible" },
        { l: qsTr("Làm phẳng ảnh"), act: "@merge:flat" }
    ] },
    { title: qsTr("T&ype"), w: 240, items: [
        { l: qsTr("Panel"), w: 180, free: true, sub: [
            { l: qsTr("Character"), act: "@side:Character", free: true },
            { l: qsTr("Paragraph"), act: "@side:Paragraph", free: true }
        ] },
        { l: qsTr("Anti-Alias"), w: 160, sub: [
            { l: qsTr("None"), act: "@aa:None" },
            { l: qsTr("Sharp"), act: "@aa:Sharp" },
            { l: qsTr("Crisp"), act: "@aa:Crisp" },
            { l: qsTr("Strong"), act: "@aa:Strong" },
            { l: qsTr("Smooth"), act: "@aa:Smooth" }
        ] },
        SEP,
        { l: qsTr("Chuyển thành chữ đoạn"), act: "@noop", req: "text" },
        { l: qsTr("Rasterize Type Layer"), act: "@noop", req: "text" },
        SEP,
        { l: qsTr("Cỡ xem trước font"), w: 160, sub: [
            { l: qsTr("Không"), act: "@fp:0" },
            { l: qsTr("Nhỏ"), act: "@fp:1" },
            { l: qsTr("Vừa"), act: "@fp:2" },
            { l: qsTr("Lớn"), act: "@fp:3" },
            { l: qsTr("Rất lớn"), act: "@fp:4" }
        ] },
        { l: qsTr("Cập nhật mọi layer chữ"), act: "@noop" },
        { l: qsTr("Thay font thiếu…"), dialog: "MissingFonts" }
    ] },
    { title: qsTr("&Select"), w: 250, items: [
        { l: qsTr("Chọn tất cả"), k: "Ctrl A", act: "@selAll" },
        { l: qsTr("Bỏ chọn"), k: "Ctrl D", act: "@deselect" },
        { l: qsTr("Chọn lại"), k: "Ctrl ⇧ D", act: "@reselect" },
        { l: qsTr("Đảo vùng chọn"), k: "Ctrl ⇧ I", act: "@inverse" },
        SEP,
        { l: qsTr("Mọi layer"), k: "Ctrl Alt A", act: "@noop" },
        SEP,
        { l: qsTr("Color Range…"), act: "@noop" },
        { l: qsTr("Subject"), act: "@ants:subject" },
        { l: qsTr("Chân dung"), w: 196, sub: [
            { l: qsTr("Da"), act: "@ants:skin", req: "faces" },
            { l: qsTr("Mắt"), act: "@ants:eyes", req: "faces" },
            { l: qsTr("Môi"), act: "@ants:lips", req: "faces" },
            { l: qsTr("Răng"), act: "@ants:teeth", req: "faces" },
            SEP,
            { l: qsTr("Da, trừ mắt và môi"), act: "@ants:skin", req: "faces" }
        ] },
        SEP,
        { l: qsTr("Select and Mask…"), k: "Ctrl Alt R", space: "SelectMask", req: "doc" },
        { l: qsTr("Modify"), req: "sel", w: 200, sub: [
            { l: qsTr("Border…"), act: "@noop", req: "sel" },
            { l: qsTr("Smooth…"), act: "@noop", req: "sel" },
            { l: qsTr("Expand…"), act: "@noop", req: "sel" },
            { l: qsTr("Contract…"), act: "@noop", req: "sel" },
            { l: qsTr("Feather…"), k: "⇧ F6", act: "@noop", req: "sel" }
        ] },
        { l: qsTr("Grow"), act: "@noop", req: "sel" },
        { l: qsTr("Similar"), act: "@noop", req: "sel" },
        { l: qsTr("Transform Selection"), act: "@noop", req: "sel" },
        SEP,
        { l: qsTr("Sửa trong Quick Mask"), k: "Q", act: "@qm" },
        SEP,
        { l: qsTr("Nạp vùng chọn…"), act: "@loadSel" },
        { l: qsTr("Lưu vùng chọn…"), act: "@saveSel", req: "sel" }
    ] },
    { title: qsTr("Fil&ter"), w: 236, items: [
        { l: qsTr("Gaussian Blur"), k: "Ctrl Alt F", act: "@step:Gaussian Blur" },
        SEP,
        { l: qsTr("Neural Filters"), w: 200, sub: [
            { l: qsTr("Skin Smoothing…"), act: "@win:Retouch" }
        ] },
        { l: qsTr("Liquify…"), k: "Ctrl ⇧ X", space: "Liquify", req: "doc" },
        SEP,
        { l: qsTr("Blur"), w: 196, sub: [
            { l: qsTr("Average"), act: "@step:Average" },
            { l: qsTr("Blur"), act: "@step:Blur" },
            { l: qsTr("Blur More"), act: "@step:Blur More" },
            { l: qsTr("Box Blur…"), dialog: "Filter", params: { filter: "boxBlur" }, req: "doc" },
            { l: qsTr("Gaussian Blur…"), dialog: "Filter", params: { filter: "gaussianBlur" }, req: "doc" },
            { l: qsTr("Motion Blur…"), dialog: "Filter", params: { filter: "motionBlur" }, req: "doc" },
            { l: qsTr("Surface Blur…"), dialog: "Filter", params: { filter: "surfaceBlur" }, req: "doc" }
        ] },
        { l: qsTr("Noise"), w: 200, sub: [
            { l: qsTr("Add Noise…"), dialog: "Filter", params: { filter: "addNoise" }, req: "doc" },
            { l: qsTr("Despeckle"), act: "@step:Despeckle" },
            { l: qsTr("Dust & Scratches…"), dialog: "Filter", params: { filter: "dustScratches" }, req: "doc" },
            { l: qsTr("Median…"), dialog: "Filter", params: { filter: "median" }, req: "doc" },
            { l: qsTr("Reduce Noise…"), dialog: "Filter", params: { filter: "reduceNoise" }, req: "doc" }
        ] },
        { l: qsTr("Sharpen"), w: 200, sub: [
            { l: qsTr("Sharpen"), act: "@step:Sharpen" },
            { l: qsTr("Sharpen More"), act: "@step:Sharpen More" },
            { l: qsTr("Sharpen Edges"), act: "@step:Sharpen Edges" },
            { l: qsTr("Unsharp Mask…"), dialog: "Filter", params: { filter: "unsharpMask" }, req: "doc" }
        ] },
        { l: qsTr("Other"), w: 180, sub: [
            { l: qsTr("Custom…"), dialog: "Filter", params: { filter: "custom" }, req: "doc" },
            { l: qsTr("High Pass…"), dialog: "Filter", params: { filter: "highPass" }, req: "doc" },
            { l: qsTr("Maximum…"), dialog: "Filter", params: { filter: "maximum" }, req: "doc" },
            { l: qsTr("Minimum…"), dialog: "Filter", params: { filter: "minimum" }, req: "doc" },
            { l: qsTr("Offset…"), dialog: "Filter", params: { filter: "offset" }, req: "doc" }
        ] }
    ] },
    { title: qsTr("&View"), w: 250, items: [
        { l: qsTr("Phóng to"), k: "Ctrl +", act: "@zoomIn" },
        { l: qsTr("Thu nhỏ"), k: "Ctrl −", act: "@zoomOut" },
        { l: qsTr("Vừa màn hình"), k: "Ctrl 0", act: "@zoomFit" },
        { l: qsTr("100%"), k: "Ctrl 1", act: "@zoom100" },
        SEP,
        { l: qsTr("Lưới điểm ảnh"), act: "@tog:pixGrid" },
        { l: qsTr("Chế độ màn hình"), w: 230, sub: [
            { l: qsTr("Chế độ chuẩn"), act: "@scr:0" },
            { l: qsTr("Toàn màn hình có menu"), act: "@scr:1" },
            { l: qsTr("Toàn màn hình"), act: "@scr:2" }
        ] },
        SEP,
        { l: qsTr("Extras"), k: "Ctrl H", act: "@tog:extras" },
        { l: qsTr("Hiện"), w: 200, sub: [
            { l: qsTr("Viền layer"), act: "@tog:edges" },
            { l: qsTr("Viền vùng chọn"), k: "Ctrl H", act: "@tog:selEdges" },
            { l: qsTr("Xem trước cọ"), act: "@tog:brushPrev" },
            SEP,
            { l: qsTr("Guide"), k: "Ctrl ;", act: "@tog:guides" },
            { l: qsTr("Lưới"), k: "Ctrl '", act: "@tog:grid" }
        ] },
        SEP,
        { l: qsTr("Thước"), k: "Ctrl R", act: "@tog:rulers" },
        SEP,
        { l: qsTr("Bắt dính"), k: "Ctrl ⇧ ;", act: "@tog:snap" },
        { l: qsTr("Bắt dính vào"), w: 200, sub: [
            { l: qsTr("Guide"), act: "@snapTo:guides" },
            { l: qsTr("Lưới"), act: "@snapTo:grid" },
            { l: qsTr("Layer"), act: "@snapTo:layers" },
            { l: qsTr("Biên tài liệu"), act: "@snapTo:bounds" },
            SEP,
            { l: qsTr("Tất cả"), act: "@snapTo:all" },
            { l: qsTr("Không"), act: "@snapTo:none" }
        ] },
        SEP,
        { l: qsTr("Khóa guide"), k: "Ctrl Alt ;", act: "@tog:lockGuides" },
        { l: qsTr("Xóa guide"), act: "@clearGuides" },
        { l: qsTr("Guide mới…"), act: "@newGuide" }
    ] },
    { title: qsTr("&Window"), w: 210, items: [
        { l: qsTr("Adjustments"), act: "@win:Adjustments", free: true },
        { l: qsTr("Brush Settings"), k: "F5", act: "@win:Brush Settings", free: true },
        { l: qsTr("Brushes"), act: "@win:Brushes", free: true },
        { l: qsTr("Channels"), act: "@win:Channels", free: true },
        { l: qsTr("Character"), act: "@win:Character", free: true },
        { l: qsTr("Color"), k: "F6", act: "@win:Color", free: true },
        { l: qsTr("Gradients"), act: "@win:Gradients", free: true },
        { l: qsTr("Histogram"), act: "@win:Histogram", free: true },
        { l: qsTr("History"), act: "@win:History", free: true },
        { l: qsTr("Info"), k: "F8", act: "@win:Info", free: true },
        { l: qsTr("Layers"), k: "F7", act: "@win:Layers", free: true },
        { l: qsTr("Navigator"), act: "@win:Navigator", free: true },
        { l: qsTr("Paragraph"), act: "@win:Paragraph", free: true },
        { l: qsTr("Properties"), act: "@win:Properties", free: true },
        { l: qsTr("Retouch"), act: "@win:Retouch", free: true },
        { l: qsTr("Swatches"), act: "@win:Swatches", free: true },
        SEP,
        { l: qsTr("Thanh tùy chọn"), act: "@tog:showOpts", free: true },
        { l: qsTr("Thanh công cụ"), act: "@tog:showTools", free: true }
    ] },
    { title: qsTr("&Help"), w: 236, items: [
        { l: qsTr("Hướng dẫn photo-beauty"), k: "F1", act: "@noop", free: true },
        { l: qsTr("Phím tắt…"), dialog: "Shortcuts", free: true },
        SEP,
        { l: qsTr("Thông tin hệ thống…"), act: "@noop", free: true },
        { l: qsTr("Mở thư mục nhật ký"), act: "@noop", free: true },
        SEP,
        { l: qsTr("Kiểm tra cập nhật…"), act: "@noop", free: true },
        { l: qsTr("Gửi báo lỗi…"), act: "@noop", free: true },
        SEP,
        { l: qsTr("Giấy phép…"), dialog: "Settings", params: { page: "about" }, free: true },
        { l: qsTr("Về photo-beauty"), act: "@noop", free: true }
    ] }
]

// "@merge:down" → "merge", "down"; "" for none.
function name(act) { return act ? String(act).replace(/^@/, "").split(":")[0] : "" }
function arg(act) {
    const a = String(act || ""), at = a.indexOf(":")
    return at < 0 ? "" : a.slice(at + 1)
}
// The Shortcut sequences of keys as a menu shows them: "Ctrl ⇧ Z" → "Ctrl+Shift+Z"; "Ctrl +" also takes Ctrl =, the
// key + is on.
function sequences(k) {
    const s = String(k).replace(/⇧/g, "Shift").replace(/−/g, "-").split(" ").join("+")
    return s === "Ctrl++" ? [s, "Ctrl+="] : [s]
}
// Every item with keys, in menu order, and the menus leading to it: [{ item, path: ["Edit", "Hoàn tác"], sequences }].
// A sequence goes to its first item only (Extras, not View › Hiện › Viền vùng chọn, both Ctrl H), as two shortcuts on
// one key would run neither.
function keyed() {
    const out = [], seen = {}
    const walk = (items, path) => {
        for (const it of items) {
            if (Array.isArray(it.sub))
                walk(it.sub, path.concat([it.l]))
            if (!it.k)
                continue
            const seqs = sequences(it.k).filter((s) => !seen[s])
            seqs.forEach((s) => { seen[s] = true })
            if (seqs.length)
                out.push({ item: it, path: path.concat([it.l]), sequences: seqs })
        }
    }
    for (const m of MENUS)
        walk(m.items, [m.title.replace("&", "")])
    return out
}
