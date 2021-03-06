#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
# include <SDKDDKVer.h>
# include <Windows.h>
# pragma warning(disable:4996) // live on the edge!
#else
#include <unistd.h>
//****FixMe, deal with these more properly!
# define GetTempPath(MAX_PATH, tpath)(tpath[0] = '\0')
# define GetTempFileName(tpath, a, b, tfile)(strcpy(tfile, "temp.tmp"))
#endif

#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <assert.h>

#include "debug.h"

// prune out unuzed miniz features
#define MINIZ_NO_STDIO					// no file I/O support
#define MINIZ_NO_TIME					// disable file time routines
#define MINIZ_NO_ZLIB_APIS				// remove zlib style calls
#define MINIZ_NO_MALLOC					// strip out malloc
#include "miniz.h"

#include "timer.h"
#include "aes.h"
#include "stream.h"
#include "serial.h"
#include "socket.h"
#include "xyzv3.h"

const char* g_ver = "v0.9.5";

//****FixMe, keep track of 'end' messages and fire them
// if we try to exit in middle of operation, like calibrating bed
//****FixMe, rewrite message loop to be state driven
// right now we assume a linear progression of events and can only
// wait or fail if things come out of order.  The real 
// protocol is more varied!

const XYZPrinterInfo XYZV3::m_infoArray[m_infoArrayLen] = { //  File parameters        Machine capabilities
	//   modelNum,       fileNum, serialNum,          webNum,    IsV5, IsZip, comV3,   tmenu, hbed,  dExtr, wifi,  scan, laser,    len, wid, hgt,   screenName
	{"dvF100B000",   "daVinciF10", "3DP01P", "davincif10_V2",   false,  true, false,    true,  true, false, false, false, false,   200, 200, 200,   "da Vinci 1.0"},
	{"dvF100A000",   "daVinciF10", "3F10AP", "dvF100A000_V2",   false,  true, false,    true,  true, false, false, false, false,   200, 200, 200,   "da Vinci 1.0A"},
	{"dvF10SA000",   "daVinciF10", "3S10AP", "dvF10SA000_V2",   false,  true, false,    true,  true, false, false,  true,  true,   200, 200, 190,   "da Vinci 1.0 AiO"}, // possibly v3?
	{"dvF110B000",   "daVinciF11", "3F11XP", "dvF110B000_V2",   false,  true,  true,    true,  true, false,  true, false, false,   200, 200, 200,   "da Vinci 1.1 Plus"}, //???? I'm suspicious of this one, think it is v1 file
	{"dvF200B000",   "daVinciF20", "3F20XP", "davincif20_V2",   false,  true, false,    true,  true,  true, false, false, false,   200, 200, 150,   "da Vinci 2.0 Duo"},
	{"dvF200A000",   "daVinciF20", "3F20AP", "dvF200A000_V2",   false,  true, false,    true,  true,  true, false, false, false,   200, 200, 150,   "da Vinci 2.0A Duo"}, // possibly v3

	{"dv1J00A000",  "daVinciJR10", "3F1J0X", "dv1J00A000_V2",   false, false,  true,    true, false, false, false, false, false,   150, 150, 150,   "da Vinci Jr. 1.0"}, // all jr's support optional 0.3mm nozzle
	{"dv1JA0A000",   "dv1JA0A000", "3F1JAP", "dv1JA0A000_V2",   false, false,  true,    true, false, false, false, false,  true,   175, 175, 175,   "da Vinci Jr. 1.0A"},
	{"dv1JW0A000", "daVinciJR10W", "3F1JWP", "dv1JW0A000_V2",   false, false,  true,    true, false, false,  true, false, false,   150, 150, 150,   "da Vinci Jr. 1.0w"},
	{"dv1JS0A000",   "dv1JSOA000", "3F1JSP", "dv1JS0A000_V2",   false, false,  true,    true, false, false,  true,  true,  true,   150, 150, 150,   "da Vinci Jr. 1.0 3in1"}, //daVinciJR10S 
	{"dv1JSOA000", "daVinciJR10S", "3F1JOP", "?????????????",    true, false,  true,    true, false, false,  true,  true,  true,   150, 150, 150,   "da Vinci Jr. 1.0 3in1 (Open filament)"}, // not sure this is right, file num is suspicioud
	{"dv2JW0A000", "daVinciJR20W", "3F2JWP", "dv2JW0A000_V2",   false, false,  true,    true, false,  true,  true, false, false,   150, 150, 150,   "da Vinci Jr. 2.0 Mix"}, // only 0.4mm nozzle

	{"dv1MX0A000",   "dv1MX0A000", "3FM1XP", "dv1MX0A000_V2",   false, false,  true,   false, false, false, false, false, false,   150, 150, 150,   "da Vinci miniMaker"},
	{"dv1MW0A000",   "dv1MW0A000", "3FM1WP", "dv1MW0A000_V2",   false, false,  true,   false, false, false,  true, false, false,   150, 150, 150,   "da Vinci mini w"},
	{"dv1MW0B000",   "dv1MW0B000", "3FM1JP", "dv1MW0B000_V2",   false, false,  true,   false, false, false,  true, false, false,   150, 150, 150,   "da Vinci mini wA"},
	{"dv1MW0C000",   "dv1MW0C000", "3FM1JP", "dv1MW0C000_V2",   false, false,  true,   false, false, false,  true, false, false,   150, 150, 150,   "da Vinci mini w+"}, // possibly 3FM3WP
	{"dv1NX0A000",   "dv1NX0A000", "3FN1XP", "dv1NX0A000_V2",   false, false,  true,   false, false, false, false, false, false,   120, 120, 120,   "da Vinci nano"}, // there are two nanos, an orange with 0.3 mm nozzl3 and white with 0.4 mm nozzle
	{"??dv1NX0A000", "dv1NW0A000", "??3FN1XP","??dv1NX0A000_V2",false, false,  true,   false, false, false,  true, false, false,   120, 120, 120,   "da Vinci nano w"},

	{"dv1JP0A000",   "dv1JP0A000", "3F1JPP", "dv1JP0A000_V2",    true, false,  true,    true, false, false, false, false, false,   150, 150, 150,   "da Vinci Jr. 1.0 Pro"},
	{"??dv1JP0A000", "dv1JPWA000", "??3F1JPP", "??dv1JP0A000_V2",true, false,  true,    true, false, false,  true, false, false,   150, 150, 150,   "da Vinci Jr. 1.0w Pro"},
	{"dvF1W0A000",  "daVinciAW10", "3F1AWP", "dvF1W0A000_V2",    true, false,  true,    true,  true, false,  true, false,  true,   200, 200, 200,   "da Vinci 1.0 Pro"},
	{"??dvF1W0A000", "dv1JA0A000", "??3F1AWP", "??dvF1W0A000_V2",true, false,  true,    true,  true, false,  true, false,  true,   200, 200, 200,   "da Vinci 1.0A Pro"},
	{"dvF1WSA000",  "daVinciAS10", "3F1ASP", "dvF1WSA000_V2",    true, false,  true,    true,  true, false,  true,  true,  true,   200, 200, 190,   "da Vinci 1.0 Pro 3in1"},
	{"dv1SW0A000",   "dv1SW0A000", "3F1SWP", "dv1SW0A000_V2",    true, false,  true,    true,  true, false,  true, false,  true,   300, 300, 300,   "da Vinci Super"}, //0.4mm/0.6mm/0.8mm nozzles

	//****Note, a fileNum of FXX may be a universal demo file marker that any printer can load
	// does not support 3w file format
//	{"dv1CP0A000",             "", "3FC1XP", "dv1CP0A000_V2",    true,  false, true,    true, false, false,  true, false, false,   200, 200, 150,   "da Vinci Color"}, // no heated bed, but full color!
//  da Vinci Super
//  da Vinci PartPro200 xTCS
};

	//  unknown
	//{"dvF110A000"}, F11A
	//{"dvF110P000"}, F11P
	//{"dvF11SA000"}, F11SA
	//{"dvF210B000"}, F21
	//{"dvF210A000"}, F21A
	//{"dvF210P000"}, F21P
	//XYZprinting WiFi Box
	//3FNAWP XYZprinting da Vinci nano w
	//3F3PMP XYZprinting da Vinci PartPro 300xT
	//3FC1SP XYZprinting da Vinci Color AiO

XYZV3::XYZV3() 
{
	debugPrint(DBG_LOG, "XYZV3::XYZV3()");

	memset(&m_status, 0, sizeof(m_status));
	memset(&pDat, 0, sizeof(pDat));
	m_stream = NULL;
	m_info = NULL;
	//m_actState = ACT_FAILURE;
	m_progress = 0;

	m_jogAxis = ' ';
	m_jogDist_mm = 0;
	m_infoIdx = -1;
	m_fileIsTemp = false;
	m_filePath[0] = '\0';
	m_fileOutPath[0] = '\0';
} 

XYZV3::~XYZV3() 
{
	debugPrint(DBG_LOG, "XYZV3::~XYZV3()");
} 

void XYZV3::setStream(Stream *s)
{
	debugPrint(DBG_LOG, "XYZV3::setStream(%d)", s);

	// close out old stream
	if(m_stream)
		m_stream->closeStream();
	// and put new in its place
	m_stream = s;

	memset(&m_status, 0, sizeof(m_status));

	if(m_stream)
		m_stream->clear();
}

bool XYZV3::serialSendMessage(const char *format, ...)
{
	debugPrint(DBG_LOG, "XYZV3::serialSendMessage(%s)", format);

	bool success = false;

	if(m_stream && m_stream->isOpen() && format)
	{
		const static int BUF_SIZE  = 2048;
		char msgBuf[BUF_SIZE];
		va_list arglist;

		va_start(arglist, format);
		vsnprintf(msgBuf, sizeof(msgBuf), format, arglist);
		msgBuf[sizeof(msgBuf)-1] = '\0';
		va_end(arglist);

		m_stream->clear();
		m_stream->writeStr(msgBuf);

		success = true;
	}
	else
		debugPrint(DBG_WARN, "XYZV3::serialSendMessage invalid input");

	return success;
}

XYZPrintStateCode XYZV3::translateStatus(int oldStatus)
{
	// translate old printer status codes
	switch(oldStatus)
	{
	case 0: return PRINT_INITIAL;
	case 1: return PRINT_HEATING;
	case 2: return PRINT_PRINTING;
	case 3: return PRINT_CALIBRATING;
	case 4: return PRINT_CALIBRATING_DONE;
	case 5: return PRINT_COOLING_DONE;
	case 6: return PRINT_COOLING_END;
	case 7: return PRINT_ENDING_PROCESS;
	case 8: return PRINT_ENDING_PROCESS_DONE;
	case 9: return PRINT_JOB_DONE;
	case 10: return PRINT_NONE;
	case 11: return PRINT_IN_PROGRESS;
	case 12: return PRINT_STOP;
	case 13: return PRINT_LOAD_FILAMENT;
	case 14: return PRINT_UNLOAD_FILAMENT;
	case 15: return PRINT_AUTO_CALIBRATION;
	case 16: return PRINT_JOG_MODE;
	case 17: return PRINT_FATAL_ERROR;
	default: return (XYZPrintStateCode)oldStatus;
	}
}

int XYZV3::translateErrorCode(int code)
{
	// 0x40000000 == extruder 1
	// 0x20000000 == extruder 2
	switch(code & 0xFFFF)
	{
	case 0x0002: return 0x0101;
	case 0x0003: return 0x001B; // machine buisy
//	case 0x0003: return 0x0102; // bead heat error
	case 0x0004: return 0x0103;
	case 0x0005: return 0x0104;
	case 0x0006: return 0x0105;
	case 0x0009: return 0x0108;
	case 0x000A: return 0x0109;
	case 0x000B: return 0x010A;
	case 0x0012: return 0x010B;
	case 0x000E: return 0x0202;
	case 0x000F: return 0x0203;
	case 0x0011: return 0x0205;
	case 0x001C: return 0x0206;
	case 0x001D: return 0x0207;
	case 0x0007: return 0x0208;
	case 0x0008: return 0x0209;
	case 0x020B: return 0x020C;
	case 0x0014: return 0x0401;
	case 0x0015: return 0x0402;
	case 0x0017: return 0x0404;
	case 0x0010: return 0x0405;
	case 0x0018: return 0x0405;

	default: return code & 0xFFFF;
	}
}

bool XYZV3::parseStatusSubstring(const char *str)
{
	debugPrint(DBG_VERBOSE, "XYZV3::parseStatusSubstring(%s)", str);

	if(str && str[0] != '\0' && str[1] == ':')
	{
		const char *strPtr = NULL;
		char s1[256] = "";
		int t;

		switch(str[0])
		{
		case 'b': // heat bed temperature, b:xx - deg C
			sscanf(str, "b:%d", &m_status.bBedActualTemp_C);
			break;

		case 'c': // Calibration values, c:{x1,x2,x3,x4,x5,x6,x7,x8,x9} 
			//only on miniMover
			m_status.cCalibIsValid = true;
			sscanf(str, "c:{%d,%d,%d,%d,%d,%d,%d,%d,%d}",
				&m_status.cCalib[0], 
				&m_status.cCalib[1], 
				&m_status.cCalib[2], 
				&m_status.cCalib[3], 
				&m_status.cCalib[4], 
				&m_status.cCalib[5], 
				&m_status.cCalib[6], 
				&m_status.cCalib[7], 
				&m_status.cCalib[8]);
			break;

		case 'd': // print status, d:ps,el,es
			//   ps - print percent complete (0-100?)
			//   el - print elapsed time (minutes)
			//   es - print estimated time left (minutes)
			//   a value of 0,0,0 indicates no job is running
			sscanf(str, "d:%d,%d,%d", &m_status.dPrintPercentComplete, &m_status.dPrintElapsedTime_m, &m_status.dPrintTimeLeft_m);

			// why is this set to this? is it a bad 3w file?
			if(m_status.dPrintTimeLeft_m == 0x04444444)
				m_status.dPrintTimeLeft_m = -1;
			break;

		case 'e': // error status, e:ec - some sort of string?
			sscanf(str, "e:%d", &t);
			// 0x40 is extruder 1
			// 0x20 is extruder 2
			// 0x00 is everything else
			m_status.eErrorStatusHiByte = (t & 0xFF000000) >> 24;
			m_status.eErrorStatus = translateErrorCode(t);

			strPtr = errorCodeToStr(m_status.eErrorStatus);
			if(strPtr)
				strcpy(m_status.eErrorStatusStr, strPtr);
			else
				m_status.eErrorStatusStr[0] = '\0';
			break;

		case 'f': // filament remaining, f:ct,len,len2
			//   ct - how many spools of filiment, 1 for normal printer
			//   len - filament 1 left in millimeters
			//   len2 - filament 2 left in millimeters, optional
			sscanf(str, "f:%d,%d,%d", &m_status.fFilamentSpoolCount, &m_status.fFilament1Remaining_mm, &m_status.fFilament2Remaining_mm);
			break;

		//case 'g': break; // unused

		case 'h': // pla filament loaded, h:x > 0 if pla filament in printer
			// not used with miniMaker
			sscanf(str, "h:%d", &m_status.hIsFilamentPLA);
			break;

		case 'i': // machine serial number, i:sn - serial number
			//****Note, convert ? characters to - characters when parsing sn
			sscanf(str, "i:%s", m_status.iMachineSerialNum);
			break;

		case 'j': // printer status, j:st,sb
			//   st - status id
			//   sb - substatus id
			sscanf(str, "j:%d,%d", &t, &m_status.jPrinterSubState);
			m_status.jPrinterState = translateStatus(t);

			// fill in status string
			strPtr = stateCodesToStr(m_status.jPrinterState, m_status.jPrinterSubState);
			if(strPtr)
				strcpy(m_status.jPrinterStateStr, strPtr); 
			else 
				m_status.jPrinterStateStr[0] = '\0';
			break;

		case 'k': // material type, k:xx
			//   xx is material type?
			//   one of 41 46 47 50 51 54 56
			//not used on miniMaker
			sscanf(str, "k:%d", &m_status.kFilamentMaterialType);

			strPtr = filamentMaterialTypeToStr(m_status.kFilamentMaterialType);
			if(strPtr)
				strcpy(m_status.kFilamentMaterialTypeStr, strPtr);
			else 
				m_status.kFilamentMaterialTypeStr[0] = '\0';

			break;

		case 'l': // language, l:ln - one of en, fr, it, de, es, jp, tw, cn, kr
			sscanf(str, "l:%s", m_status.lLang);
			break;

		case 'm': // ????? m:x,y,z
			//****FixMe, work out what this is
			sscanf(str, "m:%d,%d,%d", &m_status.mVal[0], &m_status.mVal[1], &m_status.mVal[2]);
			break;

		case 'n': // printer name, n:nm - name as a string
			sscanf(str, "n:%s", m_status.nMachineName);
			break;

		case 'o': // print options, o:ps,tt,cc,al
			//   ps is package size * 1024
			//   tt ??? //****FixMe, work out what this is
			//   cc ??? //****FixMe, work out what this is
			//   al is auto leveling on if a+
			//o:p8,t1,c1,a+
			sscanf(str, "o:p%d,t%d,c%d,%s", 
				&m_status.oPacketSize, 
				&m_status.oT, 
				&m_status.oC, 
				s1);
			m_status.oPacketSize = (m_status.oPacketSize > 0) ? m_status.oPacketSize*1024 : 8192;
			m_status.oAutoLevelEnabled = (0 == strcmp(s1, "a+")) ? true : false;
			break;

		case 'p': // printer model number, p:mn - model_num
			//p:dv1MX0A000
			sscanf(str, "p:%s", m_status.pMachineModelNumber);
			m_info = XYZV3::modelToInfo(m_status.pMachineModelNumber);
			break;

		//case 'q': break; // unused
		//case 'r': break; // unused

		case 's': // machine capabilities, s:{xx,yy...}
			//   xx is one of
			//   button:no
			//   buzzer:on  can use buzzer?
			//   dr:{front:on,top:on}  front/top door 
			//   eh:1  lazer engraver installed
			//   fd:1  ???
			//   fm:1  ???
			//   of:1  open filament allowed
			//   sd:yes  sd card yes or no
			//s:{"fm":0,"fd":1,"sd":"yes","button":"no","buzzer":"on"}
			//s:{"fm":1,"fd":1,"dr":{"top":"off","front":"off"},"sd":"yes","eh":"0","of":"1"}
			//****FixMe, need to detect if status is available or not, and indicate if feature is present
			if(findJsonVal(str, "buzzer", s1))
				m_status.sBuzzerEnabled = (0==strcmp(s1, "on")) ? true : false;
			if(findJsonVal(str, "button", s1))
				m_status.sButton = (0==strcmp(s1, "yes")) ? true : false;
			if(findJsonVal(str, "top", s1))
				m_status.sFrontDoor = (0==strcmp(s1, "on")) ? true : false;
			if(findJsonVal(str, "front", s1))
				m_status.sTopDoor = (0==strcmp(s1, "on")) ? true : false;
			if(findJsonVal(str, "sd", s1))
				m_status.sSDCard = (0==strcmp(s1, "yes")) ? true : false;
			if(findJsonVal(str, "eh", s1))
				m_status.sHasLazer = (s1[0] == '1') ? true : false;
			if(findJsonVal(str, "fd", s1))
				m_status.sFd = (s1[0] == '1') ? true : false;
			if(findJsonVal(str, "fm", s1))
				m_status.sFm = (s1[0] == '1') ? true : false;
			if(findJsonVal(str, "of", s1))
				m_status.sOpenFilament = (s1[0] == '1') ? true : false;
			break;

		case 't': // extruder temperature, t:ss,aa,bb,cc,dd
			{
			//   if ss == 1
			//     aa is extruder temp in C
			//     bb is target temp in C
			//   else
			//     aa is extruder 1 temp
			//     bb is extruder 2 temp
			//t:1,20,0
			int t;
			sscanf(str, "t:%d,%d,%d", &m_status.tExtruderCount, &m_status.tExtruder1ActualTemp_C, &t);
			if(m_status.tExtruderCount == 1)
				m_status.tExtruderTargetTemp_C = t; // set by O: if not set here
			else
				m_status.tExtruder2ActualTemp_C = t;
			}
			break;

		//case 'u': break; // unused

		case 'v': // firmware version, v:fw or v:os,ap,fw
			//   fw is firmware version string
			//   os is os version string
			//   ap is app version string
			//v:1.1.1
			sscanf(str, "v:%s", m_status.vFirmwareVersion);
			break;

		case 'w': // filament serian number, w:id,sn,xx
			//   if id == 1
			//     sn is filament 1 serial number
			//     xx is optional default filament temp
			//   else
			//     sn is filament 1 serial number
			//     xx is filament 2 serial number
			//
			//   Serial number format
			//   DDMLCMMTTTSSSS
			//   	DD - Dloc
			//   	M - Material
			//   	L - Length
			//   	    varies but in general 
			//   	    3 - 120000 mm
			//   	    5 - 185000 mm
			//   	    6 - 240000 mm
			//   	C - color
			//   	MM - Mloc
			//   	TTT - Mdate
			//   	SSSS - serial number
			//w:1,PMP6PTH6840596
			sscanf(str, "w:%d,%s,%s", &m_status.wFilamentCount, m_status.wFilament1SerialNumber, m_status.wFilament2SerialNumber);

			if(strlen(m_status.wFilament1SerialNumber) > 4)
			{
				strPtr = filamentColorIdToStr(m_status.wFilament1SerialNumber[4]);
				if(strPtr)
					strcpy(m_status.wFilament1Color, strPtr);
				else
					m_status.wFilament1Color[0] = '\0';
			}
			else
				m_status.wFilament1Color[0] = '\0';

			if(strlen(m_status.wFilament2SerialNumber) > 4)
			{
				strPtr = filamentColorIdToStr(m_status.wFilament2SerialNumber[4]);
				if(strPtr)
					strcpy(m_status.wFilament2Color, strPtr);
				else
					m_status.wFilament2Color[0] = '\0';
			}
			else
				m_status.wFilament2Color[0] = '\0';
			break;

		//case 'x': break; // unused
		//case 'y': break; // unused

		case 'z': // z offset
			if(sscanf(str, "z:%d", &m_status.zOffset))
				m_status.zOffsetSet = true;
			break;

		// case 'A' to 'F' unused

		case 'G':
			// info on last print and filament used?
			findJsonVal(str, "LastUsed", m_status.GLastUsed);
			break;

		// case 'H' to 'K' unused

		case 'L': // Lifetime timers, L:xx,ml,el,lt
			//   xx - unknown, set to 1
			//   ml - machine lifetime power on time (minutes)
			//   el - extruder lifetime power on time (minutes) (print time)
			//   lt - last power on time (minutes) (or last print time?) optional
			sscanf(str, "L:%d,%d,%d,%d", 
				&m_status.LExtruderCount,  // just a guess
				&m_status.LPrinterLifetimePowerOnTime_min, 
				&m_status.LExtruderLifetimePowerOnTime_min, 
				&m_status.LPrinterLastPowerOnTime_min); // optional
			break;

		//case 'M': break; // unused
		//case 'N': break; // unused

		case 'O': // target temp?, O:{"nozzle":"xx","bed":"yy"}
			// xx is nozzle target temp in C
			// yy is bed target temp in C
			if(findJsonVal(str, "nozzle", s1))
				m_status.tExtruderTargetTemp_C = atoi(s1); // set by t: if not set here
			if(findJsonVal(str, "bed", s1))
				m_status.OBedTargetTemp_C = atoi(s1);
			break;

		//case 'P' to 'U' unused

		case 'V': // some sort of version
			//V:5.1.5
			//****FixMe, work out what this is
			sscanf(str, "V:%s", m_status.VString);
			break;

		case 'W': // wifi information
			// wifi information, only with mini w?
			// W:{"ssid":"a","bssid":"b","channel":"c","rssiValue":"d","PHY":"e","security":"f"}
			// all are optional
			//  a is ssid
			//  b is bssid
			//  c is channel
			//  d is rssiValue
			//  e is PHY
			//  f is security
			findJsonVal(str, "ssid", m_status.WSSID);
			findJsonVal(str, "bssid", m_status.WBSSID);
			findJsonVal(str, "channel", m_status.WChannel);
			findJsonVal(str, "rssiValue", m_status.WRssiValue); //****FixMe, match N4NetRssiValue
			findJsonVal(str, "PHY", m_status.WPHY);
			findJsonVal(str, "security", m_status.WSecurity);
			break;

		case 'X': // Nozzle Info, X:nt,sn,sn2
			//   nt is nozzle type one of 
			//     3, 84
			//       nozzle diameter 0.3 mm
			//     1, 77, 82 
			//       nozzle diameter 0.4 mm
			//     2
			//       nozzle diameter 0.4 mm, dual extruder
			//     54
			//       nozzle diameter 0.6 mm
			//     56 
			//       nozzle diameter 0.8 mm
			//     L, N, H, Q
			//       lazer engraver
			//   sn is serial number in the form xx-xx-xx-xx-xx-xx-yy
			//     where xx is the nozzle serial number
			//     and yy is the total nozzle print time (in minutes)
			//   sn2 is optional second serial number for second nozzle
			sscanf(str, "X:%d,%s,%s", &m_status.XNozzleID, m_status.XNozzle1SerialNumber, m_status.XNozzle2SerialNumber);
			m_status.XNozzleDiameter_mm = XYZV3::nozzleIDToDiameter(m_status.XNozzleID);
			m_status.XNozzleIsLaser = XYZV3::nozzleIDIsLaser(m_status.XNozzleID);
			break;

		// case 'Y' to '3' unused

		case '4': // Query IP
			// some sort of json string with wlan, ip, ssid, MAC, rssiValue
			// not used by miniMaker
			//4:{"wlan":{"ip":"0.0.0.0","ssid":"","channel":"0","MAC":"20::5e:c4:4f:bd"}}
			findJsonVal(str, "ip", m_status.N4NetIP);
			findJsonVal(str, "ssid", m_status.N4NetSSID);
			findJsonVal(str, "channel", m_status.N4NetChan);
			findJsonVal(str, "MAC", m_status.N4NetMAC);
			if(findJsonVal(str, "rssiValue", s1))
			{
				m_status.N4NetRssiValue = -atoi(s1);
				m_status.N4NetRssiValuePct = rssiToPct(m_status.N4NetRssiValue);
			}
			else
			{
				m_status.N4NetRssiValue = -100;
				m_status.N4NetRssiValuePct = 0;
			}
			break;

		// case '5' to '9' unused

		default:
			debugPrint(DBG_WARN, "XYZV3::parseStatusSubstring unknown string: %s", str);
			break;
		}

		return true;
	}
	else
		debugPrint(DBG_WARN, "XYZV3::parseStatusSubstring invalid input");

	return false;
}

// query status, then look for 
// x:yyy ...
// $
// Optionally look for
// str
// $
// they will not come interleaved
// want to return true if we happen to find both status and optional string
// also need to return optional string if found
// 
// We can query using 'a' to fill in all values
// in that case we zero out the data and look for a '$' to indicate that everything was returned
//
// Otherwise we can query for up to 4 sub values
// in that case only the sub values are returned, no terminating '$' is sent
bool XYZV3::queryStatus(bool doPrint, float timeout_s, char q1, char q2, char q3, char q4)
{
	debugPrint(DBG_LOG, "XYZV3::queryStatus(%d, %0.2f, %c...)", doPrint, timeout_s, q1);

	bool success = false;

	if(m_stream && m_stream->isOpen())
	{
		int count = 0;
		char q[4] = { q1, q2, q3, q4 };
		bool b[4] = { 0, 0, 0, 0 };
		char qStr[5] = { 0, 0, 0, 0, 0 };

		if(!q[0])
			q[0] = 'a';

		// setup our query
		for(count=0; count < 4; count++)
		{
			qStr[count] = q[count];

			// break if entry is null
			if(q[count] == 0)
				break;

			// if any entry is 'a' then no need to send anything else
			if(q[count] == 'a')
			{
				count = 1;
				q[0] = 'a';
				qStr[0] = 'a';
				qStr[1] = '\0';
				break;
			}
		}
		qStr[4] = '\0';

		if(serialSendMessage("XYZv3/query=%s", qStr))
		{
			// zero out results, only if updating everything
			if(q[0] == 'a')
				memset(&m_status, 0, sizeof(m_status));

			bool foundState = false;
			bool isDone = false; // only try so many times for the answer
			const char *buf = waitForLine(timeout_s);
			while(*buf && !isDone)
			{
				if(buf[0] == '$') // end of message
				{
					isDone = true;
					if(q[0] != 'a' || foundState)
						success = true;
				}
				else if(buf[0] == 'E') // error string like E4$\n
				{
					//****FixMe, returns E4$\n or E7$\n sometimes
					// in those cases we just ignore the error and keep going

					// if error is E1$ then quite
					// otherwise expect $ on next line to terminate string
					if(buf[1] && buf[2] == '$')
						isDone = true;

					debugPrint(DBG_WARN, "XYZV3::queryStatus recieved error: %s", buf);
				}
				else if(parseStatusSubstring(buf))
				{
					//****FixMe, should we test 'j' or 'l' for success
					// j is key to our ability to operate but l shows up much later in the list of returned values
					if(buf[0] == 'j') 
						foundState = true;

					if(doPrint)
						printf("%s\n", buf);

					// if not getting all, then done once
					// we see all the requested values
					// but only on wifi!
					if(q[0] != 'a' && isWIFI())
					{
						isDone = true;
						for(int i=0; i<count; i++)
						{
							if(q[i] == buf[0])
								b[i] = true;
							if(!b[i])
								isDone = false;
						}

						if(isDone)
							success = true;
					}
				}
				else
				{
					// handle other messages
					//****FixMe, do we want this?
				}

				if(!isDone)
					buf = waitForLine(0.5f);
			}

			if(!isDone)
				debugPrint(DBG_WARN, "XYZV3::queryStatus queryStatus timed out");

			m_status.isValid = success;

			// manually pull zOffset, if not set above
			// use zero wait time, in case command is ignored
			/*
			if(!m_status.zOffsetSet)
			{
				if(serialSendMessage("XYZv3/config=zoffset:get"))
				{
					const char *buf = waitForLine(0.5f); 
					if(*buf)
					{
						waitForEndCom();
						if(doPrint)
							printf("%s\n", buf);
						m_status.zOffset = atoi(buf);
						m_status.zOffsetSet = true;
					}
				}
			}
			*/
		}
		else
			debugPrint(DBG_VERBOSE, "XYZV3::queryStatus failed to query");
	}
	else
		debugPrint(DBG_WARN, "XYZV3::queryStatus invalid input");

	return success;
}

float XYZV3::nozzleIDToDiameter(int id)
{
	switch(id)
	{
	case 3:
	case 84:
		return 0.3f;
	case 1:
	case 2:
	case 77:
	case 82:
		return 0.4f;
	case 54:
		return 0.6f;
	case 56:
		return 0.8f;
	}

	return -1.0f;
}

bool XYZV3::nozzleIDIsLaser(int id)
{
	switch(id)
	{
	case 'L':
	case 'N':
	case 'H':
	case 'Q':
		return true;
	}

	return false;
}

const char* XYZV3::stateCodesToStr(int state, int subState)
{
	static char tstr[512];
	tstr[0] = '\0';

	switch(state)
	{
	case PRINT_INITIAL:
		return "initializing";
	case PRINT_HEATING:
		return "heating";
	case PRINT_PRINTING:
		return "printing";
	case PRINT_CALIBRATING:
		return "calibrating";
	case PRINT_CALIBRATING_DONE:
		return "calibration finished";
	case PRINT_IN_PROGRESS:
		return "printing in progress";
	case PRINT_COOLING_DONE:
		return "print cooling finished";
	case PRINT_COOLING_END:
		return "print cooling end";
	case PRINT_ENDING_PROCESS:
		return "print end process";
	case PRINT_ENDING_PROCESS_DONE:
		return "print end process done";
	case PRINT_JOB_DONE:
		return "print job done";
	case PRINT_NONE:
		return "printer idle";
	case PRINT_STOP:
		return "stop print";
	case PRINT_LOAD_FILAMENT:
		return "loading filament";
	case PRINT_UNLOAD_FILAMENT:
		return "unloading filament";
	case PRINT_AUTO_CALIBRATION:
		return "auto calibrating";
	case PRINT_JOG_MODE:
		return "jog mode";
	case PRINT_FATAL_ERROR:
		return "fatal error";

	case STATE_PRINT_FILE_CHECK:
		sprintf(tstr, "file check (%d)", subState);
		return tstr;
	case STATE_PRINT_LOAD_FIALMENT: //  (substatus 12 14)
		sprintf(tstr, "load filament (%d)", subState);
		return tstr;
	case STATE_PRINT_UNLOAD_FIALMENT: //  (substate 22 24)
		// substate 21 is unload start or heating
		// substate 22 is unload unloading or done, I think
		sprintf(tstr, "unload filament (%d)", subState);
		return tstr;
	case STATE_PRINT_JOG_MODE:
		sprintf(tstr, "jog mode (%d)", subState);
		return tstr;
	case STATE_PRINT_FATAL_ERROR:
		sprintf(tstr, "fatal error (%d)", subState);
		return tstr;
	case STATE_PRINT_HOMING:
		sprintf(tstr, "homing (%d)", subState);
		return tstr;
	case STATE_PRINT_CALIBRATE: //  (substate 41 42 43 44 45 46 47 48 49)
		sprintf(tstr, "calibrating (%d)", subState);
		return tstr;
	case STATE_PRINT_CLEAN_NOZZLE: //  (substate 52)
		sprintf(tstr, "clean nozzle (%d)", subState);
		return tstr;
	case STATE_PRINT_GET_SD_FILE:
		sprintf(tstr, "get sd file (%d)", subState);
		return tstr;
	case STATE_PRINT_PRINT_SD_FILE:
		sprintf(tstr, "print sd file (%d)", subState);
		return tstr;
	case STATE_PRINT_ENGRAVE_PLACE_OBJECT: //  (substate 30)
		sprintf(tstr, "engrave place object (%d)", subState);
		return tstr;
	case STATE_PRINT_ADJUST_ZOFFSET:
		sprintf(tstr, "adjust zoffset (%d)", subState);
		return tstr;
	case PRINT_TASK_PAUSED:
		sprintf(tstr, "print paused (%d)", subState);
		return tstr;
	case PRINT_TASK_CANCELING:
		sprintf(tstr, "print canceled (%d)", subState);
		return tstr;
	case STATE_PRINT_BUSY:
		sprintf(tstr, "busy (%d)", subState);
		return tstr;
	case STATE_PRINT_SCANNER_IDLE:
		sprintf(tstr, "scanner idle (%d)", subState);
		return tstr;
	case STATE_PRINT_SCANNER_RUNNING:
		sprintf(tstr, "scanner running (%d)", subState);
		return tstr;

	default:
		sprintf(tstr, "unknown error (%d:%d)", state, subState);
		return tstr;
	}
}

/*
0010 Print bed heating problem, temp out of range
0016 Cartridge 1 not installed properly. 
0033 Turntable movement abnormalities
0055 inkjet head error 
0056 inkjet data error
0057 Unable to detect extruder, Please reinstall the extruder and reconnect the flat cable, then restart the printer.
5001 Calibration failure
5011 Right camera error
5013 Left camera error
5021 Right scanning laser error
5022 Left scanning laser error
5023 Turntable error during calibration
5031 Storage error during calibration
*/
const char* XYZV3::errorCodeToStr(int code)
{
	switch(code)
	{
	case 0x0000: return "no error";

	case 0x001B: return "M_MACHINE_BUSY";				//        machine buisy
	case 0x001F: return "scanner error";				//        scanner error
	case 0x0020: return "scanner buisy";				//        scanner buisy
	case 0x0101: return "M_THERMAL_HEATER_OUT_TIMER";	// (0011) Extruder 1 heating problem, cant heat to target temp
//	case 0x0003: return "M_THERMAL_BED_OUT_TIMER";		// (0003) Print bed heating problem
	case 0x0102: return "M_THERMAL_BED_OUT_TIMER";		// (0003) Print bed heating problem
	case 0x0103: return "M_THERMAL_HEATER_OUT_CONTROL"; // (0014) Extruder 1 heating problem, temp too high
	case 0x0104: return "M_THERMAL_BED_OUT_CONTROL";	// (0013) Print bed heating problem, temp too high
	case 0x0105: return "L_ERROR_SD_CARD";				// ??? is this (0040) Internal storage error, can not read/write sd card
	case 0x0108: return "M_MACHINE_ERROR_X_AXIS";		// (0030) X-axis movement abnormalities or bad endstop
	case 0x0109: return "M_MACHINE_ERROR_Y_AXIS";		// (0031) Y-axis movement abnormalities or bad endstop
	case 0x010A: return "M_MACHINE_ERROR_Z_AXIS";		// (0032) Z-axis movement abnormalities or bad endstop
	case 0x010B: return "M_FLASHMEMORY_ERROR";			// (0050) Memory error
	case 0x010D: return "L_ERROR_FLASH_RAM";			// (0051) Internal communication error, Reboot the printer
	case 0x010E: return "L_ERROR_NOZZLE_EEPROM";		// (0052) Extruder storage error, Replace the extruder
	case 0x010F: return "L_40W35W_NOZZLE_EEPROM";		// (0054) Incompatible Nozzle
	case 0x0201: return "M_PC_COMMUNICATION_ERROR";		// (0201) Connection error between PC and printer
	case 0x0202: return "M_FW_UPDATE_ERROR";			//        firmware update error
	case 0x0203: return "M_FILAMENT_JAM";				//        filament jam
	case 0x0205: return "M_FILAMENT_WRONG";				//        wrong filament
	case 0x0206: return "M_NO_CASSETTE";				// (0028) Cartridge 1 not installed
	case 0x0207: return "M_CASSETTE_EMPTY";				// (0029, 0060) Cartrige 1 empty
	case 0x0208: return "M_CASSET_EEPROM_WRITE_ERROR";	// (0007) Cartrige 1 chip error (write)
	case 0x0209: return "M_CASSET_EEPROM_READ_ERROR";	// (0008) Cartrige 1 chip error (read)
	case 0x020D: return "Unsuported file version";		//        ???? Only on mini?
	case 0x020C: return "L_ERROR_SD_CARD";				// ??? is this (0040) Internal storage error, can not read/write sd card
	case 0x020F: return "L_FILAMENT_NO_INSTALL";		//
	case 0x0401: return "M_TOP_DOOR_OPEN";				//        top door open
	case 0x0402: return "M_FRONT_DOOR_OPEN";			//        front door open
	case 0x0403: return "M_FILAMENT_LOW";				//
	case 0x0404: return "M_FILAMENT_LOW_TO_EMPTY";		//        filament low or out
	case 0x0405: return "M_FILAMENT_END";				//        filament out

	default: return "---";
	}
}

const char* XYZV3::filamentMaterialTypeToStr(int materialType)
{
	switch(materialType)
	{
	case 41: return "ABS";
	case 46: return "TPE";
	case 47: return "PETG";
	case 50: return "PLA";
	case 51: return "PLA";
	case 54: return "Tough PLA";
	case 56: return "PVA";
	default: return "---";
	}
}

const char* XYZV3::filamentColorIdToStr(int colorId)
{
	switch(colorId)
	{
	case '0': return "Bronze";
	case '1': return "Silver";
	case '2': return "Clear Red";
	case '3': return "Clear";
	case '4': return "Bottle Green";
	case '5': return "Neon Magenta";
	case '6': return "SteelBlue";
	case '7': return "Sun Orange";
	case '8': return "Pearl White";
	case '9': return "Copper";
	case 'A': return "Purple";
	case 'B': return "Blue";
	case 'C': return "Neon Tangerine";
	case 'D': return "Viridity";
	case 'E': return "Olivine";
	case 'F': return "Gold";
	case 'G': return "Green";
	case 'H': return "Neon Green";
	case 'I': return "Snow White";
	case 'J': return "Neon Yellow";
	case 'K': return "Black";
	case 'L': return "Violet";
	case 'M': return "Grape Purple";
	case 'N': return "Purpurine";
	case 'O': return "Clear Yellow";
	case 'P': return "Clear Green";
	case 'Q': return "Clear Tangerine";
	case 'R': return "Red";
	case 'S': return "Cyber Yellow";
	case 'T': return "Tangerine";
	case 'U': return "Clear Blue";
	case 'V': return "Clear Purple";
	case 'W': return "White";
	case 'X': return "Clear Magenta";
	case 'Y': return "Yellow";
	case 'Z': return "Nature";
	default: return "---";
	}
}

int XYZV3::rssiToPct(int rssi)
{
	if(rssi >= -50)
		return 100;
	if(rssi <= -100)
		return 0;
	return (rssi + 100) * 2;
}

// === action commands ===

void XYZV3::setState(ActState state, float timeout_s)
{
	m_actState = state;

	// force progress bar to 100% on exit
	if(state == ACT_FAILURE || state == ACT_SUCCESS)
		m_progress = 100;

	// get default if not specified
	if(timeout_s < 0)
		timeout_s = (m_stream) ? m_stream->getDefaultTimeout() : 5.0f;
	m_timeout.setTimeout_s(timeout_s);
}

ActState XYZV3::getState()
{
	return m_actState;
}

const char* XYZV3::getStateStr()
{
	switch(m_actState)
	{
	case ACT_FAILURE: return "error";
	case ACT_SUCCESS: return "success";

	// Calibrate Bed
	case ACT_CB_START: return "initializing";
	case ACT_CB_START_SUCCESS: return "initializing";
	case ACT_CB_HOME: return "homing";
	case ACT_CB_ASK_LOWER: return "lower detector";
	case ACT_CB_LOWERED: return "calibrating";
	case ACT_CB_CALIB_START: return "calibrating";
	case ACT_CB_CALIB_RUN: return "calibrating";
	case ACT_CB_ASK_RAISE: return "raise detector";
	case ACT_CB_RAISED: return "finishing";
	case ACT_CB_COMPLETE: return "finishing";

	// Clean Nozzle
	case ACT_CL_START: return "initializing";
	case ACT_CL_START_SUCCESS: return "initializing";
	case ACT_CL_WARMUP_COMPLETE: return "warming up";
	case ACT_CL_CLEAN_NOZLE: return "clean nozle";
	case ACT_CL_FINISH: return "finishing";
	case ACT_CL_COMPLETE: return "finishing";

	// Home printer
	case ACT_HP_START: return "initializing";
	case ACT_HP_START_SUCCESS: return "initializing";
	case ACT_HP_HOME_COMPLETE: return "homing";

	// Jog Printer
	case ACT_JP_START: return "initializing";
	case ACT_JP_START_SUCCESS: return "initializing";
	case ACT_JP_JOG_COMPLETE: return "jogging";

	// Load Fillament
	case ACT_LF_START: return "initializing";
	case ACT_LF_START_SUCCESS: return "initializing";
	case ACT_LF_HEATING: return "heating";
	case ACT_LF_LOADING: return "loading";
	case ACT_LF_WAIT_LOAD: return "check nozle";
	case ACT_LF_LOAD_FINISHED: return "finishing";
	case ACT_LF_LOAD_COMPLETE: return "finishing";

	// Unload Fillament
	case ACT_UF_START: return "initializing";
	case ACT_UF_START_SUCCESS: return "initializing";
	case ACT_UF_HEATING: return "heating";
	case ACT_UF_UNLOADING: return "unloading";
	case ACT_UF_UNLOAD_COMPLETE: return "finishing";
	// only get here if cancel button pressed
	case ACT_UF_CANCEL: return "canceling";
	case ACT_UF_CANCEL_COMPLETE: return "canceling";

	// convert file
	case ACT_CF_START: return "initializing";
	case ACT_CF_COMPLETE: return "finishing";

	// print file
	case ACT_PF_START: return "initializing";
	case ACT_PF_SEND: return "uploading file";
	case ACT_PF_SEND_PROCESS: return "uploading file";
	case ACT_PF_COMPLETE: return "finishing";

	// upload firmware
	case ACT_FW_START: return "initializing";
	case ACT_FW_SEND_PROCESS: return "uploading firmware";
	case ACT_FW_COMPLETE: return "finishing";

	default: 
		assert(false);
		return "unknown";
	}
}

int XYZV3::getProgress()
{
	return m_progress;
}


//--------------

// call to start calibration
void XYZV3::calibrateBedStart()
{
	debugPrint(DBG_LOG, "XYZV3::calibrateBedStart()");
	m_progress = 0;
	setState(ACT_CB_START);
}

// call in loop while true to pump status
bool XYZV3::calibrateBedRun()
{
	debugPrint(DBG_LOG, "XYZV3::calibrateBedRun() %d", m_actState);
	const char *val;

	if(!m_stream || !m_stream->isOpen())
		setState(ACT_FAILURE);

	switch(m_actState)
	{
	case ACT_CB_START: // start
		if(serialSendMessage("XYZv3/action=calibratejr:new"))
			setState(ACT_CB_START_SUCCESS);
		else setState(ACT_FAILURE);
		break;
	case ACT_CB_START_SUCCESS: // wait on success
		if(isWIFI())
			setState(ACT_CB_HOME, 120);
		else if(checkForJsonState(&val))
		{
			if(jsonValEquals(val, "start"))
				setState(ACT_CB_HOME, 120);
			else
				setState(ACT_FAILURE);
		}
		else if(m_timeout.isTimeout())
			setState(ACT_FAILURE);
		else // loop
			m_progress = (int)(5.0f + 5.0f * m_timeout.getElapsedTime_pct());
		break;
	case ACT_CB_HOME: // wait for signal to lower detector
		if(!isWIFI() && checkForJsonState(&val))
		{
			if(jsonValEquals(val, "pressdetector"))
				setState(ACT_CB_ASK_LOWER, 240);
			else
				setState(ACT_FAILURE);
		}
		else if(isWIFI() && checkForState(STATE_PRINT_CALIBRATE, 41))
			setState(ACT_CB_ASK_LOWER, 240);
		else if(m_timeout.isTimeout())
			setState(ACT_FAILURE);
		else // loop
			m_progress = (int)(10.0f + 5.0f * m_timeout.getElapsedTime_pct());
		break;
	case ACT_CB_ASK_LOWER: // ask user to lower detector
		// waiting, nothing to do
		if(m_timeout.isTimeout())
			setState(ACT_FAILURE);
		else // loop
			m_progress = (int)(15.0f + 5.0f * m_timeout.getElapsedTime_pct());
		break;
	case ACT_CB_LOWERED: // notify printer detecotr was lowered
		if(serialSendMessage("XYZv3/action=calibratejr:detectorok"))
			setState(ACT_CB_CALIB_START);
		else setState(ACT_FAILURE);
	case ACT_CB_CALIB_START: // wait for calibration to start
		if(isWIFI())
			setState(ACT_CB_CALIB_RUN, 240);
		else if(checkForJsonState(&val))
		{
			if(jsonValEquals(val, "processing"))
				setState(ACT_CB_CALIB_RUN, 240);
			else
				setState(ACT_FAILURE);
		}
		else if(m_timeout.isTimeout())
			setState(ACT_FAILURE);
		else // loop
			m_progress = (int)(20.0f + 5.0f * m_timeout.getElapsedTime_pct());
		break;
	case ACT_CB_CALIB_RUN: // wait for calibration to finish
		if(!isWIFI() && checkForJsonState(&val))
		{
			if(jsonValEquals(val, "ok")) // or stat:fail
				setState(ACT_CB_ASK_RAISE, 240);
			else
				setState(ACT_FAILURE);
		}
		else if(isWIFI() && checkForState(STATE_PRINT_CALIBRATE, 44))
			setState(ACT_CB_ASK_RAISE, 240);
		else if(m_timeout.isTimeout())
			setState(ACT_FAILURE);
		else // loop
			m_progress = (int)(25.0f + 65.0f * 4.0f * m_timeout.getElapsedTime_pct());
		break;
	case ACT_CB_ASK_RAISE: // ask user to raise detector
		// waiting, nothing to do
		if(m_timeout.isTimeout())
			setState(ACT_FAILURE);
		else // loop
			m_progress = (int)(90.0f + 5.0f * m_timeout.getElapsedTime_pct());
		break;
	case ACT_CB_RAISED: // notify printer detector was raised
		if(serialSendMessage("XYZv3/action=calibratejr:release"))
			setState(ACT_CB_COMPLETE);
		else setState(ACT_FAILURE);
		break;
	case ACT_CB_COMPLETE:
		if(isWIFI())
			setState(ACT_SUCCESS);
		else if(checkForJsonState(&val))
		{
			if(jsonValEquals(val, "complete"))
				setState(ACT_SUCCESS);
			else
				setState(ACT_FAILURE);
		}
		else  if(m_timeout.isTimeout())
			setState(ACT_FAILURE);
		else // loop
			m_progress = (int)(95.0f + 5.0f * m_timeout.getElapsedTime_pct());
		break;
	default:
		break;
	}

	// return true if not error or done
	return m_actState != ACT_FAILURE && m_actState != ACT_SUCCESS;
}

bool XYZV3::calibrateBedPromptToLowerDetector()
{
	return m_actState == ACT_CB_ASK_LOWER;
}

// ask user to lower detector, then call this
void XYZV3::calibrateBedDetectorLowered()
{
	debugPrint(DBG_LOG, "XYZV3::calibrateBedDetectorLowered()");
	assert(m_actState == ACT_CB_ASK_LOWER);

	setState(ACT_CB_LOWERED);
}

bool XYZV3::calibrateBedPromptToRaiseDetector()
{
	return m_actState == ACT_CB_ASK_RAISE;
}

// ask user to raise detector, then call this
void XYZV3::calibrateBedDetectorRaised()
{
	debugPrint(DBG_LOG, "XYZV3::calibrateBedDetectorRaised()");
	assert(m_actState == ACT_CB_ASK_RAISE);

	setState(ACT_CB_RAISED);
}

//--------------

void XYZV3::cleanNozzleStart()
{
	debugPrint(DBG_LOG, "XYZV3::cleanNozzleStart()");
	m_progress = 0;
	setState(ACT_CL_START);
}

// call in loop while true to pump status
bool XYZV3::cleanNozzleRun()
{
	debugPrint(DBG_LOG, "XYZV3::cleanNozzleRun() %d", m_actState);
	const char *val;

	if(!m_stream || !m_stream->isOpen())
		setState(ACT_FAILURE);

	switch(m_actState)
	{
	case ACT_CL_START:
		if(serialSendMessage("XYZv3/action=cleannozzle:new"))
			setState(ACT_CL_START_SUCCESS);
		else setState(ACT_FAILURE);
		break;
	case ACT_CL_START_SUCCESS:
		if(isWIFI())
			setState(ACT_CL_WARMUP_COMPLETE, 120);
		else if(checkForJsonState(&val))
		{
			if(jsonValEquals(val, "start"))
				setState(ACT_CL_WARMUP_COMPLETE, 120);
			else
				setState(ACT_FAILURE);
		}
		else if(m_timeout.isTimeout())
			setState(ACT_FAILURE);
		else // loop
			m_progress = (int)(5.0f + 5.0f * m_timeout.getElapsedTime_pct());
	case ACT_CL_WARMUP_COMPLETE:
		if(!isWIFI() && checkForJsonState(&val))
		{
			if(jsonValEquals(val, "complete")) // or state is PRINT_NONE
				setState(ACT_CL_CLEAN_NOZLE, 240);
			else
				setState(ACT_FAILURE);
		}
		else if(isWIFI() && checkForState(STATE_PRINT_CLEAN_NOZZLE, 52))
			setState(ACT_CL_CLEAN_NOZLE, 240);
		else if(m_timeout.isTimeout())
			setState(ACT_FAILURE);
		else // loop
			m_progress = (int)(10.0f + 40.0f * m_timeout.getElapsedTime_pct());
		break;
	case ACT_CL_CLEAN_NOZLE:
		//printf("clean nozzle with a wire and press enter when finished\n");
		// waiting, nothing to do
		if(m_timeout.isTimeout())
			setState(ACT_FAILURE);
		else // loop
			m_progress = (int)(50.0f + 45.0f * m_timeout.getElapsedTime_pct());
		break;
	case ACT_CL_FINISH:
		if(serialSendMessage("XYZv3/action=cleannozzle:cancel"))
			setState(ACT_CL_COMPLETE);
		else setState(ACT_FAILURE);
		break;
	case ACT_CL_COMPLETE:
		if(isWIFI())
			setState(ACT_SUCCESS);
		else if(checkForJsonState(&val))
		{
			if(jsonValEquals(val, "ok"))
				setState(ACT_SUCCESS);
			else
				setState(ACT_FAILURE);
		}
		else if(m_timeout.isTimeout())
			setState(ACT_FAILURE);
		else // loop
			m_progress = (int)(95.0f + 5.0f * m_timeout.getElapsedTime_pct());
		break;
	default:
		break;
	}

	// return true if not error or done
	return m_actState != ACT_FAILURE && m_actState != ACT_SUCCESS;
}

bool XYZV3::cleanNozzlePromtToClean()
{
	return m_actState == ACT_CL_CLEAN_NOZLE;
}

void XYZV3::cleanNozzleCancel()
{
	debugPrint(DBG_LOG, "XYZV3::cleanNozzleCancel()");
	// can cancel at any time

	setState(ACT_CL_FINISH);
}

//---------------------------

void XYZV3::homePrinterStart()
{
	debugPrint(DBG_LOG, "XYZV3::homePrinterStart()");
	m_progress = 0;
	setState(ACT_HP_START);
}

bool XYZV3::homePrinterRun()
{
	debugPrint(DBG_LOG, "XYZV3::homePrinterRun() %d", m_actState);
	const char *val;

	if(!m_stream || !m_stream->isOpen())
		setState(ACT_FAILURE);

	switch(m_actState)
	{
	case ACT_HP_START:
		if(serialSendMessage("XYZv3/action=home"))
			setState(ACT_HP_START_SUCCESS);
		else setState(ACT_FAILURE);
		break;
	case ACT_HP_START_SUCCESS:
		if(isWIFI())
			setState(ACT_HP_HOME_COMPLETE, 120);
		else if(checkForJsonState(&val))
		{
			if(jsonValEquals(val, "start"))
				setState(ACT_HP_HOME_COMPLETE, 120);
			else
				setState(ACT_FAILURE);
		}
		else if(m_timeout.isTimeout())
			setState(ACT_FAILURE);
		else // loop
			m_progress = (int)(5.0f + 5.0f * m_timeout.getElapsedTime_pct());
		break;
	case ACT_HP_HOME_COMPLETE:
		if(!isWIFI() && checkForJsonState(&val))
		{
			if(jsonValEquals(val, "complete"))
				setState(ACT_SUCCESS);
			else
				setState(ACT_FAILURE);
		}
		else if(isWIFI() && checkForNotState(STATE_PRINT_HOMING, -1))
			setState(ACT_SUCCESS);
		else if(m_timeout.isTimeout())
			setState(ACT_FAILURE);
		else // loop
			m_progress = (int)(10.0f + 90.0f * 4.0f * m_timeout.getElapsedTime_pct());
		break;
	default:
		break;
	}

	// return true if not error or done
	return m_actState != ACT_FAILURE && m_actState != ACT_SUCCESS;
}

//---------------------------

void XYZV3::jogPrinterStart(char axis, int dist_mm)
{
	debugPrint(DBG_LOG, "XYZV3::jogPrinterStart(%c, %d)", axis, dist_mm);

	m_jogAxis = axis;
	m_jogDist_mm = dist_mm;

	m_progress = 0;
	setState(ACT_JP_START);
}

bool XYZV3::jogPrinterRun()
{
	debugPrint(DBG_LOG, "XYZV3::jogPrinterRun() %d", m_actState);
	const char *val;

	if(!m_stream || !m_stream->isOpen())
		setState(ACT_FAILURE);

	switch(m_actState)
	{
	case ACT_JP_START:
		if(serialSendMessage("XYZv3/action=jog:{\"axis\":\"%c\",\"dir\":\"%c\",\"len\":\"%d\"}", m_jogAxis, (m_jogDist_mm < 0) ? '-' : '+', abs(m_jogDist_mm)))
			setState(ACT_JP_START_SUCCESS);
		else setState(ACT_FAILURE);
		break;
	case ACT_JP_START_SUCCESS:
		if(isWIFI())
			setState(ACT_JP_JOG_COMPLETE, 120);
		else if(checkForJsonState(&val))
		{
			if(jsonValEquals(val, "start"))
				setState(ACT_JP_JOG_COMPLETE, 120);
			else
				setState(ACT_FAILURE);
		}
		else if(m_timeout.isTimeout())
			setState(ACT_FAILURE);
		else // loop
			m_progress = (int)(5.0f + 5.0f * m_timeout.getElapsedTime_pct());
		break;
	case ACT_JP_JOG_COMPLETE:
		if(!isWIFI() && checkForJsonState(&val))
		{
			if(jsonValEquals(val, "complete")) // or state is PRINT_NONE
				setState(ACT_SUCCESS);
			else
				setState(ACT_FAILURE);
		}
		// we may fall into idle or error state before we can detect joging state, so just block if joging state detected
		else if(isWIFI() && checkForNotState(STATE_PRINT_JOG_MODE, -1))
			setState(ACT_SUCCESS);
		else if(m_timeout.isTimeout())
			setState(ACT_FAILURE);
		else // loop
			m_progress = (int)(10.0f + 90.0f * 4.0f * m_timeout.getElapsedTime_pct());
		break;
	default:
		break;
	}

	// return true if not error or done
	return m_actState != ACT_FAILURE && m_actState != ACT_SUCCESS;
}

//---------------------------

void XYZV3::loadFilamentStart()
{
	debugPrint(DBG_LOG, "XYZV3::loadFilamentStart()");
	m_progress = 0;
	setState(ACT_LF_START);
}

bool XYZV3::loadFilamentRun()
{
	debugPrint(DBG_LOG, "XYZV3::loadFilamentRun() %d", m_actState);
	const char *val;

	if(!m_stream || !m_stream->isOpen())
		setState(ACT_FAILURE);

	switch(m_actState)
	{
	case ACT_LF_START:
		if(serialSendMessage("XYZv3/action=load:new"))
			setState(ACT_LF_START_SUCCESS);
		else setState(ACT_FAILURE);
		break;
	case ACT_LF_START_SUCCESS:
		if(isWIFI())
			setState(ACT_LF_HEATING, 120);
		else if(checkForJsonState(&val))
		{
			if(jsonValEquals(val, "start"))
				setState(ACT_LF_HEATING, 120);
			else
				setState(ACT_FAILURE);
		}
		else if(m_timeout.isTimeout())
			setState(ACT_FAILURE);
		else // loop
			m_progress = (int)(5.0f + 5.0f * m_timeout.getElapsedTime_pct());
		break;
	case ACT_LF_HEATING:
		if(!isWIFI() && checkForJsonState(&val))
		{
			if(jsonValEquals(val, "heat"))
				setState(ACT_LF_LOADING, 240);
			else
				setState(ACT_FAILURE);
		}
		else if(isWIFI())
			setState(ACT_LF_LOADING, 360);
		else if(m_timeout.isTimeout())
			setState(ACT_FAILURE);
		else // loop
			m_progress = (int)(10.0f + 30.0f * m_timeout.getElapsedTime_pct());
		break;
	case ACT_LF_LOADING:
		if(!isWIFI() && checkForJsonState(&val))
		{
			if(jsonValEquals(val, "load")) 
				setState(ACT_LF_WAIT_LOAD, 240);
			else
				setState(ACT_FAILURE);
		}
		else if(isWIFI() && checkForState(STATE_PRINT_LOAD_FIALMENT, 12))
			setState(ACT_LF_WAIT_LOAD, 240);
		else if(m_timeout.isTimeout())
			setState(ACT_FAILURE);
		else // loop
			m_progress = (int)(40.0f + 30.0f * m_timeout.getElapsedTime_pct());
		break;
	case ACT_LF_WAIT_LOAD:
		// if user does not hit cancel/done then eventually one of these will be returned
		// checkForJsonState("stat", "fail");
		// checkForJsonState("stat", "complete");
		// or state is PRINT_NONE
		if(m_timeout.isTimeout())
			setState(ACT_FAILURE);
		else // loop
			m_progress = (int)(70.0f + 25.0f * m_timeout.getElapsedTime_pct());
		break;
	case ACT_LF_LOAD_FINISHED:
		if(serialSendMessage("XYZv3/action=load:cancel"))
			setState(ACT_LF_LOAD_COMPLETE);
		else
			setState(ACT_FAILURE);
		break;
	case ACT_LF_LOAD_COMPLETE:
		if(isWIFI())
			setState(ACT_SUCCESS);
		else if(checkForJsonState(&val))
		{
			if(jsonValEquals(val, "complete"))
				setState(ACT_SUCCESS);
			else
				setState(ACT_FAILURE);
		}
		else
			setState(ACT_FAILURE);
		break;
	default:
		break;
	}

	// return true if not error or done
	return m_actState != ACT_FAILURE && m_actState != ACT_SUCCESS;
}
bool XYZV3::loadFilamentPromptToFinish()
{
	return m_actState == ACT_LF_WAIT_LOAD;
}

void XYZV3::loadFilamentCancel()
{
	debugPrint(DBG_LOG, "XYZV3::loadFilamentCancel()");
	// can cancel at any time

	setState(ACT_LF_LOAD_FINISHED);
}

//---------------------------

void XYZV3::unloadFilamentStart()
{
	debugPrint(DBG_LOG, "XYZV3::unloadFilamentStart()");
	m_progress = 0;
	setState(ACT_UF_START);
}

bool XYZV3::unloadFilamentRun()
{
	debugPrint(DBG_LOG, "XYZV3::unloadFilamentRun() %d", m_actState);
	const char *val;

	if(!m_stream || !m_stream->isOpen())
		setState(ACT_FAILURE);

	switch(m_actState)
	{
	case ACT_UF_START:
		if(serialSendMessage("XYZv3/action=unload:new"))
			setState(ACT_UF_START_SUCCESS);
		else setState(ACT_FAILURE);
		break;
	case ACT_UF_START_SUCCESS:
		if(isWIFI())
			setState(ACT_UF_HEATING, 120);
		else if(checkForJsonState(&val))
		{
			if(jsonValEquals(val, "start"))
				setState(ACT_UF_HEATING, 120);
			else
				setState(ACT_FAILURE);
		}
		else if(m_timeout.isTimeout())
			setState(ACT_FAILURE);
		else // loop
			m_progress = (int)(5.0f + 5.0f * m_timeout.getElapsedTime_pct());
		break;
	case ACT_UF_HEATING:
		if(!isWIFI() && checkForJsonState(&val))
		{
			if(jsonValEquals(val, "heat")) // could query temp and state with  XYZv3/query=jt
				setState(ACT_UF_UNLOADING, 240);
			else
				setState(ACT_FAILURE);
		}
		else if(isWIFI())
			setState(ACT_UF_UNLOADING, 360);
		else if(m_timeout.isTimeout())
			setState(ACT_FAILURE);
		else // loop
			m_progress = (int)(10.0f + 40.0f * m_timeout.getElapsedTime_pct());
		break;
	case ACT_UF_UNLOADING:
		if(!isWIFI() && checkForJsonState(&val))
		{
			if(jsonValEquals(val, "unload")) // could query temp and state with  XYZv3/query=jt
				setState(ACT_UF_UNLOAD_COMPLETE, 240);
			else
				setState(ACT_FAILURE);
		}
		else if(isWIFI() && checkForState(STATE_PRINT_UNLOAD_FIALMENT, 22))
			setState(ACT_UF_UNLOAD_COMPLETE, 360);
		else if(m_timeout.isTimeout())
			setState(ACT_FAILURE);
		else // loop
			m_progress = (int)(50.0f + 40.0f * m_timeout.getElapsedTime_pct());
		break;
	case ACT_UF_UNLOAD_COMPLETE:
		if(!isWIFI() && checkForJsonState(&val))
		{
			if(jsonValEquals(val, "complete")) // or state is PRINT_NONE
				setState(ACT_SUCCESS);
			else
				setState(ACT_FAILURE);
		}
		else if(isWIFI() && checkForNotState(STATE_PRINT_UNLOAD_FIALMENT, 22))
			setState(ACT_SUCCESS);
		else if(m_timeout.isTimeout())
			setState(ACT_FAILURE);
		else // loop
			m_progress = (int)(90.0f + 5.0f * m_timeout.getElapsedTime_pct());
		break;

	// only get here if cancel button pressed
	case ACT_UF_CANCEL:
		if(serialSendMessage("XYZv3/action=unload:cancel"))
			setState(ACT_UF_CANCEL_COMPLETE);
		else if(m_timeout.isTimeout())
			setState(ACT_FAILURE);
		else // loop
			m_progress = (int)(90.0f + 5.0f * m_timeout.getElapsedTime_pct());
		break;
	case ACT_UF_CANCEL_COMPLETE:
		if(isWIFI())
			setState(ACT_SUCCESS);
		else if(checkForJsonState(&val))
		{
			if(jsonValEquals(val, "complete"))
				setState(ACT_SUCCESS);
			else
				setState(ACT_FAILURE);
		}
		else if(m_timeout.isTimeout())
			setState(ACT_FAILURE);
		else // loop
			m_progress = (int)(95.0f + 5.0f * m_timeout.getElapsedTime_pct());
		break;
	default:
		break;
	}

	// return true if not error or done
	return m_actState != ACT_FAILURE && m_actState != ACT_SUCCESS;
}

void XYZV3::unloadFilamentCancel()
{
	debugPrint(DBG_LOG, "XYZV3::unloadFilamentCancel()");
	// can cancel at any time

	setState(ACT_UF_CANCEL);
}

// === config commands ===

int XYZV3::incrementZOffset(bool up)
{
	debugPrint(DBG_LOG, "XYZV3::incrementZOffset(%d)", up);

	int ret = -1;
	if(serialSendMessage("XYZv3/action=zoffset:%s", (up) ? "up" : "down"))
	{
		if(!isWIFI())
		{
			const char *buf = waitForLine();
			if(*buf)
			{
				if(!waitForEndCom())
					debugPrint(DBG_WARN, "XYZV3::incrementZOffset missing end com");

				ret = atoi(buf);
			}
			else
				debugPrint(DBG_WARN, "XYZV3::incrementZOffset failed to recieve response");
		}
	}
	else
		debugPrint(DBG_WARN, "XYZV3::incrementZOffset failed to send command");

	return ret;
}

int XYZV3::getZOffset()
{
	debugPrint(DBG_LOG, "XYZV3::getZOffset()");

	int ret = -1;

	// wifi is super slow, so return cached z-offset from status if available
	if(isWIFI() && m_status.zOffsetSet)
		ret = m_status.zOffset;
	else
	{
		if(serialSendMessage("XYZv3/config=zoffset:get"))
		{
			const char *buf = waitForLine();
			if(*buf)
			{
				if(!waitForEndCom())
					debugPrint(DBG_WARN, "XYZV3::getZOffset missing end com");

				ret = atoi(buf);
			}
		}
		debugPrint(DBG_WARN, "XYZV3::getZOffset failed to send command");
	}

	return ret;
}

bool XYZV3::checkForConfigOK(bool endCom)
{
	debugPrint(DBG_LOG, "XYZV3::checkForConfigOK()");

	if(isWIFI())
		return true;

	// else check for ok

	const char *buf = checkForLine();
	if(*buf)
	{
		if(0 == strcmp("ok", buf))
		{
			if(endCom && !waitForEndCom())
				debugPrint(DBG_WARN, "XYZV3::checkForConfigOK missing end com");

			return true;
		}
		else
			debugPrint(DBG_WARN, "XYZV3::checkForConfigOK expected 'ok', got '%s'", buf);
	}
	else
		debugPrint(DBG_WARN, "XYZV3::checkForConfigOK expected 'ok', got nothing");

	return false;
}

bool XYZV3::waitForConfigOK(bool endCom, float timeout_s)
{
	debugPrint(DBG_LOG, "XYZV3::waitForConfigOK(%0.2f)", timeout_s);

	if(isWIFI())
		return true;

	// else check for ok

	const char *buf = waitForLine(timeout_s);
	if(*buf)
	{
		if(0 == strcmp("ok", buf))
		{
			if(endCom && !waitForEndCom())
				debugPrint(DBG_WARN, "XYZV3::waitForConfigOK missing end com");

			return true;
		}
		else if(buf[0] == 'E')
		{
			debugPrint(DBG_WARN, "XYZV3::waitForConfigOK got error instead '%s'", buf);
			if(endCom && !waitForEndCom())
				debugPrint(DBG_WARN, "XYZV3::waitForConfigOK missing end com");
		}
		else
			debugPrint(DBG_WARN, "XYZV3::waitForConfigOK expected 'ok', got '%s'", buf);
	}
	else
		debugPrint(DBG_WARN, "XYZV3::waitForConfigOK expected 'ok', got nothing");

	return false;
}

bool XYZV3::setZOffset(int offset)
{
	debugPrint(DBG_LOG, "XYZV3::setZOffset(%d)", offset);

	bool success = 
		serialSendMessage("XYZv3/config=zoffset:set[%d]", offset) &&
		waitForConfigOK(); // not returned on wifi

	return success;
}

bool XYZV3::restoreDefaults()
{
	debugPrint(DBG_LOG, "XYZV3::restoreDefaults()");

	bool success = 
		serialSendMessage("XYZv3/config=restoredefault:on") &&
		waitForConfigOK(); // not returned on wifi
		//****Note, XYZWare sets energy to 3 at this point

	return success;
}

bool XYZV3::setBuzzer(bool enable)
{
	debugPrint(DBG_LOG, "XYZV3::setBuzzer(%d)", enable);

	bool success = 
		serialSendMessage("XYZv3/config=buzzer:%s", (enable) ? "on" : "off") &&
		waitForConfigOK(); // not returned on wifi

	return success;
}

bool XYZV3::setAutoLevel(bool enable)
{
	debugPrint(DBG_LOG, "XYZV3::setAutoLevel(%d)", enable);

	bool success = 
		serialSendMessage("XYZv3/config=autolevel:%s", (enable) ? "on" : "off") &&
		waitForConfigOK(); // not returned on wifi

	return success;
}

bool XYZV3::setLanguage(const char *lang)
{
	debugPrint(DBG_LOG, "XYZV3::setLanguage(%s)", lang);

	bool success = 
		lang && 
		serialSendMessage("XYZv3/config=lang:[%s]", lang) &&
		waitForConfigOK(); // not returned on wifi

	return success;
}

// level is 0-9 minutes till lights turn off
// XYZWare sets this to 0,3,6
bool XYZV3::setEnergySaving(int level)
{
	debugPrint(DBG_LOG, "XYZV3::setEnergySaving(%d)", level);

	bool success = 
		serialSendMessage("XYZv3/config=energy:[%d]", level) &&
		waitForConfigOK(); // not returned on wifi

	return success;
}

bool XYZV3::sendDisconnectWifi()
{
	debugPrint(DBG_LOG, "XYZV3::sendDisconnectWifi()");

	bool success = 
		serialSendMessage("XYZv3/config=disconnectap") &&
		waitForConfigOK(); // not returned on wifi??

	return success;
}

bool XYZV3::sendEngraverPlaceObject()
{
	debugPrint(DBG_LOG, "XYZV3::sendEngraverPlaceObject()");

	bool success = 
		serialSendMessage("XYZv3/config=engrave:placeobject") &&
		waitForConfigOK(); // not returned on wifi???

	return success;
}

bool XYZV3::setMachineLife(int time_s)
{
	debugPrint(DBG_LOG, "XYZV3::setMachineLife(%d)", time_s);

	bool success = 
		serialSendMessage("XYZv3/config=life:[%d]", time_s) &&
		waitForConfigOK(); // not returned on wifi

	return success;
}

bool XYZV3::setMachineName(const char *name)
{
	debugPrint(DBG_LOG, "XYZV3::setMachineName(%s)", name);

	bool success = 
		name && 
		serialSendMessage("XYZv3/config=name:[%s]", name) &&
		waitForConfigOK(); // not returned on wifi

	return success;
}

bool XYZV3::setWifi(const char *ssid, const char *password, int channel)
{
	debugPrint(DBG_LOG, "XYZV3::setWifi(%s, %s, %d)", ssid, password, channel);

	//****FixMe, move to state machine
	bool success = 
		ssid && 
		password && 
		serialSendMessage("XYZv3/config=ssid:[%s,%s,%d]", ssid, password, channel) &&
		waitForConfigOK(true, 25.0f); // not returned on wifi

	return success;
}

// === upload commands ===

// print a gcode or 3w file, convert as needed
void XYZV3::printFileStart(const char *path)
{
	debugPrint(DBG_LOG, "XYZV3::printFileStart(%s)", path);
	m_progress = 0;

	if(path)
	{
		strncpy(m_filePath, path, MAX_PATH);
		m_filePath[MAX_PATH-1] = '\0';
		m_fileIsTemp = false;
		setState(ACT_PF_START);
	}
	else
		debugPrint(DBG_WARN, "XYZV3::printFileStart invalid input");
}

bool XYZV3::printFileRun()
{
	debugPrint(DBG_LOG, "XYZV3::printFileRun() %d", m_actState);

	if(!m_stream || !m_stream->isOpen())
		setState(ACT_FAILURE);

	switch(m_actState)
	{
	case ACT_PF_START:
		// if gcode convert to 3w file
		if(isGcodeFile(m_filePath))
		{
			char tpath[MAX_PATH] = "";
			char tfile[MAX_PATH] = "";
			if( GetTempPath(MAX_PATH, tpath) &&
				GetTempFileName(tpath, NULL, 0, tfile))
			{
				if(encryptFile(m_filePath, tfile, -1))
				{
					strcpy(m_filePath, tfile);
					m_fileIsTemp = true;
				}
			}
		}
		// else if  3w just use file directly
		else if(is3wFile(m_filePath))
		{
			//****FixMe, check if file format matches printer
			// if not then decode to temp file and re-encode
		}

		if(m_filePath[0])
			setState(ACT_PF_SEND);
		else setState(ACT_FAILURE);
		break;
	case ACT_PF_SEND:
		if(sendFileInit(m_filePath, true))
			setState(ACT_PF_SEND_PROCESS);
		else
			setState(ACT_FAILURE);
		break;
	case ACT_PF_SEND_PROCESS:
		if(!sendFileProcess())
			setState(ACT_PF_COMPLETE);
		m_progress = (int)(10.0 + 90.0f * getFileUploadPct());
		break;
	case ACT_PF_COMPLETE:
		if(sendFileFinalize())
		{
			// cleanup temp file, if used
			if(m_fileIsTemp)
				remove(m_filePath);

			setState(ACT_SUCCESS);
		}
		else
			setState(ACT_FAILURE);
		break;
	default:
		break;
	}

	// return true if not error or done
	return m_actState != ACT_FAILURE && m_actState != ACT_SUCCESS;
}

bool XYZV3::cancelPrint()
{
	debugPrint(DBG_LOG, "XYZV3::cancelPrint()");

	bool success = 
		serialSendMessage("XYZv3/config=print[cancel]") &&
		waitForConfigOK(); // not returned on wifi

	return success;
}

bool XYZV3::pausePrint()
{
	debugPrint(DBG_LOG, "XYZV3::pausePrint()");

	bool success = 
		serialSendMessage("XYZv3/config=print[pause]") &&
		waitForConfigOK(); // not returned on wifi

	return success;
}

bool XYZV3::resumePrint()
{
	debugPrint(DBG_LOG, "XYZV3::resumePrint()");

	bool success = 
		serialSendMessage("XYZv3/config=print[resume]") &&
		waitForConfigOK(); // not returned on wifi

	return success;
}

// call when print finished to prep for a new job
// switches from print done to print ready
// be sure old job is off print bed!!!
bool XYZV3::readyPrint()
{
	debugPrint(DBG_LOG, "XYZV3::readyPrint()");

	bool success = 
		serialSendMessage("XYZv3/config=print[complete]") &&
		waitForConfigOK(); // not returned on wifi

	return success;
}

//****FixMe, implement these
/*
XYZv3/config=pda:[1591]
XYZv3/config=pdb:[4387]
XYZv3/config=pdc:[7264]
XYZv3/config=pde:[8046]
*/

// print a gcode or 3w file, convert as needed
void XYZV3::uploadFirmwareStart(const char *path)
{
	debugPrint(DBG_LOG, "XYZV3::uploadFirmwareStart(%s)", path);
	m_progress = 0;

	if(path)
	{
		strncpy(m_filePath, path, MAX_PATH);
		m_filePath[MAX_PATH-1] = '\0';
		setState(ACT_FW_START);
	}
	else
		debugPrint(DBG_WARN, "XYZV3::uploadFirmwareStart invalid input");
}

bool XYZV3::uploadFirmwareRun()
{
	debugPrint(DBG_LOG, "XYZV3::uploadFirmwareRun() %d", m_actState);

	if(!m_stream || !m_stream->isOpen())
		setState(ACT_FAILURE);

	switch(m_actState)
	{
	case ACT_FW_START:
		if(sendFileInit(m_filePath, false))
			setState(ACT_FW_SEND_PROCESS);
		else
			setState(ACT_FAILURE);
		break;
	case ACT_FW_SEND_PROCESS:
		if(!sendFileProcess())
			setState(ACT_PF_COMPLETE);
		m_progress = (int)(10.0 + 90.0f * getFileUploadPct());
		break;
	case ACT_FW_COMPLETE:
		if(sendFileFinalize())
			setState(ACT_SUCCESS);
		else
			setState(ACT_FAILURE);
		break;
	default:
		break;
	}

	// return true if not error or done
	return m_actState != ACT_FAILURE && m_actState != ACT_SUCCESS;
}

bool XYZV3::sendFileInit(const char *path, bool isPrint)
{
	debugPrint(DBG_LOG, "XYZV3::sendFileInit(%s, %d)", path, isPrint);

	assert(!pDat.isPrintActive);

	memset(&pDat, 0, sizeof(pDat));

	// internal options, expose these at some point
	bool saveToSD = false; // set to true to permanently save 3w file to internal SD card
	bool downgrade = false; // set to true to remove version check on firmware?

	bool success = false;

	if(m_stream && m_stream->isOpen() && path)
	{
		// try to load file from disk
		char *buf = NULL;
		FILE *f = fopen(path, "rb");
		if(f)
		{
			// get file length
			fseek(f, 0, SEEK_END);
			int len = ftell(f);
			if(!isPrint)
				len -= 16; // skip header on firmware
			fseek(f, 0, SEEK_SET);
			if(len > 0)
			{
				buf = new char[len];
				if(buf)
				{
					// firmware has 16 byte header as a string
					char header[17];
					if((isPrint || 16 == fread(header, 1, 16, f)) &&
						len == (int)fread(buf, 1, len, f)) // read whole file into bufer
					{
						// zero terminate header string
						//****Note, only valid if !isPrint
						header[16] = '\0';

						// now we have a buffer, go ahead and start to print it
						pDat.blockSize = (m_status.isValid) ? m_status.oPacketSize : 8192;
						pDat.blockCount = (len + pDat.blockSize - 1) / pDat.blockSize; // round up
						pDat.lastBlockSize = len % pDat.blockSize;
						pDat.curBlock = 0;
						pDat.data = buf;
						pDat.blockBuf = new char[pDat.blockSize + 12];

						if(pDat.blockBuf)
						{
							if(isPrint)
								serialSendMessage("XYZv3/upload=temp.gcode,%d%s\n", len, (saveToSD) ? ",SaveToSD" : "");
							else
								serialSendMessage("XYZv3/firmware=temp.bin,%d%s\n", len, (downgrade) ? ",Downgrade" : "");

							if(waitForConfigOK(false))
							{
								pDat.isPrintActive = true;
								success = true;
							}
							else
								debugPrint(DBG_WARN, "XYZV3::sendFileInit failed to send data");
						}
						else
							debugPrint(DBG_WARN, "XYZV3::sendFileInit failed to allocate buffer");
					}

					if(!success)
					{
						if(buf)
							delete [] buf;
						buf = NULL;
						pDat.data = NULL;

						if(pDat.blockBuf)
							delete [] pDat.blockBuf;
						pDat.blockBuf = NULL;

						debugPrint(DBG_WARN, "XYZV3::sendFileInit failed");
					}
				}
				else
					debugPrint(DBG_WARN, "XYZV3::sendFileInit failed to allocate buffer");
			}
			else
				debugPrint(DBG_WARN, "XYZV3::sendFileInit failed file has no data");

			// close file if open
			fclose(f);
			f = NULL;
		}
		else
			debugPrint(DBG_WARN, "XYZV3::sendFileInit failed to open file %s", path);
	}
	else
		debugPrint(DBG_WARN, "XYZV3::sendFileInit invalid input");

	return success;
}

float XYZV3::getFileUploadPct()
{
	debugPrint(DBG_LOG, "XYZV3::getFileUploadPct()");

	if(pDat.isPrintActive && pDat.blockCount > 0)
		return (float)pDat.curBlock/(float)pDat.blockCount;

	return 0;
}

bool XYZV3::sendFileProcess()
{
	debugPrint(DBG_LOG, "XYZV3::sendFileProcess()");

	assert(pDat.isPrintActive);

	bool success = false;
	if(m_stream && m_stream->isOpen() && pDat.data && pDat.blockBuf)
	{
		int i, t;
		int bMax = pDat.blockCount;
		if(bMax > pDat.curBlock + 4)
			bMax = pDat.curBlock + 4;

		for(i=pDat.curBlock; i<bMax; i++)
		{
			int blockLen = (i+1 == pDat.blockCount) ? pDat.lastBlockSize : pDat.blockSize;
			char *tBuf = pDat.blockBuf;

			// block count
			t = swap32bit(i);
			memcpy(tBuf, &t, 4);
			tBuf += 4;

			// data length
			t = swap32bit(blockLen);
			memcpy(tBuf, &t, 4);
			tBuf += 4;

			// data block
			memcpy(tBuf, pDat.data + i*pDat.blockSize, blockLen);
			tBuf += blockLen;

			// end of data
			t = swap32bit(0);
			memcpy(tBuf, &t, 4);
			tBuf += 4;

			// write out in one shot
			m_stream->write(pDat.blockBuf, blockLen + 12);
			success = waitForConfigOK(false);
			if(!success) // bail on error
			{
				debugPrint(DBG_WARN, "XYZV3::sendFileProcess failed on write");
				break;
			}
		}

		pDat.curBlock = i;
	}
	else
		debugPrint(DBG_WARN, "XYZV3::sendFileProcess invalid input");

	return success;
}

bool XYZV3::sendFileFinalize()
{
	debugPrint(DBG_LOG, "XYZV3::sendFileFinalize()");

	assert(pDat.isPrintActive);

	if(pDat.data)
		delete [] pDat.data;
	pDat.data = NULL;

	if(pDat.blockBuf)
		delete [] pDat.blockBuf;
	pDat.blockBuf = NULL;

	pDat.isPrintActive = false;

	// close out printing
	bool success = 
		serialSendMessage("XYZv3/uploadDidFinish") &&
		waitForConfigOK(false);

	return success;
}

// === file i/o ===

// print a gcode or 3w file, convert as needed
void XYZV3::convertFileStart(const char *inPath, const char *outPath, int infoIdx)
{
	debugPrint(DBG_LOG, "XYZV3::convertFileStart(%s, %s, %d)", inPath, outPath, infoIdx);
	m_progress = 0;

	if(inPath)
	{
		strncpy(m_filePath, inPath, MAX_PATH);
		m_filePath[MAX_PATH-1] = '\0';

		m_fileOutPath[0] = '\0';
		if(outPath)
		{
			strncpy(m_fileOutPath, outPath, MAX_PATH);
			m_fileOutPath[MAX_PATH-1] = '\0';
		}

		m_infoIdx = infoIdx;

		m_fileIsTemp = false;
		setState(ACT_CF_START);
	}
	else
		debugPrint(DBG_WARN, "XYZV3::convertFileStart invalid input");
}

bool XYZV3::convertFileRun()
{
	debugPrint(DBG_LOG, "XYZV3::convertFileRun() %d", m_actState);

	// convert file can run without a connection to printer 
	//if(!m_stream || !m_stream->isOpen())
	//	setState(ACT_FAILURE);

	switch(m_actState)
	{
	case ACT_CF_START:
		// if gcode convert to 3w file
		if(isGcodeFile(m_filePath))
		{
			if(encryptFile(m_filePath, m_fileOutPath, m_infoIdx))
				setState(ACT_CF_COMPLETE);
			else setState(ACT_FAILURE);
		}
		// else if  3w just use file directly
		else if(is3wFile(m_filePath))
		{
			if(decryptFile(m_filePath, m_fileOutPath))
				setState(ACT_CF_COMPLETE);
			else setState(ACT_FAILURE);
		}
		else setState(ACT_FAILURE);
		break;
	case ACT_CF_COMPLETE:
		m_progress = 100;
		setState(ACT_SUCCESS);
		break;
	default:
		break;
	}

	// return true if not error or done
	return m_actState != ACT_FAILURE && m_actState != ACT_SUCCESS;
	// convert file can run without a connection to printer 
}

bool XYZV3::isGcodeFile(const char *path)
{
	debugPrint(DBG_LOG, "XYZV3::isGcodeFile(%s)", path);

	if(path)
	{
		const char *p = strrchr(path, '.');
		if(p)
			return 0 == strcmp(p, ".gcode") ||
				   0 == strcmp(p, ".gco") ||
				   0 == strcmp(p, ".g");
	}
	else
		debugPrint(DBG_WARN, "XYZV3::isGcodeFile invalid input");

	return false;
}

bool XYZV3::is3wFile(const char *path)
{
	debugPrint(DBG_LOG, "XYZV3::is3wFile(%s)", path);

	if(path)
	{
		const char *p = strrchr(path, '.');
		if(p)
			return 0 == strcmp(p, ".3w");
	}
	else
		debugPrint(DBG_WARN, "XYZV3::is3wFile invalid input");

	return false;
}

bool XYZV3::encryptFile(const char *inPath, const char *outPath, int infoIdx)
{
	debugPrint(DBG_LOG, "XYZV3::encryptFile(%s, %s, %d)", (inPath) ? inPath : "", (outPath) ? outPath : "", infoIdx);

	//msTimer t;
	bool success = false;

	// encrypt to currently connected printer
	const XYZPrinterInfo *info = (m_stream && m_stream->isOpen()) ? m_info : NULL; 
	// unless the user requests a particular printer model
	if(infoIdx > -1)
		info = XYZV3::indexToInfo(infoIdx);
#ifdef _DEBUG
	if(!info)
		info = XYZV3::modelToInfo("dv1MX0A000");		// miniMaker
		//info = XYZV3::modelToInfo("dvF100B000");		// 1.0
#endif

	if(info && inPath && *inPath)
	{
		const char *fileNum = info->fileNum;
		bool fileIsV5 = info->fileIsV5;
		bool fileIsZip = info->fileIsZip;

		// open our source file
		FILE *fi = fopen(inPath, "rb");
		if(fi)
		{
			char tPath[MAX_PATH] = "";
			// and write to disk
			FILE *fo = NULL;
			if(outPath && *outPath)
				fo = fopen(outPath, "wb");
			else
			{
				strcpy(tPath, inPath);
				char *ptr = strrchr(tPath, '.');
				if(!ptr)
					ptr = tPath + strlen(tPath);
				sprintf(ptr, ".3w");
				fo = fopen(tPath, "wb");
			}

			// and our destination file
			if(fo)
			{
				//==============================
				// process the source gcode file

				// get file length
				fseek(fi, 0, SEEK_END);
				int gcodeLen = ftell(fi);
				fseek(fi, 0, SEEK_SET);

				// grab gcode file data
				char *gcode = new char[gcodeLen+1];
				if(gcode)
				{
					fread(gcode, 1, gcodeLen, fi);
					gcode[gcodeLen] = '\0';

					// convert G0 commands to G1
					// strip out print time info from header
					// add back in print time info in xyz format
					// work out length of header
					// padd full file to 16 byte boundary buffer
					char *processedBuf = NULL;
					int processHeaderLen = 0;
					int processTotalLen = 0;

					if(processGCode(gcode, gcodeLen, fileNum, &processedBuf, &processHeaderLen, &processTotalLen))
					{
						char *headerBuf = NULL;
						int headerLen = 0;

						if(encryptHeader(processedBuf, processHeaderLen, fileIsV5, &headerBuf, &headerLen))
						{
							char *bodyBuf = NULL;
							int bodyLen = 0;

							if(encryptBody(processedBuf, processTotalLen, fileIsZip, &bodyBuf, &bodyLen))
							{
								if(writeFile(fo, fileIsV5, fileIsZip, headerBuf, headerLen, bodyBuf, bodyLen))
								{
									// yeay, it worked
									success = true;
								}
								else
									debugPrint(DBG_WARN, "XYZV3::encryptFile failed to write body");

								// cleanup
								delete [] bodyBuf;
								bodyBuf = NULL;
							}
							else
								debugPrint(DBG_WARN, "XYZV3::encryptFile failed to encrypt body");

							// cleanup
							delete [] headerBuf;
							headerBuf = NULL;
						}
						else
							debugPrint(DBG_WARN, "XYZV3::encryptFile failed to encrypt header");

						// cleanup
						delete [] processedBuf;
						processedBuf = NULL;
					}
					else
						debugPrint(DBG_WARN, "XYZV3::encryptFile failed to process gcode");

					delete [] gcode;
					gcode = NULL;
				}
				else
					debugPrint(DBG_WARN, "XYZV3::encryptFile failed to allocate buffer");

				// clean up file data
				fclose(fo);
				fo = NULL;
			}
			else
				debugPrint(DBG_WARN, "XYZV3::encryptFile failed to open out file %s", (outPath) ? outPath : tPath);

			fclose(fi);
			fi = NULL;
		}
		else
			debugPrint(DBG_WARN, "XYZV3::encryptFile failed to open in file %s", inPath);
	}
	else
		debugPrint(DBG_WARN, "XYZV3::encryptFile invalid input");

	//debugPrint(DBG_REPORT, "Encrypt File took %0.2f s", t.getElapsedTime_s());
	return success;
}

bool XYZV3::decryptFile(const char *inPath, const char *outPath)
{
	debugPrint(DBG_LOG, "XYZV3::decryptFile(%s, %s)", inPath, outPath);
	//msTimer t;

	bool success = false;

	if(inPath)
	{
		FILE *f = fopen(inPath, "rb");
		if(f)
		{
			char tPath[MAX_PATH];
			// and write to disk
			FILE *fo = NULL;
			if(outPath)
				fo = fopen(outPath, "wb");
			else
			{
				strcpy(tPath, inPath);
				char *ptr = strrchr(tPath, '.');
				if(!ptr)
					ptr = tPath + strlen(tPath);
				sprintf(ptr, ".gcode");
				fo = fopen(tPath, "wb");
			}

			if(fo)
			{
				char buf[64];

				// get file length
				fseek(f, 0, SEEK_END);
				int totalLen = ftell(f);
				fseek(f, 0, SEEK_SET);

				// check if this is even a 3w file
				fread(buf, 1, 12, f);
				if(0 == strncmp("3DPFNKG13WTW", buf, strlen("3DPFNKG13WTW")))
				{
					// file format id's
					// id should be 1, what does this mean?
					// file format version is 2 or 5
					fread(buf, 1, 4, f);
					int id = buf[0];
					(void)id; // currently not used

					int fv = buf[1];

					bool fileIsV5 = (fv == 5);

					// offset to zip marker
					int zipOffset = readWord(f);
					fseek(f, zipOffset, SEEK_CUR);

					// zip format marker
					fread(buf, 1, 8, f);
					bool fileIsZip = false;
					if(0 == strncmp("TagEa128", buf, strlen("TagEa128")))
						fileIsZip = true;
					else if(0 == strncmp("TagEJ256", buf, strlen("TagEJ256")))
						fileIsZip = false;
					//else error

					// optional header len
					int headerLen = (fileIsV5) ? readWord(f) : -1;
					(void)headerLen;

					// offset to header
					int headerOffset = readWord(f);

					int offset1 = ftell(f);

					//?? 
					int id2 = (fileIsV5) ? readWord(f) : -1;
					(void)id2;

					int crc32 = readWord(f);

					int offset = headerOffset - (ftell(f) - offset1);
					fseek(f, offset, SEEK_CUR);

					// header potentially goes from here to 8192
					// v5 format stores header len, but v2 files you just have
					// to search for zeros to indicate the end...
					//****Note, header is duplicated in body, for now just skip it
					// you can uncomment this if you want to verify this is the case
//#define PARCE_HEADER
#ifdef PARCE_HEADER
					if(headerLen < 1)
						headerLen = m_bodyOffset - ftell(f);
					char *hbuf = new char[headerLen + 1];
					if(hbuf)
					{
						fread(hbuf, 1, headerLen, f);
						hbuf[headerLen] = '\0';

						// search from end to remove zeros
						while(headerLen > 1)
						{
							if(hbuf[headerLen-1] != 0)
								break;
							headerLen--;
						}

						//****FixMe, V5 files from XYZMaker have unencrypted header
						// but what marks that as the case and how do we detect it
						// for now assume a comment indicates we are not encrypted
						if(hbuf[0] != ';')
						{
							// decrypt header
							struct AES_ctx ctx;
							uint8_t iv[16] = {0}; // 16 zeros
							char *key = "@xyzprinting.com";
							AES_init_ctx_iv(&ctx, key, iv);
							AES_CBC_decrypt_buffer(&ctx, (uint8_t*)hbuf, headerLen);
						}

						// remove any padding from header
						headerLen = pkcs7unpad(hbuf, headerLen);

						//****FixMe, do something with it
						debugPrint(DBG_VERBOSE, hbuf);

						delete [] hbuf;
						hbuf = NULL;
					}
#endif

					// body contains duplicate header and body of gcode file
					// and always starts at 8192 (0x2000)

					fseek(f, m_bodyOffset, SEEK_SET);
					int bodyLen = totalLen - ftell(f);
					int bufLen = bodyLen + 1;
					char *bBuf = new char[bufLen];
					if(bBuf)
					{
						memset(bBuf, 0, bufLen);
						fread(bBuf, 1, bodyLen, f);
						bBuf[bodyLen] = '\0';

						if(crc32 != (int)calcXYZcrc32(bBuf, bodyLen))
							debugPrint(DBG_WARN, "XYZV3::decryptFile crc's don't match!!!");

						if(fileIsZip)
						{
							// allocate a buf to hold the zip file
							char *zbuf = new char[bufLen];
							if(zbuf)
							{
								// decrypt body
								struct AES_ctx ctx;
								uint8_t iv[16] = {0}; // 16 zeros
								const char *key = "@xyzprinting.com";

								int readOffset = 0;
								int writeOffset = 0;
								const int blockLen = 0x2010; // block grows by 16 bytes when pkcs7 padding is applied

								//msTimer t1;
								// decrypt in blocks
								for(readOffset = 0; readOffset < bodyLen; readOffset += blockLen)
								{
									// last block is smaller, so account for it
									int len = ((bodyLen - readOffset) < blockLen) ? (bodyLen - readOffset) : blockLen;

									// reset decrypter every block
									AES_init_ctx_iv(&ctx, key, iv);

									//****FixMe, loop over this in blocks of 1,000
									AES_CBC_decrypt_buffer(&ctx, (uint8_t*)bBuf+readOffset, len);

									// remove any padding from body
									len = pkcs7unpad(bBuf+readOffset, len);

									// and stash in new buf
									memcpy(zbuf+writeOffset, bBuf+readOffset, len);
									writeOffset += len;
								}
								//debugPrint(DBG_REPORT, "CBC Decrypt took %0.2f s", t1.getElapsedTime_s());

								//msTimer t2;
								mz_zip_archive zip;
								memset(&zip, 0, sizeof(zip));
								if(mz_zip_reader_init_mem(&zip, zbuf, writeOffset, 0))
								{
									int numFiles = mz_zip_reader_get_num_files(&zip);
									if(numFiles == 1)
									{
										const int tstr_len = 512;
										char tstr[tstr_len];
										if(mz_zip_reader_get_filename(&zip, 0, tstr, tstr_len))
											debugPrint(DBG_LOG, "XYZV3::decryptFile zip file name '%s'", tstr);

										//****FixMe, replace with mz_zip_reader_extract_iter_new
										size_t size = 0;
										char *tbuf = (char*)mz_zip_reader_extract_to_heap(&zip, 0, &size, 0);
										if(tbuf)
										{
											fwrite(tbuf, 1, size, fo);
											success = true;

											mz_free(tbuf);
											tbuf = NULL;
										}
										else
											debugPrint(DBG_WARN, "XYZV3::decryptFile error %d", zip.m_last_error);
									}
									else
										debugPrint(DBG_WARN, "XYZV3::decryptFile error numfiles is %d", numFiles);

									mz_zip_reader_end(&zip);
								}
								else
									debugPrint(DBG_WARN, "XYZV3::decryptFile error %s", zip.m_last_error);

								delete [] zbuf;
								zbuf = NULL;
								//debugPrint(DBG_REPORT, "Unzip took %0.2f s", t2.getElapsedTime_s());
							}
						}
						else
						{
							// first char in an unencrypted file will be ';'
							// so check if no encrypted, v5 files sometimes are not encrypted
							if(bBuf[0] != ';')
							{
								//msTimer t1;
								// decrypt body
								struct AES_ctx ctx;
								uint8_t iv[16] = {0}; // 16 zeros
								const char *key = "@xyzprinting.com@xyzprinting.com";
								AES_init_ctx_iv(&ctx, key, iv);

								// decrypt in blocks
								const int blockLen = 0x2010;
								for(int readOffset = 0; readOffset < bodyLen; readOffset += blockLen)
								{
									// last block is smaller, so account for it
									int len = ((bodyLen - readOffset) < blockLen) ? (bodyLen - readOffset) : blockLen;

									//****FixMe, loop over this in blocks of 1,000
									AES_ECB_decrypt_buffer(&ctx, (uint8_t*)(bBuf + readOffset), len);
								}
								//debugPrint(DBG_REPORT, "EBC Decrypt took %0.2f s", t1.getElapsedTime_s());
							}

							// remove any padding from body
							bodyLen = pkcs7unpad(bBuf, bodyLen);

							fwrite(bBuf, 1, bodyLen, fo);
							success = true;
						}

						delete [] bBuf;
						bBuf = NULL;
					}
					else
						debugPrint(DBG_WARN, "XYZV3::decryptFile failed to allocate buffer");
				}

				fclose(fo);
				fo = NULL;
			}
			else
				debugPrint(DBG_WARN, "XYZV3::decryptFile failed to open out file %s", (outPath) ? outPath : tPath);

			fclose(f);
		}
		else
			debugPrint(DBG_WARN, "XYZV3::decryptFile failed to open in file %s", inPath);
	}
	else
		debugPrint(DBG_WARN, "XYZV3::decryptFile invalid input");

	//debugPrint(DBG_REPORT, "Decrypt File took %0.2f s", t.getElapsedTime_s());
	return success;
}

//------------------------------------------

//****Note, errors can be E0\n$\n or E4$\n, success is often just $\n or ok\n$\n
// need to deal with all variants
//****FixMe, does any wifi function return $, maybe we should chop it off here
bool XYZV3::waitForEndCom()
{
	debugPrint(DBG_VERBOSE, "XYZV3::waitForEndCom()");

	// check for '$' indicating end of message
	const char *buf = waitForLine(0.1f);
	if(buf)
	{
		if(buf[0] == '$')
		{
			return true;
			// success
		}
		else if(buf[0] == 'E')
			debugPrint(DBG_WARN, "XYZV3::waitForEndCom $ failed, got error '%s'", buf);
		else
			debugPrint(DBG_WARN, "XYZV3::waitForEndCom $ failed, instead got '%s'", buf);
	}
	else
		debugPrint(DBG_WARN, "XYZV3::waitForEndCom $ failed, got nothing");

	return false;
}

const char* XYZV3::checkForLine()
{
	debugPrint(DBG_VERBOSE, "XYZV3::checkForLine()");

	static const int len = 1024;
	static char buf[len]; //****Note, this buffer is overwriten every time you call checkForLine!!!
	*buf = '\0';

	if(m_stream && m_stream->isOpen())
	{
		if(m_stream->readLine(buf, len))
		{
			if(buf[0] == '$' || buf[0] == 'E')
			{
				debugPrint(DBG_WARN, "XYZV3::checkForLine failed, got early '%s'", buf);
				return "";
			}

			return buf;
		}
		else
			debugPrint(DBG_VERBOSE, "XYZV3::checkForLine failed, timed out");
	}
	else
		debugPrint(DBG_WARN, "XYZV3::checkForLine failed, connection closed");

	return "";
}

const char* XYZV3::waitForLine(float timeout_s)
{
	debugPrint(DBG_VERBOSE, "XYZV3::waitForLine(%0.2f)", timeout_s);

	static const int len = 1024;
	static char buf[len]; //****Note, this buffer is overwriten every time you call waitForLine!!!
	*buf = '\0';

	if(m_stream && m_stream->isOpen())
	{
		if(timeout_s < 0)
			timeout_s = m_stream->getDefaultTimeout();

		bool done = false;
		msTimeout timeout(timeout_s);
		do
		{
			// blocking call, no need to sleep
			if(m_stream->readLine(buf, len))
				done = true;
		} 
		while(!done && !timeout.isTimeout() && m_stream && m_stream->isOpen());

		if(done)
			return buf;
		else
			debugPrint(DBG_WARN, "XYZV3::waitForLine failed, timed out 0.2f", timeout_s);
	}
	else
		debugPrint(DBG_WARN, "XYZV3::waitForLine failed, connection closed");

	return "";
}

bool XYZV3::checkForJsonState(const char **val)
{
	const static char *key = "stat";
	debugPrint(DBG_VERBOSE, "XYZV3::checkForJsonState(%s)", key);

	if(val)
	{
		*val = NULL;
		static const int len = 1024;
		static char tVal[len]; //****Warning, returned by function!

		const char *buf = checkForLine();
		if(*buf)
		{
			waitForEndCom();
			if(findJsonVal(buf, key, tVal) && *tVal)
			{
				if(*tVal == '"')
					*val = tVal + 1;
				else 
					*val = tVal;

				debugPrint(DBG_LOG, "XYZV3::checkForJsonState match found");
				return true;
			}
			else
				debugPrint(DBG_WARN, "XYZV3::checkForJsonState expected '%s', got '%s'", key, buf);
		}
		else
			debugPrint(DBG_VERBOSE, "XYZV3::checkForJsonState expected '%s', got nothing", key);
	}
	else
		debugPrint(DBG_WARN, "XYZV3::checkForJsonState invalid input");

	return false;
}

bool XYZV3::jsonValEquals(const char *tVal, const char *val)
{
	debugPrint(DBG_LOG, "XYZV3::jsonValEquals(%s, %s)", tVal, val);

	if(tVal && val)
	{
		// check for exact match
		if(0 == strncmp(val, tVal, strlen(val)))
		{
			debugPrint(DBG_LOG, "XYZV3::jsonValEquals match found");
			return true;
		}
		else
			debugPrint(DBG_WARN, "XYZV3::jsonValEquals expected '%s', got '%s'", val, tVal);
	}
	else
		debugPrint(DBG_WARN, "XYZV3::jsonValEquals invalid input");

	return false;
}

bool XYZV3::findJsonVal(const char *str, const char *key, char *val)
{
	debugPrint(DBG_VERBOSE, "XYZV3::findJsonVal(%s, %s, %s)", str, key, val);

	if(str && key && val)
	{
		*val = '\0';

		char fullKey[256];
		sprintf(fullKey, "\"%s\":", key);
		const char *offset = strstr(str, fullKey);

		if(offset)
		{
			offset += strlen(fullKey);
		
			sscanf(offset, "%[^,}]", val);
			if(val)
			{
				// strip quotes, if found
				if(*val == '"')
				{
					int len = strlen(val);
					for(int i=0; i<len; i++)
						val[i] = val[i+1];
				}

				if(val[strlen(val)-1] == '"')
					val[strlen(val)-1] = '\0';

				if(*val)
					return true;
			}
			else
				debugPrint(DBG_WARN, "XYZV3::findJsonVal failed to find value '%s:%s'", key, val);
		}
		else
			debugPrint(DBG_VERBOSE, "XYZV3::findJsonVal failed to find key '%s:%s'", key, val);
	}
	else
		debugPrint(DBG_WARN, "XYZV3::findJsonVal invalid input");

	return false;
}

bool XYZV3::checkForState(int state, int substate, bool isSet)
{
	debugPrint(DBG_VERBOSE, "XYZV3::checkForState(%d, %d, %d)", state, substate, isSet);

	bool success = false;

	// only need to update state
	if(queryStatus(false, -1, 'j'))
	{
		if(isSet && (m_status.jPrinterState == state && (substate < 0 || m_status.jPrinterSubState == substate)))
		{
			success = true;
			debugPrint(DBG_LOG, "XYZV3::checkForState %d:%d success found stat %d:%d", state, substate, m_status.jPrinterState, m_status.jPrinterSubState);
		}
		else if(!isSet && !(m_status.jPrinterState == state && (substate < 0 || m_status.jPrinterSubState == substate)))
		{
			success = true;
			debugPrint(DBG_LOG, "XYZV3::checkForState %d:%d success found stat %d:%d", state, substate, m_status.jPrinterState, m_status.jPrinterSubState);
		}
		else
			debugPrint(DBG_WARN, "XYZV3::checkForState %d:%d but stat is %d:%d", state, substate, m_status.jPrinterState, m_status.jPrinterSubState);
	}
	else
		debugPrint(DBG_WARN, "XYZV3::checkForState queryStatus() failed");

	return success;
}

unsigned int XYZV3::swap16bit(unsigned int in)
{
	return ((in >> 8) & 0x00FF) | ((in << 8) & 0xFF00);
}

unsigned int XYZV3::swap32bit(unsigned int in)
{
	return 
		((in >> 24) & 0x000000FF) |
		((in >> 8)  & 0x0000FF00) |
		((in << 8)  & 0x00FF0000) |
		((in << 24) & 0xFF000000) ;
}

// read a word from a file, in big endian format
int XYZV3::readWord(FILE *f)
{
	//debugPrint(DBG_VERBOSE, "XYZV3::readWord()");

	int i = 0;
	if(f)
	{
		fread(&i, 4, 1, f);
	}
	else
		debugPrint(DBG_WARN, "XYZV3::readWord invalid input");

	return swap32bit(i);
}

// write a word to a file, in big endian format
void XYZV3::writeWord(FILE *f, int i)
{
	//debugPrint(DBG_VERBOSE, "XYZV3::writeWord(%d)", i);

	if(f)
	{
		int t = swap32bit(i);
		fwrite(&t, 4, 1, f);
	}
	else
		debugPrint(DBG_WARN, "XYZV3::writeWord invalid input");
}

int XYZV3::readByte(FILE *f)
{
	//debugPrint(DBG_VERBOSE, "XYZV3::readByte()");

	char i = 0;
	if(f)
	{
		//****FixMe, test?
		fread(&i, 1, 1, f);
	}
	else
		debugPrint(DBG_WARN, "XYZV3::readByte invalid input");

	return (int)i;
}

void XYZV3::writeByte(FILE *f, char c)
{
	//debugPrint(DBG_VERBOSE, "XYZV3::writeByte(%d)", c);

	if(f)
	{
		//****FixMe, test?
		fwrite(&c, 1, 1, f);
	}
	else
		debugPrint(DBG_WARN, "XYZV3::writeByte invalid input");
}

void XYZV3::writeRepeatByte(FILE *f, char byte, int count)
{
	//debugPrint(DBG_VERBOSE, "XYZV3::writeRepeatByte(%d, %d", byte, count);

	if(f)
	{
		for(int i=0; i<count; i++)
		{
			fwrite(&byte, 1, 1, f);
		}
	}
	else
		debugPrint(DBG_WARN, "XYZV3::writeRepeatByte invalid input");
}

int XYZV3::roundUpTo16(int in)
{
	return (in + 15) & 0xFFFFFFF0;
}

int XYZV3::pkcs7unpad(char *buf, int len)
{
	//debugPrint(DBG_VERBOSE, "XYZV3::pkcs7unpad()");

	if(buf && len > 0)
	{
		int count = buf[len-1];
		if(count > 0 && count <= 16)
			buf[len-count] = '\0';

		return len - count;
	}
	else
		debugPrint(DBG_WARN, "XYZV3::pkcs7unpad invalid input");

	return len;
}

//****Note, expects buf to have room to be padded out
int XYZV3::pkcs7pad(char *buf, int len)
{
	//debugPrint(DBG_VERBOSE, "XYZV3::pkcs7pad()");

	if(buf && len > 0)
	{
		// force padding even if we are on a byte boundary
		int newLen = roundUpTo16(len+1);
		int count = newLen - len;

		if(count > 0 && count <= 16)
		{
			for(int i=0; i<count; i++)
			{
				buf[len+i] = (char)count;
			}
		}

		return newLen;
	}
	else
		debugPrint(DBG_WARN, "XYZV3::pkcs7pad invalid input");

	return len;
}

// XYZ's version of CRC32, does not seem to match the standard algorithms
unsigned int XYZV3::calcXYZcrc32(char *buf, int len)
{
	//debugPrint(DBG_VERBOSE, "XYZV3::calcXYZcrc32()");

	static const unsigned int hashTable[] = { 
		0, 1996959894, 3993919788, 2567524794, 124634137, 1886057615, 3915621685,
		2657392035, 249268274, 2044508324, 3772115230, 2547177864, 162941995, 
		2125561021, 3887607047, 2428444049, 498536548, 1789927666, 4089016648, 
		2227061214, 450548861, 1843258603, 4107580753, 2211677639, 325883990, 
		1684777152, 4251122042, 2321926636, 335633487, 1661365465, 4195302755, 
		2366115317, 997073096, 1281953886, 3579855332, 2724688242, 1006888145, 
		1258607687, 3524101629, 2768942443, 901097722, 1119000684, 3686517206, 
		2898065728, 853044451, 1172266101, 3705015759, 2882616665, 651767980, 
		1373503546, 3369554304, 3218104598, 565507253, 1454621731, 3485111705, 
		3099436303, 671266974, 1594198024, 3322730930, 2970347812, 795835527, 
		1483230225, 3244367275, 3060149565, 1994146192, 31158534, 2563907772, 
		4023717930, 1907459465, 112637215, 2680153253, 3904427059, 2013776290, 
		251722036, 2517215374, 3775830040, 2137656763, 141376813, 2439277719, 
		3865271297, 1802195444, 476864866, 2238001368, 4066508878, 1812370925, 
		453092731, 2181625025, 4111451223, 1706088902, 314042704, 2344532202, 
		4240017532, 1658658271, 366619977, 2362670323, 4224994405, 1303535960, 
		984961486, 2747007092, 3569037538, 1256170817, 1037604311, 2765210733, 
		3554079995, 1131014506, 879679996, 2909243462, 3663771856, 1141124467, 
		855842277, 2852801631, 3708648649, 1342533948, 654459306, 3188396048, 
		3373015174, 1466479909, 544179635, 3110523913, 3462522015, 1591671054, 
		702138776, 2966460450, 3352799412, 1504918807, 783551873, 3082640443, 
		3233442989, 3988292384, 2596254646, 62317068, 1957810842, 3939845945, 
		2647816111, 81470997, 1943803523, 3814918930, 2489596804, 225274430, 
		2053790376, 3826175755, 2466906013, 167816743, 2097651377, 4027552580, 
		2265490386, 503444072, 1762050814, 4150417245, 2154129355, 426522225, 
		1852507879, 4275313526, 2312317920, 282753626, 1742555852, 4189708143, 
		2394877945, 397917763, 1622183637, 3604390888, 2714866558, 953729732, 
		1340076626, 3518719985, 2797360999, 1068828381, 1219638859, 3624741850, 
		2936675148, 906185462, 1090812512, 3747672003, 2825379669, 829329135, 
		1181335161, 3412177804, 3160834842, 628085408, 1382605366, 3423369109, 
		3138078467, 570562233, 1426400815, 3317316542, 2998733608, 733239954, 
		1555261956, 3268935591, 3050360625, 752459403, 1541320221, 2607071920, 
		3965973030, 1969922972, 40735498, 2617837225, 3943577151, 1913087877, 
		83908371, 2512341634, 3803740692, 2075208622, 213261112, 2463272603, 
		3855990285, 2094854071, 198958881, 2262029012, 4057260610, 1759359992, 
		534414190, 2176718541, 4139329115, 1873836001, 414664567, 2282248934, 
		4279200368, 1711684554, 285281116, 2405801727, 4167216745, 1634467795, 
		376229701, 2685067896, 3608007406, 1308918612, 956543938, 2808555105, 
		3495958263, 1231636301, 1047427035, 2932959818, 3654703836, 1088359270, 
		936918000, 2847714899, 3736837829, 1202900863, 817233897, 3183342108, 
		3401237130, 1404277552, 615818150, 3134207493, 3453421203, 1423857449, 
		601450431, 3009837614, 3294710456, 1567103746, 711928724, 3020668471, 
		3272380065, 1510334235, 755167117 };

	unsigned int num = (unsigned int)-1;

	if(buf && len > 0)
	{
		for (int i = 0; i < len; i++)
			num = num >> 8 ^ hashTable[(num ^ buf[i]) & 255];

		num = (unsigned int)((unsigned long long)num ^ (long long)-1);
	}
	else
		debugPrint(DBG_WARN, "XYZV3::calcXYZcrc32 invalid input");

	return num;
}

const char* XYZV3::readLineFromBuf(const char* buf, char *lineBuf, int lineLen)
{
	//debugPrint(DBG_VERBOSE, "XYZV3::readLineFromBuf()");

	// zero out buffer
	if(lineBuf)
		*lineBuf = '\0';

	if(buf && lineBuf && lineLen > 0)
	{
		if(*buf)
		{
			// search for newline or end of string
			int i = 0;
			while(*buf && *buf != '\n' && *buf != '\r' && i < (lineLen-2))
			{
				lineBuf[i] = *buf;
				buf++;
				i++;
			}

			// scroll past newline
			if(*buf == '\r')
				buf++;
			if(*buf == '\n')
				buf++;

			// tack on newline and terminate string
			lineBuf[i] = '\n';
			i++;
			lineBuf[i] = '\0';
			i++;

			// return poiner to new string
			return buf;
		}
		else
			debugPrint(DBG_VERBOSE, "XYZV3::readLineFromBuf buffer is empty");
	}
	else
		debugPrint(DBG_WARN, "XYZV3::readLineFromBuf invalid input");

	return NULL;
}

bool XYZV3::checkLineIsHeader(const char* lineBuf)
{
	//debugPrint(DBG_VERBOSE, "XYZV3::checkLineIsHeader()");

	if(lineBuf)
	{
		// loop over every char
		while(*lineBuf)
		{
			if(*lineBuf == ';') // if comment symbol then part of header
				return true;
			else if(!isspace(*lineBuf)) // else if regular text part of body
				return false;

			lineBuf++;
		}
	}
	else
		debugPrint(DBG_WARN, "XYZV3::checkLineIsHeader invalid input");

	return true; // else just white space, assume header
}

bool XYZV3::processGCode(const char *gcode, const int gcodeLen, const char *fileNum, char **processedBuf, int *headerLen, int *totalLen)
{
	debugPrint(DBG_VERBOSE, "XYZV3::processGCode()");

	// validate parameters
	if(gcode && gcodeLen > 1 && processedBuf && headerLen && headerLen && totalLen)
	{
		*processedBuf = NULL;
		*headerLen = 0;
		*totalLen = 0;

		const int lineLen = 1024;
		char lineBuf[lineLen];

		// parse header once to get info on print time
		int printTime = 1;
		int totalFacets = 50;
		int totalLayers = 10;
		float totalFilament = 1.0f;

		char *s = NULL;
		const char *tcode = gcode;
		tcode = readLineFromBuf(tcode, lineBuf, lineLen);
		while(*lineBuf)
		{
			//****FixMe, this only really works with cura and xyz files
			// to properly handle slic3r and KISSlicer we need to parse
			// the file and estimate everyting.
			// but is that worth the effort?
			if(checkLineIsHeader(lineBuf))
			{
				// strip out the following parameters
				// and capture info if possible
				// ; filename = temp.3w
				// ; print_time = {estimated time in seconds}
				// ; machine = dv1MX0A000 // change to match current printer
				// ; facets = {totalFacets}
				// ; total_layers = {totalLayers}
				// ; version = 18020109
				// ; total_filament = {estimated filament used in mm}

				// print time
				if(NULL != (s = strstr(lineBuf, "print_time = ")))
					printTime = atoi(s + strlen("print_time = "));
				else if(NULL != (s = strstr(lineBuf, "TIME:")))
					printTime = atoi(s + strlen("TIME:"));

				// total facets
				if(NULL != (s = strstr(lineBuf, "facets = ")))
					totalFacets = atoi(s + strlen("facets = "));

				// total layers
				if(NULL != (s = strstr(lineBuf, "total_layers = ")))
					totalLayers = atoi(s + strlen("total_layers = "));
				else if(NULL != (s = strstr(lineBuf, "LAYER_COUNT:")))
					totalLayers = atoi(s + strlen("LAYER_COUNT:"));
				//****FixMe, can look for ;LAYER: x as well

				// filament used
				else if(NULL != (s = strstr(lineBuf, "total_filament = ")))
					totalFilament = (float)atof(s + strlen("total_filament = "));
				else if(NULL != (s = strstr(lineBuf, "filament_used = ")))
					totalFilament = (float)atof(s + strlen("filament_used = "));
				else if(NULL != (s = strstr(lineBuf, "filament used = ")))
					totalFilament = (float)atof(s + strlen("filament used = "));
				else if(NULL != (s = strstr(lineBuf, "Filament used: ")))
					totalFilament = 1000.0f * (float)atof(s + strlen("Filament used: ")); // m to mm

				// potential data to be stripped from header

				// nozzle diameter
				//" nozzle_diameter = "

				// layer height
				//" layer_height = "
				//"Layer height: "

				// print speed
				//" speed = "
				//"nspeed_print = "
			}
			//****Note, probably should not early out, some put there header at the bottom
			else
				break;

			tcode = readLineFromBuf(tcode, lineBuf, lineLen);
		}

		// create working buffer with extra room
		int bBufMaxLen = gcodeLen + 1000;
		int bbufOffset = 0;
		char * bBuf = new char[bBufMaxLen];
		if(bBuf)
		{
			// make a fake header to keep printer happy
			// must come first or printer will fail
			bbufOffset += sprintf(bBuf + bbufOffset, "; filename = temp.3w\n");
			bbufOffset += sprintf(bBuf + bbufOffset, "; print_time = %d\n", printTime);
			bbufOffset += sprintf(bBuf + bbufOffset, "; machine = %s\n", fileNum);
			bbufOffset += sprintf(bBuf + bbufOffset, "; facets = %d\n", totalFacets);
			bbufOffset += sprintf(bBuf + bbufOffset, "; total_layers = %d\n", totalLayers);
			bbufOffset += sprintf(bBuf + bbufOffset, "; version = 18020109\n");
			bbufOffset += sprintf(bBuf + bbufOffset, "; total_filament = %0.2f\n", totalFilament);

			// potential data to add to header
			/*
			; nozzle_diameter = 0.40
			; layer_height = 0.30
			; support_material = 0
			; support_material_extruder = 1
			; extruder_filament = 50.15:0.00
			; extruder = 1
			; filamentid = 50,50,
			; materialid = 0,
			; fill_density = 0.10
			; raft_layers = 0
			; support_density = 0.15
			; shells = 2
			; speed = 35.000
			; brim_width = 0.000
			; dimension = 20.00:20.00:0.65
			; fill_pattern = rectilinear
			; perimeter_speed = 15.000
			; small_perimeter_speed = 10.000
			; bridge_speed = 10.000
			; travel_speed = 60.000
			; retract_speed = 15
			; retract_length = 4.000
			; first_layer_speed = 5.000
			; Call_From_APP = XYZware 2.1.26.1
			; speed_limit_open = 1
			; Total computing time = 0.000 sec. 
			; threads = 1
			*/

			bool isHeader = true;
			bool wasHeader = true;
			int headerEnd = 0;

			// for each line in gcode file
			tcode = gcode;
			tcode = readLineFromBuf(tcode, lineBuf, lineLen);
			while(*lineBuf)
			{
				wasHeader = isHeader;
				if(isHeader)
					isHeader = checkLineIsHeader(lineBuf);

				if(isHeader)
				{
					// strip out duplicate lines
					if( NULL != strstr(lineBuf, "filename")		|| 
						NULL != strstr(lineBuf, "print_time")	||
						NULL != strstr(lineBuf, "machine")		||
						NULL != strstr(lineBuf, "facets")		||
						NULL != strstr(lineBuf, "total_layers")	||
						NULL != strstr(lineBuf, "version")		||
						NULL != strstr(lineBuf, "total_filament") )
					{
						// drop the line on the floor
					}
					else // else just pass it on through
					{
						strcpy(bBuf + bbufOffset, lineBuf);
						bbufOffset += strlen(lineBuf);
					}
				}
				else
				{
					// mark end of header in main buffer
					if(wasHeader)
						headerEnd = bbufOffset;

					// convert G0 to G1
					char *s = strstr(lineBuf, "G0");
					if(!s)
						s = strstr(lineBuf, "g0");
					if(s)
						s[1] = '1';

					// copy to file
					strcpy(bBuf + bbufOffset, lineBuf);
					bbufOffset += strlen(lineBuf);
				}

				tcode = readLineFromBuf(tcode, lineBuf, lineLen);
			}

			*processedBuf = bBuf;
			*headerLen = headerEnd;
			*totalLen = bbufOffset;

			return true;
		}
		else
			debugPrint(DBG_WARN, "XYZV3::processGCode failed to allocate buffer");
	}
	else
		debugPrint(DBG_WARN, "XYZV3::processGCode invalid input");

	return false;
}

bool XYZV3::encryptHeader(const char *gcode, int gcodeLen, bool fileIsV5, char **headerBuf, int *headerLen)
{
	debugPrint(DBG_VERBOSE, "XYZV3::encryptHeader()");
	bool success = false;

	// validate parameters
	if(gcode && headerBuf && headerLen)
	{
		//==============
		// fix up header

		*headerBuf = NULL;
		*headerLen = 0;

		// padded out to 16 byte boundary
		// and copy so we can encrypt it seperately
		int hLen = roundUpTo16(gcodeLen); 
		char *hBuf = new char[hLen+1];
		if(hBuf)
		{
			memcpy(hBuf, gcode, gcodeLen);

			// don't forget to tag the padding
			pkcs7pad(hBuf, gcodeLen);
			hBuf[hLen] = '\0';

			// encrypt the header in CBC mode
			// it appears that v5 files don't always encrypt
			if(!fileIsV5)
			{
				struct AES_ctx ctx;
				uint8_t iv[16] = {0}; // 16 zeros

				const char *hkey = "@xyzprinting.com";
				AES_init_ctx_iv(&ctx, hkey, iv);
				AES_CBC_encrypt_buffer(&ctx, (uint8_t*)hBuf, hLen);
			}

			*headerBuf = hBuf;
			*headerLen = hLen;

			success = true;
		}
		else
			debugPrint(DBG_WARN, "XYZV3::encryptHeader failed to allocate buffer");
	}
	else
		debugPrint(DBG_WARN, "XYZV3::encryptHeader invalid input");

	return success;
}

bool XYZV3::encryptBody(const char *gcode, int gcodeLen, bool fileIsZip, char **bodyBuf, int *bodyLen)
{
	debugPrint(DBG_VERBOSE, "XYZV3::encryptBody()");
	bool success = false;

	// validate parameters
	if(gcode && bodyBuf && bodyLen)
	{
		//============
		// fix up body

		*bodyBuf = NULL;
		*bodyLen = 0;

		if(fileIsZip)
		{
			//msTimer t1;
			mz_zip_archive zip;
			memset(&zip, 0, sizeof(zip));
			if(mz_zip_writer_init_heap(&zip, 0, 0))
			{
				//****FixMe, break this into loop?
				if(mz_zip_writer_add_mem(&zip, "sample.gcode", gcode, gcodeLen, MZ_DEFAULT_COMPRESSION))
				{
					char *zBuf;
					size_t zBufLen;
					if(mz_zip_writer_finalize_heap_archive(&zip, (void**)&zBuf, &zBufLen))
					{
						// encryption is handled on a block by block basis and adds 16 bytes to each block
						const int blockLen = 0x2000;
						int newLen = zBufLen + (zBufLen / blockLen + 1) * 16;

						//debugPrint(DBG_REPORT, "Zip took %0.2f s", t1.getElapsedTime_s());

						char *bBuf = new char[newLen];
						if(bBuf)
						{
							//msTimer t2;
							// encrypt body
							struct AES_ctx ctx;
							uint8_t iv[16] = {0}; // 16 zeros
							const char *key = "@xyzprinting.com";

							int readOffset = 0;
							int writeOffset = 0;

							// decrypt in blocks
							for(readOffset = 0; readOffset < (int)zBufLen; readOffset += blockLen)
							{
								// last block is smaller, so account for it
								int len = ((zBufLen - readOffset) < blockLen) ? (zBufLen - readOffset) : blockLen;

								// and stash in new buf
								memcpy(bBuf+writeOffset, zBuf+readOffset, len);

								// add padding to body
								len = pkcs7pad(bBuf+writeOffset, len);

								// reset decrypter every block
								AES_init_ctx_iv(&ctx, key, iv);

								//****FixMe, loop over this in blocks of 1,000
								AES_CBC_encrypt_buffer(&ctx, (uint8_t*)bBuf+writeOffset, len);

								writeOffset += len;
							}
							//debugPrint(DBG_REPORT, "CBC Encrypt took %0.2f s", t2.getElapsedTime_s());

							*bodyBuf = bBuf;
							*bodyLen = writeOffset;

							success = true;
						}
						else
							debugPrint(DBG_WARN, "XYZV3::encryptBody failed to allocate z buffer");
					}
					else
						debugPrint(DBG_WARN, "XYZV3::encryptBody failed to finalize zip");
				}
				else
					debugPrint(DBG_WARN, "XYZV3::encryptBody failed to add to zip");

				// clean up zip memory
				mz_zip_writer_end(&zip);
			}
			else
				debugPrint(DBG_WARN, "XYZV3::encryptBody failed to init zip");
		}
		else
		{
			int bLen = roundUpTo16(gcodeLen); 
			char *bBuf = new char[bLen+1];
			if(bBuf)
			{
				memcpy(bBuf, gcode, gcodeLen);

				// don't forget to tag the padding
				pkcs7pad(bBuf, gcodeLen);
				bBuf[bLen] = '\0';

				//msTimer t1;
				// encrypt body using ECB mode
				struct AES_ctx ctx;
				uint8_t iv[16] = {0}; // 16 zeros
				const char *bkey = "@xyzprinting.com@xyzprinting.com";
				AES_init_ctx_iv(&ctx, bkey, iv);

				// decrypt in blocks
				const int blockLen = 0x2010;
				for(int writeOffset = 0; writeOffset < bLen; writeOffset += blockLen)
				{
					// last block is smaller, so account for it
					int len = ((bLen - writeOffset) < blockLen) ? (bLen - writeOffset) : blockLen;

					//****FixMe, loop over this in blocks of 1,000
					AES_ECB_encrypt_buffer(&ctx, (uint8_t*)(bBuf + writeOffset), len);
				}
				//debugPrint(DBG_REPORT, "EBC Encrypt took %0.2f s", t1.getElapsedTime_s());

				*bodyBuf = bBuf;
				*bodyLen = bLen;

				success = true;
			}
			else
				debugPrint(DBG_WARN, "XYZV3::encryptBody failed to allocate buffer");
		}
	}
	else
		debugPrint(DBG_WARN, "XYZV3::encryptBody invalid input");

	return success;
}

bool XYZV3::writeFile(FILE *fo, bool fileIsV5, bool fileIsZip, const char *headerBuf, int headerLen, char *bodyBuf, int bodyLen)
{
	debugPrint(DBG_VERBOSE, "XYZV3::writeFile()");

	if(fo && headerBuf && bodyBuf)
	{
		//==================
		// write header info

		// write file id
		fwrite("3DPFNKG13WTW", 1, strlen("3DPFNKG13WTW"), fo);

		// id, what is this
		writeByte(fo, 1);

		// file format version is 2 or 5
		if(fileIsV5)
			writeByte(fo, 5);
		else
			writeByte(fo, 2);
		
		// pad to 4 bytes
		writeByte(fo, 0);
		writeByte(fo, 0);

		// offset to zip marker
		int pos1 = ftell(fo) + 4; // count from next byte after count
		// force at least 16 bytes of padding, is this needed?
		int zipOffset = roundUpTo16(pos1 + 4684) - pos1;
		writeWord(fo, zipOffset);
		writeRepeatByte(fo, 0, zipOffset);

		// zip format marker
		if(fileIsZip)
			fwrite("TagEa128", 1, strlen("TagEa128"), fo);
		else
			fwrite("TagEJ256", 1, strlen("TagEJ256"), fo);

		// optional header len
		if(fileIsV5)
			writeWord(fo, headerLen);

		// offset to header
		int pos2 = ftell(fo) + 4; // count from next byte after count
		// force at least 16 bytes of padding, is this needed?
		int headerOffset = roundUpTo16(pos2 + 68) - pos2; 
		writeWord(fo, headerOffset);

		// mark current file location
		int offset1 = ftell(fo);

		//?? 
		if(fileIsV5)
			writeWord(fo, 1);

		int crc32 = calcXYZcrc32(bodyBuf, bodyLen);
		writeWord(fo, crc32);

		// zero pad to header offset
		int pad1 = headerOffset - (ftell(fo) - offset1);
		writeRepeatByte(fo, 0, pad1);

		// write encrypted and padded header out
		fwrite(headerBuf, 1, headerLen, fo);

		// mark current file location
		int pad2 = m_bodyOffset - ftell(fo);
		// pad with zeros to start of body
		writeRepeatByte(fo, 0, pad2);

		// write encrypted and padded body out
		fwrite(bodyBuf, 1, bodyLen, fo);

		return true;
	}
	else
		debugPrint(DBG_WARN, "XYZV3::writeFile invalid input");
	
	return false;
}

// === machine info ===

int XYZV3::getInfoCount()
{
	return m_infoArrayLen;
}

const XYZPrinterInfo* XYZV3::indexToInfo(int index)
{
	//debugPrint(DBG_VERBOSE, "XYZV3::indexToInfo(%d)", index);

	if(index >= 0 && index < m_infoArrayLen)
	{
		return &m_infoArray[index];
	}
	else
		debugPrint(DBG_WARN, "XYZV3::indexToInfo index not found %d", index);

	return NULL;
}

const XYZPrinterInfo* XYZV3::modelToInfo(const char *modelNum)
{
	debugPrint(DBG_VERBOSE, "XYZV3::modelToInfo(%s)", modelNum);

	if(modelNum)
	{
		// mini w has two model numbers
		//****FixMe, shoule we do this?
		//if(0 == strcmp(modelNum, "dv1MW0B000"))
		//	modelNum = "dv1MW0A000"; 

		for(int i=0; i<m_infoArrayLen; i++)
		{
			if(0 == strcmp(modelNum, m_infoArray[i].modelNum))
			{
				return &m_infoArray[i];
			}
		}

		debugPrint(DBG_WARN, "XYZV3::modelToInfo model number not found '%s'", modelNum);
	}
	else
		debugPrint(DBG_WARN, "XYZV3::modelToInfo invalid input");

	return NULL;
}

const XYZPrinterInfo* XYZV3::serialToInfo(const char *serialNum)
{
	debugPrint(DBG_VERBOSE, "XYZV3::serialToInfo(%s)", serialNum);

	if(serialNum)
	{
		//****Note, mini w has two model numbers
		// see above

		for(int i=0; i<m_infoArrayLen; i++)
		{
			// first 6 digits of serial number is a machine id
			if(0 == strncmp(serialNum, m_infoArray[i].serialNum, 6))
			{
				return &m_infoArray[i];
			}
		}

		debugPrint(DBG_WARN, "XYZV3::serialToInfo serial number not found '%s'", serialNum);
	}
	else
		debugPrint(DBG_WARN, "XYZV3::serialToInfo invalid input");

	return NULL;
}

const char* XYZV3::serialToName(const char *serialNum)
{
	debugPrint(DBG_VERBOSE, "XYZV3::serialToName(%s)", serialNum);

	if(serialNum)
	{
		const XYZPrinterInfo *inf = XYZV3::serialToInfo(serialNum);
		if(inf)
			return inf->screenName;
		else
			debugPrint(DBG_WARN, "XYZV3::serialToName serial number not found '%s'", serialNum);
	}
	else
		debugPrint(DBG_WARN, "XYZV3::serialToName invalid input");

	return serialNum;
}

bool XYZV3::isWIFI()
{
	return m_stream && m_stream->isWIFI();
}
