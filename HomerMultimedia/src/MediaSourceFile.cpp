/*****************************************************************************
 *
 * Copyright (C) 2009 Thomas Volkert <thomas@homer-conferencing.com>
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
 * Purpose: Implementation of a ffmpeg based local file source
 * Author:  Thomas Volkert
 * Since:   2009-02-24
 */

#include <MediaSourceFile.h>
#include <ProcessStatisticService.h>
#include <Logger.h>
#include <HBThread.h>
#include <HBCondition.h>

#include <algorithm>
#include <string>
#include <unistd.h>

namespace Homer { namespace Multimedia {

using namespace std;
using namespace Homer::Monitor;

///////////////////////////////////////////////////////////////////////////////

MediaSourceFile::MediaSourceFile(string pSourceFile, bool pGrabInRealTime):
    MediaSource("FILE: " + pSourceFile)
{
    mDesiredDevice = pSourceFile;
    mGrabInRealTime = pGrabInRealTime;
    mResampleContext = NULL;
    mDecoderFifo = NULL;
    mDecoderMetaDataFifo = NULL;
    mDuration = 0;
    mCurPts = 0;
    mCurrentDeviceName = mDesiredDevice;
}

MediaSourceFile::~MediaSourceFile()
{
    if (mMediaSourceOpened)
        CloseGrabDevice();
}

///////////////////////////////////////////////////////////////////////////////

void MediaSourceFile::getVideoDevices(VideoDevicesList &pVList)
{
    VideoDeviceDescriptor tDevice;

    tDevice.Name = mDesiredDevice;
    tDevice.Card = mDesiredDevice;
    tDevice.Desc = "file source: \"" + mDesiredDevice + "\"";

    pVList.push_back(tDevice);
}

void MediaSourceFile::getAudioDevices(AudioDevicesList &pAList)
{
    AudioDeviceDescriptor tDevice;

    tDevice.Name = mDesiredDevice;
    tDevice.Card = mDesiredDevice;
    tDevice.Desc = "file source: \"" + mDesiredDevice + "\"";
    tDevice.IoType = "Input/Output";

    pAList.push_back(tDevice);
}

void MediaSourceFile::SetFrameRate(float pFps)
{
    LOG(LOG_VERBOSE, "Calls to SetFrameRate() are ignored here, otherwise the real-time playback would delay input data for the wrong FPS value");
    LOG(LOG_VERBOSE, "Ignoring %f, keeping FPS of %f", pFps, mFrameRate);
}

bool MediaSourceFile::OpenVideoGrabDevice(int pResX, int pResY, float pFps)
{
    int                 tResult = 0;
    AVCodec             *tCodec;
    AVDictionary        *tFormatOpts = NULL;
    AVDictionaryEntry   *tFormatOptsEntry = NULL;

    LOG(LOG_VERBOSE, "Trying to open the video source");

    if (mMediaType == MEDIA_AUDIO)
    {
        LOG(LOG_ERROR, "Wrong media type detected");
        return false;
    }

    SVC_PROCESS_STATISTIC.AssignThreadName("Video-Grabber(FILE)");

    // set category for packet statistics
    ClassifyStream(DATA_TYPE_VIDEO, SOCKET_RAW);

    if (mMediaSourceOpened)
        return false;

    string tFileType = mDesiredDevice.substr(mDesiredDevice.find_last_of(".") + 1);
    transform(tFileType.begin(), tFileType.end(), tFileType.begin(), ::tolower);
    LOG(LOG_VERBOSE, "Try to open video stream from file with extension \"%s\"", tFileType.c_str());
    if (tFileType == "mkv")
        tFileType = "matroska";
    if ((tFileType == "mpg") || (tFileType == "vob"))
        tFileType = "mpeg";

    LOG(LOG_VERBOSE, "try to open \"%s\"", mDesiredDevice.c_str());

    // open input: automatic content detection is done inside ffmpeg
    mFormatContext = AV_NEW_FORMAT_CONTEXT(); // make sure we have default values in format context, otherwise avformat_open_input() will crash
    if ((tResult = avformat_open_input(&mFormatContext, mDesiredDevice.c_str(), NULL, &tFormatOpts)) != 0)
    {
        LOG(LOG_ERROR, "Couldn't open video file \"%s\" because of \"%s\".", mDesiredDevice.c_str(), strerror(AVUNERROR(tResult)));
        return false;
    }

    if ((tFormatOptsEntry = av_dict_get(tFormatOpts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        LOG(LOG_ERROR, "Option %s not found.\n", tFormatOptsEntry->key);
        return false;
    }

    mCurrentDevice = mDesiredDevice;
    mCurrentDeviceName = mDesiredDevice;

    // limit frame analyzing time for ffmpeg internal codec auto detection
    mFormatContext->max_analyze_duration = AV_TIME_BASE / 4; //  1/4 recorded seconds
    // verbose timestamp debugging    mFormatContext->debug = FF_FDEBUG_TS;

    // Retrieve stream information
    if ((tResult = av_find_stream_info(mFormatContext)) < 0)
    {
        LOG(LOG_ERROR, "Couldn't find video stream information because of \"%s\".", strerror(AVUNERROR(tResult)));
        // Close the video file
        HM_close_input(mFormatContext);
        return false;
    }

    // Find the first video stream
    mMediaStreamIndex = -1;
    for (int i = 0; i < (int)mFormatContext->nb_streams; i++)
    {
        if(mFormatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            mMediaStreamIndex = i;
            break;
        }
    }
    if (mMediaStreamIndex == -1)
    {
        LOG(LOG_ERROR, "Couldn't find a video stream");
        // Close the video file
        HM_close_input(mFormatContext);
        return false;
    }

    // Dump information about device file
    av_dump_format(mFormatContext, mMediaStreamIndex, "MediaSourceFile(video)", false);
    //printf("    ..video stream found with ID: %d, number of available streams: %d\n", mMediaStreamIndex, mFormatContext->nb_streams);

    // Get a pointer to the codec context for the video stream
    mCodecContext = mFormatContext->streams[mMediaStreamIndex]->codec;

    // set grabbing resolution and frame-rate to the resulting ones delivered by opened video codec
    mSourceResX = mCodecContext->width;
    mSourceResY = mCodecContext->height;
    mFrameRate = (float)mFormatContext->streams[mMediaStreamIndex]->time_base.den / mFormatContext->streams[mMediaStreamIndex]->time_base.num;
    LOG(LOG_VERBOSE, "Frame rate determined: %f", mFrameRate);

    if ((mSourceResX == 0) || (mSourceResY == 0))
    {
        LOG(LOG_ERROR, "Couldn't find resolution information within video file");
        // Close the video file
        HM_close_input(mFormatContext);
        return false;
    }

    // Find the decoder for the video stream
    if((tCodec = avcodec_find_decoder(mCodecContext->codec_id)) == NULL)
    {
        LOG(LOG_ERROR, "Couldn't find a fitting video codec");
        // Close the video file
        HM_close_input(mFormatContext);
        return false;
    }

    // Open codec
    if ((tResult = avcodec_open(mCodecContext, tCodec)) < 0)
    {
        LOG(LOG_ERROR, "Couldn't open video codec because \"%s\".", strerror(AVUNERROR(tResult)));
        // Close the video file
        HM_close_input(mFormatContext);
        return false;
    }

    if((tResult = avformat_seek_file(mFormatContext, mMediaStreamIndex, 0, 0, MSF_SEEK_VARIANCE, AVSEEK_FLAG_ANY)) < 0)
    {
        LOG(LOG_WARN, "Couldn't seek to the start of video stream because \"%s\".", strerror(AVUNERROR(tResult)));
    }

    // allocate software scaler context
    mScalerContext = sws_getContext(mCodecContext->width, mCodecContext->height, mCodecContext->pix_fmt, mTargetResX, mTargetResY, PIX_FMT_RGB32, SWS_BICUBIC, NULL, NULL, NULL);

    //set duration
    if (mFormatContext->streams[mMediaStreamIndex]->duration > 0)
        mDuration = mFormatContext->streams[mMediaStreamIndex]->duration;
    else
        if (mFormatContext->streams[mMediaStreamIndex]->nb_frames > 0)
            mDuration = mFormatContext->streams[mMediaStreamIndex]->nb_frames;
        else
            if (mFormatContext->duration > 0)
                mDuration = mFormatContext->duration;
            else
                mDuration = 0;

    mCurPts = 0;
    mSeekingToPos = true;
    mMediaType = MEDIA_VIDEO;

    MarkOpenGrabDeviceSuccessful();

    // init transcoder FIFO based for RGB32 pictures
    StartDecoder(mTargetResX * mTargetResY * 4 /* bytes per pixel */);

    return true;
}

bool MediaSourceFile::OpenAudioGrabDevice(int pSampleRate, bool pStereo)
{
    int                 tResult = 0;
    AVCodec             *tCodec;
    AVDictionary        *tFormatOpts = NULL;
    AVDictionaryEntry   *tFormatOptsEntry = NULL;

    LOG(LOG_VERBOSE, "Trying to open the audio source");

    if (mMediaType == MEDIA_VIDEO)
    {
        LOG(LOG_ERROR, "Wrong media type detected");
        return false;
    }

    SVC_PROCESS_STATISTIC.AssignThreadName("Audio-Grabber(FILE)");

    // set category for packet statistics
    ClassifyStream(DATA_TYPE_AUDIO, SOCKET_RAW);

    if (mMediaSourceOpened)
        return false;

    string tFileType = mDesiredDevice.substr(mDesiredDevice.find_last_of(".") + 1);
    transform(tFileType.begin(), tFileType.end(), tFileType.begin(), ::tolower);
    LOG(LOG_VERBOSE, "Try to open an audio stream from file with extension \"%s\"", tFileType.c_str());
    if ((tFileType == "mkv") || (tFileType == "mka"))
        tFileType = "matroska";
    if (tFileType == "vob")
        tFileType = "mpeg";

    LOG(LOG_VERBOSE, "try to open \"%s\"", mDesiredDevice.c_str());

    // open input: automatic content detection is done inside ffmpeg
    mFormatContext = AV_NEW_FORMAT_CONTEXT(); // make sure we have default values in format context, otherwise avformat_open_input() will crash
    if ((tResult = avformat_open_input(&mFormatContext, mDesiredDevice.c_str(), NULL, &tFormatOpts)) != 0)
    {
        LOG(LOG_ERROR, "Couldn't open audio file \"%s\" because of \"%s\".", mDesiredDevice.c_str(), strerror(AVUNERROR(tResult)));
        return false;
    }

    if ((tFormatOptsEntry = av_dict_get(tFormatOpts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        LOG(LOG_ERROR, "Option %s not found.\n", tFormatOptsEntry->key);
        return false;
    }

    mCurrentDevice = mDesiredDevice;
    mCurrentDeviceName = mDesiredDevice;

    // limit frame analyzing time for ffmpeg internal codec auto detection
    mFormatContext->max_analyze_duration = AV_TIME_BASE / 4; //  1/4 recorded seconds
    // verbose timestamp debugging    mFormatContext->debug = FF_FDEBUG_TS;

    // Retrieve stream information
    if ((tResult = av_find_stream_info(mFormatContext)) < 0)
    {
        LOG(LOG_ERROR, "Couldn't find audio stream information because of \"%s\".", strerror(AVUNERROR(tResult)));
        // Close the audio file
        HM_close_input(mFormatContext);
        return false;
    }

    // enumerate all audio streams and store them as possible input channels
    // find correct audio stream, depending on the desired input channel
    string tEntry;
    AVDictionaryEntry *tDictEntry;
    char tLanguageBuffer[256];
    int tAudioStreamCount = 0;
    LOG(LOG_VERBOSE, "Probing for multiple input channels for device %s", mCurrentDevice.c_str());
    mInputChannels.clear();
    mMediaStreamIndex = -1;
    for (int i = 0; i < (int)mFormatContext->nb_streams; i++)
    {
        if(mFormatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            tCodec = avcodec_find_decoder(mFormatContext->streams[i]->codec->codec_id);
            tDictEntry = NULL;
            int tLanguageCount = 0;
            memset(tLanguageBuffer, 0, 256);
            while((tDictEntry = HM_av_metadata_get(mFormatContext->metadata, "", tDictEntry)))
            {
                if (strcmp("language", tDictEntry->key))
                {
                    if (i == tLanguageCount)
                    {
                        av_strlcpy(tLanguageBuffer, tDictEntry->value, sizeof(tLanguageBuffer));
                        for (int j = 0; j < (int)strlen(tLanguageBuffer); j++)
                        {
                            if (tLanguageBuffer[j] == 0x0D)
                                tLanguageBuffer[j]=0;
                        }
                        LOG(LOG_VERBOSE, "Language found: %s", tLanguageBuffer);
                    }
                    tLanguageCount++;
                }
            }
            LOG(LOG_VERBOSE, "Desired inputchannel: %d, current found input channel: %d", mDesiredInputChannel, tAudioStreamCount);
            if(tAudioStreamCount == mDesiredInputChannel)
            {
                mMediaStreamIndex = i;
                LOG(LOG_VERBOSE, "Using audio input channel %d in stream %d for grabbing", mDesiredInputChannel, i);
            }

            tAudioStreamCount++;

            if (strlen(tLanguageBuffer) > 0)
                tEntry = "Audio " + toString(tAudioStreamCount) + ": " + toString(tCodec->name) + " [" + toString(mFormatContext->streams[i]->codec->channels) + " channel(s)] " + string(tLanguageBuffer);
            else
                tEntry = "Audio " + toString(tAudioStreamCount) + ": " + toString(tCodec->name) + " [" + toString(mFormatContext->streams[i]->codec->channels) + " channel(s)]";
            LOG(LOG_VERBOSE, "Found audio stream: %s", tEntry.c_str());
            mInputChannels.push_back(tEntry);
        }
    }
    if (mMediaStreamIndex == -1)
    {
        LOG(LOG_ERROR, "Couldn't find an audio stream");
        // Close the audio file
        HM_close_input(mFormatContext);
        return false;
    }

    mCurrentInputChannel = mDesiredInputChannel;

    // Dump information about device file
    av_dump_format(mFormatContext, mMediaStreamIndex, "MediaSourceFile(audio)", false);
    //printf("    ..audio stream found with ID: %d, number of available streams: %d\n", mMediaStreamIndex, mFormatContext->nb_streams);

    // Get a pointer to the codec context for the audio stream
    mCodecContext = mFormatContext->streams[mMediaStreamIndex]->codec;

    // set sample rate and bit rate to the resulting ones delivered by opened audio codec
    mSampleRate = mCodecContext->sample_rate;
    mStereo = pStereo;

    // set rate of incoming frames
    mFrameRate = (float)mFormatContext->streams[mMediaStreamIndex]->time_base.den / mFormatContext->streams[mMediaStreamIndex]->time_base.num;
    LOG(LOG_VERBOSE, "Frame rate determined: %f", mFrameRate);

    // Find the decoder for the audio stream
    if((tCodec = avcodec_find_decoder(mCodecContext->codec_id)) == NULL)
    {
        LOG(LOG_ERROR, "Couldn't find a fitting audio codec");
        // Close the audio file
        HM_close_input(mFormatContext);
        return false;
    }

    // Inform the codec that we can handle truncated bitstreams
    // bitstreams where sample boundaries can fall in the middle of packets
    if(tCodec->capabilities & CODEC_CAP_TRUNCATED)
        mCodecContext->flags |= CODEC_FLAG_TRUNCATED;

    // Open codec
    if ((tResult = avcodec_open(mCodecContext, tCodec)) < 0)
    {
        LOG(LOG_ERROR, "Couldn't open audio codec because of \"%s\".", strerror(AVUNERROR(tResult)));
        // Close the audio file
        HM_close_input(mFormatContext);
        return false;
    }

    if((tResult = avformat_seek_file(mFormatContext, mMediaStreamIndex, 0, 0, MSF_SEEK_VARIANCE, AVSEEK_FLAG_ANY)) < 0)
    {
        LOG(LOG_WARN, "Couldn't seek to the start of audio stream because \"%s\".", strerror(AVUNERROR(tResult)));
    }

    // create resample context
    if ((mCodecContext->sample_rate != 44100) || (mCodecContext->channels != 2))
    {
        LOG(LOG_WARN, "Audio samples with rate of %d Hz have to be resampled to 44100 Hz", mCodecContext->sample_rate);
        mResampleContext = av_audio_resample_init(2, mCodecContext->channels, 44100, mCodecContext->sample_rate, SAMPLE_FMT_S16, mCodecContext->sample_fmt, 16, 10, 0, 0.8);
    }

    //set duration
    if (mFormatContext->streams[mMediaStreamIndex]->duration > 0)
        mDuration = mFormatContext->streams[mMediaStreamIndex]->duration;
    else
        if (mFormatContext->streams[mMediaStreamIndex]->nb_frames > 0)
            mDuration = mFormatContext->streams[mMediaStreamIndex]->nb_frames;
        else
            if (mFormatContext->duration > 0)
                mDuration = mFormatContext->duration / AV_TIME_BASE; //TODO: value is in seconds, should be related to time base!
            else
                mDuration = 0;

    mResampleBuffer = (char*)malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
    mCurPts = 0;
    mSeekingToPos = true;
    mMediaType = MEDIA_AUDIO;

    MarkOpenGrabDeviceSuccessful();

    // init decoder FIFO based for 2048 samples with 16 bit and 2 channels, more samples are never produced by a media source per grabbing cycle
    StartDecoder(MEDIA_SOURCE_SAMPLES_MULTI_BUFFER_SIZE * 2);

    return true;
}

bool MediaSourceFile::CloseGrabDevice()
{
    bool tResult = false;

    LOG(LOG_VERBOSE, "Going to close, media type is \"%s\"", GetMediaTypeStr().c_str());

    if (mMediaSourceOpened)
    {
        StopRecording();

        mMediaSourceOpened = false;

        // make sure we can free the memory structures
        StopDecoder();

        // free the software scaler context
        if (mMediaType == MEDIA_VIDEO)
            sws_freeContext(mScalerContext);

        // free resample context
        if ((mMediaType == MEDIA_AUDIO) && (mResampleContext != NULL))
        {
            audio_resample_close(mResampleContext);
            mResampleContext = NULL;
        }

        // Close the codec
        avcodec_close(mCodecContext);

        // Close the file
        HM_close_input(mFormatContext);

        mInputChannels.clear();

        if (mMediaType == MEDIA_AUDIO)
            free(mResampleBuffer);

        LOG(LOG_INFO, "...closed, media type is \"%s\"", GetMediaTypeStr().c_str());

        tResult = true;
    }else
        LOG(LOG_INFO, "...wasn't open, media type is \"%s\"", GetMediaTypeStr().c_str());

    mGrabbingStopped = false;
    mMediaType = MEDIA_UNKNOWN;

    ResetPacketStatistic();

    return tResult;
}

int MediaSourceFile::GrabChunk(void* pChunkBuffer, int& pChunkSize, bool pDropChunk)
{
    #ifdef MSF_DEBUG_PACKETS
        LOG(LOG_VERBOSE, "Going to grab new chunk from FIFO");
    #endif
    // lock grabbing
    mGrabMutex.lock();

    if (pChunkBuffer == NULL)
    {
        // unlock grabbing
        mGrabMutex.unlock();

        // acknowledge failed"
        MarkGrabChunkFailed(GetMediaTypeStr() + " grab buffer is NULL");

        return GRAB_RES_INVALID;
    }

    if (!mMediaSourceOpened)
    {
        // unlock grabbing
        mGrabMutex.unlock();

        // acknowledge failed"
        MarkGrabChunkFailed(GetMediaTypeStr() + " source is closed");

        return GRAB_RES_INVALID;
    }

    if (mGrabbingStopped)
    {
        // unlock grabbing
        mGrabMutex.unlock();

        // acknowledge failed"
        MarkGrabChunkFailed(GetMediaTypeStr() + " source is paused");

        return GRAB_RES_INVALID;
    }

    if ((mDecoderFifo->GetUsage() == 0) && (mEOFReached))
    {
        mChunkNumber++;

        // unlock grabbing
        mGrabMutex.unlock();

        // acknowledge "success"
        MarkGrabChunkSuccessful(mChunkNumber); // don't panic, it is only EOF

        return GRAB_RES_EOF;
    }

    if (mDecoderFifo->GetUsage() == 0)
    {
        #ifdef MSF_DEBUG_TIMING
            LOG(LOG_VERBOSE, "Signal to decoder that new data is needed");
        #endif
        mDecoderMutex.lock();
        DecoderNeedWorkCondition.SignalAll();
        mDecoderMutex.unlock();
    }

    // read chunk data from FIFO
    mDecoderFifo->ReadFifo((char*)pChunkBuffer, pChunkSize);
    mChunkNumber++;

    // did we read an empty frame?
    if (pChunkSize == 0)
    {
        // unlock grabbing
        mGrabMutex.unlock();

        // acknowledge "success"
        MarkGrabChunkSuccessful(mChunkNumber); // don't panic, it is only EOF

        return GRAB_RES_EOF;
    }

    #ifdef MSF_DEBUG_PACKETS
        LOG(LOG_VERBOSE, "Grabbed chunk %d of size %d from decoder FIFO", mChunkNumber, pChunkSize);
    #endif

    // read meta description about current chunk from different FIFO
    struct ChunkDescriptor tChunkDesc;
    int tChunkDescSize = sizeof(tChunkDesc);
    mDecoderMetaDataFifo->ReadFifo((char*)&tChunkDesc, tChunkDescSize);
    #ifdef MSF_DEBUG_PACKETS
        LOG(LOG_VERBOSE, "Grabbed meta data of size %d from decoder FIFO", tChunkDescSize);
    #endif
    if (tChunkDescSize != sizeof(tChunkDesc))
        LOG(LOG_ERROR, "Read from FIFO a chunk with wrong size of %d bytes, expected size is %d bytes", tChunkDescSize, sizeof(tChunkDesc));
    mCurPts = tChunkDesc.Pts;
    #ifdef MSF_DEBUG_PACKETS
        LOG(LOG_VERBOSE, "PTS of grabbed chunk is %d", mCurPts);
    #endif
    if (mCurPts == mDuration)
        mEOFReached = true;

    // unlock grabbing
    mGrabMutex.unlock();

    // #########################################
    // frame rate emulation
    // #########################################
    // inspired by "output_packet" from ffmpeg.c
    if (mGrabInRealTime)
    {
        // should we initiate the StartPts value?
        if (mSourceStartPts == -1)
            mSourceStartPts = mCurPts;
        else
        {
            int64_t tRelativePacketTimeUSecs, tRelativeRealTimeUSecs, tDiffPtsUSecs;
            double tPacketRealPtsTmp = 1000000 * (mCurPts - mSourceStartPts) / mFrameRate; // current presentation time in useconds of the current packet from the file source
            tRelativePacketTimeUSecs = (int64_t)tPacketRealPtsTmp;
            tRelativeRealTimeUSecs = av_gettime() - mStartPtsUSecs;
            tDiffPtsUSecs = tRelativePacketTimeUSecs - tRelativeRealTimeUSecs;
            #ifdef MSF_DEBUG_TIMING
                LOG(LOG_VERBOSE, "%s-current relative time: %8lld us (Fps: %3.2f), stream start time: %6lld us, packet's relative play out time: %8lld us, time difference: %8lld us", GetMediaTypeStr().c_str(), tRelativeRealTimeUSecs, mFrameRate, mSourceStartPts, tRelativePacketTimeUSecs, tDiffPtsUSecs);
            #endif
            // adapt timing to real-time, ignore timings between -5 ms and +5 ms
            if (tDiffPtsUSecs > 0)
            {
                #ifdef MSF_DEBUG_TIMING
                    LOG(LOG_WARN, "%s-sleeping for %d us", GetMediaTypeStr().c_str(), tDiffPtsUSecs);
                #endif
                Thread::Suspend(tDiffPtsUSecs);
            }else
            {
                #ifdef MSF_DEBUG_TIMING
                    if (tDiffPtsUSecs < -MSF_FRAME_DROP_THRESHOLD)
                        LOG(LOG_VERBOSE, "%s-system too busy (delay threshold is %dms), decoded frame time diff.: %dms", GetMediaTypeStr().c_str(),MSF_FRAME_DROP_THRESHOLD / 1000, -tDiffPtsUSecs / 1000);
                #endif
            }
        }
    }

    // acknowledge success
    MarkGrabChunkSuccessful(mChunkNumber);

    return mChunkNumber;
}

void MediaSourceFile::StartDecoder(int pFifoEntrySize)
{
    LOG(LOG_VERBOSE, "Starting decoder with FIFO entry size of %d bytes", pFifoEntrySize);

    mDecoderTargetResX = mTargetResX;
    mDecoderTargetResY = mTargetResY;
    if (mDecoderFifo != NULL)
    {
        LOG(LOG_ERROR, "FIFO already initiated");
        delete mDecoderFifo;
    }else
    {
        mDecoderFifo = new MediaFifo(MEDIA_SOURCE_FILE_INPUT_QUEUE_SIZE_LIMIT, pFifoEntrySize, GetMediaTypeStr() + "-MediaSourceFile");
        mDecoderMetaDataFifo = new MediaFifo(MEDIA_SOURCE_FILE_INPUT_QUEUE_SIZE_LIMIT, sizeof(ChunkDescriptor), GetMediaTypeStr() + "-MediaSourceFile");

        mDecoderNeeded = true;

        // start transcoder main loop
        StartThread();
    }
}

void MediaSourceFile::StopDecoder()
{
    int tSignalingRound = 0;
    char tTmp[4];

    LOG(LOG_VERBOSE, "Stopping decoder");

    if (mDecoderFifo != NULL)
    {
        // tell transcoder thread it isn't needed anymore
        mDecoderNeeded = false;

        // wait for termination of decoder thread
        do
        {
            if(tSignalingRound > 0)
                LOG(LOG_WARN, "Signaling round %d to stop decoder, system has high load", tSignalingRound);
            tSignalingRound++;

            // force a wake up of decoder thread
            DecoderNeedWorkCondition.SignalAll();
        }while(!StopThread(250));

        delete mDecoderFifo;
        delete mDecoderMetaDataFifo;

        mDecoderFifo = NULL;
    }

    LOG(LOG_VERBOSE, "Decoder stopped");
}

void* MediaSourceFile::Run(void* pArgs)
{
    AVFrame             *tSourceFrame = NULL, *tRGBFrame = NULL;
    AVPacket            tPacketStruc, *tPacket = &tPacketStruc;
    int                 tFrameFinished = 0;
    int                 tBytesDecoded = 0;
    int                 tRes = 0;
    bool                tDropChunks = false;
    int                 tChunkBufferSize = 0;
    uint8_t             *tChunkBuffer;
    int                 tWaitLoop;
    /* current chunk */
    int                 tCurrentChunkSize = 0;
    int64_t             tCurrentChunkPts = 0;

    LOG(LOG_VERBOSE, "Transcoder started, media type is \"%s\"", GetMediaTypeStr().c_str());
    switch(mMediaType)
    {
        case MEDIA_VIDEO:
            SVC_PROCESS_STATISTIC.AssignThreadName("Video-Decoder(FILE," + FfmpegId2FfmpegFormat(mCodecContext->codec_id) + ")");
            tChunkBufferSize = mDecoderTargetResX * mDecoderTargetResY * 4 /* bytes per pixel */;
            break;
        case MEDIA_AUDIO:
            SVC_PROCESS_STATISTIC.AssignThreadName("Audio-Decoder(FILE," + FfmpegId2FfmpegFormat(mCodecContext->codec_id) + ")");
            tChunkBufferSize = AVCODEC_MAX_AUDIO_FRAME_SIZE;
            break;
        default:
            SVC_PROCESS_STATISTIC.AssignThreadName("Decoder(FILE," + FfmpegId2FfmpegFormat(mCodecContext->codec_id) + ")");
            tChunkBufferSize = MEDIA_SOURCE_AV_CHUNK_BUFFER_SIZE;
            break;
    }

    // allocate chunk buffer
    tChunkBuffer = (uint8_t*)malloc(tChunkBufferSize);

    // reset EOF marker
    mEOFReached = false;

    // reset last PTS
    mDecoderLastReadPts = 0;

    while(mDecoderNeeded)
    {
        #ifdef MSF_DEBUG_TIMING
            LOG(LOG_VERBOSE, "Decoder loop");
        #endif
        mDecoderMutex.lock();

        if ((mDecoderFifo != NULL) && (mDecoderFifo->GetUsage() < MEDIA_SOURCE_FILE_INPUT_QUEUE_SIZE_LIMIT - 1 /* one slot for a 0 byte signaling chunk*/) /* meta data FIFO has always the same size => hence, we don't have to check its size */)
        {

            if (mEOFReached)
                LOG(LOG_WARN, "Error in state machine, we started when EOF was already reached");

            tWaitLoop = 0;

            // Read new packet
            // return 0 if OK, < 0 if error or end of file.
            bool tShouldReadNext;
            int tReadIteration = 0;
            do
            {
                tShouldReadNext = false;

                if (tReadIteration > 0)
                {
                    // free packet buffer
                    av_free_packet(tPacket);
                }

                tReadIteration++;

                // read next sample from source - blocking
                if ((tRes = av_read_frame(mFormatContext, tPacket)) != 0)
                {// failed to read frame
                    if ((!mGrabbingStopped) && (tRes != (int)AVERROR_EOF) && (tRes != (int)AVERROR(EIO)))
                        LOG(LOG_ERROR, "Couldn't grab a frame because of \"%s\"(%d), media type is \"%s\"", strerror(AVUNERROR(tRes)), tRes, GetMediaTypeStr().c_str());

                    if (tPacket->size == 0)
                        tShouldReadNext = true;

                    if (tRes == (int)AVERROR_EOF)
                    {
                        tCurrentChunkPts = mDuration;
                        LOG(LOG_VERBOSE, "%s-Decoder reached EOF", GetMediaTypeStr().c_str());
                        mEOFReached = true;
                    }
                    if (tRes == (int)AVERROR(EIO))
                    {
                        // acknowledge failed"
                        MarkGrabChunkFailed(GetMediaTypeStr() + " source has I/O error");

                        // signal EOF instead of I/O error
                        mEOFReached = true;
                    }
                    tShouldReadNext = true;
                }else
                {// new frame was read
                    if (tPacket->stream_index == mMediaStreamIndex)
                    {
                        // is "presentation timestamp" stored within media file?
                        if (tPacket->pts == (int64_t)AV_NOPTS_VALUE)
                        {// pts isn't stored in the media file, fall back to "decompression timestamp"
                            if ((tPacket->dts < mDecoderLastReadPts) && (mDecoderLastReadPts != 0))
                                LOG(LOG_WARN, "DTS values non continuous in file, %ld is lower than last %ld", tPacket->dts, mDecoderLastReadPts);
                            tCurrentChunkPts = tPacket->dts;
                            mDecoderLastReadPts = tCurrentChunkPts;
                        }else
                        {// pts is stored in the media file, use it
                            if ((tPacket->pts < mDecoderLastReadPts) && (mDecoderLastReadPts != 0))
                                LOG(LOG_WARN, "PTS values non continuous in file, %ld is lower than last %ld, difference is: %ld", tPacket->pts, mDecoderLastReadPts, mDecoderLastReadPts - tPacket->pts);
                            tCurrentChunkPts = tPacket->pts;
                            mDecoderLastReadPts = tCurrentChunkPts;
                        }
                    }else
                    {
                        tShouldReadNext = true;
                        #ifdef MSF_DEBUG_PACKETS
                            LOG(LOG_VERBOSE, "Read stream %d instead of desired stream %d", tPacket->stream_index, mMediaStreamIndex);
                        #endif
                    }
                }
            }while ((tShouldReadNext) && (!mEOFReached));

            #ifdef MSF_DEBUG_PACKETS
                if (tReadIteration > 1)
                    LOG(LOG_VERBOSE, "Needed %d read iterations to get next media packet from source file", tReadIteration);
                LOG(LOG_VERBOSE, "New %s chunk with size: %d and stream index: %d", GetMediaTypeStr().c_str(), tPacket->size, tPacket->stream_index);
            #endif

            if ((!mEOFReached) && ((tPacket->data != NULL) && (tPacket->size > 0)))
            {
                #ifdef MSF_DEBUG_PACKETS
                    LOG(LOG_VERBOSE, "New packet..");
                    LOG(LOG_VERBOSE, "      ..duration: %d", tPacket->duration);
                    LOG(LOG_VERBOSE, "      ..pts: %ld stream [%d] pts: %ld", tPacket->pts, mMediaStreamIndex, mFormatContext->streams[mMediaStreamIndex]->pts.val);
                    LOG(LOG_VERBOSE, "      ..stream: %ld", tPacket->stream_index);
                    LOG(LOG_VERBOSE, "      ..dts: %ld", tPacket->dts);
                    LOG(LOG_VERBOSE, "      ..size: %d", tPacket->size);
                    LOG(LOG_VERBOSE, "      ..pos: %ld", tPacket->pos);
                #endif

                // #########################################
                // process packet
                // #########################################
                switch(mMediaType)
                {
                    case MEDIA_VIDEO:
                            if ((!tDropChunks) || (mRecording))
                            {
                                // log statistics
                                AnnouncePacket(tPacket->size);
                                #ifdef MSF_DEBUG_PACKETS
                                    LOG(LOG_VERBOSE, "Decode video frame..");
                                #endif

                                // Allocate video frame for source and RGB format
                                if ((tSourceFrame = avcodec_alloc_frame()) == NULL)
                                {
                                    // acknowledge failed"
                                    LOG(LOG_ERROR, "Out of video memory");
                                }

                                // Decode the next chunk of data
                                tBytesDecoded = HM_avcodec_decode_video(mCodecContext, tSourceFrame, &tFrameFinished, tPacket);

                                // transfer the presentation time value
                                tSourceFrame->pts = tCurrentChunkPts;

                                #ifdef MSF_DEBUG_PACKETS
                                    LOG(LOG_VERBOSE, "    ..with result(!= 0 => OK): %d bytes: %i\n", tFrameFinished, tBytesDecoded);
                                #endif

                                // log lost packets: difference between currently received frame number and the number of locally processed frames
                                SetLostPacketCount(tSourceFrame->coded_picture_number - mChunkNumber);

                                #ifdef MSF_DEBUG_PACKETS
                                    LOG(LOG_VERBOSE, "Video frame %d decoded", tSourceFrame->coded_picture_number);
                                #endif

                                // hint: 32 bytes additional data per line within ffmpeg
                                int tCurrentFrameResX = (tSourceFrame->linesize[0] - 32);
                            }

                            // re-encode the frame and write it to file
                            if (mRecording)
                                RecordFrame(tSourceFrame);

                            // scale only if frame shouldn't be dropped
                            if (!tDropChunks)
                            {
                                // Allocate video frame for RGB format
                                if ((tRGBFrame = avcodec_alloc_frame()) == NULL)
                                {
                                    // acknowledge failed"
                                    LOG(LOG_ERROR, "Out of video memory");

                                    tCurrentChunkSize = 0;
                                }

                                // Assign appropriate parts of buffer to image planes in tRGBFrame
                                avpicture_fill((AVPicture *)tRGBFrame, (uint8_t *)tChunkBuffer, PIX_FMT_RGB32, mDecoderTargetResX, mDecoderTargetResY);

                                // convert frame from YUV to RGB format
                                if ((tFrameFinished != 0) && (tBytesDecoded >= 0))
                                {
                                    #ifdef MSF_DEBUG_PACKETS
                                        LOG(LOG_VERBOSE, "Scale video frame..");
                                    #endif
                                    HM_sws_scale(mScalerContext, tSourceFrame->data, tSourceFrame->linesize, 0, mCodecContext->height, tRGBFrame->data, tRGBFrame->linesize);

                                    // return size of decoded frame
                                    tCurrentChunkSize = avpicture_get_size(PIX_FMT_RGB32, mDecoderTargetResX, mDecoderTargetResY);

                                    #ifdef MSF_DEBUG_PACKETS
                                        LOG(LOG_VERBOSE, "New video frame..");
                                        LOG(LOG_VERBOSE, "      ..key frame: %d", tSourceFrame->key_frame);
                                        switch(tSourceFrame->pict_type)
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
                                                    LOG(LOG_VERBOSE, "      ..picture type: %d", tSourceFrame->pict_type);
                                                    break;
                                        }
                                        LOG(LOG_VERBOSE, "      ..pts: %ld", tSourceFrame->pts);
                                        LOG(LOG_VERBOSE, "      ..coded pic number: %d", tSourceFrame->coded_picture_number);
                                        LOG(LOG_VERBOSE, "      ..display pic number: %d", tSourceFrame->display_picture_number);
                                        LOG(LOG_VERBOSE, "Resulting frame size is %d bytes", tCurrentChunkSize);
                                    #endif
                                }else
                                {
                                    // only print debug output if it is not "operation not permitted"
                                    //if ((tBytesDecoded < 0) && (AVUNERROR(tBytesDecoded) != EPERM))
                                    // acknowledge failed"
                                    LOG(LOG_WARN, "Couldn't decode video frame because \"%s\"(%d)", strerror(AVUNERROR(tBytesDecoded)), AVUNERROR(tBytesDecoded));

                                    tCurrentChunkSize = 0;
                                }

                                // Free the RGB frame
                                av_free(tRGBFrame);
                            }

                            // Free the YUV frame
                            if (tSourceFrame != NULL)
                                av_free(tSourceFrame);

                            break;

                    case MEDIA_AUDIO:
                            if ((!tDropChunks) || (mRecording))
                            {
                                // log statistics
                                AnnouncePacket(tPacket->size);

                                //printf("DecodeFrame..\n");
                                // Decode the next chunk of data
                                int tOutputBufferSize = tChunkBufferSize;
                                #ifdef MSF_DEBUG_PACKETS
                                    LOG(LOG_VERBOSE, "Decoding audio samples into buffer of size: %d", tOutputBufferSize);
                                #endif

                                int tBytesDecoded;

                                if ((mCodecContext->sample_rate == 44100) && (mCodecContext->channels == 2))
                                {
                                    tBytesDecoded = HM_avcodec_decode_audio(mCodecContext, (int16_t *)tChunkBuffer, &tOutputBufferSize, tPacket);
                                    tCurrentChunkSize = tOutputBufferSize;
                                }else
                                {// have to insert an intermediate step, which resamples the audio chunk to 44.1 kHz
                                    tBytesDecoded = HM_avcodec_decode_audio(mCodecContext, (int16_t *)mResampleBuffer, &tOutputBufferSize, tPacket);

                                    if(tOutputBufferSize > 0)
                                    {
                                        //HINT: we always assume 16 bit samples and a stereo signal, so we have to divide/multiply by 4
                                        int tResampledBytes = (2 /*16 signed char*/ * 2 /*channels*/) * audio_resample(mResampleContext, (short*)tChunkBuffer, (short*)mResampleBuffer, tOutputBufferSize / (2 * mCodecContext->channels));
                                        #ifdef MSF_DEBUG_PACKETS
                                            LOG(LOG_VERBOSE, "Have resampled %d bytes of sample rate %dHz and %d channels to %d bytes of sample rate 44100Hz and 2 channels", tOutputBufferSize, mCodecContext->sample_rate, mCodecContext->channels, tResampledBytes);
                                        #endif
                                        if(tResampledBytes > 0)
                                        {
                                            tCurrentChunkSize = tResampledBytes;
                                        }else
                                        {
                                            LOG(LOG_ERROR, "Amount of resampled bytes (%d) is invalid", tResampledBytes);
                                            tCurrentChunkSize = 0;
                                        }
                                    }else
                                    {
                                        LOG(LOG_ERROR, "Output buffer size %d from audio decoder is invalid", tOutputBufferSize);
                                        tCurrentChunkSize = 0;
                                    }
                                }

                                // re-encode the frame and write it to file
                                if ((mRecording) && (tCurrentChunkSize > 0))
                                    RecordSamples((int16_t *)tChunkBuffer, tCurrentChunkSize);

                                #ifdef MSF_DEBUG_PACKETS
                                    LOG(LOG_VERBOSE, "Result is an audio frame with size of %d bytes from %d encoded bytes", tOutputBufferSize, tBytesDecoded);
                                #endif

                                if ((tBytesDecoded <= 0) || (tOutputBufferSize <= 0))
                                {
                                    // only print debug output if it is not "operation not permitted"
                                    //if (AVUNERROR(tBytesDecoded) != EPERM)
                                    // acknowledge failed"
                                    LOG(LOG_WARN, "Couldn't decode audio samples because \"%s\"(%d)", strerror(AVUNERROR(tBytesDecoded)), AVUNERROR(tBytesDecoded));

                                    tCurrentChunkSize = 0;
                                }
                            }
                            break;

                    default:
                            LOG(LOG_ERROR, "Media type unknown");
                            break;

                }

                // was there an error during decoding process?
                if (tCurrentChunkSize > 0)
                {
                    // add new chunk to FIFO
                    #ifdef MSF_DEBUG_PACKETS
                        LOG(LOG_VERBOSE, "Writing %d bytes to FIFO", tCurrentChunkSize);
                    #endif
                    mDecoderFifo->WriteFifo((char*)tChunkBuffer, tCurrentChunkSize);
                    // add meta description about current chunk to different FIFO
                    struct ChunkDescriptor tChunkDesc;
                    tChunkDesc.Pts = tCurrentChunkPts;
                    mDecoderMetaDataFifo->WriteFifo((char*) &tChunkDesc, sizeof(tChunkDesc));
                    #ifdef MSF_DEBUG_PACKETS
                        LOG(LOG_VERBOSE, "Successful decoder loop");
                    #endif
                }
            }

            // free packet buffer
            av_free_packet(tPacket);

            if (mEOFReached)
            {// EOF, wait until restart
                // add empty chunk to FIFO
                #ifdef MSF_DEBUG_PACKETS
                    LOG(LOG_VERBOSE, "Writing empty chunk to FIFO");
                #endif
                char tData[4];
                mDecoderFifo->WriteFifo(tData, 0);

                // add meta description about current chunk to different FIFO
                struct ChunkDescriptor tChunkDesc;
                tChunkDesc.Pts = 0;
                mDecoderMetaDataFifo->WriteFifo((char*) &tChunkDesc, sizeof(tChunkDesc));

                // time to sleep
                #ifdef MSF_DEBUG_DECODER_STATE
                    LOG(LOG_VERBOSE, "EOF for %s source reached, wait some time and check again, loop %d", GetMediaTypeStr().c_str(), ++tWaitLoop);
                #endif
                DecoderNeedWorkCondition.Reset();
                DecoderNeedWorkCondition.Wait(&mDecoderMutex);
                mDecoderLastReadPts = 0;
                mEOFReached = false;
                #ifdef MSF_DEBUG_DECODER_STATE
                    LOG(LOG_VERBOSE, "Continuing after file decoding was restarted");
                #endif
            }
        }else
        {// decoder FIFO is full, nothing to be done
            #ifdef MSF_DEBUG_DECODER_STATE
                LOG(LOG_VERBOSE, "Nothing to do for decoder, wait some time and check again, loop %d", ++tWaitLoop);
            #endif
            DecoderNeedWorkCondition.Reset();
            DecoderNeedWorkCondition.Wait(&mDecoderMutex);
            mDecoderLastReadPts = 0;
            #ifdef MSF_DEBUG_DECODER_STATE
                LOG(LOG_VERBOSE, "Continuing after new data is needed, current FIFO size is: %d", mDecoderFifo->GetUsage());
            #endif
        }

        mDecoderMutex.unlock();
    }

    free((void*)tChunkBuffer);

    LOG(LOG_VERBOSE, "Decoder loop finished");

    return NULL;
}

void MediaSourceFile::DoSetVideoGrabResolution(int pResX, int pResY)
{
    LOG(LOG_VERBOSE, "Going to execute DoSetVideoGrabResolution()");

    if ((mMediaSourceOpened) && (mScalerContext != NULL))
    {
        if (mMediaType == MEDIA_UNKNOWN)
        {
            LOG(LOG_WARN, "Media type is still unknown when DoSetVideoGrabResolution is called, setting type to VIDEO ");
            mMediaType = MEDIA_VIDEO;
        }

        StopDecoder();

        // free the software scaler context
        sws_freeContext(mScalerContext);

        // allocate software scaler context
        mScalerContext = sws_getContext(mCodecContext->width, mCodecContext->height, mCodecContext->pix_fmt, pResX, pResY, PIX_FMT_RGB32, SWS_BICUBIC, NULL, NULL, NULL);

        StartDecoder(mTargetResX * mTargetResY * 4 /* bytes per pixel */);
    }
}

GrabResolutions MediaSourceFile::GetSupportedVideoGrabResolutions()
{
    VideoFormatDescriptor tFormat;

    mSupportedVideoFormats.clear();

    if (mMediaType == MEDIA_VIDEO)
    {
        tFormat.Name="SQCIF";      //      128 �  96
        tFormat.ResX = 128;
        tFormat.ResY = 96;
        //mSupportedVideoFormats.push_back(tFormat);

        tFormat.Name="QCIF";       //      176 � 144
        tFormat.ResX = 176;
        tFormat.ResY = 144;
        //mSupportedVideoFormats.push_back(tFormat);

        tFormat.Name="CIF";        //      352 � 288
        tFormat.ResX = 352;
        tFormat.ResY = 288;
        mSupportedVideoFormats.push_back(tFormat);

        tFormat.Name="VGA";       //
        tFormat.ResX = 640;
        tFormat.ResY = 480;
        mSupportedVideoFormats.push_back(tFormat);

        tFormat.Name="CIF4";       //      704 � 576
        tFormat.ResX = 704;
        tFormat.ResY = 576;
        //mSupportedVideoFormats.push_back(tFormat);

        tFormat.Name="SVGA";       //
        tFormat.ResX = 800;
        tFormat.ResY = 600;
        mSupportedVideoFormats.push_back(tFormat);

        tFormat.Name="XGA";       //
        tFormat.ResX = 1024;
        tFormat.ResY = 768;
        mSupportedVideoFormats.push_back(tFormat);

        tFormat.Name="CIF9";       //     1056 � 864
        tFormat.ResX = 1056;
        tFormat.ResY = 864;
        //mSupportedVideoFormats.push_back(tFormat);

        tFormat.Name="CIF16";      //     1408 � 1152
        tFormat.ResX = 1408;
        tFormat.ResY = 1152;
        //mSupportedVideoFormats.push_back(tFormat);

        tFormat.Name="HDTV";       //     1920 � 1080
        tFormat.ResX = 1920;
        tFormat.ResY = 1080;
        mSupportedVideoFormats.push_back(tFormat);

        if (mMediaSourceOpened)
        {
            tFormat.Name="Original";
            tFormat.ResX = mCodecContext->width;
            tFormat.ResY = mCodecContext->height;
            mSupportedVideoFormats.push_back(tFormat);
        }
    }

    return mSupportedVideoFormats;
}

bool MediaSourceFile::SupportsRecording()
{
    return true;
}

bool MediaSourceFile::SupportsSeeking()
{
    return true;
}

int64_t MediaSourceFile::GetSeekEnd()
{
    if (mFrameRate != 0)
        return (mDuration / ((int64_t)mFrameRate));
    else
        return 0;
}

bool MediaSourceFile::Seek(int64_t pSeconds, bool pOnlyKeyFrames)
{
    bool tResult = false;
    int64_t tSeekEnd = GetSeekEnd();

    if ((pSeconds < 0) || (pSeconds > tSeekEnd))
    {
        LOG(LOG_ERROR, "Seek position is out of range (%ld/%ld), media type is \"%s\"", pSeconds, tSeekEnd, GetMediaTypeStr().c_str());
        return false;
    }

    // lock grabbing
    mGrabMutex.lock();

    if (!mMediaSourceOpened)
    {
        // unlock grabbing
        mGrabMutex.unlock();

        //LOG(LOG_ERROR, "Tried to seek while source is closed, media type is \"%s\"", GetMediaTypeStr().c_str());
        return false;
    }

    int64_t tAbsoluteTimestamp = pSeconds * (int64_t)mFrameRate; // later the value for mCurPts will be changed to the same value like tAbsoluteTimestamp

    //LOG(LOG_VERBOSE, "Rel: %ld Abs: %ld", tRelativeTimestamp, tAbsoluteTimestamp);

    if ((tAbsoluteTimestamp >= 0) && (tAbsoluteTimestamp <= mDuration))
    {
        LOG(LOG_VERBOSE, "Seeking to second %ld, current pts is %ld", pSeconds, mCurPts);

        mDecoderMutex.lock();
        int64_t tMin = tAbsoluteTimestamp - MSF_SEEK_VARIANCE;
        int64_t tMax = tAbsoluteTimestamp + MSF_SEEK_VARIANCE;
        if (tMin < 0)
            tMin = 0;
        if (tMax > mDuration)
            tMax = mDuration;
        tResult = (avformat_seek_file(mFormatContext, mMediaStreamIndex, tMin, tAbsoluteTimestamp, tMax, (pOnlyKeyFrames ? 0 : AVSEEK_FLAG_ANY) | (tAbsoluteTimestamp < mCurPts ? AVSEEK_FLAG_BACKWARD : 0)) >= 0);

        mDecoderFifo->ClearFifo();
        mDecoderMetaDataFifo->ClearFifo();
        DecoderNeedWorkCondition.SignalAll();
        mDecoderMutex.unlock();

        // adopt the stored pts value which represent the start of the media presentation in real-time useconds
        int64_t tFrameNumber = tAbsoluteTimestamp - mSourceStartPts;
        int64_t tRelativeRealTimeUSecs = 1000000 * tFrameNumber / (int64_t)mFrameRate;
        //LOG(LOG_VERBOSE, "Old start: %ld", mStartPtsUSecs);
        mStartPtsUSecs = av_gettime() - tRelativeRealTimeUSecs;
        //LOG(LOG_VERBOSE, "New start: %ld", mStartPtsUSecs);

        if (tResult < 0)
            LOG(LOG_ERROR, "Error during absolute seeking in %s source file beause \"%s\"", GetMediaTypeStr().c_str(), strerror(AVUNERROR(tResult)));
        else
        {
            LOG(LOG_VERBOSE, "Seeking in file was successful");
            mCurPts = tAbsoluteTimestamp;
        }
    }else
        LOG(LOG_ERROR, "Seek position is out of range, media type is \"%s\"", GetMediaTypeStr().c_str());

    // inform about seeking state, don't inform about dropped frames because they are dropped caused by seeking and not by timing problems
    mSeekingToPos = true;

    // unlock grabbing
    mGrabMutex.unlock();

    return tResult;
}

bool MediaSourceFile::SeekRelative(int64_t pSeconds, bool pOnlyKeyFrames)
{
    bool tResult = false;

    // lock grabbing
    mGrabMutex.lock();

    if (!mMediaSourceOpened)
    {
        // unlock grabbing
        mGrabMutex.unlock();

        //LOG(LOG_ERROR, "Tried to seek while source is closed");
        return false;
    }

    //LOG(LOG_VERBOSE, "pSecs: %ld CurPts: %ld", pSeconds, mCurPts);
    int64_t tRelativeTimestamp = pSeconds * (int64_t)mFrameRate;
    int64_t tAbsoluteTimestamp = mCurPts + tRelativeTimestamp; // later the value for mCurPts will be changed to the same value like tAbsoluteTimestamp

    //LOG(LOG_VERBOSE, "Rel: %ld Abs: %ld", tRelativeTimestamp, tAbsoluteTimestamp);

    if ((tAbsoluteTimestamp >= 0) && (tAbsoluteTimestamp <= mDuration))
    {
        LOG(LOG_VERBOSE, "Seeking relative %d seconds", pSeconds);
        mDecoderMutex.lock();
        tResult = (avformat_seek_file(mFormatContext, mMediaStreamIndex, tAbsoluteTimestamp - MSF_SEEK_VARIANCE, tAbsoluteTimestamp, tAbsoluteTimestamp + MSF_SEEK_VARIANCE, (pOnlyKeyFrames ? 0 : AVSEEK_FLAG_ANY) | (tAbsoluteTimestamp < mCurPts ? AVSEEK_FLAG_BACKWARD : 0)) >= 0);
        mDecoderFifo->ClearFifo();
        mDecoderMetaDataFifo->ClearFifo();
        DecoderNeedWorkCondition.SignalAll();
        mDecoderMutex.unlock();

        // adopt the stored pts value which represent the start of the media presentation in real-time useconds
        int64_t tFrameNumber = tAbsoluteTimestamp - mSourceStartPts;
        int64_t tRelativeRealTimeUSecs = 1000000 * tFrameNumber / (int64_t)mFrameRate;
        //LOG(LOG_VERBOSE, "Old start: %ld", mStartPtsUSecs);
        mStartPtsUSecs = av_gettime() - tRelativeRealTimeUSecs;
        //LOG(LOG_VERBOSE, "New start: %ld", mStartPtsUSecs);

        if (tResult < 0)
            LOG(LOG_ERROR, "Error during relative seeking in source file, media type is \"%s\"", GetMediaTypeStr().c_str());
        else
            mCurPts = tAbsoluteTimestamp;
    }else
        LOG(LOG_ERROR, "Seek position is out of range, media type is \"%s\"", GetMediaTypeStr().c_str());

    // inform about seeking state, don't inform about dropped frames because they are dropped caused by seeking and not by timing problems
    mSeekingToPos = true;

    // unlock grabbing
    mGrabMutex.unlock();

    return tResult;
}

int64_t MediaSourceFile::GetSeekPos()
{
    if (mFrameRate != 0)
        return (mCurPts / ((int64_t)mFrameRate));
    else
        return 0;
}

bool MediaSourceFile::SupportsMultipleInputChannels()
{
    if(mMediaType == MEDIA_AUDIO)
        return true;
    else
        return false;
}

bool MediaSourceFile::SelectInputChannel(int pIndex)
{
    bool tResult = false;

    LOG(LOG_VERBOSE, "Selecting input channel: %d", pIndex);

    if (mCurrentInputChannel != pIndex)
        tResult = true;

    mDesiredInputChannel = pIndex;

    if (tResult)
    {
        int64_t tCurPos = GetSeekPos();
        tResult &= Reset();
        Seek(tCurPos, false);
    }

    return tResult;
}

list<string> MediaSourceFile::GetInputChannels()
{
    list<string> tResult;

    // lock grabbing
    mGrabMutex.lock();

    if(mMediaSourceOpened)
    {
        tResult = mInputChannels;
    }

    // unlock grabbing
    mGrabMutex.unlock();

    return tResult;
}

string MediaSourceFile::CurrentInputChannel()
{
    AVCodec *tCodec;
    string tResult = "";
    list<string>::iterator tIt;

    int tCount = 0;

    // lock grabbing
    mGrabMutex.lock();

    if(mMediaSourceOpened)
    {
        for (tIt = mInputChannels.begin(); tIt != mInputChannels.end(); tIt++)
        {
            if (tCount == mCurrentInputChannel)
            {
                tResult = (*tIt);
                break;
            }
            tCount++;
        }
    }

    // unlock grabbing
    mGrabMutex.unlock();

    return tResult;
}

///////////////////////////////////////////////////////////////////////////////

}} //namespace
