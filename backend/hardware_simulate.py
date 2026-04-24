import paho.mqtt.client as mqtt
import json
import random
import time
import numpy as np
import tensorflow as tf
from datetime import datetime

# --- CẤU HÌNH ---
MQTT_BROKER = "localhost"
MQTT_PORT = 1883
MQTT_TOPIC = "iaq/node/data"
MODEL_PATH = "backend/AQI_model.h5"

# Bộ hằng số chuẩn hóa (Cần khớp với training và MCU)
# Lưu ý: Ở đây tôi giả định mảng 30 giá trị Mean/Scale. Bạn hãy cập nhật đúng số của bạn.
AQI_MEAN = np.array([50.0] * 30) 
AQI_SCALE = np.array([20.0] * 30)

class VirtualEdgeNode:
    def __init__(self):
        # 1. Load model AI thực tế
        print("🧠 Loading AI Model (.h5)...")
        self.model = tf.keras.models.load_model(MODEL_PATH)
        
        # 2. Khởi tạo Sliding Window (6 mốc thời gian x 5 features = 30 đầu vào)
        self.window = []
        
        # 3. Kết nối MQTT (Sửa lỗi Callback API version 2.0)
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1, "Virtual_RA6M5_Node")

    def generate_sensor_values(self):
        """Giả lập dữ liệu từ cảm biến ZMOD4410."""
        eco2 = random.uniform(400, 800)
        tvoc = random.uniform(50, 200)
        return eco2, tvoc

    def map_to_5_features(self, eco2, tvoc):
        """Ánh xạ 2 chỉ số thực sang 5 đặc trưng model yêu cầu."""
        # Thứ tự: [PM2.5, PM10, CO, NO2, O3]
        return [tvoc, tvoc, eco2, tvoc, tvoc]

    def predict_aqi(self, eco2, tvoc):
        """Quy trình suy luận AI giống hệt trên MCU."""
        # A. Mapping & Thêm vào cửa sổ trượt
        features = self.map_to_5_features(eco2, tvoc)
        self.window.append(features)
        
        if len(self.window) > 6:
            self.window.pop(0) # Giữ đúng 6 mốc thời gian gần nhất
            
        if len(self.window) < 6:
            return 0.0 # Chưa đủ dữ liệu để dự báo
            
        # B. Tiền xử lý: Flatten (30 features) và Scaling
        # Công thức chuẩn hóa: $$z = \frac{x - \mu}{\sigma}$$
        input_data = np.array(self.window).flatten()
        input_scaled = (input_data - AQI_MEAN) / AQI_SCALE
        
        # C. Chạy Model Inference
        # Reshape về (1, 30) để nạp vào model MLP
        prediction = self.model.predict(input_scaled.reshape(1, -1), verbose=0)
        return float(prediction[0][0])

    def run(self):
        self.client.connect(MQTT_BROKER, MQTT_PORT)
        print("✅ Simulator is running with REAL AI model.")
        
        while True:
            eco2, tvoc = self.generate_sensor_values()
            aqi_pred = self.predict_aqi(eco2, tvoc)
            
            payload = {
                "eco2": round(eco2, 2),
                "tvoc": round(tvoc, 2),
                "aqi_pred": round(aqi_pred, 2)
            }
            
            self.client.publish(MQTT_TOPIC, json.dumps(payload))
            print(f"📤 MQTT Sent: {payload}")
            time.sleep(5)

if __name__ == "__main__":
    node = VirtualEdgeNode()
    node.run()