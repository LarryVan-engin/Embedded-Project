# 🧪 Hướng Dẫn Test Quy Trình MQTT

## 📋 Yêu Cầu

1. **Python 3.8+**
2. **MQTT Broker** (Mosquitto hoặc Eclipse Mosquitto)
3. **Các package**: paho-mqtt, fastapi, streamlit, tensorflow, scikit-learn

## 🚀 Các Bước Setup

### 1. Cài Đặt Mosquitto (MQTT Broker)

**Windows:**
```bash
# Dùng Chocolatey
choco install mosquitto

# Hoặc tải từ: https://mosquitto.org/download/
```

**Linux/Mac:**
```bash
# Ubuntu/Debian
sudo apt-get install mosquitto mosquitto-clients

# Mac
brew install mosquitto
```

### 2. Khởi Động Mosquitto Broker

```bash
# Windows (từ folder installation)
mosquitto -c mosquitto.conf

# Linux/Mac
mosquitto -p 1883
```

### 3. Kích Hoạt Python Virtual Environment

```bash
# Windows
.\.env\Scripts\Activate

# Linux/Mac
source .env/bin/activate
```

---

## 🧪 Test 1: Kiểm Tra Parser MQTT

### Chạy Unit Tests:
```bash
cd e:\gitclone\Embedded_clone\Embedded-Project
python test_mqtt_pipeline.py
```

### Output mong đợi:
```
✅ Pattern 'TVOC=(\d+\.?\d*)\s*ppb' matches 'TVOC=144.0ppb' = 144.0
✅ Valid payload contains all required fields: True
✅ ALL TESTS PASSED!
```

---

## 🧪 Test 2: Kiểm Tra Backend

### Terminal 1: Khởi động Backend
```bash
cd e:\gitclone\Embedded_clone\Embedded-Project
python -m backend.main
```

### Output mong đợi:
```
🚀 Khởi tạo Database...
📡 Đang khởi động MQTT Client Service...
✅ MQTT Connected to Broker
```

### Terminal 2: Gửi Test Message

```bash
# Test 1: Gửi dữ liệu TVOC + IAQ
mosquitto_pub -h localhost -t "iaq/node/data" -m "[547034 ms] Published: TVOC=144.0ppb | Actual=1.86 | Predict=1.80"

# Test 2: Gửi dữ liệu Temperature + Humidity
mosquitto_pub -h localhost -t "iaq/node/data" -m "[548171 ms] [sensor:263] T=31.1 C  RH=46.9%"

# Test 3: Gửi dữ liệu hoàn chỉnh (TVOC + IAQ + Temp + Humidity cùng lúc)
mosquitto_pub -h localhost -t "iaq/node/data" -m "[550255 ms] Published: TVOC=152.0ppb | Actual=1.92 | Predict=1.88 [sensor:265] T=31.2 C RH=47.0%"
```

### Terminal 1: Kiểm tra log Backend
```
📨 Raw MQTT: [547034 ms] Published: TVOC=144.0ppb | Actual=1.86 | Predict=1.80
✅ Parsed: {'tvoc': 144.0, 'iaq_actual': 1.86, 'iaq_forecast': 1.80}
✅ Saved: TVOC=144.0ppb, Actual=1.86, Predict=1.80
```

---

## 🧪 Test 3: Kiểm Tra API FastAPI

### Lấy data mới nhất:
```bash
curl http://localhost:8000/api/v1/latest
```

### Response mong đợi:
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

### Lấy lịch sử (100 records):
```bash
curl http://localhost:8000/api/v1/history?limit=100
```

---

## 🧪 Test 4: Kiểm Tra Database SQLite

### Terminal 3: Kiểm tra dữ liệu trong DB
```bash
sqlite3 backend/iaq_history.db
```

### Trong sqlite3 console:
```sql
-- Xem schema bảng
.schema air_quality_logs

-- Xem tất cả dữ liệu
SELECT * FROM air_quality_logs;

-- Xem 5 record mới nhất
SELECT * FROM air_quality_logs ORDER BY id DESC LIMIT 5;

-- Đếm tổng record
SELECT COUNT(*) FROM air_quality_logs;

-- Tính thống kê TVOC
SELECT 
    MIN(tvoc) as min_tvoc,
    MAX(tvoc) as max_tvoc,
    AVG(tvoc) as avg_tvoc
FROM air_quality_logs;

-- Xem sai số (Error) giữa Actual và Predict
SELECT 
    id, 
    timestamp,
    iaq_actual,
    iaq_forecast,
    ABS(iaq_actual - iaq_forecast) as error
FROM air_quality_logs;
```

---

## 🧪 Test 5: Kiểm Tra Frontend Dashboard

### Terminal 3: Khởi động Streamlit
```bash
streamlit run frontend/app.py
```

### Output mong đợi:
```
  You can now view your Streamlit app in your browser.

  Local URL: http://localhost:8501
```

### Mở browser:
```
http://localhost:8501
```

### Kiểm tra:
- ✅ Thấy "Dự báo IAQ (1.0 - 5.0): [value]"
- ✅ Metrics hiển thị: TVOC, IAQ Thực tế, Sai số MAE, Temperature, Humidity
- ✅ Biểu đồ UBA với vùng màu
- ✅ Biểu đồ TVOC theo thời gian
- ✅ Biểu đồ Temperature & Humidity

---

## 🔄 Test 6: Trigger Retrain (Optional)

### Để trigger learning tự động, cần ≥ 500 sample:

```bash
# Tạo script gửi 500+ messages
cat > send_test_data.sh << 'EOF'
#!/bin/bash
for i in {1..550}; do
  tvoc=$(echo "$RANDOM % 300 + 100" | bc)
  iaq_actual=$(echo "scale=2; $RANDOM / 32700 * 3 + 1" | bc)
  iaq_predict=$(echo "scale=2; $RANDOM / 32700 * 3 + 1" | bc)
  temp=$(echo "scale=1; $RANDOM % 100 / 10 + 15" | bc)
  humidity=$(echo "scale=1; $RANDOM % 100" | bc)
  
  msg="[$((i * 1000)) ms] Published: TVOC=${tvoc}.0ppb | Actual=${iaq_actual} | Predict=${iaq_predict}"
  if [ $((i % 2)) -eq 0 ]; then
    msg="$msg [sensor:$((i % 300))] T=${temp} C RH=${humidity}%"
  fi
  
  mosquitto_pub -h localhost -t "iaq/node/data" -m "$msg"
  sleep 0.1
done
EOF

chmod +x send_test_data.sh
./send_test_data.sh
```

### Kiểm tra retrain:
- Backend sẽ log: `🔄 Triggering retraining at 500 samples...`
- Kiểm tra files mới: `ls -la updates/`
- Xem: `updates/iaq_model_data.h` và `updates/scaler_constants.h`

---

## 📊 Test Data Samples

### Mẫu 1: Không khí tốt
```bash
mosquitto_pub -h localhost -t "iaq/node/data" -m "[1000 ms] Published: TVOC=50.0ppb | Actual=1.2 | Predict=1.25"
mosquitto_pub -h localhost -t "iaq/node/data" -m "[1100 ms] [sensor:1] T=22.5 C  RH=45.0%"
```

### Mẫu 2: Không khí trung bình
```bash
mosquitto_pub -h localhost -t "iaq/node/data" -m "[2000 ms] Published: TVOC=200.0ppb | Actual=3.0 | Predict=2.95"
mosquitto_pub -h localhost -t "iaq/node/data" -m "[2100 ms] [sensor:1] T=26.0 C  RH=55.0%"
```

### Mẫu 3: Không khí xấu
```bash
mosquitto_pub -h localhost -t "iaq/node/data" -m "[3000 ms] Published: TVOC=500.0ppb | Actual=4.5 | Predict=4.4"
mosquitto_pub -h localhost -t "iaq/node/data" -m "[3100 ms] [sensor:1] T=30.0 C  RH=70.0%"
```

---

## ✅ Checklist Kiểm Tra

- [ ] Mosquitto broker khởi động thành công
- [ ] `test_mqtt_pipeline.py` pass tất cả tests
- [ ] Backend connected to MQTT
- [ ] Dữ liệu lưu vào database SQLite
- [ ] API /latest và /history hoạt động
- [ ] Streamlit dashboard hiển thị đúng
- [ ] Temperature & Humidity được lưu
- [ ] Error/sai số được tính chính xác
- [ ] (Optional) Retrain trigger hoạt động tại 500 samples

---

## 🐛 Troubleshooting

### Lỗi: "Connection refused"
```
❌ MQTT Connected to Broker: Connection refused
```
**Giải pháp**: Chắc chắn Mosquitto broker đã khởi động
```bash
mosquitto -p 1883
```

### Lỗi: "Failed to import paho"
```
❌ ModuleNotFoundError: No module named 'paho'
```
**Giải pháp**: Cài đặt paho-mqtt
```bash
pip install paho-mqtt
```

### Database lock error
```
❌ database is locked
```
**Giải pháp**: Đóng tất cả kết nối SQLite khác
```bash
# Xóa lock file
rm backend/iaq_history.db-wal
rm backend/iaq_history.db-shm
```

### Streamlit port đã dùng
```
❌ Error: Could not find open port
```
**Giải pháp**: Dùng port khác
```bash
streamlit run frontend/app.py --server.port 8502
```

---

## 📖 Tài Liệu Tham Khảo

- [MQTT Protocol](https://mqtt.org/)
- [Mosquitto Documentation](https://mosquitto.org/documentation/)
- [Paho-MQTT Python](https://github.com/eclipse/paho.mqtt.python)
- [FastAPI Documentation](https://fastapi.tiangolo.com/)
- [Streamlit Documentation](https://docs.streamlit.io/)
