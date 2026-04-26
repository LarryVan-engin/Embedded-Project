import paho.mqtt.client as mqtt
import json
import time
import numpy as np
import tensorflow as tf
from datetime import datetime
import os

# Tắt log thừa của TensorFlow
os.environ['TF_CPP_MIN_LOG_LEVEL'] = '3'

# --- CẤU HÌNH ---
MQTT_BROKER = "localhost"
MQTT_PORT = 1883
MQTT_TOPIC = "iaq/node/data"
MODEL_PATH = "updates/IAQ_model.h5"

# Hằng số chuẩn hóa (Lấy từ kết quả fit_transform của StandardScaler trong train_iaq_model.py)
# Lưu ý: Cần copy lại 2 con số này mỗi khi bạn chạy Retrain!
IAQ_MEAN = 2812.118256623945
IAQ_SCALE = 1904.0350619753567

class VirtualZMOD4410Node:
    def __init__(self):
        # 1. Load model AI dự báo (AI chạy trên RA6M5)
        print("🧠 Loading AI Predictive Model (.h5)...")
        if not os.path.exists(MODEL_PATH):
            print(f"❌ KHÔNG TÌM THẤY MODEL: Vui lòng chạy train_iaq_model.py trước!")
            exit()
        self.model = tf.keras.models.load_model(MODEL_PATH, compile=False)
        
        # 2. Kết nối MQTT
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1, "ESP32_Gateway_Sim")
        
        # Biến đếm thời gian cho sóng Sine
        self.t = 0.0 

    def get_actual_iaq(self, ppb):
        """Mô phỏng thuật toán AI tích hợp sẵn của Renesas ZMOD4410 (Ground Truth)"""
        mg_m3 = (ppb / 1000) / 0.5 
        if mg_m3 < 0.3: return 1.0 + (mg_m3 / 0.3) * 0.9
        elif mg_m3 < 1.0: return 2.0 + ((mg_m3 - 0.3) / 0.7) * 0.9
        elif mg_m3 < 3.0: return 3.0 + ((mg_m3 - 1.0) / 2.0) * 0.9
        elif mg_m3 < 10.0: return 4.0 + ((mg_m3 - 3.0) / 7.0) * 0.9
        return 5.0

    def generate_sensor_data(self):
        """Giả lập dữ liệu theo sóng Sine để quét qua 5 mức độ ô nhiễm"""
        # Sóng Sine cơ bản cộng thêm nhiễu
        base_tvoc = 2800
        amplitude = 2700
        tvoc = base_tvoc + amplitude * np.sin(self.t) + np.random.normal(0, 50)
        tvoc = np.clip(tvoc, 10, 6000)
        
        # Nội suy eCO2 từ TVOC (theo Datasheet)
        eco2 = np.clip(400 + (tvoc * 0.6) + np.random.normal(0, 10), 400, 5000)
        
        # Tăng thời gian t cho chu kỳ tiếp theo
        self.t += 0.1 
        
        return tvoc, eco2

    def predict_future_iaq(self, tvoc):
        """Quy trình suy luận AI (Inference) giống hệt trên MCU RA6M5"""
        # 1. Point-to-Point: Không cần cửa sổ trượt nữa
        # 2. Chuẩn hóa: $$z = \frac{x - \mu}{\sigma}$$
        input_scaled = (tvoc - IAQ_MEAN) / IAQ_SCALE
        
        # 3. Model Inference (Dự báo)
        input_array = np.array([[input_scaled]]) # Shape (1, 1)
        prediction = self.model.predict(input_array, verbose=0)
        return float(prediction[0][0])

    def run(self):
        try:
            self.client.connect(MQTT_BROKER, MQTT_PORT)
            print("✅ Edge AI Simulator is running...")
            print("   - Sensor: ZMOD4410 (Sine Wave Simulation)")
            print("   - Predictor: RA6M5 (Point-to-Point MLP)")
            print("   - Gateway: ESP32 (MQTT)")
            
            while True:
                # 1. "Đọc" cảm biến
                tvoc, eco2 = self.generate_sensor_data()
                
                # 2. Cảm biến tự tính IAQ hiện tại
                iaq_actual = self.get_actual_iaq(tvoc)
                
                # 3. MCU dự báo IAQ tương lai
                iaq_forecast = self.predict_future_iaq(tvoc)
                
                # 4. Đóng gói JSON gửi qua ESP32
                payload = {
                    "tvoc": round(tvoc, 1),
                    "eco2": round(eco2, 1),
                    "iaq_actual": round(iaq_actual, 2),
                    "iaq_forecast": round(iaq_forecast, 2),
                    "timestamp": datetime.now().isoformat()
                }
                
                self.client.publish(MQTT_TOPIC, json.dumps(payload))
                print(f"📤 Published: TVOC={payload['tvoc']}ppb | Actual={payload['iaq_actual']} | Predict={payload['iaq_forecast']}")
                
                time.sleep(3) # Gửi dữ liệu mỗi 3 giây
                
        except KeyboardInterrupt:
            print("\n🛑 Stopped Simulator.")
        except Exception as e:
            print(f"❌ Connection Error: {e}")

if __name__ == "__main__":
    node = VirtualZMOD4410Node()
    node.run()