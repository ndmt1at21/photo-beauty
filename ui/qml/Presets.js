.pragma library
// Swatches, gradients and brushes of the panels and the options bar (docs/ui-tasks.md, U3): the prototype's RECENT_SW, SW_GROUPS,
// GR_GROUPS, BR_GROUPS, TIPS, BS_ROWS (build/proto/Main.dc.html), names through qsTr. Gradients stay CSS as there.

// Swatches: the 10 recent, then groups of [name, colours].
var RECENT_SW = ["#e9c7b3", "#ffffff", "#000000", "#c0587e", "#2b1d4e", "#f7c873", "#808080", "#d4a48c", "#6f412c", "#b3202a"];
var SW_GROUPS = [
  [qsTr("Tông da"), ["#f8e1d4", "#f2cdb8", "#eab9a0", "#e0a588", "#d39272", "#c27f5e", "#a9694b", "#8c543a", "#6f412c", "#f6d6c0", "#e9c7b3", "#d4a48c"]],
  [qsTr("Cơ bản"), ["#000000", "#404040", "#808080", "#c0c0c0", "#ffffff", "#ff0000", "#ff8000", "#ffff00", "#80ff00", "#00ff00", "#00ffff", "#0080ff", "#0000ff", "#8000ff", "#ff00ff", "#ff0080"]],
  [qsTr("Tết 2026"), ["#b3202a", "#e5a33a", "#f7c873", "#2b1d4e", "#f4ede4", "#6e1414"]]
];
// Gradients: groups of [name, [[name, CSS], …]].
var GR_GROUPS = [
  [qsTr("Cơ bản"), [[qsTr("Màu trước sang màu sau"), "linear-gradient(135deg, #ffffff, #000000)"], [qsTr("Màu trước sang trong suốt"), "linear-gradient(135deg, #e9c7b3, transparent)"], [qsTr("Đen sang trắng"), "linear-gradient(135deg, #000000, #ffffff)"], [qsTr("Trắng sang đen"), "linear-gradient(135deg, #ffffff, #000000)"]]],
  [qsTr("Da và son"), [[qsTr("Da sáng"), "linear-gradient(135deg, #f7d7c4, #c98f74)"], [qsTr("Da ấm"), "linear-gradient(135deg, #f2cdb8, #a9694b)"], [qsTr("Son hồng"), "linear-gradient(135deg, #e7a1a8, #9e2f45)"], [qsTr("Má hồng"), "linear-gradient(135deg, #f5b5a0, #c0587e)"], [qsTr("Cam đào"), "linear-gradient(135deg, #ffd6c2, #ff8a65)"], [qsTr("Bóng tối da"), "linear-gradient(135deg, #d39272, #4a2a1e)"]]],
  [qsTr("Hoàng hôn"), [[qsTr("Tím cam"), "linear-gradient(135deg, #2b1d4e, #c0587e, #f7c873)"], [qsTr("Đỏ vàng"), "linear-gradient(135deg, #6e1414, #e5a33a)"], [qsTr("Biển chiều"), "linear-gradient(135deg, #1b3a5c, #f2a65a)"]]],
  [qsTr("Ánh sáng"), [[qsTr("Trắng ấm"), "linear-gradient(135deg, #fff4e0, transparent)"], [qsTr("Vệt sáng"), "linear-gradient(90deg, transparent, #ffffff, transparent)"]]]
];
// Brush presets: groups of [name, [[name, size px, hardness %, note after the size], …]].
var BR_GROUPS = [
  [qsTr("Cơ bản"), [[qsTr("Tròn mềm"), 400, 0, ""], [qsTr("Tròn cứng"), 60, 100, ""], [qsTr("Tròn vừa"), 120, 50, ""]]],
  [qsTr("Retouch"), [[qsTr("Dodge & Burn mềm"), 300, 0, qsTr(" · Flow 6%")], [qsTr("Heal cứng"), 40, 100, ""], [qsTr("Mask tóc"), 24, 50, qsTr(" · lực nhấn")]]],
  [qsTr("Khô và mực"), [[qsTr("Mực khô"), 30, 80, ""], [qsTr("Than chì"), 12, 90, ""], [qsTr("Phấn"), 60, 40, ""]]]
];
// Brush tips: [name, hardness %, corner radius (CSS), angle °, roundness 0..1].
var TIPS = [[qsTr("Tròn mềm"), 0, "50%", 0, 1], [qsTr("Tròn cứng"), 100, "50%", 0, 1], [qsTr("Tròn vừa"), 50, "50%", 0, 1], [qsTr("Dẹt"), 100, "50%", -30, 0.45], [qsTr("Vuông"), 100, "3px", 0, 1], [qsTr("Hạt"), 20, "50%", 0, 1]];
// Brush Settings' sections: [key, name, on].
var BS_ROWS = [["shape", qsTr("Shape Dynamics"), 1], ["transfer", qsTr("Transfer"), 1], ["smooth", qsTr("Smoothing"), 1], ["build", qsTr("Build-up"), 0], ["noise", qsTr("Noise"), 0], ["wet", qsTr("Wet Edges"), 0]];
