import tensorflow as tf
import os

class ModelExporter:
    def __init__(self, h5_path="IAQ_model.h5", tflite_path="iaq_model.tflite", 
                 header_path="iaq_model_data.h", scaler_header_path="scaler_constants.h",
                 mean_val=0.0, scale_val=1.0):
        self.h5_path = h5_path
        self.tflite_path = tflite_path
        self.header_path = header_path
        self.scaler_header_path = scaler_header_path
        self.mean_val = mean_val
        self.scale_val = scale_val

    def convert_to_tflite(self):
        """Chuyển đổi model Keras sang TensorFlow Lite với lượng tử hóa (Quantization)."""
        if not os.path.exists(self.h5_path):
            print(f"❌ Error: Không tìm thấy file {self.h5_path}")
            return False

        print(f"📦 Đang chuyển đổi {self.h5_path} sang TFLite...")
        model = tf.keras.models.load_model(self.h5_path, compile=False)
        
        converter = tf.lite.TFLiteConverter.from_keras_model(model)
        # Tối ưu hóa dung lượng cho vi điều khiển
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

        with open(self.header_path, "w", encoding="utf-8") as f:
            f.write("#ifndef IAQ_MODEL_DATA_H\n")
            f.write("#define IAQ_MODEL_DATA_H\n\n")
            f.write("// Model AI cập nhật tự động từ Server\n")
            f.write("const unsigned char g_iaq_model_data[] __attribute__((aligned(16))) = {\n  ")
            
            for i, byte in enumerate(tflite_content):
                f.write(f"0x{byte:02x}, ")
                if (i + 1) % 12 == 0:
                    f.write("\n  ")
            
            f.write("\n};\n\n")
            f.write(f"const unsigned int g_iaq_model_data_len = {len(tflite_content)};\n\n")
            f.write("#endif // IAQ_MODEL_DATA_H\n")
        
        print(f"🚀 Thành công! Đã xuất Model C-Array: {self.header_path}")

    def export_scaler_constants(self):
        """Tự động sinh file C-Header chứa hệ số chuẩn hóa StandardScaler."""
        with open(self.scaler_header_path, "w") as f:
            f.write("#ifndef SCALER_CONSTANTS_H\n")
            f.write("#define SCALER_CONSTANTS_H\n\n")
            f.write("// Tự động sinh ra từ Server sau quá trình Retrain\n")
            f.write("#define SCALER_INPUT_SIZE 1\n\n")
            
            f.write("const float IAQ_MEAN[SCALER_INPUT_SIZE] = {\n")
            f.write(f"    {self.mean_val}f\n")
            f.write("};\n\n")
            
            f.write("const float IAQ_SCALE[SCALER_INPUT_SIZE] = {\n")
            f.write(f"    {self.scale_val}f\n")
            f.write("};\n\n")
            
            f.write("#endif // SCALER_CONSTANTS_H\n")
            
        print(f"📊 Thành công! Đã xuất Scaler C-Array: {self.scaler_header_path}")

    def run_all(self):
        """Chạy toàn bộ quy trình export (Model + Scaler)."""
        if self.convert_to_tflite():
            self.export_to_header()
            self.export_scaler_constants()

if __name__ == "__main__":
    exporter = ModelExporter()
    exporter.run_all()