import pandas as pd
import numpy as np
import tensorflow as tf
import matplotlib.pyplot as plt
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler
from tensorflow.keras.callbacks import EarlyStopping

# 1. Tải và chuẩn bị dữ liệu
df = pd.read_csv('iaq_uba_dataset.csv')
X = df[['tvoc']].values  # Chỉ lấy TVOC làm đầu vào duy nhất [cite: 212]
y = df['iaq'].values     # Nhãn IAQ chuẩn UBA (1.0 - 5.0) [cite: 239]

# Chia tập dữ liệu (80% Train, 20% Test)
X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2, random_state=42)

# 2. Chuẩn hóa dữ liệu (Rất quan trọng cho mạng Neural)
scaler = StandardScaler()
X_train_scaled = scaler.fit_transform(X_train)
X_test_scaled = scaler.transform(X_test)

print(f"📈 Mean: {scaler.mean_[0]}")
print(f"📉 Scale: {scaler.scale_[0]}")

# 3. Xây dựng mô hình MLP cải tiến (32-16-1)
# Cấu trúc này vẫn đủ nhỏ để nằm trong 20-40kB flash của MCU 
model = tf.keras.Sequential([
    tf.keras.layers.Dense(32, activation='relu', input_shape=(1,)), 
    tf.keras.layers.Dense(16, activation='relu'),
    tf.keras.layers.Dense(1) 
])

# Sử dụng loss MAE để AI nhạy hơn với các điểm cực trị
model.compile(optimizer=tf.keras.optimizers.Adam(learning_rate=0.001), 
              loss=tf.keras.losses.MeanAbsoluteError(),
              metrics=[tf.keras.metrics.MeanAbsoluteError()]
)
# Early Stopping để tránh học vẹt (Overfitting)
early_stop = EarlyStopping(monitor='val_loss', patience=15, restore_best_weights=True)

# 4. Huấn luyện
history = model.fit(
    X_train_scaled, y_train,
    validation_data=(X_test_scaled, y_test),
    epochs=150,
    batch_size=64,
    callbacks=[early_stop],
    verbose=1
)

# 5. Lưu Model và dự đoán thử
model.save('backend/IAQ_model.h5')
y_pred = model.predict(X_test_scaled)

# 6. Vẽ biểu đồ đối soát (Giữ nguyên hàm vẽ vùng màu UBA của bạn)
def plot_final_comparison(y_true, y_pred, num_samples=100):
    plt.figure(figsize=(15, 6))
    plt.plot(y_true[:num_samples], label='Thực tế (UBA)', color='#1f77b4', marker='o', alpha=0.7)
    plt.plot(y_pred[:num_samples], label='AI Dự báo', color='#d62728', linestyle='--', linewidth=2)
    
    # Vẽ các vùng màu UBA [cite: 239]
    plt.axhspan(1.0, 1.9, color='green', alpha=0.1, label='Level 1: Very Good')
    plt.axhspan(1.9, 2.9, color='yellowgreen', alpha=0.1, label='Level 2: Good')
    plt.axhspan(2.9, 3.9, color='yellow', alpha=0.1, label='Level 3: Medium')
    plt.axhspan(3.9, 4.9, color='orange', alpha=0.1, label='Level 4: Poor')
    plt.axhspan(4.9, 5.5, color='red', alpha=0.1, label='Level 5: Bad')
    
    plt.title('Đối soát IAQ Rating: Giải pháp Dự đoán Tức thời (Instant Prediction)')
    plt.legend(loc='upper left', bbox_to_anchor=(1, 1))
    plt.tight_layout()
    plt.show()

plot_final_comparison(y_test, y_pred)