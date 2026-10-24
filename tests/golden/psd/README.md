# PSD thử của Photoshop (E33)

File của `tools/ps-golden/psd.jsx` (các bước, danh sách file, quy ước: `tools/ps-golden/README-psd.md`). Đừng sửa tay: sửa script rồi chạy lại.

- `cases.json`: các case như script định dựng, trước khi có Photoshop (`jscheck --eval "E.offline('psd')"`); sinh lại mỗi khi sửa case.
- Lượt một (bạn chạy ở B33): `<case>.psd` (57 file, mỗi file một tính năng P1), `<case>.png` (ảnh ghép của Photoshop ở độ sâu tài liệu: 8 bit ra 8, 16 và 32 bit ra 16; CMYK, Lab là bản sRGB 16 bit), `rgb32.tif` (ảnh ghép số thực 32 bit), `e33-warm.cube` (LUT của `adj-color-lookup`), `index.json` (mỗi case: cây layer theo khóa JSON của photo-beauty như Photoshop đã dựng và đọc lại, header của file, PNG; case hỏng ở `failed`), `photoshop.txt` (phiên bản, Color Settings, từng layer như Photoshop có). Khoảng 4,5 MB.
- `read/` (lượt hai, sau E34): Photoshop mở các file photo-beauty lưu; ảnh ghép, cây layer như Photoshop đọc, `crc32` của file, `index.json`, `photoshop.txt`.

PSB rộng 31.000 px ở `samples/psd/` (ngoài git). Test của M40–M47, E34 bỏ qua có lý do khi chưa có file.
