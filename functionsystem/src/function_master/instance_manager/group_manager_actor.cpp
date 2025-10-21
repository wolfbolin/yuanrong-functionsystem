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

#include "group_manager_actor.h"

#include <google/protobuf/util/json_util.h>

#include "async/async.hpp"
#include "async/collect.hpp"
#include "async/defer.hpp"
#include "common/constants/signal.h"
#include "logs/logging.h"
#include "common/utils/collect_status.h"

namespace functionsystem::instance_manager {

bool GenGroupValueJson(const std::shared_ptr<messages::GroupInfo> &group, std::string &jsonStr)
{
    return google::protobuf::util::MessageToJsonString(*group, &jsonStr).ok();
}

std::shared_ptr<internal::ForwardKillRequest> MakeKillReq(
    const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo, const std::string &srcInstanceID, int32_t signal,
    const std::string &msg)
{
    core_service::KillRequest killRequest{};
    killRequest.set_signal(signal);
    killRequest.set_instanceid(instanceInfo->instanceid());
    killRequest.set_payload(msg);

    auto forwardKillRequest = std::make_shared<internal::ForwardKillRequest>();
    auto requestID = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    forwardKillRequest->set_requestid(requestID);
    forwardKillRequest->set_srcinstanceid(srcInstanceID);
    forwardKillRequest->set_instancerequestid(instanceInfo->requestid());
    *forwardKillRequest->mutable_req() = std::move(killRequest);

    return forwardKillRequest;
}

void GroupManagerActor::Init()
{
    curStatus_ = SLAVE_BUSINESS;
    businesses_[MASTER_BUSINESS] = std::make_shared<MasterBusiness>(member_, shared_from_this());
    businesses_[SLAVE_BUSINESS] = std::make_shared<SlaveBusiness>(member_, shared_from_this());
    business_ = businesses_[curStatus_];

    (void)explorer::Explorer::GetInstance().AddLeaderChangedCallback(
        "GroupManager", [aid(GetAID())](const explorer::LeaderInfo &leaderInfo) {
            litebus::Async(aid, &GroupManagerActor::UpdateLeaderInfo, leaderInfo);
        });

    WatchGroups();
    Receive("ForwardCustomSignalResponse", &GroupManagerActor::OnForwardCustomSignalResponse);
    Receive("KillGroup", &GroupManagerActor::KillGroup);
    Receive("OnClearGroup", &GroupManagerActor::OnClearGroup);
}

void GroupManagerActor::OnForwardCustomSignalResponse(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    ASSERT_IF_NULL(business_);
    business_->OnForwardCustomSignalResponse(from, std::move(name), std::move(msg));
}

void GroupManagerActor::MasterBusiness::OnForwardCustomSignalResponse(const litebus::AID &from, std::string &&name,
                                                                      std::string &&msg)
{
    YRLOG_DEBUG("receive OnForwardCustomSignalResponse from {}", std::string(from));
    auto killRsp = std::make_shared<internal::ForwardKillResponse>();
    killRsp->ParseFromString(msg);

    if (auto it = member_->killRspPromises.find(killRsp->requestid()); it != member_->killRspPromises.end()) {
        it->second->SetValue(Status(StatusCode(killRsp->code()), killRsp->message()));
        member_->killRspPromises.erase(it);
        return;
    }
    YRLOG_WARN("receive an kill response of unknown requestID({})", killRsp->requestid());
}

litebus::Future<Status> GroupManagerActor::OnInstanceAbnormal(
    const std::string &instanceKey, const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo)
{
    ASSERT_IF_NULL(business_);
    return business_->OnInstanceAbnormal(instanceKey, instanceInfo);
}

litebus::Future<Status> GroupManagerActor::MasterBusiness::OnInstanceAbnormal(
    const std::string &instanceKey, const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo)
{
    ProcessAbnormalInstanceChildrenGroup(instanceKey, instanceInfo);

    if (instanceInfo->groupid().empty()) {
        return Status::OK();
    }

    auto errMsg = fmt::format(
        "instance exit with group together, reason: group({}) instance ({}) abnormal, instance exit code({})",
        instanceInfo->groupid(), instanceInfo->instanceid(), instanceInfo->instancestatus().exitcode());
    return FatalGroup(instanceInfo->groupid(), instanceInfo->instanceid(), errMsg);
}

/**
 * FatalGroup will set a group to FATAL, and then set all instances in group to FATAL
 * @param instanceInfo
 * @param groupID
 * @return
 */
litebus::Future<Status> GroupManagerActor::MasterBusiness::FatalGroup(const std::string &groupID,
                                                                      const std::string &ignoredInstanceID,
                                                                      const std::string &errMsg)
{
    auto [groupKeyInfo, exists] = member_->groupCaches->GetGroupInfo(groupID);
    if (!exists) {
        return Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "group not found");
    }
    auto groupKey = groupKeyInfo.first;
    auto groupInfo = groupKeyInfo.second;
    if (groupInfo->status() == static_cast<int32_t>(GroupState::FAILED)) {
        YRLOG_WARN("group ({}) already failed", groupID);
        return Status::OK();
    }
    auto cacheInsLen = member_->groupCaches->GetGroupInstances(groupID).size();
    YRLOG_DEBUG("{}|{} receive instance delete, check group({}) instance life cycle: {}, cache instance len: {}",
                groupInfo->traceid(), groupInfo->requestid(), groupID, groupInfo->groupopts().samerunninglifecycle(),
                cacheInsLen);
    if (!groupInfo->groupopts().samerunninglifecycle() && cacheInsLen > 0) {
        YRLOG_WARN("{}|{} group ({}) is not same running lifecycle", groupInfo->traceid(),
                   groupInfo->requestid(), groupID);
        return Status::OK();
    }
    groupInfo->set_status(static_cast<int32_t>(GroupState::FAILED));
    groupInfo->set_message(errMsg);

    std::string groupValue;
    if (!GenGroupValueJson(groupInfo, groupValue)) {
        return Status(StatusCode::JSON_PARSE_ERROR, "failed to gen group value json str");
    }

    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    // transit group to FAILED
    ASSERT_IF_NULL(member_->metaClient);
    member_->metaClient->Put(groupKey, groupValue, {})
        .OnComplete(litebus::Defer(actor->GetAID(), &GroupManagerActor::FatalAllInstanceOfGroup, groupID,
                                   ignoredInstanceID, errMsg));
    return Status::OK();
}

litebus::Future<Status> GroupManagerActor::MasterBusiness::ProcessAbnormalInstanceChildrenGroup(
    const std::string &instanceKey, const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo)
{
    // if instance is some groups' parent, need to set the groups to FAILED
    for (auto [groupKey, groupInfo] : member_->groupCaches->GetChildGroups(instanceInfo->instanceid())) {
        groupInfo->set_status(static_cast<int32_t>(GroupState::FAILED));
        groupInfo->set_message(fmt::format("group parent({}) failed", instanceInfo->instanceid()));
        std::string groupValue;
        if (!GenGroupValueJson(groupInfo, groupValue)) {
            return Status(StatusCode::JSON_PARSE_ERROR, "failed to gen group value json str");
        }
        ASSERT_IF_NULL(member_->metaClient);
        member_->metaClient->Put(groupKey, groupValue, {})
            .OnComplete([gk(groupKey)](const litebus::Future<std::shared_ptr<PutResponse>> &putRsp) {
                if (putRsp.IsError()) {
                    YRLOG_ERROR("failed to put group({}) info in metastore, status({})", gk, putRsp.GetErrorCode());
                    return;
                }
                if (putRsp.Get()->status.IsError()) {
                    YRLOG_ERROR("failed to put group({}) info in metastore, putRsp({})", gk,
                                putRsp.Get()->status.GetMessage());
                    return;
                }
            });
    }
    return Status::OK();
}

litebus::Future<Status> GroupManagerActor::MasterBusiness::ProcessDeleteInstanceChildrenGroup(
    const std::string &instanceKey, const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo)
{
    auto createdGroups = member_->groupCaches->GetChildGroups(instanceInfo->instanceid());
    YRLOG_INFO("deleted instance({}) creates {} groups, will be deleted as well", instanceInfo->instanceid(),
               createdGroups.size());
    for (auto createdGroup : createdGroups) {
        YRLOG_INFO("group({}) parent({}) is deleted, clear group info", createdGroup.second->groupid(),
                   instanceInfo->instanceid());
        auto actor = actor_.lock();
        ASSERT_IF_NULL(actor);
        actor->ClearGroupInfo(createdGroup.second->groupid(), Status::OK());
    }
    return Status::OK();
}

void GroupManagerActor::FatalAllInstanceOfGroup(const std::string &groupID, const std::string &ignoredInstanceID,
                                                const std::string &errMsg)
{
    auto instances = member_->groupCaches->GetGroupInstances(groupID);
    for (const auto &iter : instances) {
        auto cachedInstanceInfo = iter.second;
        if (ignoredInstanceID == cachedInstanceInfo->instanceid()) {
            continue;
        }
        // send signal to instance owner, to set instance FATAL
        auto killReq = MakeKillReq(cachedInstanceInfo, GROUP_MANAGER_OWNER, GROUP_EXIT_SIGNAL, errMsg);
        ASSERT_IF_NULL(member_->globalScheduler);
        member_->globalScheduler->GetLocalAddress(cachedInstanceInfo->functionproxyid())
            .Then(litebus::Defer(GetAID(), &GroupManagerActor::InnerKillInstance, std::placeholders::_1,
                                 cachedInstanceInfo, killReq))
            .OnComplete([cachedInstanceInfo](const litebus::Future<Status> &s) {
                if (!s.IsOK()) {
                    YRLOG_ERROR("failed to get kill instance {}, on proxy {}, in group {}, err is {}",
                                cachedInstanceInfo->instanceid(), cachedInstanceInfo->functionproxyid(),
                                cachedInstanceInfo->groupid(), s.GetErrorCode());
                }
            });
    }
}

litebus::Future<Status> GroupManagerActor::OnInstancePut(
    const std::string &instanceKey, const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo)
{
    ASSERT_IF_NULL(business_);
    return business_->OnInstancePut(instanceKey, instanceInfo);
}

litebus::Future<Status> GroupManagerActor::MasterBusiness::OnInstancePut(
    const std::string &instanceKey, const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo)
{
    if (instanceInfo->groupid().empty()) {
        YRLOG_DEBUG("instance({}) doesn't belong to any group, ignored", instanceInfo->instanceid());
        return Status::OK();
    }

    // if instance is in a FAILED group
    if (auto [groupKeyInfo, exists] = member_->groupCaches->GetGroupInfo(instanceInfo->groupid());
        exists && groupKeyInfo.second->status() == static_cast<int32_t>(GroupState::FAILED)) {
        // check instance state, only kill if in SCHEDULING, CREATING, RUNNING
        if (instanceInfo->instancestatus().code() != static_cast<int32_t>(InstanceState::SCHEDULING) &&
            instanceInfo->instancestatus().code() != static_cast<int32_t>(InstanceState::CREATING) &&
            instanceInfo->instancestatus().code() != static_cast<int32_t>(InstanceState::RUNNING) &&
            instanceInfo->instancestatus().code() != static_cast<int32_t>(InstanceState::EXITING) &&
            instanceInfo->instancestatus().code() != static_cast<int32_t>(InstanceState::EXITED) &&
            instanceInfo->instancestatus().code() != static_cast<int32_t>(InstanceState::EVICTING)) {
            return Status::OK();
        }

        auto actor = actor_.lock();
        ASSERT_IF_NULL(actor);

        auto groupInfo = groupKeyInfo.second;
        auto killReq = MakeKillReq(instanceInfo, GROUP_MANAGER_OWNER, GROUP_EXIT_SIGNAL,
                                   "instance exit with group together, reason: group(" + instanceInfo->groupid() +
                                       ") failed due to " + groupInfo->message());
        // set instance to fatal
        ASSERT_IF_NULL(member_->globalScheduler);
        return member_->globalScheduler->GetLocalAddress(instanceInfo->functionproxyid())
            .Then(litebus::Defer(actor->GetAID(), &GroupManagerActor::InnerKillInstance, std::placeholders::_1,
                                 instanceInfo, killReq))
            .OnComplete([instanceInfo](litebus::Future<Status> s) {
                if (!s.IsOK()) {
                    YRLOG_ERROR("failed to get kill instance {}, on proxy {}, in group {}, err is {}",
                                instanceInfo->instanceid(), instanceInfo->functionproxyid(), instanceInfo->groupid(),
                                s.GetErrorCode());
                }
                return s;
            });
    }

    // otherwise, record the instance
    member_->groupCaches->AddGroupInstance(instanceInfo->groupid(), instanceKey, instanceInfo);
    return Status::OK();
}

void GroupManagerActor::OnClearGroup(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto killGroupResp = std::make_shared<::messages::KillGroupResponse>();
    if (!killGroupResp->ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse response for clear group. from({}) msg({}), ignore it", std::string(from), msg);
        return;
    }
    requestGroupClearMatch_.Synchronized(killGroupResp->groupid(), Status::OK());
}

/// ====================== Kill group instances
void GroupManagerActor::KillGroup(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    ASSERT_IF_NULL(business_);
    YRLOG_DEBUG("receive kill group request from {}", from.HashString());
    business_->KillGroup(from, std::move(name), std::move(msg));
}

void GroupManagerActor::MasterBusiness::KillGroup(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    // uses local's auth for now
    auto killGroupReq = std::make_shared<::messages::KillGroup>();
    killGroupReq->ParseFromString(msg);

    if (auto inserted = member_->killingGroups.emplace(killGroupReq->groupid()).second; !inserted) {
        YRLOG_INFO("receive repeated kill group({}) request, ignored", killGroupReq->groupid());
        return;
    }
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);

    InnerKillGroup(killGroupReq->groupid(), killGroupReq->srcinstanceid())
        .Then(litebus::Defer(actor->GetAID(), &GroupManagerActor::InnerKillInstanceOnComplete, from,
                             killGroupReq->groupid(), std::placeholders::_1));
}

litebus::Future<Status> GroupManagerActor::MasterBusiness::InnerKillGroup(const std::string &groupID,
                                                                          const std::string &srcInstanceID)
{
    YRLOG_INFO("start killing group {}", groupID);
    auto instances = member_->groupCaches->GetGroupInstances(groupID);
    auto futures = std::list<litebus::Future<Status>>();

    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);

    if (auto group = member_->groupCaches->GetGroupInfo(groupID);
        group.second && group.first.second->status() == static_cast<int32_t>(GroupState::SCHEDULING)) {
        auto reason = fmt::format("group({}) canceled", groupID);
        ASSERT_IF_NULL(member_->instanceManager);
        (void)member_->instanceManager->TryCancelSchedule(groupID, messages::CancelType::GROUP, reason);
    }
    for (const auto &inst : instances) {
        auto killReq = MakeKillReq(inst.second, srcInstanceID, SHUT_DOWN_SIGNAL, "group killed");

        auto promise = std::make_shared<litebus::Promise<Status>>();
        futures.emplace_back(promise->GetFuture());
        member_->killRspPromises[killReq->requestid()] = promise;
        ASSERT_IF_NULL(member_->globalScheduler);
        member_->globalScheduler->GetLocalAddress(inst.second->functionproxyid())
            .Then(litebus::Defer(actor->GetAID(), &GroupManagerActor::InnerKillInstance, std::placeholders::_1,
                                 inst.second, killReq))
            .OnComplete([instInfo(inst.second)](const litebus::Future<Status> &s) {
                if (!s.IsOK()) {
                    YRLOG_ERROR("failed to get kill instance {}, on proxy {}, in group {}", instInfo->instanceid(),
                                instInfo->functionproxyid(), instInfo->groupid());
                }
            });
    }

    std::string errDescription = "kill group(" + groupID + ") instances";
    return CollectStatus(futures, errDescription, StatusCode::FAILED, StatusCode::SUCCESS)
        .After(KILLGROUP_TIMEOUT,
               [](const litebus::Future<Status> &future) {
                   auto promise = litebus::Promise<Status>();
                   promise.SetValue(Status(StatusCode::REQUEST_TIME_OUT, "kill group timeout"));
                   return promise.GetFuture();
               })
        .Then(litebus::Defer(actor->GetAID(), &GroupManagerActor::ClearGroupInfo, groupID, std::placeholders::_1));
}

/**
 * @brief local abnormal, kill all other instances
 *
 * @param abnormalLocal
 * @param instanceInfo
 * @return litebus::Future<Status>
 */
litebus::Future<Status> GroupManagerActor::OnLocalAbnormal(const std::string &abnormalLocal)
{
    ASSERT_IF_NULL(business_);
    return business_->OnLocalAbnormal(abnormalLocal);
}

litebus::Future<Status> GroupManagerActor::MasterBusiness::OnLocalAbnormal(const std::string &abnormalLocal)
{
    YRLOG_INFO("master business get on local({}) abnormal", abnormalLocal);
    // Find owned groups on this local
    auto ownedGroups = member_->groupCaches->GetNodeGroups(abnormalLocal);
    YRLOG_INFO("abnormal local owns {} groups", ownedGroups.size());
    for (auto group : ownedGroups) {
        YRLOG_INFO("abnormal local owns group {}({})", group.first, group.second->status());
        auto currGroupState = group.second->status();
        group.second->set_ownerproxy(GROUP_MANAGER_OWNER);
        if (currGroupState == static_cast<int32_t>(GroupState::SCHEDULING)) {
            group.second->set_status(static_cast<int32_t>(GroupState::FAILED));
        }

        std::string groupValue;
        if (!GenGroupValueJson(group.second, groupValue)) {
            return Status(StatusCode::JSON_PARSE_ERROR, "failed to gen group value json str");
        }
        ASSERT_IF_NULL(member_->metaClient);
        member_->metaClient->Put(group.first, groupValue, {}).Then([](const std::shared_ptr<PutResponse> &rsp) {
            if (rsp->status.IsError()) {
                YRLOG_ERROR("failed to modify group owner in etcd, err(%s)", rsp->status.GetMessage());
            }
            return rsp->status;
        });

        if (currGroupState != static_cast<int32_t>(GroupState::SCHEDULING)) {
            continue;
        }

        auto actor = actor_.lock();
        // let local set fatal to all instance on this local
        auto instances = member_->groupCaches->GetGroupInstances(group.second->groupid());
        YRLOG_INFO("send GROUP_EXIT_SIGNAL to {} instances", instances.size());
        for (auto inst : instances) {
            auto killReq =
                MakeKillReq(inst.second, GROUP_MANAGER_OWNER, GROUP_EXIT_SIGNAL,
                            "instance exit with group together, reason: local scheduler(" + abnormalLocal + ") failed");
            auto promise = std::make_shared<litebus::Promise<Status>>();
            member_->killRspPromises[killReq->requestid()] = promise;
            ASSERT_IF_NULL(member_->globalScheduler);
            member_->globalScheduler->GetLocalAddress(inst.second->functionproxyid())
                .Then(litebus::Defer(actor->GetAID(), &GroupManagerActor::InnerKillInstance, std::placeholders::_1,
                                     inst.second, killReq))
                .OnComplete([instInfo(inst.second)](litebus::Future<Status> s) {
                    if (!s.IsOK()) {
                        YRLOG_ERROR("failed to get kill instance {}, on proxy {}, in group {}, err is {}",
                                    instInfo->instanceid(), instInfo->functionproxyid(), instInfo->groupid(),
                                    s.GetErrorCode());
                    }
                });
        }
    }

    // if some instances not owned by this local, but running on this local,
    // let instance manager decide, it may reschedule the instances
    // if instance manager decides to set them FATAL, it will trigger OnInstanceAbnormal later
    return Status::OK();
}

/// OnInstanceDelete
/// handles the deletion of instances.
/// once instance deleted, clear the local cache and do nothing, the recyle job would be done when fatal received.
litebus::Future<Status> GroupManagerActor::OnInstanceDelete(
    const std::string &instanceKey, const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo)
{
    ASSERT_IF_NULL(business_);
    return business_->OnInstanceDelete(instanceKey, instanceInfo);
}

litebus::Future<Status> GroupManagerActor::ClearGroupInfo(const std::string &groupID, const Status &status)
{
    if (!status.IsOk()) {
        YRLOG_WARN("status is not ok when clear group info, {}", status.GetMessage());
        return status;
    }
    auto [groupKeyInfo, exists] = member_->groupCaches->GetGroupInfo(groupID);
    if (!exists) {
        return Status(StatusCode::ERR_GROUP_SCHEDULE_FAILED, "group not found in group manager");
    }
    auto groupKey = groupKeyInfo.first;
    auto ownerProxy = groupKeyInfo.second->ownerproxy();
    auto clearGroupReq = std::make_shared<messages::KillGroup>();
    clearGroupReq->set_groupid(groupID);
    clearGroupReq->set_grouprequestid(groupKeyInfo.second->requestid());
    auto promise = std::make_shared<litebus::Promise<Status>>();
    ASSERT_IF_NULL(member_->globalScheduler);
    member_->globalScheduler->GetLocalAddress(ownerProxy)
        .Then(litebus::Defer(GetAID(), &GroupManagerActor::SendClearGroupToLocal, std::placeholders::_1, groupKey,
                             clearGroupReq, promise));
    return promise->GetFuture();
}

litebus::Future<Status> GroupManagerActor::SendClearGroupToLocal(
    const litebus::Option<std::string> &proxyAddress, const std::string &groupKey,
    const std::shared_ptr<messages::KillGroup> clearReq, const std::shared_ptr<litebus::Promise<Status>> &promise)
{
    if (proxyAddress.IsNone()) {
        YRLOG_WARN("{}|failed to clear group, local address not found", clearReq->groupid());
        DeleteGroupInfoFromMetaStore(groupKey, promise);
        return Status::OK();
    }
    auto localAID = litebus::AID(LOCAL_GROUP_CTRL_ACTOR_NAME, proxyAddress.Get());
    auto future = requestGroupClearMatch_.AddSynchronizer(clearReq->groupid());
    (void)Send(localAID, "ClearGroup", clearReq->SerializeAsString());
    future.OnComplete([promise, groupKey, aid(GetAID())](const litebus::Future<Status> &future) {
        if (future.IsError()) {
            YRLOG_WARN("failed get clear group response, group:{}", groupKey);
        }
        litebus::Async(aid, &GroupManagerActor::DeleteGroupInfoFromMetaStore, groupKey, promise);
    });
    return Status::OK();
}

void GroupManagerActor::DeleteGroupInfoFromMetaStore(const std::string &groupKey,
                                                     const std::shared_ptr<litebus::Promise<Status>> promise)
{
    ASSERT_IF_NULL(member_->metaClient);
    member_->metaClient->Delete(groupKey, {})
        .OnComplete([promise, groupKey](const litebus::Future<std::shared_ptr<DeleteResponse>> &delRsp) {
            if (delRsp.IsError()) {
                promise->SetValue(Status(StatusCode::BP_META_STORAGE_DELETE_ERROR,
                                         "failed to delete group info to metastore, key " + groupKey));
            } else {
                promise->SetValue(Status::OK());
            }
        });
}

litebus::Future<Status> GroupManagerActor::InnerKillInstanceOnComplete(const litebus::AID &from,
                                                                       const std::string &groupID, const Status &status)
{
    auto msg = ::messages::KillGroupResponse{};
    msg.set_groupid(groupID);
    msg.set_code(static_cast<int32_t>(status.StatusCode()));
    msg.set_message(status.GetMessage());
    YRLOG_INFO("send OnKillGroup of ({}) to {}, msg {}", groupID, from.HashString(), msg.message());
    Send(from, "OnKillGroup", msg.SerializeAsString());
    member_->killingGroups.erase(groupID);
    return Status::OK();
}

litebus::Future<Status> GroupManagerActor::InnerKillInstance(
    const litebus::Option<std::string> &proxyAddress, const std::shared_ptr<resource_view::InstanceInfo> &instance,
    const std::shared_ptr<internal::ForwardKillRequest> killReq)
{
    if (proxyAddress.IsNone()) {
        auto status = Status(StatusCode::ERR_INNER_COMMUNICATION, "local address not found");
        if (auto iter = member_->killRspPromises.find(killReq->requestid()); iter != member_->killRspPromises.end()) {
            iter->second->SetValue(status);
            member_->killRspPromises.erase(iter);
        }
        return status;
    }
    auto localAID =
        litebus::AID(instance->functionproxyid() + LOCAL_SCHED_INSTANCE_CTRL_ACTOR_NAME_POSTFIX, proxyAddress.Get());

    YRLOG_INFO("{}|send instance({}) kill request to local({})", killReq->requestid(), instance->instanceid(),
               std::string(localAID));
    (void)Send(localAID, "ForwardCustomSignalRequest", killReq->SerializeAsString());
    return Status::OK();
}

void GroupManagerActor::WatchGroups()
{
    YRLOG_INFO("start watch groups info");
    ASSERT_IF_NULL(member_->metaClient);
    (void)member_->metaClient->Get(GROUP_PATH_PREFIX, { .prefix = true })
        .Then(litebus::Defer(GetAID(), &GroupManagerActor::WatchGroupThen, std::placeholders::_1));
}

void GroupManagerActor::OnGroupWatch(const std::shared_ptr<Watcher> &watcher)
{
    YRLOG_INFO("start watch groups info");
    member_->watcher = watcher;
}

void GroupManagerActor::OnGroupWatchEvent(const std::vector<WatchEvent> &events)
{
    YRLOG_INFO("get group watch events");
    for (const auto &event : events) {
        switch (event.eventType) {
            case EVENT_TYPE_PUT: {
                auto eventKey = TrimKeyPrefix(event.kv.key(), member_->metaClient->GetTablePrefix());
                auto group = std::make_shared<messages::GroupInfo>();
                if (TransToGroupInfoFromJson(*group, event.kv.value())) {
                    OnGroupPut(eventKey, group);
                } else {
                    YRLOG_ERROR("failed to transform group({}) info from String.", eventKey);
                }
                break;
            }
            case EVENT_TYPE_DELETE: {
                auto history = std::make_shared<messages::GroupInfo>();
                auto eventKey = TrimKeyPrefix(event.prevKv.key(), member_->metaClient->GetTablePrefix());
                if (!TransToGroupInfoFromJson(*history, event.prevKv.value())) {
                    YRLOG_ERROR("failed to transform group({}) info from String.", eventKey);
                    break;
                }
                OnGroupDelete(eventKey, history);
                break;
            }
            default: {
                YRLOG_ERROR("not supported");
                break;
            }
        }
    }
}

litebus::Future<Status> GroupManagerActor::WatchGroupThen(const std::shared_ptr<GetResponse> &response)
{
    YRLOG_INFO("get group response size={}", response->kvs.size());
    if (!response->status.IsOk()) {
        YRLOG_ERROR("failed to get all instances.");
        return Status::OK();
    }
    if (response->header.revision > INT64_MAX - 1) {
        YRLOG_ERROR("revision({}) add operation will exceed the maximum value({}) of INT64", response->header.revision,
                    INT64_MAX);
        return Status::OK();
    }

    auto observer = [aid(GetAID())](const std::vector<WatchEvent> &events, bool) -> bool {
        litebus::Async(aid, &GroupManagerActor::OnGroupWatchEvent, events);
        return true;
    };
    auto syncer = [aid(GetAID())]() -> litebus::Future<SyncResult> {
        return litebus::Async(aid, &GroupManagerActor::GroupInfoSyncer);
    };

    WatchOption option = { .prefix = true, .prevKv = true, .revision = response->header.revision + 1 };
    ASSERT_IF_NULL(member_->metaClient);
    // eg. /sn/instance/business/yrk/tenant/0/function/../version/..
    (void)member_->metaClient->Watch(GROUP_PATH_PREFIX, option, observer, syncer)
        .Then(std::function<litebus::Future<Status>(const std::shared_ptr<Watcher> &watcher)>{
            [aid(GetAID())](const std::shared_ptr<Watcher> &watcher) -> litebus::Future<Status> {
                litebus::Async(aid, &GroupManagerActor::OnGroupWatch, watcher);
                return Status::OK();
            } });

    for (const auto &kv : response->kvs) {
        auto group = std::make_shared<messages::GroupInfo>();
        auto eventKey = TrimKeyPrefix(kv.key(), member_->metaClient->GetTablePrefix());
        if (TransToGroupInfoFromJson(*group, kv.value())) {
            OnGroupPut(eventKey, group);
        } else {
            YRLOG_ERROR("failed to transform instance({}) info from String.", eventKey);
        }
    }

    return Status::OK();
}

void GroupManagerActor::OnGroupPut(const std::string &groupKey, std::shared_ptr<messages::GroupInfo> groupInfo)
{
    ASSERT_IF_NULL(business_);
    business_->OnGroupPut(groupKey, groupInfo);
}

void GroupManagerActor::MasterBusiness::OnGroupPut(const std::string &groupKey,
                                                   std::shared_ptr<messages::GroupInfo> groupInfo)
{
    member_->groupCaches->AddGroup(groupKey, groupInfo);
    // if group parent is abnormal/deleted, fatal/delete group and all instances in group
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    ASSERT_IF_NULL(member_->instanceManager);
    member_->instanceManager->GetInstanceInfoByInstanceID(groupInfo->parentid())
        .Then(litebus::Defer(actor->GetAID(), &GroupManagerActor::OnGroupPutCheckParentStatus, groupKey, groupInfo,
                             std::placeholders::_1));
}

/// OnGroupPutCheckParentStatus
litebus::Future<Status> GroupManagerActor::OnGroupPutCheckParentStatus(
    const std::string &groupKey, const std::shared_ptr<messages::GroupInfo> &groupInfo,
    const std::pair<std::string, std::shared_ptr<resource_view::InstanceInfo>> &parentInfo)
{
    if (parentInfo.second == nullptr) {
        return OnGroupPutParentMissing(groupKey, groupInfo);
    } else if (parentInfo.second->instancestatus().code() == static_cast<int32_t>(InstanceState::FATAL)) {
        return OnGroupPutParentFatal(groupKey, groupInfo);
    }
    // else is ok
    return Status::OK();
}

/// OnGroupPutParentMissing
litebus::Future<Status> GroupManagerActor::OnGroupPutParentMissing(
    const std::string &groupKey, const std::shared_ptr<messages::GroupInfo> &groupInfo)
{
    ASSERT_IF_NULL(business_);
    return business_->InnerKillGroup(groupInfo->groupid(), groupInfo->parentid());
}

/// OnGroupPutParentFatal
litebus::Future<Status> GroupManagerActor::OnGroupPutParentFatal(const std::string &groupKey,
                                                                 const std::shared_ptr<messages::GroupInfo> &groupInfo)
{
    auto errMsg = fmt::format("group({}) parent({}) is abnormal", groupInfo->groupid(), groupInfo->parentid());
    ASSERT_IF_NULL(business_);
    return business_->FatalGroup(groupInfo->groupid(), groupInfo->parentid(), errMsg);
}

void GroupManagerActor::OnGroupDelete(const std::string &groupKey,
                                      const std::shared_ptr<messages::GroupInfo> &groupInfo)
{
    member_->groupCaches->RemoveGroup(groupInfo->groupid());
}

/// ======================= Not implemented yet
void GroupManagerActor::QueryGroupStatus(const litebus::AID &from, std::string &&name, std::string &&msg)  // NOLINT
{ /* Not implemented yet */
    YRLOG_ERROR("calling not implemented method QueryGroupStatus");
}

/// ======================= OnChange
void GroupManagerActor::MasterBusiness::OnChange()
{
    YRLOG_INFO("GroupManagerActor become master");
    // fetch failed group, fetch failed group instances, and recycle them
    for (auto group : member_->groupCaches->GetGroups()) {
        if (group.second.second->status() == static_cast<int32_t>(GroupState::FAILED)) {
            YRLOG_INFO("find group({}) is failed", group.second.second->groupid());
            for (auto instance : member_->groupCaches->GetGroupInstances(group.second.second->groupid())) {
                if (instance.second->instancestatus().code() == static_cast<int32_t>(InstanceState::RUNNING) ||
                    instance.second->instancestatus().code() == static_cast<int32_t>(InstanceState::CREATING)) {
                    YRLOG_INFO("find instance({}) with status({}) in group({}), will set it to fatal",
                               instance.second->instanceid(), instance.second->instancestatus().code(),
                               group.second.second->groupid());
                    auto actor = actor_.lock();
                    auto killReq = MakeKillReq(instance.second, GROUP_MANAGER_OWNER, GROUP_EXIT_SIGNAL,
                                               "instance exit with group together, reason: group(" +
                                                   group.second.second->groupid() + ") failed due to " +
                                                   group.second.second->message());
                    ASSERT_IF_NULL(member_->globalScheduler);
                    member_->globalScheduler->GetLocalAddress(instance.second->functionproxyid())
                        .Then(litebus::Defer(actor->GetAID(), &GroupManagerActor::InnerKillInstance,
                                             std::placeholders::_1, instance.second, killReq))
                        .OnComplete([instInfo(instance.second)](litebus::Future<Status> s) {
                            if (!s.IsOK()) {
                                YRLOG_ERROR("failed to get kill instance {}, on proxy {}, in group {}",
                                            instInfo->instanceid(), instInfo->functionproxyid(), instInfo->groupid());
                            }
                        });
                }
            }
        }
    }
}

/// ======================= Below are group caches operations
void GroupManagerActor::GroupCaches::AddGroup(const std::string groupKey,
                                              const std::shared_ptr<messages::GroupInfo> &group)
{
    YRLOG_DEBUG("adding group(id={}, parent={}, node={}, status={})", group->groupid(), group->parentid(),
                group->ownerproxy(), group->status());
    // groups
    groups_.insert_or_assign(group->groupid(), std::make_pair(groupKey, group));

    // node to group
    if (auto it = nodeName2Groups_.find(group->ownerproxy()); it != nodeName2Groups_.end()) {
        it->second.insert_or_assign(groupKey, group);
    } else {
        nodeName2Groups_.insert_or_assign(
            group->ownerproxy(),
            std::unordered_map<std::string, std::shared_ptr<messages::GroupInfo>>{ { groupKey, group } });
    }

    // parent to group
    if (auto it = parent2Groups_.find(group->parentid()); it != parent2Groups_.end()) {
        it->second.insert_or_assign(groupKey, group);
    } else {
        parent2Groups_.insert_or_assign(
            group->parentid(),
            std::unordered_map<std::string, std::shared_ptr<messages::GroupInfo>>{ { groupKey, group } });
    }
}

void GroupManagerActor::GroupCaches::RemoveGroup(const std::string &groupID)
{
    YRLOG_DEBUG("remove group({})", groupID);
    std::string groupOwner;
    std::string groupKey;
    std::string groupParent;

    // groups
    if (auto it = groups_.find(groupID); it != groups_.end()) {
        groupOwner = it->second.second->ownerproxy();
        groupParent = it->second.second->parentid();
        groupKey = it->second.first;
        groups_.erase(it);
    }

    // node to group
    if (auto it = nodeName2Groups_.find(groupOwner); it != nodeName2Groups_.end()) {
        it->second.erase(groupKey);
        if (it->second.empty()) {
            nodeName2Groups_.erase(it);
        }
    }

    // parent to group
    if (auto it = parent2Groups_.find(groupParent); it != parent2Groups_.end()) {
        it->second.erase(groupKey);
        if (it->second.empty()) {
            parent2Groups_.erase(it);
        }
    }

    // group instances
    if (auto it = groupID2Instances_.find(groupID); it != groupID2Instances_.end()) {
        groupID2Instances_.erase(it);
    }
}

std::pair<GroupKeyInfoPair, bool> GroupManagerActor::GroupCaches::GetGroupInfo(const std::string &groupID)
{
    if (auto it = groups_.find(groupID); it != groups_.end()) {
        return { it->second, true };
    }
    return { {}, false };
}

GroupKeyInfoMap GroupManagerActor::GroupCaches::GetNodeGroups(const std::string &nodeName)
{
    // node to group
    if (auto it = nodeName2Groups_.find(nodeName); it != nodeName2Groups_.end()) {
        return it->second;
    }
    return {};
}

GroupKeyInfoMap GroupManagerActor::GroupCaches::GetChildGroups(const std::string &parentID)
{
    // parent to group
    if (auto it = parent2Groups_.find(parentID); it != parent2Groups_.end()) {
        return it->second;
    }
    return {};
}

void GroupManagerActor::GroupCaches::AddGroupInstance(const std::string &groupID, const std::string &instanceKey,
                                                      const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo)
{
    if (auto it = groupID2Instances_.find(groupID); it != groupID2Instances_.end()) {
        it->second.insert({ instanceKey, instanceInfo });
    } else {
        groupID2Instances_.emplace(groupID,
                                   std::unordered_map<std::string, std::shared_ptr<resource_view::InstanceInfo>>{
                                       { instanceKey, instanceInfo } });
    }
}

InstanceKeyInfoMap GroupManagerActor::GroupCaches::GetGroupInstances(const std::string &groupID)
{
    if (auto it = groupID2Instances_.find(groupID); it != groupID2Instances_.end()) {
        return it->second;
    }
    return {};
}

std::unordered_map<std::string, GroupKeyInfoPair> GroupManagerActor::GroupCaches::GetGroupInfos()
{
    return groups_;
}

litebus::Future<SyncResult> GroupManagerActor::GroupInfoSyncer()
{
    GetOption opts;
    opts.prefix = true;
    ASSERT_IF_NULL(member_->metaClient);
    return member_->metaClient->Get(GROUP_PATH_PREFIX, opts)
        .Then(litebus::Defer(GetAID(), &GroupManagerActor::OnGroupInfoSyncer, std::placeholders::_1));
}

litebus::Future<SyncResult> GroupManagerActor::OnGroupInfoSyncer(const std::shared_ptr<GetResponse> &getResponse)
{
    if (getResponse->status.IsError()) {
        YRLOG_INFO("failed to get key({}) from meta storage", GROUP_PATH_PREFIX);
        return SyncResult{ getResponse->status, 0 };
    }

    if (getResponse->kvs.empty()) {
        YRLOG_INFO("get no result with key({}) from meta storage, revision is {}", GROUP_PATH_PREFIX,
                   getResponse->header.revision);
        return SyncResult{ Status::OK(), getResponse->header.revision + 1 };
    }

    std::vector<WatchEvent> events;
    std::set<std::string> etcdKvSet;
    for (auto &kv : getResponse->kvs) {
        auto group = std::make_shared<messages::GroupInfo>();
        auto eventKey = TrimKeyPrefix(kv.key(), member_->metaClient->GetTablePrefix());
        if (TransToGroupInfoFromJson(*group, kv.value())) {
            OnGroupPut(eventKey, group);
            etcdKvSet.emplace(group->groupid());
        } else {
            YRLOG_ERROR("failed to transform instance({}) info from String.", eventKey);
        }
    }
    for (const auto groupInfo : member_->groupCaches->GetGroupInfos()) {
        if (etcdKvSet.count(groupInfo.first) == 0) {  // not in etcd, need to delete
            YRLOG_DEBUG("delete ({}) from cache.", groupInfo.second.first);
            OnGroupDelete(groupInfo.second.first, groupInfo.second.second);
        }
    }
    return SyncResult{ Status::OK(), getResponse->header.revision + 1 };
}

}  // namespace functionsystem::instance_manager