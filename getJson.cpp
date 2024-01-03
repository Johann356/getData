#include "getJson.h"
#include <strstream>
#include <fstream>
#include "timeUtils.h"
#include "ConfigTools.h"
#include "json/json.h"
#include "zbar.h"
#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/imgproc/types_c.h"
using namespace std;
using namespace zbar;
using namespace cv;


std::map<std::string, std::string> GENDER{
		{ "F", "F" },
		{ "M", "M" },
		{ "1", "M" },
		{ "0", "F" } };


//工具: 判断year(只有年份的后两位)的前两位是20还是19
bool isTwentiethCentury(string year)
{
	if (strncmp(year.c_str(), "30", 2) <= 0)
		return true;
	return false;
}
//
////工具: 以Regex分割字符串
void mySplit(vector<string>& Result, string& Input, const char* Regex)
{
	int pos = 0;
	int npos = 0;
	int regexlen = strlen(Regex);
	while ((npos = Input.find(Regex, pos)) != -1)
	{
		string tmp = Input.substr(pos, npos - pos);
		Result.push_back(tmp);
		pos = npos + regexlen;
	}
	Result.push_back(Input.substr(pos, Input.length() - pos));
}
//工具: 去除字符串前后指定字符
string trim(string s, string macth)
{
	if (s.empty())
	{
		return s;
	}
	s.erase(0, s.find_first_not_of(macth));
	s.erase(s.find_last_not_of(macth) + 1);
	return s;
}

std::string changeDateFormat(std::string dateStr) {//把日期变成DD/MM/YYYY格式
	return dateStr.substr(6, 2) + '/' + dateStr.substr(4, 2) + '/' + dateStr.substr(0, 4);
}
void split(const string& s, vector<string>& tokens, const string& delimiters) {
	string::size_type lastPos = s.find_first_not_of(delimiters, 0);
	string::size_type pos = s.find_first_of(delimiters, lastPos);
	while (string::npos != pos || string::npos != lastPos) {
		tokens.push_back(s.substr(lastPos, pos - lastPos));//use emplace_back after C++11
		lastPos = s.find_first_not_of(delimiters, pos);
		pos = s.find_first_of(delimiters, lastPos);
	}
}

void parseName1(string nameLine, string& firstName, string& lastName) {
	int index = nameLine.find("<<");
	if (index < 0) { return; }
	string lastnameLine = nameLine.substr(0, index);//姓
	string firstnameLine = nameLine.substr(index, string::npos);//名
	firstName = lastName = "";
	vector<string> buffers1;
	vector<string> buffers2;
	split(lastnameLine, buffers1, "<");
	split(firstnameLine, buffers2, "<");
	for (int i = 0; i < buffers2.size(); i++) {
		firstName = firstName + buffers2[i] + " ";
	}
	firstName = firstName.substr(0, firstName.length() - 1);
	for (int i = 0; i < buffers1.size(); i++) {
		lastName = lastName + buffers1[i] + " ";
	}
	lastName = lastName.substr(0, lastName.length() - 1);
}
//工具：判断year(只有年份的后两位)的前两位是20还是19
bool isTwentiethCentury1(string year) {
	if (strncmp(year.c_str(), "80", 2) < 0)
		return true;
	return false;

}

//工具: GBK转UTF8编码
string GBKToUTF8(const string& strGBK)
{
	string strOutUTF8 = "";
	WCHAR* str1;
	int n = MultiByteToWideChar(CP_ACP, 0, strGBK.c_str(), -1, NULL, 0);
	str1 = new WCHAR[n];
	MultiByteToWideChar(CP_ACP, 0, strGBK.c_str(), -1, str1, n);
	n = WideCharToMultiByte(CP_UTF8, 0, str1, -1, NULL, 0, NULL, NULL);
	char* str2 = new char[n];
	WideCharToMultiByte(CP_UTF8, 0, str1, -1, str2, n, NULL, NULL);
	strOutUTF8 = str2;
	delete[]str1;
	str1 = NULL;
	delete[]str2;
	str2 = NULL;
	return strOutUTF8;
}
string UTF8ToGBK(const string str)
{
	string result;
	WCHAR* strSrc;
	LPSTR szRes;

	//获得临时变量的大小
	int i = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
	strSrc = new WCHAR[i + 1];
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, strSrc, i);

	//获得临时变量的大小
	i = WideCharToMultiByte(CP_ACP, 0, strSrc, -1, NULL, 0, NULL, NULL);
	szRes = new CHAR[i + 1];
	WideCharToMultiByte(CP_ACP, 0, strSrc, -1, szRes, i, NULL, NULL);

	result = szRes;
	delete[]strSrc;
	delete[]szRes;

	return result;
}
string processHKMOTWHRPJson(string mrzCode) {
	string jsonStr;
	//	mrzCode = delN(mrzCode);//把OCR字符串中间的换行符号去除
	//string firstLine = mrzCode.substr(0, 30);
	//string secondLine = mrzCode.substr(31, 30);
	//string nameLine = mrzCode.substr(62, 30);//这个是第三行，我把它给分割
	vector<string> strVec;
	mySplit(strVec, mrzCode, "\n");
	if (strVec.size() < 3)
	{
		LOG(INFO) << "processHKMOTWHRPJson() MRZ lines err";
		return "";
	}
	string firstLine = strVec[0].substr(0, 30);
	string secondLine = strVec[1].substr(0, 30);
	string nameLine = strVec[2].substr(0, 30);
	if (firstLine.size() < 30 || secondLine.size() < 30 || nameLine.size() < 30)
	{
		LOG(INFO) << "processHKMOTWHRPJson() MRZ char nums err";
		return "";
	}
	string firstname = "";
	string lastname = "";
	string birthPre = "";
	string validPre = "";
	parseName1(nameLine, firstname, lastname);
	if (mrzCode.substr(0, 2).compare("C<") == 0) {
		//		if (!checkHKMOHRPOLDMRZ(mrzCode)) { return "{\"iret\":-9}"; }
		//老版回乡证
		if (secondLine.substr(27, 1) == "0") {
			birthPre = "18";
		}
		else if (secondLine.substr(27, 1) == "1") {
			birthPre = "19";
		}
		else {
			birthPre = "20";
		}
		if (secondLine.substr(28, 1) == "0") {
			validPre = "18";
		}
		else if (secondLine.substr(28, 1) == "1") {
			validPre = "19";
		}
		else {
			validPre = "20";
		}
		string expiredDate = validPre + secondLine.substr(8, 6);
		string birthDate = birthPre + secondLine.substr(0, 6);

		//if (hxcSignDa == "") {//健壮性考虑，如果ocr没识别出签发日期，则为有限期-10年
		//	int SignDate = atoi(secondLine.substr(8, 6).c_str()) - 100000;
		//	hxcSignDa = "20" + to_string(SignDate);
		//}
		Json::Value passportInfo;
		passportInfo["SerialNum"] = secondLine.substr(15, 11);
		passportInfo["BirthDate"] = birthPre + secondLine.substr(0, 6);
		passportInfo["Validity"] = "20" + secondLine.substr(8, 6);
		passportInfo["Issuing"] = "";
		passportInfo["Nationality"] = "CHN";
		passportInfo["FirstName"] = firstname;
		passportInfo["MiddleName"] = "";
		passportInfo["LastName"] = lastname;
		passportInfo["DocType"] = trim(firstLine.substr(0, 2), "<");
		passportInfo["Option1"] = "";
		passportInfo["Gender"] = GENDER[secondLine.substr(7, 1)];
		passportInfo["ChineseName"] = "";
		passportInfo["RenewalTimes"] = "";
		passportInfo["hxcSignDate"] = "";
		passportInfo["personalNo"] = "";
		Json::FastWriter writer;
		jsonStr = writer.write(passportInfo);
		return jsonStr;
	}
	else if (mrzCode.substr(0, 2).compare("CR") == 0) {
		if (secondLine.substr(18, 1) == "A") {
			birthPre = "18";
		}
		else if (secondLine.substr(18, 1) == "B") {
			birthPre = "19";
		}
		else {
			birthPre = "20";
		}
		if (isTwentiethCentury1(firstLine.substr(15, 2))) {
			validPre = "20";
		}
		else {
			validPre = "19";
		}
		string hkid = secondLine.substr(19, 9);
		if (hkid.find("<") != string::npos) {
			hkid = secondLine.substr(19, 8);
		}

		string hxcSignDate;
		string birthday = birthPre + firstLine.substr(23, 6);
		string validity = validPre + firstLine.substr(15, 6);
		string Issuing = GBKToUTF8("中华人民共和国出入境管理局");
		if (hxcSignDate.empty())
		{
			Date signDate(validity);
			signDate._year -= 10;
			signDate++;
			Date birthDate(birthday);
			birthDate._year += 18;
			if (birthDate > signDate)
			{
				signDate._year += 5;
			}
			Date changeDate(2019, 6, 1);
			if (signDate < changeDate)
			{
				Issuing = GBKToUTF8("公安部出入境管理局");
			}
			hxcSignDate = signDate.tostr();
		}
		Json::Value passportInfo;
		passportInfo["SerialNum"] = firstLine.substr(2, 9);
		passportInfo["BirthDate"] = birthday;
		passportInfo["Validity"] = validity;
		passportInfo["Issuing"] = Issuing;
		passportInfo["Nationality"] = "CHN";
		passportInfo["FirstName"] = firstname;
		passportInfo["MiddleName"] = "";
		passportInfo["LastName"] = lastname;
		passportInfo["DocType"] = trim(firstLine.substr(0, 2), "<");
		passportInfo["Option1"] = "";
		passportInfo["Gender"] = GENDER[firstLine.substr(22, 1)];
		passportInfo["RenewalTimes"] = firstLine.substr(12, 2);
		passportInfo["hxcSignDate"] = hxcSignDate;
		passportInfo["ChineseName"] = "";
		passportInfo["personalNo"] = "";
		Json::FastWriter writer;
		jsonStr = writer.write(passportInfo);
		return jsonStr;
	}
	if (secondLine.substr(18, 1) == "A") {
		birthPre = "18";
	}
	else if (secondLine.substr(18, 1) == "B") {
		birthPre = "19";
	}
	else {
		birthPre = "20";
	}
	if (isTwentiethCentury1(firstLine.substr(15, 2))) {
		validPre = "20";
	}
	else {
		validPre = "19";
	}
	string hkid = secondLine.substr(19, 9);
	if (hkid.find("<") != string::npos) {
		hkid = secondLine.substr(19, 8);
	}
	nameLine = nameLine.substr(0, nameLine.size() - 4);
	parseName1(nameLine, firstname, lastname);
	string hxcSignDate;
	string birthday = birthPre + firstLine.substr(23, 6);
	string validity = validPre + firstLine.substr(15, 6);
	string Issuing = GBKToUTF8("中华人民共和国出入境管理局");
	if (hxcSignDate.empty())
	{

		Date signDate(validity);
		signDate._year -= 5;
		signDate++;
		Date changeDate(2019, 4, 1);
		if (signDate < changeDate)
		{
			Issuing = GBKToUTF8("公安部出入境管理局");
		}
		hxcSignDate = signDate.tostr();

	}
	Json::Value passportInfo;
	passportInfo["SerialNum"] = firstLine.substr(2, 8);
	passportInfo["BirthDate"] = birthday;
	passportInfo["Validity"] = validity;
	passportInfo["Issuing"] = Issuing;
	passportInfo["Nationality"] = "CHN";
	passportInfo["FirstName"] = firstname;
	passportInfo["MiddleName"] = "";
	passportInfo["LastName"] = lastname;
	passportInfo["DocType"] = trim(firstLine.substr(0, 2), "<");
	passportInfo["Option1"] = "";
	passportInfo["Gender"] = GENDER[firstLine.substr(22, 1)];
	passportInfo["RenewalTimes"] = firstLine.substr(12, 2);
	passportInfo["hxcSignDate"] = hxcSignDate;
	passportInfo["ChineseName"] = "";
	passportInfo["personalNo"] = "";
	Json::FastWriter writer;
	jsonStr = writer.write(passportInfo);
	return jsonStr;
}
string processIDCardJson() {
	//证件类型为身份证
	LOG(INFO) << "processIDCardJson()-run";
	string filename = "USB_TEMP\\IDInfo.txt";
	ifstream text;
	text.open(ConfigTools::curPath + filename, ios::in);

	if (!text.is_open())
	{
		LOG(WARNING) << "GetJson()-id_info text failed,path:" << ConfigTools::curPath + filename;
		return "";
	}
	vector<string> strVec;
	string inbuf;
	while (getline(text, inbuf)) {
		strVec.push_back(trim(inbuf));//每一行都保存到vector中
	}
	text.close();

	if (strVec.size() < 9)
	{
		LOG(WARNING) << "processIDCardJson()-id_info length < 9!";
		return "";
	}

	LOG(INFO) << "processIDCardJson() name:\"" << UTF8ToGBK(strVec[0]) << "\"";
	LOG(INFO) << "processIDCardJson() birthday:\"" << strVec[3] << "\"";
	if (isdigit(strVec[8][0]) == FALSE)
	{
		string IDDATA = "USB_TEMP\\IDDATA.txt";
		ifstream IdDataTxt;
		IdDataTxt.open(ConfigTools::curPath + IDDATA, ios::in);

		if (!IdDataTxt.is_open())
		{
			LOG(WARNING) << "GetJson()-IDDATA text failed,path:" << ConfigTools::curPath + IDDATA;
		}
		else
		{
			std::stringstream IdDataTxtBuffer;
			IdDataTxtBuffer << IdDataTxt.rdbuf();
			std::string idContents(IdDataTxtBuffer.str());
			LOG(WARNING) << "GetJson() Validity_Termi-IDDATA:" << idContents;
			IdDataTxt.close();
		}
		string cqGBK = "长期";
		string cqUTF8 = GBKToUTF8(cqGBK);
		strVec[8] = cqUTF8;
		LOG(WARNING) << "GetJson() Validity_Termi_Permanent:\"" << strVec[8] << "\"";
	}
	Json::Value passportInfo;
	if (strVec[strVec.size() - 1] == "0" || strVec[strVec.size() - 1] == "1") {
		passportInfo["Name"] = strVec[0].c_str();
		passportInfo["Gender"] = strVec[1].c_str();
		passportInfo["Ethnicity"] = strVec[2].c_str();
		passportInfo["Birthdate"] = strVec[3].c_str();
		passportInfo["Address"] = strVec[4].c_str();
		passportInfo["IDnumber"] = strVec[5].c_str();
		passportInfo["Authority"] = strVec[6].c_str();
		passportInfo["Validity_Start"] = strVec[7].c_str();
		passportInfo["Validity_Termi"] = strVec[8].c_str();
		if (strVec.size() > 10 && strVec[strVec.size() - 1] == "1") {//港澳台身份证
			//passportInfo["hxcIDHKMNu"] = trim(strVec[10]);
			//passportInfo["RenewalTimes"] = trim(strVec[11]);
			
			passportInfo["PassNumber"] = trim(strVec[9]);
			passportInfo["IssueNumber"] = trim(strVec[10]);
		}
	}
	else if (strVec[strVec.size() - 1] == "2") {//2017外国人永居证
		string blank = "";
		passportInfo["EnglishName"] = strVec[0].c_str();
		passportInfo["Gender"] = strVec[1].c_str();
		passportInfo["Nationality"] = strVec[2].c_str();
		passportInfo["Birthdate"] = strVec[3].c_str();
		
		passportInfo["PRCnumber"] = strVec[5].c_str();
		passportInfo["Authority"] = strVec[6].c_str();
		passportInfo["Validity_Start"] = strVec[7].c_str();
		passportInfo["Validity_Termi"] = strVec[8].c_str();
		passportInfo["ChineseName"] = strVec[9].c_str();
		passportInfo["DVnumber"] = strVec[10].c_str();
		passportInfo["Card_Type"] = strVec[11].c_str();
		passportInfo["IssueNumber"] = strVec[12].c_str();
		passportInfo["PRCnumber_Related"] = strVec[13].c_str();
		passportInfo["EngName1"] = strVec[14].c_str();
		passportInfo["EngName2"] = blank.c_str();

	}
	else if (strVec[strVec.size() - 1] == "3") {//2023外国人永居证
		string blank = "";
		passportInfo["EnglishName"] = strVec[0].c_str();
		passportInfo["Gender"] = strVec[1].c_str();
		passportInfo["Nationality"] = strVec[2].c_str();
		passportInfo["Birthdate"] = strVec[3].c_str();

		passportInfo["PRCnumber"] = strVec[5].c_str();
		passportInfo["Authority"] = blank.c_str();
		passportInfo["Validity_Start"] = strVec[7].c_str();
		passportInfo["Validity_Termi"] = strVec[8].c_str();
		passportInfo["ChineseName"] = strVec[9].c_str();
		passportInfo["DVnumber"] = blank.c_str();
		passportInfo["Card_Type"] = strVec[11].c_str();
		passportInfo["IssueNumber"] = strVec[12].c_str();
		passportInfo["PRCnumber_Related"] = strVec[13].c_str();
		passportInfo["EngName1"] = strVec[14].c_str();
		passportInfo["EngName2"] = strVec[15].c_str();
	}


	Json::FastWriter writer;
	string json = writer.write(passportInfo);
	LOG(INFO) << "processIDCardJson()-end";
	return json;
}

string processChip(CARD_TYPE insertType) {
	//证件类型为带有电子芯片的护照或各类通行证，并读取成功了
	LOG(INFO) << "GetJson()-chipFlag == 1";
	string chipmrztxt = "USB_TEMP\\ChipMRZ.txt";
	string dg11txt = "USB_TEMP\\DG11.txt";
	ifstream chipmrztext, dg11text;
	chipmrztext.open(ConfigTools::curPath + chipmrztxt, ios::in);
	if (!chipmrztext.is_open())
	{
		LOG(WARNING) << "GetJson()-ChipMRZ text failed,path:" << ConfigTools::curPath + chipmrztxt;
		return "";
	}
	vector<string> strVec;
	strstream json;

	while (!chipmrztext.eof())  //行0 - 行lines对应strvect[0] - strvect[lines]
	{
		string inbuf;
		getline(chipmrztext, inbuf, '\n');
		strVec.push_back(inbuf);
	}
	if (strVec.size() < 11)
	{
		LOG(WARNING) << "GetJson()-ChipMRZ length < 11!";
		return "";
	}
	for (int i = 12; i < strVec.size(); i++)
	{
		strVec[11] = strVec[11] + strVec[i];
	}
	strVec[11].erase(remove(strVec[11].begin(), strVec[11].end(), '\n'), strVec[11].end());
	strVec[11].erase(remove(strVec[11].begin(), strVec[11].end(), '\r'), strVec[11].end());
	string nameCH;
	dg11text.open(ConfigTools::curPath + dg11txt, ios::in);
	if (!dg11text.is_open())
	{
		LOG(WARNING) << "GetJson()-DG11 text failed,path:" << ConfigTools::curPath + dg11txt;
	}
	else
	{
		getline(dg11text, nameCH, '\n');
	}

	string hxcSignDate;
	string birthPre;
	if (insertType == HKMO_HOME_RETURN_PERMIT || insertType == TW_HOME_RETURN_PERMIT) {

		if (strVec[11].substr(48, 1) == "A") {
			birthPre = "18";
		}
		else if (strVec[11].substr(48, 1) == "B") {
			birthPre = "19";
		}
		else {
			birthPre = "20";
		}
	}
	else {
		if (isTwentiethCentury(strVec[1]) == true)
			birthPre = "20";
		else
			birthPre = "19";
	}

	string birthday = birthPre + strVec[1];
	string validity, validityPre;
	if (isTwentiethCentury1(strVec[2]))
	{
		validityPre = "20";
	}
	else
	{
		validityPre = "19";
	}
	validity = validityPre + strVec[2];
	string Issuing = GBKToUTF8("中华人民共和国出入境管理局");
	if (hxcSignDate.empty())
	{
		if (insertType == TW_HOME_RETURN_PERMIT)//5年
		{
			Date signDate(validity);
			signDate._year -= 5;
			signDate++;
			Date changeDate(2019, 4, 1);
			if (signDate < changeDate)
			{
				Issuing = GBKToUTF8("公安部出入境管理局");
			}
			hxcSignDate = signDate.tostr();
		}
		else if (insertType == HKMO_HOME_RETURN_PERMIT)
		{
			Date signDate(validity);
			signDate._year -= 10;
			signDate++;
			Date birthDate(birthday);
			birthDate._year += 18;
			if (birthDate > signDate)
			{
				signDate._year += 5;
			}
			Date changeDate(2019, 6, 1);
			if (signDate < changeDate)
			{
				Issuing = GBKToUTF8("公安部出入境管理局");
			}
			hxcSignDate = signDate.tostr();
		}
		else if (insertType == HKMO_EXIT_ENTRY_PERMIT || insertType == TW_EXIT_ENTRY_PERMIT) {
			Date signDate(validity);
			signDate._year -= 10;
			signDate++;
			Date birthDate(birthday);
			birthDate._year += 16;
			if (birthDate > signDate)
			{
				signDate._year += 5;
			}
			Date changeDate(2019, 6, 1);
			if (signDate < changeDate)
			{
				Issuing = GBKToUTF8("公安部出入境管理局");
			}
			hxcSignDate = signDate.tostr();
		}
	}
	string firstname, lastname, renewalTimes;
	if (insertType == PASSPORT)
	{
		Issuing = strVec[3];
		parseName1(strVec[11].substr(5, 39), firstname, lastname);
	}
	else if (insertType == TW_HOME_RETURN_PERMIT)
	{
		parseName1(strVec[11].substr(60, 26), firstname, lastname);
	}
	else {
		firstname = strVec[5];
		lastname = strVec[7];
	}
	if (insertType == HKMO_HOME_RETURN_PERMIT || insertType == TW_HOME_RETURN_PERMIT)
	{
		renewalTimes = strVec[11].substr(12, 2);
	}
	string personalNo;
	if (insertType == PASSPORT)
	{
		personalNo = trim(strVec[11].substr(44 + 28, 14), "<");
		replace(personalNo.begin(), personalNo.end(), '<', '-');
	}
	string serialNum = trim(strVec[0], "<");
	Json::Value passportInfo;
	passportInfo["SerialNum"] = serialNum;
	passportInfo["BirthDate"] = birthday;
	passportInfo["Validity"] = validity;
	passportInfo["Issuing"] = trim(Issuing, "<");
	if (passportInfo["Issuing"] == "D") passportInfo["Issuing"] = "DEU";
	passportInfo["Nationality"] = trim(strVec[4], "<");
	if (passportInfo["Nationality"] == "D") passportInfo["Nationality"] = "DEU";
	passportInfo["FirstName"] = firstname;
	passportInfo["MiddleName"] = "";
	passportInfo["LastName"] = lastname;
	passportInfo["DocType"] = trim(strVec[11].substr(0, 2), "<");
	passportInfo["Option1"] = strVec[9];
	passportInfo["Gender"] = strVec[10];
	passportInfo["RenewalTimes"] = renewalTimes;
	passportInfo["personalNo"] = personalNo;
	passportInfo["hxcSignDate"] = hxcSignDate;//签发日期
	passportInfo["ChineseName"] = nameCH;
	Json::FastWriter writer;
	string jsonStr = writer.write(passportInfo);
	chipmrztext.close();
	dg11text.close();
	LOG(INFO) << "GetJson()-----------------------------firstName:" << strVec[5] << "; lastName:" << strVec[7];
	return jsonStr;
}

string processMRZ(CARD_TYPE insertType, string mrzCode) {
	LOG(INFO) << "GetJson()-chipFlag != 1";
	if (insertType == PASSPORT)
	{
		//证件类型为没有电子芯片的护照
		LOG(INFO) << "GetJson()-chipFlag != 1 && insertType == PASSPORT";

		std::string contents = mrzCode;
		vector<string> strVec;
		mySplit(strVec, contents, "\n");
		if (strVec[0].length() < 40 || strVec[1].length() < 40)
		{
			LOG(WARNING) << "GetJson()-The length of a single row is less than 40" << endl;
			return "";
		}
		//以下是根据机读码的国际标准规范来解析出各项信息（MiddleName尚未加进去，暂时都设为空）
		vector<string> name;
		//vector<string> name2;
		//mySplit(name, strVec[0].substr(5, 10), "<");
		//String lastName = name[0];
		//String nameT = strVec[0].substr(7 + lastName.length(), 17 + lastName.length());
		//mySplit(name2, nameT, "<");
		//String firstName = name2[0];


		string firstname, lastname;

		parseName1(strVec[0].substr(5), firstname, lastname);

		string birthday;
		string validity;
		//全幅识别签发日期
		string hxcSignDate;
		string nameCH;
		if (isTwentiethCentury(strVec[1].substr(13, 2)))
			birthday = "20" + strVec[1].substr(13, 6);
		else
			birthday = "19" + strVec[1].substr(13, 6);

		if (isTwentiethCentury1(strVec[1].substr(21, 2)))
			validity = "20" + strVec[1].substr(21, 6);
		else
			validity = "19" + strVec[1].substr(21, 6);

		string serialNum = trim(strVec[1].substr(0, 9), "<");
		string personalNo = trim(strVec[1].substr(28, 14), "<");
		replace(personalNo.begin(), personalNo.end(), '<', '-');
		Json::Value passportInfo;
		passportInfo["SerialNum"] = serialNum;
		passportInfo["BirthDate"] = birthday;
		passportInfo["Validity"] = validity;
		passportInfo["Issuing"] = trim(strVec[0].substr(2, 3), "<");
		if (passportInfo["Issuing"] == "D") passportInfo["Issuing"] = "DEU";
		passportInfo["Nationality"] = trim(strVec[1].substr(10, 3), "<");
		if (passportInfo["Nationality"] == "D") passportInfo["Nationality"] = "DEU";
		passportInfo["FirstName"] = firstname;
		passportInfo["MiddleName"] = "";
		passportInfo["LastName"] = lastname;
		passportInfo["DocType"] = trim(strVec[0].substr(0, 2), "<");
		passportInfo["Option1"] = "";
		passportInfo["Gender"] = strVec[1].substr(20, 1);
		passportInfo["personalNo"] = personalNo;
		passportInfo["RenewalTimes"] = "";
		passportInfo["hxcSignDate"] = hxcSignDate;
		passportInfo["ChineseName"] = nameCH;
		Json::FastWriter writer;
		string jsonStr = writer.write(passportInfo);
		return jsonStr;
	}
	else if (insertType == TW_HOME_RETURN_PERMIT || insertType == HKMO_HOME_RETURN_PERMIT) {
		return processHKMOTWHRPJson(mrzCode);
	}
	else if (insertType == HKMOTW_IDCARD) {
		std::string contents = mrzCode;
		vector<string> strVec;
		mySplit(strVec, contents, "\n");
		if (strVec.size() < 3)
		{
			LOG(INFO) << "processMRZ() MRZ lines err";
			return "";
		}
		string firstLine = strVec[0].substr(0, 30);
		string secondLine = strVec[1].substr(0, 30);
		string nameLine = strVec[2].substr(0, 30);
		if (firstLine.size() < 30 || secondLine.size() < 30 || nameLine.size() < 30)
		{
			LOG(INFO) << "processMRZ() MRZ char nums err";
			return "";
		}
		string firstname = "";
		string lastname = "";
		string birthPre = "";
		string validPre = "";
		parseName1(nameLine, firstname, lastname);
		string birthDate = "";
		if (isTwentiethCentury(secondLine.substr(0, 6)) == true)
			birthDate = "20" + secondLine.substr(0, 6);
		else
			birthDate = "19" + secondLine.substr(0, 6);
		Json::Value passportInfo;
		passportInfo["SerialNum"] = firstLine.substr(5, 8);
		passportInfo["BirthDate"] = birthDate;
		passportInfo["Validity"] = "";
		passportInfo["Issuing"] = "";
		passportInfo["Nationality"] = "CHN";
		passportInfo["FirstName"] = firstname;
		passportInfo["MiddleName"] = "";
		passportInfo["LastName"] = lastname;
		passportInfo["DocType"] = trim(firstLine.substr(0, 2), "<");
		passportInfo["Option1"] = "";
		passportInfo["Gender"] = GENDER[secondLine.substr(7, 1)];
		passportInfo["RenewalTimes"] = "";
		passportInfo["hxcSignDate"] = "20" + secondLine.substr(8, 6);
		passportInfo["ChineseName"] = "";
		passportInfo["personalNo"] = "";
		Json::FastWriter writer;
		string jsonStr = writer.write(passportInfo);
		LOG(INFO) << "GetJson()-end" << endl << jsonStr << endl;
		return jsonStr;
	}
	else
	{
		return "";
	}
}

//二维码区域检测并返还对应的Json代码
string processZbar() {
	Mat image = imread(ConfigTools::whitePath);
	zbar::ImageScanner scanner;
	scanner.set_config(zbar::ZBAR_NONE, zbar::ZBAR_CFG_ENABLE, 1);
	cv::cvtColor(image, image, COLOR_RGB2GRAY);
	zbar::Image zbarImage(image.cols, image.rows, "Y800", image.data, image.cols * image.rows);
	scanner.scan(zbarImage);
	Json::Value zbarInfo;
	Json::FastWriter writer;
	//string jsonStr = "";
	int count = 0;
	for (zbar::Image::SymbolIterator symbol = zbarImage.symbol_begin(); symbol != zbarImage.symbol_end(); ++symbol) {
		// 获取条码类型和内容
		
		count++;
		std::string barcodeType = symbol->get_type_name();
		std::string barcodeData = symbol->get_data();
		std::stringstream keyStream1; 
		keyStream1 << "Type" << count;
		std::string key1 = keyStream1.str();
		std::stringstream keyStream2;
		keyStream2 << "Data" << count;
		std::string key2 = keyStream2.str();
		//string TypeCode = "Type: " + (char)count;
		//string DataCode = "Data: " + (char)count;
		zbarInfo[key1] = barcodeType;
		zbarInfo[key2] = barcodeData;
		
	}
	string jsonStr = writer.write(zbarInfo);
	LOG(INFO) << "GetJson()-end" << endl << jsonStr << endl;
	return jsonStr;
}

map<string, string> OCRRead() {
	map<string, string> ocrinfo;
	vector<string> keys = { "type","contryCode","primaryName","secondaryName","nationality" ,"birthDate" ,"personalNo",
		"gender" ,"birthPlace" ,"signDate","signPlace" ,"issuing","validity" };
	for (auto key : keys)
	{
		ocrinfo[key] = "";
	}
	string filename = ConfigTools::curPath + "USB_TEMP\\result.txt";
	ifstream text;
	text.open(filename.c_str(), ios::in);

	if (!text.is_open()) {
		cout << "GetJson()-cardInfo text failed,path:" << filename;
		//root["PassportInfo"] = "";//护照信息字段
		return ocrinfo;
	}
	vector<string> strVec;
	stringstream json;
	while (!text.eof())  //行0 - 行lines对应strvect[0] - strvect[lines]
	{
		string inbuf;
		getline(text, inbuf, '\n');
		vector<string> infor;
		split(inbuf, infor, ":");
		if (infor.size() > 1)
		{
			ocrinfo[infor[0]] = infor[1];
		}
	}
	text.close();

	return ocrinfo;
}
