import sqlite3
import pandas as pd
from datetime import datetime

class DatabaseManager:
    def __init__(self, db_name="backend/iaq_history.db"):
        """Khởi tạo kết nối và tạo bảng nếu chưa tồn tại."""
        self.db_name = db_name
        self.initialize_db()

    def initialize_db(self):
        """Tạo cấu trúc bảng chuẩn UBA để lưu trữ dữ liệu từ hệ thống Edge AI."""
        conn = sqlite3.connect(self.db_name)
        cursor = conn.cursor()
        
        # Bảng lưu trữ với temperature, humidity
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS air_quality_logs (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp DATETIME,
                tvoc REAL,
                iaq_actual REAL,
                iaq_forecast REAL,
                temperature REAL,
                humidity REAL
            )
        ''')
        conn.commit()
        
        # Kiểm tra migration: nếu bảng cũ không có các cột mới, thêm chúng
        cursor.execute("PRAGMA table_info(air_quality_logs)")
        columns = {row[1] for row in cursor.fetchall()}
        
        if 'temperature' not in columns:
            print("⚠️ Migrating database: Adding temperature column...")
            cursor.execute("ALTER TABLE air_quality_logs ADD COLUMN temperature REAL")
            conn.commit()
        
        if 'humidity' not in columns:
            print("⚠️ Migrating database: Adding humidity column...")
            cursor.execute("ALTER TABLE air_quality_logs ADD COLUMN humidity REAL")
            conn.commit()
        
        # Xóa cột eco2 nếu tồn tại (không cần thiết trong mô hình hiện tại)
        if 'eco2' in columns:
            print("ℹ️ Note: eco2 column will not be used (model uses TVOC only)")
        
        conn.close()

    def save_log(self, tvoc, iaq_actual, iaq_forecast, temperature=None, humidity=None):
        """Lưu bản tin sensor vào database (Được gọi bởi mqtt_client.py)"""
        try:
            conn = sqlite3.connect(self.db_name)
            cursor = conn.cursor()
            now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            cursor.execute('''
                INSERT INTO air_quality_logs (timestamp, tvoc, iaq_actual, iaq_forecast, temperature, humidity)
                VALUES (?, ?, ?, ?, ?, ?)
            ''', (now, tvoc, iaq_actual, iaq_forecast, temperature, humidity))
            conn.commit()
            conn.close()
            return True
        except Exception as e:
            print(f"❌ Database Error (save_log): {e}")
            return False

    def get_data_count(self):
        """Đếm tổng số mẫu dữ liệu (Được gọi bởi mqtt_client.py để trigger Retrain)"""
        try:
            conn = sqlite3.connect(self.db_name)
            cursor = conn.cursor()
            cursor.execute("SELECT COUNT(*) FROM air_quality_logs")
            count = cursor.fetchone()[0]
            conn.close()
            return count
        except Exception as e:
            print(f"❌ Database Error (get_data_count): {e}")
            return 0

    def get_latest_record(self):
        """Lấy 1 bản ghi mới nhất (Được gọi bởi API /latest của FastAPI)"""
        conn = sqlite3.connect(self.db_name)
        conn.row_factory = sqlite3.Row # Trả về dạng dictionary thay vì tuple
        cursor = conn.cursor()
        cursor.execute("SELECT * FROM air_quality_logs ORDER BY id DESC LIMIT 1")
        row = cursor.fetchone()
        conn.close()
        return dict(row) if row else None

    def get_history(self, limit=100):
        """Lấy lịch sử dạng JSON (Được gọi bởi API /history của FastAPI)"""
        conn = sqlite3.connect(self.db_name)
        conn.row_factory = sqlite3.Row
        cursor = conn.cursor()
        cursor.execute("SELECT * FROM air_quality_logs ORDER BY id DESC LIMIT ?", (limit,))
        rows = cursor.fetchall()
        conn.close()
        # Trả về list các dict (dễ dàng convert sang JSON cho frontend)
        return [dict(row) for row in rows]

    def get_data_for_retraining(self):
        """Trích xuất Database thành DataFrame cho Streamlit vẽ biểu đồ và AI học lại."""
        conn = sqlite3.connect(self.db_name)
        # Sử dụng pandas để trích xuất nhanh
        df = pd.read_sql_query("SELECT * FROM air_quality_logs ORDER BY timestamp ASC", conn)
        conn.close()
        return df

    def clear_old_data(self, keep_last=5000):
        """(Tùy chọn) Xóa dữ liệu cũ để tránh Database quá nặng cho Raspberry Pi / PC."""
        conn = sqlite3.connect(self.db_name)
        cursor = conn.cursor()
        cursor.execute('''
            DELETE FROM air_quality_logs 
            WHERE id NOT IN (
                SELECT id FROM air_quality_logs ORDER BY id DESC LIMIT ?
            )
        ''', (keep_last,))
        conn.commit()
        conn.close()

db_manager = DatabaseManager()
