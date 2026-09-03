#ifndef UT_DTF_H
#define UT_DTF_H
#include "UTERROR.h"

#if ANDROID_COMPILATION_SWITCH
#define CROSS_PLATFORM_API
#elif _WIN32
#define CROSS_PLATFORM_API __declspec(dllexport)
#else
#define CROSS_PLATFORM_API __attribute((visibility("default")))
#endif

// 镜头类型
typedef enum UTLensType {
	NONE_LENS = 0,	// 无镜头
	F1p0_f6mm,		// 最大光圈直径和焦距
	F1p0_f9p6mm,	// 9.6mm
	F1p0_f18p3mm,	// 18.3mm
	F1p0_f36p7mm,	// 36.7mm
	F1p3_f62p8mm,	// 62.8mm
	F1p3_f17p4mm,	// 17.4mm
	MAX_LENS_TYPE
}UT_LENS_TYPE;

// 设备类型
typedef enum UTDevType
{
	UT_JX002 = 0,
	UT_JX003,
	UT_JX004,
	UT_JX007,
}UT_DEV_TYPE;


// 控制类型
typedef enum UTCmdControl
{
	DEV_ENABLE, 					// 设备使能，参数param为bool类型 true为开启设备使能，false为关闭设备使能
	DEV_OUT_IMAGE, 					// 设备开始输出图像数据 参数param为bool true 为开始输出图像，false为停止输出图像	
	DEV_SHTTER_ONE,					// 打一次快门 参数param为NULL
	DEV_SHTTER_AUTO,				// 自动快门  参数param类型为 int  范围10-60  0为关闭自动快门
	DEV_PROTECT_EN,					// 高温防灼烧使能 参数param类型为bool类型 true为开启使能，false为关闭使能	
	DEV_SET_MODE,					// 设置为温度挡位，参数param类型为UT_TEMP_MODE
	DEV_SET_RESPONSIVITY,			// 设置响应率，参数param类型为类型UT_RESPONSIVITY
	DEV_GET_JX_CODE,				// 获取机芯码（二维码，机芯唯一标识）参数param类型为UT_DATA_FEEDBACK
	DEV_WRITE_FILE,					// 写文件到机芯内 参数param类型为类型UT_WRITE_FILE_TO_DEV
	DEV_GET_DEV_VERSION,			// 获取机芯版本信息 参数param类型为UT_DATA_FEEDBACK
	TEMP_SET_EMISS = 100, 			// 设置发射率参数类型为float（ 0.01-1.00，默认0.95）
	TEMP_SET_DISTANCE, 				// 测温距离 参数param类型为int
	TEMP_SET_REFLECT,				// 反射温度	参数param类型为float
	TEMP_SET_AMBIENT,				// 环境温度	参数param类型为float
	TEMP_SET_ATMOSPHERE,			// 大气温度 参数param类型为float
	TEMP_SET_HUMIDITY,				// 相对湿度 参数param类型为float(0-100)
	IMAGE_SET_PALETTE,				// 设置色板 参数param类型为UT_PALETTE_TYPE
	IMAGE_SET_CUSTOM_PALETTE,		// 自定义色板值 参数param类型为unsigned char palette[768] 排列顺序为RGB888
	IMAGE_SET_ISOTHERM,				// 设置等温模式 参数param类型为IsothermParam
	IMAGE_SET_BRIGHTNESS,			// 设置亮度 参数param类型为int 类型 （0-100）
	IMAGE_SET_CONTRAST,				// 设置对比度 参数param类型为int 类型 （0-100）
	IMAGE_SET_Y16_RATE,				// 设置y16帧率 参数param类型为int 类型 Y16帧率的公约数Y16帧率为30帧可以设置的值：1、2、3、5、6、10、15、30 (UT_JX003)Y16帧率为25帧可以设置值：1、5、8、12、25
	IMAGE_SET_Y8_RATE,				// 设置y8帧率 参数param类型为int 类型 y8帧率的公约数y8帧率为30帧可以设置的值：1、2、3、5、6、10、15、30 (UT_JX003)y8帧率为25帧可以设置值：1、5、8、12、25
	IMAGE_GET_Y16_RATE,				// 获取y16帧率
	IMAGE_GET_Y8_RATE,				// 获取y8帧率
	IMAGE_SET_MIRROR,				// 设置图像镜像 参数param类型为UT_MIRROR_TYPE
	IMAGE_GET_MIRROR,				// 获取图像镜像 参数param类型为UT_MIRROR_TYPE
	IMG_SET_FORMAT,					// 设置数据输出格式 参数param类型为UT_OUT_FORMAT
	IMG_GET_FORMAT,					// 设置数据输出格式 参数param类型为UT_OUT_FORMAT	
	LIB_DEBUG_INFO = 200,			// 设置是否输出库DEBUG信息，参数param类型为bool,true为开启，false为关闭
}UT_CMD_CONTROL;

typedef enum UTOutFormat
{
	YUV402 = 0,		//（暂不可用）
	Y8,
	Y16,
	RAW_Y16,		//（暂不可用）
	YUV422,			//（暂不可用）
	Y8_AND_Y16
}UT_OUT_FORMAT;

// 镜像类型
typedef enum UTMirrorType
{
	NONE = 0, // 无不镜像也不翻转
	HORIZONTAL, //水平镜像 X轴方向
	VERTICALLY, // 垂直镜像 Y轴镜像
	HORIZONTAL_AND_VERTICALLY,//水平+垂直镜像
}UT_MIRROR_TYPE;

// 帧数据类型
typedef enum UTFrameType
{
	FRAME_TYPE_Y16, 	// Y16
	FRAME_TYPE_Y8, 		// Y8
}UT_FRAME_TYPE;

//测温模式
typedef enum UTTempMode {
	UT_TEMP_MODE_L, // 低温模式
	UT_TEMP_MODE_H, // 高温模式
}UT_TEMP_MODE;

// 回调事件类型
typedef enum UTRegistryEventType
{
	EVENT_TEMP_MODE,	//	挡位变化	参数eventparam为UT_TEMP_MODE类型
	EVENT_LENS,			// 	镜头变化	参数eventparam为UT_LENS_TYPE类型
	EVENT_PROTECT_BGEIN,//  开始防灼烧	参数eventparam为NULL
	EVENT_PROTECT_END,	//	完成防灼烧	参数eventparam为NULL
}UT_REGISTRY_EVENT_TYPE;

// 等温模式
typedef enum UTIsothermMode {
	ISOTHERM_AUTO,		//	自动等温
	ISOTHERM_DOWN,		// 	向下等温
	ISOTHERM_UP,		// 	向上等温
	ISOTHERM_SECTION,	//	区间等温
	ISOTHERM_MANUAL,	//	手动等温
	ISOTHERM_PROPOTIONAL,// 比例等温
}UT_ISOTHERM_MODE;

// 色板类型
typedef enum UTPaletteType
{
	UT_PALETTE_CUSTOM = -1,	// 自定义色板 
	UT_PALETTE_IRON,		// 铁红
	UT_PALETTE_WHITEHOT,	// 白热
	UT_PALETTE_REDHOT,		// 红热
	UT_PALETTE_LAVA,		// 熔岩
	UT_PALETTE_RAINBOWHC,	// 高彩虹
	UT_PALETTE_RAINBOW,		// 彩虹
	UT_PALETTE_BLACKHOT,	// 黑热
}UT_PALETTE_TYPE;

// 等温参数
typedef struct IsothermParam
{
	UT_ISOTHERM_MODE uim;	// 等温模式
	float tempTableMax;	// 整个温度矩阵的最大值(可以不需要填入)
	float tempTableMin;		// 整个温度矩阵的最小值(可以不需要填入)
	float upTemp;			// 上等温值 向上等温/自动等温设置这个值为u小
	float downTemp;		// 下等温值 向下等温/自动等温设置这个值为u小
}ISOTHERM_PARAM;

// 设置响应率
typedef struct UTresponsivity
{
	int nint;
	int ngfid;
	int ngain;
}UT_RESPONSIVITY;

typedef struct UTDataFeedback
{
	void* data; 		// 注意data的内存需要用户创建
	int data_len;		// data的内存长度
	int reality_data;  	// 实际写入data的数据长度。由接口函数返回的实际写入data的长度
	/* data */
}UT_DATA_FEEDBACK;


typedef struct UTWriteFileToDev	// 写文件到机芯
{
	char* filepath;		// 文件路径
	char* devfilepath;	// 写到机芯的地址 
}UT_WRITE_FILE_TO_DEV;

// 注册函数回调
typedef struct UT_REGISTRY
{
	/**
	* @brief:	设备温度。
	* @param:	float detectortemp			探测器温度。
	* @param:	float pcbtemp				探测器板pcb温度
	* @param:	float shuttertemp			快门温度
	* @param:	float boardTemp				主板温度
	* @param:	void *userParam				用户参数，注册时由用户传入
	* @return:	返回值类型：UT_RESULT。成功，返回UT_OK；失败参考ERROR.h。
	**/
	UT_RESULT(*DevSensorTemp)(float detectortemp, float pcbtemp, float shuttertemp, float boardTemp, void* userParam);
	/**
	* @brief:	事件通知
	* @param:	UT_REGISTRY_EVENT_TYPE eventType	事件类型
	* @param:	void *eventParam					事件参数，参考UT_REGISTRY_EVENT_TYPE注释
	* @param:	void *userParam						用户参数，注册时由用户传入
	* @return:	返回值类型：UT_RESULT。成功，返回UT_OK；失败参考ERROR.h。
	**/
	UT_RESULT(*NotifyEvent)(UT_REGISTRY_EVENT_TYPE eventType, void* eventParam, void* userParam);

	/*用户参数，调用注册函数时再传给用户。*/
	void* userParam;
}UTRegistry;



#endif 