import os
import pandas as pd
import numpy as np
import tensorflow as tf
from sklearn.preprocessing import StandardScaler
from backend.database_manager import db_manager
from ai_engine.model_exporter import ModelExporter

# Tắt các log thừa của TensorFlow
os.environ['TF_CPP_MIN_LOG_LEVEL'] = '3'

# --- CẤU HÌNH ĐƯỜNG DẪN (Đã đồng bộ) ---
MODEL_PATH = "updates/IAQ_model.h5"
TFLITE_PATH = "updates/iaq_model.tflite"

# Thư mục đích mà MCU sẽ gọi
HEADER_PATH = "updates/iaq_model_data.h" 
SCALER_PATH = "updates/scaler_constants.h"

RETRAIN_MIN_SAMPLES = 500  # Ngưỡng mẫu mới để học lại

def calculate_actual_iaq(ppb):
    """Tính nhãn IAQ chuẩn UBA dựa trên TVOC để làm Ground Truth"""
    mg_m3 = (ppb / 1000) / 0.5 
    if mg_m3 < 0.3: return 1.0 + (mg_m3 / 0.3) * 0.9
    elif mg_m3 < 1.0: return 2.0 + ((mg_m3 - 0.3) / 0.7) * 0.9
    elif mg_m3 < 3.0: return 3.0 + ((mg_m3 - 1.0) / 2.0) * 0.9
    elif mg_m3 < 10.0: return 4.0 + ((mg_m3 - 3.0) / 7.0) * 0.9
    return 5.0

def run_retraining():
    print("\n🔄 Khởi động quá trình học tăng cường (Incremental Learning)...")
    df = db_manager.get_data_for_retraining()
    
    if len(df) < RETRAIN_MIN_SAMPLES:
        print(f"⏸ Chưa đủ dữ liệu ({len(df)}/{RETRAIN_MIN_SAMPLES}). Đang chờ tích lũy thêm.")
        return

    # 1. Chuẩn bị dữ liệu
    X = df[['tvoc']].values
    y = np.array([calculate_actual_iaq(val[0]) for val in X])
    
    # Chuẩn hóa dữ liệu đầu vào
    scaler = StandardScaler()
    X_scaled = scaler.fit_transform(X)
    
    current_mean = scaler.mean_[0]
    current_scale = scaler.scale_[0]

    # 2. Load model hiện tại
    if not os.path.exists(MODEL_PATH):
        print(f"❌ Không tìm thấy file gốc tại {MODEL_PATH}!")
        return
    
    # 3. Fine-tuning
    model = tf.keras.models.load_model(
        MODEL_PATH, 
        custom_objects={'mae': tf.keras.metrics.MeanAbsoluteError()}
    )

    model.compile(optimizer=tf.keras.optimizers.Adam(learning_rate=0.0001), loss=tf.keras.losses.MeanAbsoluteError())
    
    print("🚀 Đang huấn luyện lại mô hình với dữ liệu thực tế (Epochs=15)...")
    model.fit(X_scaled, y, epochs=15, batch_size=32, verbose=0)
    
    # 4. Lưu model mới
    model.save(MODEL_PATH)
    print("✅ Đã lưu trọng số mới vào", MODEL_PATH)
    
    # Đảm bảo thư mục API_call_model tồn tại trước khi xuất file
    os.makedirs(os.path.dirname(HEADER_PATH), exist_ok=True)
    
    # 5. ĐÓNG GÓI TỰ ĐỘNG - Truyền đúng các biến cấu hình toàn cục ở trên
    exporter = ModelExporter(
        h5_path=MODEL_PATH, 
        tflite_path=TFLITE_PATH, 
        header_path=HEADER_PATH,           # Lưu đúng vào API_call_model/
        scaler_header_path=SCALER_PATH,    # Lưu đúng vào API_call_model/
        mean_val=current_mean, 
        scale_val=current_scale
    )
    exporter.run_all()

if __name__ == "__main__":
    run_retraining()