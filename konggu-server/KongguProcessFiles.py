import os
import time
import threading
import cv2
import torch
from ultralytics import YOLO
import numpy as np
import re
from pairAlign import pair_align_func_konggu


def xywh_to_newxywh_and_ltrb(xywh_boxes, h_max, h_zoom_in_scale=1):
    """
    Convert bounding boxes from xywh format to left_top and right_bottom format.
    h_max表示orig_img的高，避免h zoom in 倍数设的太大之后溢出了
    h may zoom in for `h_zoom_in_scale` scale to contain the battery
    """
    x, y, w, h = xywh_boxes
    if y + h * (h_zoom_in_scale - 0.5) < h_max:
        new_h = h * h_zoom_in_scale
        rb_h = y + h * (h_zoom_in_scale - 0.5)
    else:
        new_h = h_max - 1 - y
        rb_h = h_max - 1
    return [int(x) for x in (x, y, w, new_h)], [int(x) for x in (int(x - w / 2), int(y - h / 2), int(x + w / 2), rb_h)]


# 监控目录并处理文件对
class KongguProcessor:
    def __init__(self, directory, out_dir, model_path):
        self.directory = directory
        self.processed_files = set()  # 存储已处理的文件
        self.unprocessed_files = set()  # 存储未处理的文件
        self.file_pairs = []  # 存储处理完成的文件对
        self.list_lock = threading.Lock()
        self.isAlarm = 0
        self.alarm_lock = threading.Lock()
        self.out_dir = out_dir
        os.makedirs(self.out_dir, exist_ok=True)
        self.model = YOLO(model_path)

    # 处理文件对的函数
    def process_file_pair(self, temp_file, camera_file):
        try:
            temp_path = os.path.join(self.directory, temp_file) # 拼接路径
            camera_path = os.path.join(self.directory, camera_file)

            # 对齐可见光和热成像图像并保存 (缩放比例为1.5，平移量为100, -10)
            hot_img, rgb_img, blend_img = pair_align_func_konggu(temp_path, camera_path, rescale_factor=1.5, trans_x=100, trans_y=-10) 
            cv2.imwrite(f'{self.out_dir}/{re.split("[./_]", camera_file)[-3]}_blended.jpg',
                        blend_img)

            # 使用训练好的模型进行语义分割  #####? 为什么这里模型推理使用的是rgb_img？ #####
            with torch.no_grad():
                results = self.model(rgb_img)

            result = results[0]
            cls = result.boxes.cls # 检测到的目标类别
            boxes_xywh = result.boxes.xywh # 检测框的位置（中心坐标、宽度、高度）
            orig_img = result.orig_img # 原始图像
            h, w, _ = orig_img.shape # 原始图像的高宽
            new_width = 640 # 缩放后的宽
            new_height = int(new_width * h / w) # 缩放后的高
            ############################################################ 什么作用?
            for i in range(len(cls)):
                # 识别出来是电动车的座位
                if cls[i] == 0:
                    # 将框往下延伸一点，包住电池所在区域
                    new_xywh, ltrb = xywh_to_newxywh_and_ltrb(boxes_xywh[i], h_max=h, h_zoom_in_scale=1)
                    cv2.rectangle(orig_img, (ltrb[0], ltrb[1]), (ltrb[2], ltrb[3]), color=(0, 0, 255), thickness=2)
            ############################################################

            # Q:为什么要在保存和显示时处理两次大小,不处理一次然后直接保存和展示处理后的图像(并不重要)
            # 将处理后的图像调整为对应大小(cv2.resize)并保存
            cv2.imwrite(f'{self.out_dir}/{re.split("[./_]", camera_file)[-3]}.jpg',
                        cv2.resize(orig_img, (new_width, new_height)))
            # 显示处理后的图像
            cv2.imshow('result window', cv2.resize(orig_img, (new_width, new_height)))
            cv2.waitKey(30)

            # 处理出现错误就返回False
            return True
        except Exception as e:
            print(e)
            return False

    # 检查目录中所有文件
    def check_for_new_files(self):
        for filename in os.listdir(self.directory):
            filepath = os.path.join(self.directory, filename)
            if os.path.isfile(filepath):
                # 只处理以 .jpg 结尾的文件
                if filename.endswith(".jpg"):
                    if filename not in self.processed_files:
                        self.unprocessed_files.add(filename)

    # 处理文件对
    def process_file_pairs(self):
        for filename in list(self.unprocessed_files): # 遍历未处理的文件列表
            temp_file = None
            camera_file = None
            prefix = None

            # 提取文件名的前缀（例如 '1'）
            if "_" in filename:
                prefix = filename.split("_")[0]

                # 查找对应的文件对
                # Q:在遍历过程中，同一对的图像是不是先遍历到其中一个(A)，就会通过命名法直接命名出B的路径然后使用。
                # 这样的话，遍历到B的时候，是否会重复处理文件对AB。
                # 还是说，每次处理都会直接删除A和B，下一次循环时，列表里已经没有AB两个文件了，所以不会重复处理。
                # A：不会。所以配对逻辑是，只要找到就直接用对应的编号找出另一张图像的路径。然后使用。使用后从未处理中删除
                if "temp" in filename:
                    temp_file = filename
                    camera_file = f"{prefix}_camera.jpg"
                elif "camera" in filename:
                    camera_file = filename
                    temp_file = f"{prefix}_temp.jpg"

                # 如果找到了配对的文件
                if temp_file and camera_file: #if和and运算可以保证两个字符变量不为空
                    # 确保文件都在未处理列表中(文件存在）
                    if temp_file in self.unprocessed_files and camera_file in self.unprocessed_files: 
                        # 处理文件对(裁剪对齐，标记目标框，保存并显示)
                        if not self.process_file_pair(temp_file, camera_file):
                            continue # 出现错误就跳过

                        # 将已处理的文件标记为已处理，并从未处理文件中删除(防止重复处理)
                        self.processed_files.add(temp_file)
                        self.processed_files.add(camera_file)
                        self.unprocessed_files.remove(temp_file)
                        self.unprocessed_files.remove(camera_file)

                        # 将已处理的文件对添加到已处理列表
                        self.file_pairs.append((temp_file, camera_file))

    # 主循环，持续监控文件夹变化并处理文件对
    def monitor_directory(self):
        while True:
            with self.list_lock:
                self.check_for_new_files() # 不断添加获取到的新文件
                self.process_file_pairs() # 处理文件对
            time.sleep(1)  # 每秒钟检查一次

    # 清空文件夹和列表 重置状态
    def clean(self):
        with self.list_lock:
            self.cleanFolder() # 清空文件夹
            self.unprocessed_files.clear() # 清空列表
            self.processed_files.clear()
            self.file_pairs.clear()
        with self.alarm_lock:
            self.isAlarm = 0  # 重置报警状态
        cv2.destroyAllWindows()

    # 重置报警状态 
    def resetAlarm(self):
        with self.alarm_lock:
            self.isAlarm = 0
    
    # 清空文件夹函数
    def cleanFolder(self):
        if os.path.exists(self.directory):
            for filename in os.listdir(self.directory):
                file_path = os.path.join(self.directory, filename)
                if os.path.isfile(file_path):  # 确保是文件而不是文件夹
                    os.remove(file_path)  # 删除文件
        if os.path.exists(self.out_dir):
            for filename in os.listdir(self.out_dir):
                file_path = os.path.join(self.out_dir, filename)
                if os.path.isfile(file_path):  # 确保是文件
                    os.remove(file_path)  # 删除文件

