#ifndef _GET_DATA_H_
#define _GET_DATA_H_

#ifndef CAMERA_DLL_export
#define CAMERA_DLL extern"C" _declspec(dllimport)
#else
#define CAMERA_DLL extern"C" _declspec(dllexport)
#endif
#define     USB_VID			0x0483	//和usb有关
#define     USB_PID			0x1850	  
#include "CameraApi.h"	
#include "USBHid.h"
#include "Types.h"
#include <stdlib.h>
#include "mrzAPI.h"
#include "ConfigTools.h"
#include "CardType.h"
#include "USBListener.h"
//==================以下为要导出的接口定义====================

CAMERA_DLL void saveErroImg(int flag);//0：不保存，1：保存，默认不保存
CAMERA_DLL void delLog(int days);//删除日志和错误图片
CAMERA_DLL int OpenCamera();//初始化相机 （0--设备连接正常，其他代表失败） 
CAMERA_DLL int CameraIsConnected();//返回相机连接情况 （0--设备连接正常，其他代表失败）
CAMERA_DLL int DocIsExist();//检查是否有证件放入 (0-有 1-无）（会立刻返回结果，不会等待）
CAMERA_DLL int DocIsExistAtCorner();//检查是否有证件放在顶角 (0-有 1-无）（会立刻返回结果，不会等待）
CAMERA_DLL int DocStable();//检查证件状态是否稳定
CAMERA_DLL int DocIsExistAndStable();//检查是否有证件放入并确保放稳后返回 (0-有 1-无）
CAMERA_DLL int DocIsExistAtCornerAndStable();//检查是否有证件放入并确保放稳后返回 (0-有 1-无）
CAMERA_DLL int GetData(int timeout = 0);//进行采图和读取芯片信息（图像+芯片信息,存放在USB_TEMP目录下）（0-成功，其他代表失败） 
CAMERA_DLL int TakePhoto();//进行采图和读取芯片信息（图像+芯片信息,存放在USB_TEMP目录下）（0-成功，其他代表失败） 
CAMERA_DLL int GetDataOCR(int timeout = 0);//港澳往来内地通行证和台湾往来内地通行证无芯片也进行OCR解析
CAMERA_DLL int CloseCamera();//关闭相机，释放占用资源（退出程序前必须调用，否则会因有资源没释放而报错） （0--设备连接正常，其他代表失败）
CAMERA_DLL int GenerateRefer();//生成校正参数（USB_Refer文件夹下）（0-成功，其他代表失败） 
//CAMERA_DLL void setDataPath(const char* path);//设置数据文件存放的位置，为空表示当前目录，需在OpenCamera函数之前调用
//以下函数需要在GetData()（获取数据）结束后调用，否则只会返回默认值
CAMERA_DLL int GetType();//返回证件类型（-1--未知；0--护照；1--身份证；2--港澳通行证）
CAMERA_DLL void GetSource(char *type);//返回证件信息来源（00：正面  01：反面 02：芯片）
CAMERA_DLL void GetJPEGPhoto(char *buffer, int *width, int* height, int* datalength);//
CAMERA_DLL int GetOcrResult();//返回机读码识别结果（1-成功，其他代表失败），暂时不需要
CAMERA_DLL int GetChipResult();//返回芯片信息读取结果（ 1-成功，其他代表失败）
CAMERA_DLL const char* GetAuthenticity();//返回证件真伪结果
CAMERA_DLL const char* GetFace();//成功：返回证件头像路径（绝对路径），失败/无此图像：返回NULL
CAMERA_DLL const char* GetJson();//返回json格式的字符串，包含了读取出来的证件信息，例如姓名等等，具体定义见SDK调用说明
CAMERA_DLL int getJsonLength();
CAMERA_DLL Mat getImageData(int type);//读取图片，0：红光；1：白光；2：紫光；3：人脸
CAMERA_DLL void getSDKVersion(char *);//获取版本字符串
CAMERA_DLL void getDeviceInfo(char *);//获取版本字符串
CAMERA_DLL cv::Mat getImageData(int type);//Read the pictures (0-ir image; 1-white image; 2-uv image; 3-face image)
CAMERA_DLL void greenTwinkle();//绿灯闪烁
CAMERA_DLL void blueTwinkle();//蓝灯闪烁
CAMERA_DLL int isConnect();//返回相机连接情况
CAMERA_DLL double getPassTime();//返回获取护照信息用时
CAMERA_DLL double getIDTime();//返回获取身份证信息用时
CAMERA_DLL char* getSoftwareVersion();//软件版本号
CAMERA_DLL char* getFirmwareVersion();//固件版本号
CAMERA_DLL char* getBoardVersion();//底板版本号
CAMERA_DLL int FactoryTest();//0630测试新的摄像头
CAMERA_DLL void testfornew();//新摄像头设备的测试函数
//==================以下为程序内部要用到的函数定义====================

int InitCamera();//初始化相机
int getConfigPath();//获取dll路径和参数路径
int InitLight(CUSBHid &hidEnum);//初始化光源
int inputDetection();//输入检测
bool isTwentiethCentury(string year);//工具：判断year(只有年份的后两位)的前两位是20还是19
string getStrFromTxt(string path);//工具，从指定路径获取text文件内的内容
CARD_TYPE distinguishSmallCardLine(CARD_TYPE& docType, Mat& ir);//检测是否是港澳通行证（返回：2-港澳通行证；3-（回乡证）；-2-未知）
int getMat(cv::Mat& img, int confNum);//获取相机图片（获取Mat）(没有旋转)
int getMat(cv::Mat& img);//获取相机图片（获取Mat）(已旋转)
Mat saveFinalImage(const Mat &trans, Mat& src, Size outputsize, int n, CARD_TYPE docType);//保存最终图片,n:保存图像的种类，docType：证件种类
void saveOriImg(Mat& src, int n);
int saveErrorImage(Mat irSrc, Mat irCut);//保存Ocr失败图片
int isConnect();//检测相机连接是否正常
//const char* GetFaceImage();//成功：返回证件在可见光下头像路径（绝对路径），失败/无此图像：返回NULL
string GetChipImage();//成功：返回证件芯片内头像路径（绝对路径），失败/无此图像：返回NULL

int getChipType();//获取证件芯片种类:0--身份证；1--护照或其他带电子芯片港澳台证件；-1--无芯片；-2--芯片读取设备没打开
int getOcr(Mat);//进行ocr识别，输入红外图像的路径USB_TEMP\\2_ir.bmp和证件类型，调用后会在dll根目录生成mrz.txt，保存着识别后的机读码
UINT WINAPI ProcThread(LPVOID lpParam);//读取芯片信息（护照和各种通行证等）
UINT WINAPI IDcardThread(LPVOID lpParam);//读取身份证芯片信息
UINT WINAPI DelLogThread(LPVOID lpParam);
void waitForIdThread();//等待身份证读取线程结束
void waitForChipThread();//等待芯片读取线程结束
void existAndDelete();
bool RemoveDir(const char* szFileDir);
void DeviceChange(int vid, int pid, DeviceChangeType changeType);
int getFirmVer();
//判断区域内是否有条形码，若有返回1，没有返回0
#endif