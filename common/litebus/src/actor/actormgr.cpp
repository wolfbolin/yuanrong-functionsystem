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

#include "actor/actormgr.hpp"
#include "actor/actorpolicy.hpp"
#include "actor/iomgr.hpp"

namespace litebus {

std::shared_ptr<ActorMgr> ActorMgr::actorMgr = std::make_shared<ActorMgr>();
std::map<std::string, std::shared_ptr<IOMgr>> ActorMgr::ioMgrs;

std::shared_ptr<IOMgr> &ActorMgr::GetIOMgrRef(const std::string &protocol)
{
    auto it = ioMgrs.find(protocol);
    if (it != ioMgrs.end()) {
        return it->second;
    } else {
        BUSLOG_DEBUG("Can't find IOMgr of protocol:{}", protocol);
        static std::shared_ptr<IOMgr> nullIOMgr = nullptr;
        return nullIOMgr;
    }
}
ActorMgr::ActorMgr() : actors(), procotols(), urls()
{
    actors.clear();
    procotols.clear();
    urls.clear();
}

ActorMgr::~ActorMgr()
{
}

const std::string ActorMgr::GetUrl(const std::string &protocol)
{
    auto it = procotols.find(protocol);
    if (it != procotols.end()) {
        return it->second;
    } else if (procotols.size() > 0) {
        return procotols.begin()->second;
    } else {
        return "";
    }
}

void ActorMgr::AddUrl(const std::string &protocol, const std::string &url)
{
    procotols[protocol] = url;
    AID id("a@" + url);
    (void)urls.insert(id.GetIp() + ":" + std::to_string(id.GetPort()));
    (void)urls.insert(id.GetProtocol() + "://" + id.GetIp() + ":" + std::to_string(id.GetPort()));
    (void)urls.insert(std::string("127.0.0.1:") + std::to_string(id.GetPort()));
    (void)urls.insert(protocol + "://127.0.0.1:" + std::to_string(id.GetPort()));
}

void ActorMgr::AddIOMgr(const std::string &protocol, const std::shared_ptr<IOMgr> &ioMgr)
{
    ioMgrs[protocol] = ioMgr;
}

void ActorMgr::RemoveActor(const std::string &name)
{
    BUSLOG_DEBUG("ACTOR was terminated with aid={}", name);
    actorsMutex.lock();
    (void)actors.erase(name);
    actorsMutex.unlock();
}

void ActorMgr::TerminateAll()
{
    // copy all the actors
    std::list<ActorReference> actorsWaiting;
    actorsMutex.lock();
    for (auto actorIt = actors.begin(); actorIt != actors.end(); ++actorIt) {
        actorsWaiting.push_back(actorIt->second);
    }
    actorsMutex.unlock();

    // send terminal msg to all actors.
    for (auto actorIt = actorsWaiting.begin(); actorIt != actorsWaiting.end(); ++actorIt) {
        std::unique_ptr<MessageBase> msg(new (std::nothrow) MessageBase("Terminate", MessageBase::Type::KTERMINATE));
        BUS_OOM_EXIT(msg);
        (void)(*actorIt)->EnqueMessage(std::move(msg));
        (*actorIt)->SetRunningStatus(true);
    }

    // wait actor's thread to finish.
    for (auto actorIt = actorsWaiting.begin(); actorIt != actorsWaiting.end(); ++actorIt) {
        (*actorIt)->Await();
    }
}

void ActorMgr::Initialize(int threadCount)
{
    threadPool.AddThread(threadCount);
}

void ActorMgr::Finalize()
{
    this->TerminateAll();
    BUSLOG_INFO("litebus Actors finish exiting.");
    // stop all actor threads;
    threadPool.Finalize();
    BUSLOG_INFO("litebus Threads finish exiting.");
    // stop iomgr thread
    for (auto mgrIt = ioMgrs.begin(); mgrIt != ioMgrs.end(); ++mgrIt) {
        BUSLOG_INFO("finalize IOMgr={}", mgrIt->first);
        mgrIt->second->Finish();
    }

    BUSLOG_INFO("litebus IOMGRS finish exiting.");
}

ActorReference ActorMgr::GetActor(const AID &id)
{
    ActorReference result;
    actorsMutex.lock();
    auto actorIt = actors.find(id.Name());
    if (actorIt != actors.end()) {
        result = actorIt->second;
    } else {
        result = nullptr;
    }
    // find the
    actorsMutex.unlock();
    return result;
}
int ActorMgr::Send(const AID &to, std::unique_ptr<MessageBase> msg, bool remoteLink, bool isExactNotRemote)
{
    // The destination is local
    if (IsLocalAddres(to)) {
        auto actor = GetActor(to);
        if (actor != nullptr) {
            if (to.GetProtocol() == BUS_UDP && msg->GetType() == MessageBase::Type::KMSG) {
                msg->type = MessageBase::Type::KUDP;
            }
            return actor->EnqueMessage(std::move(msg));
        } else {
            return ACTOR_NOT_FIND;
        }
    } else {
        // send to remote actor
        if (msg->GetType() != MessageBase::Type::KMSG) {
            BUSLOG_ERROR("The msg is not KMSG,it can't send to remote={}", std::string(to));
            return ACTOR_PARAMER_ERR;
        } else {
            // null
        }
        msg->SetTo(to);
        auto io = ActorMgr::GetIOMgrRef(to);
        if (io != nullptr) {
            return io->Send(std::move(msg), remoteLink, isExactNotRemote);
        } else {
            BUSLOG_ERROR("The protocol is not supported:p={},f={},t={},m={}", to.GetProtocol(), msg->From().Name(),
                         to.Name(), msg->Name());
            return IO_NOT_FIND;
        }
    }
}

AID ActorMgr::Spawn(ActorReference &actor, bool shareThread, bool start)
{
    actorsMutex.lock();
    if (actors.find(actor->GetAID().Name()) != actors.end()) {
        actorsMutex.unlock();
        BUSLOG_ERROR("The actor's name conflicts,name:{}", actor->GetAID().Name());
        BUS_EXIT("Actor name conflicts.");
    }

    BUSLOG_DEBUG("ACTOR {} was spawned", actor->GetAID().Name());
    std::unique_ptr<ActorPolicy> threadPolicy;

    if (shareThread) {
        threadPolicy.reset(new (std::nothrow) ShardedThread(actor));
        BUS_OOM_EXIT(threadPolicy);
        actor->Spawn(actor, std::move(threadPolicy));
    } else {
        threadPolicy.reset(new (std::nothrow) SingleThread());
        BUS_OOM_EXIT(threadPolicy);
        actor->Spawn(actor, std::move(threadPolicy));
        ActorMgr::GetActorMgrRef()->SetActorReady(actor);
    }

    (void)this->actors.emplace(actor->GetAID().Name(), actor);
    actorsMutex.unlock();

    // long time
    actor->Init();

    actor->SetRunningStatus(start);

    return actor->GetAID();
}

void ActorMgr::Terminate(const AID &id)
{
    auto actor = GetActor(id);
    if (actor != nullptr) {
        std::unique_ptr<MessageBase> msg(new (std::nothrow) MessageBase("Terminate", MessageBase::Type::KTERMINATE));
        BUS_OOM_EXIT(msg);
        (void)actor->EnqueMessage(std::move(msg));
        actor->SetRunningStatus(true);
    }
}

void ActorMgr::SetActorStatus(const AID &pid, bool start)
{
    auto actor = GetActor(pid);
    if (actor != nullptr) {
        actor->SetRunningStatus(start);
    }
}

void ActorMgr::Wait(const AID &id)
{
    auto actor = GetActor(id);
    if (actor != nullptr) {
        actor->Await();
    }
}

};    // end of namespace litebus
