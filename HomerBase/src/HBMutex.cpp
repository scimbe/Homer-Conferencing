/*****************************************************************************
 *
 * Copyright (C) 2010 Thomas Volkert <thomas@homer-conferencing.com>
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
 * Purpose: Implementation of os independent mutex handling
 * Author:  Thomas Volkert
 * Since:   2010-09-20
 */

#include <Logger.h>
#include <HBMutex.h>
#include <HBThread.h>

namespace Homer { namespace Base {

using namespace std;

///////////////////////////////////////////////////////////////////////////////

Mutex::Mutex()
{
    mOwnerThreadId = -1;
    bool tResult = false;
    #if defined(LINUX) || defined(APPLE) || defined(BSD)
		tResult = (pthread_mutex_init(&mMutex, NULL) == 0);
	#endif
	#if defined(WIN32) ||defined(WIN64)
		mMutex = CreateMutex(NULL, false, NULL);
		tResult = (mMutex != NULL);
	#endif
	if (!tResult)
        LOG(LOG_ERROR, "Initiation of mutex failed");
}

Mutex::~Mutex()
{
    bool tResult = false;
    #if defined(LINUX) || defined(APPLE) || defined(BSD)
		tResult = (pthread_mutex_destroy(&mMutex) == 0);
	#endif
	#if defined(WIN32) ||defined(WIN64)
	    tResult = (CloseHandle(mMutex) != 0);
	#endif
    if (!tResult)
		LOG(LOG_ERROR, "Destruction of mutex failed");
}

///////////////////////////////////////////////////////////////////////////////

bool Mutex::lock()
{
    int tThreadId = Thread::GetTId();

    if ((mOwnerThreadId != -1) && (mOwnerThreadId == tThreadId))
        LOG(LOG_ERROR, "Recursive locking in thread %d detected", tThreadId);

    mOwnerThreadId = tThreadId;

    #if defined(LINUX) || defined(APPLE) || defined(BSD)
		return !pthread_mutex_lock(&mMutex);
	#endif
	#if defined(WIN32) ||defined(WIN64)
		return (WaitForSingleObject(mMutex, INFINITE) != WAIT_FAILED);
	#endif
}

bool Mutex::unlock()
{
    mOwnerThreadId = -1;

    #if defined(LINUX) || defined(APPLE) || defined(BSD)
		return !pthread_mutex_unlock(&mMutex);
	#endif
	#if defined(WIN32) ||defined(WIN64)
		return (ReleaseMutex(mMutex) != 0);
	#endif
}

bool Mutex::tryLock()
{
	bool tResult = false;
    #if defined(LINUX) || defined(APPLE) || defined(BSD)
		switch(int tRes = pthread_mutex_trylock(&mMutex))
		{
			case EDEADLK:
				LOG(LOG_ERROR, "Lock already held by calling thread");
				break;
			case EBUSY: // lock can't be obtained because it is busy
				break;
			case EINVAL:
				LOG(LOG_ERROR, "Lock was found in uninitialized state");
				break;
			case EFAULT:
				LOG(LOG_ERROR, "Invalid lock pointer was given");
				break;
			case 0: // lock was free and is obtained now
				tResult = true;
				break;
			default:
				LOG(LOG_ERROR, "Error occurred while trying to get lock: code %d", tRes);
				break;
		}
	#endif
	#if defined(WIN32) ||defined(WIN64)
		switch(WaitForSingleObject(mMutex, (DWORD)0))
		{
			case WAIT_ABANDONED:
			case WAIT_TIMEOUT:
			case WAIT_FAILED:
				break;
			case WAIT_OBJECT_0:
				tResult = true;
				break;
		}
	#endif
	return tResult;
}


bool Mutex::tryLock(int pMSecs)
{
	bool tResult = false;
    #if defined(APPLE) || defined(BSD)
	    // OSX/BSD don't support pthread_mutex_timedlock() and clock_gettime(), fall back to simple pthread_mutex_trylock()
        switch(pthread_mutex_trylock(&mMutex))
        {
            case EDEADLK:
                LOG(LOG_ERROR, "Lock already held by calling thread");
                break;
            case EBUSY: // lock can't be obtained because it is busy
                break;
            case EINVAL:
                LOG(LOG_ERROR, "Lock was found in uninitialized state");
                break;
            case EFAULT:
                LOG(LOG_ERROR, "Invalid lock pointer was given");
                break;
            case 0: // lock was free and is obtained now
                tResult = true;
                break;
            default:
                LOG(LOG_ERROR, "Error occurred while trying to get lock: code %d", tResult);
                break;
        }
    #endif
	#ifdef LINUX
		struct timespec tTimeout;

		if (clock_gettime(CLOCK_REALTIME, &tTimeout) == -1)
			LOG(LOG_ERROR, "Failed to get time from clock");

		// add msecs to current time stamp
		tTimeout.tv_nsec += pMSecs * 1000 * 1000;
		long int tSecAdd = (tTimeout.tv_nsec / (1000 * 1000 * 1000));
		tTimeout.tv_sec += tSecAdd;
		tTimeout.tv_nsec %= 1000 * 1000 * 1000;
        #ifdef HB_DEBUG_MUTEX
            LOG(LOG_VERBOSE, "Current    time: %8ld:%09ld", tTimeout.tv_sec, tTimeout.tv_nsec);
            LOG(LOG_VERBOSE, "      wait time: %14d", pMSecs);
            LOG(LOG_VERBOSE, "       add sec.: %10d", tSecAdd);
            LOG(LOG_VERBOSE, "Locks stop time: %8ld:%09ld", tTimeout.tv_sec, tTimeout.tv_nsec);
        #endif
		switch(pthread_mutex_timedlock(&mMutex, &tTimeout))
		{
			case EDEADLK:
                #ifdef HB_DEBUG_MUTEX
                    printf("Lock already held by calling thread.\n");
                #endif
				break;
			case EBUSY: // lock can't be obtained because it is busy
				break;
			case EINVAL:
				LOG(LOG_ERROR, "Lock was found in uninitialized state or timeout is invalid");
				break;
			case EFAULT:
				LOG(LOG_ERROR, "Invalid lock pointer was given");
				break;
			case ETIMEDOUT:
                #ifdef HB_DEBUG_MUTEX
				    printf("Lock couldn't be obtained in given time.\n");
                #endif
				break;
			case 0: // lock was free and is obtained now
				tResult = true;
				break;
			default:
				LOG(LOG_ERROR, "Error occurred while trying to get lock: code %d", tResult);
				break;
		}
	#endif
	#if defined(WIN32) ||defined(WIN64)
		switch(WaitForSingleObject(mMutex, (DWORD)pMSecs))
		{
			case WAIT_ABANDONED:
			case WAIT_TIMEOUT:
			case WAIT_FAILED:
				break;
			case WAIT_OBJECT_0:
				tResult = true;
				break;
		}
	#endif
	return tResult;
}

///////////////////////////////////////////////////////////////////////////////

}} //namespace
