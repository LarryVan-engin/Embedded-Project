# 🔍 Chi Tiết Các Thay Đổi theo File

## 📄 backend/mqtt_client.py

### 🔴 Vấn đề Cũ
```python
# Cố parse JSON nhưng dữ liệu không phải JSON
payload = json.loads(msg.payload.decode())
# → ValueError: Expecting value
```

### 🟢 Giải Pháp Mới
```python
import re

def parse_mqtt_payload(self, payload_str):
    """Parse text payload với regex patterns"""
    data = {}
    
    # Pattern 1: TVOC, Actual, Predict
    tvoc_pattern = r'TVOC=(\d+\.?\d*)\s*ppb'
    actual_pattern = r'Actual=(\d+\.?\d*)'
    predict_pattern = r'Predict=(\d+\.?\d*)'
    
    # Pattern 2: Temperature, Humidity
    temp_pattern = r'T=(\d+\.?\d*)\s*C'
    humidity_pattern = r'RH=(\d+\.?\d*)%'
    
    # Extract từ payload
    tvoc_match = re.search(tvoc_pattern, payload_str)
    if tvoc_match:
        data['tvoc'] = float(tvoc_match.group(1))
    
    # ... similar cho các fields khác
    
    return data
```

### 🔄 Quy Trình Xử Lý
```
Input: "[547034 ms] Published: TVOC=144.0ppb | Actual=1.86 | Predict=1.80"
                            ↓
                   regex.search()
                            ↓
Output: {
    'tvoc': 144.0,
    'iaq_actual': 1.86,
    'iaq_forecast': 1.80
}
```

### ✨ Thêm Mới
- `self.temperature` - Lưu giá trị temperature mới nhất
- `self.humidity` - Lưu giá trị humidity mới nhất
- Validation: Chỉ lưu khi có đầy đủ TVOC, Actual, Predict
- Improved logging với emoji & detailed messages

---

## 📄 backend/database_manager.py

### 🔴 Schema Cũ
```sql
CREATE TABLE air_quality_logs (
    id INTEGER PRIMARY KEY,
    timestamp DATETIME,
    eco2 REAL,                 -- ❌ Không dùng
    tvoc REAL,
    iaq_actual REAL,
    iaq_forecast REAL
    -- ❌ Thiếu temperature & humidity
)
```

### 🟢 Schema Mới
```sql
CREATE TABLE air_quality_logs (
    id INTEGER PRIMARY KEY,
    timestamp DATETIME,
    tvoc REAL,                 -- ✅
    iaq_actual REAL,          -- ✅
    iaq_forecast REAL,        -- ✅
    temperature REAL,         -- ✅ MỚI
    humidity REAL             -- ✅ MỚI
)
```

### 🔄 Hàm `save_log()` - Trước & Sau

**Trước:**
```python
def save_log(self, eco2, tvoc, iaq_forecast, iaq_actual):
    # Tham số thứ tự hỗn loạn
    cursor.execute('''
        INSERT INTO air_quality_logs 
        (timestamp, eco2, tvoc, iaq_actual, iaq_forecast)
        VALUES (?, ?, ?, ?, ?)
    ''', (now, eco2, tvoc, iaq_actual, iaq_forecast))
```

**Sau:**
```python
def save_log(self, tvoc, iaq_actual, iaq_forecast, temperature=None, humidity=None):
    # Tham số rõ ràng, có default
    cursor.execute('''
        INSERT INTO air_quality_logs 
        (timestamp, tvoc, iaq_actual, iaq_forecast, temperature, humidity)
        VALUES (?, ?, ?, ?, ?, ?)
    ''', (now, tvoc, iaq_actual, iaq_forecast, temperature, humidity))
```

### ✨ Thêm Mới: Auto Migration
```python
def initialize_db(self):
    # ... create table ...
    
    # Kiểm tra nếu bảng cũ không có các cột mới
    cursor.execute("PRAGMA table_info(air_quality_logs)")
    columns = {row[1] for row in cursor.fetchall()}
    
    if 'temperature' not in columns:
        cursor.execute("ALTER TABLE air_quality_logs ADD COLUMN temperature REAL")
        print("⚠️ Migrating database: Adding temperature column...")
    
    if 'humidity' not in columns:
        cursor.execute("ALTER TABLE air_quality_logs ADD COLUMN humidity REAL")
        print("⚠️ Migrating database: Adding humidity column...")
```

### 📊 Kết Quả
- Database tự động cập nhật schema
- Không mất dữ liệu cũ
- Backward compatible

---

## 📄 frontend/app.py

### 🔴 Vấn Đề Cũ
```python
# Import thiếu pandas
import streamlit as st
import plotly.graph_objects as go

# Chỉ hiển thị TVOC, eCO2, IAQ
c1.metric("TVOC (ppb)", f"{latest['tvoc']:.0f}")
c2.metric("eCO2 (ppm)", f"{latest['eco2']:.0f}")  # ❌ Không có eCO2 nữa
c3.metric("📉 Sai số MAE", f"{df['error'].mean():.3f}")

# ❌ Không hiển thị Temperature & Humidity
```

### 🟢 Giải Pháp Mới

**Import:**
```python
import pandas as pd  # ✅ THÊM
```

**Metrics:**
```python
c1, c2, c3, c4, c5 = st.columns(5)  # 5 columns thay vì 3
c1.metric("TVOC (ppb)", f"{latest['tvoc']:.0f}")
c2.metric("IAQ Thực tế", f"{latest['iaq_actual']:.2f}")     # ✅ THÊM
c3.metric("📊 Sai số MAE", f"{df['error'].mean():.3f}")

# ✅ THÊM Temperature & Humidity
if 'temperature' in df.columns and pd.notna(latest['temperature']):
    c4.metric("🌡️ Temperature", f"{latest['temperature']:.1f}°C")

if 'humidity' in df.columns and pd.notna(latest['humidity']):
    c5.metric("💧 Humidity", f"{latest['humidity']:.1f}%")
```

**Biểu đồ Mới:**
```python
# ✅ TVOC Chart
fig_tvoc = go.Figure()
fig_tvoc.add_trace(go.Scatter(
    x=df['timestamp'], y=df['tvoc'], 
    name="TVOC (ppb)", 
    fill='tozeroy'
))
st.plotly_chart(fig_tvoc, use_container_width=True)

# ✅ Environment Chart
fig_env = go.Figure()
fig_env.add_trace(go.Scatter(
    x=df['timestamp'], y=df['temperature'], 
    name="Temperature (°C)"
))
fig_env.add_trace(go.Scatter(
    x=df['timestamp'], y=df['humidity'], 
    name="Humidity (%)"
))
st.plotly_chart(fig_env, use_container_width=True)
```

### 📊 Dashboard Trước & Sau

**Trước:**
```
IAQ Rating: 1.80
Metrics: TVOC | eCO2 | MAE
Chart: IAQ Prediction (UBA)
```

**Sau:**
```
IAQ Rating: 1.80
Metrics: TVOC | IAQ Actual | MAE | Temperature | Humidity
Charts:
  - IAQ Prediction (UBA)
  - TVOC Time Series
  - Temperature & Humidity Time Series
```

---

## 📄 ai_engine/retraining_script.py

### ✅ Không cần thay đổi!
- Model training vẫn dùng TVOC (chính xác)
- Function `calculate_actual_iaq()` vẫn tính từ TVOC
- Fine-tuning quy trình vẫn giống cũ
- Export model vẫn giống cũ

**Ghi chú:**
```python
# Dữ liệu từ database
df = db_manager.get_data_for_retraining()
# Schema mới sẽ tự động return temperature, humidity
# (chúng không được dùng trong model, nhưng không gây lỗi)

X = df[['tvoc']].values  # ✅ Vẫn chỉ dùng TVOC
y = np.array([calculate_actual_iaq(val[0]) for val in X])
```

---

## 📄 Environmental_EdgeAI.py & environmental-edgeai.ipynb

### ✅ Không cần thay đổi!
- Đây là training script, không dùng MQTT
- Dùng CSV dataset
- Không liên quan đến quy trình xử lý real-time

---

## 📊 Bảng So Sánh Chi Tiết

| Thành Phần | Trước | Sau | Ghi Chú |
|-----------|-------|-----|--------|
| **mqtt_client.py** | json.loads() | regex parsing | ✅ Parse text format |
| **mqtt_client.py** | Không lưu Temp/Humid | Lưu Temp/Humid | ✅ Thêm fields |
| **mqtt_client.py** | Lỗi nếu JSON invalid | Lỗi nếu format invalid | ✅ Better validation |
| **database_manager.py** | eco2 column | Xóa eco2 | ✅ Cleanup unused |
| **database_manager.py** | Không có Temp/Humid | Có Temp/Humid columns | ✅ New columns |
| **database_manager.py** | Signature: (eco2, tvoc, ...) | Signature: (tvoc, ...) | ✅ Clear params |
| **database_manager.py** | Cố định schema | Auto migration | ✅ Flexible |
| **frontend/app.py** | 3 columns metrics | 5 columns metrics | ✅ More info |
| **frontend/app.py** | 1 biểu đồ | 3 biểu đồ | ✅ Rich visualization |
| **ai_engine/** | N/A | N/A | ✅ No changes |

---

## 🔗 Dependencies

### Thêm:
- `re` (regex) - Built-in Python

### Giữ nguyên:
- `paho-mqtt`
- `sqlite3`
- `pandas`
- `tensorflow`
- `scikit-learn`
- `fastapi`
- `streamlit`
- `plotly`

---

## 🚀 Cách Cập Nhật Code

### Nếu dùng Git:
```bash
git pull origin main
```

### Nếu cập nhật thủ công:
1. **backend/mqtt_client.py** - Copy toàn bộ file
2. **backend/database_manager.py** - Copy toàn bộ file
3. **frontend/app.py** - Copy toàn bộ file
4. **test_mqtt_pipeline.py** - File mới (optional)
5. **Documentation files** - File mới (reference only)

### Xóa database cũ (optional, để test migration):
```bash
rm backend/iaq_history.db
```

### Chạy unit tests:
```bash
python test_mqtt_pipeline.py
```

---

## ✅ Verification

Sau khi cập nhật, verify:

- [ ] Backend khởi động thành công
- [ ] MQTT topics được subscribe
- [ ] Dữ liệu được parse đúng (xem logs)
- [ ] Database tự động migrate (nếu cần)
- [ ] API response có temperature/humidity
- [ ] Streamlit dashboard hiển thị 5 columns
- [ ] Các biểu đồ mới hiển thị

---

## 📞 Troubleshooting

### Nếu gặp lỗi "eco2" column not found:
```python
# Solution: Delete database để tạo lại fresh
rm backend/iaq_history.db
```

### Nếu Parser không match regex:
```python
# Check: Copy payload vào test_mqtt_pipeline.py
python test_mqtt_pipeline.py
```

### Nếu Streamlit error:
```python
# Restart kernel
streamlit run frontend/app.py --logger.level=debug
```
