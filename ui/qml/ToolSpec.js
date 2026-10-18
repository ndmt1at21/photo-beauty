.pragma library
.import "Fmt.js" as Fmt
// The toolbar and the options bars (docs/ui-tasks.md, U5): the prototype's tables, copied below by a script (not
// retyped: build/shots/u5/tools/gen_toolspec.js of the main checkout), and what the toolbar, the options bar, the brush
// picker, the status bar and Phím tắt (U20) read from them. Tools.values keeps a bar's cells by "<bar>:<index of the
// item in its BARS row>" (but those another panel shows too: key()) and the brush of a painting bar by "<bar>:size" (px),
// ":hard" (%), ":spacing" (%), ":tip" (an index of Presets.TIPS); a key not set has its item's default:
// Tools.value(key, ToolSpec.def(bar, i)), brush(bar).

// ---- copied by gen_toolspec.js from build/proto/Main.dc.html ----------------------------------------------------------

// Toolbar: groups of [id, [[tool id, name, key, icon, options bar]…]]; 0 is a separator.
var GROUPS = [
    ["move", [["move", qsTr("Move"), "V", "open_with", "move"]]],
    0,
    ["marquee", [["rect", qsTr("Rectangular Marquee"), "M", "select", "marquee"], ["ellip", qsTr("Elliptical Marquee"), "M", "mq_ellipse", "marquee"]]],
    ["lasso", [["lasso", qsTr("Lasso"), "L", "lasso_select", "lasso"], ["plasso", qsTr("Polygonal Lasso"), "L", "lasso_poly", "lasso"]]],
    ["wand", [["wand", qsTr("Magic Wand"), "W", "auto_fix_normal", "wand"], ["selbrush", qsTr("Selection Brush"), "W", "sel_brush", "selbrush"]]],
    ["crop", [["crop", qsTr("Crop"), "C", "crop", "crop"], ["pcrop", qsTr("Perspective Crop"), "C", "crop_persp", "pcrop"]]],
    ["eye", [["eyedrop", qsTr("Eyedropper"), "I", "colorize", "eyedrop"]]],
    0,
    ["heal", [["spot", qsTr("Spot Healing Brush"), "J", "spot_heal", "spot"], ["heal", qsTr("Healing Brush"), "J", "healing", "heal"], ["patch", qsTr("Patch"), "J", "patch", "patch"]]],
    ["brush", [["brush", qsTr("Brush"), "B", "brush", "brush"], ["pencil", qsTr("Pencil"), "B", "edit", "pencil"]]],
    ["stamp", [["clone", qsTr("Clone Stamp"), "S", "approval", "clone"]]],
    ["eraser", [["eraser", qsTr("Eraser"), "E", "ink_eraser", "eraser"]]],
    ["grad", [["gradient", qsTr("Gradient"), "G", "gradient", "gradient"], ["bucket", qsTr("Paint Bucket"), "G", "format_color_fill", "bucket"]]],
    ["dodge", [["dodge", qsTr("Dodge"), "O", "dodge", "dodge"], ["burn", qsTr("Burn"), "O", "local_fire_department", "dodge"], ["sponge", qsTr("Sponge"), "O", "sponge", "sponge"]]],
    0,
    ["type", [["type", qsTr("Horizontal Type"), "T", "title", "type"]]],
    ["shape", [["rectS", qsTr("Rectangle"), "U", "rectangle", "rectS"], ["ellipS", qsTr("Ellipse"), "U", "circle", "ellipS"], ["tri", qsTr("Triangle"), "U", "change_history", "rectS"], ["poly", qsTr("Polygon"), "U", "pentagon", "polygon"], ["line", qsTr("Line"), "U", "pen_size_2", "line"]]],
    0,
    ["hand", [["hand", qsTr("Hand"), "H", "pan_tool", "hand"], ["rot", qsTr("Rotate View"), "R", "rotate_90_degrees_ccw", "rot"]]],
    ["zoom", [["zoom", qsTr("Zoom"), "Z", "zoom_in", "zoom"]]]
]
// The painting tools: their bar has a brush (BP).
var PAINT_T = { spot: 1, heal: 1, brush: 1, pencil: 1, clone: 1, eraser: 1, dodge: 1, burn: 1, sponge: 1, selbrush: 1 }
// Options bar per tool. Item DSL: 'ic' tool icon, '|' divider, 'L~text', 'D~value~width~list~name',
// 'F~label~value~width~name' (FC adds a chevron), 'T~icon~tip~on~action', 'B~icon~tip~action', 'C~label~on',
// 'BT~text~action', 'BI~icon~text~action', 'M' selection modes, 'S~a|b~index~name' segmented text,
// 'S2~icon|icon~index~name', 'GT' gradient types, 'BP~size~hard', 'SW~css~name', 'G' gradient preset,
// 'IF~icon~value~width~name', 'AN' reference point, 'SP' spacer. A leading '!' dims the item. Actions: '=Board' link,
// '@action'.
var BS = "B~instant_mix~" + qsTr("Brush Settings (F5)") + "~@side:Brush Settings"
var SMOOTH = "B~settings~" + qsTr("Pulled String Mode, Stroke Catch-up, Catch-up on Stroke End, Adjust for Zoom")
var BARS = {
    move: ["ic", "|", "C~" + qsTr("Auto-Select") + "~1", "D~Layer~84~layerGroup~" + qsTr("Auto-Select"), "C~" + qsTr("Show Transform Controls") + "~0", "|", "B~align_horizontal_left~" + qsTr("Căn trái"), "B~align_horizontal_center~" + qsTr("Căn giữa ngang"), "B~align_horizontal_right~" + qsTr("Căn phải"), "B~align_vertical_top~" + qsTr("Căn trên"), "B~align_vertical_center~" + qsTr("Căn giữa dọc"), "B~align_vertical_bottom~" + qsTr("Căn dưới")],
    marquee: ["ic", "|", "M", "|", "F~" + qsTr("Feather") + "~0 px~64", "!C~" + qsTr("Anti-alias") + "~1", "|", "L~" + qsTr("Style"), "D~Normal~112~style~" + qsTr("Style"), "!F~" + qsTr("W") + "~~72~" + qsTr("Chiều rộng"), "!B~swap_horiz~" + qsTr("Đổi W và H"), "!F~" + qsTr("H") + "~~72~" + qsTr("Chiều cao"), "SP", "BT~" + qsTr("Select and Mask…") + "~=SelectMask"],
    lasso: ["ic", "|", "M", "|", "F~" + qsTr("Feather") + "~0 px~64", "C~" + qsTr("Anti-alias") + "~1", "SP", "BT~" + qsTr("Select and Mask…") + "~=SelectMask"],
    wand: ["ic", "|", "M", "|", "L~" + qsTr("Sample Size"), "D~Point Sample~128~sample5~" + qsTr("Sample Size"), "F~" + qsTr("Tolerance") + "~32~48", "C~" + qsTr("Anti-alias") + "~1", "C~" + qsTr("Contiguous") + "~1", "C~" + qsTr("Sample All Layers") + "~0", "SP", "BI~frame_person~" + qsTr("Select Subject") + "~@ants:subject", "BT~" + qsTr("Select and Mask…") + "~=SelectMask"],
    selbrush: ["ic", "|", "S~" + qsTr("Cộng") + "|" + qsTr("Trừ") + "~0~" + qsTr("Chế độ"), "|", "BP~80~0", "F~" + qsTr("Độ cứng") + "~50%~56", "|", "L~" + qsTr("Lớp phủ khi vẽ"), "SW~rgba(255,0,0,.5)~" + qsTr("Màu lớp phủ"), "F~" + qsTr("Opacity") + "~50%~56~" + qsTr("Opacity lớp phủ"), "SP", "BT~" + qsTr("Select and Mask…") + "~=SelectMask"],
    crop: ["ic", "|", "D~4:5 (8:10)~124~ratio~" + qsTr("Tỉ lệ"), "F~~4~52~" + qsTr("Tỉ lệ W"), "B~swap_horiz~" + qsTr("Đổi W và H") + "~@cropSwap", "F~~5~52~" + qsTr("Tỉ lệ H"), "BT~" + qsTr("Xóa W, H") + "~@cropClear", "|", "B~straighten~" + qsTr("Straighten: kẻ một đường để làm thẳng ảnh"), "B~grid_on~" + qsTr("Lưới phụ"), "B~settings~" + qsTr("Classic Mode, hiện vùng bị cắt, tấm che"), "C~" + qsTr("Delete Cropped Pixels") + "~0", "SP", "B~restart_alt~" + qsTr("Đặt lại") + "~@cropClear", "B~close~" + qsTr("Hủy (Esc)") + "~@cropCancel", "B~check~" + qsTr("Áp dụng (Enter)") + "~@cropApply"],
    pcrop: ["ic", "|", "F~" + qsTr("W") + "~~64~" + qsTr("Chiều rộng"), "B~swap_horiz~" + qsTr("Đổi W và H"), "F~" + qsTr("H") + "~~64~" + qsTr("Chiều cao"), "F~" + qsTr("Resolution") + "~~64", "D~Pixels/Inch~108~res~" + qsTr("Đơn vị độ phân giải"), "BT~" + qsTr("Front Image"), "BT~" + qsTr("Xóa W, H"), "C~" + qsTr("Show Grid") + "~1", "SP", "B~close~" + qsTr("Hủy (Esc)"), "B~check~" + qsTr("Áp dụng (Enter)")],
    eyedrop: ["ic", "|", "L~" + qsTr("Sample Size"), "D~3 by 3 Average~140~sample5~" + qsTr("Sample Size"), "L~" + qsTr("Sample"), "D~All Layers~150~sampleEye~" + qsTr("Sample"), "C~" + qsTr("Show Sampling Ring") + "~1"],
    spot: ["ic", "|", "BP~60~0", "|", "L~" + qsTr("Mode"), "D~Normal~104~paint~" + qsTr("Mode"), "L~" + qsTr("Type"), "S~" + qsTr("Proximity Match") + "|" + qsTr("Create Texture") + "~0~" + qsTr("Type"), "C~" + qsTr("Sample All Layers") + "~1"],
    heal: ["ic", "|", "BP~40~1", "|", "L~" + qsTr("Mode"), "D~Normal~104~paint~" + qsTr("Mode"), "L~" + qsTr("Source"), "S~" + qsTr("Sampled") + "~0~" + qsTr("Source"), "C~" + qsTr("Aligned") + "~1", "L~" + qsTr("Sample"), "D~Current Layer~136~sampleLayers~" + qsTr("Sample"), "F~" + qsTr("Diffusion") + "~5~40"],
    patch: ["ic", "|", "M", "|", "L~" + qsTr("Patch"), "D~Normal~104~patch~" + qsTr("Patch"), "S~" + qsTr("Source") + "|" + qsTr("Destination") + "~0~" + qsTr("Patch"), "C~" + qsTr("Transparent") + "~0", "F~" + qsTr("Diffusion") + "~5~40"],
    brush: ["ic", "|", "BP~400~0", BS, "|", "L~" + qsTr("Mode"), "D~Normal~112~paint~" + qsTr("Mode"), "FC~" + qsTr("Opacity") + "~6%~64", "T~stylus~" + qsTr("Lực nhấn điều khiển Opacity") + "~1", "FC~" + qsTr("Flow") + "~100%~64", "T~stylus~" + qsTr("Lực nhấn điều khiển Flow") + "~0", "T~blur_on~" + qsTr("Airbrush") + "~0", "|", "F~" + qsTr("Smoothing") + "~10%~52", SMOOTH, "|", "IF~rotate_right~0°~52~" + qsTr("Góc đầu cọ"), "T~line_weight~" + qsTr("Lực nhấn điều khiển cỡ") + "~0"],
    pencil: ["ic", "|", "BP~4~1", BS, "|", "L~" + qsTr("Mode"), "D~Normal~112~paint~" + qsTr("Mode"), "FC~" + qsTr("Opacity") + "~100%~64", "T~stylus~" + qsTr("Lực nhấn điều khiển Opacity") + "~0", "|", "F~" + qsTr("Smoothing") + "~0%~52", SMOOTH, "|", "IF~rotate_right~0°~52~" + qsTr("Góc đầu cọ"), "T~line_weight~" + qsTr("Lực nhấn điều khiển cỡ") + "~0", "C~" + qsTr("Auto Erase") + "~0"],
    clone: ["ic", "|", "BP~120~0", BS, "|", "L~" + qsTr("Mode"), "D~Normal~112~paint~" + qsTr("Mode"), "FC~" + qsTr("Opacity") + "~100%~64", "T~stylus~" + qsTr("Lực nhấn điều khiển Opacity") + "~0", "FC~" + qsTr("Flow") + "~100%~64", "T~stylus~" + qsTr("Lực nhấn điều khiển Flow") + "~0", "T~blur_on~" + qsTr("Airbrush") + "~0", "|", "C~" + qsTr("Aligned") + "~1", "L~" + qsTr("Sample"), "D~Current & Below~144~sampleLayers~" + qsTr("Sample")],
    eraser: ["ic", "|", "BP~200~0", BS, "|", "L~" + qsTr("Mode"), "D~Brush~92~eraser~" + qsTr("Mode"), "FC~" + qsTr("Opacity") + "~100%~64", "T~stylus~" + qsTr("Lực nhấn điều khiển Opacity") + "~0", "FC~" + qsTr("Flow") + "~100%~64", "T~stylus~" + qsTr("Lực nhấn điều khiển Flow") + "~0", "T~blur_on~" + qsTr("Airbrush") + "~0", "|", "F~" + qsTr("Smoothing") + "~10%~52", SMOOTH, "|", "IF~rotate_right~0°~52~" + qsTr("Góc đầu cọ"), "T~line_weight~" + qsTr("Lực nhấn điều khiển cỡ") + "~0"],
    gradient: ["ic", "|", "G", "S~" + qsTr("Gradient") + "|" + qsTr("Classic") + "~0~" + qsTr("Kiểu gradient"), "|", "GT", "|", "L~" + qsTr("Mode"), "D~Normal~104~paint~" + qsTr("Mode"), "F~" + qsTr("Opacity") + "~100%~56", "C~" + qsTr("Reverse") + "~0", "C~" + qsTr("Dither") + "~1", "L~" + qsTr("Method"), "D~Perceptual~112~method~" + qsTr("Method")],
    bucket: ["ic", "|", "L~" + qsTr("Fill"), "D~Màu trước~112~bucket~" + qsTr("Fill"), "L~" + qsTr("Mode"), "D~Normal~104~paint~" + qsTr("Mode"), "F~" + qsTr("Opacity") + "~100%~56", "F~" + qsTr("Tolerance") + "~32~48", "C~" + qsTr("Anti-alias") + "~1", "C~" + qsTr("Contiguous") + "~1", "C~" + qsTr("All Layers") + "~0"],
    dodge: ["ic", "|", "BP~300~0", BS, "|", "L~" + qsTr("Range"), "D~Midtones~108~range~" + qsTr("Range"), "F~" + qsTr("Exposure") + "~8%~52", "T~blur_on~" + qsTr("Airbrush") + "~0", "|", "IF~rotate_right~0°~52~" + qsTr("Góc đầu cọ"), "C~" + qsTr("Protect Tones") + "~1", "T~line_weight~" + qsTr("Lực nhấn điều khiển cỡ") + "~0"],
    sponge: ["ic", "|", "BP~300~0", BS, "|", "L~" + qsTr("Mode"), "S~" + qsTr("Desaturate") + "|" + qsTr("Saturate") + "~1~" + qsTr("Mode"), "F~" + qsTr("Flow") + "~10%~52", "T~blur_on~" + qsTr("Airbrush") + "~0", "|", "IF~rotate_right~0°~52~" + qsTr("Góc đầu cọ"), "C~" + qsTr("Vibrance") + "~1", "T~line_weight~" + qsTr("Lực nhấn điều khiển cỡ") + "~0"],
    type: ["ic", "|", "D~Be Vietnam Pro~172~font~" + qsTr("Font"), "D~SemiBold~104~fstyle~" + qsTr("Kiểu chữ"), "IF~format_size~48 pt~72~" + qsTr("Cỡ chữ"), "D~aA Sharp~96~aa~" + qsTr("Khử răng cưa"), "|", "B~format_align_left~" + qsTr("Căn trái"), "B~format_align_center~" + qsTr("Căn giữa"), "B~format_align_right~" + qsTr("Căn phải"), "SW~#f4ede4~" + qsTr("Màu chữ"), "B~text_fields~" + qsTr("Panel Character, Paragraph") + "~@side:Character", "SP", "B~close~" + qsTr("Hủy (Esc)"), "B~check~" + qsTr("Xác nhận (Ctrl Enter)")],
    rectS: ["ic", "|", "D~Shape~84~shapeMode~" + qsTr("Chế độ vẽ hình"), "L~" + qsTr("Fill"), "SW~#c0587e~" + qsTr("Màu Fill"), "L~" + qsTr("Stroke"), "SW~none~" + qsTr("Màu Stroke"), "F~~3 px~52~" + qsTr("Độ dày Stroke"), "D~————~72~strokeStyle~" + qsTr("Kiểu nét"), "|", "F~" + qsTr("W") + "~1.200 px~80~" + qsTr("Chiều rộng"), "B~link~" + qsTr("Giữ tỉ lệ"), "F~" + qsTr("H") + "~800 px~72~" + qsTr("Chiều cao"), "|", "B~mode_add~" + qsTr("Thao tác path"), "B~settings~" + qsTr("Tùy chọn path"), "|", "IF~rounded_corner~144 px~60~" + qsTr("Bo góc")],
    polygon: ["ic", "|", "D~Shape~84~shapeMode~" + qsTr("Chế độ vẽ hình"), "L~" + qsTr("Fill"), "SW~#2b1d4e~" + qsTr("Màu Fill"), "L~" + qsTr("Stroke"), "SW~#f7c873~" + qsTr("Màu Stroke"), "F~~2 px~52~" + qsTr("Độ dày Stroke"), "D~————~72~strokeStyle~" + qsTr("Kiểu nét"), "|", "F~" + qsTr("W") + "~600 px~72~" + qsTr("Chiều rộng"), "B~link~" + qsTr("Giữ tỉ lệ"), "F~" + qsTr("H") + "~570 px~72~" + qsTr("Chiều cao"), "|", "B~mode_add~" + qsTr("Thao tác path"), "B~settings~" + qsTr("Tùy chọn path"), "|", "F~" + qsTr("Số cạnh") + "~5~40", "F~" + qsTr("Tỉ lệ sao") + "~100%~56", "IF~rounded_corner~0 px~60~" + qsTr("Bo góc")],
    line: ["ic", "|", "D~Shape~84~shapeMode~" + qsTr("Chế độ vẽ hình"), "L~" + qsTr("Fill"), "SW~none~" + qsTr("Màu Fill"), "L~" + qsTr("Stroke"), "SW~#ececed~" + qsTr("Màu Stroke"), "F~~4 px~52~" + qsTr("Độ dày Stroke"), "D~————~72~strokeStyle~" + qsTr("Kiểu nét"), "|", "F~" + qsTr("W") + "~900 px~72~" + qsTr("Chiều rộng"), "F~" + qsTr("H") + "~0 px~60~" + qsTr("Chiều cao"), "|", "B~mode_add~" + qsTr("Thao tác path"), "B~settings~" + qsTr("Đầu mũi tên, độ dày")],
    hand: ["ic", "|", "BT~" + qsTr("100%") + "~@zoom100", "BT~" + qsTr("Vừa màn hình") + "~@zoomFit", "BT~" + qsTr("Lấp đầy màn hình") + "~@zoomFill"],
    rot: ["ic", "|", "IF~rotate_right~0°~60~" + qsTr("Góc xoay view"), "BT~" + qsTr("Đặt lại view")],
    zoom: ["ic", "|", "S2~zoom_in|zoom_out~0~" + qsTr("Phóng hay thu"), "!C~" + qsTr("Resize Windows to Fit") + "~0", "C~" + qsTr("Scrubby Zoom") + "~1", "BT~" + qsTr("100%") + "~@zoom100", "BT~" + qsTr("Vừa màn hình") + "~@zoomFit", "BT~" + qsTr("Lấp đầy màn hình") + "~@zoomFill"],
    xf: ["ic", "|", "AN", "F~" + qsTr("X") + "~2.200 px~80~" + qsTr("Vị trí X"), "T~change_history~" + qsTr("Vị trí tương đối với điểm gốc") + "~0", "F~" + qsTr("Y") + "~2.800 px~80~" + qsTr("Vị trí Y"), "|", "F~" + qsTr("W") + "~100%~60~" + qsTr("Tỉ lệ W"), "T~link~" + qsTr("Giữ tỉ lệ") + "~1", "F~" + qsTr("H") + "~100%~60~" + qsTr("Tỉ lệ H"), "|", "IF~rotate_right~0,00°~64~" + qsTr("Góc xoay"), "L~" + qsTr("Nghiêng"), "F~" + qsTr("H") + "~0,0°~52~" + qsTr("Nghiêng ngang"), "F~" + qsTr("V") + "~0,0°~52~" + qsTr("Nghiêng dọc"), "|", "L~" + qsTr("Nội suy"), "D~Bicubic~104~interp~" + qsTr("Nội suy"), "T~grid_on~" + qsTr("Đổi sang Warp") + "~0~@warp", "SP", "B~close~" + qsTr("Hủy (Esc)") + "~@xfCancel", "B~check~" + qsTr("Áp dụng (Enter)") + "~@xfApply"],
    warp: ["ic", "|", "L~" + qsTr("Lưới"), "D~3 × 3~84~warpGrid~" + qsTr("Lưới warp"), "L~" + qsTr("Warp"), "D~Custom~104~warpStyle~" + qsTr("Kiểu warp"), "T~grid_on~" + qsTr("Đổi giữa Warp và Free Transform") + "~1~@warp", "SP", "B~close~" + qsTr("Hủy (Esc)") + "~@xfCancel", "B~check~" + qsTr("Áp dụng (Enter)") + "~@xfApply"]
}
BARS.ellipS = BARS.rectS.slice(0, -2)
var LISTS = {
    paint: ["Normal", "Dissolve", "Behind", "Clear", "Darken", "Multiply", "Color Burn", "Linear Burn", "Darker Color", "Lighten", "Screen", "Color Dodge", "Linear Dodge (Add)", "Lighter Color", "Overlay", "Soft Light", "Hard Light", "Vivid Light", "Linear Light", "Pin Light", "Hard Mix", "Difference", "Exclusion", "Subtract", "Divide", "Hue", "Saturation", "Color", "Luminosity"],
    blend: ["Normal", "Dissolve", "Darken", "Multiply", "Color Burn", "Linear Burn", "Darker Color", "Lighten", "Screen", "Color Dodge", "Linear Dodge (Add)", "Lighter Color", "Overlay", "Soft Light", "Hard Light", "Vivid Light", "Linear Light", "Pin Light", "Hard Mix", "Difference", "Exclusion", "Subtract", "Divide", "Hue", "Saturation", "Color", "Luminosity"],
    group: ["Pass Through", "Normal", "Dissolve", "Darken", "Multiply", "Color Burn", "Linear Burn", "Darker Color", "Lighten", "Screen", "Color Dodge", "Linear Dodge (Add)", "Lighter Color", "Overlay", "Soft Light", "Hard Light", "Vivid Light", "Linear Light", "Pin Light", "Hard Mix", "Difference", "Exclusion", "Subtract", "Divide", "Hue", "Saturation", "Color", "Luminosity"],
    layerGroup: ["Layer", "Group"],
    style: ["Normal", "Fixed Ratio", "Fixed Size"],
    sample5: ["Point Sample", "3 by 3 Average", "5 by 5 Average", "11 by 11 Average", "31 by 31 Average", "51 by 51 Average", "101 by 101 Average"],
    sampleLayers: ["Current Layer", "Current & Below", "All Layers"],
    sampleEye: ["Current Layer", "Current & Below", "All Layers", "All Layers No Adjustments", "Current & Below No Adjustments"],
    ratio: ["Ratio", "W x H x Resolution", "Original Ratio", "1:1 (Square)", "4:5 (8:10)", "5:7", "2:3 (4:6)", "16:9"],
    res: ["Pixels/Inch", "Pixels/Centimeter"],
    patch: ["Normal"],
    eraser: ["Brush", "Pencil", "Block"],
    range: ["Shadows", "Midtones", "Highlights"],
    method: ["Perceptual", "Linear", "Classic", "Smooth", "Stripes"],
    bucket: ["Màu trước"],
    font: ["Be Vietnam Pro", "Montserrat", "Lora", "Playfair Display", "Noto Serif"],
    fstyle: ["Regular", "Medium", "SemiBold", "Bold"],
    aa: ["aA None", "aA Sharp", "aA Crisp", "aA Strong", "aA Smooth"],
    shapeMode: ["Shape", "Path", "Pixels"],
    strokeStyle: ["————", "- - - -", "· · · ·"],
    interp: ["Nearest Neighbor", "Bilinear", "Bicubic", "Bicubic Smoother", "Bicubic Sharper"],
    warpGrid: ["3 × 3", "4 × 4", "5 × 5", "Tùy ý…"],
    warpStyle: ["Custom", "Arc", "Arc Lower", "Arc Upper", "Arch", "Bulge", "Shell Lower", "Shell Upper", "Flag", "Wave", "Fish", "Rise", "Fisheye", "Inflate", "Squeeze", "Twist"],
    chan: ["RGB", "Red", "Green", "Blue"],
    chan3: ["Red", "Green", "Blue"],
    hueRange: ["Master", "Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas"],
    scColors: ["Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas", "Whites", "Neutrals", "Blacks"],
    pfilter: ["Warming Filter (85)", "Warming Filter (LBA)", "Warming Filter (81)", "Cooling Filter (80)", "Cooling Filter (LBB)", "Cooling Filter (82)", "Red", "Orange", "Yellow", "Green", "Cyan", "Blue", "Violet", "Magenta", "Sepia", "Underwater"],
    lut: ["Phim ấm.cube", "Phim lạnh.cube", "Teal Orange.3dl", "Nạp 3DLUT…"],
    lutA: ["Chọn…"],
    docMode: ["RGB, 8 bit", "RGB, 16 bit", "Grayscale, 8 bit", "Grayscale, 16 bit"],
    profile: ["sRGB IEC61966-2.1", "Display P3", "Adobe RGB (1998)", "ProPhoto RGB"],
    colorMode: ["HSB", "RGB", "Lab"],
    stat: ["Tài liệu", "Hồ sơ màu", "Kích thước", "RAM, scratch", "Thời gian thao tác", "Công cụ đang dùng"],
    bsCtl: ["Off", "Pen Pressure", "Pen Tilt", "Stylus Wheel", "Rotation"]
}
// Selection modes: [icon, tip].
var MODES = [["mode_new", qsTr("Vùng chọn mới")], ["mode_add", qsTr("Cộng vào vùng chọn (⇧)")], ["mode_sub", qsTr("Trừ khỏi vùng chọn (Alt)")], ["mode_int", qsTr("Giao với vùng chọn (⇧ Alt)")]]
// Gradient types: [Segmented's grad, name] (the prototype's CSS swatch named as Segmented draws it).
var GTYPES = [["linear", qsTr("Linear")], ["radial", qsTr("Radial")], ["angle", qsTr("Angle")], ["reflected", qsTr("Reflected")], ["diamond", qsTr("Diamond")]]
// What a tool does, for the Info panel; other tools: "<name>: nhấp lên canvas để dùng." (hint()).
var HINT = {
    move: qsTr("Move: kéo để dời layer; giữ Alt kéo để nhân bản."),
    brush: qsTr("Brush: nhấp và kéo để vẽ. Shift nhấp để vẽ đường thẳng; giữ Alt để lấy màu."),
    spot: qsTr("Spot Healing Brush: tô lên vết cần xóa."),
    heal: qsTr("Healing Brush: giữ Alt nhấp để đặt nguồn, rồi tô."),
    clone: qsTr("Clone Stamp: giữ Alt nhấp để đặt nguồn, rồi tô."),
    rect: qsTr("Rectangular Marquee: kéo để chọn; Shift giữ hình vuông, Alt vẽ từ tâm."),
    crop: qsTr("Crop: kéo tay nắm, Enter để áp dụng."),
    zoom: qsTr("Zoom: nhấp để phóng to; giữ Alt để thu nhỏ.")
}
// The cells the number keys set, by bar: OPACITY Opacity (Exposure of Dodge, Burn; Flow of Sponge), FLOW (with Shift) Flow.
var OPACITY = { brush: 7, pencil: 7, clone: 7, eraser: 7, gradient: 9, bucket: 6, dodge: 7, sponge: 7 }
var FLOW = { brush: 9, clone: 9, eraser: 9, sponge: 7 }
// ---- end of the copy -----------------------------------------------------------------------------------------------

// ---- tools ----------------------------------------------------------------------------------------------------------
// TOOL: id → { id, n name, k key, icn icon, bar, g group }; TOP0: group → its first tool (as the prototype builds them).
var TOOL = {}
var TOP0 = {}
GROUPS.forEach((g) => { if (g) { TOP0[g[0]] = g[1][0][0]; g[1].forEach((t) => { TOOL[t[0]] = { id: t[0], n: t[1], k: t[2], icn: t[3], bar: t[4], g: g[0] } }) } })

function tool(id) { return TOOL[id] || TOOL.brush }
function name(id) { return TOOL[id] ? TOOL[id].n : String(id) }
// The tools of a group, in order.
function members(group) {
    const g = GROUPS.find((x) => x && x[0] === group)
    return g ? g[1].map((t) => TOOL[t[0]]) : []
}
// "Brush (B)"; the name alone when Thiết lập › Công cụ has no keys in tooltips.
function tip(id, keys) { return keys === false ? tool(id).n : tool(id).n + " (" + tool(id).k + ")" }
function hint(id) { return HINT[id] || qsTr("%1: nhấp lên canvas để dùng.").arg(name(id)) }
// The bar a tool shows; Free Transform's or Warp's while one is on (Tools.xf, Tools.warp).
function barOf(id, xf, warp) { return xf ? (warp ? "warp" : "xf") : tool(id).bar }
// Alt held picks a colour with these, as Photoshop's: Healing Brush and Clone Stamp take Alt for their source, Dodge and
// Burn to swap, Eraser for Erase to History. Ctrl held is Move but with the view's tools, where it zooms.
var ALT_PICK = { brush: 1, pencil: 1, gradient: 1, bucket: 1 }
var VIEW_T = { hand: 1, rot: 1, zoom: 1 }
// The bar whose brush the brush picker and [ ] change: the painting tool's, else Brush's.
function brushBar(id, xf) { return PAINT_T[id] && !xf ? tool(id).bar : "brush" }
// The tool a key picks: its group's tool on top when that one has the key, else the first that has it; with next (Shift
// and the key, or the key again when Thiết lập › Công cụ cycles without Shift) the one after the tool in use.
function keyTool(k, current, top, next) {
    const ids = Object.keys(TOOL).filter((id) => TOOL[id].k === k)
    if (!ids.length)
        return ""
    const i = ids.indexOf(current), t = top[TOOL[ids[0]].g]
    return next && i >= 0 ? ids[(i + 1) % ids.length] : ids.indexOf(t) >= 0 ? t : ids[0]
}

// ---- bars -----------------------------------------------------------------------------------------------------------
// Limits of the number cells (plan.md's Công cụ table) by label; other angles go half a turn either way.
var RANGES = {}
RANGES[qsTr("Feather")] = [0, 1000]
RANGES[qsTr("Tolerance")] = [0, 255]
RANGES[qsTr("Opacity")] = [1, 100]
RANGES[qsTr("Flow")] = [1, 100]
RANGES[qsTr("Exposure")] = [1, 100]
RANGES[qsTr("Smoothing")] = [0, 100]
RANGES[qsTr("Diffusion")] = [1, 7]
RANGES[qsTr("Độ cứng")] = [0, 100]
RANGES[qsTr("Số cạnh")] = [3, 100]
RANGES[qsTr("Tỉ lệ sao")] = [1, 100]
RANGES[qsTr("Cỡ chữ")] = [1, 1296]  // as Character's

// A bar's item as an object: kind (the DSL's: "ic", "|", "L", "D", "F", "FC", "T", "B", "C", "BT", "BI", "M", "S", "S2",
// "GT", "BP", "SW", "G", "IF", "AN", "SP"), dis (dimmed, "!"), and by kind: text, tip, icon, w (px), list, listName (D),
// options (M, S, S2, GT, as Segmented takes them), act ("@name[:arg]", "=Board", "" for none), def, the value of a key not set
// (a string for D and SW, a number for F, FC, IF, NaN when empty, true or false for C and T, an index for M, S, S2, GT,
// AN), unit, decimals, min and max (F, FC, IF), size and hard (BP).
function cell(bar, i) {
    const raw = BARS[bar][i], dis = raw[0] === "!", a = (dis ? raw.slice(1) : raw).split("~")
    const c = { kind: a[0], dis: dis }
    switch (a[0]) {
    case "L": c.text = a[1]; break
    case "D": c.def = a[1]; c.w = +a[2] || 104; c.list = LISTS[a[3]] || [a[1]]; c.listName = a[3] || ""; c.tip = a[4] || a[1]; break
    case "F": case "FC": c.text = a[1]; c.tip = a[4] || a[1]; number(c, a[2] || "", +a[3] || 64); break
    case "IF": c.icon = a[1]; c.tip = a[4] || a[2]; number(c, a[2], +a[3] || 56); break
    case "T": c.icon = a[1]; c.tip = a[2]; c.def = a[3] === "1"; c.act = a[4] || ""; break
    case "B": c.icon = a[1]; c.tip = a[2]; c.act = a[3] || ""; break
    case "C": c.text = a[1]; c.def = a[2] === "1"; break
    case "BT": c.text = a[1]; c.act = a[2] || ""; break
    case "BI": c.icon = a[1]; c.text = a[2]; c.act = a[3] || ""; break
    case "M": c.options = MODES.map((m) => ({ icon: m[0], tip: m[1] })); c.def = 0; c.tip = qsTr("Chế độ vùng chọn"); break
    case "S": c.options = a[1].split("|"); c.def = +a[2]; c.tip = a[3] || ""; break
    case "S2": c.options = a[1].split("|").map((n) => ({ icon: n, tip: n === "zoom_in" ? qsTr("Phóng to") : qsTr("Thu nhỏ") })); c.def = +a[2]; c.tip = a[3] || ""; break
    case "GT": c.options = GTYPES.map((g) => ({ grad: g[0], tip: g[1] })); c.def = 0; c.tip = qsTr("Kiểu gradient"); break
    case "BP": c.size = +a[1]; c.hard = a[2] === "1" ? 100 : 0; break
    case "SW": c.def = a[1]; c.tip = a[2] || qsTr("Màu"); break
    case "AN": c.def = 4; c.tip = qsTr("Điểm gốc"); break
    }
    return c
}
// A number cell's default ("1.200 px", "0,00°", "" empty) as value, unit and decimals, its box w px wide.
function number(c, s, w) {
    const u = /(px|pt|%|°)$/.exec(s), d = /,(\d+)/.exec(s), r = RANGES[c.text] || RANGES[c.tip]  // an IF cell by its tip
    c.def = s === "" ? NaN : Fmt.pnum(s)
    c.unit = u ? u[1] : ""
    c.decimals = d ? d[1].length : 0
    c.w = w
    c.min = r ? r[0] : c.unit === "°" ? -180 : -Infinity
    c.max = r ? r[1] : c.unit === "°" ? 180 : Infinity
}
function cells(bar) { return (BARS[bar] || []).map((x, i) => cell(bar, i)) }
function def(bar, i) { return cell(bar, i).def }
// Cells another panel shows too keep one value with it, as Photoshop's: a painting bar's tip angle is Brush Settings'
// "<bar>:angle", the Type bar's font, style, size and colour are Character's "char:*". key(bar, i): where Tools keeps cell i.
var SHARED = { "brush:16": "brush:angle", "pencil:13": "pencil:angle", "eraser:16": "eraser:angle", "dodge:10": "dodge:angle", "sponge:10": "sponge:angle",
    "type:2": "char:font", "type:3": "char:style", "type:4": "char:size", "type:10": "char:color" }
function key(bar, i) { return SHARED[bar + ":" + i] || bar + ":" + i }
// A painting bar's brush as it starts: { size px, hard %, spacing %, tip }, from its BP item.
function brush(bar) {
    const a = ((BARS[bar] || []).find((x) => x.indexOf("BP~") === 0) || "BP~400~0").split("~")
    return { size: +a[1], hard: a[2] === "1" ? 100 : 0, spacing: 25, tip: 0 }
}
// [ ] step the size as Photoshop does, by 1 px under 10, 10 under 100, 25 under 200, 50 under 300, then 100, within
// 1 to 5.000 px; Shift [ ] the hardness by quarters.
function sizeStep(size, dir) {
    const step = (s) => (s < 10 ? 1 : s < 100 ? 10 : s < 200 ? 25 : s < 300 ? 50 : 100)
    return Math.max(1, Math.min(5000, dir > 0 ? size + step(size) : size - step(size - 1)))
}
function hardStep(hard, dir) { return dir > 0 ? Math.min(100, Math.floor(hard / 25) * 25 + 25) : Math.max(0, Math.ceil(hard / 25) * 25 - 25) }

// ---- keys -----------------------------------------------------------------------------------------------------------
// The tab Công cụ of Phím tắt (Shortcuts.dc.html): [group name, rows], a row { l, en (English name), k (as shown, "⇧ ["),
// and tool (the tool id its key picks), act (what the toolbar runs: resetColors, swapColors, size-, size+, hard-, hard+)
// or menu (a key of Main's menus: F, Q) }. The toolbar runs these keys and, besides, Shift and a tool's key (the next
// tool with that key), 0 … 9 (Opacity, OPACITY), Shift 0 … 9 (Flow, FLOW), and held Space (Hand), Alt (Eyedropper, with
// ALT_PICK's tools), Ctrl (Move, but with VIEW_T's), the tool back on release.
function toolRows(groups) { return groups.reduce((o, g) => o.concat(members(g).map((t) => ({ l: t.n, en: "", k: t.k, tool: t.id }))), []) }
var KEYS = [
    [qsTr("Chọn, cắt, lấy mẫu"), toolRows(["move", "marquee", "lasso", "wand", "crop", "eye"])],
    [qsTr("Vẽ và sửa"), toolRows(["heal", "brush", "stamp", "eraser", "grad", "dodge"])],
    [qsTr("Chữ và hình"), toolRows(["type", "shape"])],
    [qsTr("Xem"), toolRows(["hand", "zoom"]).concat([{ l: qsTr("Đổi chế độ màn hình"), en: "Screen Mode", k: "F", menu: true }])],
    [qsTr("Màu và cọ"), [{ l: qsTr("Màu mặc định"), en: "Default Colors", k: "D", act: "resetColors" },
        { l: qsTr("Đổi màu trước, sau"), en: "Switch Colors", k: "X", act: "swapColors" }, { l: qsTr("Quick Mask"), en: "", k: "Q", menu: true },
        { l: qsTr("Giảm cỡ cọ"), en: "", k: "[", act: "size-" }, { l: qsTr("Tăng cỡ cọ"), en: "", k: "]", act: "size+" },
        { l: qsTr("Giảm độ cứng"), en: "", k: "⇧ [", act: "hard-" }, { l: qsTr("Tăng độ cứng"), en: "", k: "⇧ ]", act: "hard+" }]]
]
