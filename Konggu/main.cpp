

#if _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#endif
#include <iostream>
#include <thread>
#include "UThermalLib.h"
#include <opencv2/opencv.hpp>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <ctime>
#include <chrono>
using nlohmann::json;
using namespace std;
#include <mutex>
#include <vector>
#include <dirent.h>
#include <cstring>
#include <cstdio>
#include <sys/types.h>
#include <sys/stat.h>

// 删除指定目录下的所有文件
bool deleteFilesInDirectory(const char *path)
{
    DIR *dir = opendir(path); // 打开目录
    if (dir == nullptr)
    {
        std::cerr << "无法打开目录: " << strerror(errno) << std::endl;
        return false;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        // 排除 "." 和 ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        // 构建文件的完整路径
        std::string filePath = std::string(path) + "/" + entry->d_name;

        struct stat fileInfo;
        if (stat(filePath.c_str(), &fileInfo) == 0 && S_ISREG(fileInfo.st_mode))
        {
            // 如果是文件，尝试删除
            if (remove(filePath.c_str()) != 0)
            {
                std::cerr << "删除文件失败: " << filePath << std::endl;
                closedir(dir);
                return false;
            }
            else
            {
                std::cout << "删除文件成功: " << filePath << std::endl;
            }
        }
    }

    closedir(dir); // 关闭目录
    return true;
}

std::mutex mtx; // 全局互斥锁
int frame_cnt = 0;
const string &url = "http://192.168.26.215:8088";
const string &upload_url = url + "/upload";
const string &clean_url = url + "/clean";

#if _WIN32
#define MUTEX_TYPE CRITICAL_SECTION
#define MUTEX_SETUP(x) InitializeCriticalSection(&(x))
#define MUTEX_CLEANUP(x) DeleteCriticalSection(&(x))
#define MUTEX_LOCK(x) EnterCriticalSection(&(x))
#define MUTEX_UNLOCK(x) LeaveCriticalSection(&(x))
#define usleep(x) Sleep((x) / 1000)
#define sleep(x) Sleep((x) * 1000)
#else
#define MUTEX_TYPE pthread_mutex_t
#define MUTEX_SETUP(x) pthread_mutex_init(&(x), NULL)
#define MUTEX_CLEANUP(x) pthread_mutex_destroy(&(x)0)
#define MUTEX_LOCK(x) pthread_mutex_lock(&(x))
#define MUTEX_UNLOCK(x) pthread_mutex_unlock(&(x))
#endif
// #pragma once

#define MAX_LISTS 25

#if 1
#define IMAGE_W 640
#define IMAGE_H 512
#else // JX007使用此宽高
#define IMAGE_W 640
#define IMAGE_H 512
#endif
// #pragma once

struct queue_data
{
    char *data; // y8 数据
    UT_FRAME_TYPE type;
};

// 定义循环队列结构
struct circular_queue
{
    struct queue_data nodes[MAX_LISTS];
    int front;
    int rear;
    int count;
    MUTEX_TYPE lock;
};

typedef struct
{
    cv::VideoCapture *cap;
    bool *isrun;
    void *pInstance;
    struct circular_queue *scq;
} read_frame_param;

#pragma pack(1)

static void dump_to_file(const char *file_path, const void *data, int len)
{
    FILE *fp = fopen(file_path, "w");
    if (fp == NULL)
    {
        printf("open file %s error\n", file_path);
        return;
    }
    fwrite(data, len, 1, fp);
    fclose(fp);
    printf("save to file %s success\n", file_path);
}

void saveIntArrayToMatFile(const int *array, int rows, int cols, const char *filePath)
{
    // 检查输入参数
    if (array == nullptr || rows <= 0 || cols <= 0)
    {
        std::cerr << "Invalid array or size." << std::endl;
        return;
    }

    // 创建一个 CV_32S 类型的 Mat 对象（int 类型）
    cv::Mat mat(rows, cols, CV_32S, (void *)array);

    // 将 Mat 保存为图片
    cv::imwrite(filePath, mat); // 保存为图像文件
}

// 写入回调函数
size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *userp)
{
    userp->append((char *)contents, size * nmemb);
    return size * nmemb;
}

// 封装文件上传功能
bool uploadFile(const string filepath)
{
    // init curl
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
    // 设置目标URL
    curl_easy_setopt(curl, CURLOPT_URL, upload_url.c_str());

    // 设置写入回调函数
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    CURLcode res;
    std::string readBuffer; // 存储返回的数据

    if (curl)
    {

        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer); // 传递字符串以存储返回内容

        // 设置文件上传
        curl_mime *mime;
        curl_mimepart *part;

        mime = curl_mime_init(curl);

        part = curl_mime_addpart(mime);
        curl_mime_name(part, "file");               // 表单字段名
        curl_mime_filedata(part, filepath.c_str()); // 文件路径

        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

        // 发送请求
        res = curl_easy_perform(curl);

        // 检查错误
        if (res != CURLE_OK)
        {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            curl_mime_free(mime);

            return false; // 上传失败
        }

        // 输出服务器返回的数据
        // cout << "Response data: " << readBuffer << endl;

        // 解析 JSON 数据
        try
        {
            json jsonResponse = json::parse(readBuffer);
            // cout << "Result: " << jsonResponse["result"] << endl;
        }
        catch (json::parse_error &ex)
        {
            std::cerr << "parse error " << ex.what() << std::endl;
        }

        // 清理
        curl_mime_free(mime);
    }
    // clean curl
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    return true; // 上传成功
}

// 初始化循环队列
void initializeQueue(struct circular_queue *queue)
{
    queue->front = 0;
    queue->rear = 0;
    queue->count = 0;
    MUTEX_SETUP(queue->lock);
}

// 入队操作
bool enqueue(struct circular_queue *queue, char *data, UT_FRAME_TYPE type)
{
    MUTEX_LOCK(queue->lock);
    bool isenqueue = false;
    if (queue->count < MAX_LISTS)
    {
        queue->nodes[queue->rear].data = data;
        queue->nodes[queue->rear].type = type;
        queue->rear = (queue->rear + 1) % MAX_LISTS;
        queue->count++;

        isenqueue = true;
    }
    else
    {
        printf("Queue is full. Nothing to enqueue.\n\r");
    }
    MUTEX_UNLOCK(queue->lock);
    return isenqueue;
}

// 出队操作
int dequeue(struct circular_queue *queue, char **data, UT_FRAME_TYPE *type)
{

    int ret = -1;
    MUTEX_LOCK(queue->lock);
    if (queue->count > 0)
    {
        *data = queue->nodes[queue->front].data;
        *type = queue->nodes[queue->front].type;
        queue->front = (queue->front + 1) % MAX_LISTS;
        queue->count--;
        ret = 0;
    }
    else
    {
        *data = nullptr;
        // printf("Queue is empty. Nothing to dequeue.\n\r");
    }
    MUTEX_UNLOCK(queue->lock);
    return ret;
}

void saveCameraImg(cv::VideoCapture &cap, const string &path)
{
    cv::Mat frame;
    cap >> frame;
    if (frame.empty())
    {
        std::cerr << "无法获取摄像头画面" << std::endl;
        return;
    }
    cv::imwrite(path, frame);
}

// void parse_data_run(void *arg)
// {
//     read_frame_param rfp = *static_cast<read_frame_param *>(arg);
//     vector<float> rect_temp(IMAGE_W * IMAGE_H);
//     int rect_temp_size = IMAGE_W * IMAGE_H * sizeof(float);
//     vector<char> imagergb(IMAGE_W * IMAGE_H * 3);
//     int imagergb_size = IMAGE_W * IMAGE_H * 3;
//     int ncount = 0;

//     while (*rfp.isrun)
//     {
//         void *framedata = NULL;
//         UT_FRAME_TYPE type;

//         dequeue(rfp.scq, (char **)&framedata, &type);
//         if (framedata == NULL)
//         {
//             std::this_thread::sleep_for(std::chrono::milliseconds(2)); // sleep for 2 ms
//             continue;
//         }
//         ncount++;

//         // 数据如果是温度数据
//         if (type == FRAME_TYPE_TEMP)
//         {
//             UT_AnalysisTempFrame(rfp.pInstance, framedata, rect_temp.data(), rect_temp_size);
//             // rect_temp 是温度数据，用户可以自行处理了。
//             // mtx.lock();  // 手动获取锁
//             frame_cnt += 1;

//             printf("Center = %f framecount %d\n", rect_temp[IMAGE_H / 2 * IMAGE_W + IMAGE_W / 2], frame_cnt); // 打印中心温度
//             if (frame_cnt % 1 == 0)
//             {
//                 cout << "start uploading" << endl;
//             }
//             // mtx.unlock();  // 手动释放锁
//         }
//         else if (type == FRAME_TYPE_IMAGE) // 数据如果是图像数据
//         {
//             UT_AnalysisImageFrameRGB(rfp.pInstance, framedata, imagergb.data(), imagergb_size);
//             // 每30帧数据保存一张 imagergb 为rgb888数据
//             if (ncount % 30 == 0)
//             {
//                 char path[1024];
//                 sprintf(path, "./%d.raw", ncount);
//                 // dump_to_file(path,imagergb,imagergb_size);
//             }
//         }
//         // 释放一帧数据（必须操作）
//         UT_ReleaseFrame(rfp.pInstance, framedata);
//     }
// }

// void read_drame_run(void *arg)
// {
//     read_frame_param rfp = *static_cast<read_frame_param *>(arg);
//     UT_FRAME_TYPE type;
//     void *framedata = NULL;
//     while (*rfp.isrun)
//     {
//         UT_RESULT res = UT_GetFrame(rfp.pInstance, &framedata, &type); // 获取一帧数据
//         if (res == UT_OK)
//         {
//             if (enqueue(rfp.scq, (char *)framedata, type) == false)
//             {
//                 printf("error enqueue\r\n");
//                 UT_ReleaseFrame(rfp.pInstance, framedata);
//             }
//             else
//             {

//                 // std::this_thread::sleep_for(std::chrono::seconds(1));
//             }
//         }
//         else
//         {
//             printf("UT_GetFrame    ERROR %d\r\n", res);
//         }
//     }
// }

// 事件
UT_RESULT NotifyEvent(UT_REGISTRY_EVENT_TYPE eventType, void *eventParam, void *userParam)
{
    if (eventType == EVENT_TEMP_MODE)
    { // 挡位发生改变
        UT_TEMP_MODE data = *static_cast<UT_TEMP_MODE *>(eventParam);
        printf("current mode is %d \r\n", data);
    }
    else if (eventType == EVENT_LENS) // 镜头发生改变
    {
        UT_LENS_TYPE data = *static_cast<UT_LENS_TYPE *>(eventParam);
        printf("current lens is %d \r\n", data);
    }
    else if (eventType == EVENT_PROTECT_BGEIN) // 防灼烧状态开始
    {
    }
    else if (eventType == EVENT_PROTECT_END) // 防灼烧状态结束
    {
    }

    return UT_OK;
}

// 设备温度传感器的各项温度
UT_RESULT DevSensorTemp(float detectortemp, float pcbtemp, float shuttertemp, float boardTemp, void *userParam)
{
    // printf("detectortemp = %f \n,pcbtemp = %f\n,shuttertemp = %f\n,boardTemp = %f\n,userParam = %f\r\n",detectortemp,pcbtemp,shuttertemp,boardTemp,userParam);
    return UT_OK;
}

// 函数：发送 GET 请求并返回响应数据
std::string cleanServer()
{
    CURL *curl;
    CURLcode res;
    std::string response_data;

    // 初始化 libcurl 库
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if (curl)
    {
        // 设置目标 URL
        curl_easy_setopt(curl, CURLOPT_URL, clean_url.c_str());

        // 设置自定义的响应数据回调
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

        // 执行请求
        res = curl_easy_perform(curl);

        // 检查请求是否成功
        if (res != CURLE_OK)
        {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            response_data = "Request failed";
        }

        cout << response_data << endl;

        // 清理
        curl_easy_cleanup(curl);
    }

    // 全局清理
    curl_global_cleanup();

    return response_data;
}

void rgb2bgr(vector<char> &imgrgb)
{
    for (int i = 0; i < IMAGE_H; i++)
    {
        for (int j = 0; j < IMAGE_W; j++)
        {
            char r = imgrgb[(i * IMAGE_W + j) * 3 + 0];
            char g = imgrgb[(i * IMAGE_W + j) * 3 + 1];
            char b = imgrgb[(i * IMAGE_W + j) * 3 + 2];
            imgrgb[(i * IMAGE_W + j) * 3 + 0] = b;
            imgrgb[(i * IMAGE_W + j) * 3 + 1] = g;
            imgrgb[(i * IMAGE_W + j) * 3 + 2] = r;
        }
    }
}


cv::VideoCapture cap(0); // 0 表示第一个摄像头设备
int main(void)
{    
    cleanServer();
    deleteFilesInDirectory("../data");
    if (!cap.isOpened())
    {
        std::cerr << "无法打开摄像头" << std::endl;
        return -1;
    }

    bool isrun = true;
    void * pInstance;
    
    // 初始化句柄
    UT_RESULT res = UT_Init(&pInstance,UT_JX007);
    cout<<"100"<<endl;	
    if (res != UT_OK)
    {
        UT_Uninit(pInstance);

        return -1;
    }
    // bool isopendebug = true;
    // UT_CmdControl(pInstance,LIB_DEBUG_INFO,&isopendebug);// 是否打开日志输出
    UTRegistry registry;
    // 注册回调事件
    registry.NotifyEvent = NotifyEvent;
    registry.DevSensorTemp = DevSensorTemp;
    UT_Register(pInstance,&registry);  

    // struct circular_queue scq;
    // // 初始化队列
    // initializeQueue(&scq);
    // read_frame_param rfp;
    // rfp.scq = &scq;
    // rfp.isrun = &isrun;
    // rfp.pInstance = pInstance;
    bool isstartimg = true;

    void *framedata = NULL;
    UT_FRAME_TYPE type;
    



    vector<float> rect_temp(IMAGE_W * IMAGE_H);
    int rect_temp_size = IMAGE_W * IMAGE_H * sizeof(float);
    vector<char> imagergb(IMAGE_W * IMAGE_H * 3);
    int imagergb_size = IMAGE_W * IMAGE_H * 3;
    vector<char> imagebgr(IMAGE_W * IMAGE_H * 3);
    
    int count = 0;

    while (1)
    {
        count ++;
        if (count == 5){
            cout<<"100"<<endl;	
            UT_CmdControl(pInstance,DEV_OUT_IMAGE,&isstartimg);  // 开始输出图像 延时一会，因为连接机芯有个时间差
            cout<<"100"<<endl;
            UT_PALETTE_TYPE pt = UT_PALETTE_IRON;
            UT_CmdControl(pInstance, IMAGE_SET_PALETTE, &pt);
            break;
        }

        sleep(1);
    }
    cout<<"100"<<endl;

    while (1)
    {
        count++;
        // if(count==2){
        //     isstartimg=true;
        //     UT_CmdControl(pInstance, DEV_OUT_IMAGE, &isstartimg); // 开始接收数据
        // }

        res = UT_GetFrame(pInstance, &framedata, &type); // 获取一帧数据
        // cout<<"1"<<endl;
        // 数据如果是温度数据
        // cout<<type<<endl;
        cout<<res<<endl;
        if (res == UT_OK)
        {
            if (type == FRAME_TYPE_Y16)
            {
                UT_AnalysisTempFrame(pInstance, framedata, rect_temp.data(), rect_temp_size);
                // rect_temp 是温度数据，用户可以自行处理了。
                frame_cnt += 1;

                printf("Center = %f framecount %d\n", rect_temp[IMAGE_H / 2 * IMAGE_W + IMAGE_W / 2], frame_cnt); // 打印中心温度
                // mtx.unlock();  // 手动释放锁
                // 释放一帧数据（必须操作）
                UT_ReleaseFrame(pInstance, framedata);
            }
            else if (type == FRAME_TYPE_Y8) // 数据如果是图像数据
            {
                UT_AnalysisImageFrameRGB(pInstance, framedata, imagergb.data(), imagergb_size);
                // 每30帧数据保存一张 imagergb 为rgb888数据
                if (count % 10 == 0)
                {
                    cout<<"count="<<count<<endl;
                    char camerapath[1024];
                    if (count > 30){
                        sprintf(camerapath, "../data/%d_camera.jpg", count);
                        saveCameraImg(cap, camerapath);
                        cout<<"gua"<<endl;
                    }

                    char path[1024];
                    sprintf(path, "../data/%d_temp.jpg", count);
                    rgb2bgr(imagergb);
                    cv::Mat img(IMAGE_H, IMAGE_W, CV_8UC3, (void *)imagergb.data());
                    cv::flip(img, img, -1);
                    // 将图像从 RGB 转换为 BGR
                    // 保存图像
                    cv::imwrite(path, img);
                    if (count > 30){
                        //   上传
                         std::thread t1(uploadFile, path);

                        //   上传
                         std::thread t2(uploadFile, camerapath);

                         t1.detach();
                         t2.detach();
                    }

                    

                    // uploadFile(path, camerapath);

                    // std::thread t(uploadFile, path, camerapath);
                    // t.detach();
                }
                // 释放一帧数据（必须操作）
                UT_ReleaseFrame(pInstance, framedata);
            }
        }

        // sleep(1);
    } 

    // 退出程序 释放SDK
    isstartimg = false;
    UT_CmdControl(pInstance,DEV_OUT_IMAGE,&isstartimg);    // 停止接收数据
    isrun = false;    

    UT_Uninit(pInstance);
    return 0;
}   