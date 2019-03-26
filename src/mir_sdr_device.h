/**
** RSP_tcp - TCP/IP I/Q Data Server for the sdrplay RSP2
** Copyright (C) 2017 Clem Schmidt, softsyst GmbH, http://www.softsyst.com
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
**
**/

#pragma once
#include "rsp_tcp.h"
#include "common.h"
#include "IPAddress.h"
#include <mirsdrapi-rsp.h>
#include "rsp_cmdLineArgs.h"
#define HAVE_STRUCT_TIMESPEC
#include <pthread.h>
#ifdef _WIN32
#define sleep(n) Sleep(n*1000)
#define usleep(n) Sleep(n/1000)
#else
#include <string.h> //memset etc.
#include <unistd.h>
#endif
using namespace std;

void* receive(void* md);
void streamCallback(short *xi, short *xq, unsigned int firstSampleNum,
	int grChanged, int rfChanged, int fsChanged, unsigned int numSamples,
	unsigned int reset, unsigned int hwRemoved, void *cbContext);

void gainChangeCallback(unsigned int gRdB, unsigned int lnaGRdB, void* cbContext);

struct samplingConfiguration
{
	const int samplingRateHz;
	const int deviceSamplingRateHz;
	const mir_sdr_Bw_MHzT bandwidth;
	const int decimationFactor;
	const bool doDecimation;

	samplingConfiguration(int srHz, int devSrHz, mir_sdr_Bw_MHzT bw, int decimFact, bool doDecim)
	  : samplingRateHz(srHz), 
		deviceSamplingRateHz(devSrHz),
		bandwidth(bw),
		decimationFactor(decimFact),
		doDecimation(doDecim)
	{
	}
};


class mir_sdr_device
{
public:
	mir_sdr_device();
	~mir_sdr_device();

private:
	int getSamplingConfigurationTableIndex(int requestedSrHz);
	void writeWelcomeString() const;
	void cleanup();

	friend void* receive(void* p);
	friend void streamCallback(short *xi, short *xq, unsigned int firstSampleNum,
		int grChanged, int rfChanged, int fsChanged, unsigned int numSamples,
		unsigned int reset, unsigned int hwRemoved, void *cbContext);

public:
	void init(rsp_cmdLineArgs* pargs);
	void start(SOCKET client);
	void stop();

	pthread_mutex_t mutex_rxThreadStarted;
	pthread_cond_t started_cond = PTHREAD_COND_INITIALIZER;
	pthread_t* thrdRx;

	/// <summary>
	/// API: Device Enumeration Structure
	/// </summary>
	string serno;		// serial number
	string DevNm;		// device string (USB)
	BYTE hwVer;			// HW version
	bool devAvail;		// true if available
	bool isStreaming;

	bool started = false;
	unsigned int DeviceIndex;


private:

	const int c_welcomeMessageLength = 100;
	BYTE* mergeIQ(const short* idata, const short* qdata, int samplesPerPacket, int& buflen);
	mir_sdr_ErrT setFrequencyCorrection(int value);
	mir_sdr_ErrT setAntenna(int value);
	mir_sdr_ErrT setAGC(bool on);
	mir_sdr_ErrT setGain(int value);
	mir_sdr_ErrT setSamplingRate(int requestedSrHz);
	mir_sdr_ErrT setFrequency(int valueHz);
	mir_sdr_ErrT reinit_Frequency(int valueHz);
	mir_sdr_ErrT stream_InitForSamplingRate(int sampleConfigsTableIndex);
	mir_sdr_ErrT stream_Uninit();

	//Reference: rtl_tcp.c fct command_worker, line 277
	//Copied from QIRX
	enum eRTLCommands
	{
		CMD_SET_FREQUENCY = 1
		, CMD_SET_SAMPLINGRATE = 2
		, CMD_SET_GAIN_MODE = 3               //int manual
		, CMD_SET_GAIN = 4                    //int gain, tenth dB; fct tuner_r82xx.c fct r82xx_set_gain
		, CMD_SET_FREQUENCYCORRECTION = 5     //int ppm
		, CMD_SET_IF_GAIN = 6                 //int stage, int gain
		, CMD_SET_AGC_MODE = 8                //int on
		, CMD_SET_DIRECT_SAMPLING = 9         //int on
		, CMD_SET_OFFSET_TUNING = 10          //int on
		, CMD_SET_TUNER_GAIN_BY_INDEX = 13
		, CMD_SET_RSP2_ANTENNA_CONTROL = 33   //int Antenna Select
	};

	// This server is able to stream native 16-bit data (of "short" type)
	// or - for comaptibility with some apps, 8-bit data, 
	// where ( 8-bit Byte) =  ( 16-bit short /64) + 127
	eBitWidth bitWidth = BITS_16;

public:
	// ha: made following members public and static,
	//    (to make them available from command line)
	//    and moved contents to .cpp
	static const int c_numSamplingConfigs;  // = 6
	static const samplingConfiguration samplingConfigs[6];
	static int initSamplingConfigIdx; // = 3;

private:

	//QIRX internal type for the RSP2
	BYTE rxType = 8;

	//Assumed gain steps for the RSP2
	//This is "quick and dirty" due to the overall complexity of the RSPs gain settings
	BYTE gainCount = 100;

	//The socket of the remote app
	SOCKET remoteClient;

	//Generic API error type
	mir_sdr_ErrT err;

	int sys = 40;
	int agcReduction = -25;

	// currently commanded values
	int currentFrequencyHz;
	int gainReduction;
	double currentSamplingRateHz;
	int antenna = 5;
	int enableBiasT = 0;	// ha: added bias-T to allow powering external LNAs

	//Callbacks per second, for a "timer" to discard samples 
	//to prevent overrun in the device in the error case
	int cbksPerSecond = 0;
	bool cbkTimerStarted = false;
};

