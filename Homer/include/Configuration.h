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
 * Purpose: global program settings
 * Author:  Thomas Volkert
 * Since:   2009-07-30
 */

#ifndef _CONFIGURATION_
#define _CONFIGURATION_

#include <MainWindow.h>
#include <Dialogs/IdentityDialog.h>
#include <Dialogs/ConfigurationDialog.h>
#include <Dialogs/UpdateCheckDialog.h>
#include <Dialogs/OpenVideoAudioPreviewDialog.h>
#include <Widgets/VideoWidget.h>
#include <Widgets/StreamingControlWidget.h>
#include <Widgets/OverviewContactsWidget.h>
#include <Widgets/OverviewDataStreamsWidget.h>
#include <Widgets/OverviewErrorsWidget.h>
#include <Widgets/OverviewFileTransfersWidget.h>
#include <Widgets/OverviewNetworkStreamsWidget.h>
#include <Widgets/OverviewThreadsWidget.h>
#include <Widgets/ParticipantWidget.h>
#include <Widgets/StreamingControlWidget.h>

#include <string>

#include <QSettings>

namespace Homer { namespace Gui {

///////////////////////////////////////////////////////////////////////////////

// select if it is a release or development version
//#define RELEASE_VERSION -> is now defined automatically via build system

// debug GUI timer?
//#define DEBUG_TIMING

// version string which is checked/used in the whole application
#define RELEASE_VERSION_STRING          "Beta 0.23"

// for home calls
#define RELEASE_SERVER                  "www.homer-conferencing.com"
#define PATH_VERSION_TXT                "/live/version.txt"
#define PATH_CHANGELOG_TXT              "/live/changelog.txt"
#define PATH_HELP_TXT                   "/live/help.txt"
#define PATH_STUN_SERVER_TXT            "/live/stun_server.txt"
#define PATH_SIP_SERVER_TXT             "/live/sip_server.txt"
#define PATH_HOMER_RELEASES				"http://"RELEASE_SERVER"/releases/"

#define CONF Configuration::GetInstance()

#define AUDIO_OUTPUT_SAMPLE_RATE        44100

///////////////////////////////////////////////////////////////////////////////
class Configuration
{
public:
    Configuration();

    virtual ~Configuration();

    static Configuration& GetInstance();

    void Init(string pAbsBinPath);

    /* general settings */
    QString GetBinaryPath();
    QString GetConferenceAvailability();
    QString GetContactFile();
    QString GetDataDirectory();

    QPoint GetMainWindowPosition();
    QSize GetMainWindowSize();

    bool GetVisibilityContactsWidget();
    bool GetVisibilityFileTransfersWidget();
    bool GetVisibilityErrorsWidget();

    bool GetVisibilityPlaylistWidgetAudio();
    bool GetVisibilityPlaylistWidgetVideo();
    bool GetVisibilityPlaylistWidgetMovie();

    bool GetVisibilityThreadsWidget();
    bool GetVisibilityNetworkStreamsWidget();
    bool GetVisibilityDataStreamsWidget();
    bool GetVisibilityBroadcastMessageWidget();
    bool GetVisibilityBroadcastWidget();

    bool GetVisibilityToolBarMediaSources();
    bool GetVisibilityToolBarOnlineStatus();

    bool GetParticipantWidgetsSeparation();
    bool GetParticipantWidgetsCloseImmediately();

    bool GetSmoothVideoPresentation();
    bool GetAutoUpdateCheck();

    /* user settings */
    QString GetUserName();
    QString GetUserMail();

    /* video settings */
    bool GetVideoActivation();
    bool GetVideoRtp();
    QString GetVideoCodec();
    int GetVideoQuality();
    int GetVideoMaxPacketSize();
    enum Homer::Base::TransportType GetVideoTransportType();
    QString GetVideoStreamingGAPIImpl();
    QString GetLocalVideoSource();
    int GetVideoFps();
    QString GetVideoResolution();
    bool GetLocalVideoSourceHFlip();
    bool GetLocalVideoSourceVFlip();

    /* audio settings */
    bool GetAudioActivation();
    bool GetAudioRtp();
    QString GetAudioCodec();
    int GetAudioQuality();
    int GetAudioMaxPacketSize();
    enum Homer::Base::TransportType GetAudioTransportType();
    QString GetAudioStreamingGAPIImpl();
    QString GetLocalAudioSource();

    /* playback settings */
    QString GetLocalAudioSink();

    /* network settings */
    int GetVideoAudioStartPort();
    QString GetSipUserName();
    QString GetSipPassword();
    QString GetSipServer();
    bool GetSipContactsProbing();
    int GetSipInfrastructureMode();
    bool GetNatSupportActivation();
    QString GetStunServer();
    /* SIP network listener */
    QString GetSipListenerAddress();
    enum Homer::Base::TransportType GetSipListenerTransport();
    int GetSipStartPort();

    /* notification settings */
    // instant message
    QString GetImSoundFile();
    bool GetImSound();
    bool GetImSystray();
    // call
    QString GetCallSoundFile();
    bool GetCallSound();
    bool GetCallSystray();
    // call acknowledge
    QString GetCallAcknowledgeSoundFile();
    bool GetCallAcknowledgeSound();
    bool GetCallAcknowledgeSystray();
    // call canceled
    QString GetCallDenySoundFile();
    bool GetCallDenySound();
    bool GetCallDenySystray();
    // call hangup
    QString GetCallHangupSoundFile();
    bool GetCallHangupSound();
    bool GetCallHangupSystray();
    // error
    QString GetErrorSoundFile();
    bool GetErrorSound();
    bool GetErrorSystray();
    // registration failed
    QString GetRegistrationFailedSoundFile();
    bool GetRegistrationFailedSound();
    bool GetRegistrationFailedSystray();
    // registration successful
    QString GetRegistrationSuccessfulSoundFile();
    bool GetRegistrationSuccessfulSound();
    bool GetRegistrationSuccessfulSystray();

    /* additional fixed values */
    int GetSystrayTimeout(){ return 6000; /* ms */ }
    int GetContactPresenceCheckPeriod(){ return 60*1000; /* 1 minute to allow NAT hole punching based on bidirectional SIP probe request ping-pong */ }

    /* debugging state machine */
    bool DebuggingEnabled();

private:
    friend class MainWindow;
    friend class ConfigurationDialog;
    friend class ContactsPool;
    friend class IdentityDialog;
    friend class UpdateCheckDialog;
    friend class AddNetworkSinkDialog;
    friend class MessageWidget;
    friend class OpenVideoAudioPreviewDialog;
    friend class OverviewContactsWidget;
    friend class OverviewErrorsWidget;
    friend class OverviewFileTransfersWidget;
    friend class OverviewNetworkStreamsWidget;
    friend class OverviewPlaylistWidget;
    friend class OverviewThreadsWidget;
    friend class OverviewDataStreamsWidget;
    friend class ParticipantWidget;
    friend class StreamingControlWidget;
    friend class VideoWidget;

    /* global settings */
    void SetConferenceAvailability(QString pState);
    void SetContactFile(QString pContactFile);
    void SetDataDirectory(QString pDataDirectory);
    void SetMainWindowPosition(QPoint pPos);
    void SetMainWindowSize(QSize pSize);
    void SetParticipantWidgetsSeparation(bool pSepWins);
    void SetParticipantWidgetsCloseImmediately(bool pActive);
    void SetVisibilityContactsWidget(bool pActive);
    void SetVisibilityErrorsWidget(bool pActive);
    void SetVisibilityFileTransfersWidget(bool pActive);

    void SetVisibilityPlaylistWidgetAudio(bool pActive);
    void SetVisibilityPlaylistWidgetVideo(bool pActive);
    void SetVisibilityPlaylistWidgetMovie(bool pActive);

    void SetVisibilityThreadsWidget(bool pActive);
    void SetVisibilityNetworkStreamsWidget(bool pActive);
    void SetVisibilityDataStreamsWidget(bool pActive);
    void SetVisibilityBroadcastMessageWidget(bool pActive);
    void SetVisibilityBroadcastWidget(bool pActive);

    void SetVisibilityToolBarMediaSources(bool pActive);
    void SetVisibilityToolBarOnlineStatus(bool pActive);

    void SetSmoothVideoPresentation(bool pActive);
    void SetAutoUpdateCheck(bool pActive);

    /* user settings */
    void SetUserName(QString pUserName);
    void SetUserMail(QString pUserMail);

    /* video settings */
    void SetVideoActivation(bool pActivation);
    void SetVideoRtp(bool pActivation);
    void SetVideoCodec(QString pCodec);
    void SetVideoQuality(int pQuality);
    void SetVideoMaxPacketSize(int pSize);
    void SetVideoTransport(enum Homer::Base::TransportType pType);
    void SetVideoStreamingGAPIImpl(QString pImpl);
    void SetVideoResolution(QString pResolution);
    void SetLocalVideoSource(QString pVSource);
    void SetVideoFps(int pFps);
    void SetLocalVideoSourceHFlip(bool pHFlip);
    void SetLocalVideoSourceVFlip(bool pVFlip);

    /* audio settings */
    void SetAudioActivation(bool pActivation);
    void SetAudioRtp(bool pActivation);
    void SetAudioCodec(QString pCodec);
    void SetAudioQuality(int pQuality);
    void SetAudioMaxPacketSize(int pSize);
    void SetAudioTransport(enum Homer::Base::TransportType pType);
    void SetAudioStreamingGAPIImpl(QString pImpl);
    void SetLocalAudioSource(QString pASource);

    /* playback settings */
    void SetLocalAudioSink(QString pASink);

    /* network settings */
    void SetVideoAudioStartPort(int pPort);
    void SetSipUserName(QString pUserName);
    void SetSipPassword(QString pPassword);
    void SetSipServer(QString pServer);
    void SetSipInfrastructureMode(int pMode);
    void SetSipContactsProbing(bool pActivation);
    void SetStunServer(QString pServer);
    void SetNatSupportActivation(bool pActivation);
    void SetSipListenerAddress(QString pAddress);
    void SetSipListenerTransport(enum Homer::Base::TransportType pType);
    void SetSipStartPort(int pPort);

    /* notification settings */
    // instant message
    void SetImSoundFile(QString pSoundFile);
    void SetImSound(bool pActivation);
    void SetImSystray(bool pActivation);
    // call
    void SetCallSoundFile(QString pSoundFile);
    void SetCallSound(bool pActivation);
    void SetCallSystray(bool pActivation);
    // call acknowledge
    void SetCallAcknowledgeSoundFile(QString pSoundFile);
    void SetCallAcknowledgeSound(bool pActivation);
    void SetCallAcknowledgeSystray(bool pActivation);
    // call canceled
    void SetCallDenySoundFile(QString pSoundFile);
    void SetCallDenySound(bool pActivation);
    void SetCallDenySystray(bool pActivation);
    // call hangup
    void SetCallHangupSoundFile(QString pSoundFile);
    void SetCallHangupSound(bool pActivation);
    void SetCallHangupSystray(bool pActivation);
    // error
    void SetErrorSoundFile(QString pSoundFile);
    void SetErrorSound(bool pActivation);
    void SetErrorSystray(bool pActivation);
    // registration failed
    void SetRegistrationFailedSoundFile(QString pSoundFile);
    void SetRegistrationFailedSound(bool pActivation);
    void SetRegistrationFailedSystray(bool pActivation);
    // registration successful
    void SetRegistrationSuccessfulSoundFile(QString pSoundFile);
    void SetRegistrationSuccessfulSound(bool pActivation);
    void SetRegistrationSuccessfulSystray(bool pActivation);

    /* debug state */
    void SetDebugging(bool pState = true);

    // important because some write operations might be delayed
    void Sync();

    std::string             mAbsBinPath;
    QSettings               *mQSettings;
    bool                    mDebuggingEnabled;
};

///////////////////////////////////////////////////////////////////////////////

}}

#endif
