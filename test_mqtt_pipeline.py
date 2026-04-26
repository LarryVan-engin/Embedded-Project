"""
Test MQTT Data Processing Pipeline
Kiểm tra quy trình xử lý dữ liệu MQTT từ ESP32
"""

import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from backend.mqtt_client import mqtt_service
from backend.database_manager import db_manager
import re

# Test data samples từ ESP32
TEST_MQTT_MESSAGES = [
    "[547034 ms] Published: TVOC=144.0ppb | Actual=1.86 | Predict=1.80",
    "[548171 ms] [sensor:263] T=31.1 C  RH=46.9%",
    "[550255 ms] [sensor:264] T=31.1 C  RH=46.3%",
    "[552043 ms] Published: TVOC=152.5ppb | Actual=1.92 | Predict=1.88",
]

def test_mqtt_parser():
    """Test parsing MQTT payload"""
    print("=" * 60)
    print("🧪 TEST 1: MQTT Parser")
    print("=" * 60)
    
    for msg in TEST_MQTT_MESSAGES:
        print(f"\n📨 Input: {msg}")
        data = mqtt_service.parse_mqtt_payload(msg)
        print(f"✅ Parsed: {data}")
        
        # Verify
        if 'tvoc' in data:
            assert isinstance(data['tvoc'], float), "TVOC phải là float"
            print(f"   ✓ TVOC: {data['tvoc']} ppb")
        
        if 'iaq_actual' in data:
            assert 1.0 <= data['iaq_actual'] <= 5.0, "IAQ phải trong 1.0-5.0"
            print(f"   ✓ IAQ Actual: {data['iaq_actual']} (UBA)")
        
        if 'iaq_forecast' in data:
            assert 1.0 <= data['iaq_forecast'] <= 5.0, "IAQ phải trong 1.0-5.0"
            print(f"   ✓ IAQ Predict: {data['iaq_forecast']} (UBA)")
        
        if 'temperature' in data:
            assert -40 <= data['temperature'] <= 85, "Nhiệt độ hợp lý"
            print(f"   ✓ Temperature: {data['temperature']}°C")
        
        if 'humidity' in data:
            assert 0 <= data['humidity'] <= 100, "Độ ẩm 0-100%"
            print(f"   ✓ Humidity: {data['humidity']}%")

def test_database_storage():
    """Test lưu dữ liệu vào database"""
    print("\n" + "=" * 60)
    print("🧪 TEST 2: Database Storage")
    print("=" * 60)
    
    # Test data
    test_record = {
        'tvoc': 144.0,
        'iaq_actual': 1.86,
        'iaq_forecast': 1.80,
        'temperature': 31.1,
        'humidity': 46.9
    }
    
    # Lưu vào DB
    success = db_manager.save_log(**test_record)
    assert success, "Lỗi lưu dữ liệu"
    print(f"✅ Saved record: {test_record}")
    
    # Kiểm tra DB
    count = db_manager.get_data_count()
    print(f"📊 Total records in DB: {count}")
    
    # Lấy record mới nhất
    latest = db_manager.get_latest_record()
    print(f"✅ Latest record: {latest}")
    
    assert latest is not None, "Không tìm thấy record"
    assert latest['tvoc'] == 144.0, "TVOC không khớp"
    print("   ✓ Data integrity verified")

def test_data_validation():
    """Test xác thực dữ liệu"""
    print("\n" + "=" * 60)
    print("🧪 TEST 3: Data Validation")
    print("=" * 60)
    
    # Valid payload
    valid_payload = "[547034 ms] Published: TVOC=144.0ppb | Actual=1.86 | Predict=1.80"
    data = mqtt_service.parse_mqtt_payload(valid_payload)
    
    # Check required fields
    required_fields = ['tvoc', 'iaq_actual', 'iaq_forecast']
    has_all = all(k in data for k in required_fields)
    print(f"✅ Valid payload contains all required fields: {has_all}")
    assert has_all, "Payload thiếu trường bắt buộc"
    
    # Test incomplete payload
    incomplete_payload = "[548171 ms] [sensor:263] T=31.1 C  RH=46.9%"
    data2 = mqtt_service.parse_mqtt_payload(incomplete_payload)
    has_required = all(k in data2 for k in required_fields)
    print(f"❌ Incomplete payload missing required fields: {not has_required}")
    assert not has_required, "Payload này thiếu TVOC/Actual/Predict"
    print("   ✓ Data validation working correctly")

def test_regex_patterns():
    """Test các regex pattern"""
    print("\n" + "=" * 60)
    print("🧪 TEST 4: Regex Patterns")
    print("=" * 60)
    
    test_cases = [
        ("TVOC=144.0ppb", r'TVOC=(\d+\.?\d*)\s*ppb', 144.0),
        ("Actual=1.86", r'Actual=(\d+\.?\d*)', 1.86),
        ("Predict=1.80", r'Predict=(\d+\.?\d*)', 1.80),
        ("T=31.1 C", r'T=(\d+\.?\d*)\s*C', 31.1),
        ("RH=46.9%", r'RH=(\d+\.?\d*)%', 46.9),
    ]
    
    for text, pattern, expected in test_cases:
        match = re.search(pattern, text)
        if match:
            value = float(match.group(1))
            status = "✅" if value == expected else "❌"
            print(f"{status} Pattern '{pattern}' matches '{text}' = {value}")
            assert value == expected, f"Expected {expected}, got {value}"
        else:
            print(f"❌ Pattern '{pattern}' NOT matched in '{text}'")
            raise AssertionError(f"Pattern mismatch for {text}")

def test_edge_cases():
    """Test các trường hợp đặc biệt"""
    print("\n" + "=" * 60)
    print("🧪 TEST 5: Edge Cases")
    print("=" * 60)
    
    # Case 1: Very large TVOC value
    payload1 = "[500000 ms] Published: TVOC=5000.0ppb | Actual=5.0 | Predict=4.95"
    data1 = mqtt_service.parse_mqtt_payload(payload1)
    print(f"✅ Large TVOC: {data1.get('tvoc')} ppb")
    
    # Case 2: Very small temperature
    payload2 = "[500001 ms] [sensor:1] T=0.5 C  RH=10.0%"
    data2 = mqtt_service.parse_mqtt_payload(payload2)
    print(f"✅ Small Temperature: {data2.get('temperature')}°C, Humidity: {data2.get('humidity')}%")
    
    # Case 3: Maximum humidity
    payload3 = "[500002 ms] [sensor:1] T=25.0 C  RH=99.9%"
    data3 = mqtt_service.parse_mqtt_payload(payload3)
    print(f"✅ High Humidity: {data3.get('humidity')}%")

def main():
    print("\n")
    print("╔" + "=" * 58 + "╗")
    print("║" + " " * 58 + "║")
    print("║" + "  🔧 MQTT Data Processing Pipeline - Test Suite  ".center(58) + "║")
    print("║" + " " * 58 + "║")
    print("╚" + "=" * 58 + "╝")
    
    try:
        test_regex_patterns()
        test_mqtt_parser()
        test_data_validation()
        test_database_storage()
        test_edge_cases()
        
        print("\n" + "=" * 60)
        print("✅ ALL TESTS PASSED!")
        print("=" * 60)
        print("\n✨ Quy trình xử lý MQTT đã hoạt động chính xác!")
        
    except AssertionError as e:
        print(f"\n❌ TEST FAILED: {e}")
        return False
    except Exception as e:
        print(f"\n❌ UNEXPECTED ERROR: {e}")
        import traceback
        traceback.print_exc()
        return False
    
    return True

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
