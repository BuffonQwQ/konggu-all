import cv2
import os
import numpy as np


def merge_images_with_overlap(folder_path, overlap_length=50, output_filename="merged_image.jpg"):
    # 获取所有文件，按数字顺序排序
    filenames = sorted([f for f in os.listdir(folder_path) if f.endswith('.jpg')],
                        key=lambda x: int(x.split('.')[0]))  # 按文件名前面的数字排序

    images = []

    for filename in filenames:
        # 读取每张图像
        img = cv2.imread(os.path.join(folder_path, filename))
        images.append(img)

    # 获取第一张图片的尺寸（假设所有图片尺寸相同）
    img_height, img_width, channels = images[0].shape

    # 合并图片
    merged_image = images[0]  # 初始化合并图像为第一张图片

    for i in range(1, len(images)):
        # 当前图片和上一张图片的重叠部分
        current_img = images[i]

        # 取当前图像和前一张图像的重叠区域
        overlap_img = merged_image[:, -overlap_length:]

        # 当前图像去掉重叠部分
        current_img_no_overlap = current_img[:, overlap_length:]

        # 合并当前图像的非重叠部分
        merged_image = np.hstack((merged_image, current_img_no_overlap))

    # 保存合并后的大图
    cv2.imwrite(output_filename, merged_image)

    return merged_image


if __name__=="__main__":
    # 调用函数示例
    folder_path = r"G:\codes\RapberryPIBackend\test_merge_folder"  # 替换为图片文件夹路径
    merged_image = merge_images_with_overlap(folder_path, overlap_length=50, output_filename="merged_image.jpg")
    cv2.imshow("Merged Image", merged_image)
    cv2.waitKey(0)
    cv2.destroyAllWindows()
