#ifndef UT_THERMALLIB_H
#define UT_THERMALLIB_H
#include "UTDF.h"
#ifdef __cplusplus
extern "C"
{
#endif
	/**
	* @brief:	初始化UT库。
	* @param:	void* pInstance			初始化句柄。
	* @return:	返回值类型：UT_RESULT。成功，返回UT_OK；失败参考ERROR.h。
	**/
	CROSS_PLATFORM_API UT_RESULT UT_Init(void** pInstance, UT_DEV_TYPE type);

	/**
	* @brief:	反初始化UT库。
	* @param:	void* pInstance			初始化句柄。
	* @return:	返回值类型：UT_RESULT。成功，返回UT_OK；失败参考ERROR.h。
	**/
	CROSS_PLATFORM_API UT_RESULT UT_Uninit(void* pInstance);

	/**
	* @brief:	注册机制。
	* @param:	void* pInstance			初始化句柄。
	* @param:	UTRegistry *registry	用户注册函数。
	* @return:	返回值类型：UT_RESULT。成功，返回UT_OK；失败参考ERROR.h。
	**/
	CROSS_PLATFORM_API UT_RESULT UT_Register(void* pInstance, UTRegistry* registry);
	
	/**
	* @brief:	设备控制。
	* @param:	void* pInstance			初始化句柄。
	* @param:	UT_CMD_CONTROL type		控制指令。
	* @param:	void* param				控制参数。详见参考UT_CMD_CONTROL
	* @return:	返回值类型：UT_RESULT。成功，返回UT_OK；失败参考ERROR.h。
	**/
	CROSS_PLATFORM_API UT_RESULT UT_CmdControl(void* pInstance, UT_CMD_CONTROL type, void* param);
	
	/**
	* @brief:	获取一帧数据
	* @param:	void* pInstance			初始化句柄。
	* @param:	void* framedata			获取帧数据，用户传入一个指针，不需要分配内存。	
	* @return:	返回值类型：UT_RESULT。成功，返回UT_OK；失败参考ERROR.h。
	**/
	CROSS_PLATFORM_API UT_RESULT UT_GetFrame(void* pInstance,void** framedata,UT_FRAME_TYPE* frametype);

	/**
	* @brief:	解析一帧图像数据，输出RGB图像
	* @param:	void* pInstance			初始化句柄。
	* @param:	void* framedata			UT_GetFrame 中获取到的帧数据。	
	* @param:	char* outimage			RGB数据，需要用户分配空间输出数据为RGB888格式 分配数据一般是宽*高*3
	* @param:	int imagesize			outimage实际大小
	* @return:	返回值类型：UT_RESULT。成功，返回UT_OK；失败参考ERROR.h。
	**/
	CROSS_PLATFORM_API UT_RESULT UT_AnalysisImageFrameRGB(void* pInstance,void* framedata,char* outimage, int imagesize);
	
	/**
	* @brief:	解析一帧图像数据，输出Y8图像
	* @param:	void* pInstance			初始化句柄。
	* @param:	void* framedata			UT_GetFrame 中获取到的帧数据。	
	* @param:	unsigned char* y8		Y8数据，需要用户分配空间输出数据为y8格式 分配数据一般是宽*高
	* @param:	int imagesize			y8实际大小
	* @return:	返回值类型：UT_RESULT。成功，返回UT_OK；失败参考ERROR.h。
	**/
	CROSS_PLATFORM_API UT_RESULT UT_AnalysisImageFramey8(void* pInstance, void* framedata,unsigned char* y8, int y8size);

	/**
	* @brief:	解析一帧图像数据，输出Y16图像
	* @param:	void* pInstance			初始化句柄。
	* @param:	void* framedata			UT_GetFrame 中获取到的帧数据。	
	* @param:	unsigned char* y6		Y16数据，需要用户分配空间输出数据为y16格式 分配数据一般是宽*高*2+8
	* 多出来的8个字节分别是 short型2个字节的探测器温度，short型2个字节的探测器板pcb温度，short型2个字节的快门温度，short型2个字节的主板温度8个字节在y16数据的前8位，计算计算公式是short/100
	* 例如： 探测器温度 = 探测器short / 100  温度为℃
	* @param:	int imagesize			y16实际大小
	* @return:	返回值类型：UT_RESULT。成功，返回UT_OK；失败参考ERROR.h。
	**/
	CROSS_PLATFORM_API UT_RESULT UT_AnalysisImageFramey16(void* pInstance, void* framedata,unsigned char* y16, int y16size);
	
	/**
	* @brief:	解析一帧温度数据
	* @param:	void* pInstance			初始化句柄。
	* @param:	void* framedata			UT_GetFrame 中获取到的帧数据。	
	* @param:	float* outtemp			温度数据，需要用户分配空间 分配数据一般是宽*高*4
	* @param:	int tempsize			outtemp实际大小
	* @return:	返回值类型：UT_RESULT。成功，返回UT_OK；失败参考ERROR.h。
	**/
	CROSS_PLATFORM_API UT_RESULT UT_AnalysisTempFrame(void* pInstance,void* framedata,float* outtemp, int tempsize);

	/**
	* @brief:	获取温度配置
	* @param:	void* pInstance			初始化句柄。
	* @param:	char* Config			数据，需要用户创建内存，返回的数据根据当前挡位来的，当前挡位为低温档，返回低温数据，高温档返回高温数据
	* @param:	int configlen			Config的内存大小
	* @return:	返回值 Config的数据长度
	**/
	CROSS_PLATFORM_API int UT_ReadCurrentModeTempConfig(void* pInstance,char* Config,int configlen);
	
	/**
	* @brief:	获取镜头参数配置
	* @param:	void* pInstance			初始化句柄。
	* @param:	char* pdata				数据，需要用户创建内存，返回当前挡位和当前镜头对应的数据
	* @param:	int pdatalen			pdata的内存大小
	* @return:	返回值 pdata的数据长度
	**/
	CROSS_PLATFORM_API int UT_GetShotData(void* pInstance,char* pdata,int pdatalen);
	/**
	* @brief:	释放一帧数据
	* @param:	void* pInstance			初始化句柄。
	* @param:	void* framedata			释放的帧数据。	
	* @return:	返回值类型：UT_RESULT。成功，返回UT_OK；失败参考ERROR.h。
	**/
	CROSS_PLATFORM_API UT_RESULT UT_ReleaseFrame(void* pInstance,void* framedata);

	
#ifdef __cplusplus
}
#endif

#endif
