// CameraImageDLL.cpp : 定义 DLL 应用程序的导出函数。
//

#include "stdafx.h"
#define CAMERA_DLL_export
#include "ImageProcess.h"
#include "getData.h"
#include "mrz.h"
#include <process.h>
#include <strstream>
#include <algorithm>
#include "getJson.h"
#include "zbar.h"
#include "test1.h"
#include <CropCard.h>

using namespace google;
using namespace cv;
using namespace std;
using namespace zbar;
CameraHandle m_hCamera = -1;//与相机SDK有关的句柄等
tSdkCameraCapbility m_cap;
extern int CameraIndex;
extern Mat imgfornew;
CUSBHid hidEnum;//控制光源
class FuncLog
{
public:
	FuncLog(string, function<void()> func = 0);
	~FuncLog();

private:
	string _funcname;
	function<void()> _func;
};

FuncLog::FuncLog(string funcname, function<void()> func)
{
	_funcname = funcname;
	_func = func;
	LOG(INFO) << _funcname << "() run";
}

FuncLog::~FuncLog()
{
	if (_func)
		_func();
	LOG(INFO) << _funcname << "() end";
}
//=================以下为动态调用其他人的dll时定义的函数指针=====================

typedef int(*lpChip)(const char*); //读取护照和通行证等芯片（需要输入机读码）-胡定坤提供
typedef int(*lpIDcard)(void);//读取身份证芯片
typedef int(*lpChipType)(void);//获取芯片类型
typedef void(*lpGlogOpen)(const char*);//初始化/关闭日志
typedef void(*lpGlog)(void);//初始化/关闭日志
typedef int(*lpGetFirmVersion)(char*, int*);//获取固件版本号
typedef void(*lpFace)(char*);//截取人脸（提供图像路径，截取的人脸保存在scanner_face目录下）-鲁剑箫提供
typedef long long(*lpIrUvRes)(const char*, const char*, const char*, const char*);//获取鉴伪结果 -王小川提供

//============以下为一些表示状态和结果的全局变量，以后可以用类成员变量等代替==============

BOOL isOpenCamera = FALSE; //是否打开相机
BOOL isOpenLog = FALSE; //是否打开Log
BOOL isRegisterUsb = FALSE; //是否注册USB监听
BOOL isloadConfig = FALSE; //是否已加载配置
BOOL OcrDll = FALSE;//ocr dll调用结果，true为调用成功
BOOL OcrInit = FALSE;//ocr dll调用结果，true为调用成功
int ocrFlag = 0;//ocr识别结果---1代表成功，其他代表失败
int chipFlag = 0;//芯片读取结果---1代表成功，其他代表失败
int IDcardFlag = 0;//芯片读取结果---1代表成功，其他代表失败
int zbarFlag = 0;//二维码识别结果---1代表有二维码，其他代表失败
CARD_TYPE insertType = UNKNOWN;//证件类型
int getImgFlag = -1;//采图（采集数据）结果---0代表成功，其他代表失败
Mat whiteImage;
string mrzCode;
MRZ OCRworker;
double passTime, idTime;


//============以下为两个读取芯片线程的句柄及ID==============

UINT            m_threadID;//Ocr+芯片读取线程ID
HANDLE          m_ProcThread;//OCR+芯片读取线程句柄
UINT            m_IDcardID;//身份证芯片读取线程ID
HANDLE          m_IDcardThread;//身份证芯片读取线程句柄
HINSTANCE hDllChip; //DLL句柄 


//#define _HAS_STD_BYTE = 0

void getSDKVersion(char* version)
{
	ConfigTools::getSDKVersion(version);
	return;
}
double getPassTime()
{
	return passTime;
}
double getIDTime()
{
	return idTime;
}

void getDeviceInfo(char* info)
{
	sprintf(info, "BIAC000001,NJHKHD100,NJ-ZW");
	return;
}

//读取图片，0: 红光；1: 白光；2: 紫光；3: 人脸
Mat getImageData(int type) {
	switch (type)
	{
	case 0: {
		return imread(ConfigTools::irPath);
		break;
	}
	case 1: {
		return imread(ConfigTools::whitePath);
		break;
	}
	case 2: {
		return imread(ConfigTools::uvPath);
		break;
	}
	case 3: {
		return imread(ConfigTools::facePath);
		break;
	}
	default: return Mat();
		break;
	}
}
//第一步: 获取文件最后修改时间
bool GetFileModifyDate(WIN32_FIND_DATA& wfd, SYSTEMTIME& modDate)
{
	SYSTEMTIME   systime;
	FILETIME   localtime;

	//转换时间  
	FileTimeToLocalFileTime(&wfd.ftLastWriteTime, &localtime);
	FileTimeToSystemTime(&localtime, &systime);

	modDate = systime;
	return true;
}

//第二步: 转换时间秒数
time_t TimeConvertToSec(int year, int month, int day)
{
	tm info = { 0 };
	info.tm_year = year - 1900;
	info.tm_mon = month - 1;
	info.tm_mday = day;
	return mktime(&info);
}

//第三步: 计算两个日期相差天数
int DaysOffset(int fYear, int fMonth, int fDay, int tYear, int tMonth, int tDay)
{
	int fromSecond = (int)TimeConvertToSec(fYear, fMonth, fDay);
	int toSecond = (int)TimeConvertToSec(tYear, tMonth, tDay);
	return (toSecond - fromSecond) / 24 / 3600;
}

//清理指定文件夹下所有文件 包括文件目录本身
//[in] const wstring wstDirectory : 要清理的文件目录
//返回值 : 无
void RemoveAll(string st, int days)
{
	if (days < 0)
		days = 30;
	SYSTEMTIME curDate;
	GetLocalTime(&curDate); //获取当前时间

	string stCurrentFindPath;
	stCurrentFindPath.assign(st);
	stCurrentFindPath.append("\\*.*");

	string stCurrentFile;
	WIN32_FIND_DATA wfd;
	HANDLE h = FindFirstFile(stCurrentFindPath.c_str(), &wfd);
	if (h == INVALID_HANDLE_VALUE)
	{
		return;
	}
	do
	{
		if (lstrcmp(wfd.cFileName, ".") == 0 ||
			lstrcmp(wfd.cFileName, "..") == 0)
		{
			continue;
		}

		//如果在规定天数以内，跳过
		SYSTEMTIME fDate;
		if (GetFileModifyDate(wfd, fDate))
		{
			int dayOffset = DaysOffset(fDate.wYear, fDate.wMonth, fDate.wDay,
				curDate.wYear, curDate.wMonth, curDate.wDay);
			if (dayOffset < days)
				continue;
		}

		stCurrentFile.assign(st);
		stCurrentFile.append("\\");
		stCurrentFile.append(wfd.cFileName);
		if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			RemoveAll(stCurrentFile, days);
		}
		else
		{
			DeleteFile(stCurrentFile.c_str());
		}
	} while (FindNextFile(h, &wfd));
	FindClose(h);
	//RemoveDirectoryW(wst.c_str());
}
//void RemoveAll(wstring wst, int days)
//{
//	if (days < 0)
//		days = 30;
//	SYSTEMTIME curDate;
//	GetLocalTime(&curDate); //获取当前时间
//
//	wstring wstCurrentFindPath;
//	wstCurrentFindPath.assign(wst);
//	wstCurrentFindPath.append(L"\\*.*");
//
//	wstring wstCurrentFile;
//	WIN32_FIND_DATAW wfd;
//	HANDLE h = FindFirstFileW(wstCurrentFindPath.c_str(), &wfd);
//	if (h == INVALID_HANDLE_VALUE)
//	{
//		return;
//	}
//	do
//	{
//		if (lstrcmpW(wfd.cFileName, L".") == 0 ||
//			lstrcmpW(wfd.cFileName, L"..") == 0)
//		{
//			continue;
//		}
//
//		//如果在规定天数以内，跳过
//		SYSTEMTIME fDate;
//		if (GetFileModifyDate(wfd, fDate))
//		{
//			int dayOffset = DaysOffset(fDate.wYear, fDate.wMonth, fDate.wDay,
//				curDate.wYear, curDate.wMonth, curDate.wDay);
//			if (dayOffset < days)
//				continue;
//		}
//
//		wstCurrentFile.assign(wst);
//		wstCurrentFile.append(L"\\");
//		wstCurrentFile.append(wfd.cFileName);
//		if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
//		{
//			RemoveAll(wstCurrentFile, days);
//		}
//		else
//		{
//			DeleteFileW(wstCurrentFile.c_str());
//		}
//	} while (FindNextFileW(h, &wfd));
//	FindClose(h);
//	//RemoveDirectoryW(wst.c_str());
//}

void saveErroImg(int flag) {
	if (flag == 1) {
		ConfigTools::saveErrImg = true;
	}
	else {
		ConfigTools::saveErrImg = false;
	}
}

void delLog(int days) {
	//获取三个路径
	//图片路径
	//char curDllPath[400] = { 0 };
	//GetModuleFileName(GetSelfModuleHandle(), curDllPath, MAX_PATH);
	//(_tcsrchr(curDllPath, _T('\\')))[1] = 0; // 删除文件名，获取当前dll路径
	//char*转wstring
	//wchar_t* wchar;
	//int len = MultiByteToWideChar(CP_ACP, 0, curDllPath, strlen(curDllPath), NULL, 0);
	//wchar = new wchar_t[len + 1];
	//MultiByteToWideChar(CP_ACP, 0, curDllPath, strlen(curDllPath), wchar, len);
	//wchar[len] = '\0';
	//wstring w_str = wchar;
	//delete[]wchar;

	string errOcrImg = ConfigTools::logPath + "errOcrImg\\";
	string getDataLog = ConfigTools::logPath + "getDataLog\\";
	string EchipLog = ConfigTools::logPath + "EchipLog\\";

	//删除图片
	RemoveAll(errOcrImg, days);
	RemoveAll(getDataLog, days);
	RemoveAll(EchipLog, days);
}

//初始化相机，设备，日志等等（0--设备连接正常，其他代表失败）
int OpenCamera()
{
	if (isOpenCamera)
	{
		return 0;
	}

	if (!isloadConfig) {
		if (ConfigTools::loadConfig() == -1)
		{
			return -1;
		}
		isloadConfig = TRUE;
	}

	if (!isOpenLog) {
		SetDllDirectory(ConfigTools::curPath.c_str());
		isOpenLog = true;
#ifdef _WIN64
		hDllChip = LoadLibrary("cis_passportreader_x64.dll");
#else
		hDllChip = LoadLibrary("cis_passportreader.dll");
#endif
		if (hDllChip == NULL) {
			int ercode = GetLastError();
			LOG(ERROR) << "OpenCamera()-loadLibrary cis_passportreader.dll failed!" << ercode;
		}
		else {
			lpGlogOpen logInit = (lpGlogOpen)GetProcAddress(hDllChip, "LogOpen");
			if (logInit != NULL)
			{
				logInit(ConfigTools::logPath.c_str());
				LOG(INFO) << "OpenCamera()-LogOpen";
			}
			else
				LOG(INFO) << "OpenCamera()-LogOpen not found";
		}
	}

	if (!isRegisterUsb)
	{
		if (RegisterUsbListener(DeviceChange) != 0) {
			LOG(WARNING) << "OpenCamera()-RegisterUsbListener Error";
			return -1;
		}
		LOG(INFO) << "OpenCamera()-RegisterUsbListener Success";
		isRegisterUsb = true;
	}
	//Size outputSize;
	//Mat irSrc = imread("G:\\Temp\\temp\\getData - Absolute\\getDataTest\\Release\\USB_TEMP\\1.jpg");
	//Mat trans = edgeDetectionAny(insertType, outputSize, irSrc);
	//Mat finalIr = saveFinalImage(trans, irSrc, outputSize, 0, insertType);//保存红外图并返回Mat
	if (CameraSdkInit(1) != 0)
	{
		LOG(WARNING) << "OpenCamera()-Camera SDK Init Error";
		return 1;
	}
	if (InitCamera() != 0)
	{
		LOG(WARNING) << "OpenCamera()-Camera Connect Failed!";
		return 2;
	}
	CameraReadParameterFromFile(m_hCamera, (char*)ConfigTools::conB.c_str());//切换参数（红外）
	//CUSBHid hidEnum;
	//InitLight(hidEnum);
	int lRes = InitLight(hidEnum);
	LOG(INFO) << "OpenCamera()-InitLight():" << lRes;
	if (lRes != 0)
	{
		LOG(WARNING) << "OpenCamera()-Light Device Open Failed!";
		return 3;
	}
	BOOL deviceverification = hidEnum.SystemAuthentic();
	LOG(INFO) << "OpenCamera()- SystemAuthentic()(1 - success):" << deviceverification;
	if (deviceverification == FALSE)
		LOG(ERROR) << "OpenCamera()- SystemAuthentic() failed";
	getFirmVer();

	string bVersion = " ";
	if (hidEnum.SystemGetVersionStr(bVersion))
		LOG(INFO) << "SystemAuthentic()-SystemGetVersionStr:" << bVersion;
	else
	{
		LOG(WARNING) << "SystemAuthentic()-SystemGetVersionStr failed";
		bVersion = " ";
	}
	ConfigTools::boardVersion = bVersion;
	hidEnum.LightInfrared();//打开红外
	//hidEnum.Close();
	LOG(INFO) << "OpenCamera() run success";
	isOpenCamera = true;

	return 0;
}

void greenTwinkle()
{
	CUSBHid hidEnum;
	InitLight(hidEnum);
	hidEnum.LED_GreenON();
	Sleep(500);
	hidEnum.LED_GreenOFF();
	hidEnum.Close();
}
void blueTwinkle()
{
	CUSBHid hidEnum;
	InitLight(hidEnum);
	hidEnum.LED_BlueON();
	Sleep(400);
	hidEnum.LED_BlueOFF();
	//Sleep(500);
	//hidEnum.LED_BlueON();
	//Sleep(500);
	//hidEnum.LED_BlueOFF();
	//Sleep(500);
	//hidEnum.LED_BlueON();
	//Sleep(500);
	//hidEnum.LED_BlueOFF();
	hidEnum.Close();
}



//关闭相机，释放占用资源
int CloseCamera()
{
	LOG(INFO) << "CloseCamera() run";
	LOG(INFO) << "isOpenCamera " << isOpenCamera << endl;
	if (isOpenCamera)
	{
		LOG(INFO) << "CloseCamera() CloseCamera" << endl;
		try {
			//CUSBHid hidEnum;
			//InitLight(hidEnum);
			if (hidEnum.IsOpened()) {
				hidEnum.LightALLOFF();
				LOG(INFO) << "CloseCamera() LightALLOFF" << endl;
				hidEnum.Close();
			}
			FreeLibrary(hDllChip);
			CameraUnInit(m_hCamera);
		}
		catch (...) {
			LOG(ERROR) << "CloseCamera() error" << endl;
		}
		isOpenCamera = false;
	}
	LOG(INFO) << "isOpenLog " << isOpenLog << endl;
	if (isOpenLog)
	{
		LOG(INFO) << "CloseCamera() CloseLog" << endl;
		if (hDllChip != NULL) {
			lpGlog logClose = (lpGlog)GetProcAddress(hDllChip, "LogClose");
			if (logClose != NULL)
			{
				logClose();
			}
		}
		google::ShutdownGoogleLogging();
		isOpenLog = false;
	}
	LOG(INFO) << "CloseCamera() end" << endl;
	return 0;
}

//获取证件具体类型
int GetType()
{
	LOG(INFO) << "GetType() run- ,insert:" << insertType;
	if (insertType == IDCARD)
	{
		LOG(INFO) << "Try to distinguish from IDcard and Foreigners' Permanent Residence Permit";
		string filename = "USB_TEMP\\IDInfo.txt";
		ifstream text;
		text.open(ConfigTools::curPath + filename, ios::in);

		if (!text.is_open())
			LOG(WARNING) << "Read text failed,path:" << ConfigTools::curPath + filename;
		else {
			vector<string> strVec;
			string inbuf;
			while (getline(text, inbuf)) {
				strVec.push_back(trim(inbuf));//每一行都保存到vector中
			}
			text.close();
			if (strVec[strVec.size() - 1] == "0")
				return IDCARD;
			else if (strVec[strVec.size() - 1] == "1")
				return HKMOTW_IDCARD;
			else if (strVec[strVec.size() - 1] == "2" || strVec[strVec.size() - 1] == "3")
				return Foreigner_Permanent_Residence_Permit;
		}
	}
	if (insertType == ONE_LINE_CARD || insertType == THTEE_LINE_CARD)
	{
		if (ocrFlag != 1)
		{
			LOG(WARNING) << "GetType()-ocrFlag != 1";
			return insertType;
		}

		String typeStr = mrzCode.substr(0, 2);//取机读码前两位字符，代表证件类型
		LOG(INFO) << "GetType()-specific type(机读码前两位): " << typeStr;
		if (insertType == ONE_LINE_CARD)
		{
			if (typeStr == "CD")//往来台湾通行证
			{
				insertType = TW_EXIT_ENTRY_PERMIT;
			}
			else {
				insertType = HKMO_EXIT_ENTRY_PERMIT;
			}

		}
		if (insertType == THTEE_LINE_CARD)
		{
			if (typeStr == "CT")//台胞证
			{
				insertType = TW_HOME_RETURN_PERMIT;
			}
			else if (typeStr == "CR"|| typeStr == "T<") {
				insertType = HKMO_HOME_RETURN_PERMIT;
			}
			//else if (typeStr == "T<") {
			//	insertType = HKMOTW_IDCARD;
			//}
		}

	}
	return insertType;
}
void GetSource(char* type) {
	type[0] = '0';
	type[1] = '2';
	return;
}
void GetJPEGPhoto(char* buffer, int* width, int* height, int* datalength)
{
	Mat output;
	whiteImage.copyTo(output);
	Size imgsize = output.size();
	//cvtColor(whiteImage, output, COLOR_BGR2RGB);
	int max_size = 1024 * 100;
	if (imgsize.area() * 3 > max_size)
	{
		double refactor = imgsize.area() * 3 / max_size;
		refactor = sqrt(refactor);
		int w = imgsize.width / refactor;
		int h = imgsize.height / refactor;
		resize(output, output, Size(w, h));
		imgsize = output.size();
	}
	vector<uchar> buf;
	imencode(".jpg", output, buf);
	memcpy(buffer, buf.data(), buf.size());
	*width = imgsize.width;
	*height = imgsize.height;
	*datalength = buf.size();
	return;
}
//暂时不需要此接口，为了兼容性暂时保留
int GetOcrResult()
{
	LOG(INFO) << "GetOcrResult() run";
	return ocrFlag;
}

//获取芯片读取结果（ 1-成功，其他代表失败）
int GetChipResult()
{
	LOG(INFO) << "GetChipResult() run";
	if (insertType == IDCARD)//身份证
	{
		LOG(INFO) << "GetChipResult() run-IDcardFlag:" << IDcardFlag;
		return IDcardFlag;
	}
	else
	{
		LOG(INFO) << "GetChipResult() run-chipFlag:" << chipFlag;
		return chipFlag;
	}
}


//返回证件真伪结果
const char* GetAuthenticity()
{
	LOG(INFO) << "GetAuthenticity() run";
	static char* authenticityResult = NULL;
	if (insertType != PASSPORT)
		return NULL;
	if (getImgFlag != 0)
		return NULL;
	if (ocrFlag != 1)
		return NULL;
	if (mrzCode[0] != 'P')
		return NULL;
	if (authenticityResult != NULL)
	{
		delete[] authenticityResult;
	}
	authenticityResult = new char[16];
	authenticityResult[15] = 0;
	memset(authenticityResult, '0', 15);
	if (GetChipResult() == 1)
		return authenticityResult;

	std::string contents = mrzCode;
	if (contents.length() < 88 || contents.length() > 100)
	{
		LOG(WARNING) << "GetAuthenticity()-contents.length < 88 || > 100";
		return NULL;
	}

	string tPath = ConfigTools::curPath;
	fstream image_uv;
	string UVPath;
	UVPath = ConfigTools::uvPath;
	image_uv.open(UVPath, ios::in);
	if (!image_uv)
	{
		LOG(WARNING) << "GetAuthenticity()-uv.bmp not found";
		image_uv.close();
		return NULL;
	}
	try {
		HINSTANCE hDllauth; //DLL句柄 
		lpIrUvRes authFun; //函数指针
#ifdef _WIN64
		hDllauth = LoadLibrary("ContentCkeck_DLL_x64.dll");
#else
		hDllauth = LoadLibrary("ContentCkeck_DLL.dll");

#endif // _WIN64

		if (hDllauth != NULL)
		{
			int result[14];
			long long res;
			authFun = (lpIrUvRes)GetProcAddress(hDllauth, "pyTrueResult");
			res = authFun(ConfigTools::whitePath.c_str(), ConfigTools::irPath.c_str(), ConfigTools::uvPath.c_str(), contents.c_str());
			FreeLibrary(hDllauth);
			int fac = 1;
			for (int i = 0; i < 14; i++)
			{

				result[i] = (res / fac) % 10;
				fac *= 10;
				if (result[i] == 1)
					authenticityResult[i] = '0';
				else if (result[i] == 2)
					authenticityResult[i] = '1';
				else
					authenticityResult[i] = '2';
			}
			LOG(INFO) << "GetAuthenticity() :" << authenticityResult;
			LOG(INFO) << "GetAuthenticity() end";
			return authenticityResult;
		}
		else
		{
			int ercode = GetLastError();
			LOG(WARNING) << "GetAuthenticity()-LoadLibrary ContentCkeck_DLL.dll failed! " << ercode << endl;
			LOG(INFO) << "GetAuthenticity() end";
			return NULL;
		}
	}
	catch (...)
	{
		LOG(ERROR) << "GetAuthenticity()-get authenticity error!" << endl;
		LOG(INFO) << "GetAuthenticity() end";
		return NULL;
	}
}

//把所有标志变量置为初始值
void initFlag()
{
	ocrFlag = 0;//ocr识别结果---1代表成功，其他代表失败
	chipFlag = 0;//芯片读取结果---1代表成功，其他代表失败
	IDcardFlag = 0;//身份证芯片读取结果---1代表成功，其他代表失败
	zbarFlag = 0;//二维码识别结果---1代表有二维码，其它表示失败
	insertType = UNKNOWN;//证件类型
}
int TakePhoto() {
	FuncLog f("TakePhoto");
	existAndDelete();

	static int delLogInterval = ConfigTools::delLogInterval;
	if (ConfigTools::delLogStatus && --delLogInterval <= 0)
	{
		delLogInterval = ConfigTools::delLogInterval;
		_beginthreadex(NULL, 0, &DelLogThread, NULL, 0, NULL);
	}

	initFlag();
	int getResult = 0;
	int isConn = CameraConnectTest(m_hCamera);
	BOOL isLightOpen = hidEnum.IsOpened();
	LOG(INFO) << "GetData()-CameraConnectTest():" << isConn;
	//控制光源
	if (!(isConn == 0 && isLightOpen == TRUE)) {
		getImgFlag = 1;
		if (isConn != 0)
			LOG(WARNING) << "GetData()-Camera Connect Failed! ";
		if (isLightOpen != TRUE)
			LOG(WARNING) << "GetData()-Light Open Failed! ";
		return 1;
	}
	try {
		int lRes = hidEnum.LightInfrared();//打开红外
		hidEnum.LED_WhiteON();//指示灯（金黄色），开始数据采集
		CameraReadParameterFromFile(m_hCamera, (char*)ConfigTools::conB.c_str());//切换参数（红外）
		do {
			//采集红外
			Mat irSrc;
			int imgBuff = getMat(irSrc);//采集红外（ir）
			if (imgBuff == 1) {
				LOG(WARNING) << "GetData()-get ir Buffer is null" << imgBuff;
				getResult = 4;
				break;
			}
			ir_offset(irSrc, ConfigTools::irOffset);

			Size outputSize;
			insertType = ANY_CARD;
			Mat trans = edgeDetectionAny(insertType, outputSize, irSrc);
			if (trans.empty()) {
				LOG(WARNING) << "GetData()-Can not find certificate edge! ";
				getResult = 5;
				break;
			}
			LOG(INFO) << "GetData()-after edgeDetectionAny()-insertType:" << insertType;
			Mat finalIr = saveFinalImage(trans, irSrc, outputSize, 0, insertType);//保存红外图并返回Mat

			//saveOriImg(irSrc, 0);//保存红外图并返回Mat


					//采集白光（左）
			hidEnum.LightWhiteLeft();//打开白光左
			if (CameraConnectTest(m_hCamera) != 0)
			{
				LOG(WARNING) << "GetData()-Camera Connect Failed! ";
				return 1;
			}
			int code = CameraReadParameterFromFile(m_hCamera, (char*)ConfigTools::conA.c_str());
			Mat wLeft;
			int imgBuff2 = getMat(wLeft);//采集白光（左）
			if (imgBuff2 == 1) {
				LOG(WARNING) << "GetData()-采集白光（左）Buffer为空！" << imgBuff2;
				getResult = 4;
				break;
			}
			hidEnum.LightWhiteRight();//打开白光右
			LOG(INFO) << "GetData()-finish getting image white-left";
			if (CameraConnectTest(m_hCamera) != 0)
			{
				LOG(WARNING) << "GetData()-Camera Connect Failed! ";
				return 1;
			}
			CameraReadParameterFromFile(m_hCamera, (char*)ConfigTools::conD.c_str());
			//Sleep(200);
			//采集白光（右）
			Mat wRight;
			int imgBuff3 = getMat(wRight);//采集白光（右）
			if (imgBuff3 == 1) {
				LOG(WARNING) << "GetData()-采集白光（右）Buffer为空！" << imgBuff3;
				getResult = 4;
				break;
			}
			LOG(INFO) << "GetData()-start mergeing image white-right";
			Mat whiteMerge = white_merge(wRight, wLeft, ConfigTools::whiteOffset);//合成白光图
			//imwrite("src.jpg", whiteMerge);
			saveFinalImage(trans, whiteMerge, outputSize, 1, insertType);//保存白光图
			//saveOriImg(whiteMerge, 1);//保存白光图
			LOG(INFO) << "GetData()-finish getting image white";

			hidEnum.LightPurple();//打开紫外
			if (CameraConnectTest(m_hCamera) != 0)
			{
				LOG(WARNING) << "GetData()-Camera Connect Failed! ";
				return 1;
			}
			CameraReadParameterFromFile(m_hCamera, (char*)ConfigTools::conC.c_str());
			//采集紫外
			Mat uvSrc;
			int imgBuff4 = getMat(uvSrc);
			if (imgBuff4 == 1) {
				LOG(WARNING) << "GetData()-采集紫外Buffer为空！" << imgBuff4;
				getResult = 4;
				break;
			}
			saveFinalImage(trans, uvSrc, outputSize, 2, insertType);//保存白光图
			//saveOriImg(uvSrc, 2);//保存紫外图
			LOG(INFO) << "GetData()-finish getting image uv";
			getResult = 0;
		} while (0);
		hidEnum.LightInfrared();//打开红外
		hidEnum.LED_WhiteOFF();
		LOG(INFO) << "GetData()-CameraReadParameterFromFile";
		//hidEnum.Close();
		if (CameraConnectTest(m_hCamera) != 0)
		{
			LOG(WARNING) << "GetData()-Camera Connect Failed! ";
			return 1;
		}
		CameraReadParameterFromFile(m_hCamera, (char*)ConfigTools::conB.c_str());//切换参数（红外）
		LOG(INFO) << "GetData()-(exit getdata function)- getResult:" << getResult;
		getImgFlag = getResult;
		return getResult;
	}
	catch (...) {
		LOG(ERROR) << "GetData() - GetData failed!";
		hidEnum.LightInfrared();//打开红外
		hidEnum.LED_WhiteOFF();
		CameraReadParameterFromFile(m_hCamera, (char*)ConfigTools::conB.c_str());//切换参数（红外）
		return -1;
	}
}
//开始采集数据: 拍照、处理照片（剪裁，合成等）、ocr、读取芯片等等

int myGetData(int timeout, bool smallCardNochipBan) {
	LOG(INFO) << "GetData() run";
	passTime = 0;
	idTime = 0;
	clock_t start = clock();
	existAndDelete();

	static int delLogInterval = ConfigTools::delLogInterval;
	if (ConfigTools::delLogStatus && --delLogInterval <= 0)
	{
		delLogInterval = ConfigTools::delLogInterval;
		_beginthreadex(NULL, 0, &DelLogThread, NULL, 0, NULL);
	}

	initFlag();
	int getResult = 0;
	int isConn = CameraConnectTest(m_hCamera);
	BOOL isLightOpen = hidEnum.IsOpened();
	LOG(INFO) << "GetData()-CameraConnectTest():" << isConn;
	//控制光源
	//CUSBHid hidEnum;
	if (!(isConn == 0 && isLightOpen == TRUE)) {
		getImgFlag = 1;
		if (isConn != 0)
			LOG(WARNING) << "GetData()-Camera Connect Failed! ";
		if (isLightOpen != TRUE)
			LOG(WARNING) << "GetData()-Light Open Failed! ";
		return 1;
	}
	try {
		//int lRes = InitLight(hidEnum);
		int lRes = hidEnum.LightInfrared();//打开红外
		hidEnum.LED_WhiteON();//指示灯（金黄色），开始数据采集
		CameraReadParameterFromFile(m_hCamera, (char*)ConfigTools::conB.c_str());//切换参数（红外）
		LOG(INFO) << "GetData()-LightInfrared():" << lRes;
		int chipType = getChipType();
		LOG(INFO) << "-------------------------------GetData()-getChipType()(0:IDCARD, 1:E-chip):--------------xxx" << chipType;
		if (chipType != 1) {//不是护照
			m_IDcardThread = (HANDLE)_beginthreadex(NULL, 0, &IDcardThread, NULL, 0, &m_IDcardID);
		}
		do {
			//采集红外
			Mat irSrc;
			int imgBuff = getMat(irSrc);//采集红外（ir）
			if (imgBuff == 1) {
				LOG(WARNING) << "GetData()-get ir Buffer is null" << imgBuff;
				getResult = 4;
				break;
			}
			waitForIdThread();//等待身份证读取线程结束
			LOG(INFO) << "GetData()-after m_IDcardThread-insertType:" << insertType;
			if (insertType == IDCARD && ConfigTools::checkSupport(IDCARD) == 0) {
				LOG(INFO) << "GetData()-insertType == 1,supportCertificate[IDCARD] == 0,break";
				getResult = 6;
				break;
			}
			if (!ConfigTools::allDocSave && insertType == IDCARD) {
				LOG(INFO) << "GetData()-IDCARD don't need edgeDetection, break";
				getResult = 0;
				break;
			}
			ir_offset(irSrc, ConfigTools::irOffset);
			//边缘检测
			Size outputSize;
			Mat trans = edgeDetectionAny(insertType, outputSize, irSrc);
			if (trans.empty()) {
				LOG(WARNING) << "GetData()-Can not find certificate edge! ";
				getResult = 5;
				break;
			}
			LOG(INFO) << "GetData()-after edgeDetectionAny()-insertType:" << insertType;
			Mat finalIr = saveFinalImage(trans, irSrc, outputSize, 0, insertType);//保存红外图并返回Mat

			if (insertType == PASSPORT && ConfigTools::checkSupport(PASSPORT) == 0) {
				LOG(INFO) << "GetData()-insertType == 0,supportCertificate[PASSPORT] == 0,break";
				getResult = 6;
				break;
			}
			if (insertType == SMALL_CARD) {
				//distinguishSmallCardLine(insertType, finalIr);
				int lines = OCRworker.getMrzNums(finalIr);
				if (lines == 1)
				{
					insertType = ONE_LINE_CARD;
				}
				else
				{
					insertType = THTEE_LINE_CARD;
				}
				LOG(INFO) << "GetData()-after distinguishSmallCardLine()-insertType:" << insertType;
			}
			if (insertType != IDCARD)
			{
				getOcr(finalIr);
				m_ProcThread = (HANDLE)_beginthreadex(NULL, 0, &ProcThread, NULL, 0, &m_threadID);
				GetType();
				if (!ConfigTools::checkSupport(insertType)) {
					LOG(INFO) << "GetData()-insertType == " << insertType << ",supportCertificate[" << insertType << "] == 0,break";
					getResult = 6;
					break;
				}
			}
			
			if (ConfigTools::allDocSave || insertType == PASSPORT) {
				if (ConfigTools::savewhite) {

					//采集白光（左）
					hidEnum.LightWhiteLeft();//打开白光左
					if (CameraConnectTest(m_hCamera) != 0)
					{
						LOG(WARNING) << "GetData()-Camera Connect Failed! ";
						return 1;
					}
					int code = CameraReadParameterFromFile(m_hCamera, (char*)ConfigTools::conA.c_str());
					Mat wLeft;
					int imgBuff2 = getMat(wLeft);//采集白光（左）
					if (imgBuff2 == 1) {
						LOG(WARNING) << "GetData()-采集白光（左）Buffer为空！" << imgBuff2;
						getResult = 4;
						break;
					}
					hidEnum.LightWhiteRight();//打开白光右
					LOG(INFO) << "GetData()-finish getting image white-left";
					if (CameraConnectTest(m_hCamera) != 0)
					{
						LOG(WARNING) << "GetData()-Camera Connect Failed! ";
						return 1;
					}
					CameraReadParameterFromFile(m_hCamera, (char*)ConfigTools::conD.c_str());
					//Sleep(200);
					//采集白光（右）
					Mat wRight;
					int imgBuff3 = getMat(wRight);//采集白光（右）
					if (imgBuff3 == 1) {
						LOG(WARNING) << "GetData()-采集白光（右）Buffer为空！" << imgBuff3;
						getResult = 4;
						break;
					}
					LOG(INFO) << "GetData()-start mergeing image white-right";
					Mat whiteMerge = white_merge(wRight, wLeft, ConfigTools::whiteOffset);//合成白光图
					//imwrite("src.jpg", whiteMerge);
					//zbarFlag = getZbarResult(whiteMerge);//获取红外光图下是否有二维码
					whiteImage = saveFinalImage(trans, whiteMerge, outputSize, 1, insertType);//保存白光图
					LOG(INFO) << "GetData()-finish getting image white";
				}
				if (ConfigTools::saveuv && zbarFlag == 0)
				{
					hidEnum.LightPurple();//打开紫外
					if (CameraConnectTest(m_hCamera) != 0)
					{
						LOG(WARNING) << "GetData()-Camera Connect Failed! ";
						return 1;
					}
					CameraReadParameterFromFile(m_hCamera, (char*)ConfigTools::conC.c_str());
					//采集紫外
					Mat uvSrc;
					int imgBuff4 = getMat(uvSrc);
					if (imgBuff4 == 1) {
						LOG(WARNING) << "GetData()-采集紫外Buffer为空！" << imgBuff4;
						getResult = 4;
						break;
					}
					saveFinalImage(trans, uvSrc, outputSize, 2, insertType);//保存紫外图
					LOG(INFO) << "GetData()-finish getting image uv";
				}
			}
			if (insertType != IDCARD && zbarFlag == 0)
			{
				waitForChipThread();
				if (chipFlag != 1) {
					if (insertType == PASSPORT && ConfigTools::passportNoChipBan) {
						LOG(INFO) << "Passport no chip";
						getResult = 6;
						break;
					}
					else if (insertType != PASSPORT && smallCardNochipBan) {
						LOG(INFO) << "Small Card no chip";
						getResult = 6;
						break;
					}
				}
				//ocr失败保存(读取芯片失败都保存)
				if (chipFlag != 1 && chipType == 1 && ConfigTools::saveErrImg)
					saveErrorImage(irSrc, finalIr);
				getResult = 0;
			}
			
		} while (0);
		waitForIdThread();
		waitForChipThread();
		hidEnum.LightInfrared();//打开红外
		hidEnum.LED_WhiteOFF();
		LOG(INFO) << "GetData()-CameraReadParameterFromFile";
		//hidEnum.Close();
		if (CameraConnectTest(m_hCamera) != 0)
		{
			LOG(WARNING) << "GetData()-Camera Connect Failed! ";
			return 1;
		}
		CameraReadParameterFromFile(m_hCamera, (char*)ConfigTools::conB.c_str());//切换参数（红外）
		LOG(INFO) << "GetData()-(exit getdata function)- getResult:" << getResult;
		getImgFlag = getResult;

		clock_t endF = clock();
		passTime = (double)(endF - start) / CLOCKS_PER_SEC;
		idTime = (double)(endF - start) / CLOCKS_PER_SEC;
		return getResult;
	}
	catch (...) {
		LOG(ERROR) << "GetData() - GetData failed!";
		waitForIdThread();
		waitForChipThread();
		hidEnum.LightInfrared();//打开红外
		hidEnum.LED_WhiteOFF();
		CameraReadParameterFromFile(m_hCamera, (char*)ConfigTools::conB.c_str());//切换参数（红外）
			//hidEnum.Close();
		LOG(INFO) << "GetData() - end";
		return -1;
	}
}

//开始采集数据: 拍照、处理照片（剪裁，合成等）、ocr、读取芯片等等
int GetData(int timeout)
{
	return myGetData(timeout, ConfigTools::smallCardNoChipBan);
}


//开始采集数据: 拍照、处理照片（剪裁，合成等）、ocr、读取芯片等等
int GetDataOCR(int timeout)
{
	return myGetData(timeout, false);
}


//返回json格式的字符串，包含了读取出来的证件信息，例如姓名等等，具体定义见SDK调用说明
//目前做法是把sdk根目录下的存有信息的txt文件读取出来，按照标准的json的key-value格式返回一个含有各项信息的字符串
//USB_TEMP文件夹内的IDInfo.txt（身份证信息）或是ChipMrz.txt（护照或通行证信息）或是mrz.txt（无芯片的护照，需手动解析各项信息）
static char* info = NULL;
const char* GetJson()
{
	LOG(INFO) << "GetJson() run";
	string buf;
	try
	{
		if (IDcardFlag == 1)
		{
			buf = processIDCardJson();
		}
		if (chipFlag == 1)
		{
			buf = processChip(insertType);
		}
		else if (ocrFlag == 1 && zbarFlag == 0)//芯片读取失败
		{
			buf = processMRZ(insertType, mrzCode);
		}
		if (zbarFlag == 1)//有二维码
		{
			buf = processZbar();
		}

		if (info != NULL)
		{
			delete[] info;
		}
		if (buf.empty())
		{
			info = NULL;
		}
		else
		{
			info = new char[buf.size() + 1];
			memcpy(info, buf.c_str(), buf.length() + 1);
		}
		LOG(INFO) << "GetJson() json:" << UTF8ToGBK(buf);
		LOG(INFO) << "GetJson() end";
		return info;
	}
	catch (...)
	{
		LOG(ERROR) << "GetJson()-GetJson error!";
		LOG(INFO) << "GetJson() end";
		return NULL;
	}
}
int getJsonLength() {
	return strlen(info);
}

//初始化相机，其中使用的函数基本都为相机厂商（MindVision）提供的
int InitCamera()
{
	LOG(INFO) << "InitCamera() run";
	int iCameraNums;
	CameraSdkStatus status;
	//枚举设备，获得设备列表
	iCameraNums = CameraEnumerateDeviceEx();
	if (iCameraNums <= 0)
	{
		LOG(WARNING) << "InitCamera()-No camera was found!";
		return 1;
	}
	//该示例中，我们只假设连接了一个相机。因此，只初始化第一个相机。(-1,-1)表示加载上次退出前保存的参数，如果是第一次使用该相机，则加载默认参数.
	//In this demo ,we just init the first camera.
	if (m_hCamera != -1)
		CameraUnInit(m_hCamera);
	if ((status = CameraInitEx(0, 0, 1, &m_hCamera)) != CAMERA_STATUS_SUCCESS)
	{
		LOG(WARNING) << "InitCamera()-CameraInitEx failed,CameraSdkStatus = " << status;
		return 2;
	}
	//Get properties description for this camera.
	CameraGetCapability(m_hCamera, &m_cap);
	if (m_cap.sIspCapacity.bMonoSensor)
	{
		// 让黑白相机最终输出MONO8，如果不设置，则CameraImageProcess会默认输出24位的灰度图
		CameraSetIspOutFormat(m_hCamera, CAMERA_MEDIA_TYPE_MONO8);
	}
	CameraPlay(m_hCamera);
	LOG(INFO) << "InitCamera() end";
	return 0;
}

//初始化光源，执行此函数后，传入的CUSBHid句柄即可操作设备，如开关灯
int InitLight(CUSBHid& hidEnum)
{
	BOOL ret;
	std::tstring gDevicePath;
	if (hidEnum.FindHidDevice(USB_VID, USB_PID, gDevicePath))
	{
		//LOG(INFO) << "InitLight()-Open开始";
		LOG(INFO) << "InitLight()-gDevicePath:" << gDevicePath;
		ret = hidEnum.Open(gDevicePath);
		if (ret == TRUE)
		{
			LOG(INFO) << "InitLight()-Open Success";
			//BOOL isOpenLight = hidEnum.LightInfrared();//打开红外
			//if (isOpenLight == false)
			//{
			//	LOG(WARNING) << "InitLight()-光源打开失败";
			//	return 3;
			//}
			//LOG(INFO) << "InitLight()-光源打开成功";
			return 0;
		}
		else
		{
			LOG(WARNING) << "InitLight()-light open failed";
			return 1;
		}
	}
	else
	{
		LOG(WARNING) << "InitLight()-can not find light device! ";
		return 2;
	}
}

//检测相机连接是否正常
int CameraIsConnected()
{
	try {
		int iCameraNums;
		CameraSdkStatus status;
		iCameraNums = CameraEnumerateDeviceEx();//枚举设备，获得设备列表
		if (iCameraNums <= 0)
			return 1;//MessageBox("No camera was found!");

		//CUSBHid hidEnum;
		//BOOL ret;
		//std::tstring gDevicePath;
		//if (hidEnum.FindHidDevice(USB_VID, USB_PID, gDevicePath))
		//{
		//	ret = hidEnum.Open(gDevicePath);
		//	if (ret == TRUE)
		//	{
		//		BOOL isOpenLight = hidEnum.LightInfrared();//打开红外
		//		if (isOpenLight == false)
		//		{
		//			LOG(WARNING) << "CameraIsConnected()-没有找到设备或设备激活失败";
		//			return 2;
		//		}
		//	}
		//	else
		//	{
		//		LOG(WARNING) << "CameraIsConnected()-光源打开失败";
		//		return 2;
		//	}
		//}
		//else
		//{
		//	LOG(WARNING) << "CameraIsConnected()-没有找到光源设备！";
		//	return 2;
		//}

		//hidEnum.Close();

		return 0;
	}
	catch (...) {
		LOG(ERROR) << "CameraIsConnected()-catch exception! ";
		return 2;
	}
}

//用于判断设备内是否有浅色物体进入，过程为拍一张红外光下的图像，检测图像中间区域像素值大于100小于230的像素数量，并返回像素数量
int inputDetectSingle(Mat input,float x, float y, float length, float width) {
	try {

		if (input.empty())
			return 0;
		ir_offset(input, ConfigTools::irOffset);
		cvtColor(input, input, COLOR_BGR2GRAY);
		if (!ConfigTools::highResolution) {
			double scale = 2592 / 1024;
			resize(input, input, Size(0, 0), scale, scale);
		}
		//resize to speed up calc
		resize(input, input, Size(0, 0), 0.5, 0.5,
			cv::INTER_NEAREST);
		//cv::blur(input, input, Size(5, 5));
		//inner area
		// w&h: 10%~90%
		Mat ROI_test = input(Rect(input.cols * x, input.rows * y,
			input.cols * length, input.rows * width));
		// count pixels between 100~230
		int count = countNonZero(ROI_test > 100);
		//LOG(INFO) << "inputDetectSingle()-count: " << count;
		return count;
	}
	catch (...) {
		LOG(ERROR) << "inputDetectSingle()-find error";
		return 500;
	}
}

//用于判断是否顶角检测
int inputDetectCorner(Mat input) {
	try {
		if (input.empty())
			return 0;
		ir_offset(input, ConfigTools::irOffset);
		cvtColor(input, input, COLOR_BGR2GRAY);
		if (!ConfigTools::highResolution) {
			double scale = 2592 / 1024;
			resize(input, input, Size(0, 0), scale, scale);
		}
		//resize to speed up calc
		resize(input, input, Size(0, 0), 0.5, 0.5,
			cv::INTER_NEAREST);
		int edge = 700;
		for (int i = 640; i < input.size().height - 5; i += 5) {
			if (input.at<uchar>(i, 1) + input.at<uchar>(i+5, 1) > 200) {
				edge = i;
				break;
			}
		}
		float y = (edge - 25) / 768.f;
		Mat ROI_test = input(Rect(input.cols * 0.025, input.rows * (y - 0.1),
			input.cols * 0.1, input.rows * 0.1));
		// count pixels between 100~230
		int count = countNonZero(ROI_test > 100);
		//LOG(INFO) << "inputDetectSingle()-count: " << count;
		return count;
	}
	catch (...) {
		LOG(ERROR) << "inputDetectCorner()-find error";
		return 500;
	}
}

//检查是否有证件放入 ，亮的像素数量大于阈值则认为有证件放入

int DocIsExist(Mat input) {
	//time_t now = time(0);
	//if (now % 5 == 0)
	//	LOG(INFO) << "DocIsExist()-定时记录";
	static bool exist = true;
	if (ConfigTools::turn)
	{
		exist = !exist;
		return exist;
	}
	static int lastStatus = 0;
	int thisStatus = inputDetectSingle(input, 0.1, 0.1, 0.8, 0.8);
	if (ConfigTools::absolute)
	{
		return !(thisStatus > 22000);
	}
	static bool isIn = false;
	int diff = thisStatus - lastStatus;
	lastStatus = thisStatus;
	if (isIn == false && diff > 10000)
	{
		isIn = true;
		LOG(INFO) << "DocIsExist()-insertion detected";
		if (thisStatus > 16000)
			return 0;
	}
	if (isIn == true && diff < -10000 && thisStatus <= 16000)
	{
		isIn = false;
		LOG(INFO) << "DocIsExist()-leave detected";
	}
	if (isIn == true && thisStatus > 16000)
		return 0;
	return 1;

	//int thisStatus = inputDetectSingle();
	//if (thisStatus > 25000)
	//	return 0;
	//else
	//	return 1;
	//
	//cout << " inputDetectSingle:" << thisStatus << endl;;
	//if (thisStatus > last1Status * 0.7 && thisStatus > last2Status * 1.5 &&
	//	last1Status > last2Status * 1.5) {
	//	// 避免连续触发
	//	last2Status = INT_MAX;
	//	last1Status = INT_MAX;
	//	return 0;
	//}
	//else {
	//	if (thisStatus < 20000)
	//		thisStatus = 20000;
	//	last2Status = last1Status;
	//	last1Status = thisStatus;
	//	return 1;
	//}

}

//检查是否有证件放入 ，亮的像素数量大于阈值则认为有证件放入

int DocIsExistAtCorner(Mat input) {
	//time_t now = time(0);
	//if (now % 5 == 0)
	//	LOG(INFO) << "DocIsExist()-定时记录";
	static bool exist = true;
	if (ConfigTools::turn)
	{
		exist = !exist;
		return exist;
	}
	static int lastStatus = 0;
	int thisStatus = inputDetectSingle(input, 0.1, 0.1, 0.8, 0.8);
	if (ConfigTools::absolute)
	{
		int cornerStatus = inputDetectCorner(input);
		Mat whitePure = Mat(input.cols, input.rows, CV_8UC3, Scalar(255, 255, 255));
		int whiteStatus = inputDetectSingle(whitePure, 0, 0, 0.1, 0.1);
		//cout << "whiteStatus = " << whiteStatus << endl;
		//cout << "cornerStatus = " << cornerStatus << endl;
		exist = !((thisStatus > 22000) && (whiteStatus - 1700 < cornerStatus));
		//cout << exist << endl;
		return exist;
	}
	static bool isIn = false;
	int diff = thisStatus - lastStatus;
	lastStatus = thisStatus;
	if (isIn == false && diff > 10000)
	{
		isIn = true;
		LOG(INFO) << "DocIsExistAtCorner()-insertion detected";
	}
	if (isIn == true && diff < -10000)
	{
		isIn = false;
		LOG(INFO) << "DocIsExistAtCorner()-leave detected";
	}
	if (isIn == true && thisStatus > 16000)
		return 0;
	return 1;
}
int DocIsExist() {
	Mat input;
	getMat(input);
	return DocIsExist(input);
}

int DocIsExistAtCorner() {
	Mat input;
	getMat(input);
	return DocIsExistAtCorner(input);
}
int aHash(Mat matSrc1, Mat matSrc2)
{
	Mat matDst1, matDst2;
	cv::resize(matSrc1, matDst1, cv::Size(8, 8), 0, 0, cv::INTER_CUBIC);
	cv::resize(matSrc2, matDst2, cv::Size(8, 8), 0, 0, cv::INTER_CUBIC);

	cv::cvtColor(matDst1, matDst1, COLOR_BGR2GRAY);
	cv::cvtColor(matDst2, matDst2, COLOR_BGR2GRAY);

	int iAvg1 = 0, iAvg2 = 0;
	int arr1[64], arr2[64];

	for (int i = 0; i < 8; i++)
	{
		uchar* data1 = matDst1.ptr<uchar>(i);
		uchar* data2 = matDst2.ptr<uchar>(i);

		int tmp = i * 8;

		for (int j = 0; j < 8; j++)
		{
			int tmp1 = tmp + j;

			arr1[tmp1] = data1[j] / 4 * 4;
			arr2[tmp1] = data2[j] / 4 * 4;

			iAvg1 += arr1[tmp1];
			iAvg2 += arr2[tmp1];
		}
	}

	iAvg1 /= 64;
	iAvg2 /= 64;

	for (int i = 0; i < 64; i++)
	{
		arr1[i] = (arr1[i] >= iAvg1) ? 1 : 0;
		arr2[i] = (arr2[i] >= iAvg2) ? 1 : 0;
	}

	int iDiffNum = 0;

	for (int i = 0; i < 64; i++)
		if (arr1[i] != arr2[i])
			++iDiffNum;

	return iDiffNum;
}

int DocStable() {
	Mat img1, img2;
	getMat(img1);
	Sleep(300);
	getMat(img2);
	if (img1.empty() || img2.empty())
	{
		LOG(INFO) << "DocStable()-img1.empty() || img2.empty()";
		return 0;
	}
	int ahashres = aHash(img1, img2);

	if (ahashres < 3)
	{
		return 1;
	}
	return 0;
}
int DocIsExistAtCornerAndStable() {
	if (DocIsExistAtCorner() == 1)
	{
		return 1;
	}
	Mat img1, img2, img3, img4;
	int ahashres;
	for (int i = 0; i < 30; i++) {
		getMat(img1);
		if (DocIsExistAtCorner(img1) == 1)
		{
			return 1;
		}
		Sleep(100);
		getMat(img2);
		if (DocIsExistAtCorner(img2) == 1)
		{
			return 1;
		}
		Sleep(100);
		getMat(img3);
		if (DocIsExistAtCorner(img3) == 1)
		{
			return 1;
		}
		Sleep(100);
		getMat(img4);
		if (DocIsExistAtCorner(img4) == 1)
		{
			return 1;
		}
		ahashres = aHash(img1, img4);
		if (ahashres < 3)
		{
			return 0;
		}
	}
	LOG(ERROR) << "DocIsExistAtCornerAndStable()-Document is not stable";
	return 0;
}

int DocIsExistAndStable() {
	if (DocIsExist() == 1)
	{
		return 1;
	}
	Mat img1, img2, img3, img4;
	int ahashres;
	for (int i = 0; i < 30; i++) {
		getMat(img1);
		if (DocIsExist(img1) == 1)
		{
			return 1;
		}
		Sleep(100);
		getMat(img2);
		if (DocIsExist(img2) == 1)
		{
			return 1;
		}
		Sleep(100);
		getMat(img3);
		if (DocIsExist(img3) == 1)
		{
			return 1;
		}
		Sleep(100);
		getMat(img4);
		if (DocIsExist(img4) == 1)
		{
			return 1;
		}
		ahashres = aHash(img1, img4);
		if (ahashres < 3)
		{
			return 0;
		}
	}
	LOG(ERROR) << "DocIsExistAndStable()-Document is not stable";
	return 0;
}
//相机拍照，采集到的图像存于以Mat形式返回，默认的照片和正常相比旋转了180度，confNum暂时没有使用
int getMat(cv::Mat& img, int confNum)
{
	tSdkFrameHead 	sFrameInfo;
	BYTE* pRgbBuffer = NULL;;
	try {
		pRgbBuffer = CameraGetImageBufferPriorityEx(m_hCamera, &sFrameInfo.iWidth, &sFrameInfo.iHeight, 1000, CAMERA_GET_IMAGE_PRIORITY_NEWEST);
		sFrameInfo.uiMediaType = (m_cap.sIspCapacity.bMonoSensor ? CAMERA_MEDIA_TYPE_MONO8 : CAMERA_MEDIA_TYPE_BGR8);
		sFrameInfo.uBytes = sFrameInfo.iWidth * sFrameInfo.iHeight * CAMERA_MEDIA_TYPE_PIXEL_SIZE(sFrameInfo.uiMediaType) / 8;
		if (pRgbBuffer == NULL)
		{
			LOG(WARNING) << "getMat()-Camera Buffer is null";
			return 1;
		}
		Mat matImage(
			Size(sFrameInfo.iWidth, sFrameInfo.iHeight),
			sFrameInfo.uiMediaType == CAMERA_MEDIA_TYPE_MONO8 ? CV_8UC1 : CV_8UC3,
			pRgbBuffer
		);
		//LOG(INFO) << "getMat()-matImage.cols:" << matImage.cols;
		//LOG(INFO) << "getMat()-matImage.rows:" << matImage.rows;
		if (matImage.empty())//|| matImage.cols != 2592 || matImage.rows != 1944)
		{
			LOG(WARNING) << "getMat()-get mat failed";
			return 1;
		}
		img = matImage;
		return 0;
	}
	catch (...) {
		LOG(ERROR) << "getMat()-get mat error";
		return 1;
	}
}

//相机拍照，采集到的图像存于以Mat形式返回，照片已经经过旋转处理，可以正常使用
int getMat(cv::Mat& img)
{
	tSdkFrameHead 	sFrameInfo;
	BYTE* pRgbBuffer = NULL;
	try {
		pRgbBuffer = CameraGetImageBufferPriorityEx(m_hCamera, &sFrameInfo.iWidth, &sFrameInfo.iHeight, 1000, CAMERA_GET_IMAGE_PRIORITY_NEWEST);
		sFrameInfo.uiMediaType = (m_cap.sIspCapacity.bMonoSensor ? CAMERA_MEDIA_TYPE_MONO8 : CAMERA_MEDIA_TYPE_BGR8);
		sFrameInfo.uBytes = sFrameInfo.iWidth * sFrameInfo.iHeight * CAMERA_MEDIA_TYPE_PIXEL_SIZE(sFrameInfo.uiMediaType) / 8;
		if (pRgbBuffer == NULL)
		{
			LOG(WARNING) << "getMat()-Camera Buffer is null";
			return 1;
		}
		Mat matImage(
			Size(sFrameInfo.iWidth, sFrameInfo.iHeight),
			sFrameInfo.uiMediaType == CAMERA_MEDIA_TYPE_MONO8 ? CV_8UC1 : CV_8UC3,
			pRgbBuffer
		);
		if (matImage.empty())
		{
			LOG(WARNING) << "getMat()-mat is empty";
			return 1;
		}
		matImage.copyTo(img);
		flip(img, img, 1);
		flip(img, img, -1);
		return 0;
	}
	catch (...) {
		LOG(ERROR) << "getMat(flip)-get mat error";
		return 1;
	}
}

//保存最终图片（经过剪裁和色彩处理）,n取0~3分别代表 红外，白光，紫外 的路径;docType: 0--护照；1--身份证；2--港澳通行证
Mat saveFinalImage(const Mat& trans, Mat& src, Size outputsize, int n, CARD_TYPE docType)
{
	if (n < 0 || n>2)
		return Mat();
	//string originPath[] = { tPath + "USB_TEMP\\outputIr.BMP", tPath + "USB_TEMP\\output.BMP", tPath + "USB_TEMP\\UV.bmp" };
	string finalPath[] = { ConfigTools::irPath, ConfigTools::whitePath , ConfigTools::uvPath };
	//Mat Oring = imread(originPath[n]);//读入图片 
	Mat Oring;
	src.copyTo(Oring);
	Mat FinalResize;
	warpAffine(Oring, FinalResize, trans, outputsize, INTER_LINEAR, BORDER_REPLICATE);
	imwrite(finalPath[n], FinalResize);
	return FinalResize;
}
void saveOriImg(Mat& src, int n) {
	if (n < 0 || n>2)
		return;
	//string originPath[] = { tPath + "USB_TEMP\\outputIr.BMP", tPath + "USB_TEMP\\output.BMP", tPath + "USB_TEMP\\UV.bmp" };
	string finalPath[] = { ConfigTools::irPath, ConfigTools::whitePath , ConfigTools::uvPath };
	imwrite(finalPath[n], src);
}
bool DecSort(vector<Point> a, vector<Point> b)//工具: 定义排序规则
{
	return (contourArea(a) > contourArea(b));
}

//检测是否是港澳通行证（返回: 2-港澳通行证；3-（回乡证）；-2-未知）
//(准确说是判断证件是一行机读码还是三行机读码，返回2: 一行，返回3: 三行，返回-2: 未知)
CARD_TYPE distinguishSmallCardLine(CARD_TYPE& docType, Mat& ir)
{

	Mat IRMat;
	double scale = 2592 / 1024;
	resize(ir, IRMat, Size(0, 0), scale, scale);
	if (IRMat.cols < 500 || IRMat.rows < 500)
	{
		LOG(WARNING) << "distinguishSmallCardLine()-ir.cols < 500 || ir.rows < 500";
		docType = UNKNOWN;
		return UNKNOWN;
	}
	Mat src, IrGray, IrBinary, closeImg, rectKernel, sqKernel;
	rectKernel = getStructuringElement(MORPH_RECT, Size(75, 25));
	sqKernel = getStructuringElement(MORPH_RECT, Size(61, 61));
	string tPath = ConfigTools::curPath;
	//IrGray = imread(tPath + ".\\USB_TEMP\\2_ir.BMP", 0);
	IRMat.copyTo(src);
	cvtColor(src, IrGray, COLOR_BGR2GRAY);
	GaussianBlur(IrGray, IrGray, Size(3, 3), 0);
	cv::threshold(IrGray, IrBinary, 100, 255, THRESH_BINARY_INV);
	morphologyEx(IrBinary, closeImg, MORPH_CLOSE, rectKernel);
	morphologyEx(closeImg, closeImg, MORPH_CLOSE, sqKernel);

	vector<vector<Point>>contours;
	vector<Vec4i>hierarchy;
	findContours(closeImg, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);//获取边缘
	if (contours.empty())
	{
		LOG(WARNING) << "distinguishSmallCardLine()-contours.empty()";
		docType = UNKNOWN;
		return UNKNOWN;
	}
	Rect maxRect;
	sort(contours.begin(), contours.end(), DecSort);//按面积降序排列
	for (int i = 0; i < contours.size(); i++)
	{
		maxRect = boundingRect(contours[i]);//获取矩形边框
		double por = (double)maxRect.width / IrGray.cols;
		if (por > 0.95)
			continue;
		if (por > 0.8)
		{
			double porVe = (double)maxRect.height / IrGray.rows;
			if (porVe > 0.1)
			{
				docType = THTEE_LINE_CARD;
				return THTEE_LINE_CARD;
			}
			else
			{
				docType = ONE_LINE_CARD;
				return ONE_LINE_CARD;
			}
		}
	}
	LOG(WARNING) << "distinguishSmallCardLine()-docType = UNKNOWN";
	docType = UNKNOWN;
	return UNKNOWN;
}

//保存Ocr失败图片（保存在errOcrImg文件夹内，原始红外图和剪裁后的红外图）
int saveErrorImage(Mat irSrc, Mat irCut)
{
	char save_time[100];
	time_t now = time(NULL);
	struct tm* ptr;
	ptr = localtime(&now);
	strftime(save_time, 60, "%Y-%m-%d_%H-%M-%S", ptr);

	string saveTime = save_time;
	string tPath = ConfigTools::curPath;
	char dir[MAX_PATH] = { 0 };
	string tP = ConfigTools::logPath + "errOcrImg";
	strcpy(dir, tP.c_str());
	CreateDirectory(dir, NULL);
	string errFile = ConfigTools::logPath + "errOcrImg\\";
	imwrite(errFile + saveTime + "Origin.jpg", irSrc);
	imwrite(errFile + saveTime + "Cut.jpg", irCut);
	LOG(WARNING) << "saveErrorImage()";
	return 0;
}

//获取证件芯片种类:1--护照或其他带电子芯片港澳台证件；-1--无芯片；-2--芯片读取设备没打开
int getChipType()
{
	//HINSTANCE hDllChip; //DLL句柄 
	lpChipType chipType; //函数指针
	try {
		//hDllChip = LoadLibrary("cis_passportreader.dll");
		if (hDllChip != NULL)
		{
			chipType = (lpChipType)GetProcAddress(hDllChip, "PassportTest");
			int type = chipType();
			//FreeLibrary(hDllChip);
			return type;
		}
		else
			LOG(ERROR) << "loadLibrary cis_passportreader.dll failed!";
		return -3;
	}
	catch (...) {
		LOG(ERROR) << "unknown error: loadLibrary cis_passportreader.dll!";
		return -4;
	}
}
void mrzInt2char(std::string& input, int startPos, int endPos) {
	std::replace(input.begin() + startPos, input.begin() + endPos, '0', 'O');
	std::replace(input.begin() + startPos, input.begin() + endPos, '1', 'I');
	std::replace(input.begin() + startPos, input.begin() + endPos, '2', 'Z');
	std::replace(input.begin() + startPos, input.begin() + endPos, '5', 'S');
	std::replace(input.begin() + startPos, input.begin() + endPos, '8', 'B');
}

void mrzChar2int(std::string& input, int startPos, int endPos) {
	std::replace(input.begin() + startPos, input.begin() + endPos, 'O', '0');
	std::replace(input.begin() + startPos, input.begin() + endPos, 'D', '0');
	std::replace(input.begin() + startPos, input.begin() + endPos, 'I', '1');
	std::replace(input.begin() + startPos, input.begin() + endPos, 'Z', '2');
	std::replace(input.begin() + startPos, input.begin() + endPos, 'U', '0');
	std::replace(input.begin() + startPos, input.begin() + endPos, 'S', '5');
	std::replace(input.begin() + startPos, input.begin() + endPos, 'B', '8');
}
void correctPassportMrz(std::string& mrzCode) {
	if (mrzCode.size() < 89) { return; }
	mrzInt2char(mrzCode, 0, 44);
	mrzChar2int(mrzCode, 45 + 13, 45 + 13 + 7);
	mrzChar2int(mrzCode, 45 + 21, 45 + 21 + 7);
	if (mrzCode[45 + 20] == 'H') {
		mrzCode[45 + 20] = 'M';
	}
}
int getOcr(Mat ir)
{
	string tPath = ConfigTools::curPath;
	char irPath[MAX_PATH] = { 0 };
	char biPath[MAX_PATH] = { 0 };
	string irT = tPath + "USB_TEMP\\IR.jpg";
	//string irB = tPath + "USB_TEMP\\mrzImg.bmp";
	string USB_TEMP = tPath + "USB_TEMP";
	string pb = tPath + "models\\mrzChar.pb";
	//string mrz = tPath + "mrz.txt";
	//char* irTp = (char*)irT.c_str();
	//char* irBp = (char*)irB.c_str();
	//char* USB_TEMP_P = (char*)USB_TEMP.c_str();
	//char* pb_p = (char*)pb.c_str();
	//char* mrz_P = (char*)mrz.c_str();
	if (!OcrInit)
	{
		if (OCRworker.initNet(pb) == 0)
		{
			LOG(INFO) << "getOcr()-initNet Success";
			OcrInit = true;
		}
		else
		{
			OcrDll = FALSE;
			ocrFlag = 0;//ocr识别结果---1代表成功，其他代表失败
			chipFlag = 0;//芯片读取结果---1代表成功，其他代表失败
			LOG(ERROR) << "getOcr()-initNet error！";
			return 0;
		}

	}
	OcrDll = TRUE;
	LOG(INFO) << "getOcr()-ocr start";
	//cvtColor(ir, ir, COLOR_RGB2GRAY);
	if (insertType == PASSPORT) {
		mrzCode = OCRworker.OCR(ir, 0, USB_TEMP);
		string mrzCodeOri = mrzCode;
		correctPassportMrz(mrzCode);
		if (mrzCodeOri != mrzCode) {
			LOG(INFO) << "getOcr()-ORI MRZcode:" << mrzCode;
		}
	}
	else if (insertType == ONE_LINE_CARD)
		mrzCode = OCRworker.OCR(ir, 1, USB_TEMP);
	else if (insertType == THTEE_LINE_CARD)
		mrzCode = OCRworker.OCR(ir, 2, USB_TEMP);
	if ((mrzCode.length() >= 30) && (mrzCode.length() < 100))
	{
		if (insertType == PASSPORT)
		{
			if (mrzCode.size() < 88)
			{
				ocrFlag = 0;//ocr识别结果---1代表成功，其他代表失败
				LOG(INFO) << "getOcr()-ocrFlag = 0";
				chipFlag = 0;//芯片读取结果---1代表成功，其他代表失败
			}
			else {

				ocrFlag = 1;//ocr识别结果---1代表成功，其他代表失败
				LOG(INFO) << "getOcr()-ocrFlag = 1";
			}
		}
		else
		{
			ocrFlag = 1;//ocr识别结果---1代表成功，其他代表失败
			LOG(INFO) << "getOcr()-ocrFlag = 1";
		}
	}
	ofstream OutFile("mrz.txt");
	OutFile << mrzCode;
	OutFile.close();
	LOG(INFO) << "getOcr()-MRZcode: " << mrzCode;
	LOG(INFO) << "getOcr()-length:" << mrzCode.length();
	LOG(INFO) << "getOcr()-ocr end";
	return 0;
}

//进行ocr识别，输入红外图像的路径2_ir.bmp和证件类型，调用后会在dll根目录生成mrz.txt，保存着识别后的机读码
//int getOcr()
//{
//	string tPath = ConfigTools::curPath;
//	char irPath[MAX_PATH] = { 0 };
//	char biPath[MAX_PATH] = { 0 };
//	string irT = ConfigTools::irPath;
//	string irB = tPath + "USB_TEMP\\mrzImg.bmp";
//	string USB_TEMP = tPath + "USB_TEMP";
//	string pb = ConfigTools::curPath + string("models\\mrzChar.pb");
//	string mrz = tPath + "mrz.txt";
//	char* irTp = (char*)irT.c_str();
//	char* irBp = (char*)irB.c_str();
//	char* USB_TEMP_P = (char*)USB_TEMP.c_str();
//	char* pb_p = (char*)pb.c_str();
//	char* mrz_P = (char*)mrz.c_str();
//
//	OcrDll = TRUE;
//	LOG(INFO) << "getOcr()-ocr start";
//	try
//	{
//		int f = 0;
//		if (insertType == PASSPORT)
//			f = findMrzAPI(irTp, 0, USB_TEMP_P);
//		else if (insertType == ONE_LINE_CARD)
//			f = findMrzAPI(irTp, 1, USB_TEMP_P);
//		else if (insertType == THTEE_LINE_CARD)
//			f = findMrzAPI(irTp, 2, USB_TEMP_P);
//		else
//			f = 1;
//		if (f == 0)
//		{
//			if (insertType == PASSPORT)
//				mrzOcrAPI(irBp, 0, pb_p, mrz_P);
//			else if (insertType == ONE_LINE_CARD)
//				mrzOcrAPI(irBp, 1, pb_p, mrz_P);
//			else
//				mrzOcrAPI(irBp, 2, pb_p, mrz_P);
//		}
//
//		else
//		{
//			LOG(WARNING) << "getOcr()-findOcrFun()!=0";
//			OcrDll = FALSE;
//			ocrFlag = 0;//ocr识别结果---1代表成功，其他代表失败
//			chipFlag = 0;//芯片读取结果---1代表成功，其他代表失败
//		}
//	}
//	catch (...)
//	{
//		OcrDll = FALSE;
//		ocrFlag = 0;//ocr识别结果---1代表成功，其他代表失败
//		chipFlag = 0;//芯片读取结果---1代表成功，其他代表失败
//		LOG(ERROR) << "getOcr()-ocr error！";
//	}
//	string filename = "mrz.txt";
//	ifstream t;
//	t.open(ConfigTools::curPath + filename, ios::in);
//	if (!t.is_open())
//	{
//		LOG(WARNING) << "getOcr()-mrz.text open failed!" << ConfigTools::curPath + filename;
//		ocrFlag = 0;//ocr识别结果---1代表成功，其他代表失败
//		chipFlag = 0;//芯片读取结果---1代表成功，其他代表失败
//		LOG(INFO) << "getOcr end";
//		return 0;
//	}
//	std::stringstream buffer;
//	buffer << t.rdbuf();
//	std::string contents(buffer.str());
//	LOG(INFO) << "getOcr()-MRZcode(mrz.txt): " << contents;
//	LOG(INFO) << "getOcr()-length:" << contents.length();
//	t.close();
//
//	if ((contents.length() >= 20) && (contents.length() < 100))
//	{
//		if ((insertType == PASSPORT) && (contents.length() < 88))
//		{
//			ocrFlag = 0;//ocr识别结果---1代表成功，其他代表失败
//			LOG(INFO) << "getOcr()-ocrFlag = 0";
//			chipFlag = 0;//芯片读取结果---1代表成功，其他代表失败
//		}
//		else
//		{
//			ocrFlag = 1;//ocr识别结果---1代表成功，其他代表失败
//			LOG(INFO) << "getOcr()-ocrFlag = 1";
//			mrzCode = contents;
//#ifdef _WIN64
//			//判断新版还是旧版中国护照,或者台湾、澳门护照：
//			MRZFlage = NEW;
//			if (contents[45] == 'G')MRZFlage = OLD;
//			else if (contents[45] == 'K')MRZFlage = HK;
//			else if (contents[45] == 'M')MRZFlage = AM;
//			else if (contents[2] == 'T' && contents[3] == 'W' && contents[4] == 'N')MRZFlage = TW;
//			LOG(INFO) << "getOcr()-MRZFlage：" << contents[45] << "  " << MRZFlage;
//#endif
//		}
//	}
//	LOG(INFO) << "getOcr()-ocr end";
//	return 0;
//}
UINT WINAPI DelLogThread(LPVOID lpParam) {
	LOG(INFO) << "DelLogThread start";
	delLog(ConfigTools::delLogDays);
	LOG(INFO) << "DelLogThread end";
	return 0;
}
//读取芯片信息（护照和各种通行证等），输入机读码和证件类型，读取成功后会在USB_TEMP文件夹内会生成ChipMrz.txt和DG2.bmp两个文件
//分别为读取到的个人信息和头像
UINT WINAPI ProcThread(LPVOID lpParam)
{
	LOG(INFO) << "ProcThread start";
	string tPath = ConfigTools::curPath;
	if (ocrFlag == 1)
	{
		//读取芯片信息
		//HINSTANCE hDllChip; //DLL句柄 
		lpChip chipFun; //函数指针
		try
		{
			//hDllChip = LoadLibrary("cis_passportreader.dll");
			if (hDllChip != NULL)
			{
				LOG(INFO) << "ProcThread()-insertType:" << insertType;
				int resultChip;
				if (insertType == PASSPORT)
					chipFun = (lpChip)GetProcAddress(hDllChip, "EchipRead");
				else if (insertType == ONE_LINE_CARD || insertType == HKMO_EXIT_ENTRY_PERMIT || insertType == TW_EXIT_ENTRY_PERMIT)
					chipFun = (lpChip)GetProcAddress(hDllChip, "EchipCardRead");
				else
					chipFun = (lpChip)GetProcAddress(hDllChip, "EchipThreeCardRead");
				//去掉结尾换行
				String finalString = mrzCode.substr(0, mrzCode.length() - 1);
				resultChip = chipFun(finalString.c_str());
				if (resultChip != 1)
					resultChip = chipFun(finalString.c_str());
				//FreeLibrary(hDllChip);
				LOG(INFO) << "ProcThread()-read the chip information finish (1 for success): " << resultChip;
				chipFlag = resultChip;//芯片读取结果---1代表成功，其他代表失败
			}
		}
		catch (...)
		{
			chipFlag = 0;
			LOG(ERROR) << "ProcThread()-chip read error!" << endl;
		}
	}
	LOG(INFO) << "ProcThread()-chip read end";
	LOG(INFO) << "ProcThread end";
	return 0;
}

//读取身份证芯片信息，读取成功后会在USB_TEMP文件夹内会生成IDInfo.txt和id.bmp两个文件
//分别为读取到的个人信息和头像
UINT WINAPI IDcardThread(LPVOID lpParam)
{
	LOG(INFO) << "IDcardThread start";
	//读取芯片信息
	//HINSTANCE hDllChip; //DLL句柄 
	lpIDcard IDFun; //函数指针
	//hDllChip = LoadLibrary("cis_passportreader.dll");
	try {
		if (hDllChip != NULL) {
			int resultCard = 0;
			IDFun = (lpIDcard)GetProcAddress(hDllChip, "IDCardRead");
			resultCard = IDFun();
			//FreeLibrary(hDllChip);
			LOG(INFO) << "IDcardThread()-Try to read the IDcard chip information finish (1 for success): " << resultCard;
			IDcardFlag = resultCard;//芯片读取结果---1代表成功，其他代表失败
		}
		else {
			LOG(WARNING) << "IDcardThread - LoadLibrary cis_passportreader.dll failed";
		}

	}
	catch (...) {
		LOG(ERROR) << "IDcardThread()-IDCardRead() error";
	}
	LOG(INFO) << "IDcardThread end";
	return 0;
}

//等待身份证读取线程结束，m_IDcardThread为null说明线程没有开启，直接退出
void waitForIdThread()
{
	if (NULL != m_IDcardThread)
	{
		LOG(INFO) << "waitForIdThread()-try to read IDCard";
		//::WaitForSingleObject(m_IDcardThread, INFINITE);
		try {
			DWORD idRet = ::WaitForSingleObject(m_IDcardThread, 8000);
			LOG(INFO) << "waitForIdThread()-m_IDcardThread end";
			if (idRet == WAIT_TIMEOUT) {
				DWORD exCode = 0;
				TerminateThread(m_IDcardThread, exCode);
				LOG(WARNING) << "waitForIdThread()=============read IDCard out of time=============";
			}
			CloseHandle(m_IDcardThread);
			m_IDcardThread = NULL;
			if (IDcardFlag == 1) {
				insertType = IDCARD;
				LOG(INFO) << "waitForIdThread()-Certificate type is IDcard";
			}
		}
		catch (...) {
			LOG(ERROR) << "waitForIdThread()-waitForIdThread error";
		}
	}
}

//等待芯片读取线程结束，m_ProcThread为null说明线程没有开启，直接退出
void waitForChipThread()
{
	if (NULL != m_ProcThread)
	{
		LOG(INFO) << "waitForChipThread()-wait for chip thread to finish";
		try {
			DWORD chipTH = ::WaitForSingleObject(m_ProcThread, 8000);
			if (chipTH == WAIT_TIMEOUT)
			{
				DWORD exCode = 0;
				TerminateThread(m_ProcThread, exCode);
				LOG(WARNING) << "waitForChipThread()-read chip timeout!";
			}
			CloseHandle(m_ProcThread);
			LOG(INFO) << "waitForChipThread()-chip tread finished!";
			m_ProcThread = NULL;
		}
		catch (...) {
			LOG(ERROR) << "waitForChipThread()-waitForIdThread error";
		}
	}
}

//生成校正参数（位于USB_Refer文件夹）,返回0代表成功，暂时不需要提供给客户，目前是生成一个调用此接口的exe文件随sdk放在一起，需要的时候再调用
int GenerateRefer()
{
	LOG(INFO) << "GenerateRefer start";
	int isConn = CameraConnectTest(m_hCamera);
	if (isConn == 0)
	{
		//控制光源
		//CUSBHid hidEnum;
		//int l = InitLight(hidEnum);
		BOOL l = hidEnum.IsOpened();
		if (l != TRUE)
		{
			CameraUnInit(m_hCamera);
			LOG(ERROR) << "generateRefer()-光源打开失败！";
			return 2;
		}
		hidEnum.LightInfrared();

		ConfigTools::conA = ConfigTools::curPath + "Camera\\Configs\\high\\MV-UB500-Group0.config";
		ConfigTools::conB = ConfigTools::curPath + "Camera\\Configs\\high\\MV-UB500-Group1.config";
		ConfigTools::conC = ConfigTools::curPath + "Camera\\Configs\\high\\MV-UB500-Group2.config";
		ConfigTools::conD = ConfigTools::curPath + "Camera\\Configs\\high\\MV-UB500-Group3.config";
		CameraReadParameterFromFile(m_hCamera, (char*)ConfigTools::conB.c_str());
		Sleep(700);
		tSdkFrameHead 	sFrameInfo;
		unsigned char* pRgbBuffer;
		char curDir[400] = { 0 };
		GetModuleFileName(GetSelfModuleHandle(), curDir, 100);
		(_tcsrchr(curDir, _T('\\')))[1] = 0; // 删除文件名，只获得路径字串
		//cout << "Dll路径:" << curDir << endl;
		string referImg = curDir;
		string referImg1 = referImg + "USB_Refer\\IR_Refer";
		string referImg2 = referImg + "USB_Refer\\Right_Refer";
		string referImg3 = referImg + "USB_Refer\\Left_Refer";
		char* file_path1 = new char[referImg1.length() + 1];
		strcpy(file_path1, referImg1.c_str());
		char* file_path2 = new char[referImg2.length() + 1];
		strcpy(file_path2, referImg2.c_str());
		char* file_path3 = new char[referImg3.length() + 1];
		strcpy(file_path3, referImg3.c_str());
		//char *file_path3 = ".\\USB_Refer\\Right_Refer";//打开左边白光使用右边图像
		//char *file_path4 = ".\\USB_Refer\\Left_Refer";
		//采集红外
		pRgbBuffer = CameraGetImageBufferPriorityEx(m_hCamera, &sFrameInfo.iWidth, &sFrameInfo.iHeight, 1000, CAMERA_GET_IMAGE_PRIORITY_NEWEST);
		sFrameInfo.uiMediaType = (m_cap.sIspCapacity.bMonoSensor ? CAMERA_MEDIA_TYPE_MONO8 : CAMERA_MEDIA_TYPE_BGR8);
		sFrameInfo.uBytes = sFrameInfo.iWidth * sFrameInfo.iHeight * CAMERA_MEDIA_TYPE_PIXEL_SIZE(sFrameInfo.uiMediaType) / 8;
		CameraSaveImage(m_hCamera, file_path1, pRgbBuffer, &sFrameInfo, m_cap.sIspCapacity.bMonoSensor ? FILE_BMP_8BIT : FILE_BMP, 100);
		LOG(INFO) << "generateRefer()- 采集红外结束";
		hidEnum.LightWhiteLeft();//打开白光左
		GenerateIrBin();//生成红外参数
		LOG(INFO) << "generateRefer()- 生成红外系数";
		//CameraLoadParameter(m_hCamera, 0);
		CameraReadParameterFromFile(m_hCamera, (char*)ConfigTools::conA.c_str());
		//采集白光（左）
		pRgbBuffer = CameraGetImageBufferPriorityEx(m_hCamera, &sFrameInfo.iWidth, &sFrameInfo.iHeight, 1000, CAMERA_GET_IMAGE_PRIORITY_NEWEST);
		hidEnum.LightWhiteRight();//打开白光右
		CameraSaveImage(m_hCamera, file_path2, pRgbBuffer, &sFrameInfo, m_cap.sIspCapacity.bMonoSensor ? FILE_BMP_8BIT : FILE_BMP, 100);
		//CameraLoadParameter(m_hCamera, 3);//第四组参数
		CameraReadParameterFromFile(m_hCamera, (char*)ConfigTools::conD.c_str());
		LOG(INFO) << "generateRefer()- 采集白光(左)结束";
		Sleep(600);
		//采集白光（右）
		pRgbBuffer = CameraGetImageBufferPriorityEx(m_hCamera, &sFrameInfo.iWidth, &sFrameInfo.iHeight, 1000, CAMERA_GET_IMAGE_PRIORITY_NEWEST);
		CameraSaveImage(m_hCamera, file_path3, pRgbBuffer, &sFrameInfo, m_cap.sIspCapacity.bMonoSensor ? FILE_BMP_8BIT : FILE_BMP, 100);
		LOG(INFO) << "generateRefer()- 采集白光(右)结束";
		GenerateBin();//生成白光参数
		LOG(INFO) << "generateRefer()- 生成白光系数";
		hidEnum.LightInfrared();//打开红外
		//CameraUnInit(m_hCamera);
		delete[] file_path1;
		delete[] file_path2;
		delete[] file_path3;
		string referPath = ConfigTools::curPath + "USB_Refer\\";
		if (!ConfigTools::highResolution)
		{
			ConfigTools::conA = ConfigTools::curPath + "Camera\\Configs\\MV-UB500-Group0.config";
			ConfigTools::conB = ConfigTools::curPath + "Camera\\Configs\\MV-UB500-Group1.config";
			ConfigTools::conC = ConfigTools::curPath + "Camera\\Configs\\MV-UB500-Group2.config";
			ConfigTools::conD = ConfigTools::curPath + "Camera\\Configs\\MV-UB500-Group3.config";
		}
		ConfigTools::getOffset();
		CameraReadParameterFromFile(m_hCamera, (char*)ConfigTools::conB.c_str());
		LOG(INFO) << "GenerateRefer end";
		//hidEnum.Close();
		return 0;
	}
	else
	{
		std::cout << "Init connection failed！ " << std::endl;
		LOG(INFO) << "GenerateRefer start";
		return 1;
	}
}

Mat cropHead(Mat white, CARD_TYPE type) {
	LOG(INFO) << "cropHead start";
	if (type == PASSPORT) {//裁剪护照
		Rect rect(floor(white.cols * 0.039), floor(white.rows * 0.233), floor(white.cols * 0.244), floor(white.rows * 0.45));
		return white(rect);
	}
	//else if (ocrResult == 20) {//裁剪旧版回乡证头像
	// Rect rect(floor(viF.cols * 0.04), floor(viF.rows * 0.19), floor(viF.cols * 0.31), floor(viF.rows * 0.6));
	// Mat viF_roi = viF(rect);
	// imwrite(image_Path, viF_roi);
	//}
	else if (type == HKMO_EXIT_ENTRY_PERMIT || type == TW_EXIT_ENTRY_PERMIT) {
		Rect rect(floor(white.cols * 0.04), floor(white.rows * 0.22), floor(white.cols * 0.27), floor(white.rows * 0.6));
		return white(rect);
	}
	return Mat();
}

//返回头像路径，如果有芯片信息则返回芯片头像路径，如果是没有芯片的护照则返回从证件表面截取的头像
const char* GetFace()
{
	LOG(INFO) << "GetFace() run";
	static char* imgpath;
	string path;
	if (GetChipResult() == 1)
		path = GetChipImage();
	else if (ConfigTools::savewhite && !whiteImage.empty()) {
		Mat face = cropHead(whiteImage, insertType);
		if (!face.empty())
		{
			path = ConfigTools::facePath;
			imwrite(path, face);
		}
	}
	if (path.empty())
	{
		return NULL;
	}
	if (imgpath != NULL)
	{
		delete[] imgpath;
	}
	imgpath = new char[path.size() + 1];
	strcpy(imgpath, path.c_str());
	LOG(INFO) << "GetFace() end";
	return imgpath;
}

//如果是没有芯片的护照则返回从证件表面截取的头像
//const char* GetFaceImage()
//{
//	static char imgPath[400];
//	LOG(INFO) << "GetFaceImage() start";
//	if (insertType != PASSPORT)
//	{
//		LOG(INFO) << "GetFaceImage()-insertType != PASSPORT";
//		LOG(INFO) << "GetFaceImage()-end";
//		return NULL;
//	}
//	if (getImgFlag != 0)
//	{
//		LOG(INFO) << "GetFaceImage()-getImgFlag != 0";
//		LOG(INFO) << "GetFaceImage()-end";
//		return NULL;
//	}
//	if (ocrFlag != 1)
//	{
//		LOG(INFO) << "GetFaceImage()-ocrFlag != 1";
//		LOG(INFO) << "GetFaceImage()-end";
//		return NULL;
//	}
//	//typedef void(*lpFace)(char *);//截取人脸
//	string tPath = ConfigTools::curPath;
//	char wPath[MAX_PATH] = { 0 };
//	char faPath[MAX_PATH] = { 0 };
//	string wT = ConfigTools::whitePath;
//	string fB = tPath + "scanner_face\\face_big.bmp";
//	strcpy(wPath, wT.c_str());
//	strcpy(faPath, fB.c_str());
//
//	HINSTANCE hDllFace; //DLL句柄 
//	lpFace faceFun; //函数指针
//	hDllFace = LoadLibrary("FaceDetection.dll");
//	if (hDllFace != NULL)
//	{
//		LOG(INFO) << "GetFaceImage()-FaceDetection start";
//		faceFun = (lpFace)GetProcAddress(hDllFace, "pyScannerFace");
//		try
//		{
//			faceFun(wPath);
//			//cout << "人脸截取成功!" << endl;
//		}
//		catch (...)
//		{
//			LOG(ERROR) << "GetFaceImage()-failed!";
//			LOG(INFO) << "GetFaceImage()-end";
//			return NULL;
//		}
//		LOG(INFO) << "GetFaceImage()-FaceDetection end";
//	}
//	else
//	{
//		LOG(ERROR) << "GetFaceImage()-face dll load failed";
//		LOG(INFO) << "GetFaceImage()-end";
//		return NULL;
//	}
//	FreeLibrary(hDllFace);
//
//	fstream image_file;
//	string path = faPath;
//
//	image_file.open(path, ios::in);
//	if (!image_file)
//	{
//		image_file.close();
//		LOG(WARNING) << "GetFaceImage()-face_big.bmp not found";
//		LOG(INFO) << "GetFaceImage()-end";
//		return NULL;
//	}
//	else
//	{
//		image_file.close();
//		Mat faceBMP = imread(path);
//		imwrite(tPath + "scanner_face\\face_big.jpg", faceBMP);
//		char exePath[MAX_PATH] = { 0 };
//		GetModuleFileName(GetSelfModuleHandle(), exePath, MAX_PATH);
//		(_tcsrchr(exePath, _T('\\')))[1] = 0; // 删除文件名，只获得路径字符串
//		string img = "scanner_face\\face_big.jpg";
//		string finalPath = exePath + img;
//		strcpy(imgPath, finalPath.c_str());
//		LOG(INFO) << "GetFaceImage()-end";
//		return imgPath;
//	}
//}

//返回芯片头像路径，根据证件是否为身份证有两种不同的路径
string GetChipImage()
{
	static string imgPath;
	LOG(INFO) << "GetChipImage()-start";
	if (GetChipResult() != 1)
	{
		LOG(WARNING) << "GetChipImage()-GetChipResult() != 1";
		LOG(INFO) << "GetChipImage()-end";
		return "";
	}
	string tPath = ConfigTools::curPath;
	fstream image_file;
	string path;
	path = tPath + "USB_TEMP\\DG2.bmp";
	image_file.open(path, ios::in);
	if (!image_file)
	{
		image_file.close();
		path = tPath + "USB_TEMP\\id.bmp";
		image_file.open(path, ios::in);
		if (!image_file) {
			LOG(WARNING) << "GetChipImage()-" << path << " not found";
			LOG(INFO) << "GetChipImage()-end";
			return "";
		}
	}


	image_file.close();

	imgPath = ConfigTools::facePath;

	Mat BMP = imread(path);
	imwrite(imgPath, BMP);
	LOG(INFO) << "GetChipImage()-end";
	return imgPath;

}


void existAndDelete() {
	LOG(INFO) << "existAndDelete() delete file";
	string s = ConfigTools::curPath;
	s = s + "mrz.txt";
	ifstream mrzTxt;
	mrzTxt.open(s, ios::in);
	if (mrzTxt.is_open())
	{
		mrzTxt.close();
		//删除
		remove(s.c_str());
	}
	//删除usbtemp文件夹下所有文件
	string s1 = ConfigTools::curPath;
	s1 = s1 + "USB_TEMP";
	RemoveDir(s1.c_str());
}

// 删除指定文件夹目录中全部文件(包含文件夹)
bool RemoveDir(const char* szFileDir)
{
	std::string strDir = szFileDir;
	if (strDir.at(strDir.length() - 1) != '\\')
		strDir += '\\';
	WIN32_FIND_DATA wfd;
	HANDLE hFind = FindFirstFile((strDir + "*.*").c_str(), &wfd);
	if (hFind == INVALID_HANDLE_VALUE)
		return false;
	do
	{
		if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if (strcmp(wfd.cFileName, ".") != 0 &&
				strcmp(wfd.cFileName, "..") != 0)
				RemoveDir((strDir + wfd.cFileName).c_str());
		}
		else
		{
			DeleteFile((strDir + wfd.cFileName).c_str());
		}
	} while (FindNextFile(hFind, &wfd));
	FindClose(hFind);
	return true;
}
void DeviceChange(int vid, int pid, DeviceChangeType changeType) {
	LOG(INFO) << "DeviceChange " << ((changeType == DeviceAdd) ? "Add" : "Remove") << " VID:" << USB_VID << " PID:" << USB_PID;
	if (vid == USB_VID && pid == USB_PID)
	{
		LOG(INFO) << "DeviceChange OUR DEVICE";
		if (changeType == DeviceAdd)
		{
			Sleep(500);
			LOG(INFO) << "DeviceChange DeviceAdd";
			OpenCamera();
		}
		else if (changeType = DeviceRemove)
		{
			LOG(INFO) << "DeviceChange DeviceRemove";
			CloseCamera();
		}
	}
}
//检测相机连接是否正常
int isConnect()
{
	int iCameraNums;
	CameraSdkStatus status;
	iCameraNums = CameraEnumerateDeviceEx();//枚举设备，获得设备列表
	if (iCameraNums <= 0)
		return 1;//MessageBox("No camera was found!");
	return 0;
}
char* getSoftwareVersion()
{
	static char softwareVersion[10] = "1.0.5";
	return softwareVersion;
}
char* getFirmwareVersion()
{
	static char FirmwareVersion[100];
	if (ConfigTools::firmWareVersion.length() > 80 || ConfigTools::firmWareVersion.length() < 5)
		ConfigTools::firmWareVersion = " ";
	memcpy(FirmwareVersion, ConfigTools::firmWareVersion.c_str(), ConfigTools::firmWareVersion.length() + 1);
	LOG(INFO) << "getFirmwareVersion()-FirmwareVersion:" << FirmwareVersion;
	return FirmwareVersion;
}
char* getBoardVersion()
{
	static char BoardVersion[20];
	if (ConfigTools::boardVersion.length() > 15 || ConfigTools::boardVersion.length() < 10)
		ConfigTools::boardVersion = " ";
	memcpy(BoardVersion, ConfigTools::boardVersion.c_str(), ConfigTools::boardVersion.length() + 1);
	LOG(INFO) << "getBoardVersion()-BoardVersion:" << BoardVersion;
	return BoardVersion;
}
int getFirmVer()
{
	LOG(INFO) << "getFirmVer() run";
	//HINSTANCE hDllGetVer; //DLL句柄 
	lpGetFirmVersion getVer; //函数指针
	//hDllGetVer = LoadLibrary("cis_passportreader.dll");
	if (hDllChip != NULL)
	{
		getVer = (lpGetFirmVersion)GetProcAddress(hDllChip, "GetFirmwareVersion");
		int len = 0;
		//int* plen = &len;
		char ver[100];
		int re = getVer(ver, &len);
		LOG(INFO) << "getFirmVer()-GetFirmwareVersion:" << re;
		ConfigTools::firmWareVersion = ver;
		if (ConfigTools::firmWareVersion.length() > 80 || ConfigTools::firmWareVersion.length() < 30)
			ConfigTools::firmWareVersion = " ";
		else
		{
			string deleteDate = ConfigTools::firmWareVersion.substr(0, 24);
			ConfigTools::firmWareVersion = deleteDate;
		}
		LOG(INFO) << "getFirmVer()-GetFirmwareVersion:" << ConfigTools::firmWareVersion;
		//FreeLibrary(hDllGetVer);
		return 0;
	}
	else
		LOG(ERROR) << "getFirmVer()-loadLibrary cis_passportreader.dll failed!";
	return -3;
}

//判断区域内是否有条形码，若有返回1，没有返回0
int getZbarResult(Mat input) {
	zbar::ImageScanner scanner;
	cv::cvtColor(input,input,cv::COLOR_RGB2GRAY);
	scanner.set_config(zbar::ZBAR_NONE, zbar::ZBAR_CFG_ENABLE, 1);
	zbar::Image zbarImage(input.cols, input.rows, "Y800", input.data, input.cols * input.rows);
	int res = scanner.scan(zbarImage);
	cout << res << endl;
	return res > 0 ? 1 : 0;
}

int Factorytest(CUSBHid& hidEnum) {
	BOOL ret;
	tstring devPaths;
	static int count = 0;
	static  int flag = 0;

	std::string str1, str2;

	if (hidEnum.FindHidDevice(USB_VID, USB_PID, devPaths))
	{
		ret = hidEnum.Open(devPaths);
		if (ret == TRUE)
		{
			std::cout << "Open success" << std::endl;
			ret = hidEnum.SystemFactorySet();
			if (ret){
				std::cout << count << ": SystemFactorySet Success" << std::endl;
			}
			else{
				std::cout << "请勿重复进行出厂设置" << std::endl;
			}
			// 产品验证
			ret = hidEnum.SystemAuthentic();
			if (ret) {
				std::cout << count << ": SystemAuthentic Success" << std::endl;
			}
			else {
				std::cout << count << ": SystemAuthentic Error" << std::endl;
				hidEnum.Close();
			}
			Sleep(500);
			ret = hidEnum.SystemGetVersion();
			if (ret) {
				std::cout << count << ": SystemGetVersion Success" << std::endl;
			}
			else {
				std::cout << count << ": SystemGetVersion Error" << std::endl;
				hidEnum.Close();
				return 0;
			}
			Sleep(500);
			ret = hidEnum.SystemGetChipID(str1, str2);
			if (ret) {
				std::cout << count << ": SystemGetChipID Success" << std::endl;
			}
			else {
				std::cout << count << ": SystemGetChipID Error" << std::endl;
				hidEnum.Close();
				return 0;
			}
			Sleep(500);
			ret = hidEnum.LightInfrared();
			if (ret) {
				std::cout << count << ": LightInfrared Success" << std::endl;
			}
			else {
				std::cout << count << ": LightInfrared Error" << std::endl;
				hidEnum.Close();
				return 0;
			}
			Sleep(500);
			ret = hidEnum.LightInfraredLeft();
			if (ret) {
				std::cout << count << ": LightInfraredLeft Success" << std::endl;
			}
			else {
				std::cout << count << ": LightInfraredLeft Error" << std::endl;
				hidEnum.Close();
				return 0;
			}
			Sleep(500);
			ret = hidEnum.LightInfraredRight();
			if (ret) {
				std::cout << count << ": LightInfraredRight Success" << std::endl;
			}
			else {
				std::cout << count << ": LightInfraredRight Error" << std::endl;
				hidEnum.Close();
				return 0;
			}

			Sleep(500);
			ret = hidEnum.LightWhiteLeft();
			if (ret) {
				std::cout << count << ": LightWhiteLeft Success" << std::endl;
			}
			else {
				std::cout << count << ": LightWhiteLeft Error" << std::endl;
				hidEnum.Close();
				return 0;
			}
			Sleep(500);
			ret = hidEnum.LightWhiteRight();
			if (ret) {
				std::cout << count << ": LightWhiteRight Success" << std::endl;
			}
			else {
				std::cout << count << ": LightWhiteRight Error" << std::endl;
				hidEnum.Close();
				return 0;
			}
			Sleep(500);
			ret = hidEnum.LightWhite();
			if (ret) {
				std::cout << count << ": LightWhite Success" << std::endl;
			}
			else {
				std::cout << count << ": LightWhite Error" << std::endl;
				hidEnum.Close();
				return 0;
			}
			Sleep(500);
			ret = hidEnum.LightPurple();
			if (ret) {
				std::cout << count << ": LightPurple Success" << std::endl;
			}
			else {
				std::cout << count << ": LightPurple Error" << std::endl;
				hidEnum.Close();
				return 0;
			}
			Sleep(500);
			ret = hidEnum.LightALLOFF();
			if (ret) {
				std::cout << count << ": LightALLOFF Success" << std::endl;
			}
			else {
				std::cout << count << ": LightALLOFF Error" << std::endl;
				hidEnum.Close();
				return 0;
			}
			Sleep(500);
			ret = hidEnum.LED_WhiteON();
			if (ret) {
				std::cout << count << ": LED_WhiteON Success" << std::endl;
			}
			else {
				std::cout << count << ": LED_WhiteON Error" << std::endl;
				hidEnum.Close();
				return 0;
			}
			Sleep(500);
			ret = hidEnum.LED_WhiteOFF();
			if (ret) {
				std::cout << count << ": LED_WhiteOFF Success" << std::endl;
			}
			else {
				std::cout << count << ": LED_WhiteOFF Error" << std::endl;
				hidEnum.Close();
				return 0;
			}
			Sleep(500);
			ret = hidEnum.LED_RedON();
			if (ret) {
				std::cout << count << ": LED_RedON Success" << std::endl;
			}
			else {
				std::cout << count << ": LED_RedON Error" << std::endl;
				hidEnum.Close();
				return 0;
			}
			Sleep(500);
			ret = hidEnum.LED_RedOFF();
			if (ret) {
				std::cout << count << ": LED_RedOFF Success" << std::endl;
			}
			else {
				std::cout << count << ": LED_RedOFF Error" << std::endl;
				hidEnum.Close();
				return 0;
			}

			Sleep(500);
			ret = hidEnum.LED_BlueON();
			if (ret) {
				std::cout << count << ": LED_BlueON Success" << std::endl;
			}
			else {
				std::cout << count << ": LED_BlueON Error" << std::endl;
				hidEnum.Close();
				return 0;
			}
			Sleep(500);
			ret = hidEnum.LED_BlueOFF();
			if (ret) {
				std::cout << count << ": LED_BlueOFF Success" << std::endl;
			}
			else {
				std::cout << count << ": LED_BlueOFF Error" << std::endl;
				hidEnum.Close();
				return 0;
			}
			Sleep(500);
			ret = hidEnum.LED_GreenON();
			if (ret) {
				std::cout << count << ": LED_GreenON Success" << std::endl;
			}
			else {
				std::cout << count << ": LED_GreenON Error" << std::endl;
				hidEnum.Close();
				return 0;
			}
			Sleep(500);
			ret = hidEnum.LED_GreenOFF();
			if (ret) {
				std::cout << count << ": LED_GreenOFF Success" << std::endl;
			}
			else {
				std::cout << count << ": LED_GreenOFF Error" << std::endl;
				hidEnum.Close();
				return 0;
			}
			Sleep(500);
			ret = hidEnum.Beep_ON();
			if (ret) {
				std::cout << count << ": Beep_ON Success" << std::endl;
			}
			else {
				std::cout << count << ": Beep_ON Error" << std::endl;
				hidEnum.Close();
				return 0;
			}
			Sleep(500);
			ret = hidEnum.Beep_OFF();
			if (ret) {
				std::cout << count << ": Beep_OFF Success" << std::endl;
			}
			else {
				std::cout << count << ": Beep_OFF Error" << std::endl;
				hidEnum.Close();
				return 0;
			}
			count++;
		}
		else
		{
			std::cout << "open error" << std::endl;
		}
	}
	else
	{
		std::cout << "Find Device Error" << std::endl;
	}
}


			//1.打开红外拍照并边缘检测处理
			//hidEnum.LightInfrared();
			//DirectShow_UpdateSetting(-6, 1, 1, 0, 0, 2, 100, 4600);
			//SYSTEMTIME tv1;
			//GetLocalTime(&tv1);
		


void testfornew(CUSBHid& hidEnum) {
	int getResult = 0;
	BOOL ret;
	tstring devPaths;
	if (hidEnum.FindHidDevice(USB_VID, USB_PID, devPaths)) {
		ret = hidEnum.Open(devPaths);
		if (ret) {
			CameraIndex = getCameraIndex("vid_1bcf&pid_28c4");
			DirectShow_Initialize(CameraIndex);//打开相机	
	

			ret = hidEnum.LightInfrared();
			if (ret) {
				DirectShow_UpdateSetting(-6, -1, 1, 0, 0, 2, 100, 4600);
				Sleep(100);
				DirectShow_TakePhoto(300, 2);
				Mat irSrc = imgfornew;
				Size outputSize;
				Mat trans = edgeDetectionAny(insertType, outputSize, irSrc);
				if (trans.empty()) {
					LOG(WARNING) << "GetData()-Can not find certificate edge! ";
				}
				LOG(INFO) << "GetData()-after edgeDetectionAny()-insertType:" << insertType;
				Mat finalIr = saveFinalImage(trans, irSrc, outputSize, 0, insertType);//保存红外图并返回Mat
				//string filePath = "D:\\getData\\Release32\\USB_TEMP\\2_ir.bmp";
				//imwrite(filePath, imgfornew);
			}
			
			/*hidEnum.Close();*/
	
			//double scale = 1024 / 3840;
			//resize(imgfornew, imgfornew, Size(0, 0), scale, scale);
			//Mat irSrc = imgfornew;
			//Size outputSize;
			////边缘检测并初步返回拍照类型
			//Mat trans = edgeDetectionAny(insertType, outputSize, irSrc);
			//if (trans.empty()) {
			//	LOG(WARNING) << "GetData()-Can not find certificate edge! ";
			//	//getResult = 5;
			//	//return getResult;
			//}
			//LOG(INFO) << "GetData()-after edgeDetectionAny()-insertType:" << insertType;
			//Mat finalIr = saveFinalImage(trans, irSrc, outputSize, 0, insertType);//保存红外图并返回Mat
			//2.拍白光和紫光图
			ret = hidEnum.LightWhite();
			if (ret) {
				DirectShow_UpdateSetting(-5, -1, 1, 0, 64, 2, 100, 4600);
				Sleep(100);
				DirectShow_TakePhoto(300, 1);
			}
			
			
			ret = hidEnum.LightPurple();
			if (ret) {
				DirectShow_UpdateSetting(-2, 1, 1, 0, 64, 2, 100, 4600);
				DirectShow_TakePhoto(500, 3);
			}
			

			//3.关灯结束
			hidEnum.LightALLOFF();
			hidEnum.Close();
			//getchar();

		}
	}
}

int main() {
	std::string path;
	while (std::cin >> path)
	{

		insertType = THTEE_LINE_CARD;
		getOcr(imread(path));
		std::cout << "end" << std::endl;
	}
}

int FactoryTest() {
	Factorytest(hidEnum);
	return 1;
}
void testfornew() {
	testfornew(hidEnum);
}