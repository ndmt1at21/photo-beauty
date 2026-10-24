# photo-beauty

Trình chỉnh ảnh kiểu Photoshop: C++20, Qt Quick, Vulkan/Slang. Kế hoạch ở `plan.md`.

- Đọc mục Cách phối hợp và dòng task của mình trong `docs/p0-tasks.md`; chỉ sửa file của task, không sửa `docs/p0-tasks.md`.
- Không phụ thuộc snapone, không ghi gì vào `~/snapone`. Cần mã của snapone thì đọc ở `~/snapone`, chép sang `core/` (namespace `pb`); từ đó là mã của photo-beauty.
- Thêm file là đủ: build lấy `core/*.cpp`, `tests/*.cpp`, `bench/*.cpp`, `shaders/*.slang` bằng glob. `TEST(test_x) { … }` (`tests/check.h`: `CHECK`, `pb::test::gpu_device()`) và `BENCH(x) { … }` (`bench/bench.h`: `report()` in `tên: giá trị đơn vị`) tự đăng ký. Kernel: `shaders/<tên>.slang` có entry `<tên>`, mã chung ở `shaders/lib/` (`import`), C++ lấy `#include "<tên>.spv.h"` (`<tên>_spv`, `<tên>_spv_sizeInBytes`).
- Code: namespace `pb`, comment tiếng Anh, zone Tracy ở đường nóng (`PROFILE_ZONE` trong `core/profile.h`; preset `profile`), kernel GPU có bản CPU cùng công thức và test so khớp. GCC 9 không có `std::span`, `<=>`, concepts, `std::bit_cast`, `std::format`, `jthread`, `atomic::wait`, `<ranges>`, `using enum`.

## Máy dev (WSL2, Ubuntu-24.04, GCC 13.3; từ 22/10/2026, trước đó Ubuntu 20.04, GCC 9.4)

```sh
export VCPKG_ROOT=$HOME/vcpkg PATH=$HOME/.local/bin:$HOME/.local/opt/llvm-mingw-20250402-ucrt-ubuntu-20.04-x86_64/bin:$PATH
tools/configure.sh default          # hay debug, asan, tsan, profile, mingw; đối số cmake thêm sau tên preset
tools/heavy.sh cmake --build build/default
ctest --test-dir build/default --output-on-failure
build/default/pb-tests test_smoke   # test theo tên; build/default/pb-bench [tên [đối số]]
```

- `tools/configure.sh` dựng `build/<preset>` của worktree chứa nó (chỉ ở 24.04): `vcpkg_installed` chung mọi worktree (chỉ cài khi thiếu gói; preset ui, mingw, ui-mingw mỗi cái một thư mục riêng), ccache riêng `~/.cache/ccache-pb`, slangc của vcpkg, `libtsan_preinit.o` khi compiler thiếu, link `samples/`. Ubuntu-20.04 còn một bản chép cũ của repo, không đồng bộ: không dùng.
- Build, bộ test sanitizer, test GPU dài, bench: qua `tools/heavy.sh` (một việc nặng mỗi lúc, chờ RAM trống 4 GB, `-j4`). Worktree chỉ giữ `build/default`; xóa `build/asan`, `build/tsan`, `build/mingw` ngay sau khi dùng; `df -h /mnt/c` dưới 2 GB thì dừng, báo lead. Tiến trình crash (đột biến, fuzz) để lại dump khoảng 200 MB ở `/mnt/c/Users/Teko/AppData/Local/Temp/wsl-crashes/` (tên chứa đường dẫn worktree): xóa dump của mình sau mỗi đợt, bằng đường dẫn đầy đủ.
- Sanitizer: chạy qua `setarch $(uname -m) -R` (runtime cũ hỏng với ASLR của kernel 6.6), cắt log (`| head -c 200000`):
  `setarch $(uname -m) -R env ASAN_OPTIONS=quarantine_size_mb=0:thread_local_quarantine_size_kb=0:detect_leaks=0 ctest --test-dir build/asan --output-on-failure`;
  `setarch $(uname -m) -R env TSAN_OPTIONS=suppressions=$PWD/tests/tsan.supp ctest --test-dir build/tsan --output-on-failure`.
- GPU: WSL chỉ có lavapipe. GPU thật (Iris Xe): build preset `mingw`, chép `.exe` vào `/mnt/c/Users/Teko/AppData/Local/Temp/pb-<mã task>/`, `cd` vào đó rồi chạy `./pb-tests.exe` (biến môi trường qua `WSLENV`; không truyền đường dẫn `/mnt/c` làm đối số); xóa thư mục đó khi xong. Số đo trên laptop dao động ±50%: chạy vài lần, lấy trung vị.

## Giao diện (Qt 6.11.3, Ubuntu-24.04)

- Task giao diện (U, N) đọc `docs/ui-tasks.md` (mục Cách làm, dòng task của mình). Build, test, chụp ảnh chạy qua `tools/ui.sh <lệnh> [đối số]`: đặt `QT_ROOT_DIR` (`~/Qt/6.11.3/gcc_64` khi chưa đặt), chạy ở thư mục hiện tại, đối số giữ nguyên, trả mã thoát của lệnh; biến môi trường thì `tools/ui.sh env X=1 <lệnh>`. Worktree giao diện giữ `build/ui`. Từ thư mục của worktree:

```sh
tools/ui.sh tools/configure.sh ui                                      # một lần mỗi worktree
tools/ui.sh tools/heavy.sh cmake --build build/ui --target pb-ui pb-ui-tests
tools/ui.sh ctest --test-dir build/ui -R '^ui$' --output-on-failure    # trước merge một lượt; 4 lượt (--repeat until-fail:4) ở đợt tích hợp
tools/ui.sh build/ui/ui/pb-ui-tests -input ui/tests/tst_u0.qml         # một file; một hàm: thêm U0::test_window (tên TestCase::hàm)
tools/ui.sh tools/heavy.sh cmake --build build/ui --target all_qmllint  # sạch là không có dòng Warning (đích vẫn thoát 0)
tools/ui.sh build/ui/ui/pb-ui --size 1440x900 --screenshot /home/ndmt1at21/photo-beauty/build/shots/<mã>/x.png   # --state, --theme light, --settle ms
```

- QML không phải build lại: thêm `-import build/ui/ui/live` vào `pb-ui-tests` hay `pb-ui` (như `pb-ui-tests -import build/ui/ui/live -input ui/tests/tst_u2.qml`), module lấy thẳng `ui/qml/*` của worktree thay bản đã biên dịch. Thêm hay xóa file QML, JS thì build lại một lần (`live/` dựng lúc configure); C++ thì luôn build.
- Module QML `PhotoBeauty` (`import PhotoBeauty`), thư viện tĩnh `pb_qml`; `pb-ui`, `pb-ui-tests` link `pb_qmlplugin`; `pb_qml` link `pb_core` (canvas, V1). Thêm file là đủ (glob; `cmake --build` tự configure lại): `ui/*.cpp`, `ui/*.h` vào module (`main.cpp` là của `pb-ui`), QML, JS phẳng trong `ui/qml/`, file có dòng `pragma Singleton` là singleton (thêm dòng đó vào file đã có thì chạy lại `tools/ui.sh tools/configure.sh ui`), `ui/tests/tst_*.qml` đọc từ cây nguồn lúc chạy (`pb-ui-tests` không phải build lại).
- Resource: `ui/fonts/*.ttf`, `*.otf` (`look.h` nạp hết, QML gọi theo tên họ font), `ui/mock/photos/*`, `ui/brand/*.png`, `*.svg`; QML gọi bằng đường dẫn tuyệt đối `qrc:/qt/qml/PhotoBeauty/` + đường dẫn dưới `ui/`, như `qrc:/qt/qml/PhotoBeauty/mock/photos/x.jpg` (đường dẫn tương đối hỏng với `-import`).
- `main.cpp` đưa vào `Main` qua initial properties `startState` (`--state`), `startTheme` (`--theme dark|light`, "" khi không có), `persist` (true khi chạy thường; false với `--screenshot` và trong test); `--size WxH`, `--settle ms`, `--screenshot png` (tạo thư mục, thoát 0 khi lưu được) xử lý trong `main.cpp`. `pb-ui --screenshot` và `pb-ui-tests` chạy offscreen (scene graph phần mềm như CI) trừ khi đặt `QT_QPA_PLATFORM`.
- Bản Windows: preset `ui-mingw` build chéo ở 24.04 (Qt `~/Qt-win/6.11.3/llvm-mingw_64` link động, đổi bằng `QT_WIN_DIR`; llvm-mingw của preset `mingw`; `vcpkg_installed-ui-mingw` riêng), xóa `build/ui-mingw` khi xong; `ui/deploy-win.sh <thư mục>` gom exe, DLL, plugin, module QML, font dự phòng và `tests\` (~85 MB). Chạy trong `/mnt/c/Users/Teko/AppData/Local/Temp/pb-<mã>/` (xóa khi xong); exe không in gì ra terminal của WSL, nên test ghi kết quả ra file:

```sh
tools/ui.sh tools/configure.sh ui-mingw            # lần đầu trên máy ~6 phút (vcpkg dựng 10 gói mingw), sau đó ~45 s
tools/ui.sh tools/heavy.sh cmake --build build/ui-mingw --target pb-ui pb-ui-tests
tools/ui.sh ui/deploy-win.sh /mnt/c/Users/Teko/AppData/Local/Temp/pb-<mã>
cd /mnt/c/Users/Teko/AppData/Local/Temp/pb-<mã> && ./pb-ui-tests.exe -input tests -o out.txt,txt; cat out.txt
./pb-ui.exe --size 1440x900 --screenshot x.png     # offscreen như Linux; HiDPI 150%: QT_SCALE_FACTOR=1.5 WSLENV=QT_SCALE_FACTOR; cửa sổ thật (D3D11): QT_QPA_PLATFORM=windows WSLENV=QT_QPA_PLATFORM
```
