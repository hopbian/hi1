# Hướng dẫn vận hành Hộp Tình Yêu Thương Mại (Bảo mật Tài khoản + Cloudflare + GitHub Pages)

## 1. Kiến trúc hệ thống
- **Trang Web chính (Frontend):** `https://hopbian.github.io/hi1/` (0đ vĩnh viễn trên GitHub Pages).
- **Máy chủ API & Lưu trữ bảo mật (Backend):** `https://lovebox-api.tonepite.workers.dev` (Cloudflare Workers + KV — 0đ, chạy 24/24 toàn cầu không cần bật máy tính).
- **Chế độ bảo mật:**
  - Người ngoài vào web chỉ thấy màn hình **Đăng nhập** (tuyệt đối không tự tạo tài khoản được, không xem trộm được ảnh của bất kỳ hộp nào).
  - Mọi ảnh gửi lên được mã hóa kích thước `240x320` JPEG và lưu riêng theo từng `boxId` trên Cloudflare KV.

## 2. Cách Admin cấp tài khoản cho khách mua hộp
1. Mở `https://hopbian.github.io/hi1/` và đăng nhập bằng **Tài khoản Admin** (đã được lưu bảo mật trong **Cloudflare Worker Secrets**, không lưu trên GitHub).
2. Vào mục **👑 Quản Lý Tài Khoản & Hộp Thương Mại**:
   - Nhập **Tài khoản**, **Mật khẩu** bạn tự đặt và **Mã Hộp** (VD: `LOVE01`, `LOVE02`...).
   - Bấm **✨ Tạo Tài Khoản Khách**.
   - Hệ thống sẽ tự sinh một **`Device Key`** riêng cho hộp đó.
3. Giao lại cho khách: **Link web `https://hopbian.github.io/hi1/` + Tài khoản + Mật khẩu** vừa tạo.
   - Bạn có thể **Đổi mật khẩu**, **Khóa/Mở khóa**, hoặc **Xóa** tài khoản khách bất cứ lúc nào.

## 3. Cách nạp code cho mỗi chiếc Hộp ESP32 (240x320 TFT SPI)
1. Mở file `esp32_lovebox_github.ino` bằng Arduino IDE.
2. Điền thông tin Wi-Fi (`WIFI_SSID`, `WIFI_PASSWORD`), `BOX_ID` (VD: `LOVE01`) và `DEVICE_KEY` tương ứng lấy từ bảng Admin.
3. Nạp code lần đầu qua USB; các lần sau nạp code không dây qua Wi-Fi (`ArduinoOTA`).
