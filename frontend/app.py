import streamlit as st
import plotly.graph_objects as go
import pandas as pd
import time
import os
import sys
# Lấy đường dẫn thư mục hiện tại của app.py (frontend) và lùi lại 1 bước để ra thư mục gốc
current_dir = os.path.dirname(os.path.abspath(__file__))
parent_dir = os.path.abspath(os.path.join(current_dir, '..'))
sys.path.append(parent_dir)

from backend.database_manager import db_manager


# ---------------------------------------------------

st.set_page_config(page_title="IAQ Monitor", layout="wide")
st.title("🍃 Predictive Edge AI Using CK-RA6M5")

# Lấy dữ liệu từ DB (giả sử có các cột: timestamp, tvoc, iaq_actual, iaq_forecast, temperature, humidity)
df = db_manager.get_data_for_retraining()

if not df.empty:
    latest = df.iloc[-1]
    df['error'] = abs(df['iaq_actual'] - df['iaq_forecast'])
    
    # Status
    st.subheader(f"Dự báo IAQ (1.0 - 5.0): {latest['iaq_forecast']:.2f}")
    
    # Metrics
    c1, c2, c3, c4, c5 = st.columns(5)
    c1.metric("TVOC (ppb)", f"{latest['tvoc']:.0f}")
    c2.metric("IAQ Thực tế", f"{latest['iaq_actual']:.2f}")
    c3.metric("📊 Sai số MAE", f"{df['error'].mean():.3f}")
    
    # Thêm Temperature và Humidity nếu có dữ liệu
    if 'temperature' in df.columns and pd.notna(latest['temperature']):
        c4.metric("🌡️ Temperature", f"{latest['temperature']:.1f}°C")
    if 'humidity' in df.columns and pd.notna(latest['humidity']):
        c5.metric("💧 Humidity", f"{latest['humidity']:.1f}%")

    # Đồ thị UBA
    fig = go.Figure()
    fig.add_hrect(y0=1.0, y1=1.9, fillcolor="green", opacity=0.1, annotation_text="Rất Tốt")
    fig.add_hrect(y0=1.9, y1=2.9, fillcolor="yellowgreen", opacity=0.1, annotation_text="Tốt")
    fig.add_hrect(y0=2.9, y1=3.9, fillcolor="yellow", opacity=0.1, annotation_text="Trung bình")
    fig.add_hrect(y0=3.9, y1=4.9, fillcolor="orange", opacity=0.1, annotation_text="Kém")
    fig.add_hrect(y0=4.9, y1=5.5, fillcolor="red", opacity=0.1, annotation_text="Xấu")

    fig.add_trace(go.Scatter(x=df['timestamp'], y=df['iaq_actual'], name="IAQ Thực tế (ZMOD)", line=dict(color='#1f77b4', width=2)))
    fig.add_trace(go.Scatter(x=df['timestamp'], y=df['iaq_forecast'], name="IAQ Dự báo (AI)", line=dict(color='#d62728', dash='dot')))
    
    fig.update_layout(height=450, yaxis_range=[0.8, 5.5])
    st.plotly_chart(fig, use_container_width=True)
    
    # Biểu đồ TVOC theo thời gian
    if 'tvoc' in df.columns:
        fig_tvoc = go.Figure()
        fig_tvoc.add_trace(go.Scatter(x=df['timestamp'], y=df['tvoc'], name="TVOC (ppb)", 
                                       line=dict(color='#ff7f0e', width=2), fill='tozeroy'))
        fig_tvoc.update_layout(height=300, title="TVOC theo thời gian")
        st.plotly_chart(fig_tvoc, use_container_width=True)
    
    # Biểu đồ Temperature & Humidity
    if 'temperature' in df.columns or 'humidity' in df.columns:
        fig_env = go.Figure()
        
        if 'temperature' in df.columns:
            fig_env.add_trace(go.Scatter(x=df['timestamp'], y=df['temperature'], 
                                        name="Temperature (°C)", line=dict(color='#d62728', width=2)))
        
        if 'humidity' in df.columns:
            fig_env.add_trace(go.Scatter(x=df['timestamp'], y=df['humidity'], 
                                        name="Humidity (%)", line=dict(color='#2ca02c', width=2)))
        
        fig_env.update_layout(height=300, title="Nhiệt độ & Độ ẩm theo thời gian")
        st.plotly_chart(fig_env, use_container_width=True)

else:
    st.warning("Đang chờ dữ liệu từ ESP32...")

time.sleep(3) 
st.rerun()