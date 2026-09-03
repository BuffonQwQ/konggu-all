#ifndef UT_ERROR_H
#define UT_ERROR_H

typedef enum Result
{
	UT_ERROR = -100,				// 错误
	UT_TEMP_CONFIG_ERROR = -9,	// 温度配置文件错误
	UT_TEMP_INIT_ERROR = -8,	//	温度算法库初始化失败
	UT_IMAGE_INIT_ERROR = -7,	//	图像算法库初始化失败
	UT_GET_FRAME_ERROR = -6,			// 获取图像帧出错
	UT_DEV_NOT_INIT = -5,		// 设备未初始化
	UT_CMD_ERROR = -4,			// 参数错误
	UT_UNKNOWN_CMD = -3,		// 未知指令
	UT_UNKNOWN_DEVICE_TYPE = -2, // 未知设备类型
	UT_INIT_FACTORY_ERROR = -1, //初始化工厂错误
	UT_OK = 0,

}UT_RESULT;
#endif