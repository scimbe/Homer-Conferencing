/*****************************************************************************
 *
 * Copyright (C) 2011 Thomas Volkert <thomas@homer-conferencing.com>
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
 * Purpose: Cep
 * Author:  Thomas Volkert
 * Since:   2012-05-30
 */

#ifndef _GAPI_SIMULATION_CEP_
#define _GAPI_SIMULATION_CEP_

#include <HBSocket.h>
#include <HBSocketQoSSettings.h>
#include <Name.h>

#include <MediaFifo.h>
#include <MediaSourceMem.h>

#include <list>
#include <string>

namespace Homer { namespace Base {

///////////////////////////////////////////////////////////////////////////////
#define CEP_QUEUE_SIZE              256
#define CEP_QUEUE_ENTRY_SIZE        (MEDIA_SOURCE_MEM_FRAGMENT_BUFFER_SIZE)
///////////////////////////////////////////////////////////////////////////////

class Node;

class Cep;
typedef std::list<Cep*> CepList;

struct Packet{
    std::string     Source;
    std::string     Destination;
    unsigned int    SourcePort;
    unsigned int    DestinationPort;
    QoSSettings     QoSRequirements;
    void            *Data;
    ssize_t         DataSize;
};

///////////////////////////////////////////////////////////////////////////////

class Cep
{
public:
    /* server */
    Cep(Node *pNode, enum TransportType pTransportType, unsigned int pLocalPort);

    /* client */
    Cep(Node *pNode, enum TransportType pTransportType, std::string pTarget, unsigned int pTargetPort);

    virtual ~Cep();

    /* QoS management */
    bool SetQoS(const QoSSettings &pQoSSettings);

    /* transfer */
    bool Send(std::string pTargetNode, unsigned int pTargetPort, void *pBuffer, ssize_t pBufferSize);
    bool Receive(std::string &pPeerNode, unsigned int &pPeerPort, void *pBuffer, ssize_t &pBufferSize);
    bool Close();
    enum TransportType GetTransportType();
    unsigned int GetLocalPort();
    std::string GetLocalNode();
    unsigned int GetPeerPort();
    std::string GetPeerNode();

    bool HandlePacket(Packet *pPacket);

private:
    bool                mClosed;
    unsigned int        mLocalPort;
    Node                *mNode;
    std::string         mPeerNode;
    unsigned int        mPeerPort;
    enum TransportType  mTransportType;
    Homer::Multimedia::MediaFifo *mPacketQueue;
    /* QoS parameter */
    QoSSettings         mQoSSettings;
};

///////////////////////////////////////////////////////////////////////////////

}} // namespaces

#endif
