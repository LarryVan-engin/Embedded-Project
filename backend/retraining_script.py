import pandas as pd
import numpy as np
import sqlite3
import tensorflow as tf
from sklearn.preprocessing import StandardScaler
from backend.database_manager import db_manager
import os
os.environ['TF_CPP_MIN_LOG_LEVEL'] = '2' # Tắt các log thừa của TensorFlow
# --- CẤU HÌNH ---
WINDOW_SIZE = 6  # 6 mốc thời gian 
FEATURES_COUNT = 5 # PM2.5, PM10, CO, NO2, O3 
MODEL_PATH = "AQI_model.h5" # Model gốc đã train từ Kaggle 
RETRAIN_MIN_SAMPLES = 100 # Cần tối thiểu 100 mẫu mới để học lại

def prepare_training_data(df):
    """Ánh xạ eCO2/TVOC sang cấu trúc 5 features và tạo cửa sổ trượt."""
    # Bước 1: Mapping dữ liệu cảm biến thực tế 
    # Thứ tự: [PM2.5, PM10, CO, NO2, O3]
    mapped_data = []
    for _, row in df.iterrows():
        # Ánh xạ Proxy: eCO2 -> CO, TVOC -> các khí còn lại 
        mapped_data.append([
            row['tvoc'], row['tvoc'], row['eco2'], row['tvoc'], row['tvoc']
        ])
    
    mapped_df = pd.DataFrame(mapped_data, columns=['pm2_5', 'pm10', 'co', 'no2', 'o3'])
    
    # Bước 2: Tạo Sliding Window (Cửa sổ trượt) 
    X, y = [], []
    for i in range(len(mapped_df) - WINDOW_SIZE):
        X.append(mapped_df.iloc[i : i + WINDOW_SIZE].values.flatten())
        # Nhãn thực tế là AQI đo được sau 1 giờ (giả định tính từ cảm biến) 
        # Lưu ý: Trong thực tế bạn cần lưu AQI thực tế vào DB để làm nhãn
        y.append(mapped_df.iloc[i + WINDOW_SIZE - 1]['aqi_predicted']) # Tạm thời dùng aqi_pred để demo
        
    return np.array(X), np.array(y)

def run_retraining():
    print("🔄 Đang kiểm tra dữ liệu để học tăng cường...")
    df = db_manager.get_data_for_retraining()
    
    if len(df) < RETRAIN_MIN_SAMPLES + WINDOW_SIZE:
        print(f"⏸ Chưa đủ dữ liệu ({len(df)}/{RETRAIN_MIN_SAMPLES}). Đang chờ tích lũy thêm.")
        return

    # 1. Chuẩn bị dữ liệu
    X, y = prepare_training_data(df)
    
    # 2. Load model hiện tại
    if not os.path.exists(MODEL_PATH):
        print("❌ Không tìm thấy file model gốc!")
        return
    
    model = tf.keras.models.load_model(MODEL_PATH)
    
    # 3. Fine-tuning (Học tăng cường)
    # Chúng ta học với learning rate rất nhỏ để không làm hỏng kiến thức cũ
    model.compile(optimizer=tf.keras.optimizers.Adam(learning_rate=0.0001), loss='mse')
    
    print("🚀 Đang huấn luyện lại mô hình với dữ liệu thực tế...")
    model.fit(X, y, epochs=10, batch_size=16, verbose=0)
    
    # 4. Lưu model mới
    model.save(MODEL_PATH)
    
    # 5. Export sang TFLite cho Kit RA6M5 
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT] # Lượng tử hóa 
    tflite_model = converter.convert()
    
    with open("updated_model.tflite", "wb") as f:
        f.write(tflite_model)
    
    print("✅ Đã cập nhật mô hình mới: updated_model.tflite")
    # Tại đây bạn có thể gọi thêm script để convert sang file .h cho MCU

if __name__ == "__main__":
    run_retraining()