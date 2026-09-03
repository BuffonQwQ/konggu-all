import cv2

# 打开摄像头
cap = cv2.VideoCapture(0)  # 0表示打开默认摄像头

# 检查摄像头是否成功打开
if not cap.isOpened():
    print("Error: Couldn't open the camera.")
    exit()

# 设置一个较大的分辨率，摄像头将调整为其支持的最大分辨率
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 4096)  # 设置宽度为 1920
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 2160) # 设置高度为 1080

# 获取实际的分辨率（可能会被摄像头调整为最大支持的分辨率）
width = cap.get(cv2.CAP_PROP_FRAME_WIDTH)
height = cap.get(cv2.CAP_PROP_FRAME_HEIGHT)
print(f"Max Resolution: {width}x{height}")

# 捕获视频流并显示
while True:
    ret, frame = cap.read()
    if not ret:
        print("Error: Couldn't fetch frame.")
        break

    # 显示图像
    cv2.imshow("Camera", frame)

    # 按 'q' 键退出
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

# 释放摄像头和关闭窗口
cap.release()
cv2.destroyAllWindows()
