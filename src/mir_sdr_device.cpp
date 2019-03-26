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

#include "mir_sdr_device.h"
#include <iostream>
using namespace std;


// ha: made following members public and static,
//    (to make them available from command line)
//    and moved contents from .h here
const int mir_sdr_device::c_numSamplingConfigs = 6;

const samplingConfiguration mir_sdr_device::samplingConfigs[6] = {
    // ha: added 384 kHz samplerate with 200 kHz bandwidth
    samplingConfiguration(384000, 3072000, mir_sdr_BW_0_200, 8, true),
    samplingConfiguration(512000, 2048000, mir_sdr_BW_0_300, 4, true),
    samplingConfiguration(1024000, 2048000, mir_sdr_BW_0_600, 2, true),
    samplingConfiguration(2048000, 2048000, mir_sdr_BW_1_536, 1, false),
    samplingConfiguration(4096000, 4096000, mir_sdr_BW_5_000, 1, false),
    samplingConfiguration(8192000, 8192000, mir_sdr_BW_8_000, 1, false)
};

int mir_sdr_device::initSamplingConfigIdx = 3;


mir_sdr_device::~mir_sdr_device()
{
	pthread_mutex_destroy(&mutex_rxThreadStarted);
	pthread_cond_destroy(&started_cond);
}

mir_sdr_device::mir_sdr_device() 
{
	thrdRx = 0;
	pthread_mutex_init(&mutex_rxThreadStarted, NULL);
	pthread_cond_init(&started_cond, NULL);

}

void mir_sdr_device::init(rsp_cmdLineArgs* pargs)
{
	//From the command line
	currentFrequencyHz = pargs->Frequency;
	gainReduction = pargs->GainReduction;
	currentSamplingRateHz = pargs->SamplingRate;
	bitWidth = (eBitWidth)pargs->BitWidth;
	antenna = pargs->Antenna;

	// ha: determine the SamplingConfigIdx - to allow direct initialization in this mode
	int defaultSamplingConfigIdx = initSamplingConfigIdx;
	initSamplingConfigIdx = getSamplingConfigurationTableIndex(currentSamplingRateHz);
	if ( initSamplingConfigIdx < 0 || initSamplingConfigIdx >= c_numSamplingConfigs )
		initSamplingConfigIdx = defaultSamplingConfigIdx;
}

void mir_sdr_device::cleanup()
{
}


void mir_sdr_device::stop()
{
	mir_sdr_ErrT err;

	if (!started)
	{
		cout << "Already Stopped. Nothing to do here.";
		return;
	}

	cleanup();

	cout << "Stopping, calling mir_sdr_StreamUninit..." << endl;

	err = mir_sdr_StreamUninit();
	cout << "mir_sdr_StreamUninit returned with: " << err << endl;
	if (err == mir_sdr_Success)
		cout << "StreamUnInit successful(0)" << endl;
	else
		cout << "StreamUnInit failed (1) with " << err << endl;
	isStreaming = false;

	err = mir_sdr_ReleaseDeviceIdx();
	cout << "\nmir_sdr_ReleaseDeviceIdx returned with: " << err << endl;
	cout << "DeviceIndex released: " << DeviceIndex << endl;
	started = false;

	closesocket(remoteClient);
	remoteClient = INVALID_SOCKET;
	cout << "Socket closed\n\n";
}

void mir_sdr_device::writeWelcomeString() const
{
	BYTE buf0[] = "RTL0";
	BYTE* buf = new BYTE[c_welcomeMessageLength];
	memset(buf, 0, c_welcomeMessageLength);
	memcpy(buf, buf0, 4);
	buf[6] = bitWidth;
	buf[7] = rxType;
	buf[11] = gainCount;
	buf[15] = 0x52; buf[16] = 0x53; buf[17] = 0x50; buf[18] = 0x32; //"RSP2", interpreted e.g. by qirx
	send(remoteClient, (const char*)buf, c_welcomeMessageLength, 0);
	delete[] buf;
}


void mir_sdr_device::start(SOCKET client)
{
	started = true;

	remoteClient = client;
	writeWelcomeString();

	cout << endl << "Starting..." << endl;


	if (thrdRx != 0) // just in case..
	{
		pthread_cancel(*thrdRx );
		delete thrdRx;
		thrdRx = 0;
	}
	thrdRx = new pthread_t();
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	int res = pthread_create(thrdRx, &attr, &receive, this);
	pthread_attr_destroy(&attr);
}

BYTE* mir_sdr_device::mergeIQ(const short* idata, const short* qdata, int samplesPerPacket, int& buflen)
{
	BYTE* buf = 0;
	buflen = 0;

	if (bitWidth == BITS_16)
	{
		buflen = samplesPerPacket *4;
		buf = new BYTE[buflen];
		for (int i = 0, j = 0; i < samplesPerPacket; i++)
		{
			buf[j++] = (BYTE)(idata[i] & 0xff);			
			buf[j++] = (BYTE)((idata[i] & 0xff00) >> 8);  

			buf[j++] = (BYTE)(qdata[i] & 0xff);			
			buf[j++] = (BYTE)((qdata[i] & 0xff00) >> 8); 
		}
	}
	else if (bitWidth == BITS_8)
	{
		buflen = samplesPerPacket * 2;
		buf = new BYTE[buflen];
		for (int i = 0, j = 0; i < samplesPerPacket; i++)
		{
			buf[j++] = (BYTE)(idata[i] / 64 + 127);;
			buf[j++] = (BYTE)(qdata[i] / 64 + 127);
		}
	}
	return buf;
}


void streamCallback(short *xi, short *xq, unsigned int firstSampleNum,
	int grChanged, int rfChanged, int fsChanged, unsigned int numSamples,
	unsigned int reset, unsigned int hwRemoved, void *cbContext)
{
	if (hwRemoved)
	{
		cout << " !!! HW removed !!!" << endl;
		return;
	}
	if (reset)
	{
		cout << " !!! reset !!!" << endl;
		return;
	}

	mir_sdr_device* md = (mir_sdr_device*)cbContext;
	try
	{
		if (!md->isStreaming)
		{
			return;
		}
		//In case of a socket error, dont process the data for one second
		if (md->cbksPerSecond-- > 0)
		{
			if (!md->cbkTimerStarted)
			{
				md->cbkTimerStarted = true;
				cout << "Discarding samples for one second\n";
			}
			return;
		}
		else if (md->cbkTimerStarted)
		{
			md->cbkTimerStarted = false;
		} 
		if (md->remoteClient == INVALID_SOCKET)
			return;

		int buflen = 0;
		BYTE* buf = md->mergeIQ(xi, xq, numSamples, buflen);
		int remaining = buflen;
		int sent = 0;
		while (remaining > 0)
		{
			fd_set writefds;
			struct timeval tv= {1,0};
			FD_ZERO(&writefds);
			FD_SET(md->remoteClient, &writefds);
			int res  = select(md->remoteClient+1, NULL, &writefds, NULL, &tv);
			if (res > 0)
			{
				sent = send(md->remoteClient, (const char*)buf + (buflen - remaining), remaining, 0);
				remaining -= sent;
			}
			else
			{
				md->cbksPerSecond = md->currentSamplingRateHz / numSamples; //1 sec "timer" in the error case. assumed this does not change frequently
				delete[] buf;
				throw msg_exception("socket error " + to_string(errno));
			}
		}
		delete[] buf;
	}
	catch (exception& e)
	{
		cout << "Error in streaming callback :" << e.what() <<  endl;
	}
}

void gainChangeCallback(unsigned int gRdB, unsigned int lnaGRdB, void* cbContext)
{
	// do nothing...
}


int getCommandAndValue(char* rxBuf, int& value)
{
	BYTE valbuf[4];
	int cmd = rxBuf[0];
	memcpy( valbuf, rxBuf + 1,4);

	if (common::isLittleEndian())
		value = valbuf[3] + valbuf[2] * 0x100 + valbuf[1] * 0x10000 + valbuf[0] * 0x1000000;
	else
		value = valbuf[0] + valbuf[1] * 0x100 + valbuf[2] * 0x10000 + valbuf[3] * 0x1000000;
	return cmd;
}


/// <summary>
/// Receive thread, to process commands
/// </summary>
void* receive(void* p)
{ 
	mir_sdr_ErrT err;
	mir_sdr_device* md = (mir_sdr_device*)p;
	cout << "**** receive thread entered.   *****" << endl;
	/*
	pthread_mutex_lock(&md->mutex_rxThreadStarted);
	pthread_cond_signal(&md->started_cond);
	pthread_mutex_unlock(&md->mutex_rxThreadStarted);
	cout << "**** receive thread signalled. *****" << endl;
	*/
	try
	{
		float apiVersion = 0.0f;

		err = mir_sdr_ApiVersion(&apiVersion);

		cout << "\nmir_sdr_ApiVersion returned with: " << err << endl;
		cout << "API Version " << apiVersion << endl;

		// ha: initialize directly to desired samplingConfig
		md->currentSamplingRateHz = md->samplingConfigs[md->initSamplingConfigIdx].samplingRateHz;

		int smplsPerPacket;

		mir_sdr_ErrT errInit = mir_sdr_StreamInit( &md->gainReduction,
			md->samplingConfigs[md->initSamplingConfigIdx].deviceSamplingRateHz / 1e6,	// ha: initialize directly to desired samplingConfig
			md->currentFrequencyHz / 1e6,
			md->samplingConfigs[md->initSamplingConfigIdx].bandwidth,	// ha: initialize directly to desired samplingConfig
			mir_sdr_IF_Zero,
			0,
			&md->sys,
			mir_sdr_USE_SET_GR,
			&smplsPerPacket,
			streamCallback,
			gainChangeCallback,
			md);

		// ha: detailed output - including samplerate and bandwidth
		cout << "\nmir_sdr_StreamInit(bw " << md->samplingConfigs[md->initSamplingConfigIdx].bandwidth << " , srate " << md->currentSamplingRateHz << ") returned with: " << errInit << endl;

		// disable DC offset and IQ imbalance correction (default is for these to be enabled  this
		// just show how to disable if required)
		//err = mir_sdr_DCoffsetIQimbalanceControl(1, 0);
		//cout << "\nmir_sdr_DCoffsetIQimbalanceControl returned with: " << err << endl;

		if (errInit == mir_sdr_Success || errInit == mir_sdr_AlreadyInitialised)
		{
			cout << "Starting SDR streaming" << endl;
			md->isStreaming = true;
		}
		else
		{
			cout << "API Init failed with error: " << errInit << endl;
			md->isStreaming = false;
		}

		// ha: show RSP hardware model / version
		unsigned char acHwVer[4] = { 0, 0, 0, 0 };
		err = mir_sdr_GetHwVersion(&acHwVer[0]);
		cout << endl << "mir_sdr_GetHwVersion returned " << int(acHwVer[0]) << " with " << err << endl;

		md->setAntenna(md->antenna);

		// configure DC tracking in tuner 
		err = mir_sdr_SetDcMode(4, 1); // select one-shot tuner DC offset correction with speedup 
		cout << "\nmir_sdr_SetDcMode returned with: " << err << endl;

		err = mir_sdr_SetDcTrackTime(63); // with maximum tracking time 
		cout << "\nmir_sdr_SetDcTrackTime returned with: " << err << endl;

		md->setAGC(true);

		// ha: initialize directly to desired samplingConfig - which might use decimation
		if ( md->samplingConfigs[md->initSamplingConfigIdx].doDecimation )
		{
			int decimationFactor = md->samplingConfigs[md->initSamplingConfigIdx].decimationFactor;
			err = mir_sdr_DecimateControl(1, decimationFactor, 0);
			cout << "mir_sdr_DecimateControl returned with: " << err << endl;
			if (err != mir_sdr_Success)
			{
				cout << "Requested Decimation Factor  was: " << decimationFactor << endl;
			}
			else
			{
				cout << "Decimation Factor set to  " << decimationFactor << endl;
			}
		}
	}
	catch (const std::exception& )
	{
		cout << " !!!!!!!!! Exception in the initialization !!!!!!!!!!\n";
		return 0;
	}

	try
	{
		char rxBuf[16];
		for (;;)
		{
			const int cmd_length = 5;
			memset(rxBuf, 0, 16);
			int remaining = cmd_length;

			while (remaining > 0)
			{
				int rcvd = recv(md->remoteClient, rxBuf + (cmd_length - remaining), remaining, 0); //read 5 bytes (cmd + value)
				remaining -= rcvd;

				if (rcvd  == 0)
					throw msg_exception("Socket closed");
				if (rcvd == SOCKET_ERROR )
					throw msg_exception("Socket error");
			}

			int value = 0; // out parameter
			int cmd = getCommandAndValue(rxBuf,  value);

			// The ids of the commands are defined in rtl_tcp, the names had been inserted here
			// for better readability
			switch (cmd)
			{
			case mir_sdr_device::CMD_SET_FREQUENCY: //set frequency
													  //value is freq in Hz
				err = md->setFrequency(value);
				break;

			case (int)mir_sdr_device::CMD_SET_SAMPLINGRATE:
				err = md->setSamplingRate(value);//value is sr in Hz
				break;

			case (int)mir_sdr_device::CMD_SET_FREQUENCYCORRECTION: //value is ppm correction
				md->setFrequencyCorrection(value);
				break;

			case (int)mir_sdr_device::CMD_SET_TUNER_GAIN_BY_INDEX:
				//value is gain value between 0 and 100
				err = md->setGain(value);
				break;

			case (int)mir_sdr_device::CMD_SET_AGC_MODE:
				err = md->setAGC(value != 0);
				break;

			case (int)mir_sdr_device::CMD_SET_RSP2_ANTENNA_CONTROL:
				md->setAntenna(value);
				break;
			default:
				printf("Unknown Command; 0x%x 0x%x 0x%x 0x%x 0x%x\n",
					rxBuf[0], rxBuf[1], rxBuf[2], rxBuf[3], rxBuf[4]);
				break;
			}
		}
	}
	catch (exception& e)
	{
		cout << "Error in receive :" << e.what() <<  endl;
	}
	cout << "**** Rx thread terminating. ****" << endl;
	return 0;
}

//value is correction in ppm
mir_sdr_ErrT mir_sdr_device::setFrequencyCorrection(int value)
{
	mir_sdr_ErrT err = mir_sdr_SetPpm((double)value);
	cout << "\nmir_sdr_SetPpm returned with: " << err << endl;
	if (err != mir_sdr_Success)
		cout << "PPM setting error: " << err << endl;
	else
		cout << "PPM correction: " << value << endl;
	return err;
}

mir_sdr_ErrT mir_sdr_device::setAntenna(int value)
{
	mir_sdr_ErrT err = mir_sdr_RSPII_AntennaControl((mir_sdr_RSPII_AntennaSelectT)value);

	cout << "\nmir_sdr_RSPII_AntennaControl returned with: " << err << endl;
	if (err != mir_sdr_Success)
		cout << "Antenna Control Setting error: " << err << endl;
	else
		cout << "Antenna Control Setting: " << value << endl;
	return err;
}

mir_sdr_ErrT mir_sdr_device::setAGC(bool on)
{
	mir_sdr_ErrT err = mir_sdr_Fail;
	if (on == false)
	{
		err = mir_sdr_AgcControl(mir_sdr_AGC_DISABLE, agcReduction, 0, 0, 0, 0, 1);
		cout << "\nmir_sdr_AgcControl OFF returned with: " << err << endl;
	}
	else
	{
		// enable AGC with a setPoint of -15dBfs //optimum for DAB
		err = mir_sdr_AgcControl(mir_sdr_AGC_5HZ, agcReduction, 0, 0, 0, 0, 1);
		cout << "\nmir_sdr_AgcControl 5Hz, " << agcReduction << " dBfs returned with: " << err << endl;
	}
	if (err != mir_sdr_Success)
	{
		cout << "SetAGC failed." << endl;
	}

	return err;
}

mir_sdr_ErrT mir_sdr_device::setGain(int value)
{
	mir_sdr_ErrT err = mir_sdr_SetGr(100 - value, 1, 0);

	cout << "\nmir_sdr_SetGr returned with: " << err << endl;
	if (err != mir_sdr_Success)
	{
		cout << "SetGr failed with requested value: " << 100-value << endl;
	}
	else
		cout << "SetGr succeeded with requested value: " << 100-value << endl;

	return err;
}

mir_sdr_ErrT mir_sdr_device::setSamplingRate(int requestedSrHz)
{
	mir_sdr_ErrT err = stream_Uninit();
	if (err != mir_sdr_Success)
		return err;

	int ix = getSamplingConfigurationTableIndex(requestedSrHz);
	if (ix == -1)
		return mir_sdr_Fail;

	err = stream_InitForSamplingRate(ix);
	return err;
}

mir_sdr_ErrT mir_sdr_device::setFrequency(int valueHz)
{
	mir_sdr_ErrT err = mir_sdr_SetRf((double)valueHz, 1, 0);
	cout << "\nmir_sdr_SetRf returned with: " << err << endl;

	switch (err)
	{
	case mir_sdr_Success:
		break;
	case mir_sdr_RfUpdateError:
		sleep(0.5f);//wait for the old command to settle
		err = mir_sdr_SetRf((double)valueHz, 1, 0);
		cout << "\nmir_sdr_SetRf returned with: " << err << endl;
		cout << "Frequency setting result: " << err << endl;

		if (err == mir_sdr_OutOfRange || mir_sdr_RfUpdateError)
			err = reinit_Frequency(valueHz);
		break;

	case mir_sdr_OutOfRange:
		err = reinit_Frequency(valueHz);
		break;
	default:
		break;
	}
	if (err != mir_sdr_Success)
	{
		cout << "Frequency setting error: " << err << endl;
		cout << "Requested Frequency was: " << +valueHz << endl;
	}
	else
	{
		currentFrequencyHz = valueHz;
		cout << "Frequency set to (Hz): " << valueHz << endl;
	}
	return err;
}

mir_sdr_ErrT mir_sdr_device::reinit_Frequency(int valueHz)
{
	mir_sdr_ErrT err = mir_sdr_Fail;

	int samplesPerPacket;

	err = mir_sdr_Reinit(&gainReduction,
		currentSamplingRateHz / 1e6,
		(double)valueHz / 1e6,
		(mir_sdr_Bw_MHzT)0,//mir_sdr_Bw_MHzT.mir_sdr_BW_1_536,
		(mir_sdr_If_kHzT)0,//mir_sdr_If_kHzT.mir_sdr_IF_Zero,
		(mir_sdr_LoModeT)0,//mir_sdr_LoModeT.mir_sdr_LO_Undefined,
		0,
		&sys,
		(mir_sdr_SetGrModeT)0,//mir_sdr_SetGrModeT.mir_sdr_USE_SET_GR,
		&samplesPerPacket,
		mir_sdr_CHANGE_RF_FREQ);
	cout << "\nmir_sdr_Reinit returned with: " << err << endl;
	if (err != mir_sdr_Success)
	{
		cout << "Requested Frequency (Hz) was: " << valueHz << endl;
	}
	else
	{
		cout << "Frequency set to (Hz): " << valueHz << endl;
		currentFrequencyHz = valueHz;
	}
	return err;
}

mir_sdr_ErrT mir_sdr_device::stream_InitForSamplingRate(int sampleConfigsTableIndex)
{
	int ix = sampleConfigsTableIndex;

	mir_sdr_Bw_MHzT bandwidth = samplingConfigs[ix].bandwidth;
	int reqSamplingRateHz = samplingConfigs[ix].samplingRateHz;
	int deviceSamplingRateHz = samplingConfigs[ix].deviceSamplingRateHz;
	unsigned int decimationFactor = samplingConfigs[ix].decimationFactor;
	unsigned int  doDecimation = samplingConfigs[ix].doDecimation ? 1 : 0;

	mir_sdr_ErrT err = mir_sdr_Fail;

	int samplesPerPacket;

	err = mir_sdr_StreamInit(&gainReduction,
		(double)deviceSamplingRateHz / 1e6,
		currentFrequencyHz / 1e6,
		bandwidth,
		mir_sdr_IF_Zero,
		0,
		&sys,
		mir_sdr_USE_SET_GR,
		&samplesPerPacket,
		streamCallback,
		gainChangeCallback,
		this);

	// ha: detailed output - including samplerate and bandwidth
	cout << "\nmir_sdr_StreamInit(bw " << bandwidth << " , srate " << deviceSamplingRateHz << ") returned with: " << err << endl;
	if (err != mir_sdr_Success)
	{
		cout << "Sampling Rate setting error: " << err << endl;
		cout << "Requested Sampling Rate was: " << reqSamplingRateHz << endl;
	}
	else
	{
		cout << "Sampling Rate set to (Hz): " << deviceSamplingRateHz << endl;
		currentSamplingRateHz = deviceSamplingRateHz;

		// ha: always configure decimation - also switch it off - in case previously activated
		if ( !doDecimation )
			decimationFactor = 1;
		err = mir_sdr_DecimateControl(doDecimation, decimationFactor, 0);
		cout << "mir_sdr_DecimateControl returned with: " << err << endl;
		if (doDecimation == 1)
		{
			if (err != mir_sdr_Success)
			{
				cout << "Requested Decimation Factor  was: " << decimationFactor << endl;
			}
			else
			{
				cout << "Decimation Factor set to  " << decimationFactor << endl;
			}

		}
		else
			cout << "No Decimation applied\n";
	}
	return err;
}

mir_sdr_ErrT mir_sdr_device::stream_Uninit()
{
	mir_sdr_ErrT err = mir_sdr_Fail;
	int cnt = 0;
	while (err != mir_sdr_Success)
	{
		cnt++;
		err = mir_sdr_StreamUninit();
		cout << "\nmir_sdr_StreamUninit returned with: " << err << endl;
		if (err == mir_sdr_Success)
			break;
		usleep(100000.0f);
		if (cnt == 5)
			break;
	}
	if (err != mir_sdr_Success)
	{
		cout << "StreamUninit failed with: " << err << endl;
	}
	return err;
}

/// <summary>
/// Gets the config table index for a requested sampling rate
/// </summary>
/// <param name="requestedSrHz">Requested sampling rate in Hz</param>
/// <returns>Index into the samplingConfigs table, -1 if not found</returns>
int mir_sdr_device::getSamplingConfigurationTableIndex(int requestedSrHz)
{
	for (int i = 0; i < c_numSamplingConfigs; i++)
	{
		samplingConfiguration sc = samplingConfigs[i];
		if (requestedSrHz == sc.samplingRateHz)
		{
			return i;
		}
	}
	printf("Invalid Sampling Rate: %d; Must be ", requestedSrHz);
	// ha: use single source - no duplicates of possible samplerates
	for (int i = 0; i < c_numSamplingConfigs; i++)
	{
		printf("%d%s",
			samplingConfigs[i].samplingRateHz,
			(i+1==c_numSamplingConfigs ? "\n" : " or ") );
	}
	return -1;
}



