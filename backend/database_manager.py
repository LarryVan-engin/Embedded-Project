import sqlite3
from datetime import datetime

class DatabaseManager:
    def __init__(self, db_name="iaq_history.db"):
        """Khởi tạo kết nối và tạo bảng nếu chưa tồn tại."""
        self.db_name = db_name
        self.init_db()

    def init_db(self):
        """Tạo cấu trúc bảng để lưu trữ dữ liệu từ Kit CK-RA6M5."""
        conn = sqlite3.connect(self.db_name)
        cursor = conn.cursor()
        # Bảng lưu trữ dữ liệu thô và kết quả dự đoán từ Edge AI
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS air_quality_logs (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp DATETIME,
                eco2 REAL,
                tvoc REAL,
                aqi_predicted REAL
            )
        ''')
        conn.commit()
        conn.close()

    def save_log(self, eco2, tvoc, aqi_pred):
        """Lưu bản tin sensor vào database phục vụ việc học lại sau này."""
        try:
            conn = sqlite3.connect(self.db_name)
            cursor = conn.cursor()
            now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            cursor.execute('''
                INSERT INTO air_quality_logs (timestamp, eco2, tvoc, aqi_predicted)
                VALUES (?, ?, ?, ?)
            ''', (now, eco2, tvoc, aqi_pred))
            conn.commit()
            conn.close()
            return True
        except Exception as e:
            print(f"❌ Database Error: {e}")
            return False

    def get_recent_data(self, limit=100):
        """Lấy dữ liệu gần nhất để hiển thị trên Dashboard."""
        conn = sqlite3.connect(self.db_name)
        cursor = conn.cursor()
        cursor.execute("SELECT * FROM air_quality_logs ORDER BY timestamp DESC LIMIT ?", (limit,))
        rows = cursor.fetchall()
        conn.close()
        return rows

    def get_data_for_retraining(self):
        """Trích xuất toàn bộ Database để Server tiến hành huấn luyện lại model MLP."""
        conn = sqlite3.connect(self.db_name)
        # Sử dụng pandas để trích xuất nhanh cho việc training AI
        import pandas as pd
        df = pd.read_sql_query("SELECT * FROM air_quality_logs", conn)
        conn.close()
        return df

# Khởi tạo instance dùng chung
db_manager = DatabaseManager()