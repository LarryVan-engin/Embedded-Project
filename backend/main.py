from contextlib import asynccontextmanager
import uvicorn
from fastapi import FastAPI, BackgroundTasks, HTTPException
from fastapi.responses import FileResponse
from fastapi.middleware.cors import CORSMiddleware
from backend.database_manager import db_manager
from backend.mqtt_client import mqtt_service
import threading
import os

# --- 1. QUẢN LÝ VÒNG ĐỜI (THAY THẾ CHO on_event) ---
@asynccontextmanager
async def lifespan(app: FastAPI):
    # Logic chạy khi ứng dụng Startup
    print("[DB] Khoi tao Database...")
    db_manager.initialize_db()
    
    print("[MQTT] Dang khoi dong MQTT Client Service...")
    mqtt_thread = threading.Thread(target=mqtt_service.run, daemon=True)
    mqtt_thread.start()
    
    yield # Ứng dụng API sẽ hoạt động trong suốt quá trình này
    
    # Logic chạy khi ứng dụng Shutdown (Tắt server)
    print("[SYS] Dang don dep va tat he thong...")

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

MODEL_FILE_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "updates", "iaq_model.tflite")

@app.get("/api/v1/model/version")
async def get_model_version():
    if not os.path.exists(MODEL_FILE_PATH):
        return {"version": 0}
    
    # SỬA LỖI ĐỒNG BỘ OTA: Dùng thời gian chỉnh sửa (Last Modified Time) thay vì File Size
    # để đảm bảo ESP32 luôn nhận diện được model mới dù kích thước file không đổi.
    file_mtime = int(os.path.getmtime(MODEL_FILE_PATH))
    return {"version": file_mtime}

@app.get("/api/v1/model/latest")
async def get_latest_model():
    if not os.path.exists(MODEL_FILE_PATH):
        raise HTTPException(status_code=404, detail="Model file not found")
    
    return FileResponse(
        path=MODEL_FILE_PATH,
        media_type="application/octet-stream",
        filename="iaq_model.tflite"
    )

# --- 4. CHẠY SERVER ---
if __name__ == "__main__":
    # ĐÃ SỬA: Chỉ định rõ đường dẫn "backend.main:app" thay vì "main:app"
    uvicorn.run("backend.main:app", host="0.0.0.0", port=8000, reload=True)