/*****************************************************************************
 *
 * Copyright (C) 2008 Thomas Volkert <thomas@homer-conferencing.com>
 *
 * This software is free software.
 * Your are allowed to redistribute it and/or modify it under the terms of
 * the GNU General Public License version 2 as published by the Free Software
 * Foundation.
 *
 * This source is published in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License version 2 for more details.
 *
 * You should have received a copy of the GNU General Public License version 2
 * along with this program. Otherwise, you can write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 * Alternatively, you find an online version of the license text under
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 *****************************************************************************/

/*
 * Purpose: Implementation of an abstract media source
 * Author:  Thomas Volkert
 * Since:   2008-12-02
 */

/*
    supported codecs:
        http://ffmpeg.org/general.html#SEC6

    codec licenses:

    H.261: built-in ffmpeg: LGPL
    H.263: built-in ffmpeg: LGPL
    H.263+: built-in ffmpeg: LGPL
    H.264: GPL-2
    MPEG2: built-in ffmpeg: LGPL
    MPEG4: built-in ffmpeg: LGPL
    MJPEG: LGPL

    AC3: decoder: built-in ffmpeg: LGPL, encoder: tbd??
    AAC: built-in ffmpeg: LGPL, encoder: libfaac LGPL-2
    MP3: decoder: built-in ffmpeg: LGPL, encoder: LGPL, http://lame.sourceforge.net
    GSM FREE, http://kbs.cs.tu-berlin.de/~jutta/toast.html
    A-LAW: built-in ffmpeg: LGPL
    MU_LAW built-in ffmpeg: LGPL
    PCM_S16LE: built-in ffmpeg: LGPL
    AMR_NB: apache-2.0 license
 */

#include <Header_Ffmpeg.h>
#include <MediaSource.h>
#include <Logger.h>

#include <string>

namespace Homer { namespace Multimedia {

using namespace std;
using namespace Homer::Base;
using namespace Homer::Monitor;

///////////////////////////////////////////////////////////////////////////////

Mutex MediaSource::mFfmpegInitMutex;
bool MediaSource::mFfmpegInitiated = false;

MediaSource::MediaSource(string pName):
    PacketStatistic(pName)
{
    mMediaSourceOpened = false;
    mGrabbingStopped = false;
    mRecording = false;
    SetRtpActivation(true);
    mCodecContext = NULL;
    mRecorderCodecContext = NULL;
    mRecorderFormatContext = NULL;
    mRecorderScalerContext = NULL;
    mFormatContext = NULL;
    mRecordingSaveFileName = "";
    mDesiredDevice = "";
    mCurrentDevice = "";
    mCurrentDeviceName = "";
    mRecorderRealTime = true;
    mLastGrabResultWasError = false;
    mDuration = 0;
    mChunkNumber = 0;
    mChunkDropCounter = 0;
    mSampleRate = 44100;
    mStereo = true;
    mSourceResX = 352;
    mSourceResY = 288;
    mTargetResX = 352;
    mTargetResY = 288;
    mFrameRate = 29.97;
    mCurrentInputChannel = 0;
    mDesiredInputChannel = 0;
    mMediaType = MEDIA_UNKNOWN;

    mFfmpegInitMutex.lock();
    if(!mFfmpegInitiated)
    {
		// console logging of FFMPG
		if (LOGGER.GetLogLevel() == LOG_VERBOSE)
			av_log_set_level(AV_LOG_DEBUG);
		else
			av_log_set_level(AV_LOG_QUIET);

		// Register all formats and codecs
		av_register_all();

		// init network support once instead for every stream
		//avformat_network_init();

		// Register all supported input and output devices
		avdevice_register_all();

		// register our own lock manager at ffmpeg
		//HINT: we can do this as many times as this class is instanced
		if(av_lockmgr_register(FfmpegLockManager))
			LOG(LOG_ERROR, "Registration of own lock manager at ffmpeg failed.");

		mFfmpegInitiated = true;
    }
    mFfmpegInitMutex.unlock();

    // ###################################################################
    // ### add all 6 default video formats to the list of supported ones
    // ###################################################################
    VideoFormatDescriptor tFormat;

    tFormat.Name="SQCIF";      //      128 �  96
    tFormat.ResX = 128;
    tFormat.ResY = 96;
    mSupportedVideoFormats.push_back(tFormat);

    tFormat.Name="QCIF";       //      176 � 144
    tFormat.ResX = 176;
    tFormat.ResY = 144;
    mSupportedVideoFormats.push_back(tFormat);

    tFormat.Name="CIF";        //      352 � 288
    tFormat.ResX = 352;
    tFormat.ResY = 288;
    mSupportedVideoFormats.push_back(tFormat);

    tFormat.Name="CIF4";       //      704 � 576
    tFormat.ResX = 704;
    tFormat.ResY = 576;
    mSupportedVideoFormats.push_back(tFormat);

    tFormat.Name="DVD";        //      720 � 576
    tFormat.ResX = 720;
    tFormat.ResY = 576;
    mSupportedVideoFormats.push_back(tFormat);

    tFormat.Name="CIF9";       //     1056 � 864
    tFormat.ResX = 1056;
    tFormat.ResY = 864;
    mSupportedVideoFormats.push_back(tFormat);

    tFormat.Name="EDTV";       //     1280 � 720
    tFormat.ResX = 1280;
    tFormat.ResY = 720;
    mSupportedVideoFormats.push_back(tFormat);

    tFormat.Name="CIF16";      //     1408 � 1152
    tFormat.ResX = 1408;
    tFormat.ResY = 1152;
    mSupportedVideoFormats.push_back(tFormat);

    tFormat.Name="HDTV";       //     1920 � 1080
    tFormat.ResX = 1920;
    tFormat.ResY = 1080;
    mSupportedVideoFormats.push_back(tFormat);
}

MediaSource::~MediaSource()
{
    DeleteAllRegisteredMediaSinks();
}

///////////////////////////////////////////////////////////////////////////////

bool MediaSource::SupportsMuxing()
{
    return false;
}

string MediaSource::GetMuxingCodec()
{
    return "";
}

void MediaSource::GetMuxingResolution(int &pResX, int &pResY)
{
    pResX = 0;
    pResY = 0;
}

int MediaSource::GetMuxingBufferCounter()
{
    return 0;
}

int MediaSource::GetMuxingBufferSize()
{
    return 0;
}

MediaSource* MediaSource::GetMediaSource()
{
    LOG(LOG_VERBOSE, "This is only the dummy function");
    return this;
}

int MediaSource::FfmpegLockManager(void **pMutex, enum AVLockOp pMutexOperation)
{
    int tTmpResult = 1;
    Mutex *tMutex;

    switch(pMutexOperation)
    {
        case AV_LOCK_CREATE:
            #ifdef MS_DEBUG_FFMPEG_MUTEX
                LOG(LOG_VERBOSE, "Creating FFMPEG mutex");
            #endif
            *pMutex = new Mutex();
            return 0;
        case AV_LOCK_OBTAIN:
            #ifdef MS_DEBUG_FFMPEG_MUTEX
                LOG(LOG_VERBOSE, "Going to lock FFMPEG mutex");
            #endif
            tMutex = (Mutex*)*pMutex;
            tTmpResult = !tMutex->lock();
            #ifdef MS_DEBUG_FFMPEG_MUTEX
                LOG(LOG_VERBOSE, "FFMPEG mutex locked");
            #endif
            return tTmpResult;
        case AV_LOCK_RELEASE:
            #ifdef MS_DEBUG_FFMPEG_MUTEX
                LOG(LOG_VERBOSE, "Going to unlock FFMPEG mutex");
            #endif
            tMutex = (Mutex*)*pMutex;
            tTmpResult = !tMutex->unlock();
            #ifdef MS_DEBUG_FFMPEG_MUTEX
                LOG(LOG_VERBOSE, "FFMPEG mutex unlock");
            #endif
            return tTmpResult;
        case AV_LOCK_DESTROY:
            #ifdef MS_DEBUG_FFMPEG_MUTEX
                LOG(LOG_VERBOSE, "Destroying FFMPEG mutex");
            #endif
            tMutex = (Mutex*)*pMutex;
            delete tMutex;
            return 0;
        default:
        	LOGEX(MediaSource, LOG_ERROR, "We should never reach this point. Unknown mutex operation requested by ffmpeg library.");
    }
    return 1;
}

std::string MediaSource::CodecName2FfmpegName(std::string pStdName)
{
    string tResult = "h261";

    /* video */
    // translate from standardized names to FFMPEG internal names
    if (pStdName == "H.261")
        tResult = "h261";
    if (pStdName == "H.263")
        tResult = "h263";
    if (pStdName == "H.263+")
        tResult = "h263+";
    if (pStdName == "H.264")
        tResult = "h264";
    if (pStdName == "MPEG1")
        tResult = "mpeg1video";
    if (pStdName == "MPEG2")
        tResult = "mpeg2video";
    if (pStdName == "MPEG4")
        tResult = "m4v";
    if (pStdName == "MJPEG")
        tResult = "mjpeg";
    if (pStdName == "THEORA")
        tResult = "theora";

    /* audio */
    // translate from standardized names to FFMPEG internal names
    if (pStdName == "AC3")
        tResult = "ac3";
    if (pStdName == "AAC")
        tResult = "aac";
    if (pStdName == "MP3 (MPA)")
        tResult = "mp3";
    if (pStdName == "GSM")
        tResult = "gsm";
    if (pStdName == "G711 A-law (PCMA)")
        tResult = "alaw";
    if (pStdName == "G711 �-law (PCMU)")
        tResult = "mulaw";
    if (pStdName == "PCM_S16_LE")
        tResult = "pcms16le";
    if (pStdName == "AMR")
        tResult = "amr";

    //LOG(LOG_VERBOSE, "Translated %s to %s", pStdName.c_str(), tResult.c_str());

    return tResult;
}

enum CodecID MediaSource::FfmpegName2FfmpegId(std::string pName)
{
    enum CodecID tResult = CODEC_ID_NONE;

    /* video */
    // translate from standardized names to FFMPEG internal names
    if (pName == "h261")
        tResult = CODEC_ID_H261;
    if (pName == "h263")
        tResult = CODEC_ID_H263;
    if ((pName == "h263+") || (pName == "h263p"))
        tResult = CODEC_ID_H263P;
    if ((pName == "h264") || (pName == "libx264"))
        tResult = CODEC_ID_H264;
    if ((pName == "mpeg1") || (pName == "mpeg1video"))
        tResult = CODEC_ID_MPEG1VIDEO;
    if ((pName == "mpeg2") || (pName == "mpeg2video"))
        tResult = CODEC_ID_MPEG2VIDEO;
    if ((pName == "m4v") || (pName == "mpeg4"))
        tResult = CODEC_ID_MPEG4;
    if (pName == "mjpeg")
        tResult = CODEC_ID_MJPEG;
    if ((pName == "theora") || (pName == "THEORA") || (pName == "libtheora"))
        tResult = CODEC_ID_THEORA;

    /* audio */
    // translate from standardized names to FFMPEG internal names
    if (pName == "ac3")
        tResult = CODEC_ID_AC3;
    if ((pName == "aac") || (pName == "libfaac"))
        tResult = CODEC_ID_AAC;
    if ((pName == "mp3") || (pName == "libmp3lame"))
        tResult = CODEC_ID_MP3;
    if (pName == "gsm")
        tResult = CODEC_ID_GSM;
    if ((pName == "alaw") || (pName == "pcm_alaw"))
        tResult = CODEC_ID_PCM_ALAW;
    if ((pName == "mulaw") || (pName == "pcm_mulaw"))
        tResult = CODEC_ID_PCM_MULAW;
    if (pName == "pcms16le")
        tResult = CODEC_ID_PCM_S16LE;
    if (pName == "amr")
        tResult = CODEC_ID_AMR_NB;

    //LOG(LOG_VERBOSE, "Translated %s to %d", pName.c_str(), tResult);

    return tResult;
}

string MediaSource::FfmpegName2FfmpegFormat(std::string pName)
{
    string tResult = "";

    /* video */
    // translate from standardized names to FFMPEG internal names
    if (pName == "rawvideo")
        tResult = "raw";
    if (pName == "h261")
        tResult = "h261";
    if (pName == "h263")
        tResult = "h263";
    if ((pName == "h263+") || (pName == "h263p"))
        tResult = "h263"; // HINT: ffmpeg has no separate h263+ format
    if ((pName == "h264") || (pName == "libx264")) //GPL-2
        tResult = "h264";
    if ((pName == "mpeg1") || (pName == "mpeg1video"))
        tResult = "mpeg1video";
    if ((pName == "mpeg2") || (pName == "mpeg2video"))
        tResult = "mpeg2video";
    if ((pName == "m4v") || (pName == "mpeg4"))
        tResult = "mpeg4";
    if (pName == "mjpeg")
        tResult = "mjpeg";
    if ((pName == "theora") || (pName == "THEORA") || (pName == "libtheora"))
        tResult = "theora";

    /* audio */
    // translate from standardized names to FFMPEG internal names
    if (pName == "ac3")
        tResult = "ac3";
    if ((pName == "aac") || (pName == "libfaac"))
        tResult = "aac";
    if ((pName == "mp3") || (pName == "libmp3lame"))
        tResult = "mp3";
    if ((pName == "gsm") || (pName == "libgsm"))
        tResult = "gsm";
    if ((pName == "alaw") || (pName == "pcm_alaw"))
        tResult = "alaw";
    if ((pName == "mulaw") || (pName == "pcm_mulaw"))
        tResult = "mulaw";
    if (pName == "pcms16le")
        tResult = "pcm_s16le";
    if (pName == "amr")
        tResult = "amr";

    //LOG(LOG_VERBOSE, "Translated %s to format %s", pName.c_str(), tResult.c_str());

    return tResult;
}

string MediaSource::FfmpegId2FfmpegFormat(enum CodecID pCodecId)
{
    string tResult = "";

    /* video */
    // translate from standardized names to FFMPEG internal names
    if (pCodecId == CODEC_ID_H261)
        tResult = "h261";
    if (pCodecId == CODEC_ID_H263)
        tResult = "h263";
    if (pCodecId == CODEC_ID_H263P)
        tResult = "h263"; // HINT: ffmpeg has no separate h263+ format
    if (pCodecId == CODEC_ID_H264)
        tResult = "h264";
    if (pCodecId == CODEC_ID_MPEG1VIDEO)
        tResult = "mpeg1video";
    if (pCodecId == CODEC_ID_MPEG2VIDEO)
        tResult = "mpeg2video";
    if (pCodecId == CODEC_ID_MPEG4)
        tResult = "m4v";
    if (pCodecId == CODEC_ID_MJPEG)
        tResult = "mjpeg";
    if (pCodecId == CODEC_ID_THEORA)
        tResult = "theora";

    /* audio */
    // translate from standardized names to FFMPEG internal names
    if (pCodecId == CODEC_ID_AC3)
        tResult = "ac3";
    if (pCodecId == CODEC_ID_AAC)
        tResult = "aac";
    if (pCodecId == CODEC_ID_MP3)
        tResult = "mp3";
    if (pCodecId == CODEC_ID_GSM)
        tResult = "libgsm";
    if (pCodecId == CODEC_ID_PCM_ALAW)
        tResult = "alaw";
    if (pCodecId == CODEC_ID_PCM_MULAW)
        tResult = "mulaw";
    if (pCodecId == CODEC_ID_PCM_S16LE)
        tResult = "s16le";
    if (pCodecId == CODEC_ID_AMR_NB)
        tResult = "amr";

    //LOGEX(MediaSource, LOG_VERBOSE, "Translated codec id %d to format %s", pCodecId, tResult.c_str());

    return tResult;
}

enum MediaType MediaSource::FfmpegName2MediaType(std::string pName)
{
    enum MediaType tResult = MEDIA_UNKNOWN;

    /* video */
    // translate from standardized names to FFMPEG internal names
    if (pName == "h261")
        tResult = MEDIA_VIDEO;
    if (pName == "h263")
        tResult = MEDIA_VIDEO;
    if (pName == "h263+")
        tResult = MEDIA_VIDEO;
    if (pName == "h264") //GPL-2
        tResult = MEDIA_VIDEO;
    if (pName == "mpeg1video")
        tResult = MEDIA_VIDEO;
    if (pName == "mpeg2video")
        tResult = MEDIA_VIDEO;
    if (pName == "m4v")
        tResult = MEDIA_VIDEO;
    if (pName == "mjpeg")
        tResult = MEDIA_VIDEO;
    if (pName == "theora")
        tResult = MEDIA_VIDEO;

    /* audio */
    // translate from standardized names to FFMPEG internal names
    if (pName == "ac3")
        tResult = MEDIA_AUDIO;
    if (pName == "aac")
        tResult = MEDIA_AUDIO;
    if (pName == "mp3")
        tResult = MEDIA_AUDIO;
    if (pName == "gsm")
        tResult = MEDIA_AUDIO;
    if (pName == "alaw")
        tResult = MEDIA_AUDIO;
    if (pName == "mulaw")
        tResult = MEDIA_AUDIO;
    if (pName == "pcms16le")
        tResult = MEDIA_AUDIO;
    if (pName == "amr")
        tResult = MEDIA_AUDIO;

    //LOG(LOG_VERBOSE, "Translated %s to %d", pName.c_str(), tResult);

    return tResult;
}

int MediaSource::AudioQuality2BitRate(int pQuality)
{
    int tResult = 128000;

    switch(pQuality)
    {
        case 100:
                tResult = 256000;
                break;
        case 90:
                tResult = 128000;
                break;
        case 80:
                tResult = 96000;
                break;
        case 70:
                tResult = 64000;
                break;
        case 60:
                tResult = 56000;
                break;
        case 50:
                tResult = 48000;
                break;
        case 40:
                tResult = 32000;
                break;
        case 30:
                tResult = 24000;
                break;
        case 20:
                tResult = 16000;
                break;
        case 10:
                tResult = 8000;
                break;
    }

    return tResult;
}

int MediaSource::GetSampleRate()
{
    return mSampleRate;
}

AVFrame *MediaSource::AllocFrame()
{
	return avcodec_alloc_frame();
}

int MediaSource::FillFrame(AVFrame *pFrame, void *pData, enum PixelFormat pPixFormat, int pWidth, int pHeight)
{
	return avpicture_fill((AVPicture *)pFrame, (uint8_t *)pData, pPixFormat, pWidth, pHeight);
}

void MediaSource::VideoFormat2Resolution(VideoFormat pFormat, int& pX, int& pY)
{
    switch(pFormat)
    {

        case SQCIF:      /*      128 �  96       */
                    pX = 128;
                    pY = 96;
                    break;
        case QCIF:       /*      176 � 144       */
                    pX = 176;
                    pY = 144;
                    break;
        case CIF:        /*      352 � 288       */
                    pX = 352;
                    pY = 288;
                    break;
        case CIF4:       /*      704 � 576       */
                    pX = 704;
                    pY = 576;
                    break;
        case DVD:        /*      720 � 576       */
                    pX = 720;
                    pY = 576;
                    break;
        case CIF9:       /*     1056 � 864       */
                    pX = 1056;
                    pY = 864;
                    break;
        case EDTV:       /*     1280 � 720       */
                    pX = 1280;
                    pY = 720;
                    break;
        case CIF16:      /*     1408 � 1152      */
                    pX = 1408;
                    pY = 1152;
                    break;
        case HDTV:       /*     1920 � 1080       */
                    pX = 1920;
                    pY = 1080;
                    break;
    }
}

void MediaSource::VideoString2Resolution(string pString, int& pX, int& pY)
{
    pX = 352;
    pY = 288;

    if (pString == "auto")
    {
        pX = -1;
        pY = -1;
    }
    if (pString == "128 * 96")
    {
        pX = 128;
        pY = 96;
    }
    if (pString == "176 * 144")
    {
        pX = 176;
        pY = 144;
    }
    if (pString == "352 * 288")
    {
        pX = 352;
        pY = 288;
    }
    if (pString == "704 * 576")
    {
        pX = 704;
        pY = 576;
    }
    if (pString == "720 * 576")
    {
        pX = 720;
        pY = 576;
    }
    if (pString == "1056 * 864")
    {
        pX = 1056;
        pY = 864;
    }
    if (pString == "1280 * 720")
    {
        pX = 1280;
        pY = 720;
    }
    if (pString == "1408 * 1152")
    {
        pX = 1408;
        pY = 1152;
    }
    if (pString == "1920 * 1080")
    {
        pX = 1920;
        pY = 1080;
    }
}

void MediaSource::DoSetVideoGrabResolution(int pResX, int pResY)
{
    CloseGrabDevice();
    OpenVideoGrabDevice(pResX, pResY, mFrameRate);
}

void MediaSource::SetVideoGrabResolution(int pResX, int pResY)
{
	if(!mRecording)
	{
		if ((pResX != mTargetResX) || (pResY != mTargetResY))
		{
			LOG(LOG_VERBOSE, "Setting video grabbing resolution to %d * %d", pResX, pResY);

			mTargetResX = pResX;
			mTargetResY = pResY;
			if ((mMediaType == MEDIA_VIDEO) && (mMediaSourceOpened))
			{
				// lock grabbing
				mGrabMutex.lock();

				DoSetVideoGrabResolution(mTargetResX, mTargetResY);

				// unlock grabbing
				mGrabMutex.unlock();
			}
		}
	}else
	{
		LOG(LOG_ERROR, "Can't change video resolution if recording is activated");
	}
}

void MediaSource::GetVideoGrabResolution(int &pResX, int &pResY)
{
    if (mMediaType == MEDIA_AUDIO)
    {
        LOG(LOG_ERROR, "Wrong media type detected");
        return;
    }

    pResX = mTargetResX;
    pResY = mTargetResY;
}

void MediaSource::GetVideoSourceResolution(int &pResX, int &pResY)
{
    if (mMediaType == MEDIA_AUDIO)
    {
        LOG(LOG_ERROR, "Wrong media type detected");
        return;
    }

    pResX = mSourceResX;
    pResY = mSourceResY;
}

GrabResolutions MediaSource::GetSupportedVideoGrabResolutions()
{
    if (mMediaType == MEDIA_AUDIO)
    {
        LOG(LOG_ERROR, "Wrong media type detected");
        GrabResolutions tRes;
        return tRes;
    }

    return mSupportedVideoFormats;
}

void MediaSource::StopGrabbing()
{
    mGrabbingStopped = true;
}

bool MediaSource::Reset(enum MediaType pMediaType)
{
	bool tResult = false;

    // HINT: closing the grab device resets the media type!
    int tMediaType = (pMediaType == MEDIA_UNKNOWN) ? mMediaType : pMediaType;

    StopGrabbing();

    // lock grabbing
    mGrabMutex.lock();

    CloseGrabDevice();

    // restart media source, assuming that the last start of the media source was successful
    // otherwise a manual call to Open(Video/Audio)GrabDevice besides this reset function is need
    switch(tMediaType)
    {
        case MEDIA_VIDEO:
            tResult = OpenVideoGrabDevice(mSourceResX, mSourceResY, mFrameRate);
            break;
        case MEDIA_AUDIO:
            tResult = OpenAudioGrabDevice(mSampleRate, mStereo);
            break;
        case MEDIA_UNKNOWN:
            //LOG(LOG_ERROR, "Media type unknown");
            break;
    }

    // unlock grabbing
    mGrabMutex.unlock();

    mChunkNumber = 0;

    return tResult;
}

string MediaSource::GetCodecName()
{
	string tResult = "unknown";

    if (mMediaSourceOpened)
        if (mCodecContext != NULL)
            if (mCodecContext->codec != NULL)
                if (mCodecContext->codec->name != NULL)
                {
                	string tName = FfmpegName2FfmpegFormat(string(mCodecContext->codec->name));
                	if (tName != "")
                		tResult = tName;
                	else
                		tResult = string(mCodecContext->codec->name);
                }

    return tResult;
}

string MediaSource::GetCodecLongName()
{
	string tResult = "unknown";

    if (mMediaSourceOpened)
        if (mCodecContext != NULL)
            if (mCodecContext->codec != NULL)
                if (mCodecContext->codec->long_name != NULL)
                    tResult = string(mCodecContext->codec->long_name);

    return tResult;
}

bool MediaSource::SetInputStreamPreferences(std::string pStreamCodec, bool pDoReset, bool pRtpActivated)
{
    LOG(LOG_VERBOSE, "SetInputStreamPreferences is only a dummy function in MediaSource");
    return false;
}

void MediaSource::ResetPacketStatistic()
{
    PacketStatistic::ResetPacketStatistic();
    mChunkDropCounter = 0;
}

int MediaSource::GetChunkDropCounter()
{
    return mChunkDropCounter;
}

int MediaSource::GetFragmentBufferCounter()
{
    return 0;
}

int MediaSource::GetFragmentBufferSize()
{
    return 0;
}

MediaSinkNet* MediaSource::RegisterMediaSink(string pTarget, Requirements *pTransportRequirements, bool pRtpActivation, int pMaxFps)
{
    MediaSinksList::iterator tIt;
    bool tFound = false;
    MediaSinkNet *tResult = NULL;
    string tId = pTarget + "[" + pTransportRequirements->getDescription() + "]" + (pRtpActivation ? "(RTP)" : "");

    if (pTarget == "")
    {
        LOG(LOG_ERROR, "Sink is ignored because its target is undefined");
        return NULL;
    }

    LOG(LOG_VERBOSE, "Registering GAPI based media sink: %s (Requirements: %s)", pTarget.c_str(), pTransportRequirements->getDescription().c_str());

    // lock
    mMediaSinksMutex.lock();

    for (tIt = mMediaSinks.begin(); tIt != mMediaSinks.end(); tIt++)
    {
        if ((*tIt)->GetId().find(tId) != string::npos)
        {
            LOG(LOG_WARN, "Sink already registered");
            tFound = true;
            break;
        }
    }

    if (!tFound)
    {
        MediaSinkNet *tMediaSinkNet = new MediaSinkNet(pTarget, pTransportRequirements, (mMediaType == MEDIA_VIDEO) ? MEDIA_SINK_VIDEO : MEDIA_SINK_AUDIO, pRtpActivation);
        tMediaSinkNet->SetMaxFps(pMaxFps);
        mMediaSinks.push_back(tMediaSinkNet);
        tResult = tMediaSinkNet;
    }

    // unlock
    mMediaSinksMutex.unlock();

    return tResult;
}

bool MediaSource::UnregisterMediaSink(string pTarget, Requirements *pTransportRequirements, bool pAutoDelete)
{
    bool tResult = false;
    MediaSinksList::iterator tIt;
    string tId = pTarget + "[" + pTransportRequirements->getDescription() + "]";

    if (pTarget == "")
        return false;

    LOG(LOG_VERBOSE, "Unregistering GAPI based media sink: %s (Requirements: %s)", pTarget.c_str(), pTransportRequirements->getDescription().c_str());

    // lock
    mMediaSinksMutex.lock();

    for (tIt = mMediaSinks.begin(); tIt != mMediaSinks.end(); tIt++)
    {
        if ((*tIt)->GetId().find(tId) != string::npos)
        {
            LOG(LOG_VERBOSE, "Found registered sink");

            tResult = true;
            // free memory of media sink object
            if (pAutoDelete)
            {
                delete (*tIt);
                LOG(LOG_VERBOSE, "..deleted");
            }
            // remove registration of media sink object
            mMediaSinks.erase(tIt);
            LOG(LOG_VERBOSE, "..unregistered");
            break;
        }
    }

    // unlock
    mMediaSinksMutex.unlock();

    return tResult;
}

MediaSinkNet* MediaSource::RegisterMediaSink(string pTargetHost, unsigned int pTargetPort, Socket* pSocket, bool pRtpActivation, int pMaxFps)
{
    MediaSinksList::iterator tIt;
    bool tFound = false;
    MediaSinkNet *tResult = NULL;
    string tId = MediaSinkNet::CreateId(pTargetHost, toString(pTargetPort), pSocket->GetTransportType(), pRtpActivation);

    if ((pTargetHost == "") || (pTargetPort == 0))
    {
        LOG(LOG_ERROR, "Sink is ignored because its target is undefined");
        return NULL;
    }

    LOG(LOG_VERBOSE, "Registering Berkeley sockets based media sink: %s<%d>", pTargetHost.c_str(), pTargetPort);

    // lock
    mMediaSinksMutex.lock();

    for (tIt = mMediaSinks.begin(); tIt != mMediaSinks.end(); tIt++)
    {
        if ((*tIt)->GetId().find(tId) != string::npos)
        {
            LOG(LOG_WARN, "Sink already registered");
            tFound = true;
            break;
        }
    }

    if (!tFound)
    {
        MediaSinkNet *tMediaSinkNet = new MediaSinkNet(pTargetHost, pTargetPort, pSocket, (mMediaType == MEDIA_VIDEO) ? MEDIA_SINK_VIDEO : MEDIA_SINK_AUDIO, pRtpActivation);
        tMediaSinkNet->SetMaxFps(pMaxFps);
        mMediaSinks.push_back(tMediaSinkNet);
        tResult = tMediaSinkNet;
    }

    // unlock
    mMediaSinksMutex.unlock();

    return tResult;
}

bool MediaSource::UnregisterMediaSink(string pTargetHost, unsigned int pTargetPort, bool pAutoDelete)
{
    bool tResult = false;
    MediaSinksList::iterator tIt;
    string tId = MediaSinkNet::CreateId(pTargetHost, toString(pTargetPort));

    if ((pTargetHost == "") || (pTargetPort == 0))
        return false;

    LOG(LOG_VERBOSE, "Unregistering net based media sink: %s<%d>", pTargetHost.c_str(), pTargetPort);

    // lock
    mMediaSinksMutex.lock();

    for (tIt = mMediaSinks.begin(); tIt != mMediaSinks.end(); tIt++)
    {
        if ((*tIt)->GetId().find(tId) != string::npos)
        {
            LOG(LOG_VERBOSE, "Found registered sink");

            tResult = true;
            // free memory of media sink object
            if (pAutoDelete)
            {
                delete (*tIt);
                LOG(LOG_VERBOSE, "..deleted");
            }
            // remove registration of media sink object
            mMediaSinks.erase(tIt);
            LOG(LOG_VERBOSE, "..unregistered");
            break;
        }
    }

    // unlock
    mMediaSinksMutex.unlock();

    return tResult;
}

MediaSinkFile* MediaSource::RegisterMediaSink(string pTargetFile)
{
    MediaSinksList::iterator tIt;
    bool tFound = false;
    MediaSinkFile *tResult = NULL;
    string tId = pTargetFile;

    if (pTargetFile == "")
    {
        LOG(LOG_ERROR, "Sink is ignored because its id is undefined");
        return NULL;
    }

    LOG(LOG_VERBOSE, "Registering file based media sink: %s", pTargetFile.c_str());

    // lock
    mMediaSinksMutex.lock();

    for (tIt = mMediaSinks.begin(); tIt != mMediaSinks.end(); tIt++)
    {
        if ((*tIt)->GetId() == tId)
        {
            LOG(LOG_WARN, "Sink already registered");
            tFound = true;
            break;
        }
    }

    if (!tFound)
    {
        MediaSinkFile *tMediaSinkFile = new MediaSinkFile(pTargetFile, (mMediaType == MEDIA_VIDEO)?MEDIA_SINK_VIDEO:MEDIA_SINK_AUDIO);
        mMediaSinks.push_back(tMediaSinkFile);
        tResult = tMediaSinkFile;
    }

    // unlock
    mMediaSinksMutex.unlock();

    return tResult;
}

bool MediaSource::UnregisterMediaSink(string pTargetFile, bool pAutoDelete)
{
    bool tResult = false;
    MediaSinksList::iterator tIt;
    string tId = pTargetFile;

    if (pTargetFile == "")
        return false;

    LOG(LOG_VERBOSE, "Unregistering file based media sink: %s", pTargetFile.c_str());

    // lock
    mMediaSinksMutex.lock();

    for (tIt = mMediaSinks.begin(); tIt != mMediaSinks.end(); tIt++)
    {
        if ((*tIt)->GetId() == tId)
        {
            LOG(LOG_VERBOSE, "Found registered sink");

            tResult = true;
            // free memory of media sink object
            if (pAutoDelete)
            {
                delete (*tIt);
                LOG(LOG_VERBOSE, "..deleted");
            }
            // remove registration of media sink object
            mMediaSinks.erase(tIt);
            LOG(LOG_VERBOSE, "..unregistered");
            break;
        }
    }

    // unlock
    mMediaSinksMutex.unlock();

    return tResult;
}

MediaSink* MediaSource::RegisterMediaSink(MediaSink* pMediaSink)
{
    MediaSinksList::iterator tIt;
    bool tFound = false;
    string tId = pMediaSink->GetId();

    if (tId == "")
    {
        LOG(LOG_ERROR, "Sink is ignored because its id is undefined");
        return NULL;
    }

    LOG(LOG_VERBOSE, "Registering media sink: %s", tId.c_str());

    // lock
    mMediaSinksMutex.lock();

    for (tIt = mMediaSinks.begin(); tIt != mMediaSinks.end(); tIt++)
    {
        if ((*tIt)->GetId() == tId)
        {
            LOG(LOG_WARN, "Sink already registered");
            tFound = true;
            break;
        }
    }

    if (!tFound)
        mMediaSinks.push_back(pMediaSink);

    // unlock
    mMediaSinksMutex.unlock();

    return pMediaSink;
}

bool MediaSource::UnregisterMediaSink(MediaSink* pMediaSink, bool pAutoDelete)
{
    bool tResult = false;
    MediaSinksList::iterator tIt;
    string tId = pMediaSink->GetId();

    if (tId == "")
        return false;

    LOG(LOG_VERBOSE, "Unregistering media sink: %s", tId.c_str());

    // lock
    mMediaSinksMutex.lock();

    for (tIt = mMediaSinks.begin(); tIt != mMediaSinks.end(); tIt++)
    {
        if ((*tIt)->GetId() == tId)
        {
            LOG(LOG_VERBOSE, "Found registered sink");

            tResult = true;
            // free memory of media sink object
            if (pAutoDelete)
            {
                delete (*tIt);
                LOG(LOG_VERBOSE, "..deleted");
            }
            // remove registration of media sink object
            mMediaSinks.erase(tIt);
            LOG(LOG_VERBOSE, "..unregistered");
            break;
        }
    }

    // unlock
    mMediaSinksMutex.unlock();

    return tResult;
}

bool MediaSource::UnregisterGeneralMediaSink(string pId, bool pAutoDelete)
{
    bool tResult = false;
    MediaSinksList::iterator tIt;

    if (pId == "")
        return false;

    LOG(LOG_VERBOSE, "Unregistering media sink: %s", pId.c_str());

    // lock
    mMediaSinksMutex.lock();

    for (tIt = mMediaSinks.begin(); tIt != mMediaSinks.end(); tIt++)
    {
        if ((*tIt)->GetId() == pId)
        {
            LOG(LOG_VERBOSE, "Found registered sink");

            tResult = true;
            // free memory of media sink object
            if (pAutoDelete)
            {
                delete (*tIt);
                LOG(LOG_VERBOSE, "..deleted");
            }
            // remove registration of media sink object
            mMediaSinks.erase(tIt);
            LOG(LOG_VERBOSE, "..unregistered");
            break;
        }
    }

    // unlock
    mMediaSinksMutex.unlock();

    return tResult;
}

list<string> MediaSource::ListRegisteredMediaSinks()
{
    list<string> tResult;
    MediaSinksList::iterator tIt;

    // lock
    if (mMediaSinksMutex.tryLock(250))
    {
        if(mMediaSinks.size() > 0)
        {
            for (tIt = mMediaSinks.begin(); tIt != mMediaSinks.end(); tIt++)
            {
                tResult.push_back((*tIt)->GetId());
            }
        }

        // unlock
        mMediaSinksMutex.unlock();
    }else
        LOG(LOG_WARN, "Failed to lock mutex in specified time");

    return tResult;
}

void MediaSource::DeleteAllRegisteredMediaSinks()
{
    list<string> tResult;
    MediaSinksList::iterator tIt;

    // lock
    mMediaSinksMutex.lock();

    while(mMediaSinks.size())
    {
        for (tIt = mMediaSinks.begin(); tIt != mMediaSinks.end(); tIt++)
        {
            LOG(LOG_VERBOSE, "Deleting registered sink %s", (*tIt)->GetId().c_str());

            // free memory of media sink object
            delete (*tIt);
            LOG(LOG_VERBOSE, "..deleted");

            // remove registration of media sink object
            mMediaSinks.erase(tIt);
            LOG(LOG_VERBOSE, "..unregistered");
            break;
        }
    }

    // unlock
    mMediaSinksMutex.unlock();
}

void MediaSource::SetRtpActivation(bool pState)
{
    mRtpActivated = pState;
}

bool MediaSource::GetRtpActivation()
{
    return mRtpActivated;
}

bool MediaSource::SupportsRelaying()
{
    return false;
}

void MediaSource::RelayPacketToMediaSinks(char* pPacketData, unsigned int pPacketSize, bool pIsKeyFrame)
{
    MediaSinksList::iterator tIt;

    // lock
    mMediaSinksMutex.lock();

    if (mMediaSinks.size() > 0)
    {
        for (tIt = mMediaSinks.begin(); tIt != mMediaSinks.end(); tIt++)
        {
            (*tIt)->ProcessPacket(pPacketData, pPacketSize, mFormatContext->streams[0], pIsKeyFrame);
        }
    }

    // unlock
    mMediaSinksMutex.unlock();
}

bool MediaSource::StartRecording(std::string pSaveFileName, int pSaveFileQuality, bool pRealTime)
{
    int                 tResult;
    AVOutputFormat      *tFormat;
    AVStream            *tStream;
    AVCodec             *tCodec;
    CodecID             tSaveFileCodec = CODEC_ID_NONE;
    int                 tMediaStreamIndex = 0; // we always use stream number 0

    LOG(LOG_VERBOSE, "Going to open recorder, media type is \"%s\"", GetMediaTypeStr().c_str());

    //find simple file name
	size_t tSize;
	tSize = pSaveFileName.rfind('/');

	// Windows path?
	if (tSize == string::npos)
		tSize = pSaveFileName.rfind('\\');

	// nothing found?
	if (tSize != string::npos)
		tSize++;
	else
		tSize = 0;

	string tSimpleFileName = pSaveFileName.substr(tSize, pSaveFileName.length() - tSize);

    char tAuthor[512] = "HomerMultimedia";
    //char tTitle[512] = "recorded live video";
    char tCopyright[512] = "free for use";
    char tComment[512] = "www.homer-conferencing.com";

    // lock grabbing
    mGrabMutex.lock();

    if (!mMediaSourceOpened)
    {
        // unlock grabbing
        mGrabMutex.unlock();

        LOG(LOG_ERROR, "Tried to start recording while media source is closed");
        return false;
    }

    if (mRecording)
    {
        // unlock grabbing
        mGrabMutex.unlock();

        LOG(LOG_ERROR, "Recording already started");
        return false;
    }

    // allocate new format context
    mRecorderFormatContext = AV_NEW_FORMAT_CONTEXT();

    // find format
    tFormat = AV_GUESS_FORMAT(NULL, pSaveFileName.c_str(), NULL);

    if (tFormat == NULL)
    {
        LOG(LOG_ERROR, "Invalid suggested format");
        // Close the format context
        av_free(mRecorderFormatContext);

        // unlock grabbing
        mGrabMutex.unlock();

        return false;
    }

    // set correct output format
    mRecorderFormatContext->oformat = tFormat;

    // set meta data
    HM_av_metadata_set(&mRecorderFormatContext->metadata, "author"   , tAuthor);
    HM_av_metadata_set(&mRecorderFormatContext->metadata, "comment"  , tComment);
    HM_av_metadata_set(&mRecorderFormatContext->metadata, "copyright", tCopyright);
    HM_av_metadata_set(&mRecorderFormatContext->metadata, "title"    , tSimpleFileName.c_str());

    // set filename
    sprintf(mRecorderFormatContext->filename, "%s", pSaveFileName.c_str());

    // allocate new stream structure
    tStream = av_new_stream(mRecorderFormatContext, 0);
    mRecorderCodecContext = tStream->codec;

    // put sample parameters
    mRecorderCodecContext->bit_rate = 500000;

    switch(mMediaType)
    {
        case MEDIA_VIDEO:
                tSaveFileCodec = tFormat->video_codec;
                mRecorderCodecContext->codec_id = tFormat->video_codec;
                mRecorderCodecContext->codec_type = AVMEDIA_TYPE_VIDEO;

                if (tFormat->video_codec == CODEC_ID_H263P)
                    mRecorderCodecContext->flags |= CODEC_FLAG_H263P_SLICE_STRUCT | CODEC_FLAG_4MV | CODEC_FLAG_AC_PRED | CODEC_FLAG_H263P_UMV | CODEC_FLAG_H263P_AIV;
                // resolution

                mRecorderCodecContext->width = mSourceResX;
                mRecorderCodecContext->height = mSourceResY;

                /*
                 * time base: this is the fundamental unit of time (in seconds) in terms
                 * of which frame timestamps are represented. for fixed-FrameRate content,
                 * timebase should be 1/framerate and timestamp increments should be
                 * identically to 1.
                 */
                mRecorderCodecContext->time_base.den = (int)mFrameRate * 100;
                mRecorderCodecContext->time_base.num = 100;
                tStream->time_base.den = (int)mFrameRate * 100;
                tStream->time_base.num = 100;
                // set i frame distance: GOP = group of pictures
                mRecorderCodecContext->gop_size = (100 - pSaveFileQuality) / 5; // default is 12
                mRecorderCodecContext->qmin = 1; // default is 2
                mRecorderCodecContext->qmax = 2 +(100 - pSaveFileQuality) / 4; // default is 31
                // set pixel format
                mRecorderCodecContext->pix_fmt = PIX_FMT_YUV420P;

                // workaround for incompatibility of ffmpeg/libx264
                // inspired by check within libx264 in "x264_validate_parameters()" of encoder.c
                if (tFormat->video_codec == CODEC_ID_H264)
                {
                    mRecorderCodecContext->me_range = 16;
                    mRecorderCodecContext->max_qdiff = 4;
                    mRecorderCodecContext->qcompress = 0.6;
                }

                // set MPEG quantizer: for h261/h263/mjpeg use the h263 quantizer, in other cases use the MPEG2 one
            //    if ((tFormat->video_codec == CODEC_ID_H261) || (tFormat->video_codec == CODEC_ID_H263) || (tFormat->video_codec == CODEC_ID_H263P) || (tFormat->video_codec == CODEC_ID_MJPEG))
            //        mRecorderCodecContext->mpeg_quant = 0;
            //    else
            //        mRecorderCodecContext->mpeg_quant = 1;

                // set pixel format
                if (tFormat->video_codec == CODEC_ID_MJPEG)
                    mRecorderCodecContext->pix_fmt = PIX_FMT_YUVJ420P;
                else
                    mRecorderCodecContext->pix_fmt = PIX_FMT_YUV420P;

                // allocate software scaler context if necessary
        		if (mCodecContext != NULL)
                	mRecorderScalerContext = sws_getContext(mSourceResX, mSourceResY, mCodecContext->pix_fmt, mSourceResX, mSourceResY, mRecorderCodecContext->pix_fmt, SWS_BICUBIC, NULL, NULL, NULL);
        		else
        		{
        			LOG(LOG_WARN, "Codec context is invalid, pixel format cannot be determined automatically, assuming RGB32 as input");
                	mRecorderScalerContext = sws_getContext(mSourceResX, mSourceResY, PIX_FMT_RGB32, mSourceResX, mSourceResY, mRecorderCodecContext->pix_fmt, SWS_BICUBIC, NULL, NULL, NULL);
        		}

                // Dump information about device file
        		av_dump_format(mRecorderFormatContext, 0, "MediaSource recorder (video)", true);

                break;
        case MEDIA_AUDIO:
                tSaveFileCodec = tFormat->audio_codec;
                mRecorderCodecContext->codec_id = tFormat->audio_codec;
                mRecorderCodecContext->codec_type = AVMEDIA_TYPE_AUDIO;

                mRecorderCodecContext->channels = mStereo?2:1; // stereo?
                mRecorderCodecContext->bit_rate = MediaSource::AudioQuality2BitRate(pSaveFileQuality); // streaming rate
                mRecorderCodecContext->sample_rate = mSampleRate; // sampling rate: 22050, 44100

                mRecorderCodecContext->qmin = 2; // 2
                mRecorderCodecContext->qmax = 9;/*2 +(100 - mAudioStreamQuality) / 4; // 31*/
                mRecorderCodecContext->sample_fmt = SAMPLE_FMT_S16;

                // init fifo buffer
                mRecorderSampleFifo = HM_av_fifo_alloc(MEDIA_SOURCE_SAMPLES_MULTI_BUFFER_SIZE * 2);

                // Dump information about device file
                av_dump_format(mRecorderFormatContext, 0, "MediaSource recorder (audio)", true);

                break;
        default:
        case MEDIA_UNKNOWN:
                LOG(LOG_ERROR, "Media type unknown");
                break;
    }

    // activate ffmpeg internal fps emulation
    //mRecorderCodecContext->rate_emu = 1;

    // some formats want stream headers to be separate
    if(mRecorderFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
        mRecorderCodecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;

    // Find the encoder for the video stream
    if ((tCodec = avcodec_find_encoder(tSaveFileCodec)) == NULL)
    {
        LOG(LOG_ERROR, "Couldn't find a fitting %s codec", GetMediaTypeStr().c_str());
        // free codec and stream 0
        av_freep(&mRecorderFormatContext->streams[0]->codec);
        av_freep(&mRecorderFormatContext->streams[0]);

        // Close the format context
        av_free(mRecorderFormatContext);

        // unlock grabbing
        mGrabMutex.unlock();

        return false;
    }

    // Open codec
    if ((tResult = avcodec_open(mRecorderCodecContext, tCodec)) < 0)
    {
        LOG(LOG_ERROR, "Couldn't open %s codec because of \"%s\".", GetMediaTypeStr().c_str(), strerror(AVUNERROR(tResult)));
        // free codec and stream 0
        av_freep(&mRecorderFormatContext->streams[0]->codec);
        av_freep(&mRecorderFormatContext->streams[0]);

        // Close the format context
        av_free(mRecorderFormatContext);

        // unlock grabbing
        mGrabMutex.unlock();

        return false;
    }

    // open the output file, if needed
    if (!(tFormat->flags & AVFMT_NOFILE))
    {
        if (url_fopen(&mRecorderFormatContext->pb, pSaveFileName.c_str(), URL_WRONLY) < 0)
        {
            LOG(LOG_ERROR, "Could not open \"%s\"\n", pSaveFileName.c_str());
            // free codec and stream 0
            av_freep(&mRecorderFormatContext->streams[0]->codec);
            av_freep(&mRecorderFormatContext->streams[0]);

            // Close the format context
            av_free(mRecorderFormatContext);

            // unlock grabbing
            mGrabMutex.unlock();

            return false;
        }
    }

    // allocate streams private data buffer and write the streams header, if any
    avformat_write_header(mRecorderFormatContext, NULL);

    LOG(LOG_INFO, "%s recorder opened...", GetMediaTypeStr().c_str());

    LOG(LOG_INFO, "    ..selected recording quality: %d%", pSaveFileQuality);
    LOG(LOG_INFO, "    ..pts adaption for real-time recording: %d", pRealTime);

    LOG(LOG_INFO, "    ..codec name: %s", mRecorderCodecContext->codec->name);
    LOG(LOG_INFO, "    ..codec long name: %s", mRecorderCodecContext->codec->long_name);
    LOG(LOG_INFO, "    ..codec flags: 0x%x", mRecorderCodecContext->flags);
    LOG(LOG_INFO, "    ..codec time_base: %d/%d", mRecorderCodecContext->time_base.den, mRecorderCodecContext->time_base.num); // inverse
    LOG(LOG_INFO, "    ..stream rfps: %d/%d", mRecorderFormatContext->streams[tMediaStreamIndex]->r_frame_rate.num, mRecorderFormatContext->streams[tMediaStreamIndex]->r_frame_rate.den);
    LOG(LOG_INFO, "    ..stream time_base: %d/%d", mRecorderFormatContext->streams[tMediaStreamIndex]->time_base.den, mRecorderFormatContext->streams[tMediaStreamIndex]->time_base.num); // inverse
    LOG(LOG_INFO, "    ..stream codec time_base: %d/%d", mRecorderFormatContext->streams[tMediaStreamIndex]->codec->time_base.den, mRecorderFormatContext->streams[tMediaStreamIndex]->codec->time_base.num); // inverse
    LOG(LOG_INFO, "    ..bit rate: %d", mRecorderCodecContext->bit_rate);
    LOG(LOG_INFO, "    ..desired device: %s", mDesiredDevice.c_str());
    LOG(LOG_INFO, "    ..current device: %s", mCurrentDevice.c_str());
    LOG(LOG_INFO, "    ..qmin: %d", mRecorderCodecContext->qmin);
    LOG(LOG_INFO, "    ..qmax: %d", mRecorderCodecContext->qmax);
    LOG(LOG_INFO, "    ..frame size: %d", mRecorderCodecContext->frame_size);
    LOG(LOG_INFO, "    ..duration: %ld frames", mDuration);
    LOG(LOG_INFO, "    ..stream context duration: %ld frames, %.0f seconds, format context duration: %ld, nr. of frames: %ld", mRecorderFormatContext->streams[tMediaStreamIndex]->duration, (float)mRecorderFormatContext->streams[tMediaStreamIndex]->duration / mFrameRate, mRecorderFormatContext->duration, mRecorderFormatContext->streams[tMediaStreamIndex]->nb_frames);
    switch(mMediaType)
    {
        case MEDIA_VIDEO:
            LOG(LOG_INFO, "    ..source resolution: %d * %d", mRecorderCodecContext->width, mRecorderCodecContext->height);
            LOG(LOG_INFO, "    ..target resolution: %d * %d", mTargetResX, mTargetResY);
            LOG(LOG_INFO, "    ..i-frame distance: %d", mRecorderCodecContext->gop_size);
            LOG(LOG_INFO, "    ..mpeg quant: %d", mRecorderCodecContext->mpeg_quant);
            LOG(LOG_INFO, "    ..pixel format: %d", (int)mRecorderCodecContext->pix_fmt);
            break;
        case MEDIA_AUDIO:
            LOG(LOG_INFO, "    ..sample rate: %d", mRecorderCodecContext->sample_rate);
            LOG(LOG_INFO, "    ..channels: %d", mRecorderCodecContext->channels);
            LOG(LOG_INFO, "    ..sample format: %d", (int)mRecorderCodecContext->sample_fmt);
            LOG(LOG_INFO, "Fifo opened...");
            LOG(LOG_INFO, "    ..fill size: %d bytes", av_fifo_size(mRecorderSampleFifo));
            break;
        default:
            LOG(LOG_ERROR, "Media type unknown");
            break;
    }

    // unlock grabbing
    mGrabMutex.unlock();

    mRecorderStartPts = -1;
    mRecorderChunkNumber = 0;
    mRecordingSaveFileName = pSaveFileName;
    mRecording = true;
    mRecorderRealTime = pRealTime;

    // allocate all needed buffers
    LOG(LOG_VERBOSE, "Allocating needed recorder buffers");
    mRecorderEncoderChunkBuffer = (char*)malloc(MEDIA_SOURCE_AV_CHUNK_BUFFER_SIZE);
    if (mMediaType == MEDIA_AUDIO)
        mRecorderSamplesTempBuffer = (char*)malloc(MEDIA_SOURCE_SAMPLES_MULTI_BUFFER_SIZE);

    return true;
}

void MediaSource::StopRecording()
{
    if (mRecording)
    {
        LOG(LOG_VERBOSE, "Going to close recorder, media type is \"%s\"", GetMediaTypeStr().c_str());

        mRecording = false;

        // lock grabbing
        mGrabMutex.lock();

        // write the trailer, if any
        av_write_trailer(mRecorderFormatContext);

        switch(mMediaType)
        {
            case MEDIA_VIDEO:
                    // free the software scaler context
                    sws_freeContext(mRecorderScalerContext);

                    break;
            case MEDIA_AUDIO:
                    // free fifo buffer
                    av_fifo_free(mRecorderSampleFifo);

                    break;
            default:
            case MEDIA_UNKNOWN:
                    LOG(LOG_ERROR, "Media type unknown");
                    break;
        }

        // Close the codec
        avcodec_close(mRecorderCodecContext);

        // free codec and stream 0
        av_freep(&mRecorderFormatContext->streams[0]->codec);
        av_freep(&mRecorderFormatContext->streams[0]);

        if (!(mRecorderFormatContext->oformat->flags & AVFMT_NOFILE))
        {
            // close the output file
            url_fclose(mRecorderFormatContext->pb);
        }

        // Close the format context
        av_free(mRecorderFormatContext);

        LOG(LOG_VERBOSE, "Releasing recorder buffers");
        if (mMediaType == MEDIA_AUDIO)
            free(mRecorderSamplesTempBuffer);
        free(mRecorderEncoderChunkBuffer);

        // unlock grabbing
        mGrabMutex.unlock();

        LOG(LOG_INFO, "...closed, media type is \"%s\"", GetMediaTypeStr().c_str());

    }

    mRecorderStartPts = -1;
}

bool MediaSource::SupportsRecording()
{
	return false;
}

bool MediaSource::IsRecording()
{
	return mRecording;
}

void MediaSource::RecordFrame(AVFrame *pSourceFrame)
{
    AVFrame             *tFrame;
    AVPacket            tPacket;
    int                 tFrameSize;
    int64_t             tCurrentPts = 1;

    if (mMediaType == MEDIA_AUDIO)
    {
        LOG(LOG_ERROR, "Wrong media type (audio)");
        return;
    }

    if (!mRecording)
    {
        LOG(LOG_ERROR, "Recording not started");
        return;
    }

    // #########################################
    // frame rate emulation
    // #########################################
    // inspired by "output_packet" from ffmpeg.c
    if (mRecorderRealTime)
    {
        // should we initiate the StartPts value?
        if (mRecorderStartPts == -1)
        {
            tCurrentPts = 1;
            mRecorderStartPts = pSourceFrame->pts;
        }else
            tCurrentPts = pSourceFrame->pts - mRecorderStartPts;
    }else
        tCurrentPts = mRecorderChunkNumber;

    // #########################################
    // has resolution changed since last call?
    // #########################################
    if ((mSourceResX != mRecorderCodecContext->width) || (mSourceResY != mRecorderCodecContext->height))
    {
        // free the software scaler context
        sws_freeContext(mRecorderScalerContext);

        // set grabbing resolution to the resulting ones delivered by received frame
        mRecorderCodecContext->width = mSourceResY;
        mRecorderCodecContext->height = mSourceResY;

        // allocate software scaler context
		if (mCodecContext != NULL)
        	mRecorderScalerContext = sws_getContext(mSourceResX, mSourceResY, mCodecContext->pix_fmt, mSourceResX, mSourceResY, mRecorderCodecContext->pix_fmt, SWS_BICUBIC, NULL, NULL, NULL);
		else
		{
			LOG(LOG_WARN, "Codec context is invalid, pixel format cannot be determined automatically, assuming RGB32 as input");
        	mRecorderScalerContext = sws_getContext(mSourceResX, mSourceResY, PIX_FMT_RGB32, mSourceResX, mSourceResY, mRecorderCodecContext->pix_fmt, SWS_BICUBIC, NULL, NULL, NULL);
		}

        LOG(LOG_INFO, "Resolution changed to (%d * %d)", mSourceResX, mSourceResY);
    }

    // #########################################
    // scale resolution and transform pixel format
    // #########################################
    pSourceFrame->coded_picture_number = mRecorderChunkNumber;

    #ifdef MS_DEBUG_PACKETS
        LOG(LOG_VERBOSE, "Recorder source frame..");
        LOG(LOG_VERBOSE, "      ..key frame: %d", pSourceFrame->key_frame);
        switch(pSourceFrame->pict_type)
        {
                case FF_I_TYPE:
                    LOG(LOG_VERBOSE, "      ..picture type: i-frame");
                    break;
                case FF_P_TYPE:
                    LOG(LOG_VERBOSE, "      ..picture type: p-frame");
                    break;
                case FF_B_TYPE:
                    LOG(LOG_VERBOSE, "      ..picture type: b-frame");
                    break;
                default:
                    LOG(LOG_VERBOSE, "      ..picture type: %d", pSourceFrame->pict_type);
                    break;
        }
        LOG(LOG_VERBOSE, "      ..pts: %ld", pSourceFrame->pts);
        LOG(LOG_VERBOSE, "      ..coded pic number: %d", pSourceFrame->coded_picture_number);
        LOG(LOG_VERBOSE, "      ..display pic number: %d", pSourceFrame->display_picture_number);
    #endif

    // transform pixel format to target format
    if (((tFrame = avcodec_alloc_frame()) == NULL) || (avpicture_alloc((AVPicture*)tFrame, mRecorderCodecContext->pix_fmt, mSourceResX, mSourceResY) != 0))
    {
        LOG(LOG_ERROR, "Couldn't allocate video frame memory");
    }else
    {
        tFrame->coded_picture_number = tCurrentPts;
        tFrame->pts = tCurrentPts;
        tFrame->pict_type = pSourceFrame->pict_type;
        tFrame->key_frame = pSourceFrame->key_frame;

        #ifdef MS_DEBUG_PACKETS
            LOG(LOG_VERBOSE, "Recording video frame..");
            LOG(LOG_VERBOSE, "      ..key frame: %d", tFrame->key_frame);
            switch(tFrame->pict_type)
            {
                    case FF_I_TYPE:
                        LOG(LOG_VERBOSE, "      ..picture type: i-frame");
                        break;
                    case FF_P_TYPE:
                        LOG(LOG_VERBOSE, "      ..picture type: p-frame");
                        break;
                    case FF_B_TYPE:
                        LOG(LOG_VERBOSE, "      ..picture type: b-frame");
                        break;
                    default:
                        LOG(LOG_VERBOSE, "      ..picture type: %d", tFrame->pict_type);
                        break;
            }
            LOG(LOG_VERBOSE, "      ..pts: %ld", tFrame->pts);
            LOG(LOG_VERBOSE, "      ..coded pic number: %d", tFrame->coded_picture_number);
            LOG(LOG_VERBOSE, "      ..display pic number: %d", tFrame->display_picture_number);
        #endif

        // convert pixel format in pSourceFrame and store it in tFrame
        //HINT: we should execute this step in every case (incl. when pixel format is equal), otherwise data structures are wrong
		HM_sws_scale(mRecorderScalerContext, pSourceFrame->data, pSourceFrame->linesize, 0, mSourceResY, tFrame->data, tFrame->linesize);

        // #########################################
        // re-encode the frame
        // #########################################
        tFrameSize = avcodec_encode_video(mRecorderCodecContext, (uint8_t *)mRecorderEncoderChunkBuffer, MEDIA_SOURCE_AV_CHUNK_BUFFER_SIZE, tFrame);

        if (tFrameSize > 0)
        {
            av_init_packet(&tPacket);

            // mark i-frame
            if (mRecorderCodecContext->coded_frame->key_frame)
                tPacket.flags |= AV_PKT_FLAG_KEY;

            // we only have one stream per video stream
            tPacket.stream_index = 0;
            tPacket.data = (uint8_t *)mRecorderEncoderChunkBuffer;
            tPacket.size = tFrameSize;
            tPacket.pts = tCurrentPts;
            tPacket.dts = tCurrentPts;
            tPacket.duration = 1; // always 1 because we increase pts for every packet by one
            tPacket.pos = -1;

            #ifdef MS_DEBUG_PACKETS
                LOG(LOG_VERBOSE, "Recording packet..");
                LOG(LOG_VERBOSE, "      ..duration: %d", tPacket.duration);
                LOG(LOG_VERBOSE, "      ..pts: %ld (fps: %3.2f)", tPacket.pts, mFrameRate);
                LOG(LOG_VERBOSE, "      ..dts: %ld", tPacket.dts);
                LOG(LOG_VERBOSE, "      ..size: %d", tPacket.size);
                LOG(LOG_VERBOSE, "      ..pos: %ld", tPacket.pos);
            #endif

             // distribute the encoded frame
             if (av_write_frame(mRecorderFormatContext, &tPacket) != 0)
                 LOG(LOG_ERROR, "Couldn't write video frame to file");

             // Free the file frame's data buffer
             avpicture_free((AVPicture*)tFrame);

             // Free the file frame
             av_free(tFrame);
        }else
            LOG(LOG_ERROR, "Couldn't re-encode current video frame");
    }

    mRecorderChunkNumber++;
}

void MediaSource::RecordSamples(int16_t *pSourceSamples, int pSourceSamplesSize)
{
    AVPacket            tPacket;
    int                 tFrameSize;
    int64_t             tCurrentPts = 1;

    if (mMediaType == MEDIA_VIDEO)
    {
        LOG(LOG_ERROR, "Wrong media type (video)");
        return;
    }

    if (!mRecording)
    {
        LOG(LOG_ERROR, "Recording not started");
        return;
    }

    // #########################################
    // frame rate emulation
    // #########################################
    // inspired by "output_packet" from ffmpeg.c
//TODO: adapt the following code for real-time recording to the file? (similar to the video recording part)
//    if (mRecorderRealTime)
//    {
//        // should we initiate the StartPts value?
//        if (mRecorderStartPts == -1)
//        {
//            tCurrentPts = 1;
//            mRecorderStartPts = pSourceFrame->pts;
//        }else
//            tCurrentPts = pSourceFrame->pts - mRecorderStartPts;
//    }else
//        tCurrentPts = mRecorderChunkNumber;

    // increase fifo buffer size by size of input buffer size
    #ifdef MS_DEBUG_PACKETS
        LOG(LOG_VERBOSE, "Adding %d bytes to fifo buffer with size of %d bytes", pSourceSamplesSize, av_fifo_size(mRecorderSampleFifo));
    #endif
    if (av_fifo_realloc2(mRecorderSampleFifo, av_fifo_size(mRecorderSampleFifo) + pSourceSamplesSize) < 0)
    {
        // acknowledge failed
        LOG(LOG_ERROR, "Reallocation of FIFO audio buffer failed");

        return;
    }

    //LOG(LOG_VERBOSE, "ChunkSize: %d", pChunkSize);
    // write new samples into fifo buffer
    av_fifo_generic_write(mRecorderSampleFifo, pSourceSamples, pSourceSamplesSize, NULL);
    //LOG(LOG_VERBOSE, "ChunkSize: %d", pChunkSize);

    while (av_fifo_size(mRecorderSampleFifo) > 2 * mRecorderCodecContext->frame_size * mRecorderCodecContext->channels)
    {
        #ifdef MS_DEBUG_PACKETS
            LOG(LOG_VERBOSE, "Reading %d bytes from %d bytes of fifo", 2 * mRecorderCodecContext->frame_size * mRecorderCodecContext->channels, av_fifo_size(mRecorderSampleFifo));
        #endif
        // read sample data from the fifo buffer
        HM_av_fifo_generic_read(mRecorderSampleFifo, (void*)mRecorderSamplesTempBuffer, /* assume signed 16 bit */ 2 * mRecorderCodecContext->frame_size * mRecorderCodecContext->channels);

        //####################################################################
        // re-encode the sample
        // ###################################################################
        // re-encode the sample
        #ifdef MS_DEBUG_PACKETS
            LOG(LOG_VERBOSE, "Reencoding audio frame..");
            LOG(LOG_VERBOSE, "Gonna encode with frame_size %d and channels %d", mRecorderCodecContext->frame_size, mRecorderCodecContext->channels);
        #endif
        int tEncodingResult = avcodec_encode_audio(mRecorderCodecContext, (uint8_t *)mRecorderEncoderChunkBuffer, /* assume signed 16 bit */ 2 * mRecorderCodecContext->frame_size * mRecorderCodecContext->channels, (const short *)mRecorderSamplesTempBuffer);

        //printf("encoded to mp3: %d\n\n", tSampleSize);
        if (tEncodingResult > 0)
        {
            av_init_packet(&tPacket);
            mChunkNumber++;

            // adapt pts value
            if ((mRecorderCodecContext->coded_frame) && (mRecorderCodecContext->coded_frame->pts != 0))
                tPacket.pts = av_rescale_q(mRecorderCodecContext->coded_frame->pts, mRecorderCodecContext->time_base, mRecorderFormatContext->streams[0]->time_base);
            tPacket.flags |= AV_PKT_FLAG_KEY;

            // we only have one stream per audio stream
            tPacket.stream_index = 0;
            tPacket.data = (uint8_t *)mRecorderEncoderChunkBuffer;
            tPacket.size = tEncodingResult;
            tPacket.pts = mRecorderChunkNumber;
            tPacket.dts = mRecorderChunkNumber;
//            tPacket.pos = av_gettime() - mStartPts;

            #ifdef MSM_DEBUG_PACKETS
                LOG(LOG_VERBOSE, "Writing audio packet: %5d to file", mRecorderChunkNumber);
                LOG(LOG_VERBOSE, "      ..pts: %ld", tPacket.pts);
                LOG(LOG_VERBOSE, "      ..dts: %ld", tPacket.dts);
                LOG(LOG_VERBOSE, "      ..size: %d", tPacket.size);
                LOG(LOG_VERBOSE, "      ..pos: %ld", tPacket.pos);
            #endif

            //####################################################################
            // distribute the encoded frame
            // ###################################################################
             if (av_write_frame(mRecorderFormatContext, &tPacket) != 0)
             {
                 LOG(LOG_ERROR, "Couldn't write audio sample to file");
             }
        }else
            LOG(LOG_INFO, "Couldn't re-encode current audio sample");
    }

    mRecorderChunkNumber++;
}

string MediaSource::GetMediaTypeStr()
{
    switch (mMediaType)
    {
        case MEDIA_VIDEO:
            return "VIDEO";
        case MEDIA_AUDIO:
            return "AUDIO";
        default:
        	return "VIDEO/AUDIO";
    }
    return "unknown";
}

enum MediaType MediaSource::GetMediaType()
{
	return mMediaType;
}

void MediaSource::getVideoDevices(VideoDevicesList &pVList)
{
    VideoDeviceDescriptor tDevice;

    tDevice.Name = "NULL: unsupported";
    tDevice.Card = "";
    tDevice.Desc = "Unsupported media type";

    pVList.push_back(tDevice);
}

void MediaSource::getAudioDevices(AudioDevicesList &pAList)
{
    AudioDeviceDescriptor tDevice;

    tDevice.Name = "NULL: unsupported";
    tDevice.Card = "";
    tDevice.Desc = "Unsupported media type";
    tDevice.IoType = "Input/Output";

    pAList.push_back(tDevice);
}

bool MediaSource::SelectDevice(std::string pDeviceName, enum MediaType pMediaType, bool &pIsNewDevice)
{
    VideoDevicesList::iterator tVIt;
    VideoDevicesList tVList;
    AudioDevicesList::iterator tAIt;
    AudioDevicesList tAList;
    string tOldDesiredDevice = mDesiredDevice;
    bool tAutoSelect = false;
    int tMediaType = mMediaType;
    bool tResult = false;

    pIsNewDevice = false;

    if ((pMediaType == MEDIA_VIDEO) || (pMediaType == MEDIA_AUDIO))
        tMediaType = pMediaType;

    LOG(LOG_INFO, "%s-Selecting new device..", GetStreamName().c_str());

    if ((pDeviceName == "auto") || (pDeviceName == ""))
    {
        tAutoSelect = true;
        LOG(LOG_VERBOSE, "%s-Automatic device selection..", GetStreamName().c_str());
    }

    switch(tMediaType)
    {
        case MEDIA_VIDEO:
                getVideoDevices(tVList);
                for (tVIt = tVList.begin(); tVIt != tVList.end(); tVIt++)
                {
                    if ((pDeviceName == tVIt->Name) || (tAutoSelect))
                    {
                        mDesiredDevice = tVIt->Card;
                        mCurrentDeviceName = tVIt->Name;
                        if (mDesiredDevice != mCurrentDevice)
                        {
                            pIsNewDevice = true;
                            LOG(LOG_INFO, "%s-Video device selected: %s", GetStreamName().c_str(), mDesiredDevice.c_str());
                        }else
                            LOG(LOG_INFO, "%s-Video device re-selected: %s", GetStreamName().c_str(), mDesiredDevice.c_str());
                        tResult = true;
                        break;
                    }
                }
                break;
        case MEDIA_AUDIO:
                getAudioDevices(tAList);
                for (tAIt = tAList.begin(); tAIt != tAList.end(); tAIt++)
                {
                    if ((pDeviceName == tAIt->Name) || (tAutoSelect))
                    {
                        mDesiredDevice = tAIt->Card;
                        mCurrentDeviceName = tAIt->Name;
                        if (mDesiredDevice != mCurrentDevice)
                        {
                            pIsNewDevice = true;
                            LOG(LOG_INFO, "%s-Audio device selected: %s", GetStreamName().c_str(), mDesiredDevice.c_str());
                        }else
                            LOG(LOG_INFO, "%s-Audio device re-selected: %s", GetStreamName().c_str(), mDesiredDevice.c_str());
                        tResult = true;
                        break;
                    }
                }
                break;
        default:
                LOG(LOG_ERROR, "%s-Media type undefined", GetStreamName().c_str());
                break;
    }

    if ((!pIsNewDevice) && (((pDeviceName == "auto") || (pDeviceName == "automatic") || pDeviceName == "")))
    {
        LOG(LOG_INFO, "%s-Auto detected device was selected: auto detect (card: auto)", GetStreamName().c_str());
        mDesiredDevice = "";
        mCurrentDeviceName = "auto selection";
    }

    LOG(LOG_VERBOSE, "%s-Source should be reseted: %d", GetStreamName().c_str(), pIsNewDevice);
    if (!tResult)
        LOG(LOG_INFO, "%s-Selected device %s is not available", GetStreamName().c_str(), pDeviceName.c_str());

    return tResult;
}

std::string MediaSource::GetCurrentDevicePeerName()
{
	return "";
}

std::string MediaSource::GetCurrentDeviceName()
{
	return mCurrentDeviceName;
}

bool MediaSource::RegisterMediaSource(MediaSource* pMediaSource)
{
    LOG(LOG_VERBOSE, "Registering media source: 0x%x", pMediaSource);
    LOG(LOG_VERBOSE, "This is only the dummy function");
    return true;
}

bool MediaSource::UnregisterMediaSource(MediaSource* pMediaSource, bool pAutoDelete)
{
    LOG(LOG_VERBOSE, "Unregistering media source: 0x%x", pMediaSource);
    LOG(LOG_VERBOSE, "This is only the dummy function");
    return true;
}

float MediaSource::GetFrameRate()
{
    return mFrameRate;
}

void MediaSource::SetFrameRate(float pFps)
{
    if (pFps > 29.97)
        pFps = 29.97;
    if (pFps < 5)
        pFps = 5;
    mFrameRate = pFps;
}

void* MediaSource::AllocChunkBuffer(int& pChunkBufferSize, enum MediaType pMediaType)
{
    enum MediaType tMediaType = mMediaType;

    if ((pMediaType == MEDIA_UNKNOWN) && (!mMediaSourceOpened))
    {
        LOG(LOG_ERROR, "Tried to allocate a chunk buffer while media type is undefined");
        return NULL;
    }

    if ((pMediaType == MEDIA_VIDEO) || (pMediaType == MEDIA_AUDIO))
        tMediaType = pMediaType;

    switch(tMediaType)
    {
        case MEDIA_VIDEO:
            pChunkBufferSize = avpicture_get_size(PIX_FMT_RGB32, mTargetResX, mTargetResY);
            return av_malloc(pChunkBufferSize);
        case MEDIA_AUDIO:
            pChunkBufferSize = MEDIA_SOURCE_SAMPLES_MULTI_BUFFER_SIZE * 2 + FF_INPUT_BUFFER_PADDING_SIZE;
            return av_malloc(pChunkBufferSize);
        default:
        	LOG(LOG_WARN, "Undefined media type, returning chunk buffer will be invalid");
            return NULL;
    }
}

void MediaSource::FreeChunkBuffer(void *pChunk)
{
    av_free(pChunk);
}

bool MediaSource::SupportsSeeking()
{
    return false;
}

int64_t MediaSource::GetSeekEnd()
{
    return 0;
}

bool MediaSource::Seek(int64_t pSeconds, bool pOnlyKeyFrames)
{
    return false;
}

bool MediaSource::SeekRelative(int64_t pSeconds, bool pOnlyKeyFrames)
{
    return false;
}

int64_t MediaSource::GetSeekPos()
{
    return 0;
}

bool MediaSource::SupportsMultipleInputChannels()
{
    return false;
}

bool MediaSource::SelectInputChannel(int pIndex)
{
    return false;
}

list<string> MediaSource::GetInputChannels()
{
    list<string> tResult;

    return tResult;
}

string MediaSource::CurrentInputChannel()
{
    return mCurrentDevice;
}

void MediaSource::FpsEmulationInit()
{
    mStartPtsUSecs = av_gettime();
}

int64_t MediaSource::FpsEmulationGetPts()
{
    int64_t tRelativeRealTimeUSecs = av_gettime() - mStartPtsUSecs; // relative playback time in usecs
    float tRelativeFrameNumber = mFrameRate * tRelativeRealTimeUSecs / 1000000;
    return (int64_t)tRelativeFrameNumber;
}

void MediaSource::EventOpenGrabDeviceSuccessful(string pSource, int pLine)
{
    //######################################################
    //### give some verbose output
    //######################################################
    LOG_REMOTE(LOG_INFO, pSource, pLine, "%s source opened...", GetMediaTypeStr().c_str());
    LOG_REMOTE(LOG_INFO, pSource, pLine, "    ..codec name: %s", mCodecContext->codec->name);
    LOG_REMOTE(LOG_INFO, pSource, pLine, "    ..codec long name: %s", mCodecContext->codec->long_name);
    LOG_REMOTE(LOG_INFO, pSource, pLine, "    ..codec flags: 0x%x", mCodecContext->flags);
    LOG_REMOTE(LOG_INFO, pSource, pLine, "    ..codec time_base: %d/%d", mCodecContext->time_base.den, mCodecContext->time_base.num); // inverse
    LOG_REMOTE(LOG_INFO, pSource, pLine, "    ..stream rfps: %d/%d", mFormatContext->streams[mMediaStreamIndex]->r_frame_rate.num, mFormatContext->streams[mMediaStreamIndex]->r_frame_rate.den);
    LOG_REMOTE(LOG_INFO, pSource, pLine, "    ..stream time_base: %d/%d", mFormatContext->streams[mMediaStreamIndex]->time_base.den, mFormatContext->streams[mMediaStreamIndex]->time_base.num); // inverse
    LOG_REMOTE(LOG_INFO, pSource, pLine, "    ..stream codec time_base: %d/%d", mFormatContext->streams[mMediaStreamIndex]->codec->time_base.den, mFormatContext->streams[mMediaStreamIndex]->codec->time_base.num); // inverse
    LOG_REMOTE(LOG_INFO, pSource, pLine, "    ..bit rate: %d", mCodecContext->bit_rate);
    LOG_REMOTE(LOG_INFO, pSource, pLine, "    ..desired device: %s", mDesiredDevice.c_str());
    LOG_REMOTE(LOG_INFO, pSource, pLine, "    ..current device: %s", mCurrentDevice.c_str());
    LOG_REMOTE(LOG_INFO, pSource, pLine, "    ..qmin: %d", mCodecContext->qmin);
    LOG_REMOTE(LOG_INFO, pSource, pLine, "    ..qmax: %d", mCodecContext->qmax);
    LOG_REMOTE(LOG_INFO, pSource, pLine, "    ..frame size: %d", mCodecContext->frame_size);
    LOG_REMOTE(LOG_INFO, pSource, pLine, "    ..duration: %ld frames", mDuration);
    LOG_REMOTE(LOG_INFO, pSource, pLine, "    ..stream context duration: %ld frames, %.0f seconds, format context duration: %ld seconds, nr. of frames: %ld", mFormatContext->streams[mMediaStreamIndex]->duration, (float)mFormatContext->streams[mMediaStreamIndex]->duration / mFrameRate, mFormatContext->duration / AV_TIME_BASE, mFormatContext->streams[mMediaStreamIndex]->nb_frames);
    switch(mMediaType)
    {
        case MEDIA_VIDEO:
            LOG_REMOTE(LOG_INFO, pSource, pLine, "    ..source resolution: %d * %d", mCodecContext->width, mCodecContext->height);
            LOG_REMOTE(LOG_INFO, pSource, pLine, "    ..target resolution: %d * %d", mTargetResX, mTargetResY);
            LOG_REMOTE(LOG_INFO, pSource, pLine, "    ..i-frame distance: %d", mCodecContext->gop_size);
            LOG_REMOTE(LOG_INFO, pSource, pLine, "    ..mpeg quant: %d", mCodecContext->mpeg_quant);
            LOG_REMOTE(LOG_INFO, pSource, pLine, "    ..pixel format: %d", (int)mCodecContext->pix_fmt);
            break;
        case MEDIA_AUDIO:
            LOG_REMOTE(LOG_INFO, pSource, pLine, "    ..sample rate: %d", mCodecContext->sample_rate);
            LOG_REMOTE(LOG_INFO, pSource, pLine, "    ..channels: %d", mCodecContext->channels);
            LOG_REMOTE(LOG_INFO, pSource, pLine, "    ..sample format: %s", av_get_sample_fmt_name(mCodecContext->sample_fmt));
            break;
        default:
            LOG(LOG_ERROR, "Media type unknown");
            break;
    }

    //######################################################
    //### initiate local variables
    //######################################################
    FpsEmulationInit();
    mSourceStartPts = -1;
    mChunkNumber = 0;
    mChunkDropCounter = 0;
    mLastGrabFailureReason = "";
    mLastGrabResultWasError = false;
    mMediaSourceOpened = true;
}

void MediaSource::EventGrabChunkSuccessful(string pSource, int pLine, int pChunkNumber)
{
    if (mLastGrabResultWasError)
    {
        switch(mMediaType)
        {
            case MEDIA_VIDEO:
                LOG_REMOTE(LOG_WARN, pSource, pLine, "Grabbing successful with chunk %d -recovered from error state of last video grabbing", pChunkNumber);
                break;
            case MEDIA_AUDIO:
                LOG_REMOTE(LOG_WARN, pSource, pLine, "Grabbing successful with chunk %d -recovered from error state of last audio grabbing", pChunkNumber);
                break;
            default:
                LOG_REMOTE(LOG_ERROR, pSource, pLine, "Media type unknown");
                LOG_REMOTE(LOG_WARN, pSource, pLine, "Grabbing successful with chunk %d -recovered from error state of last grabbing", pChunkNumber);
                break;
        }
    }
    mLastGrabResultWasError = false;
}

void MediaSource::EventGrabChunkFailed(string pSource, int pLine, string pReason)
{
    if ((!mLastGrabResultWasError) || (mLastGrabFailureReason != pReason))
    {
        LOG_REMOTE(LOG_ERROR, pSource, pLine, "Grabbing failed because \"%s\"", pReason.c_str());
    }
    mLastGrabResultWasError = true;
    mLastGrabFailureReason = pReason;
}

///////////////////////////////////////////////////////////////////////////////

}} //namespace
