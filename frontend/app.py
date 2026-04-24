import streamlit as st
import pandas as pd
import plotly.graph_objects as go
import time
import os
from backend.database_manager import db_manager
from backend.retraining_script import run_retraining

# Tắt cảnh báo TensorFlow để Terminal sạch hơn
os.environ['TF_CPP_MIN_LOG_LEVEL'] = '3'

# --- 1. CẤU HÌNH TRANG ---
st.set_page_config(page_title="Edge AI Monitor", layout="wide")

st.title("🍃 Edge AI Air Quality: Real-time & Incremental Learning")
st.markdown("Hệ thống giám sát RA6M5 tích hợp vòng lặp học lại tự động")

# --- 2. QUẢN LÝ MÔ HÌNH (MODEL MANAGEMENT) ---
with st.expander("⚙️ Quản lý mô hình & Học tăng cường", expanded=False):
    col_info, col_btn = st.columns([3, 1])
    df_count = db_manager.get_data_for_retraining()
    
    with col_info:
        st.write(f"**Dữ liệu tích lũy:** {len(df_count)} mẫu")
        st.info("Nhấn Retrain để cập nhật trọng số MLP nhằm giảm sai số MAE.")
    
    with col_btn:
        if st.button("🚀 Kích hoạt Retrain"):
            with st.spinner("Đang huấn luyện..."):
                run_retraining() 
                st.success("Đã cập nhật Model!")
                time.sleep(2) # Đợi để hiển thị thông báo trước khi rerun

# --- 3. XỬ LÝ DỮ LIỆU VÀ HIỂN THỊ ---
df = db_manager.get_data_for_retraining()

if not df.empty:
    # Tính toán AQI thực tế và sai số
    df['aqi_actual'] = df['eco2'] * 0.05 + df['tvoc'] * 0.1 
    df['prev_prediction'] = df['aqi_predicted'].shift(1)
    df['error'] = abs(df['aqi_actual'] - df['prev_prediction'])
    
    latest = df.iloc[-1]
    
    # Hiển thị Metrics
    c1, c2, c3, c4 = st.columns(4)
    c1.metric("eCO2 (ppm)", f"{latest['eco2']:.1f}")
    c2.metric("TVOC (ppb)", f"{latest['tvoc']:.1f}")
    c3.metric("🔮 Dự báo (Next 1h)", f"{latest['aqi_predicted']:.1f}")
    
    mae = df['error'].mean()
    c4.metric("📉 Sai số MAE", f"{mae:.2f}", delta=f"{15.06 - mae:.2f} vs Base")

    # Vẽ đồ thị - KHÔNG dùng key trong vòng lặp vô hạn nữa
    fig = go.Figure()
    fig.add_trace(go.Scatter(x=df['timestamp'], y=df['aqi_actual'], name="AQI Thực tế", line=dict(color='#1f77b4', width=3)))
    fig.add_trace(go.Scatter(x=df['timestamp'], y=df['prev_prediction'], name="Dự đoán cũ", line=dict(color='#d62728', dash='dot')))
    
    fig.update_layout(title="Đồ thị đối soát độ chính xác (Real-time Overlay)", height=450)
    st.plotly_chart(fig, use_container_width=True)

    # Thanh tiến trình độ tin cậy
    acc_rate = (df['error'] < 15.0).mean() * 100
    st.write(f"**Tỉ lệ dự đoán tin cậy (Sai số < 15):** {acc_rate:.1f}%")
    st.progress(acc_rate / 100)

# --- 4. CƠ CHẾ LÀM MỚI (REAL-TIME REFRESH) ---
# Thay vì while True, ta dùng time.sleep và st.rerun()
time.sleep(3) 
st.rerun()