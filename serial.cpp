#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
#include <SDKDDKVer.h>
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <Setupapi.h>
#include <assert.h>
#include <winioctl.h>

#include "serial.h"
#include "debug.h"

#pragma comment(lib, "Setupapi.lib")
#pragma warning(disable:4996)

// static class data

int SerialHelper::portCount = 0;
int SerialHelper::defaultPortNum = -1;
int SerialHelper::defaultPortID = -1;

int SerialHelper::portNumbers[maxPortCount] = {-1};
char SerialHelper::portNames[maxPortCount][maxPortName] = {""};

Serial::Serial() 
	: m_serial(NULL) 
	, m_port(-1)
	, m_baudRate(-1)
{
}

Serial::~Serial() 
{ 
	closeSerial(); 
}

bool Serial::verifyPort(int port)
{
	char portStr[64] = "";
	sprintf(portStr, "\\\\.\\COM%d", port);

	// open serial port
	HANDLE serial = CreateFileA( portStr, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(serial != INVALID_HANDLE_VALUE)
	{
		CloseHandle(serial);
		return true;
	}

	return false;
}

/*
bool setupConfig(const char *portStr, HANDLE h)
{
 	COMMCONFIG commConfig = {0};
	DWORD dwSize = sizeof(commConfig);
	commConfig.dwSize = dwSize;
	if(GetDefaultCommConfigA(portStr, &commConfig, &dwSize))
	{
		// Set the default communication configuration
		if (SetCommConfig(h, &commConfig, dwSize))
		{
			return true;
		}
	}
	return false;
}
*/

bool Serial::openSerial(int port, int baudRate)
{
	bool blocking = false; // set to true to block till data arives
	int timeout_ms = 50;

	// if already connected just return
	if( (port < 0 || getPort() == port) && 
		getBaudRate() == baudRate)
		return true;

	// close out any previous connection
	closeSerial();
	
	// auto detect port
	if(port < 0)
		port = SerialHelper::queryForPorts();

	if(port >= 2) // reject default ports, maybe there is a better way?
	{
		char portStr[64] = "";
		sprintf(portStr, "\\\\.\\COM%d", port);

		// open serial port
		m_serial = CreateFileA( portStr, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if(m_serial != INVALID_HANDLE_VALUE)
		{
			//if(setupConfig(portStr, m_serial))
			{
				// setup serial port baud rate
				DCB dcb = {0};
				if (GetCommState(m_serial, &dcb))
				{
					dcb.BaudRate=baudRate;
					dcb.ByteSize=8;
					dcb.StopBits=ONESTOPBIT;
					dcb.Parity=NOPARITY;
					//dcb.fParity = 0;

					if(SetCommState(m_serial, &dcb))
					{
						COMMTIMEOUTS cto;
						if(GetCommTimeouts(m_serial, &cto))
						{
							cto.ReadIntervalTimeout = (blocking) ? 0 : MAXDWORD; // wait forever or wait ReadTotalTimeoutConstant
							cto.ReadTotalTimeoutConstant = timeout_ms;
							cto.ReadTotalTimeoutMultiplier = 0;
							//cto.WriteTotalTimeoutMultiplier = 20000L / baudRate;
							//cto.WriteTotalTimeoutConstant = 0;

							if(SetCommTimeouts(m_serial, &cto))
							{
								if(SetCommMask(m_serial, EV_BREAK|EV_ERR|EV_RXCHAR))
								{
									if(SetupComm(m_serial, m_max_serial_buf, m_max_serial_buf))
									{
										clear();

										m_port = port;
										m_baudRate = baudRate;

										return true;
									}
								}
							}
						}
					}
				}
			}
		}

		closeSerial();
	}
	return false;
}

void Serial::closeSerial()
{
	if(m_serial)
		CloseHandle(m_serial);
	m_serial = NULL;
	m_port = -1;
	m_baudRate = -1;
}

int Serial::getPort()
{
	return (m_serial) ? m_port : -1;
}

int Serial::getBaudRate()
{
	return (m_serial) ? m_baudRate : -1;
}
 
void Serial::clear()
{
	//****FixMe, drain buffer and log to error log
	if(m_serial)
		PurgeComm (m_serial, PURGE_TXABORT | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_RXCLEAR);
}

int Serial::read(char *buf, int len)
{
	DWORD bytesRead = 0;

	if(m_serial && buf && len > 0)
	{
		//****FixMe, drain readLine buffer first!
		buf[0] = '\0';
		if(ReadFile(m_serial, buf, len-1, &bytesRead, NULL) && bytesRead > 0)
		{
			if(bytesRead > (DWORD)(len-1))
				bytesRead = len-1;

			buf[bytesRead] = '\0';
		}
	}

	return bytesRead;
}



int Serial::write(const char *buf, int len)
{
	DWORD bytesWritten = 0;

	if(m_serial && buf && len > 0)
	{
		if(WriteFile(m_serial, buf, len, &bytesWritten, NULL))
		{
			// success

			debugPrint(DBG_LOG, "write array: %d bytes", len);
			debugPrintArray(DBG_VERBOSE, buf, len);
		}
		else
			debugPrint(DBG_ERR, "failed to write bytes");
	}

	return bytesWritten;
}

// enumerate all available ports and find there string name as well
// only usb serial ports have a string name, but that is most serial devices these days
// based on CEnumerateSerial http://www.naughter.com/enumser.html
int SerialHelper::queryForPorts(const char *hint)
{
	portCount = 0;
	defaultPortNum = -1;
	defaultPortID = -1;
	bool hintFound = false;

	HDEVINFO hDevInfoSet = SetupDiGetClassDevsA( &GUID_DEVINTERFACE_COMPORT, 
								NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if(hDevInfoSet != INVALID_HANDLE_VALUE)
	{
		SP_DEVINFO_DATA devInfo;
		devInfo.cbSize = sizeof(SP_DEVINFO_DATA);

		const static int maxLen = 512;
		char tstr[maxLen];

		int nIndex = 0;
		while(SetupDiEnumDeviceInfo(hDevInfoSet, nIndex, &devInfo))
		{
			int nPort = -1;
			HKEY key = SetupDiOpenDevRegKey(hDevInfoSet, &devInfo, DICS_FLAG_GLOBAL, 
											0, DIREG_DEV, KEY_QUERY_VALUE);
			if(key != INVALID_HANDLE_VALUE)
			{
				tstr[0] = '\0';
				ULONG tLen = maxLen;
				DWORD dwType = 0;
				LONG status = RegQueryValueExA(key, "PortName", nullptr, &dwType, reinterpret_cast<LPBYTE>(tstr), &tLen);
				if(ERROR_SUCCESS == status && tLen > 0 && tLen < maxLen)
				{
					// it may be possible for string to be unterminated
					// add an extra terminator just to be safe
					tstr[tLen] = '\0';

					// check for COMxx wher xx is a number
					if (strlen(tstr) > 3 && strncmp(tstr, "COM", 3) == 0 && isdigit(tstr[3]))
					{
						// srip off the number
						nPort = atoi(tstr+3);
						if (nPort != -1)
						{
							// if this triggers we need to increase maxPortCount
							assert(portCount < maxPortCount);
							if(portCount < maxPortCount)
							{
								portNumbers[portCount] = nPort;

								// pick highest port by default, unless already found by hint
								if(!hintFound && defaultPortNum < nPort)
								{
									defaultPortNum = nPort;
									defaultPortID = portCount;
								}

								DWORD dwType = 0;
								DWORD dwSize = maxPortName;
								portNames[portCount][0] = '\0';
								if(SetupDiGetDeviceRegistryPropertyA(hDevInfoSet, &devInfo, SPDRP_DEVICEDESC, &dwType, (PBYTE)portNames[portCount], maxPortName, &dwSize))
								{
									// check if this port matches our hint
									// for now we take the last match
									if(hint && strstr(portNames[portCount], hint))
									{
										hintFound = true;
										defaultPortNum = nPort;
										defaultPortID = portCount;
									}
								}
								else
									portNames[portCount][0] = '\0';

								portCount++;
							}
						}
					}
				}
				RegCloseKey(key);
			}

			++nIndex;
		}

		if(SerialHelper::getPortName(defaultPortID))
			debugPrint(DBG_LOG, "detected port: %d:%s", defaultPortNum, SerialHelper::getPortName(defaultPortID));
		else
			debugPrint(DBG_LOG, "detected port: %d:NULL", defaultPortNum);
		for(int i=0; i<SerialHelper::getPortCount(); i++)
			debugPrint(DBG_LOG, "  %d:%s", SerialHelper::getPortNumber(i), SerialHelper::getPortName(i));

		SetupDiDestroyDeviceInfoList(hDevInfoSet);
	}
	
	return defaultPortNum;
}

//****Note, older code that is reliable but does not give you a string name for the port
/*
int SerialHelper::enumeratePorts(int list[], int *count)
{
	//What will be the return value
	bool bSuccess = false;

	int bestGuess = -1;
	int maxList = 0;
	if(count)
	{
		maxList = *count;
		*count = 0;
	}

	//Use QueryDosDevice to look for all devices of the form COMx. Since QueryDosDevice does
	//not consitently report the required size of buffer, lets start with a reasonable buffer size
	//and go from there
	const static int nChars = 40000;
	char pszDevices[nChars];
	DWORD dwChars = QueryDosDeviceA(NULL, pszDevices, nChars);
	if (dwChars == 0)
	{
		DWORD dwError = GetLastError();
		if (dwError == ERROR_INSUFFICIENT_BUFFER)
			debugPrint(DBG_ERR, "needs more room!");
	}
	else
	{
		bSuccess = true;
		size_t i=0;

		while (pszDevices[i] != '\0')
		{
			//Get the current device name
			char* pszCurrentDevice = &pszDevices[i];

			//If it looks like "COMX" then
			//add it to the array which will be returned
			size_t nLen = strlen(pszCurrentDevice);
			if (nLen > 3)
			{
				if ((0 == strncmp(pszCurrentDevice, "COM", 3)) && isdigit(pszCurrentDevice[3]))
				{
					//Work out the port number
					int nPort = atoi(&pszCurrentDevice[3]);
					if(list && count && maxList > *count)
					{
						list[*count] = nPort;
						(*count)++;
					}
					if(bestGuess < nPort)
						bestGuess = nPort;
				}
			}

			//Go to next device name
			i += (nLen + 1);
		}
	}
	return bestGuess;
}
*/
