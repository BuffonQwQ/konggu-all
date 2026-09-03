import os.path

import cv2
import numpy as np
from PIL import Image
import matplotlib.cm as cm
import matplotlib.colors as mcolors


def pair_align_func_konggu(temp_path, camera_path, rescale_factor=1.5, trans_x=100, trans_y=-10):
    # 读取两张图像
    infrared_img = cv2.imread(temp_path, cv2.IMREAD_COLOR)  # 红外图 (bmp)
    visible_img = cv2.imread(camera_path, cv2.IMREAD_COLOR)  # 可见光图 (jpg)

    # 获取图像的尺寸
    h_infrared, w_infrared = infrared_img.shape[:2]
    h_visible, w_visible = visible_img.shape[:2]
    visible_img = cv2.resize(visible_img, (int(w_visible * rescale_factor), int(h_visible * rescale_factor)))
    h_visible, w_visible = visible_img.shape[:2]

    # 计算图像中心
    infrared_center = (w_infrared // 2, h_infrared // 2)
    visible_center = (w_visible // 2, h_visible // 2)

    # 计算偏差（用于对齐两张图像）
    dx = visible_center[0] - infrared_center[0]
    dy = visible_center[1] - infrared_center[1]
    print(f"Center Offset: dx = {dx}, dy = {dy}")

    # 计算透视变换矩阵：平移矩阵
    # 平移矩阵的格式为 [[1, 0, dx], [0, 1, dy]]
    translation_matrix = np.float32([[1, 0, dx + trans_x], [0, 1, dy + trans_y]])
    # translation_matrix = np.float32([[1, 0, dx], [0, 1, dy]])
    #
    # 对红外图进行平移变换，使其中心与可见光图的中心对齐
    aligned_infrared_img = cv2.warpAffine(infrared_img, translation_matrix, (w_visible, h_visible))

    # cv2.imshow('aligned', aligned_infrared_img)
    # cv2.waitKey(0)

    # 将两张图像叠加在一起，叠加效果可以调整，下面的代码将显示两张图像的合成效果
    blended_img = cv2.addWeighted(aligned_infrared_img, 0.5, visible_img, 0.5, 0)

    # 使用 PIL 显示叠加后的结果
    # cv2.imshow('blended', blended_img)
    # cv2.waitKey(0)



    # 保存最终结果
    #
    # # 提取覆盖区域：将对齐后的红外图像区域与可见光图像交集部分提取出来
    # # 我们将裁剪对齐后，红外图像所覆盖的区域在可见光图像上的对应部分
    x_min = max(0, visible_center[0] + trans_x - infrared_center[0])
    y_min = max(0, visible_center[1] + trans_y - infrared_center[1])
    x_max = min(w_visible, visible_center[0] + trans_x + infrared_center[0])  # 确保不会超出宽度
    y_max = min(h_visible, visible_center[1] + trans_y + infrared_center[1])  # 确保不会超出高度
    print('{} {} {} {}'.format(y_min, y_max, x_min, x_max))
    print(visible_center)
    print(visible_img.shape[:2])
    # 提取被红外图像覆盖的区域
    aligned_infrared_img = aligned_infrared_img[y_min:y_max, x_min:x_max]
    covered_visible_img = visible_img[y_min:y_max, x_min:x_max]
    #
    # # 保存提取的区域
    # cv2.imwrite("covered_visible_img.png", covered_visible_img)
    #
    # # # 显示结果
    # # cv2.imshow("Covered Visible Image", covered_visible_img)
    # # cv2.waitKey(0)
    # # cv2.destroyAllWindows()

    return aligned_infrared_img, covered_visible_img, blended_img

if __name__ == "__main__":
    pair_align_func_konggu('uploads_ebike/3_temp.jpg', 'uploads_ebike/3_camera.jpg')


