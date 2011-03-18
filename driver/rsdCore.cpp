/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "rsdCore.h"
#include "rsdBcc.h"
#include "rsdProgramStore.h"

#include <malloc.h>
#include "rsContext.h"

#include <sys/types.h>
#include <sys/resource.h>
#include <sched.h>
#include <cutils/properties.h>
#include <cutils/sched_policy.h>
#include <sys/syscall.h>
#include <string.h>

using namespace android;
using namespace android::renderscript;

static void Shutdown(Context *rsc);
static void SetPriority(const Context *rsc, int32_t priority);

static RsdHalFunctions FunctionTable = {
    Shutdown,
    NULL,
    SetPriority,
    {
        rsdScriptInit,
        rsdScriptInvokeFunction,
        rsdScriptInvokeRoot,
        rsdScriptInvokeForEach,
        rsdScriptInvokeInit,
        rsdScriptSetGlobalVar,
        rsdScriptSetGlobalBind,
        rsdScriptSetGlobalObj,
        rsdScriptDestroy
    },


    {
        rsdProgramStoreInit,
        rsdProgramStoreSetActive,
        rsdProgramStoreDestroy
    }

};



static void * HelperThreadProc(void *vrsc) {
    Context *rsc = static_cast<Context *>(vrsc);
    RsHal *dc = (RsHal *)rsc->mHal.drv;


    uint32_t idx = (uint32_t)android_atomic_inc(&dc->mWorkers.mLaunchCount);

    //LOGV("RS helperThread starting %p idx=%i", rsc, idx);

    dc->mWorkers.mLaunchSignals[idx].init();
    dc->mWorkers.mNativeThreadId[idx] = gettid();

#if 0
    typedef struct {uint64_t bits[1024 / 64]; } cpu_set_t;
    cpu_set_t cpuset;
    memset(&cpuset, 0, sizeof(cpuset));
    cpuset.bits[idx / 64] |= 1ULL << (idx % 64);
    int ret = syscall(241, rsc->mWorkers.mNativeThreadId[idx],
              sizeof(cpuset), &cpuset);
    LOGE("SETAFFINITY ret = %i %s", ret, EGLUtils::strerror(ret));
#endif

    int status = pthread_setspecific(rsc->gThreadTLSKey, rsc->mTlsStruct);
    if (status) {
        LOGE("pthread_setspecific %i", status);
    }

    while (!dc->mExit) {
        dc->mWorkers.mLaunchSignals[idx].wait();
        if (dc->mWorkers.mLaunchCallback) {
           dc->mWorkers.mLaunchCallback(dc->mWorkers.mLaunchData, idx);
        }
        android_atomic_dec(&dc->mWorkers.mRunningCount);
        dc->mWorkers.mCompleteSignal.set();
    }

    //LOGV("RS helperThread exited %p idx=%i", rsc, idx);
    return NULL;
}

void rsdLaunchThreads(Context *rsc, WorkerCallback_t cbk, void *data) {
    RsHal *dc = (RsHal *)rsc->mHal.drv;

    dc->mWorkers.mLaunchData = data;
    dc->mWorkers.mLaunchCallback = cbk;
    android_atomic_release_store(dc->mWorkers.mCount, &dc->mWorkers.mRunningCount);
    for (uint32_t ct = 0; ct < dc->mWorkers.mCount; ct++) {
        dc->mWorkers.mLaunchSignals[ct].set();
    }
    while (android_atomic_acquire_load(&dc->mWorkers.mRunningCount) != 0) {
        dc->mWorkers.mCompleteSignal.wait();
    }
}

bool rsdHalInit(Context *rsc, uint32_t version_major, uint32_t version_minor) {
    rsc->mHal.funcs = FunctionTable;

    RsHal *dc = (RsHal *)calloc(1, sizeof(RsHal));
    if (!dc) {
        LOGE("Calloc for driver hal failed.");
        return false;
    }
    rsc->mHal.drv = dc;


    int cpu = sysconf(_SC_NPROCESSORS_ONLN);
    LOGV("RS Launching thread(s), reported CPU count %i", cpu);
    if (cpu < 2) cpu = 0;

    dc->mWorkers.mCount = (uint32_t)cpu;
    dc->mWorkers.mThreadId = (pthread_t *) calloc(dc->mWorkers.mCount, sizeof(pthread_t));
    dc->mWorkers.mNativeThreadId = (pid_t *) calloc(dc->mWorkers.mCount, sizeof(pid_t));
    dc->mWorkers.mLaunchSignals = new Signal[dc->mWorkers.mCount];
    dc->mWorkers.mLaunchCallback = NULL;

    dc->mWorkers.mCompleteSignal.init();

    android_atomic_release_store(dc->mWorkers.mCount, &dc->mWorkers.mRunningCount);
    android_atomic_release_store(0, &dc->mWorkers.mLaunchCount);

    int status;
    pthread_attr_t threadAttr;
    status = pthread_attr_init(&threadAttr);
    if (status) {
        LOGE("Failed to init thread attribute.");
        return false;
    }

    for (uint32_t ct=0; ct < dc->mWorkers.mCount; ct++) {
        status = pthread_create(&dc->mWorkers.mThreadId[ct], &threadAttr, HelperThreadProc, rsc);
        if (status) {
            dc->mWorkers.mCount = ct;
            LOGE("Created fewer than expected number of RS threads.");
            break;
        }
    }
    while (android_atomic_acquire_load(&dc->mWorkers.mRunningCount) != 0) {
        usleep(100);
    }

    pthread_attr_destroy(&threadAttr);
    return true;
}


void SetPriority(const Context *rsc, int32_t priority) {
    RsHal *dc = (RsHal *)rsc->mHal.drv;
    for (uint32_t ct=0; ct < dc->mWorkers.mCount; ct++) {
        setpriority(PRIO_PROCESS, dc->mWorkers.mNativeThreadId[ct], priority);
    }
}

void Shutdown(Context *rsc) {
    RsHal *dc = (RsHal *)rsc->mHal.drv;

    dc->mExit = true;
    dc->mWorkers.mLaunchData = NULL;
    dc->mWorkers.mLaunchCallback = NULL;
    android_atomic_release_store(dc->mWorkers.mCount, &dc->mWorkers.mRunningCount);
    for (uint32_t ct = 0; ct < dc->mWorkers.mCount; ct++) {
        dc->mWorkers.mLaunchSignals[ct].set();
    }
    int status;
    void *res;
    for (uint32_t ct = 0; ct < dc->mWorkers.mCount; ct++) {
        status = pthread_join(dc->mWorkers.mThreadId[ct], &res);
    }
    rsAssert(android_atomic_acquire_load(&dc->mWorkers.mRunningCount) == 0);
}

