// FBAlpha YM-2151 sound core interface
#include "burnint.h"
#include "burn_ym2151.h"

// Irq Callback timing notes..
// Due to the way the internal timing of the ym2151 works, BurnYM2151Render()
// should not be called more than ~65 times per frame.  See DrvFrame() in
// drv/konami/d_surpratk.cpp for a simple and effective work-around.

static UINT32 nBurnCurrentYM2151Register;

static INT32 nBurnYM2151SoundRate;

static INT16* pBuffer;
static INT16* pYM2151Buffer[2];

static INT32 bYM2151AddSignal;

static INT32 nYM2151Position;
static UINT32 nSampleSize;
static INT32 nFractionalPosition;
static UINT32 nSamplesRendered;

static double YM2151Volumes[2];
static INT32 YM2151RouteDirs[2];

static INT32 YM2151BurnTimer = 0;

static INT32 bBurnYM2151IsBuffered = 0;
static INT32 (*BurnYM2151StreamCallback)(INT32 nSoundRate) = NULL;


//  Buffered Zone
static void YM2151RenderBuffered(INT32 nSegmentLength)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_YM2151Initted) bprintf(PRINT_ERROR, _T("YM2151Render called without init\n"));
#endif

	if (nYM2151Position >= nSegmentLength) {
		return;
	}

//	bprintf(PRINT_NORMAL, _T("    YM2151 render %6i -> %6i\n"), nYM2151Position, nSegmentLength);

	nSegmentLength -= nYM2151Position;

	if (nSegmentLength < 1) return;

	pYM2151Buffer[0] = pBuffer + 0 * 4096 + 4 + nYM2151Position;
	pYM2151Buffer[1] = pBuffer + 1 * 4096 + 4 + nYM2151Position;

	YM2151UpdateOne(0, pYM2151Buffer, nSegmentLength);

	nYM2151Position += nSegmentLength;
}

void BurnYM2151UpdateRequest()
{
	if (bBurnYM2151IsBuffered) {
		YM2151RenderBuffered(BurnYM2151StreamCallback(nBurnYM2151SoundRate));
	}
}

// ----------------------------------------------------------------------------
// Update the sound buffer

#define INTERPOLATE_ADD_SOUND_LEFT(route, buffer)																	\
	if ((YM2151RouteDirs[route] & BURN_SND_ROUTE_LEFT) == BURN_SND_ROUTE_LEFT) {									\
		nLeftSample[0] += (INT32)(pYM2151Buffer[buffer][(nFractionalPosition >> 16) - 3]/* * YM2151Volumes[route]*/);	\
		nLeftSample[1] += (INT32)(pYM2151Buffer[buffer][(nFractionalPosition >> 16) - 2]/* * YM2151Volumes[route]*/);	\
		nLeftSample[2] += (INT32)(pYM2151Buffer[buffer][(nFractionalPosition >> 16) - 1]/* * YM2151Volumes[route]*/);	\
		nLeftSample[3] += (INT32)(pYM2151Buffer[buffer][(nFractionalPosition >> 16) - 0]/* * YM2151Volumes[route]*/);	\
	}

#define INTERPOLATE_ADD_SOUND_RIGHT(route, buffer)																	\
	if ((YM2151RouteDirs[route] & BURN_SND_ROUTE_RIGHT) == BURN_SND_ROUTE_RIGHT) {									\
		nRightSample[0] += (INT32)(pYM2151Buffer[buffer][(nFractionalPosition >> 16) - 3]/* * YM2151Volumes[route]*/);	\
		nRightSample[1] += (INT32)(pYM2151Buffer[buffer][(nFractionalPosition >> 16) - 2]/* * YM2151Volumes[route]*/);	\
		nRightSample[2] += (INT32)(pYM2151Buffer[buffer][(nFractionalPosition >> 16) - 1]/* * YM2151Volumes[route]*/);	\
		nRightSample[3] += (INT32)(pYM2151Buffer[buffer][(nFractionalPosition >> 16) - 0]/* * YM2151Volumes[route]*/);	\
	}

void BurnYM2151RenderBuffered(INT32 nSegmentEnd)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_YM2151Initted) bprintf(PRINT_ERROR, _T("BurnYM2151Update called without init\n"));
#endif

	INT16* pSoundBuf = pBurnSoundOut;

	if (nBurnSoundRate == 0 || pBurnSoundOut == NULL) {
		return;
	}

	if (nSegmentEnd != nBurnSoundLen) {
		bprintf(0, _T("BurnYM2151RenderBuffered() - once per frame, please!\n"));
		return;
	}

//	bprintf(PRINT_NORMAL, _T("    YM2151 render %6i -> %6i\n"), nYM2151Position, nSegmentEnd);

	INT32 nSegmentLength = nSegmentEnd;
	INT32 nSamplesNeeded = nSegmentEnd * nBurnYM2151SoundRate / nBurnSoundRate + 1;

	if (nSamplesNeeded < nYM2151Position) {
		nSamplesNeeded = nYM2151Position;
	}

	if (nSegmentLength > nBurnSoundLen) {
		nSegmentLength = nBurnSoundLen;
	}
	nSegmentLength <<= 1;

	YM2151RenderBuffered(nSamplesNeeded);

	pYM2151Buffer[0] = pBuffer + 0 * 4096 + 4;
	pYM2151Buffer[1] = pBuffer + 1 * 4096 + 4;

	for (INT32 i = (nFractionalPosition & 0xFFFF0000) >> 15; i < nSegmentLength; i += 2, nFractionalPosition += nSampleSize) {
		INT32 nLeftSample[4] = {0, 0, 0, 0};
		INT32 nRightSample[4] = {0, 0, 0, 0};
		INT32 nTotalLeftSample, nTotalRightSample;

		INTERPOLATE_ADD_SOUND_LEFT  (BURN_SND_YM2151_YM2151_ROUTE_1, 0)
		INTERPOLATE_ADD_SOUND_RIGHT (BURN_SND_YM2151_YM2151_ROUTE_1, 0)
		INTERPOLATE_ADD_SOUND_LEFT  (BURN_SND_YM2151_YM2151_ROUTE_2, 1)
		INTERPOLATE_ADD_SOUND_RIGHT (BURN_SND_YM2151_YM2151_ROUTE_2, 1)

		nTotalLeftSample  = INTERPOLATE4PS_16BIT((nFractionalPosition >> 4) & 0x0fff, nLeftSample[0], nLeftSample[1], nLeftSample[2], nLeftSample[3]);
		nTotalRightSample = INTERPOLATE4PS_16BIT((nFractionalPosition >> 4) & 0x0fff, nRightSample[0], nRightSample[1], nRightSample[2], nRightSample[3]);

		nTotalLeftSample  = BURN_SND_CLIP(nTotalLeftSample * YM2151Volumes[BURN_SND_YM2151_YM2151_ROUTE_1]);
		nTotalRightSample = BURN_SND_CLIP(nTotalRightSample * YM2151Volumes[BURN_SND_YM2151_YM2151_ROUTE_2]);

		if (bYM2151AddSignal) {
			pSoundBuf[i + 0] = BURN_SND_CLIP(pSoundBuf[i + 0] + nTotalLeftSample);
			pSoundBuf[i + 1] = BURN_SND_CLIP(pSoundBuf[i + 1] + nTotalRightSample);
		} else {
			pSoundBuf[i + 0] = nTotalLeftSample;
			pSoundBuf[i + 1] = nTotalRightSample;
		}
	}

	if (nSegmentEnd >= nBurnSoundLen) {
		INT32 nExtraSamples = nSamplesNeeded - (nFractionalPosition >> 16);

		for (INT32 i = -4; i < nExtraSamples; i++) {
			pYM2151Buffer[0][i] = pYM2151Buffer[0][(nFractionalPosition >> 16) + i];
			pYM2151Buffer[1][i] = pYM2151Buffer[1][(nFractionalPosition >> 16) + i];
		}

		nFractionalPosition &= 0xFFFF;

		nYM2151Position = nExtraSamples;
	}
}



void BurnYM2151Render(INT16* pSoundBuf, INT32 nSegmentLength)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_YM2151Initted) bprintf(PRINT_ERROR, _T("BurnYM2151Render called without init\n"));
#endif

	if (bBurnYM2151IsBuffered) {
		BurnYM2151RenderBuffered(nSegmentLength);
		return;
	}

	nYM2151Position += nSegmentLength;

	if (nYM2151Position >= nBurnSoundRate) {
		nYM2151Position = nSegmentLength;

		pYM2151Buffer[0][1] = pYM2151Buffer[0][(nFractionalPosition >> 16) - 3];
		pYM2151Buffer[0][2] = pYM2151Buffer[0][(nFractionalPosition >> 16) - 2];
		pYM2151Buffer[0][3] = pYM2151Buffer[0][(nFractionalPosition >> 16) - 1];

		pYM2151Buffer[1][1] = pYM2151Buffer[1][(nFractionalPosition >> 16) - 3];
		pYM2151Buffer[1][2] = pYM2151Buffer[1][(nFractionalPosition >> 16) - 2];
		pYM2151Buffer[1][3] = pYM2151Buffer[1][(nFractionalPosition >> 16) - 1];

		nSamplesRendered -= (nFractionalPosition >> 16) - 4;

		for (UINT32 i = 0; i <= nSamplesRendered; i++) {
			pYM2151Buffer[0][4 + i] = pYM2151Buffer[0][(nFractionalPosition >> 16) + i];
			pYM2151Buffer[1][4 + i] = pYM2151Buffer[1][(nFractionalPosition >> 16) + i];
		}

		nFractionalPosition &= 0x0000FFFF;
		nFractionalPosition |= 4 << 16;
	}

	pYM2151Buffer[0] = pBuffer + 4 + nSamplesRendered;
	pYM2151Buffer[1] = pBuffer + 4 + nSamplesRendered + 65536;

	YM2151UpdateOne(0, pYM2151Buffer, (UINT32)(nYM2151Position + 1) * nBurnYM2151SoundRate / nBurnSoundRate - nSamplesRendered);
	nSamplesRendered += (UINT32)(nYM2151Position + 1) * nBurnYM2151SoundRate / nBurnSoundRate - nSamplesRendered;

	pYM2151Buffer[0] = pBuffer;
	pYM2151Buffer[1] = pBuffer + 65536;

	nSegmentLength <<= 1;
	
	for (INT32 i = 0; i < nSegmentLength; i += 2, nFractionalPosition += nSampleSize) {
		INT32 nLeftSample[4] = {0, 0, 0, 0};
		INT32 nRightSample[4] = {0, 0, 0, 0};
		INT32 nTotalLeftSample, nTotalRightSample;
		
		if ((YM2151RouteDirs[BURN_SND_YM2151_YM2151_ROUTE_1] & BURN_SND_ROUTE_LEFT) == BURN_SND_ROUTE_LEFT) {
			nLeftSample[0] += (INT32)(pYM2151Buffer[0][(nFractionalPosition >> 16) - 3] * YM2151Volumes[BURN_SND_YM2151_YM2151_ROUTE_1]);
			nLeftSample[1] += (INT32)(pYM2151Buffer[0][(nFractionalPosition >> 16) - 2] * YM2151Volumes[BURN_SND_YM2151_YM2151_ROUTE_1]);
			nLeftSample[2] += (INT32)(pYM2151Buffer[0][(nFractionalPosition >> 16) - 1] * YM2151Volumes[BURN_SND_YM2151_YM2151_ROUTE_1]);
			nLeftSample[3] += (INT32)(pYM2151Buffer[0][(nFractionalPosition >> 16) - 0] * YM2151Volumes[BURN_SND_YM2151_YM2151_ROUTE_1]);
		}
		if ((YM2151RouteDirs[BURN_SND_YM2151_YM2151_ROUTE_1] & BURN_SND_ROUTE_RIGHT) == BURN_SND_ROUTE_RIGHT) {
			nRightSample[0] += (INT32)(pYM2151Buffer[0][(nFractionalPosition >> 16) - 3] * YM2151Volumes[BURN_SND_YM2151_YM2151_ROUTE_1]);
			nRightSample[1] += (INT32)(pYM2151Buffer[0][(nFractionalPosition >> 16) - 2] * YM2151Volumes[BURN_SND_YM2151_YM2151_ROUTE_1]);
			nRightSample[2] += (INT32)(pYM2151Buffer[0][(nFractionalPosition >> 16) - 1] * YM2151Volumes[BURN_SND_YM2151_YM2151_ROUTE_1]);
			nRightSample[3] += (INT32)(pYM2151Buffer[0][(nFractionalPosition >> 16) - 0] * YM2151Volumes[BURN_SND_YM2151_YM2151_ROUTE_1]);
		}
		
		if ((YM2151RouteDirs[BURN_SND_YM2151_YM2151_ROUTE_2] & BURN_SND_ROUTE_LEFT) == BURN_SND_ROUTE_LEFT) {
			nLeftSample[0] += (INT32)(pYM2151Buffer[1][(nFractionalPosition >> 16) - 3] * YM2151Volumes[BURN_SND_YM2151_YM2151_ROUTE_2]);
			nLeftSample[1] += (INT32)(pYM2151Buffer[1][(nFractionalPosition >> 16) - 2] * YM2151Volumes[BURN_SND_YM2151_YM2151_ROUTE_2]);
			nLeftSample[2] += (INT32)(pYM2151Buffer[1][(nFractionalPosition >> 16) - 1] * YM2151Volumes[BURN_SND_YM2151_YM2151_ROUTE_2]);
			nLeftSample[3] += (INT32)(pYM2151Buffer[1][(nFractionalPosition >> 16) - 0] * YM2151Volumes[BURN_SND_YM2151_YM2151_ROUTE_2]);
		}
		if ((YM2151RouteDirs[BURN_SND_YM2151_YM2151_ROUTE_2] & BURN_SND_ROUTE_RIGHT) == BURN_SND_ROUTE_RIGHT) {
			nRightSample[0] += (INT32)(pYM2151Buffer[1][(nFractionalPosition >> 16) - 3] * YM2151Volumes[BURN_SND_YM2151_YM2151_ROUTE_2]);
			nRightSample[1] += (INT32)(pYM2151Buffer[1][(nFractionalPosition >> 16) - 2] * YM2151Volumes[BURN_SND_YM2151_YM2151_ROUTE_2]);
			nRightSample[2] += (INT32)(pYM2151Buffer[1][(nFractionalPosition >> 16) - 1] * YM2151Volumes[BURN_SND_YM2151_YM2151_ROUTE_2]);
			nRightSample[3] += (INT32)(pYM2151Buffer[1][(nFractionalPosition >> 16) - 0] * YM2151Volumes[BURN_SND_YM2151_YM2151_ROUTE_2]);
		}
		
		nTotalLeftSample = INTERPOLATE4PS_CUSTOM((nFractionalPosition >> 4) & 0x0fff, nLeftSample[0], nLeftSample[1], nLeftSample[2], nLeftSample[3], 16384.0);
		nTotalRightSample = INTERPOLATE4PS_CUSTOM((nFractionalPosition >> 4) & 0x0fff, nRightSample[0], nRightSample[1], nRightSample[2], nRightSample[3], 16384.0);
		
		nTotalLeftSample = BURN_SND_CLIP(nTotalLeftSample);
		nTotalRightSample = BURN_SND_CLIP(nTotalRightSample);
			
		pSoundBuf[i + 0] = nTotalLeftSample;
		pSoundBuf[i + 1] = nTotalRightSample;
	}
}

void BurnYM2151Reset()
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_YM2151Initted) bprintf(PRINT_ERROR, _T("BurnYM2151Reset called without init\n"));
#endif

	if (YM2151BurnTimer)
		BurnTimerReset();

	YM2151ResetChip(0);
}

void BurnYM2151Exit()
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_YM2151Initted) bprintf(PRINT_ERROR, _T("BurnYM2151Exit called without init\n"));
#endif

	if (!DebugSnd_YM2151Initted) return;

	BurnYM2151SetIrqHandler(NULL);
	BurnYM2151SetPortHandler(NULL);

	YM2151Shutdown();

	if (YM2151BurnTimer) {
		BurnTimerExit();
		YM2151BurnTimer = 0;
	}

	BurnFree(pBuffer);

	bBurnYM2151IsBuffered = 0;
	BurnYM2151StreamCallback = NULL;
	bYM2151AddSignal = 0;

	DebugSnd_YM2151Initted = 0;
}

void BurnYM2151InitBuffered(INT32 nClockFrequency, INT32 use_timer, INT32 (*StreamCallback)(INT32), INT32 bAdd)
{
	BurnYM2151Init(nClockFrequency, use_timer);

	if (use_timer && StreamCallback == NULL) {
		StreamCallback = BurnSynchroniseStream; // BurnTimer has us covered!
	}

	bBurnYM2151IsBuffered = (StreamCallback != NULL);
	BurnYM2151StreamCallback = StreamCallback;

	if (bBurnYM2151IsBuffered) {
		bprintf(0, _T("YM2151: Using Buffered-mode.\n"));
	}

	bYM2151AddSignal = bAdd;
}

INT32 BurnYM2151Init(INT32 nClockFrequency)
{
	return BurnYM2151Init(nClockFrequency, 0);
}

INT32 BurnYM2151Init(INT32 nClockFrequency, INT32 use_timer)
{
	DebugSnd_YM2151Initted = 1;

	bBurnYM2151IsBuffered = 0; // Can I recommend BurnYM2151InitBuffered()? :)
	BurnYM2151StreamCallback = NULL; // ""
	bYM2151AddSignal = 0; // ""

	if (nBurnSoundRate <= 0) {
		YM2151Init(1, nClockFrequency, 11025, NULL);
		return 0;
	}

	// Set YM2151 core samplerate to match the hardware
	nBurnYM2151SoundRate = nClockFrequency >> 6;
	// Bring YM2151 core samplerate within usable range
	while (nBurnYM2151SoundRate > nBurnSoundRate * 3) {
		nBurnYM2151SoundRate >>= 1;
	}

	if (use_timer)
	{
		bprintf(0, _T("YM2151: Using FM-Timer.\n"));
		YM2151BurnTimer = 1;
		BurnTimerInit(&ym2151_timer_over, NULL);
	}

	YM2151Init(1, nClockFrequency, nBurnYM2151SoundRate, (YM2151BurnTimer) ? BurnOPMTimerCallback : NULL);

	pBuffer = (INT16*)BurnMalloc(65536 * 2 * sizeof(INT16));
	memset(pBuffer, 0, 65536 * 2 * sizeof(INT16));

	nSampleSize = (UINT32)nBurnYM2151SoundRate * (1 << 16) / nBurnSoundRate;
	nFractionalPosition = 4 << 16;
	nSamplesRendered = 0;
	nYM2151Position = 0;

	// default routes
	YM2151Volumes[BURN_SND_YM2151_YM2151_ROUTE_1] = 1.00;
	YM2151Volumes[BURN_SND_YM2151_YM2151_ROUTE_2] = 1.00;
	YM2151RouteDirs[BURN_SND_YM2151_YM2151_ROUTE_1] = BURN_SND_ROUTE_BOTH;
	YM2151RouteDirs[BURN_SND_YM2151_YM2151_ROUTE_2] = BURN_SND_ROUTE_BOTH;

	return 0;
}

// Some games make heavy use of the B timer, to increase the accuracy of
// this timer, call this with the number of times per frame BurnYM2151Render()
// is called.  See drv/dataeast/d_rohga for example usage.
void BurnYM2151SetInterleave(INT32 nInterleave)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_YM2151Initted) bprintf(PRINT_ERROR, _T("BurnYM2151SetInterleave called without init\n"));
#endif

	YM2151SetTimerInterleave(nInterleave * (nBurnFPS / 100));
}

void BurnYM2151SetRoute(INT32 nIndex, double nVolume, INT32 nRouteDir)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_YM2151Initted) bprintf(PRINT_ERROR, _T("BurnYM2151SetRoute called without init\n"));
	if (nIndex < 0 || nIndex > 1) bprintf(PRINT_ERROR, _T("BurnYM2151SetRoute called with invalid index %i\n"), nIndex);
#endif
	
	YM2151Volumes[nIndex] = nVolume;
	YM2151RouteDirs[nIndex] = nRouteDir;
}

void BurnYM2151Scan(INT32 nAction, INT32 *pnMin)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_YM2151Initted) bprintf(PRINT_ERROR, _T("BurnYM2151Scan called without init\n"));
#endif
	
	if ((nAction & ACB_DRIVER_DATA) == 0) {
		return;
	}

	SCAN_VAR(nBurnCurrentYM2151Register);

	BurnYM2151Scan_int(nAction); // Scan the YM2151's internal registers

	if (YM2151BurnTimer)
		BurnTimerScan(nAction, pnMin);
}

void BurnYM2151Write(INT32 offset, const UINT8 nData)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_YM2151Initted) bprintf(PRINT_ERROR, _T("BurnYM2151Write called without init\n"));
#endif

	if (offset & 1) {
		BurnYM2151UpdateRequest();
		YM2151WriteReg(0, nBurnCurrentYM2151Register, nData);
	} else {
		nBurnCurrentYM2151Register = nData;
	}
}

void BurnYM2151SelectRegister(const UINT8 nRegister)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_YM2151Initted) bprintf(PRINT_ERROR, _T("BurnYM2151SelectRegister called without init\n"));
#endif

	nBurnCurrentYM2151Register = nRegister;
}

void BurnYM2151WriteRegister(const UINT8 nValue)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_YM2151Initted) bprintf(PRINT_ERROR, _T("BurnYM2151WriteRegister called without init\n"));
#endif

	BurnYM2151UpdateRequest();
	YM2151WriteReg(0, nBurnCurrentYM2151Register, nValue);
}

UINT8 BurnYM2151Read()
{
	//  status contains: chip busy status (not emulated) and timer state.
	//  when using BurnTimer, this timer state is asynchronous to rendering
	//  state; therefore, updaterequest here is not needed.  Most games poll
	//  status this 800x+ per frame!
	//	BurnYM2151UpdateRequest();
	return YM2151ReadStatus(0);
}

