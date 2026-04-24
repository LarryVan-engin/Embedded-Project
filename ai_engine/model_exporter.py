import tensorflow as tf
import os

class ModelExporter:
    def __init__(self, h5_path="AQI_model.h5", tflite_path="aqi_model.tflite", header_path="aqi_model_data.h"):
        self.h5_path = h5_path
        self.tflite_path = tflite_path
        self.header_path = header_path

    def convert_to_tflite(self):
        """Chuyển đổi model Keras sang TensorFlow Lite với lượng tử hóa (Quantization)."""
        if not os.path.exists(self.h5_path):
            print(f"❌ Error: Không tìm thấy file {self.h5_path}")
            return False

        print(f"📦 Đang chuyển đổi {self.h5_path} sang TFLite...")
        model = tf.keras.models.load_model(self.h5_path)
        
        converter = tf.lite.TFLiteConverter.from_keras_model(model)
        # Tối ưu hóa dung lượng cho vi điều khiển (Lượng tử hóa 8-bit weights)
        converter.optimizations = [tf.lite.Optimize.DEFAULT] 
        
        tflite_model = converter.convert()
        
        with open(self.tflite_path, "wb") as f:
            f.write(tflite_model)
        
        print(f"✅ Đã tạo file: {self.tflite_path}")
        return True

    def export_to_header(self):
        """Chuyển đổi file nhị phân TFLite thành mảng C (Hex array) cho MCU."""
        if not os.path.exists(self.tflite_path):
            print("❌ Error: Chưa có file TFLite để export.")
            return

        with open(self.tflite_path, "rb") as f:
            tflite_content = f.read()

        with open(self.header_path, "w") as f:
            f.write("#ifndef AQI_MODEL_DATA_H\n")
            f.write("#define AQI_MODEL_DATA_H\n\n")
            f.write("// Model AI cập nhật tự động từ Server\n")
            # Căn lề 16-byte để tối ưu tốc độ đọc trên nhân Cortex-M33 của RA6M5
            f.write("const unsigned char g_aqi_model_data[] __attribute__((aligned(16))) = {\n  ")
            
            for i, byte in enumerate(tflite_content):
                f.write(f"0x{byte:02x}, ")
                if (i + 1) % 12 == 0:
                    f.write("\n  ")
            
            f.write("\n};\n\n")
            f.write(f"const unsigned int g_aqi_model_data_len = {len(tflite_content)};\n\n")
            f.write("#endif // AQI_MODEL_DATA_H\n")
        
        print(f"🚀 Thành công! Đã xuất file: {self.header_path}")

    def run_all(self):
        """Chạy toàn bộ quy trình export."""
        if self.convert_to_tflite():
            self.export_to_header()

if __name__ == "__main__":
    exporter = ModelExporter()
    exporter.run_all()