import torch.cuda
from ultralytics import YOLO
import cv2
from flask import Flask, request
import numpy as np
import scipy
import os
from natsort import natsort
import re
from KongguProcessFiles import KongguProcessor
import time
import shutil
import threading
from MergeImage import merge_images_with_overlap

# 加载模型
while True:
    # 获取用户输入
    user_input = input("提供低温版本和高温版本模型，请根据使用场景选择适用的模型：键入1为低温，键入2为高温: ")

    # 根据输入选择模型
    if user_input == '1':
        model_path="konggu_models/cold-best.pt"
        print("已加载低温版本模型A.pt")
        break  # 跳出循环
    elif user_input == '2':
        model_path = "konggu_models/hot-best.pt"
        print("已加载高温版本模型B.pt")
        break  # 跳出循环
    else:
        print("无效的输入，请键入1或2来选择模型。")

upload_dir = 'uploads_konggu'
out_dir = 'out_konggu'

# 创建一个锁和队列
lock = threading.Lock()
temp_file_path_list = []
camera_file_path_list = []
cur_temp_file = None
cur_camera_file = None
processor = None

last_time=None

################################################## 有使用吗？？
def show_data():
    global temp_file_path_list, cur_temp_file, camera_file_path_list, cur_camera_file
    while True:
        with lock:
            if len(temp_file_path_list) > 0 and len(camera_file_path_list) > 0:
                # 匹配文件名
                if re.split('[/_.]', temp_file_path_list[0])[-3] == re.split('[/_.]', camera_file_path_list[0])[-3]:
                    cur_temp_file = temp_file_path_list[0]  # 取出第一个文件路径
                    cur_camera_file = camera_file_path_list[0]
                    temp_file_path_list.pop(0) # pop:删除列表中的第一个元素，并且后续元素全部往前移1位
                    camera_file_path_list.pop(0)
        if cur_temp_file is None or cur_camera_file is None:
            time.sleep(0.01)
            continue
###################################################<<<<<<<<<<<

def resize_and_align(rgb_img, scale, delta_h, delta_w):
    """
    与其缩放mat文件，还不如缩放rgb文件（缩小，还省点运算时间），然后做中心对齐&裁剪
    """


app = Flask(__name__)

@app.route('/clean', methods=['GET'])
def clean():
    global cur_temp_file, cur_camera_file, temp_file_path_list, camera_file_path_list, isAlarm
    try:
        processor.clean() # 清空处理器的缓存
        with lock:
            cur_temp_file = None #重置路径
            cur_camera_file = None
            temp_file_path_list.clear()  # 清空文件路径列表
            camera_file_path_list.clear()
    except:
        return 'Failed to clean!'
    return 'Successfully cleaned!'


# 确保有一个接收文件的路由
@app.route('/upload', methods=['POST'])
def upload_file():
    if 'file' not in request.files: # 检测请求中是否有文件
        return 'No file part', 400
    file = request.files['file']
    if file.filename == '': # 检查文件名是否为空
        return 'No selected file', 400
    # 保存文件
    os.makedirs(upload_dir, exist_ok=True) # 确保文件夹存在，如果不存在就创建
    sav_p = os.path.join(upload_dir, file.filename)
    file.save(sav_p)
    global last_time, temp_file_path_list, camera_file_path_list 
    last_time=time.time() # 记录当前的时间戳
    if sav_p.endswith('temp.jpg'):  # 分类图片类型，加入不同列表
        with lock:
            temp_file_path_list.append(sav_p)  # 将文件路径加入队列
    if sav_p.endswith('camera.jpg'):
        with lock:
            camera_file_path_list.append(sav_p)  # 将文件路径加入队列
    return {'result': 1}, 200, {"Content-Type": "application/json"}

######################################################################################## 哪里用上了？？
def monitor_last_time():
    while True:
        cur_time=time.time()
        if last_time is None:
            time.sleep(2)
            continue
        diff=cur_time-last_time
        if diff > 2:
            # 合成全景图
            merge_images_with_overlap(out_dir, overlap_length=50, output_filename=f'{out_dir}/merged.jpg')
            break
        time.sleep(2)
#########################################################################################

if __name__ == '__main__':
    os.makedirs(upload_dir, exist_ok=True) # 创建传输文件夹
    os.makedirs(out_dir, exist_ok=True) # 创建输出文件夹
    # 启动后台线程来显示图像
    # t = threading.Thread(target=show_data, daemon=True)
    # t.start()

    # 初始化文件处理器并启动文件监控
    processor = KongguProcessor(upload_dir, out_dir, model_path)
    processor.clean() 

    # 启动一个线程来监控文件夹
    monitoring_thread = threading.Thread(target=processor.monitor_directory)
    # monitoring_thread.daemon = True  # 设置为守护线程，主程序退出时线程也会退出
    monitoring_thread.start()

    # 启动 Flask 服务器
    app.run(host='0.0.0.0', port=8088)