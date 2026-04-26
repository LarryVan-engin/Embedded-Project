#!/bin/bash
# 🧪 MQTT Test Messages
# Gửi các test messages để kiểm tra quy trình xử lý

echo "📡 MQTT Test Messages - Copy & Paste vào Terminal"
echo "=================================================="

echo ""
echo "✅ TEST 1: Dữ liệu hoàn chỉnh (TVOC + IAQ)"
echo "mosquitto_pub -h localhost -t \"iaq/node/data\" -m \"[547034 ms] Published: TVOC=144.0ppb | Actual=1.86 | Predict=1.80\""

echo ""
echo "✅ TEST 2: Dữ liệu environment (Temperature + Humidity)"
echo "mosquitto_pub -h localhost -t \"iaq/node/data\" -m \"[548171 ms] [sensor:263] T=31.1 C  RH=46.9%\""

echo ""
echo "✅ TEST 3: Không khí tốt"
echo "mosquitto_pub -h localhost -t \"iaq/node/data\" -m \"[550000 ms] Published: TVOC=50.0ppb | Actual=1.2 | Predict=1.25\""

echo ""
echo "✅ TEST 4: Không khí trung bình"
echo "mosquitto_pub -h localhost -t \"iaq/node/data\" -m \"[551000 ms] Published: TVOC=200.0ppb | Actual=3.0 | Predict=2.95\""

echo ""
echo "✅ TEST 5: Không khí xấu"
echo "mosquitto_pub -h localhost -t \"iaq/node/data\" -m \"[552000 ms] Published: TVOC=500.0ppb | Actual=4.5 | Predict=4.4\""

echo ""
echo "✅ TEST 6: Nhiệt độ thấp"
echo "mosquitto_pub -h localhost -t \"iaq/node/data\" -m \"[553000 ms] [sensor:264] T=5.0 C  RH=30.0%\""

echo ""
echo "✅ TEST 7: Độ ẩm cao"
echo "mosquitto_pub -h localhost -t \"iaq/node/data\" -m \"[554000 ms] [sensor:265] T=28.0 C  RH=85.5%\""

echo ""
echo "✅ TEST 8: TVOC rất cao"
echo "mosquitto_pub -h localhost -t \"iaq/node/data\" -m \"[555000 ms] Published: TVOC=1000.0ppb | Actual=5.0 | Predict=4.9\""

echo ""
echo "=================================================="
echo "💡 Lưu ý:"
echo "  1. Mở 3 terminal:"
echo "     - Terminal 1: mosquitto (MQTT Broker)"
echo "     - Terminal 2: python -m backend.main (Backend)"
echo "     - Terminal 3: Copy & paste các lệnh mosquitto_pub ở dưới"
echo ""
echo "  2. Kiểm tra:"
echo "     - Terminal 2 sẽ show log \"✅ Saved\""
echo "     - SQLite: sqlite3 backend/iaq_history.db"
echo "     - API: curl http://localhost:8000/api/v1/latest"
echo "     - Dashboard: streamlit run frontend/app.py"
echo "=================================================="
