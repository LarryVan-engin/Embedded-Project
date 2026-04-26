import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

def generate_uba_data_sine_fixed(samples=5000):
    np.random.seed(42)
    # Tạo trục thời gian sao cho có ít nhất 5 chu kỳ sóng hoàn chỉnh
    t = np.linspace(0, 10 * np.pi, samples) 
    
    # 1. Tạo sóng Sine cho TVOC (ppb)
    # Biên độ rộng để quét qua tất cả các Level UBA (từ 10ppb đến 5500ppb) [cite: 239, 410]
    base_tvoc = 2800 
    amplitude = 2700
    
    # Kết hợp sóng sine chính và nhiễu trắng nhẹ để mô phỏng thực tế [cite: 233, 431]
    tvoc_ppb = base_tvoc + amplitude * np.sin(t) 
    tvoc_ppb += np.random.normal(0, 30, samples) # Nhiễu cảm biến [cite: 467]
    tvoc_ppb = np.clip(tvoc_ppb, 10, 6000)

    def calculate_iaq_smooth(ppb):
        # Quy đổi theo Datasheet: ppb -> mg/m3 [cite: 240, 241]
        mg_m3 = (ppb / 1000) / 0.5 
        
        # Ánh xạ theo bảng 6 UBA (trang 10 Datasheet) 
        if mg_m3 < 0.3:     # Level 1: Very Good 
            val = 1.0 + (mg_m3 / 0.3) * 0.9
        elif mg_m3 < 1.0:   # Level 2: Good 
            val = 2.0 + ((mg_m3 - 0.3) / 0.7) * 0.9
        elif mg_m3 < 3.0:   # Level 3: Medium 
            val = 3.0 + ((mg_m3 - 1.0) / 2.0) * 0.9
        elif mg_m3 < 10.0:  # Level 4: Poor 
            val = 4.0 + ((mg_m3 - 3.0) / 7.0) * 0.9
        else:               # Level 5: Bad 
            val = 5.0
        return val + np.random.normal(0, 0.01)

    iaq_labels = [calculate_iaq_smooth(p) for p in tvoc_ppb]
    
    # eCO2 nội suy (chỉ để theo dõi trên Dashboard) [cite: 5, 337]
    eco2_ppm = 400 + (tvoc_ppb * 0.6) + np.random.normal(0, 10, samples)
    
    df = pd.DataFrame({'tvoc': tvoc_ppb, 'eco2': eco2_ppm, 'iaq': iaq_labels})
    df.to_csv('iaq_uba_dataset.csv', index=False)
    
    # VẼ BIỂU ĐỒ KIỂM TRA (Tăng số lượng mẫu hiển thị để thấy hình sine)
    plt.figure(figsize=(15, 5))
    plt.plot(tvoc_ppb[:1000], label='TVOC (ppb)', color='#1f77b4')
    plt.plot(np.array(iaq_labels[:1000]) * 1000, label='IAQ Rating (Scaled x1000)', color='#ff7f0e')
    plt.title("Dữ liệu biến thiên hình SINE chuẩn UBA (1000 mẫu đầu tiên)")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.show()

generate_uba_data_sine_fixed()