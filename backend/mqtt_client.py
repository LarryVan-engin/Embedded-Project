import re
import paho.mqtt.client as mqtt
from backend.database_manager import db_manager
import threading
from datetime import datetime

class MqttService:
    def __init__(self):
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1)
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
        self.is_retraining = False
        self.temperature = None
        self.humidity = None

    def on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            print("✅ MQTT Connected to Broker")
            client.subscribe("iaq/node/data")

    def parse_mqtt_payload(self, payload_str):
        """
        Parse MQTT payload từ ESP32 với định dạng:
        [547034 ms] Published: TVOC=144.0ppb | Actual=1.86 | Predict=1.80
        [548171 ms] [sensor:263] T=31.1 C  RH=46.9%
        """
        data = {}
        
        # Pattern 1: Parse TVOC, Actual IAQ, Predicted IAQ
        # [547034 ms] Published: TVOC=144.0ppb | Actual=1.86 | Predict=1.80
        tvoc_pattern = r'TVOC=(\d+\.?\d*)\s*ppb'
        actual_pattern = r'Actual=(\d+\.?\d*)'
        predict_pattern = r'Predict=(\d+\.?\d*)'
        
        tvoc_match = re.search(tvoc_pattern, payload_str)
        actual_match = re.search(actual_pattern, payload_str)
        predict_match = re.search(predict_pattern, payload_str)
        
        if tvoc_match:
            data['tvoc'] = float(tvoc_match.group(1))
        if actual_match:
            data['iaq_actual'] = float(actual_match.group(1))
        if predict_match:
            data['iaq_forecast'] = float(predict_match.group(1))
        
        # Pattern 2: Parse Temperature và Humidity
        # [548171 ms] [sensor:263] T=31.1 C  RH=46.9%
        temp_pattern = r'T=(\d+\.?\d*)\s*C'
        humidity_pattern = r'RH=(\d+\.?\d*)%'
        
        temp_match = re.search(temp_pattern, payload_str)
        humidity_match = re.search(humidity_pattern, payload_str)
        
        if temp_match:
            data['temperature'] = float(temp_match.group(1))
            self.temperature = data['temperature']
        if humidity_match:
            data['humidity'] = float(humidity_match.group(1))
            self.humidity = data['humidity']
        
        return data

    def on_message(self, client, userdata, msg):
        try:
            payload_str = msg.payload.decode('utf-8')
            print(f"📨 Raw MQTT: {payload_str}")
            
            # Parse dữ liệu từ string
            data = self.parse_mqtt_payload(payload_str)
            
            # Chỉ lưu khi có đầy đủ dữ liệu chính (TVOC, Actual, Predict)
            if all(k in data for k in ['tvoc', 'iaq_actual', 'iaq_forecast']):
                # Sử dụng temperature/humidity mới nhất nếu không có trong payload này
                temp = data.get('temperature', self.temperature)
                humidity = data.get('humidity', self.humidity)
                
                db_manager.save_log(
                    tvoc=data['tvoc'],
                    iaq_actual=data['iaq_actual'],
                    iaq_forecast=data['iaq_forecast'],
                    temperature=temp,
                    humidity=humidity
                )
                print(f"✅ Saved: TVOC={data['tvoc']:.1f}ppb, Actual={data['iaq_actual']:.2f}, Predict={data['iaq_forecast']:.2f}")
                self.check_retrain()
            else:
                print(f"⚠️ Incomplete data received: {data}")
                
        except Exception as e:
            print(f"⚠️ Error parsing MQTT message: {e}")

    def check_retrain(self):
        if self.is_retraining: return
        count = db_manager.get_data_count()
        if count > 0 and count % 500 == 0:
            self.is_retraining = True
            print(f"🔄 Triggering retraining at {count} samples...")
            threading.Thread(target=self.run_retrain).start()

    def run_retrain(self):
        try:
            from ai_engine.retraining_script import run_retraining
            run_retraining()
            print("🚀 Tự động học lại hoàn tất!")
        finally:
            self.is_retraining = False

    def run(self):
        self.client.connect("localhost", 1883, 60)
        self.client.loop_forever()

mqtt_service = MqttService()