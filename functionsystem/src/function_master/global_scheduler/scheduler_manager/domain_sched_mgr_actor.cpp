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

#include "domain_sched_mgr_actor.h"

#include <logs/api/provider.h>

#include <async/asyncafter.hpp>

#include "common/constants/actor_name.h"
#include "logs/logging.h"
#include "proto/pb/message_pb.h"
#include "status/status.h"
#include "common/utils/generate_message.h"
#include "param_check.h"

namespace functionsystem::global_scheduler {

constexpr int RETRY_BASE_INTERVAL = 1000;
constexpr int RETRY_LOW_BOUND = 2000;
constexpr int RETRY_MAX_BOUND = 4000;
constexpr int RETRY_MAX_TIMES = 5;

DomainSchedMgrActor::DomainSchedMgrActor(const std::string &name)
    : litebus::ActorBase(name), member_(std::make_shared<Member>())
{
    auto backOff = [lower(RETRY_LOW_BOUND), upper(RETRY_MAX_BOUND), base(RETRY_BASE_INTERVAL)](int64_t attempt) {
        return GenerateRandomNumber(base + lower * attempt, base + upper * attempt);
    };
    queryResourceHelper_.SetBackOffStrategy(backOff, RETRY_MAX_TIMES);
}

void DomainSchedMgrActor::Init()
{
    YRLOG_DEBUG("init DomainSchedMgrActor");
    ASSERT_IF_NULL(member_);
    auto masterBusiness = std::make_shared<MasterBusiness>(shared_from_this(), member_);
    auto slaveBusiness = std::make_shared<SlaveBusiness>(shared_from_this(), member_);

    (void)businesses_.emplace(MASTER_BUSINESS, masterBusiness);
    (void)businesses_.emplace(SLAVE_BUSINESS, slaveBusiness);

    curStatus_ = SLAVE_BUSINESS;
    business_ = slaveBusiness;

    Receive("Register", &DomainSchedMgrActor::Register);
    Receive("NotifySchedAbnormal", &DomainSchedMgrActor::NotifySchedAbnormal);
    Receive("NotifyWorkerStatus", &DomainSchedMgrActor::NotifyWorkerStatus);
    Receive("ResponseSchedule", &DomainSchedMgrActor::ResponseSchedule);
    Receive("ResponseQueryAgentInfo", &DomainSchedMgrActor::ResponseQueryAgentInfo);
    Receive("ResponseQueryResourcesInfo", &DomainSchedMgrActor::ResponseQueryResourcesInfo);
    Receive("ResponseGetSchedulingQueue", &DomainSchedMgrActor::ResponseGetSchedulingQueue);
}

void DomainSchedMgrActor::Register(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    ASSERT_IF_NULL(business_);
    business_->Register(from, std::move(name), std::move(msg));
}

void DomainSchedMgrActor::Registered(const litebus::AID &dst,
                                     const litebus::Option<messages::ScheduleTopology> &topology)
{
    if (topology.IsNone()) {
        YRLOG_ERROR("topology message is none, destination is {}", std::string(dst));
        SendRegisteredMessage(
            dst, GenRegistered(StatusCode::GS_REGISTERED_SCHEDULER_TOPOLOGY_IS_NONE, "topology message is none"));
        return;
    }

    SendRegisteredMessage(dst, GenRegistered(StatusCode::SUCCESS, "registered success", topology.Get()));
}

void DomainSchedMgrActor::NotifySchedAbnormal(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    ASSERT_IF_NULL(business_);
    business_->NotifySchedAbnormal(from, std::move(name), std::move(msg));
}

void DomainSchedMgrActor::NotifyWorkerStatus(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    ASSERT_IF_NULL(business_);
    business_->NotifyWorkerStatus(from, std::move(name), std::move(msg));
}

void DomainSchedMgrActor::UpdateSchedTopoView(const std::string &name, const std::string &address,
                                              const messages::ScheduleTopology &topology)
{
    std::string responseMsg;
    if (!topology.SerializeToString(&responseMsg)) {
        YRLOG_ERROR("response message is invalid from {}", std::string(address));
        return;
    }
    Send(litebus::AID(name + DOMAIN_SCHEDULER_SRV_ACTOR_NAME_POSTFIX, address), "UpdateSchedTopoView",
         std::move(responseMsg));
}

Status DomainSchedMgrActor::AddDomainSchedCallback(const functionsystem::global_scheduler::CallbackAddFunc &func)
{
    return SetCallback([func, &handler = this->addDomainSchedCallback_]() { handler = func; }, func);
}

Status DomainSchedMgrActor::DelDomainSchedCallback(const functionsystem::global_scheduler::CallbackDelFunc &func)
{
    return SetCallback([func, &handler = this->delDomainSchedCallback_]() { handler = func; }, func);
}

Status DomainSchedMgrActor::DelLocalSchedCallback(const functionsystem::global_scheduler::CallbackDelFunc &func)
{
    return SetCallback([func, &handler = this->delLocalSchedCallback_]() { handler = func; }, func);
}

Status DomainSchedMgrActor::NotifyWorkerStatusCallback(const CallbackWorkerFunc &func)
{
    return SetCallback([func, &handler = this->notifyWorkerStatusCallback_]() { handler = func; }, func);
}

Status DomainSchedMgrActor::Connect(const std::string &name, const std::string &address)
{
    // destroy old actor
    heartbeatObserveDriver_ = nullptr;
    heartbeatObserveDriver_ = std::make_unique<HeartbeatObserveDriver>(
        name, litebus::AID(name + "-PingPong", address),
        [&handler = this->delDomainSchedCallback_, name](const litebus::AID &) { handler(name, ""); });
    // create new domainSchedulerAid
    if (domainSchedulerAID_ == nullptr) {
        domainSchedulerAID_ = std::make_shared<litebus::AID>(name + DOMAIN_SCHEDULER_SRV_ACTOR_NAME_POSTFIX, address);
    } else {
        domainSchedulerAID_->SetUrl(address);
    }

    if (heartbeatObserveDriver_->Start() != 0) {
        YRLOG_ERROR("heartbeat to name: {}, in {} start failed", std::string(name), std::string(address));
        this->delDomainSchedCallback_(name, address);
        this->Disconnect();
        return Status(StatusCode::FAILED);
    }
    YRLOG_DEBUG("heartbeat start successfully");
    return Status::OK();
}

void DomainSchedMgrActor::Disconnect()
{
    heartbeatObserveDriver_ = nullptr;
}

litebus::Future<Status> DomainSchedMgrActor::Schedule(const std::string &name, const std::string &address,
                                                      const std::shared_ptr<messages::ScheduleRequest> &req,
                                                      const uint32_t retryCycle)
{
    if (req == nullptr) {
        YRLOG_ERROR("ScheduleRequest pointer is nullptr");
        return Status(StatusCode::FAILED);
    }

    if (req->requestid().empty()) {
        YRLOG_ERROR("request ID is empty");
        return Status(StatusCode::FAILED);
    }

    auto promise = std::make_shared<litebus::Promise<Status>>();
    SendScheduleRequest(name, address, req, retryCycle, promise);
    return promise->GetFuture();
}

litebus::Future<messages::QueryAgentInfoResponse> DomainSchedMgrActor::QueryAgentInfo(
    const std::string &name, const std::string &address, const std::shared_ptr<messages::QueryAgentInfoRequest> &req)
{
    if (queryAgentPromise_) {
        YRLOG_INFO("{}|another agent query is in progress", req->requestid());
        return queryAgentPromise_->GetFuture();
    }
    queryAgentPromise_ = std::make_shared<litebus::Promise<messages::QueryAgentInfoResponse>>();
    auto future = queryAgentPromise_->GetFuture();
    YRLOG_INFO("send QueryAgentInfo {}", req->requestid());
    Send(litebus::AID(name + DOMAIN_SCHEDULER_SRV_ACTOR_NAME_POSTFIX, address), "QueryAgentInfo",
         req->SerializeAsString());
    return future;
}

void DomainSchedMgrActor::ResponseQueryAgentInfo(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto resp = messages::QueryAgentInfoResponse();
    if (msg.empty() || !resp.ParseFromString(msg)) {
        YRLOG_WARN("invalid QueryAgentInfoResponse {}", msg);
        return;
    }
    YRLOG_DEBUG("{}|recevied ResponseQueryAgentInfo {}", resp.requestid(), resp.DebugString());
    queryAgentPromise_->SetValue(resp);
    queryAgentPromise_ = nullptr;
}

litebus::Future<messages::QueryResourcesInfoResponse> DomainSchedMgrActor::QueryResourcesInfo(
    const std::string &name, const std::string &address,
    const std::shared_ptr<messages::QueryResourcesInfoRequest> &req)
{
    if (queryResourcePromise_ && !queryResourcePromise_->GetFuture().IsError()) {
        // if queryResourceHelper_ over retry time, future is error and need to rebuild.
        YRLOG_INFO("{}|another resource query is in progress", req->requestid());
        return queryResourcePromise_->GetFuture();
    }

    queryResourcePromise_ = std::make_shared<litebus::Promise<messages::QueryResourcesInfoResponse>>();
    YRLOG_DEBUG("{}|send a query resource info request to domainScheduler, address is {}", req->requestid(), address);
    if (domainSchedulerAID_ == nullptr) {
        domainSchedulerAID_ = std::make_shared<litebus::AID>(name + DOMAIN_SCHEDULER_SRV_ACTOR_NAME_POSTFIX, address);
    }
    auto future = queryResourceHelper_.Begin(req->requestid(), domainSchedulerAID_, "QueryResourcesInfo",
                                             req->SerializeAsString());
    queryResourcePromise_->Associate(future);
    return queryResourcePromise_->GetFuture();
}

void DomainSchedMgrActor::ResponseQueryResourcesInfo(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto resp = messages::QueryResourcesInfoResponse();
    if (msg.empty() || !resp.ParseFromString(msg)) {
        YRLOG_WARN("invalid QueryResourcesInfoResponse {}", msg);
        return;
    }
    if (!queryResourcePromise_) {
        YRLOG_WARN("{}|No task exists for querying resource information.", resp.requestid());
        return;
    }
    YRLOG_DEBUG("{}|received a response from domainScheduler for querying resource info: {}", resp.requestid(),
                resp.DebugString());
    queryResourceHelper_.End(resp.requestid(), std::move(resp));
    queryResourcePromise_ = nullptr;
}

litebus::Future<messages::QueryInstancesInfoResponse> DomainSchedMgrActor::GetSchedulingQueue(
    const std::string &name, const std::string &address,
    const std::shared_ptr<messages::QueryInstancesInfoRequest> &req)
{
    if (getSchedulingQueuePromise_) {
        YRLOG_INFO("{}|another getSchedulingQueuePromise_ is in progress", req->requestid());
        return getSchedulingQueuePromise_->GetFuture();
    }
    getSchedulingQueuePromise_ = std::make_shared<litebus::Promise<messages::QueryInstancesInfoResponse>>();
    auto future = getSchedulingQueuePromise_->GetFuture();
    YRLOG_DEBUG("{}|send a get scheduling queue request to domainScheduler.", req->requestid());

    Send(litebus::AID(name + DOMAIN_SCHEDULER_SRV_ACTOR_NAME_POSTFIX, address), "GetSchedulingQueue",
         req->SerializeAsString());

    return future.OnComplete(
        [req, aid(GetAID()), name, address](const litebus::Future<messages::QueryInstancesInfoResponse> &future) {
            if (future.IsError()) {
                YRLOG_DEBUG("{}|send a get scheduling queue request to domainScheduler timeout.", req->requestid());
                return litebus::Async(aid, &DomainSchedMgrActor::GetSchedulingQueue, name, address, req);
            }

            return future;
        });
}

void DomainSchedMgrActor::ResponseGetSchedulingQueue(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto resp = messages::QueryInstancesInfoResponse();
    if (msg.empty() || !resp.ParseFromString(msg)) {
        YRLOG_WARN("invalid QueryInstancesInfoResponse {}", msg);
        return;
    }
    if (!getSchedulingQueuePromise_) {
        YRLOG_WARN("{}|No task exists for QueryInstancesInfoResponse.", resp.requestid());
        return;
    }
    YRLOG_DEBUG("{}|received a response from domainScheduler for QueryInstancesInfoResponse: {}", resp.requestid(),
                resp.DebugString());

    getSchedulingQueuePromise_->SetValue(resp);
    getSchedulingQueuePromise_ = nullptr;
}

void DomainSchedMgrActor::ResponseSchedule(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    ASSERT_IF_NULL(business_);
    business_->ResponseSchedule(from, std::move(name), std::move(msg));
}

template <typename T>
Status DomainSchedMgrActor::SetCallback(std::function<void()> &&func, const T &callback) const
{
    if (callback == nullptr) {
        YRLOG_ERROR("callback function is nullptr.");
        return Status(StatusCode::FAILED);
    }

    func();
    return Status(StatusCode::SUCCESS);
}

void DomainSchedMgrActor::SendScheduleRequest(const std::string &name, const std::string &address,
                                              const std::shared_ptr<messages::ScheduleRequest> &req,
                                              const uint32_t retryCycle,
                                              const std::shared_ptr<litebus::Promise<Status>> &promise)
{
    RETURN_IF_NULL(req);
    if (member_->scheduleTimers.find(req->requestid()) != member_->scheduleTimers.end()) {
        YRLOG_INFO("cancel last schedule request timer");
        (void)litebus::TimerTools::Cancel(member_->scheduleTimers[req->requestid()]);
    }

    if (member_->schedulePromises.find(req->requestid()) == member_->schedulePromises.end()) {
        member_->schedulePromises[req->requestid()] = promise;
    }

    litebus::AID domainAID(name + DOMAIN_SCHEDULER_SRV_ACTOR_NAME_POSTFIX, address);
    YRLOG_INFO("send scheduler to domain {}", std::string(domainAID));
    Send(domainAID, "Schedule", req->SerializeAsString());
    member_->scheduleTimers[req->requestid()] = litebus::AsyncAfter(
        retryCycle, GetAID(), &DomainSchedMgrActor::SendScheduleRequest, name, address, req, retryCycle, promise);
}

void DomainSchedMgrActor::SendRegisteredMessage(const litebus::AID &dst, const messages::Registered &msg)
{
    std::string serializeMsg = msg.SerializeAsString();
    YRLOG_DEBUG("send Registered to {}, message: {}", dst.HashString(), serializeMsg);
    Send(dst, "Registered", std::move(serializeMsg));
}

void DomainSchedMgrActor::UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo)
{
    litebus::AID masterAID(GLOBAL_SCHED_ACTOR_NAME, leaderInfo.address);

    auto newStatus = leader::GetStatus(GetAID(), masterAID, curStatus_);
    if (businesses_.find(newStatus) == businesses_.end()) {
        YRLOG_WARN("new status({}) business don't exist for DomainSchedMgr", newStatus);
        return;
    }
    business_ = businesses_[newStatus];
    ASSERT_IF_NULL(business_);
    business_->OnChange();
    curStatus_ = newStatus;
}

void DomainSchedMgrActor::MasterBusiness::Register(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    messages::Register request;
    if (!request.ParseFromString(msg) || request.name().empty() || request.address().empty()) {
        YRLOG_ERROR("invalid request message from {}", std::string(from));
        actor->SendRegisteredMessage(from,
                                     GenRegistered(StatusCode::GS_REGISTER_REQUEST_INVALID, "invalid request message"));
        return;
    }

    YRLOG_DEBUG("{} from {} receive message: {}", name, from.HashString(), request.ShortDebugString());
    actor->addDomainSchedCallback_(from, request.name(), request.address());
}

void DomainSchedMgrActor::MasterBusiness::NotifySchedAbnormal(const litebus::AID &from, std::string &&name,
                                                              std::string &&msg)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    YRLOG_DEBUG("{} from {} receive message: {}", name, from.HashString(), msg);

    messages::NotifySchedAbnormalRequest request;
    if (!request.ParseFromString(msg)) {
        YRLOG_ERROR("invalid request message from {}", std::string(from));
        return;
    }

    messages::NotifySchedAbnormalResponse response;
    response.set_schedname(request.schedname());
    if (request.schedname().find(LOCAL_SCHED_SRV_ACTOR_NAME)) {
        actor->delLocalSchedCallback_(request.schedname(), request.ip());
        actor->Send(from, "ResponseNotifySchedAbnormal", response.SerializeAsString());
        return;
    }

    if (request.schedname().find(DOMAIN_SCHEDULER_SRV_ACTOR_NAME_POSTFIX)) {
        actor->delDomainSchedCallback_(request.schedname(), request.ip());
        actor->Send(from, "ResponseNotifySchedAbnormal", response.SerializeAsString());
    }
}

void DomainSchedMgrActor::MasterBusiness::NotifyWorkerStatus(const litebus::AID &from, std::string &&name,
                                                             std::string &&msg)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    messages::NotifyWorkerStatusRequest request;
    if (!request.ParseFromString(msg)) {
        YRLOG_ERROR("invalid request message from: {}, name: {}", std::string(from), name);
        return;
    }
    if (actor->notifyWorkerStatusCallback_ != nullptr) {
        actor->notifyWorkerStatusCallback_(request.workerip(), DS_WORKER_TAINT_KEY, request.healthy());
    }
    messages::NotifyWorkerStatusResponse response;
    response.set_workerip(request.workerip());
    response.set_healthy(request.healthy());
    actor->Send(from, "ResponseNotifyWorkerStatus", response.SerializeAsString());
}

void DomainSchedMgrActor::MasterBusiness::ResponseSchedule(const litebus::AID &from, std::string &&name,
                                                           std::string &&msg)
{
    messages::ScheduleResponse response;
    if (!response.ParseFromString(msg) || response.requestid().empty()) {
        YRLOG_ERROR("response is invalid from {}, requestId is {}", std::string(from), response.requestid());
        return;
    }

    YRLOG_DEBUG("{} from {} receive message: {}", name, from.HashString(), response.ShortDebugString());
    if (member_->scheduleTimers.find(response.requestid()) == member_->scheduleTimers.end()) {
        YRLOG_ERROR("The timer with request ID {} is not existed", response.requestid());
        return;
    }

    if (!litebus::TimerTools::Cancel(member_->scheduleTimers[response.requestid()])) {
        YRLOG_ERROR("Cancel timer failed with request ID {}", response.requestid());
    }

    if (member_->schedulePromises.find(response.requestid()) == member_->schedulePromises.end()) {
        YRLOG_ERROR("The schedule promise with request ID {} is not existed", response.requestid());
        return;
    }

    member_->schedulePromises[response.requestid()]->SetValue(
        Status{ StatusCode(response.code()), response.message() });
    member_->schedulePromises.erase(response.requestid());
    (void)member_->scheduleTimers.erase(response.requestid());
}

void DomainSchedMgrActor::SlaveBusiness::Register(const litebus::AID &, std::string &&, std::string &&)
{
}

void DomainSchedMgrActor::SlaveBusiness::NotifySchedAbnormal(const litebus::AID &, std::string &&, std::string &&)
{
}

void DomainSchedMgrActor::SlaveBusiness::ResponseSchedule(const litebus::AID &, std::string &&, std::string &&)
{
}

void DomainSchedMgrActor::SlaveBusiness::NotifyWorkerStatus(const litebus::AID &, std::string &&, std::string &&)
{
}

}  // namespace functionsystem::global_scheduler
