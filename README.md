2. Quy trình tạo Model TensorFlow LiteQuy trình này sẽ biến dữ liệu thô thành một file .tflite cực nhẹ để nạp vào chip RA6M5:

Bước 1: Huấn luyện (Keras/TensorFlow)

Tiền xử lý: Chuẩn hóa dữ liệu (Scaling) về khoảng $[0, 1]$ vì vi điều khiển xử lý số nhỏ tốt hơn.

Xây dựng Model: Một mạng Neural đơn giản (Dense layers) hoặc GRU/LSTM nếu bạn muốn dự đoán theo chuỗi thời gian.
```
    import tensorflow as tf
    import pandas as pd
    import numpy as np
    from sklearn.model_selection import train_test_split
    from sklearn.preprocessing import StandardScaler
```
1. Giả lập hoặc Load dữ liệu (Ví dụ từ UCI Air Quality)
Giả sử bạn có 3 đầu vào: Temp, Humidity, Gas_Sensor và 1 đầu ra: AQI_Index
```
    data = {
        'temp': np.random.uniform(20, 40, 1000),

        'humidity': np.random.uniform(40, 90, 1000),

        'gas\_sensor': np.random.uniform(0, 500, 1000),

        'aqi': np.random.uniform(0, 300, 1000) # Mục tiêu dự đoán
    }
    df = pd.DataFrame(data)

    X = df[['temp', 'humidity', 'gas_sensor']].values
    y = df['aqi'].values
```
2. Tiền xử lý dữ liệu
Rất quan trọng cho MCU: Chuẩn hóa dữ liệu về cùng một thang đo
```
    scaler = StandardScaler()
    X_scaled = scaler.fit_transform(X)

    X_train, X_test, y_train, y_test = train_test_split(X_scaled, y, test_size=0.2)
```
3. Xây dựng Model đơn giản cho Edge AI
```
    model = tf.keras.Sequential([
        tf.keras.layers.Dense(16, activation='relu', input\_shape=(3,)),

        tf.keras.layers.Dense(8, activation='relu'),

        tf.keras.layers.Dense(1) # Đầu ra là 1 giá trị liên tục (Regression)

    ])

    model.compile(optimizer='adam', loss='mse', metrics=['mae'])
```
4. Huấn luyện
   ``` 
    model.fit(X_train, y_train, epochs=50, batch_size=16, validation_split=0.1)
```
5. Chuyển đổi sang TFLite với INT8 Quantization (Tối ưu cho RA6M5)
```
    def representative_data_gen():
        for i in range(100):
            yield \[X\_train\[i].astype(np.float32).reshape(1, 3)] 

        converter = tf.lite.TFLiteConverter.from_keras_model(model)
        converter.optimizations = [tf.lite.Optimize.DEFAULT]
        converter.representative_dataset = representative_data_gen

    Đảm bảo model chỉ dùng kiểu INT8 để nhẹ và nhanh nhất
        converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
        converter.inference_input_type = tf.int8
        converter.inference_output_type = tf.int8

        tflite_model = converter.convert()
```
6. Lưu file model
```
    with open('model_quantized.tflite', 'wb') as f:
        f.write(tflite\_model)
    print("Đã tạo xong model_quantized.tflite thành công!")
```
Bước 2: Chuyển đổi & Định lượng (Quantization)

- Đây là bước "phép màu" giúp model chạy được trên Edge AI:

 --Chuyển model .h5 sang .tflite.

 --Sử dụng Integer Quantization (INT8) để nén kích thước model xuống còn vài KB thay vì vài MB.

Bước 3: Convert sang C++ ArrayDo MCU không đọc được file trực tiếp, bạn cần chuyển file .tflite thành một mảng hex trong file header (model_data.h):
```
    Bashxxd -i model.tflite > model_data.cc
```

3. Triển khai lên CK-RA6M5Sau khi có file header, bạn sử dụng e2 studio (IDE của Renesas):

-Add thư viện TFLM: Tích hợp bộ thư viện TensorFlow Lite cho nhân Cortex-M33.

-Khai báo Interpreter: Thiết lập vùng nhớ (Tensor Arena) cho mô hình.Inference:

-Đưa dữ liệu từ sensor (hoặc dữ liệu giả lập) vào model và nhận kết quả dự đoán.

