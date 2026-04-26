from contextlib import asynccontextmanager
import uvicorn
from fastapi import FastAPI, BackgroundTasks
from fastapi.middleware.cors import CORSMiddleware
from backend.database_manager import db_manager
from backend.mqtt_client import mqtt_service
import threading
import os

# --- 1. QUẢN LÝ VÒNG ĐỜI (THAY THẾ CHO on_event) ---
@asynccontextmanager
async def lifespan(app: FastAPI):
    # Logic chạy khi ứng dụng Startup
    print("🚀 Khởi tạo Database...")
    db_manager.initialize_db()
    
    print("📡 Đang khởi động MQTT Client Service...")
    mqtt_thread = threading.Thread(target=mqtt_service.run, daemon=True)
    mqtt_thread.start()
    
    yield # Ứng dụng API sẽ hoạt động trong suốt quá trình này
    
    # Logic chạy khi ứng dụng Shutdown (Tắt server)
    print("🛑 Đang dọn dẹp và tắt hệ thống...")

# --- 2. KHỞI TẠO APP VỚI LIFESPAN ---
app = FastAPI(
    title="ZMOD4410 Edge AI Gateway",
    description="Backend xử lý chỉ số IAQ theo chuẩn UBA",
    version="2.0.0",
    lifespan=lifespan  # Kích hoạt Lifespan
)

# Cấu hình CORS
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# --- 3. CÁC API ENDPOINTS ---
@app.get("/")
async def root():
    return {"message": "IAQ Edge AI Backend is running", "status": "online"}

@app.get("/api/v1/latest")
async def get_latest_data():
    latest = db_manager.get_latest_record()
    if latest:
        return latest
    return {"error": "No data available"}

@app.get("/api/v1/history")
async def get_history(limit: int = 100):
    data = db_manager.get_history(limit)
    return data

@app.post("/api/v1/retrain")
async def trigger_retrain(background_tasks: BackgroundTasks):
    from ai_engine.retraining_script import run_retraining
    background_tasks.add_task(run_retraining)
    return {"message": "Retraining process started in background"}

# --- 4. CHẠY SERVER ---
if __name__ == "__main__":
    # ĐÃ SỬA: Chỉ định rõ đường dẫn "backend.main:app" thay vì "main:app"
    uvicorn.run("backend.main:app", host="0.0.0.0", port=8000, reload=True)