# Hướng dẫn đưa Hộp Tình Yêu lên `github.io` (Miễn phí 0đ & Cố định vĩnh viễn)

## Bước 1: Tạo Repository trên GitHub
1. Đăng nhập GitHub → bấm **New repository** (`+` góc trên bên phải).
2. Đặt tên repo, ví dụ: `hop-tinh-yeu`
3. Chọn **Public** (để ESP32 tải ảnh không cần token, và GitHub Pages chạy miễn phí).
4. Bấm **Create repository**.

---

## Bước 2: Đẩy thư mục `hop-tinh-yeu-github` lên Repo đó
Mở PowerShell tại thư mục `d:\build lai dt cu thanh bot tele ko bao h bi sap\hop-tinh-yeu-github` và chạy:
```bash
git init
git add .
git commit -m "Khoi tao Hop Tinh Yeu Cloud"
git branch -M main
git remote add origin https://github.com/<TEN_TAI_KHOAN_CUA_BAN>/hop-tinh-yeu.git
git push -u origin main
```

---

## Bước 3: Bật GitHub Pages (`*.github.io`)
1. Vào Repo vừa tạo trên GitHub → chọn tab **Settings** → bên trái chọn **Pages**.
2. Ở mục **Build and deployment** → **Branch**: chọn **`main`** và thư mục **`/ (root)`** → bấm **Save**.
3. Chờ khoảng 30–60 giây, trang web cố định vĩnh viễn của bạn sẽ hiện ra tại:
   👉 **`https://<TEN_TAI_KHOAN_CUA_BAN>.github.io/hop-tinh-yeu/`**

---

## Bước 4: Tạo Chìa Khóa (Fine-grained Token) để Web gửi ảnh lên Hộp
1. Trên GitHub, bấm Avatar góc phải → **Settings** → kéo xuống dưới cùng chọn **Developer settings** → **Personal access tokens** → **Fine-grained tokens** → **Generate new token**.
2. Đặt tên: `HopTinhYeu-Key`, Expiration: **No expiration** (hoặc 1 năm).
3. Ở mục **Repository access**: chọn **Only select repositories** → chọn đúng repo `hop-tinh-yeu` (rất an toàn, token không chạm được vào các repo khác của bạn!).
4. Ở mục **Permissions** → **Repository permissions** → dòng **Contents**: chọn **Read and write**.
5. Bấm **Generate token** và copy đoạn mã `github_pat_...`.

---

## Bước 5: Kết nối Web & Tạo Link Tặng Người Yêu / Khách Hàng
1. Mở trang web **`https://<TEN_TAI_KHOAN_CUA_BAN>.github.io/hop-tinh-yeu/`**.
2. Bấm **⚙️ Cài đặt & Chia sẻ** → dán **GitHub Owner**, **Tên Repo**, **Mã Hộp (`LOVE01`)**, và **Token** → bấm **Lưu Cài Đặt**.
3. Copy đường link ở ô **Link Tặng Người Yêu / Khách Hàng** (có dạng `.../?box=LOVE01#key=github_pat_...`) gửi cho người yêu hoặc tạo mã QR dán vào Hộp. Người nhận chỉ cần mở link 1 lần là từ sau up ảnh trực tiếp ngay!

---

## Bước 6: Nạp code vào ESP32
1. Mở file `esp32_lovebox_github.ino` bằng Arduino IDE.
2. Sửa `WIFI_SSID`, `WIFI_PASSWORD`, `GH_OWNER` (tên tài khoản GitHub của bạn), `GH_REPO` (`hop-tinh-yeu`).
3. Nạp vào ESP32 lần đầu bằng cáp USB (những lần sau nạp không dây qua Wi-Fi OTA!).
