# photo-beauty

Trình chỉnh ảnh kiểu Photoshop: C++20, Qt Quick, Vulkan/Slang. Kế hoạch ở `plan.md`.

- Đọc mục Cách phối hợp và dòng task của mình trong `docs/p0-tasks.md`; chỉ sửa file của task, không sửa `docs/p0-tasks.md`.
- Không phụ thuộc snapone, không ghi gì vào `~/snapone`. Cần mã của snapone thì đọc ở `~/snapone`, chép sang `core/` (namespace `pb`); từ đó là mã của photo-beauty.
- Thêm file là đủ: build lấy `core/*.cpp`, `tests/*.cpp`, `bench/*.cpp`, `shaders/*.slang` bằng glob. `TEST(test_x) { … }` (`tests/check.h`: `CHECK`, `pb::test::gpu_device()`) và `BENCH(x) { … }` (`bench/bench.h`: `report()` in `tên: giá trị đơn vị`) tự đăng ký. Kernel: `shaders/<tên>.slang` có entry `<tên>`, mã chung ở `shaders/lib/` (`import`), C++ lấy `#include "<tên>.spv.h"` (`<tên>_spv`, `<tên>_spv_sizeInBytes`).
- Code: namespace `pb`, comment tiếng Anh, zone Tracy ở đường nóng (`PROFILE_ZONE` trong `core/profile.h`; preset `profile`), kernel GPU có bản CPU cùng công thức và test so khớp. GCC 9 không có `std::span`, `<=>`, concepts, `std::bit_cast`, `std::format`, `jthread`, `atomic::wait`, `<ranges>`, `using enum`.

## Máy dev (WSL2, Ubuntu 20.04, GCC 9.4)

```sh
export VCPKG_ROOT=$HOME/vcpkg PATH=$HOME/.local/bin:$HOME/.local/opt/llvm-mingw-20250402-ucrt-ubuntu-20.04-x86_64/bin:$HOME/.local/opt/nasm-2.14.02/usr/bin:$PATH
tools/configure.sh default          # hay debug, asan, tsan, profile, mingw; đối số cmake thêm sau tên preset
tools/heavy.sh cmake --build build/default
ctest --test-dir build/default --output-on-failure
build/default/pb-tests test_smoke   # test theo tên; build/default/pb-bench [tên [đối số]]
```

- `tools/configure.sh` dựng `build/<preset>` của worktree chứa nó: `vcpkg_installed` chung mọi worktree (chỉ cài khi thiếu gói), ccache riêng `~/.cache/ccache-pb`, slangc chạy được trên glibc 2.31, `libtsan_preinit.o` cho TSan của GCC 9, link `samples/`.
- Build, bộ test sanitizer, test GPU dài, bench: qua `tools/heavy.sh` (một việc nặng mỗi lúc, chờ RAM trống 4 GB, `-j4`). Worktree chỉ giữ `build/default`; xóa `build/asan`, `build/tsan`, `build/mingw` ngay sau khi dùng; `df -h /mnt/c` dưới 2 GB thì dừng, báo lead. Tiến trình crash (đột biến, fuzz) để lại dump khoảng 200 MB ở `/mnt/c/Users/Teko/AppData/Local/Temp/wsl-crashes/` (tên chứa đường dẫn worktree): xóa dump của mình sau mỗi đợt, bằng đường dẫn đầy đủ.
- Sanitizer: chạy qua `setarch $(uname -m) -R` (runtime cũ hỏng với ASLR của kernel 6.6), cắt log (`| head -c 200000`):
  `setarch $(uname -m) -R env ASAN_OPTIONS=quarantine_size_mb=0:thread_local_quarantine_size_kb=0:detect_leaks=0 ctest --test-dir build/asan --output-on-failure`;
  `setarch $(uname -m) -R env TSAN_OPTIONS=suppressions=$PWD/tests/tsan.supp ctest --test-dir build/tsan --output-on-failure`.
- GPU: WSL chỉ có lavapipe. GPU thật (Iris Xe): build preset `mingw`, chép `.exe` vào `/mnt/c/Users/Teko/AppData/Local/Temp/pb-<mã task>/`, `cd` vào đó rồi chạy `./pb-tests.exe` (biến môi trường qua `WSLENV`; không truyền đường dẫn `/mnt/c` làm đối số); xóa thư mục đó khi xong. Số đo trên laptop dao động ±50%: chạy vài lần, lấy trung vị.
