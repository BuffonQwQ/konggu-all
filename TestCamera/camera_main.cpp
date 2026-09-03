#include <opencv2/opencv.hpp>
#include <iostream>

int main()
{
    // 打开摄像头（通常为 /dev/video0）
    cv::VideoCapture cap(0); // 0 表示第一个摄像头设备
    if (!cap.isOpened())
    {
        std::cerr << "无法打开摄像头" << std::endl;
        return -1;
    }

    // 设置一个非常大的分辨率值，摄像头会自动调整为它支持的最大分辨率
    // cap.set(cv::CAP_PROP_FRAME_WIDTH, 2560);  // 设置宽度为 2560
    // cap.set(cv::CAP_PROP_FRAME_HEIGHT, 1440); // 设置高度为 1440

    // 获取实际的分辨率（摄像头可能调整为最大支持的分辨率）
    double maxWidth = cap.get(cv::CAP_PROP_FRAME_WIDTH);
    double maxHeight = cap.get(cv::CAP_PROP_FRAME_HEIGHT);
    std::cout << "Max Resolution: " << maxWidth << "x" << maxHeight << std::endl;
    cv::Mat frame;

    // 从摄像头读取一帧
    cap >> frame;
    if (frame.empty())
    {
        std::cerr << "无法获取摄像头画面" << std::endl;
    }

    // 显示图像
    // cv::imshow("Camera", frame);
    cv::imwrite("Camera.jpg", frame);

    // 释放摄像头资源
    cap.release();
    cv::destroyAllWindows();
    return 0;
}
