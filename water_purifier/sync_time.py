"""Sync Beijing time to Arduino over Serial."""
import serial
import time

PORT = "COM6"      # 改成你的 Arduino 端口号
BAUD = 9600

try:
    ser = serial.Serial(PORT, BAUD, timeout=2)
    print(f"Connected to {PORT}")
    # 等待 Arduino 复位完成
    time.sleep(2.5)
    # 读取启动信息
    ser.reset_input_buffer()
    # 发送当前北京时间 HH:MM:SS
    t = time.localtime()
    msg = f"{t.tm_hour:02d}:{t.tm_min:02d}:{t.tm_sec:02d}\n"
    ser.write(msg.encode())
    print(f"Sent: {msg.strip()}")
    ser.close()
    print("Done!")
except Exception as e:
    print(f"Error: {e}")
    print("Make sure Arduino IDE Serial Monitor is CLOSED first!")
