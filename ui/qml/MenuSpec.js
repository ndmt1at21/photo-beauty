.pragma library
.import "Mock.js" as Mock
// The menu bar (docs/ui-tasks.md, U4): MENUS and SUBS of build/proto/Main.dc.html (copied by a script, not retyped),
// their actions, requirements and keys, as PopupMenu items. One source for the menus, the keys Main registers and the
// Phím tắt dialog (U20); KEYS, the keys of Photoshop no menu item has (U31). An item:
//   l the label; k its keys as shown ("Ctrl ⇧ Z", Main registers them, see sequences()); disabled always greyed;
//   act "@name[:arg]", the prototype's exec(): Docs.exec() runs it, but for Main's own (openFile, quit, close,
//       closeAll, clearRecent, rename, noop);
//   dialog "<Name>" (+ params): Main.openDialog() opens <Name>Dialog.qml; space "<Name>" (+ params): Main.openSpace()
//       opens <Name>Space.qml; either greyed while that file is not in the module (Main.can());
//   req what it needs, a key of Doc.can() ("doc", "sel", "!bgl"), else its action's REQ (Mock.js);
//   sub a submenu's items ("recent": the recent files, Docs.recent), w its width;
//   free: runs with no document open too (Start.dc.html's menus: Window, Help, Tạo mới, Mở, Thoát, Thiết lập…);
//       anything else needs one;
//   fade: the label names the step Hoàn tác would take back ("Fade Brush…");
//   en: its English name in Photoshop, for Phím tắt (else ShortcutsDialog's table);
//   uses: the command of run() it takes on a real document, when not its act's (decision 4: Doc.supports()); "" none.
// Two items follow the layer picked, as Photoshop's: Layer › Mới's Chuyển thành Background is Layer từ Background… while
// the Background is picked, Liên kết layer is Bỏ liên kết layer while the layers picked are linked together (getters on
// Main's Docs.can(), setDocCan()).
// Prototype artboards become dialogs: FillStroke → Fill, Stroke; ImageSize → ImageSize, CanvasSize; ColorSettings →
// ColorSettings, AssignProfile, ConvertProfile; Filters → Filter { filter }; LayerStyle(Blend) → LayerStyle { page };
// Notices → MissingFonts (Thoát asks SaveChanges through Main); @dlg:<adjustment> → Adjust { adjust }. A param's value
// is the item's English name in camelCase ("Gaussian Blur…" → "gaussianBlur", "Hue/Saturation" → "hueSaturation").

var SEP = { t: "sep" }

// Main's Docs.can(), for the items whose label and action follow the layer picked; none: as with no document.
var docCan = null
function setDocCan(f) { docCan = f }
function has(name) { return !!docCan && !!docCan(name) }

var MENUS = [
    { title: qsTr("&File"), w: 250, items: [
        { l: qsTr("Tạo mới…"), k: "Ctrl N", dialog: "NewDoc", free: true },
        { l: qsTr("Mở…"), k: "Ctrl O", act: "@openFile", free: true },
        { l: qsTr("Mở gần đây"), sub: "recent", w: 260, free: true },
        SEP,
        { l: qsTr("Đóng"), k: "Ctrl W", act: "@close" },
        { l: qsTr("Đóng tất cả"), k: "Ctrl Alt W", act: "@closeAll" },
        { l: qsTr("Đóng các tài liệu khác"), k: "Ctrl Alt P", act: "@closeOthers", req: "doc", uses: "", en: "Close Others" },
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
        { l: qsTr("Fade…"), k: "Ctrl ⇧ F", dialog: "Fade", req: "undo", fade: true },
        SEP,
        { l: qsTr("Cắt"), k: "Ctrl X", act: "@noop", req: "sel" },
        { l: qsTr("Sao chép"), k: "Ctrl C", act: "@noop", req: "sel" },
        { l: qsTr("Sao chép hợp nhất"), k: "Ctrl ⇧ C", act: "@noop", req: "sel" },
        { l: qsTr("Dán"), k: "Ctrl V", act: "@paste" },
        { l: qsTr("Dán tại chỗ"), k: "Ctrl ⇧ V", act: "@paste" },
        { l: qsTr("Xóa", "Edit › Clear"), k: "Del", act: "@noop", req: "sel" },
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
        { l: qsTr("Define Brush Preset…"), dialog: "DefineBrush", req: "doc" },
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
            { l: qsTr("Tùy ý…"), dialog: "RotateCanvas", req: "doc" },
            SEP,
            { l: qsTr("Lật canvas ngang"), act: "@cflip:h" },
            { l: qsTr("Lật canvas dọc"), act: "@cflip:v" }
        ] },
        { l: qsTr("Crop"), act: "@cropSel", req: "sel" },
        { l: qsTr("Trim…"), dialog: "Trim", req: "doc" },
        { l: qsTr("Reveal All"), act: "@noop" },
        SEP,
        { l: qsTr("Nhân bản…"), act: "@dupDoc" }
    ] },
    { title: qsTr("&Layer"), w: 272, items: [
        { l: qsTr("Mới"), w: 250, sub: [
            { l: qsTr("Layer…"), k: "Ctrl ⇧ N", act: "@newLayer" },
            { l: qsTr("Nhóm…"), act: "@newGroup" },
            { l: qsTr("Nhóm từ layer…"), act: "@groupLayer", en: "Group from Layers" },
            {
                get l() { return has("bgl") ? qsTr("Layer từ Background…") : qsTr("Chuyển thành Background") },
                get act() { return has("bgl") ? "@bgToLayer" : "@layerToBg" },
                get en() { return has("bgl") ? "Layer from Background" : "Background from Layer" }
            },
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
            { l: qsTr("Gradient Overlay…"), dialog: "LayerStyle", params: { page: "gradientOverlay" }, req: "doc" },
            SEP,
            { l: qsTr("Xóa Layer Style"), act: "@clearStyle", en: "Clear Layer Style" }
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
            { l: qsTr("Đưa xuống dưới cùng"), k: "Ctrl ⇧ [", act: "@arr:bottom" },
            { l: qsTr("Đảo thứ tự"), act: "@reverse", en: "Reverse" }
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
        { l: qsTr("Phân bố"), w: 220, sub: [  // three layers picked or more
            { l: qsTr("Mép trên"), act: "@distribute:top" },
            { l: qsTr("Giữa theo chiều dọc"), act: "@distribute:vcenter" },
            { l: qsTr("Mép dưới"), act: "@distribute:bottom" },
            SEP,
            { l: qsTr("Mép trái"), act: "@distribute:left" },
            { l: qsTr("Giữa theo chiều ngang"), act: "@distribute:hcenter" },
            { l: qsTr("Mép phải"), act: "@distribute:right" }
        ] },
        SEP,
        { l: qsTr("Khóa layer…"), k: "Ctrl /", act: "@lockAll" },
        { get l() { return has("linked") ? qsTr("Bỏ liên kết layer") : qsTr("Liên kết layer") }, act: "@link" },
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
        { l: qsTr("Bỏ chọn layer"), act: "@deselectLayers", req: "layer", uses: "selectLayers", en: "Deselect Layers" },
        SEP,
        { l: qsTr("Color Range…"), dialog: "ColorRange", req: "doc" },
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
            { l: qsTr("Border…"), dialog: "ModifySelection", params: { kind: "border" }, req: "sel" },
            { l: qsTr("Smooth…"), dialog: "ModifySelection", params: { kind: "smooth" }, req: "sel" },
            { l: qsTr("Expand…"), dialog: "ModifySelection", params: { kind: "expand" }, req: "sel" },
            { l: qsTr("Contract…"), dialog: "ModifySelection", params: { kind: "contract" }, req: "sel" },
            { l: qsTr("Feather…"), k: "⇧ F6", dialog: "ModifySelection", params: { kind: "feather" }, req: "sel" }
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
            { l: qsTr("Layer", "View › Snap To › Layers"), act: "@snapTo:layers" },
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
        { l: qsTr("Thông tin hệ thống…"), dialog: "SystemInfo", free: true },
        { l: qsTr("Mở thư mục nhật ký"), act: "@noop", free: true },
        SEP,
        { l: qsTr("Kiểm tra cập nhật…"), dialog: "Update", free: true },
        { l: qsTr("Gửi báo lỗi…"), dialog: "BugReport", free: true },
        SEP,
        { l: qsTr("Giấy phép…"), dialog: "Settings", params: { page: "about" }, free: true },
        { l: qsTr("Về photo-beauty"), dialog: "About", free: true }
    ] }
]
// File with no document open (Start.dc.html's MENUS): eight rows of the one above, the same items.
MENUS[0].start = (f => [f[0], f[1], SEP, f[4], f[8], f[14].sub[1], SEP, f[16]])(MENUS[0].items)

// "@merge:down" → "merge", "down"; "" for none.
function name(act) { return act ? String(act).replace(/^@/, "").split(":")[0] : "" }
function arg(act) {
    const a = String(act || ""), at = a.indexOf(":")
    return at < 0 ? "" : a.slice(at + 1)
}
// The keys of a row of Phím tắt, each as a menu shows one: several after the modifiers are each a key with them (the
// four arrows: "⇧ ← → ↑ ↓" → "⇧ ←", "⇧ →", "⇧ ↑", "⇧ ↓"); else the one ("Ctrl ⇧ Z").
function keysOf(k) {
    const t = String(k).split(" "), mod = x => x === "Ctrl" || x === "Alt" || x === "⇧"
    const mods = t.filter(mod), keys = t.filter(x => !mod(x))
    return keys.length > 1 ? keys.map(x => mods.concat([x]).join(" ")) : [String(k)]
}
// The Shortcut sequences of keys as a menu shows them: "Ctrl ⇧ Z" → "Ctrl+Shift+Z"; + also as =, the key + is on
// ("Ctrl +" takes Ctrl =; ⇧ + comes as Shift = on Windows); arrows by their names; a row of keys (keysOf()), each's.
var ARROWS = { "←": "Left", "→": "Right", "↑": "Up", "↓": "Down" }
function sequences(k) {
    const ks = keysOf(k)
    if (ks.length > 1)
        return ks.reduce((o, x) => o.concat(sequences(x)), [])
    const s = String(k).replace(/⇧/g, "Shift").replace(/−/g, "-").split(" ").map(x => ARROWS[x] || x).join("+")
    return /.\+\+$/.test(s) ? [s, s.slice(0, -1) + "="] : [s]
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

// ---- keys no menu item has (U31) --------------------------------------------------------------------------------------
// Photoshop 2025's, as on Windows (the menus' Ctrl, Alt stand for ⌘, ⌥ on macOS): Phím tắt's tab Khác lists them by
// group, Main registers them (bound()). A row: { l, en (Photoshop's English name), k (as shown; a row of arrows, a key
// each), act (as an item's, run by Main.act(): Docs.exec()'s, or Main's own: layerSel, blend, fillWith, nudge, maskView,
// nextDoc, brushPreset), dialog, req and uses as an item's, ctx (when it is on but for its req: "brush", a tool with a
// brush, ToolSpec.PAINT_T; "paint", a tool whose Mode has the blend modes, ToolSpec.modeCell(); "nudge", Move, or a
// selection tool while there is a selection) }; a click in Layers, the panel's to run: { l, en, k (its modifiers),
// click (where) }.
// The blend modes' letters, with Alt ⇧ (Behind, Clear: a painting tool's), by Mock.BLENDS' names.
var BLEND_KEYS = [["normal", "N"], ["dissolve", "I"], ["behind", "Q"], ["clear", "R"], ["darken", "K"], ["multiply", "M"], ["colorBurn", "B"],
    ["linearBurn", "A"], ["lighten", "G"], ["screen", "S"], ["colorDodge", "D"], ["linearDodge", "W"], ["overlay", "O"], ["softLight", "F"],
    ["hardLight", "H"], ["vividLight", "V"], ["linearLight", "J"], ["pinLight", "Z"], ["hardMix", "L"], ["difference", "E"], ["exclusion", "X"],
    ["hue", "U"], ["saturation", "T"], ["color", "C"], ["luminosity", "Y"]]
// A blend mode's label as Layers and the options bars show it (Mock.BLEND_LABELS; Behind, Clear).
function blendLabel(name) { return { behind: "Behind", clear: "Clear" }[name] || Mock.BLEND_LABELS[Mock.BLENDS.indexOf(name)] || "" }
var KEYS = [
    [qsTr("Layer", "Phím tắt › Khác"), [
        { l: qsTr("Chọn layer dưới"), en: "Select Next Layer Down", k: "Alt [", act: "@layerSel:down", req: "below", uses: "selectLayers" },
        { l: qsTr("Chọn layer trên"), en: "Select Next Layer Up", k: "Alt ]", act: "@layerSel:up", req: "layer", uses: "selectLayers" },
        { l: qsTr("Chọn layer dưới cùng"), en: "Select Bottom Layer", k: "Alt ,", act: "@layerSel:bottom", req: "multi", uses: "selectLayers" },
        { l: qsTr("Chọn layer trên cùng"), en: "Select Top Layer", k: "Alt .", act: "@layerSel:top", req: "multi", uses: "selectLayers" },
        { l: qsTr("Chọn thêm layer dưới"), en: "Add Next Layer Down to Selection", k: "⇧ Alt [", act: "@layerSel:addDown", req: "below", uses: "selectLayers" },
        { l: qsTr("Chọn thêm layer trên"), en: "Add Next Layer Up to Selection", k: "⇧ Alt ]", act: "@layerSel:addUp", req: "layer", uses: "selectLayers" },
        { l: qsTr("Gộp layer đang hiện vào layer mới"), en: "Stamp Visible", k: "Ctrl ⇧ Alt E", act: "@stampVisible" },
        { l: qsTr("Gộp layer đang chọn vào layer mới"), en: "Stamp Selected Layers", k: "Ctrl Alt E", act: "@stampLayers" },
        { l: qsTr("Layer mới, không hỏi"), en: "New Layer Without Dialog", k: "Ctrl ⇧ Alt N", act: "@newLayer" },
        { l: qsTr("Xem mask thành lớp phủ đỏ"), en: "Toggle Rubylith Mode", k: "\\", act: "@maskView:overlay", req: "mask", uses: "" },
        { l: qsTr("Nạp độ trong suốt làm vùng chọn"), en: "Load Layer Transparency as Selection", k: "Ctrl", click: qsTr("nhấp thumbnail") },
        { l: qsTr("Cộng độ trong suốt vào vùng chọn"), en: "Add Transparency to Selection", k: "Ctrl ⇧", click: qsTr("nhấp thumbnail") },
        { l: qsTr("Trừ độ trong suốt khỏi vùng chọn"), en: "Subtract Transparency from Selection", k: "Ctrl Alt", click: qsTr("nhấp thumbnail") },
        { l: qsTr("Giao độ trong suốt với vùng chọn"), en: "Intersect Transparency with Selection", k: "Ctrl ⇧ Alt", click: qsTr("nhấp thumbnail") },
        { l: qsTr("Nạp mask làm vùng chọn"), en: "Load Layer Mask as Selection", k: "Ctrl", click: qsTr("nhấp mask") },
        { l: qsTr("Xem riêng mask"), en: "View Layer Mask", k: "Alt", click: qsTr("nhấp mask") },
        { l: qsTr("Tắt, bật mask"), en: "Disable/Enable Layer Mask", k: "⇧", click: qsTr("nhấp mask") },
        { l: qsTr("Tạo, bỏ Clipping Mask"), en: "Create/Release Clipping Mask", k: "Alt", click: qsTr("nhấp giữa hai layer") }
    ]],
    [qsTr("Tô màu"), [
        { l: qsTr("Tô màu trước"), en: "Fill with Foreground Color", k: "Alt Backspace", act: "@fillWith:foreground", req: "layer" },
        { l: qsTr("Tô màu sau"), en: "Fill with Background Color", k: "Ctrl Backspace", act: "@fillWith:background", req: "layer" },
        { l: qsTr("Tô màu trước, giữ chỗ trong suốt"), en: "Fill with Foreground Color, Preserve Transparency", k: "⇧ Alt Backspace", act: "@fillWith:foreground,keep", req: "layer" },
        { l: qsTr("Tô màu sau, giữ chỗ trong suốt"), en: "Fill with Background Color, Preserve Transparency", k: "Ctrl ⇧ Backspace", act: "@fillWith:background,keep", req: "layer" },
        { l: qsTr("Mở hộp thoại Fill"), en: "Display Fill Dialog Box", k: "⇧ Backspace", dialog: "Fill", req: "doc" }
    ]],
    [qsTr("Dời, biến đổi"), [
        { l: qsTr("Dời 1 px"), en: "Nudge 1 Pixel", k: "← → ↑ ↓", act: "@nudge", req: "doc", ctx: "nudge" },
        { l: qsTr("Dời 10 px"), en: "Nudge 10 Pixels", k: "⇧ ← → ↑ ↓", act: "@nudge", req: "doc", ctx: "nudge" },
        { l: qsTr("Nhân bản rồi Transform lại"), en: "Duplicate and Transform Again", k: "Ctrl ⇧ Alt T", act: "@transformAgain" }
    ]],
    [qsTr("Tài liệu"), [
        { l: qsTr("Tài liệu kế"), en: "Next Document", k: "Ctrl Tab", act: "@nextDoc:1", req: "doc", uses: "" },
        { l: qsTr("Tài liệu trước"), en: "Previous Document", k: "Ctrl ⇧ Tab", act: "@nextDoc:-1", req: "doc", uses: "" }
    ]],
    [qsTr("Cọ"), [
        { l: qsTr("Preset cọ trước"), en: "Previous Brush", k: ",", act: "@brushPreset:-1", req: "doc", uses: "", ctx: "brush" },
        { l: qsTr("Preset cọ sau"), en: "Next Brush", k: ".", act: "@brushPreset:1", req: "doc", uses: "", ctx: "brush" },
        { l: qsTr("Preset cọ đầu"), en: "First Brush", k: "⇧ ,", act: "@brushPreset:first", req: "doc", uses: "", ctx: "brush" },
        { l: qsTr("Preset cọ cuối"), en: "Last Brush", k: "⇧ .", act: "@brushPreset:last", req: "doc", uses: "", ctx: "brush" }
    ]],
    [qsTr("Blend mode"), [
        { l: qsTr("Blend mode kế"), en: "Next Blend Mode", k: "⇧ +", act: "@blend:+1", req: "doc", uses: "setProps" },
        { l: qsTr("Blend mode trước"), en: "Previous Blend Mode", k: "⇧ −", act: "@blend:-1", req: "doc", uses: "setProps" }
    ].concat(BLEND_KEYS.map(b => ({ l: blendLabel(b[0]), en: "", k: "⇧ Alt " + b[1], act: "@blend:" + b[0], req: "doc", uses: "setProps",
                                     ctx: b[0] === "behind" || b[0] === "clear" ? "paint" : "" })))]
]
// KEYS' keys as Main registers them: [{ item, sequences, plain }], a row of arrows an entry a key, its act given the key
// ("@nudge:Shift+Left"); plain: a key without Ctrl or Alt, off while a control has the focus (one takes those).
function bound() {
    const out = []
    for (const g of KEYS)
        for (const r of g[1]) {
            if (r.click || !r.k)
                continue
            const ks = keysOf(r.k)
            for (const k of ks) {
                const seqs = sequences(k)
                out.push({ item: ks.length > 1 ? Object.assign({}, r, { act: r.act + ":" + seqs[0] }) : r, sequences: seqs, plain: !seqs.some(s => /(^|\+)(Ctrl|Alt)\+/.test(s)) })
            }
        }
    return out
}
// What the layer keys pick (Alt [ ] , . and, with Shift, add): the ids picked after it, the active one last; null when
// there is no such layer. rows: Layers' (Doc.rows, top first), selected: Doc.selected.
function layerSel(rows, selected, how) {
    const ids = rows.map(l => l.id), at = selected.length ? ids.indexOf(selected[selected.length - 1]) : -1
    const to = how === "top" ? 0 : how === "bottom" ? ids.length - 1 : at < 0 ? -1 : /down/i.test(how) ? at + 1 : at - 1
    if (to < 0 || to >= ids.length)
        return null
    return /^add/.test(how) ? selected.filter(x => x !== ids[to]).concat([ids[to]]) : [ids[to]]
}
// The blend mode a key gives what has the modes list now at cur (a layer: Mock.BLENDS' names; a tool's Mode: labels):
// arg "+1", "-1" the next or the one before, round; else a name of BLEND_KEYS, as a label when labels; "" for one
// the list lacks.
function blendAfter(list, cur, arg, labels) {
    if (arg === "+1" || arg === "-1")
        return list[(Math.max(0, list.indexOf(cur)) + (arg === "+1" ? 1 : list.length - 1)) % list.length]
    const v = labels ? blendLabel(arg) : arg
    return list.indexOf(v) >= 0 ? v : ""
}
// The brush preset , . and with Shift pick among n, the one in use at (-1: none): how "-1", "1", "first", "last"; -1
// when it stays.
function presetAfter(n, at, how) {
    const to = how === "first" ? 0 : how === "last" ? n - 1 : at < 0 ? (+how > 0 ? 0 : n - 1) : Math.max(0, Math.min(n - 1, at + (+how)))
    return n > 0 && to !== at ? to : -1
}
// Layers' view of a mask (Tools.values "layers:maskView", not History's; the canvas is a later task's): { doc, layer, view
// "alone" (Alt click on its thumbnail) or "overlay" (\, red over the image) }, or {} for none; the one after a toggle.
function maskView(cur, doc, layer, view) {
    return cur && cur.doc === doc && cur.layer === layer && cur.view === view ? {} : { doc: doc, layer: layer, view: view }
}
