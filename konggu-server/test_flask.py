import threading
import time
import cv2
from flask import Flask, request
import os
import shutil
from PIL import Image, ImageTk
import numpy as np
from tkinter import Tk, Canvas
import queue
import re

# 创建一个锁和队列
lock = threading.Lock()
temp_file_path_list = []
camera_file_path_list = []
cur_temp_file = None
cur_camera_file = None


def show_data():
    global temp_file_path_list, cur_temp_file, camera_file_path_list, cur_camera_file
    while True:
        with lock:
            if len(temp_file_path_list) > 0 and len(camera_file_path_list) > 0:
                if re.split('[/_.]', temp_file_path_list[0])[-3] == re.split('[/_.]', camera_file_path_list[0])[-3]:
                    cur_temp_file = temp_file_path_list[0]  # 取出第一个文件路径
                    cur_camera_file = camera_file_path_list[0]
                    temp_file_path_list.pop(0)
                    camera_file_path_list.pop(0)

        if cur_temp_file is None or cur_camera_file is None:
            time.sleep(0.01)
            continue

        temp_img = cv2.imread(cur_temp_file)
        camera_img = cv2.imread(cur_camera_file)
        if temp_img is not None and camera_img is not None:
            cv2.imshow('temperature window', temp_img)
            cv2.waitKey(30)
            cv2.imshow('camera window', camera_img)
            cv2.waitKey(30)


app = Flask(__name__)


@app.route('/', methods=['GET'])
def get():
    return 'hello!'


@app.route('/clean', methods=['GET'])
def clean():
    global cur_temp_file, cur_camera_file, temp_file_path_list, camera_file_path_list
    try:
        if os.path.exists('uploads'):
            shutil.rmtree('uploads')
            with lock:
                cur_temp_file = None
                cur_camera_file = None
                temp_file_path_list.clear()
                camera_file_path_list.clear()

    except:
        return 'Failed to clean!'
    return 'Successfully cleaned!'


# 确保有一个接收文件的路由
@app.route('/upload', methods=['POST'])
def upload_file():
    global temp_file_path_list, camera_file_path_list
    if 'file' not in request.files:
        return 'No file part', 400
    file = request.files['file']
    if file.filename == '':
        return 'No selected file', 400

    # 保存文件
    sav_dir = 'uploads'
    os.makedirs(sav_dir, exist_ok=True)
    sav_p = os.path.join(sav_dir, file.filename)
    file.save(sav_p)
    if sav_p.endswith('temp.jpg'):
        with lock:
            temp_file_path_list.append(sav_p)  # 将文件路径加入队列
    if sav_p.endswith('camera.jpg'):
        with lock:
            camera_file_path_list.append(sav_p)  # 将文件路径加入队列

    return {'result': 1}, 200, {"Content-Type": "application/json"}


if __name__ == '__main__':
    # 启动后台线程来显示图像
    t = threading.Thread(target=show_data)  # 设置为守护线程
    t.start()

    # 启动 Flask 服务器
    app.run(debug=True, host='0.0.0.0', port=8080)
