import json
import paho.mqtt.client as mqtt
from datetime import datetime
from database_manager import db_manager 

# Cấu hình Broker MQTT (Có thể dùng Mosquitto hoặc HiveMQ)
MQTT_BROKER = "localhost"
MQTT_PORT = 1883
MQTT_TOPIC = "iaq/node/data"

def on_connect(client, userdata, flags, rc):
    """Callback khi kết nối thành công với Broker."""
    if rc == 0:
        print(f"✅ Connected to MQTT Broker at {MQTT_BROKER}")
        client.subscribe(MQTT_TOPIC)
        print(f"📡 Subscribed to topic: {MQTT_TOPIC}")
    else:
        print(f"❌ Connection failed with code {rc}")

def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode())
        eco2 = payload.get("eco2")
        tvoc = payload.get("tvoc")
        aqi_pred = payload.get("aqi_pred")

        # Ghi dữ liệu vào "nhật ký" của hệ thống
        if db_manager.save_log(eco2, tvoc, aqi_pred):
            print(f"💾 Data logged successfully to Database")
            
    except Exception as e:
        print(f"⚠️ Error: {e}")

def start_mqtt_client():
    """Khởi tạo và duy trì kết nối MQTT client."""
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1)
    client.on_connect = on_connect
    client.on_message = on_message

    try:
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
        # Bắt đầu vòng lặp lắng nghe (Non-blocking)
        client.loop_start() 
    except Exception as e:
        print(f"❌ Could not connect to Broker: {e}")

if __name__ == "__main__":
    start_mqtt_client()
    # Giữ cho script chạy liên tục
    import time
    while True:
        time.sleep(1)