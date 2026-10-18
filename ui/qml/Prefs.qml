pragma Singleton
import QtQuick
import QtCore

// Thiết lập (docs/ui-tasks.md, U3): every value of build/proto/Settings.dc.html's PANES and its Hiệu năng page, by key,
// defaults as the artboard. `panes` is that list as data for the dialog (U21). Kept between starts as one JSON string in
// QtCore Settings once Main sets persist (main.cpp: not in tests, nor with --screenshot); Theme does not read it (U4 ties
// them). A select or a group of segments keeps the index of its option.
QtObject {
    id: prefs
    property bool persist: false
    property var values: ({})
    function value(key) { return values[key] }
    function set(key, v) { values = Object.assign({}, values, { [key]: v }) }
    function reset() { values = Object.assign({}, defaults) }

    // The dialog's 16 sections: [id, title, icon]; "performance" is the page of its own (perf).
    readonly property var sections: [["general", qsTr("Chung"), "settings"], ["interface", qsTr("Giao diện"), "contrast"], ["workspace", qsTr("Workspace"), "dashboard"],
        ["tools", qsTr("Công cụ"), "handyman"], ["history", qsTr("Lịch sử và khôi phục"), "history"], ["files", qsTr("File và xuất"), "save"],
        ["performance", qsTr("Hiệu năng"), "speed"], ["cursors", qsTr("Con trỏ"), "ads_click"], ["transparency", qsTr("Trong suốt và gam"), "opacity"],
        ["units", qsTr("Đơn vị và thước"), "straighten"], ["guides", qsTr("Guide và lưới"), "grid_on"], ["type", qsTr("Chữ"), "title"], ["pen", qsTr("Bút"), "stylus"],
        ["shortcuts", qsTr("Phím tắt"), "keyboard"], ["ai", qsTr("AI"), "auto_awesome"], ["about", qsTr("Báo lỗi, cập nhật, giấy phép"), "system_update"]]
    readonly property var canvasColors: [qsTr("Đen"), qsTr("Xám đậm"), qsTr("Xám vừa"), qsTr("Xám nhạt"), qsTr("Tùy chọn…")]  // BG
    // The rows of each section, by kind: toggle, select and group (options; the value is an index), range (min, max,
    // step, unit), colors (one key per colour), accent (options: [hex, name]), model (an AI model: downloaded or not),
    // button (text, action: "copyId", "license"), link (text, target: a dialog's name), note. The prototype's "P2: …",
    // "P3: …" notes are design notes, left out.
    readonly property var panes: ({
        general: [
            { kind: "toggle", key: "placeSmartObject", label: qsTr("Place luôn tạo smart object"), def: true },
            { kind: "toggle", key: "placeResize", label: qsTr("Đổi cỡ ảnh cho vừa canvas khi Place"), def: true },
            { kind: "toggle", key: "transformProportional", label: qsTr("Free Transform giữ tỉ lệ mặc định"), def: true },
            { kind: "toggle", key: "autoReload", label: qsTr("Tự tải lại file đang mở khi file đổi trên đĩa"), def: true }
        ],
        interface: [
            { kind: "group", key: "theme", label: qsTr("Theme"), options: [qsTr("Tối"), qsTr("Sáng")], def: 0 },
            { kind: "accent", key: "accent", label: qsTr("Màu nhấn"), options: [["#2FB5B0", qsTr("Xanh ngọc")], ["#4C8DF6", qsTr("Xanh dương")], ["#E0A53A", qsTr("Vàng hổ phách")],
                ["#E8664A", qsTr("Cam san hô")]], def: "#2FB5B0" },
            { kind: "select", key: "canvasStandard", label: qsTr("Nền quanh ảnh · chuẩn"), options: canvasColors, def: 1 },
            { kind: "select", key: "canvasFullMenu", label: qsTr("Nền quanh ảnh · toàn màn hình có menu"), options: canvasColors, def: 1 },
            { kind: "select", key: "canvasFull", label: qsTr("Nền quanh ảnh · toàn màn hình"), options: canvasColors, def: 0 },
            { kind: "group", key: "canvasBorder", label: qsTr("Viền canvas"), options: [qsTr("Bóng"), qsTr("Đường"), qsTr("Không")], def: 0 },
            { kind: "group", key: "fontSize", label: qsTr("Cỡ chữ"), options: [qsTr("Nhỏ"), qsTr("Vừa"), qsTr("Lớn")], def: 1 },
            { kind: "group", key: "language", label: qsTr("Ngôn ngữ"), options: ["Tiếng Việt", "English"], def: 0 },
            { kind: "toggle", key: "channelsInColor", label: qsTr("Hiện kênh bằng màu"), def: false }
        ],
        workspace: [
            { kind: "toggle", key: "autoCollapsePanels", label: qsTr("Tự thu gọn panel về icon"), def: false },
            { kind: "toggle", key: "documentTabs", label: qsTr("Mở tài liệu dạng tab"), def: true },
            { kind: "toggle", key: "narrowOptions", label: qsTr("Thanh tùy chọn hẹp"), def: false },
            { kind: "toggle", key: "twoColumnTools", label: qsTr("Thanh công cụ hai cột"), def: false }  // U5's toolbar, 80 px
        ],
        tools: [
            { kind: "toggle", key: "tooltipKeys", label: qsTr("Tooltip có phím và tên tiếng Anh"), def: true },
            { kind: "toggle", key: "shiftCycle", label: qsTr("Shift + phím để đổi công cụ trong nhóm"), def: true },
            { kind: "toggle", key: "wheelZoom", label: qsTr("Zoom bằng con lăn"), def: false },
            { kind: "toggle", key: "zoomToClick", label: qsTr("Zoom vào chỗ nhấp"), def: true },
            { kind: "toggle", key: "animatedZoom", label: qsTr("Zoom có chuyển động"), def: true },
            { kind: "toggle", key: "snapPixels", label: qsTr("Bắt dính lưới điểm ảnh"), def: true },
            { kind: "toggle", key: "maskOpensSelectMask", label: qsTr("Nhấp đúp Layer Mask mở Select and Mask"), def: true }
        ],
        history: [
            { kind: "range", key: "historyBudget", label: qsTr("Ngân sách lịch sử (của ngân sách ô)"), min: 5, max: 75, step: 5, unit: "%", def: 25 },
            { kind: "select", key: "autoSave", label: qsTr("Tự lưu khôi phục"), options: [qsTr("Mỗi 5 phút"), qsTr("Mỗi 10 phút"), qsTr("Mỗi 15 phút"), qsTr("Mỗi 30 phút"), qsTr("Mỗi giờ")], def: 0 },
            { kind: "note", label: qsTr("Bước cũ ra scratch trước; vượt nữa thì bỏ bước cũ nhất.") }
        ],
        files: [
            { kind: "select", key: "psdCompatibility", label: qsTr("Maximize PSD Compatibility"), options: [qsTr("Luôn"), qsTr("Hỏi"), qsTr("Không")], def: 1 },
            { kind: "select", key: "recentFiles", label: qsTr("Số file gần đây"), options: ["10", "20", "30", "50"], def: 1 },
            { kind: "select", key: "quickFormat", label: qsTr("Quick Export: định dạng"), options: ["PNG", "JPEG", "WebP", "TIFF"], def: 0 },
            { kind: "select", key: "quickPlace", label: qsTr("Quick Export: chỗ lưu"), options: [qsTr("Hỏi mỗi lần"), qsTr("Thư mục assets cạnh tài liệu")], def: 0 },
            { kind: "select", key: "quickMetadata", label: qsTr("Quick Export: metadata"), options: [qsTr("Không"), qsTr("Bản quyền và liên hệ")], def: 0 },
            { kind: "toggle", key: "quickSrgb", label: qsTr("Quick Export: đổi sang sRGB"), def: true }
        ],
        cursors: [
            { kind: "group", key: "paintCursor", label: qsTr("Khi vẽ"), options: [qsTr("Standard"), qsTr("Precise"), qsTr("Normal Brush Tip"), qsTr("Full Size Brush Tip")], def: 3 },
            { kind: "toggle", key: "crosshair", label: qsTr("Chữ thập trong đầu cọ"), def: true },
            { kind: "toggle", key: "crosshairWhilePainting", label: qsTr("Chỉ chữ thập khi đang vẽ"), def: false },
            { kind: "group", key: "otherCursor", label: qsTr("Công cụ khác"), options: [qsTr("Standard"), qsTr("Precise")], def: 0 },
            { kind: "colors", keys: ["brushPreviewColor"], label: qsTr("Màu xem trước cọ"), def: ["#ff0000"] }
        ],
        transparency: [
            { kind: "group", key: "checkerSize", label: qsTr("Cỡ ô caro"), options: [qsTr("Không"), qsTr("Nhỏ"), qsTr("Vừa"), qsTr("Lớn")], def: 2 },
            { kind: "colors", keys: ["checkerColor1", "checkerColor2"], label: qsTr("Màu ô"), def: ["#ffffff", "#c9c9c9"] }
        ],
        units: [
            { kind: "select", key: "rulerUnits", label: qsTr("Thước"), options: [qsTr("Pixels"), qsTr("Inches"), qsTr("Centimeters"), qsTr("Millimeters"), qsTr("Points"), qsTr("Picas"), qsTr("Percent")], def: 0 },
            { kind: "select", key: "typeUnits", label: qsTr("Chữ"), options: [qsTr("Pixels"), qsTr("Points"), qsTr("Millimeters")], def: 1 },
            { kind: "select", key: "printResolution", label: qsTr("Độ phân giải in mặc định"), options: ["150 ppi", "240 ppi", "300 ppi", "360 ppi"], def: 2 },
            { kind: "select", key: "screenResolution", label: qsTr("Độ phân giải màn hình"), options: ["72 ppi", "96 ppi", "144 ppi"], def: 0 }
        ],
        guides: [
            { kind: "colors", keys: ["guideColor"], label: qsTr("Guide"), def: ["#29d3f5"] },
            { kind: "select", key: "guideStyle", label: qsTr("Kiểu guide"), options: [qsTr("Đường liền"), qsTr("Nét đứt")], def: 0 },
            { kind: "colors", keys: ["gridColor"], label: qsTr("Lưới"), def: ["#8a8a8a"] },
            { kind: "select", key: "gridStyle", label: qsTr("Kiểu lưới"), options: [qsTr("Đường liền"), qsTr("Nét đứt"), qsTr("Chấm")], def: 0 },
            { kind: "select", key: "gridSpacing", label: qsTr("Khoảng lưới"), options: ["25 px", "50 px", "100 px", "200 px"], def: 2 },
            { kind: "select", key: "gridSubdivisions", label: qsTr("Số ô chia"), options: ["1", "2", "4", "8"], def: 2 },
            { kind: "toggle", key: "pixelGrid", label: qsTr("Lưới điểm ảnh từ 600%"), def: true }
        ],
        type: [
            { kind: "toggle", key: "smartQuotes", label: qsTr("Ngoặc kép thông minh"), def: true },
            { kind: "toggle", key: "protectGlyphs", label: qsTr("Bảo vệ glyph thiếu"), def: true },
            { kind: "toggle", key: "englishFontNames", label: qsTr("Tên font bằng tiếng Anh"), def: false },
            { kind: "select", key: "fontPreviewSize", label: qsTr("Cỡ xem trước font"), options: [qsTr("Nhỏ"), qsTr("Vừa"), qsTr("Lớn"), qsTr("Rất lớn")], def: 1 },
            { kind: "toggle", key: "placeholderText", label: qsTr("Điền chữ mẫu khi tạo layer chữ"), def: true }
        ],
        pen: [
            { kind: "group", key: "tabletApi", label: qsTr("Trên Windows"), options: ["Windows Ink", "Wintab"], def: 0 },
            { kind: "note", label: qsTr("Wintab cho bảng vẽ Wacom đời cũ.") }
        ],
        shortcuts: [
            { kind: "link", label: qsTr("Phím mặc định như Photoshop"), text: qsTr("Mở Phím tắt…"), target: "Shortcuts" },
            { kind: "note", label: qsTr("Bảng phím theo menu ứng dụng, menu panel và công cụ, có ô tìm.") }
        ],
        ai: [
            { kind: "model", key: "modelBiRefNet", label: "BiRefNet_lite", size: "85 MB", def: true },
            { kind: "model", key: "modelYuNet", label: "YuNet", size: "0,3 MB", def: false },
            { kind: "model", key: "modelFaceLandmarker", label: "Face Landmarker", size: "4 MB", def: false },
            { kind: "note", label: qsTr("Model tải khi dùng lần đầu, kiểm SHA-256, chạy trên máy.") }
        ],
        about: [
            { kind: "toggle", key: "crashReports", label: qsTr("Gửi báo cáo crash"), def: true },
            { kind: "toggle", key: "usageStats", label: qsTr("Gửi số liệu sử dụng"), def: false },
            { kind: "button", label: qsTr("Mã cài đặt: %1").arg("7F3A-92C1-4D0E-B85A"), text: qsTr("Sao chép mã"), action: "copyId" },
            { kind: "toggle", key: "autoUpdate", label: qsTr("Tự kiểm tra bản mới"), def: true },
            { kind: "button", label: qsTr("Giấy phép: dùng thử"), text: qsTr("Nhập mã kích hoạt"), action: "license" }
        ]
    })
    // Hiệu năng: budgets (RAM as % of the machine's, VRAM and scratch in GB), GPU on, Cache Levels, scratch disks in the
    // order they fill, on or not.
    readonly property var perf: ({ ramBudget: 60, vramBudget: 1.5, scratchBudget: 200, gpu: true, cacheLevels: 4,
        scratchDisks: [{ name: "C:", on: true }, { name: "D:", on: true }, { name: "E:", on: false }] })
    // The machine as the page shows it, mock until N2 asks K1's stats(): RAM GB, the GPU, what is in use, disks by name.
    readonly property var system: ({ ramGB: 16, gpuName: "Intel Iris Xe Graphics", gpuApi: "Vulkan 1.3", ramUsed: 3.1, vramUsed: 0.9, scratchUsed: 1.2,
        disks: { "C:": { kind: qsTr("SSD NVMe"), free: 118 }, "D:": { kind: qsTr("SSD"), free: 402 }, "E:": { kind: qsTr("HDD ngoài"), free: 1200 } } })
    readonly property var defaults: {
        // New's Đã lưu (U16): presets as NewDocSpec.P() makes them; Nền quanh ảnh's Tùy chọn…, a colour a screen mode (U23).
        const o = Object.assign({ newDocPresets: [], canvasStandardColor: "#282828", canvasFullMenuColor: "#282828", canvasFullColor: "#000000" }, perf)
        for (const id in panes)
            for (const r of panes[id]) {
                if (r.keys)
                    r.keys.forEach((k, i) => { o[k] = r.def[i] })
                else if (r.key)
                    o[r.key] = r.def
            }
        return o
    }

    Component.onCompleted: reset()
    // A value kept takes the place of its default when of the same type; the rest keeps the default.
    function restore(json) {
        let kept = null
        try { kept = JSON.parse(json || "{}") } catch (e) { console.warn("Prefs: ignored, not JSON:", e) }
        const o = Object.assign({}, defaults)
        for (const k in o)
            if (kept && kept[k] !== undefined && typeof kept[k] === typeof o[k] && Array.isArray(kept[k]) === Array.isArray(o[k]))
                o[k] = kept[k]
        values = o
    }
    property Settings store: null  // [prefs] json=… in the app's settings
    readonly property Component storeC: Component { Settings { category: "prefs" } }
    onPersistChanged: {
        if (!persist || store)
            return
        store = storeC.createObject(prefs)
        restore(store.value("json", ""))
    }
    onValuesChanged: {
        if (store)
            store.setValue("json", JSON.stringify(values))
    }
}
