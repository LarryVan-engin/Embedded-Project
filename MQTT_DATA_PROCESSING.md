# 📡 Quy Trình Xử Lý Dữ Liệu MQTT từ ESP32

## 🔄 Tổng Quan Quy Trình

```
ESP32 (Gửi MQTT)
    ↓
mqtt_client.py (Parse String Text)
    ↓
database_manager.py (Lưu vào SQLite)
    ↓
frontend/app.py (Hiển thị Dashboard)
    ↓
ai_engine/retraining_script.py (Học lại mô hình)
```

---

## 📤 Format Dữ Liệu từ ESP32

### Dữ liệu được gửi có định dạng text như sau:

```
[547034 ms] Published: TVOC=144.0ppb | Actual=1.86 | Predict=1.80
[548171 ms] [sensor:263] T=31.1 C  RH=46.9%
[550000 ms] [timer] LED2 toggles=1100
[550255 ms] [sensor:264] T=31.1 C  RH=46.3%
[552040 ms] [SensorSim_Read OK]
[552043 ms] [IAQ_Predict OK]
```

### Các trường dữ liệu được trích xuất:

| Trường | Ví dụ | Ghi chú |
|--------|-------|--------|
| **TVOC** | 144.0 ppb | Volatile Organic Compounds (ppb) |
| **Actual IAQ** | 1.86 | IAQ thực tế từ ZMOD4410 (1.0-5.0) |
| **Predicted IAQ** | 1.80 | IAQ dự đoán từ mô hình AI (1.0-5.0) |
| **Temperature** | 31.1°C | Nhiệt độ cảm biến |
| **Humidity** | 46.9% | Độ ẩm cảm biến |

---

## 🔧 Chi Tiết Xử Lý từng Bước

### 1️⃣ **MQTT Client** (`backend/mqtt_client.py`)

#### Chức năng:
- Kết nối tới MQTT Broker tại `localhost:1883`
- Subscribe topic: `iaq/node/data`
- Parse dữ liệu text từ payload

#### Quy trình parsing:

```python
def parse_mqtt_payload(self, payload_str):
    # Pattern 1: Trích xuất TVOC, Actual, Predict
    tvoc_pattern = r'TVOC=(\d+\.?\d*)\s*ppb'
    actual_pattern = r'Actual=(\d+\.?\d*)'
    predict_pattern = r'Predict=(\d+\.?\d*)'
    
    # Pattern 2: Trích xuất Temperature, Humidity
    temp_pattern = r'T=(\d+\.?\d*)\s*C'
    humidity_pattern = r'RH=(\d+\.?\d*)%'
    
    # Lưu trữ giá trị mới nhất để xử dụng khi chưa có trong payload hiện tại
    self.temperature = extracted_temp
    self.humidity = extracted_humidity
```

#### Xác thực dữ liệu:
- Chỉ lưu vào DB khi có đầy đủ: `tvoc`, `iaq_actual`, `iaq_forecast`
- Sử dụng giá trị temperature/humidity mới nhất nếu không có trong payload này

#### Trigger Retrain:
- Kiểm tra mỗi lần nhận dữ liệu
- Nếu tổng số sample ≥ 500 → Trigger học lại

---

### 2️⃣ **Database Manager** (`backend/database_manager.py`)

#### Schema bảng `air_quality_logs`:

```sql
CREATE TABLE air_quality_logs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp DATETIME,
    tvoc REAL,
    iaq_actual REAL,
    iaq_forecast REAL,
    temperature REAL,
    humidity REAL
)
```

#### Các hàm chính:

| Hàm | Mục đích |
|-----|---------|
| `initialize_db()` | Tạo bảng & migration cột mới |
| `save_log()` | Lưu 1 bản ghi từ MQTT |
| `get_data_count()` | Đếm tổng sample (dùng trigger retrain) |
| `get_latest_record()` | Lấy dữ liệu mới nhất (API /latest) |
| `get_history()` | Lấy lịch sử (API /history) |
| `get_data_for_retraining()` | Export DataFrame cho AI học lại |

#### Migration tự động:
```python
# Nếu bảng cũ không có temperature/humidity → Thêm cột
cursor.execute("ALTER TABLE air_quality_logs ADD COLUMN temperature REAL")
```

---

### 3️⃣ **Frontend Dashboard** (`frontend/app.py`)

#### Hiển thị:
- **IAQ Rating**: Dự báo IAQ (1.0-5.0)
- **Metrics**: TVOC, IAQ thực tế, sai số MAE, Temperature, Humidity
- **Biểu đồ UBA**: Vùng màu theo chuẩn (Xanh→Đỏ)
- **Biểu đồ TVOC**: Theo thời gian
- **Biểu đồ Environment**: Temperature & Humidity

---

### 4️⃣ **AI Retraining** (`ai_engine/retraining_script.py`)

#### Khi được trigger (mỗi 500 sample):

1. **Trích xuất dữ liệu**: Lấy DataFrame từ database
2. **Tính IAQ chuẩn**: Sử dụng `calculate_actual_iaq(tvoc)` làm Ground Truth
3. **Chuẩn hóa**: `StandardScaler.fit_transform(X)`
4. **Fine-tune model**: Epochs=15, Learning Rate=0.0001
5. **Xuất model**: TFLite → C-Array Header cho MCU

---

## ✅ Kiểm Tra Quy Trình

### Test 1: Xem log MQTT
```bash
# Terminal 1: Khởi động backend
python -m backend.main

# Terminal 2: Gửi test message
mosquitto_pub -h localhost -t "iaq/node/data" -m \
"[547034 ms] Published: TVOC=144.0ppb | Actual=1.86 | Predict=1.80"
```

### Test 2: Kiểm tra database
```bash
sqlite3 backend/iaq_history.db
SELECT * FROM air_quality_logs;
```

### Test 3: API
```bash
# Lấy record mới nhất
curl http://localhost:8000/api/v1/latest

# Lấy lịch sử (100 records)
curl http://localhost:8000/api/v1/history?limit=100
```

### Test 4: Dashboard
```bash
streamlit run frontend/app.py
# Mở http://localhost:8501
```

---

## 📊 Dữ Liệu Mẫu trong Database

```json
{
  "id": 1,
  "timestamp": "2024-04-26 10:30:45",
  "tvoc": 144.0,
  "iaq_actual": 1.86,
  "iaq_forecast": 1.80,
  "temperature": 31.1,
  "humidity": 46.9
}
```

---

## 🚀 Tối Ưu Hóa & Best Practices

### Performance:
- ✅ Chỉ parse khi có dữ liệu hoàn chỉnh
- ✅ Lưu temperature/humidity mới nhất (cảm biến có thể độc lập)
- ✅ Async threading cho retrain

### Data Quality:
- ✅ Validate regex pattern trước khi lưu
- ✅ Log các bản tin lỗi để debug
- ✅ Migration tự động khi schema thay đổi

### Storage:
- ✅ Optional: `clear_old_data()` để giới hạn DB size
- ✅ SQLite efficient cho edge device

---

## 🔗 File Liên Quan

- `backend/mqtt_client.py` - MQTT Parser
- `backend/database_manager.py` - Database Schema & Operations
- `backend/main.py` - FastAPI Server
- `frontend/app.py` - Streamlit Dashboard
- `ai_engine/retraining_script.py` - Model Retraining
- `ai_engine/model_exporter.py` - Export to C-Header
