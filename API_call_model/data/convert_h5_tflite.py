import tensorflow as tf
import os

# --- CẤU HÌNH ---
MODEL_H5_PATH = r'E:\gitclone\Embedded_clone\Embedded-Project\ai_engine\vault\IAQ_model.h5'
TFLITE_PATH = r'E:\gitclone\Embedded_clone\Embedded-Project\updates\iaq_model.tflite'
HEADER_PATH = r'E:\gitclone\Embedded_clone\Embedded-Project\ai_engine\vault\iaq_data_model.h'

def convert_to_header():
    # 1. Tải mô hình .h5 đã huấn luyện
    if not os.path.exists(MODEL_H5_PATH):
        print(f"❌ Không tìm thấy file {MODEL_H5_PATH}")
        return
    
    model = tf.keras.models.load_model(MODEL_H5_PATH, compile=False)
    print("✅ Đã load mô hình .h5.")

    # 2. Chuyển đổi sang định dạng TFLite
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    # Tối ưu hóa kích thước cho MCU (Quantization)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    tflite_model = converter.convert()
    
    # Lưu file .tflite để đối soát
    with open(TFLITE_PATH, 'wb') as f:
        f.write(tflite_model)
    print(f"✅ Đã chuyển đổi sang {TFLITE_PATH} (Kích thước: {len(tflite_model)/1024:.2f} KB).")

    # 3. Chuyển đổi TFLite sang mảng Hex trong file .h
    with open(HEADER_PATH, 'w') as f:
        f.write('#ifndef IAQ_MODEL_H\n')
        f.write('#define IAQ_MODEL_H\n\n')
        f.write(f'// Model size: {len(tflite_model)} bytes\n')
        f.write('const unsigned char iaq_model_tflite[] = {\n    ')
        
        # Ghi dữ liệu dưới dạng byte hex
        for i, byte in enumerate(tflite_model):
            f.write(f'0x{byte:02x}, ')
            if (i + 1) % 12 == 0: # Xuống dòng sau mỗi 12 byte cho đẹp
                f.write('\n    ')
        
        f.write('\n};\n\n')
        f.write(f'const unsigned int iaq_model_tflite_len = {len(tflite_model)};\n\n')
        f.write('#endif // IAQ_MODEL_H\n')
    
    print(f"🚀 THÀNH CÔNG: Đã tạo file header tại {HEADER_PATH}")

if __name__ == "__main__":
    convert_to_header()