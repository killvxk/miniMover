#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
# include <SDKDDKVer.h>
# include <Windows.h>
#else
# include <sys/time.h>
#endif

#include <stdio.h>

#include "timer.h"

long long msTime::StartingTime;
long long msTime::Frequency;
bool msTime::init = false;

float msTime::getTime_micro()
{
#ifdef _WIN32
	LARGE_INTEGER CurrentTime;
	QueryPerformanceCounter(&CurrentTime);

	if(!init)
	{
		StartingTime = CurrentTime.QuadPart;

		LARGE_INTEGER f;
		QueryPerformanceFrequency(&f);  // get conversion rate
		Frequency = f.QuadPart;

		init = true;
	}

	return (float) ((CurrentTime.QuadPart - StartingTime) * 1000000 / Frequency);
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	long long CurrentTime = (1000000 * tv.tv_sec) + tv.tv_usec;

	if(!init)
	{
		StartingTime = CurrentTime;
		init = true;
	}

	return (float) (CurrentTime - StartingTime);

#endif
}

float msTime::getTime_s()
{ 
	return getTime_micro() / 1000000.0f;
}

float msTime::getTime_ms() 
{ 
	return getTime_micro() / 1000.0f;
}


//-------------------


msTimer::msTimer() 
{
	startTimer();
}

void msTimer::startTimer()
{ 
	m_startTime = msTime::getTime_micro(); 
	m_elapsedTime = 0;
}

float msTimer::stopTimer() 
{ 
	m_elapsedTime = msTime::getTime_micro() - m_startTime;
	return getLastTime_s();
}

float msTimer::getLastTime_s()
{ 
	return m_elapsedTime / 1000000.0f;
}

float msTimer::getLastTime_ms()
{ 
	return m_elapsedTime / 1000.0f; 
}

float msTimer::getLastTime_micro()
{ 
	return m_elapsedTime; 
}

float msTimer::getElapsedTime_s()
{
	return (msTime::getTime_micro() - m_startTime) / 1000000.0f;
}


//-------------------------


msTimeout::msTimeout()
{
	m_startTime = 0;
	m_endTime = 0;
}

msTimeout::msTimeout(float timeout_s)
{
	setTimeout_s(timeout_s);
}

void msTimeout::setTimeout_s(float timeout_s)
{
	m_startTime = msTime::getTime_s();
	m_endTime = m_startTime + timeout_s;
}

bool msTimeout::isTimeout()
{
	return msTime::getTime_s() > m_endTime;
}

float msTimeout::getElapsedTime_s()
{
	return msTime::getTime_s() - m_startTime;
}

float msTimeout::getElapsedTime_pct()
{
	float elapsed = (msTime::getTime_s() - m_startTime) / (m_endTime - m_startTime);
	return (elapsed < 1.0f) ? elapsed : 1.0f;
}

