import os

def make_tflite_header(tflite_path, output_path):
    # Kiểm tra file đầu vào có tồn tại không
    if not os.path.exists(tflite_path):
        print(f"Lỗi: Không tìm thấy file {tflite_path}")
        return

    with open(tflite_path, 'rb') as f:
        tflite_content = f.read()
    
    with open(output_path, 'w') as f:
        f.write('#ifndef AQI_MODEL_DATA_H\n')
        f.write('#define AQI_MODEL_DATA_H\n\n')
        f.write('// Model tflite converted for Renesas RA6M5\n')
        f.write('const unsigned char g_aqi_model_data[] __attribute__((aligned(16))) = {\n  ')
        
        for i, byte in enumerate(tflite_content):
            f.write(f'0x{byte:02x}, ')
            if (i + 1) % 12 == 0:
                f.write('\n  ')
        
        f.write('\n};\n\n')
        f.write(f'const unsigned int g_aqi_model_data_len = {len(tflite_content)};\n\n')
        f.write('#endif // AQI_MODEL_DATA_H\n')
    
    print(f"Thành công! Đã tạo file: {output_path}")

# Chạy hàm convert
make_tflite_header('aqi_model.tflite', 'aqi_model_data.h')
