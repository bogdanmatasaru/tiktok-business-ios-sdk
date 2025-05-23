//
//  TTSDKThread.c
//
//  Created by Karl Stenerud on 2012-01-29.
//
//  Copyright (c) 2012 Karl Stenerud. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall remain in place
// in this source code.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "TTSDKThread.h"

#include "TTSDKMemory.h"
#include "TTSDKSystemCapabilities.h"

// #define TTSDKLogger_LocalLevel TRACE
#include <dispatch/dispatch.h>
#include <mach/mach.h>
#include <pthread.h>
#include <sys/sysctl.h>

#include "TTSDKLogger.h"

TTSDKThread ttsdkthread_self(void)
{
    thread_t thread_self = mach_thread_self();
    mach_port_deallocate(mach_task_self(), thread_self);
    return (TTSDKThread)thread_self;
}

bool ttsdkthread_getThreadName(const TTSDKThread thread, char *const buffer, int bufLength)
{
    // WARNING: This implementation is no longer async-safe!

    const pthread_t pthread = pthread_from_mach_thread_np((thread_t)thread);
    return pthread_getname_np(pthread, buffer, (unsigned)bufLength) == 0;
}

bool ttsdkthread_getQueueName(const TTSDKThread thread, char *const buffer, int bufLength)
{
    // WARNING: This implementation is no longer async-safe!

    integer_t infoBuffer[THREAD_IDENTIFIER_INFO_COUNT] = { 0 };
    thread_info_t info = infoBuffer;
    mach_msg_type_number_t inOutSize = THREAD_IDENTIFIER_INFO_COUNT;
    kern_return_t kr = 0;

    kr = thread_info((thread_t)thread, THREAD_IDENTIFIER_INFO, info, &inOutSize);
    if (kr != KERN_SUCCESS) {
        TTSDKLOG_TRACE("Error getting thread_info with flavor THREAD_IDENTIFIER_INFO from mach thread : %s",
                    mach_error_string(kr));
        return false;
    }

    thread_identifier_info_t idInfo = (thread_identifier_info_t)info;
    if (!ttsdkmem_isMemoryReadable(idInfo, sizeof(*idInfo))) {
        TTSDKLOG_DEBUG("Thread %p has an invalid thread identifier info %p", thread, idInfo);
        return false;
    }
    dispatch_queue_t *dispatch_queue_ptr = (dispatch_queue_t *)idInfo->dispatch_qaddr;
    if (!ttsdkmem_isMemoryReadable(dispatch_queue_ptr, sizeof(*dispatch_queue_ptr))) {
        TTSDKLOG_DEBUG("Thread %p has an invalid dispatch queue pointer %p", thread, dispatch_queue_ptr);
        return false;
    }
    // thread_handle shouldn't be 0 also, because
    // identifier_info->dispatch_qaddr =  identifier_info->thread_handle +
    // get_dispatchqueue_offset_from_proc(thread->task->bsd_info);
    if (dispatch_queue_ptr == NULL || idInfo->thread_handle == 0 || *dispatch_queue_ptr == NULL) {
        TTSDKLOG_TRACE("This thread doesn't have a dispatch queue attached : %p", thread);
        return false;
    }

    dispatch_queue_t dispatch_queue = *dispatch_queue_ptr;
    const char *queue_name = dispatch_queue_get_label(dispatch_queue);
    if (queue_name == NULL) {
        TTSDKLOG_TRACE("Error while getting dispatch queue name : %p", dispatch_queue);
        return false;
    }
    TTSDKLOG_TRACE("Dispatch queue name: %s", queue_name);
    int length = (int)strlen(queue_name);

    // Queue label must be a null terminated string.
    int iLabel;
    for (iLabel = 0; iLabel < length + 1; iLabel++) {
        if (queue_name[iLabel] < ' ' || queue_name[iLabel] > '~') {
            break;
        }
    }
    if (queue_name[iLabel] != 0) {
        // Found a non-null, invalid char.
        TTSDKLOG_TRACE("Queue label contains invalid chars");
        return false;
    }
    bufLength = MIN(length, bufLength - 1);  // just strlen, without null-terminator
    strncpy(buffer, queue_name, bufLength);
    buffer[bufLength] = 0;  // terminate string
    TTSDKLOG_TRACE("Queue label = %s", buffer);
    return true;
}
