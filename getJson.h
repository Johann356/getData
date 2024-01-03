#pragma once
#include "stdafx.h"
#include <iostream>
#include "CardType.h"
#include "zbar.h"
#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/imgproc/types_c.h"
#include <map>
using namespace std;
using namespace zbar;
using namespace cv;
string GBKToUTF8(const string& strGBK);
string UTF8ToGBK(const string str);
string processIDCardJson();
string processChip(CARD_TYPE insertType);
string processMRZ(CARD_TYPE insertType, string mrzCode);
string processZbar();
map<string, string> OCRRead();
string trim(string s, string macth = " ");