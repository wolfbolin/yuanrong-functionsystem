/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "actor/actorthread.hpp"
#include <atomic>

namespace litebus {
constexpr int MAXTHREADNAMELEN = 12;
ActorThread::ActorThread() : readyActors(), workers()
{
    readyActors.clear();
    workers.clear();

    char *envThreadName = getenv("LITEBUS_THREAD_NAME");
    if (envThreadName != nullptr) {
        threadName = envThreadName;
        if (threadName.size() > MAXTHREADNAMELEN) {
            threadName.resize(MAXTHREADNAMELEN);
        }
    } else {
        threadName = "HARES_LB_ACT";
    }
}

ActorThread::~ActorThread()
{
}
void ActorThread::AddThread(int threadCount)
{
    for (int i = 0; i < threadCount; ++i) {
        std::unique_ptr<std::thread> worker(new (std::nothrow) std::thread(&ActorThread::Run, this));
        BUS_OOM_EXIT(worker);
        workers.push_back(std::move(worker));
    }
}
void ActorThread::Finalize()
{
    BUSLOG_INFO("Actor's threads are exiting.");
    // terminate all thread; enque nullptr actor to terminate;
    std::shared_ptr<ActorBase> exitActor(nullptr);
    for (auto it = workers.begin(); it != workers.end(); ++it) {
        EnqueReadyActor(exitActor);
    }
    // wait all thread to exit
    for (auto it = workers.begin(); it != workers.end(); ++it) {
        std::unique_ptr<std::thread> &worker = *it;
        try {
            if (worker->joinable()) {
                worker->join();
            }
        } catch (const std::system_error &e) {
            BUSLOG_ERROR("thread Caught system_error e.code:{},e.what:{}", e.code().value(), e.what());
        }
    }
    workers.clear();
    BUSLOG_INFO("Actor's threads finish exiting.");
}

void ActorThread::DequeReadyActor(std::shared_ptr<ActorBase> &actor)
{
    std::unique_lock<std::mutex> lock(readyActorMutex);
    conditionVar.wait(lock, [this] { return (this->readyActors.size() > 0); });
    actor = readyActors.front();
    readyActors.pop_front();
}

void ActorThread::EnqueReadyActor(const std::shared_ptr<ActorBase> &actor)
{
    {
        std::lock_guard<std::mutex> lock(readyActorMutex);
        readyActors.push_back(actor);
    }
    conditionVar.notify_one();
}

void ActorThread::Run()
{
#if __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 12
    static std::atomic<int> actorCount(1);
    int ret = pthread_setname_np(pthread_self(), (threadName + std::to_string(actorCount.fetch_add(1))).c_str());
    if (0 != ret) {
        BUSLOG_INFO("set pthread name fail,ret:{}", ret);
    } else {
        BUSLOG_INFO("set pthread name success, threadID:{}", pthread_self());
    }
#endif

    bool terminate = false;
    do {
        std::shared_ptr<ActorBase> actor;
        DequeReadyActor(actor);
        if (actor != nullptr) {
            try {
                actor->Run();
            } catch (const std::exception &e) {
                BUSLOG_ERROR("Will Exit:{},{}", actor->GetAID().Name(), e.what());
                actor->PrintMsgRecord();
                BUS_EXIT("litebus catch exception: actor=" + actor->GetAID().Name() + " , what= " + e.what());
            }
        } else {
            terminate = true;
            BUSLOG_DEBUG("Actor this Threads have finished exiting.");
        }
    } while (!terminate);
}

};    // end of namespace litebus
