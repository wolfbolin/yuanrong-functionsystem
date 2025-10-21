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

#ifndef COMMON_UTILS_STRUCT_TRANSFER_H
#define COMMON_UTILS_STRUCT_TRANSFER_H

#include <exception>
#include <map>
#include <regex>
#include <nlohmann/json.hpp>
#include <sstream>

#include "logs/logging.h"
#include "metadata/metadata.h"
#include "common/constants/actor_name.h"
#include "proto/pb/message_pb.h"
#include "proto/pb/posix_pb.h"
#include "resource_type.h"
#include "common/schedule_decision/scheduler_common.h"
#include "common/scheduler_framework/utils/label_affinity_selector.h"

namespace functionsystem {
const std::string SCHEDULE_POLICY = "schedule_policy";
const std::string NODE_SELECTOR = "node_selector";
const std::string INIT_CALL_TIMEOUT = "init_call_timeout";
const int32_t DEFAULT_RESCHEDULE_TIME = 1;
const int32_t DEFAULT_REDEPLOY_TIME = 1;
const int32_t DEFAULT_SCHEDULE_TIMEOUT_MS = 18000;
const int64_t INSTANCE_INIT_VERSION = 0;
const int32_t MAX_PREFERRED_AFFINITY_SCORE = 100;
const int32_t PREFERRED_AFFINITY_SCORE_STEP = 10;
const size_t LOCAL_SPLIT_SIZE = 2;

const int64_t DEFAULT_PREEMPTION_WEIGHT = 3;
const std::string PREFERRED_PREEMPTION_AFFINITY = "PreferredPreemptionAffinity";
const std::string PREFERRED_PREEMPTION_ANTIAFFINITY = "PreferredPreemptionAntiAffinity";
const std::string PREEMPTIBLE = "Preemptible";
const std::string NOT_PREEMPTIBLE = "NotPreemptible";
const std::string FAAS_FRONTEND_FUNCTION_NAME_PREFIX = "0/0-system-faasfrontend/";
const std::string CREATE_SOURCE = "source";
const std::string FRONTEND_STR = "frontend";
const std::string RUNTIME_UUID_PREFIX = "runtime-";
const std::string APP_ENTRYPOINT = "ENTRYPOINT";
const std::string PID = "pid";
const std::string CREATE_TIME_STAMP = "createTimestamp";
const std::string RECEIVED_TIMESTAMP = "receivedTimestamp";
const std::string INSTANCE_MOD_REVISION = "modRevision";
const std::string NAMED = "named";

/**
 * transfer from FunctionMeta's hbm value to ScheduleRequest
 * @param scheduleRequest: struct of ScheduleRequest
 * @param funcMeta: function meta info
 */
static void LoadHbmToScheduleRequest(const std::shared_ptr<messages::ScheduleRequest> &scheduleRequest,
                                     const FunctionMeta &funcMeta)
{
    auto deviceFunctionMeta = funcMeta.extendedMetaData.deviceMetaData;
    auto type = deviceFunctionMeta.type;
    if (type.empty()) {
        return;
    }
    YRLOG_INFO("{}|receive heterogeneous create req, cardType: {}", scheduleRequest->requestid(), type);

    std::string keyName;
    if (type == NPU_RESOURCE_NAME) {
        keyName = NPU_RESOURCE_NAME + "/" + deviceFunctionMeta.model + "/" + HETEROGENEOUS_MEM_KEY;
    } else if (type == GPU_RESOURCE_NAME) {
        keyName = GPU_RESOURCE_NAME + "/" + deviceFunctionMeta.model + "/" + HETEROGENEOUS_MEM_KEY;
    } else {
        YRLOG_WARN("{}|type: {} not supported, recheck the card type", scheduleRequest->requestid(), type);
        return;
    }
    resource_view::Resource resource;
    resource.set_name(keyName);
    resource.set_type(resource_view::ValueType::Value_Type_SCALAR);
    resource.mutable_scalar()->set_value(deviceFunctionMeta.hbm);
    (*scheduleRequest->mutable_instance()->mutable_resources()->mutable_resources())[keyName] = resource;
}

/**
 * transfer from FunctionMeta's stream and latency to ScheduleRequest
 * @param scheduleRequest: struct of ScheduleRequest. The stream and latency info will be stored in resources
 * @param funcMeta: function meta info
 */
static void LoadLatencyStreamToScheduleRequest(const std::shared_ptr<messages::ScheduleRequest> &scheduleRequest,
                                               const FunctionMeta &funcMeta)
{
    auto devieMetaData = funcMeta.extendedMetaData.deviceMetaData;
    auto cardType = devieMetaData.type;
    if (cardType.empty()) {
        return;
    }

    std::string latencyKey;
    if (cardType == NPU_RESOURCE_NAME) {
        latencyKey = NPU_RESOURCE_NAME + "/" + devieMetaData.model + "/" + HETEROGENEOUS_LATENCY_KEY;
    } else if (cardType == GPU_RESOURCE_NAME) {
        latencyKey = GPU_RESOURCE_NAME + "/" + devieMetaData.model + "/" + HETEROGENEOUS_LATENCY_KEY;
    } else {
        YRLOG_WARN("{}|type: {} not supported, recheck the card type", scheduleRequest->requestid(), cardType);
        return;
    }
    resource_view::Resource resource;
    resource.set_name(latencyKey);
    resource.set_type(resource_view::ValueType::Value_Type_SCALAR);
    resource.mutable_scalar()->set_value(devieMetaData.latency);
    (*scheduleRequest->mutable_instance()->mutable_resources()->mutable_resources())[latencyKey] = resource;

    std::string streamKey;

    if (cardType == NPU_RESOURCE_NAME) {
        streamKey = NPU_RESOURCE_NAME + "/" + devieMetaData.model + "/" + HETEROGENEOUS_STREAM_KEY;
    } else if (cardType == GPU_RESOURCE_NAME) {
        streamKey = GPU_RESOURCE_NAME + "/" + devieMetaData.model + "/" + HETEROGENEOUS_STREAM_KEY;
    } else {
        YRLOG_WARN("{}|type: {} not supported, recheck the card type", scheduleRequest->requestid(), cardType);
        return;
    }

    resource_view::Resource streamResource;
    streamResource.set_name(streamKey);
    streamResource.set_type(resource_view::ValueType::Value_Type_SCALAR);
    streamResource.mutable_scalar()->set_value(devieMetaData.latency);
    if (devieMetaData.stream <= 1) {
        streamResource.mutable_scalar()->set_value(1);
    } else {
        streamResource.mutable_scalar()->set_value(MULTI_STREAM_DEFAULT_NUM);
    }
    (*scheduleRequest->mutable_instance()->mutable_resources()->mutable_resources())[streamKey] = streamResource;
}

/**
 * Load Named Function meta to ScheduleRequest
 * @param scheduleRequest: struct of ScheduleRequest
 * @param funcMeta function meta
 */
[[maybe_unused]] static void LoadDeviceFunctionMetaToScheduleRequest(
    const std::shared_ptr<messages::ScheduleRequest> &scheduleRequest, const FunctionMeta &funcMeta)
{
    // load xpu hbm resource
    LoadHbmToScheduleRequest(scheduleRequest, funcMeta);
    // load latency and stream resource
    LoadLatencyStreamToScheduleRequest(scheduleRequest, funcMeta);
}

/**
 * transfer from FunctionMeta's hbm value to CreateRequest
 * @param createRequest: struct of CreateRequest
 * @param funcMeta: function meta info
 */
static void LoadHbmToCreateRequest(CreateRequest &createRequest, const FunctionMeta &funcMeta)
{
    auto deviceFunctionMeta = funcMeta.extendedMetaData.deviceMetaData;
    auto requestRes = createRequest.schedulingops().resources();
    auto type = deviceFunctionMeta.type;
    if (type.empty()) {
        return;
    }
    YRLOG_INFO("{}|receive heterogeneous create req, cardType: {}", createRequest.requestid(), type);

    std::string keyName;
    if (type == NPU_RESOURCE_NAME) {
        keyName = NPU_RESOURCE_NAME + "/" + deviceFunctionMeta.model + "/" + HETEROGENEOUS_MEM_KEY;
    } else if (type == GPU_RESOURCE_NAME) {
        keyName = GPU_RESOURCE_NAME + "/" + deviceFunctionMeta.model + "/" + HETEROGENEOUS_MEM_KEY;
    } else {
        YRLOG_WARN("{}|type: {} not supported, recheck the card type", createRequest.requestid(), type);
        return;
    }
    (*createRequest.mutable_schedulingops()->mutable_resources())[keyName] = deviceFunctionMeta.hbm;
}

/**
 * transfer from FunctionMeta's stream and latency to CreateRequest
 * @param createRequest: struct of CreateRequest. The stream and latency info will be stored in schedulingOps
 * @param funcMeta: function meta info
 */
static void LoadLatencyStreamToCreateRequest(CreateRequest &createRequest, const FunctionMeta &funcMeta)
{
    auto devieMetaData = funcMeta.extendedMetaData.deviceMetaData;
    auto cardType = devieMetaData.type;
    if (cardType.empty()) {
        return;
    }

    std::string latencyKey;
    if (cardType == NPU_RESOURCE_NAME) {
        latencyKey = NPU_RESOURCE_NAME + "/" + devieMetaData.model + "/" + HETEROGENEOUS_LATENCY_KEY;
    } else if (cardType == GPU_RESOURCE_NAME) {
        latencyKey = GPU_RESOURCE_NAME + "/" + devieMetaData.model + "/" + HETEROGENEOUS_LATENCY_KEY;
    } else {
        YRLOG_WARN("{}|type: {} not supported, recheck the card type", createRequest.requestid(), cardType);
        return;
    }

    (*createRequest.mutable_schedulingops()->mutable_resources())[latencyKey] = devieMetaData.latency;

    std::string streamKey;

    if (cardType == NPU_RESOURCE_NAME) {
        streamKey = NPU_RESOURCE_NAME + "/" + devieMetaData.model + "/" + HETEROGENEOUS_STREAM_KEY;
    } else if (cardType == GPU_RESOURCE_NAME) {
        streamKey = GPU_RESOURCE_NAME + "/" + devieMetaData.model + "/" + HETEROGENEOUS_STREAM_KEY;
    } else {
        YRLOG_WARN("{}|type: {} not supported, recheck the card type", createRequest.requestid(), cardType);
        return;
    }

    if (devieMetaData.stream <= 1) {
        (*createRequest.mutable_schedulingops()->mutable_resources())[streamKey] = 1;
    } else {
        (*createRequest.mutable_schedulingops()->mutable_resources())[streamKey] = MULTI_STREAM_DEFAULT_NUM;
    }
}

/**
 * Load Named Function meta to CreateRequest
 * @param createRequest: struct of CreateRequest
 * @param funcMeta function meta
 */
[[maybe_unused]] static void LoadDeviceFunctionMetaToCreateRequest(CreateRequest &createRequest,
                                                                   const FunctionMeta &funcMeta)
{
    // load xpu hbm resource
    LoadHbmToCreateRequest(createRequest, funcMeta);
    // load latency and stream resource
    LoadLatencyStreamToCreateRequest(createRequest, funcMeta);
}

/**
 * Setting CallRequest From CreateRequest
 * @param callRequest: struct of CallRequest
 * @param createReq: struct of CreateRequest
 * @param parentID: parent ID
 */
static void SetCallReq(runtime::CallRequest &callRequest, CreateRequest &createReq, const std::string &parentID)
{
    callRequest.set_traceid(createReq.traceid());
    callRequest.set_requestid(createReq.requestid());
    callRequest.set_function(createReq.function());
    callRequest.set_iscreate(true);
    callRequest.set_senderid(parentID);
    callRequest.mutable_args()->CopyFrom(*createReq.mutable_args());
}

static void SetInstanceInfoResources(::resources::InstanceInfo *instanceInfo, const CreateRequest &createReq)
{
    // InstanceInfo: resources
    auto resources = instanceInfo->mutable_resources()->mutable_resources();
    for (auto &r : createReq.schedulingops().resources()) {
        resource_view::Resource resource;
        resource.set_name(r.first);
        resource.set_type(resource_view::ValueType::Value_Type_SCALAR);
        resource.mutable_scalar()->set_value(r.second);
        (*resources)[r.first] = std::move(resource);
    }
}

[[maybe_unused]] static int64_t GetAffinityMaxScore(messages::ScheduleRequest *scheduleReq)
{
    int64_t optimalScore = 0;
    if (auto iter = scheduleReq->mutable_contexts()->find(LABEL_AFFINITY_PLUGIN);
            iter != scheduleReq->mutable_contexts()->end()) {
        optimalScore = iter->second.mutable_affinityctx()->maxscore();
    }
    return optimalScore;
}

static void SetAffinityWeight(affinity::Selector &selector, int64_t &optiomalScore)
{
    if (selector.condition().subconditions_size() == 0) {
        return;
    }

    if (selector.condition().orderpriority()) {
        for (int i = 0; i < selector.condition().subconditions_size() && i < PREFERRED_AFFINITY_SCORE_STEP; i++) {
            auto &label = (*selector.mutable_condition()->mutable_subconditions())[i];
            label.set_weight(MAX_PREFERRED_AFFINITY_SCORE - PREFERRED_AFFINITY_SCORE_STEP * i);
        }
        optiomalScore += MAX_PREFERRED_AFFINITY_SCORE;
        return;
    }

    int maxWeight = 0;
    for (int i = 0; i < selector.condition().subconditions_size(); i++) {
        auto &label = (*selector.mutable_condition()->mutable_subconditions())[i];
        auto weight = label.weight();
        if (weight == 0) {
            label.set_weight(MAX_PREFERRED_AFFINITY_SCORE);
        }
        maxWeight = (label.weight() > maxWeight) ? label.weight() : maxWeight;
    }
    optiomalScore += maxWeight;
}

[[maybe_unused]] static int GroupBinPackAffinity(const std::string &label, const std::string &value,
                                                 const common::GroupPolicy &policy,
                                                 resources::InstanceInfo &instanceInfo)
{
    auto grouplb = instanceInfo.mutable_scheduleoption()->mutable_affinity()->mutable_inner()->mutable_grouplb();
    if (policy == common::GroupPolicy::Spread) {
        auto antiAffinity = Selector(false, { { In(label, std::vector<std::string>{ value }) } });
        *grouplb->mutable_preferredantiaffinity() = std::move(antiAffinity);
        return MAX_PRIORITY_SCORE;
    }
    if (policy == common::GroupPolicy::Pack) {
        auto affinity = Selector(false, { { In(label, std::vector<std::string>{ value }) } });
        *grouplb->mutable_preferredaffinity() = std::move(affinity);
        return MAX_PRIORITY_SCORE;
    }
    if (policy == common::GroupPolicy::StrictSpread) {
        auto affinity = Selector(false, { { In(label, std::vector<std::string>{ value }) } });
        *grouplb->mutable_requiredantiaffinity() = std::move(affinity);
        return 0;
    }
    // None or StrictPack does not add any affinity
    return 0;
}

[[maybe_unused]] static void SetPreemptionAffinity(const std::shared_ptr<messages::ScheduleRequest> &scheduleReq)
{
    auto &preempt = *scheduleReq->mutable_instance()->mutable_scheduleoption()->mutable_affinity()->mutable_inner()
        ->mutable_preempt();
    int64_t optimalScore = GetAffinityMaxScore(scheduleReq.get());

    affinity::Selector affinity;
    affinity::Selector antiAffinity;
    std::string addLabel = "";
    if (scheduleReq->instance().scheduleoption().preemptedallowed()) {
        affinity = Selector(false, { { Exist(PREEMPTIBLE) } });
        antiAffinity = Selector(false, { { Exist(NOT_PREEMPTIBLE) } });
        addLabel = PREEMPTIBLE;  // to add label to nodelabels
        YRLOG_INFO("This instance is preemptible, add preemptible label to instance.");
    } else {
        affinity = Selector(false, { { Exist(NOT_PREEMPTIBLE) } });
        antiAffinity = Selector(false, { { Exist(PREEMPTIBLE) } });
        addLabel = NOT_PREEMPTIBLE;  // to add label to nodelabels
    }
    scheduleReq->mutable_instance()->mutable_labels()->Add(std::move(addLabel));
    (*affinity.mutable_condition()->mutable_subconditions())[0].set_weight(DEFAULT_PREEMPTION_WEIGHT);
    (*antiAffinity.mutable_condition()->mutable_subconditions())[0].set_weight(DEFAULT_PREEMPTION_WEIGHT);
    optimalScore += DEFAULT_PREEMPTION_WEIGHT + DEFAULT_PREEMPTION_WEIGHT;  // add affinity & antiAffinity weight
    (*scheduleReq->mutable_contexts())[LABEL_AFFINITY_PLUGIN].mutable_affinityctx()->set_maxscore(
        optimalScore);
    (*preempt.mutable_preferredaffinity()) = std::move(affinity);
    (*preempt.mutable_preferredantiaffinity()) = std::move(antiAffinity);
}

[[maybe_unused]] inline static void SetResourceGroupAffinity(::resources::InstanceInfo &instanceInfo)
{
    auto scheduleOpt = instanceInfo.mutable_scheduleoption();
    if (scheduleOpt->rgroupname().empty() || scheduleOpt->rgroupname() == PRIMARY_TAG) {
        return;
    }
    auto rgRequired = scheduleOpt->mutable_affinity()->mutable_inner()->mutable_rgroup()->mutable_requiredaffinity();
    *rgRequired = Selector(true, { { In(RGROUP, { scheduleOpt->rgroupname() }) } });
}

static void SetAffinityOpt(::resources::InstanceInfo &instanceInfo, const CreateRequest &createReq,
                           messages::ScheduleRequest *schedReq)
{
    auto scheduleOpt = instanceInfo.mutable_scheduleoption();
    (*scheduleOpt->mutable_affinity()->mutable_resource()) = createReq.schedulingops().scheduleaffinity().resource();
    (*scheduleOpt->mutable_affinity()->mutable_instance()) = createReq.schedulingops().scheduleaffinity().instance();

    auto &resourceAffinity = *scheduleOpt->mutable_affinity()->mutable_resource();
    auto &instanceAffinity = *scheduleOpt->mutable_affinity()->mutable_instance();
    int64_t optimalScore = 0;
    if (resourceAffinity.has_preferredaffinity()) {
        SetAffinityWeight(*(resourceAffinity.mutable_preferredaffinity()), optimalScore);
    }
    if (resourceAffinity.has_preferredantiaffinity()) {
        SetAffinityWeight(*(resourceAffinity.mutable_preferredantiaffinity()), optimalScore);
    }
    if (resourceAffinity.has_requiredaffinity() && resourceAffinity.requiredaffinity().condition().orderpriority()) {
        SetAffinityWeight(*(resourceAffinity.mutable_requiredaffinity()), optimalScore);
    }
    if (resourceAffinity.has_requiredantiaffinity() &&
        resourceAffinity.requiredantiaffinity().condition().orderpriority()) {
        SetAffinityWeight(*(resourceAffinity.mutable_requiredantiaffinity()), optimalScore);
    }

    if (instanceAffinity.has_preferredaffinity()) {
        SetAffinityWeight(*(instanceAffinity.mutable_preferredaffinity()), optimalScore);
    }
    if (instanceAffinity.has_preferredantiaffinity()) {
        SetAffinityWeight(*(instanceAffinity.mutable_preferredantiaffinity()), optimalScore);
    }
    if (instanceAffinity.has_requiredaffinity() && instanceAffinity.requiredaffinity().condition().orderpriority()) {
        SetAffinityWeight(*(instanceAffinity.mutable_requiredaffinity()), optimalScore);
    }
    if (instanceAffinity.has_requiredantiaffinity() &&
        instanceAffinity.requiredantiaffinity().condition().orderpriority()) {
        SetAffinityWeight(*(instanceAffinity.mutable_requiredantiaffinity()), optimalScore);
    }

    (*schedReq->mutable_contexts())[LABEL_AFFINITY_PLUGIN].mutable_affinityctx()->set_maxscore(
        optimalScore);
    auto topo = scheduleOpt->schedpolicyname() == MONOPOLY_SCHEDULE ? affinity::NODE : affinity::POD;
    scheduleOpt->mutable_affinity()->mutable_instance()->set_scope(topo);
    SetResourceGroupAffinity(instanceInfo);
}

static void SetAffinityOpt(::resources::InstanceInfo &instanceInfo, const CreateRequest &createReq,
                           std::shared_ptr<messages::ScheduleRequest> &schedReq)
{
    SetAffinityOpt(instanceInfo, createReq, schedReq.get());
}

static void SetInstanceInfoScheduleOptions(::resources::InstanceInfo *instanceInfo, const CreateRequest &createReq,
                                           const runtime::CallRequest &callRequest)
{
    auto scheduleOpt = instanceInfo->mutable_scheduleoption();
    // priority schedule
    scheduleOpt->set_priority(createReq.schedulingops().priority());
    // currently using 18s as default timeout
    // will deprecated if scheduleTimeout supported by sdk
    scheduleOpt->set_scheduletimeoutms(createReq.schedulingops().scheduletimeoutms() == 0
                                           ? DEFAULT_SCHEDULE_TIMEOUT_MS
                                           : createReq.schedulingops().scheduletimeoutms());
    scheduleOpt->set_preemptedallowed(createReq.schedulingops().preemptedallowed());
    // scheduleAffinity
    auto createAffinityInfo = createReq.schedulingops().affinity();
    auto scheduleAffinity = scheduleOpt->mutable_affinity()->mutable_instanceaffinity()->mutable_affinity();
    for (auto &a : createAffinityInfo) {
        (*scheduleAffinity)[a.first] = (resource_view::AffinityType)(int32_t)a.second;
    }
    // instance range
    const auto &range = createReq.schedulingops().range();
    *(scheduleOpt->mutable_range()) = range;

    const auto &extension = createReq.schedulingops().extension();
    *(scheduleOpt->mutable_extension()) = extension;

    // policy name
    if (auto iter(extension.find(SCHEDULE_POLICY)); iter != extension.end()) {
        scheduleOpt->set_schedpolicyname(iter->second);
    }

    // node selector
    if (auto it(extension.find(NODE_SELECTOR)); it != extension.end()) {
        nlohmann::json nodeSelectorJson;
        try {
            nodeSelectorJson = nlohmann::json::parse(it->second);
        } catch (nlohmann::json::parse_error &e) {
            YRLOG_ERROR(
                "failed to parse node selectors, maybe not a valid json, reason: {}, id: {}, byte position: {}. Origin "
                "string: {}",
                e.what(), e.id, e.byte, it->second);
        }
        for (auto &nsIt : nodeSelectorJson.items()) {
            (*scheduleOpt->mutable_nodeselector())[nsIt.key()] = nodeSelectorJson.at(nsIt.key());
        }
    }

    // init timeout
    const auto &createOptions = callRequest.createoptions();
    if (auto iter(createOptions.find(INIT_CALL_TIMEOUT)); iter != createOptions.end()) {
        uint32_t timeout = 0;
        std::stringstream ss(iter->second);
        ss >> timeout;  // std::string to uint32
        scheduleOpt->set_initcalltimeout(timeout);
    }

    scheduleOpt->set_target(resources::CreateTarget::INSTANCE);
    scheduleOpt->set_rgroupname(createReq.schedulingops().rgroupname());
}

static void SetGracefulShutdownTime(::resources::InstanceInfo *instanceInfo, const runtime::CallRequest &callRequest)
{
    if (callRequest.createoptions().find("GRACEFUL_SHUTDOWN_TIME") == callRequest.createoptions().end()) {
        instanceInfo->set_gracefulshutdowntime(-1);
        return;
    }
    YRLOG_DEBUG("GRACEFUL_SHUTDOWN_TIME in create option is {}",
                callRequest.createoptions().at("GRACEFUL_SHUTDOWN_TIME"));
    try {
        auto time = stol(callRequest.createoptions().at("GRACEFUL_SHUTDOWN_TIME"));
        instanceInfo->set_gracefulshutdowntime(time);
    } catch (std::exception &e) {
        YRLOG_ERROR("failed to parse GRACEFUL_SHUTDOWN_TIME, {}", e.what());
        instanceInfo->set_gracefulshutdowntime(-1);
    }
}

static int GetRuntimeRecoverTimes(const resources::InstanceInfo &instanceInfo)
{
    auto iter = instanceInfo.createoptions().find(RECOVER_RETRY_TIMES_KEY);
    if (iter == instanceInfo.createoptions().end()) {
        return 0;
    }

    try {
        int recoverTimes = std::stoi(iter->second);
        return recoverTimes;
    } catch (std::exception &e) {
        return 0;
    }
}

[[maybe_unused]] inline uint64_t GetRuntimeRecoverTimeout(const resources::InstanceInfo &instanceInfo)
{
    auto iter = instanceInfo.createoptions().find(RECOVER_RETRY_TIMEOUT_KEY);
    if (iter == instanceInfo.createoptions().end()) {
        return DEFAULT_RECOVER_TIMEOUT_MS ;
    }

    try {
        return static_cast<uint64_t>(std::stoi(iter->second));
    } catch (std::exception &e) {
        return DEFAULT_RECOVER_TIMEOUT_MS ;
    }
}

// Checks if there are any heterogeneous resources of numeric types (e.g., hbm, device id, latency, stream).
[[maybe_unused]] static bool HasHeteroResourceNumeric(const resources::ResourceUnit &unit, const std::string cardType,
                                                      const std::string resourceType)
{
    auto resource = unit.capacity().resources().find(cardType);
    if (resource == unit.capacity().resources().end() ||
        resource->second.vectors().values().find(resourceType) == resource->second.vectors().values().end()) {
        return false;
    }
    return true;
};

//  Checks if there are any heterogeneous resources of string type. (e.g., device ip).
[[maybe_unused]] static bool HasHeteroResourceString(const resources::ResourceUnit &unit,
                                                     const std::string cardType, const std::string resourceType)
{
    auto resource = unit.capacity().resources().find(cardType);
    if (resource == unit.capacity().resources().end() ||
        resource->second.heterogeneousinfo().find(resourceType) == resource->second.heterogeneousinfo().end()) {
        return false;
    }
    return true;
};

[[maybe_unused]] static bool HasHeteroResourceInResources(const resources::Resources &resources,
                                                          const std::string cardType, const std::string resourceType)
{
    if (resources.resources().find(cardType) == resources.resources().end()) {
        return false;
    }
    if (resources.resources().at(cardType).vectors().values().find(resourceType) ==
        resources.resources().at(cardType).vectors().values().end()) {
        return false;
    }
    return true;
}

[[maybe_unused]] static std::string GetHeteroCardTypeFromResName(const std::string &resourceName)
{
    auto resourceNameFields = litebus::strings::Split(resourceName, "/");
    // heterogeneous resource name is like: NPU/310/memory or GPU/cuda/count...
    if (resourceNameFields.size() != HETERO_RESOURCE_FIELD_NUM) {
        return "";
    }
    return resourceNameFields[VENDOR_IDX] + "/" + resourceNameFields[PRODUCT_INDEX];
}

[[maybe_unused]] static std::string GetHeteroCardType(const resources::InstanceInfo &instance)
{
    std::string cardType = "";
    for (auto &req : instance.resources().resources()) {
        cardType = GetHeteroCardTypeFromResName(req.first);
        if (!cardType.empty()) {
            break;
        }
    }
    return cardType;
};

[[maybe_unused]] static std::vector<std::string> GetDeviceIps(const resources::ResourceUnit &unit,
                                                              const std::string cardType)
{
    if (!HasHeteroResourceString(unit, cardType, resource_view::DEV_CLUSTER_IPS_KEY)) {
        YRLOG_WARN("unit({}) does not have dev_cluster_ips", unit.id());
        return std::vector<std::string>{};
    }
    std::string deviceIpsString = unit.capacity().resources().at(cardType).heterogeneousinfo().at(
        resource_view::DEV_CLUSTER_IPS_KEY);
    deviceIpsString.erase(std::remove(deviceIpsString.begin(), deviceIpsString.end(), '\n'), deviceIpsString.end());
    auto deviceIps = litebus::strings::Split(deviceIpsString, ",");
    return deviceIps;
}

//  Check if the request requires heterogeneous resources.
[[maybe_unused]] static bool IsHeterogeneousRequest(const std::shared_ptr<messages::ScheduleRequest> &request)
{
    auto instance = request->instance();
    bool isHeteroReq = !GetHeteroCardType(instance).empty();
    return isHeteroReq;
}

//  Checks if there are any heterogeneous requests.
[[maybe_unused]] static bool HasHeterogeneousRequest(std::vector<std::shared_ptr<messages::ScheduleRequest>> &requests)
{
    for (auto &req : requests) {
        if (IsHeterogeneousRequest(req)) {
            return true;
        }
    }
    return false;
}

//  Checks if there are any resource group requests.
[[maybe_unused]] static bool HasResourceGroupRequest(std::vector<std::shared_ptr<messages::ScheduleRequest>> &requests)
{
    for (auto &req : requests) {
        if (req->instance().scheduleoption().target() == resources::CreateTarget::RESOURCE_GROUP) {
            return true;
        }
    }
    return false;
}

[[maybe_unused]] inline void SetScheduleReqFunctionAgentIDAndHeteroConfig(
    const std::shared_ptr<messages::ScheduleRequest> &scheduleReq, const schedule_decision::ScheduleResult &result)
{
    scheduleReq->mutable_instance()->set_functionagentid(result.id);
    scheduleReq->mutable_instance()->set_unitid(result.unitID);
    // only set once
    scheduleReq->mutable_instance()->clear_schedulerchain();
    (*scheduleReq->mutable_instance()->mutable_schedulerchain()->Add()) = result.id;

    // set hetero device IDs
    auto deviceIDs = result.realIDs;
    if (deviceIDs.empty() || deviceIDs[0] == -1) {
        return;
    }

    auto required = scheduleReq->instance().resources().resources();
    for (const auto &res : required) {
        auto resourceNameFields = litebus::strings::Split(res.first, "/");
        if (resourceNameFields.size() != HETERO_RESOURCE_FIELD_NUM) {
            continue;
        }
        std::string vendor = resourceNameFields[VENDOR_IDX];
        auto createOpt = scheduleReq->mutable_instance()->mutable_createoptions();
        std::string deviceIDsStr;
        for (auto deviceID : deviceIDs) {
            deviceIDsStr += (std::to_string(deviceID) + ",");
        }
        (void)deviceIDsStr.erase(deviceIDsStr.length() - 1);
        (*createOpt)["func-" + vendor + "-DEVICE-IDS"] = deviceIDsStr;
        YRLOG_INFO("{}|{}: {} will be allocated to instance: {}", vendor, scheduleReq->requestid(), deviceIDsStr,
                   scheduleReq->instance().instanceid());
    }

    // add hetero schedule result to instance info (for instance recover)
    auto *resources = scheduleReq->mutable_instance()->mutable_resources()->mutable_resources();
    for (const auto &allocated : result.allocatedVectors) {
        auto *vectors = (*resources)[allocated.first].mutable_vectors();
        (*resources)[allocated.first].set_name(allocated.first);
        (*resources)[allocated.first].set_type(resource_view::ValueType::Value_Type_VECTORS);
        for (const auto &value : allocated.second.values()) {
            (*vectors->mutable_values())[value.first] = value.second;
        }
    }
}

// Checks if the Heterogeneous product regex syntax is valid.
[[maybe_unused]] inline static bool IsHeteroProductRegexValid(const std::string &productRegex)
{
    try {
        std::regex re(productRegex);
        return true;
    } catch (const std::regex_error&) {
        return false;
    }
}

[[maybe_unused]] inline static std::string GetResourceCardTypeByRegex(const resources::Resources &resources,
                                                                      const std::string cardTypeRegex)
{
    std::string cardType = "";
    if (!IsHeteroProductRegexValid(cardTypeRegex)) {
        YRLOG_ERROR("Heterogeneous product regex syntax error: {}.", cardTypeRegex);
        return cardType;
    }

    std::regex resourceTypePattern = std::regex(cardTypeRegex);
    for (const auto &pair : resources.resources()) {
        if (std::regex_match(pair.first, resourceTypePattern)
            && pair.second.type() == resource_view::ValueType::Value_Type_VECTORS) {
            cardType = pair.first;
            return cardType;
        }
    }
    return cardType;
}

/**
 * This function generates instance rank IDs based on the device's IP and ID,
 * and is used during the generation of SFMD function group running information.
 * @param insDeviceIpMap: A mapping where the key is an instance, and the value is the IPs of devices it uses.
 * @param deviceIP2DeviceRankIdMap: A mapping table that stores device IPs and their corresponding device rank id.
 * @param insRankIdMap: A mapping of instanceId to their instance rank id, as the final result.
 */
[[maybe_unused]] static void GenerateInsRankId(
    const std::unordered_map<std::string, std::vector<std::string>> &insDeviceIpMap,
    const std::unordered_map<std::string, int> &deviceIP2DeviceRankIdMap,
    std::unordered_map<std::string, int> &insRankIdMap)
{
    std::vector<std::pair<std::string, std::set<int>>> insDeviceRankIdMap{};
    for (const auto& [instanceId, deviceIps] : insDeviceIpMap) {
        std::set<int> rankIds;
        for (auto &deviceIp : deviceIps) {
            auto rankId = deviceIP2DeviceRankIdMap.at(deviceIp);
            rankIds.insert(rankId);
        }
        insDeviceRankIdMap.push_back({instanceId, rankIds});
    }

    std::sort(insDeviceRankIdMap.begin(), insDeviceRankIdMap.end(), [](const auto &a, const auto &b) {
        return a.second < b.second;
    });

    for (size_t i = 0; i < insDeviceRankIdMap.size(); ++i) {
        insRankIdMap[insDeviceRankIdMap[i].first] = static_cast<int>(i);
    }
}

[[maybe_unused]] inline static bool IsRuntimeRecoverEnable(
    const resources::InstanceInfo &instanceInfo,
    const litebus::Future<std::string> &cancelTag = litebus::Future<std::string>())
{
    if (cancelTag.IsOK()) {
        return false;
    }
    // runtime is recover-able only when RECOVER_RETRY_TIMES > 0
    return GetRuntimeRecoverTimes(instanceInfo) > 0;
}

[[maybe_unused]] static bool IsFrontendFunction(const std::string &function)
{
    if (function.size() < FAAS_FRONTEND_FUNCTION_NAME_PREFIX.size()) {
        return false;
    }
    return function.substr(0, FAAS_FRONTEND_FUNCTION_NAME_PREFIX.size()) == FAAS_FRONTEND_FUNCTION_NAME_PREFIX;
}

[[maybe_unused]] static bool IsCreateByFrontend(const std::shared_ptr<InstanceInfo> &info)
{
    if (info->extensions().find(CREATE_SOURCE) == info->extensions().end()) {
        return false;
    }
    return info->extensions().at(CREATE_SOURCE) == FRONTEND_STR;
}

[[maybe_unused]] static bool IsDriver(const InstanceInfo &info)
{
    if (info.instanceid().find("driver") != std::string::npos) {
        return true;
    }
    if (auto iter = info.extensions().find(CREATE_SOURCE);
        iter != info.extensions().end() && iter->second == "driver") {
        return true;
    }
    return false;
}

[[maybe_unused]] static bool IsDriver(const std::shared_ptr<InstanceInfo> &info)
{
    return IsDriver(*info);
}

static void SetInstanceInfo(::resources::InstanceInfo *instanceInfo, CreateRequest &createReq,
                            const runtime::CallRequest &callRequest, const std::string &parentID)
{
    // InstanceInfo: instanceID
    instanceInfo->set_instanceid(createReq.designatedinstanceid());
    // InstanceInfo: requestID
    instanceInfo->set_requestid(createReq.requestid());
    // function
    instanceInfo->set_function(createReq.function());
    instanceInfo->set_parentid(parentID);
    // InstanceInfo create options
    auto createOptions = callRequest.createoptions();
    *instanceInfo->mutable_createoptions() = createOptions;

    // set after CreateOptions
    instanceInfo->set_scheduletimes(GetRuntimeRecoverTimes(*instanceInfo) > 0 ? GetRuntimeRecoverTimes(*instanceInfo)
                                                                              : DEFAULT_RESCHEDULE_TIME);
    instanceInfo->set_deploytimes(DEFAULT_REDEPLOY_TIME);

    if (createOptions.find("lifecycle") != createOptions.end()) {
        YRLOG_DEBUG("instance({}) create options include lifecycle detached", createReq.designatedinstanceid());
        instanceInfo->set_detached(createOptions["lifecycle"] == "detached");
    }

    instanceInfo->mutable_args()->CopyFrom(*createReq.mutable_args());

    // Instance status code 0 means InstanceState::NEW, should modify it after StateMachine move to common directory.
    instanceInfo->mutable_instancestatus()->set_code(0);
    instanceInfo->mutable_instancestatus()->set_msg("new instance");

    // InstanceInfo: resources
    SetInstanceInfoResources(instanceInfo, createReq);

    // InstanceInfo: scheduleOption
    SetInstanceInfoScheduleOptions(instanceInfo, createReq, callRequest);

    // InstanceInfo: labels
    auto labels = instanceInfo->mutable_labels();
    *labels = createReq.labels();

    instanceInfo->set_version(INSTANCE_INIT_VERSION);

    SetGracefulShutdownTime(instanceInfo, callRequest);
    (*instanceInfo->mutable_extensions())[NAMED] = createReq.designatedinstanceid().empty() ? "false" : "true";
}

/**
 * transfer from CreateRequest to ScheduleRequest
 * @param createReq: struct of CreateRequest
 * @return struct of ScheduleRequest
 */
[[maybe_unused]] static std::shared_ptr<messages::ScheduleRequest> TransFromCreateReqToScheduleReq(
    CreateRequest &&createReq, const std::string &parentID)
{
    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();
    // ScheduleRequest traceID
    scheduleReq->set_traceid(createReq.traceid());
    // ScheduleRequest requestID
    scheduleReq->set_requestid(createReq.requestid());
    scheduleReq->set_scheduleround(0);
    // initRequest
    runtime::CallRequest callRequest;
    SetCallReq(callRequest, createReq, parentID);
    *callRequest.mutable_createoptions() = std::move(*createReq.mutable_createoptions());
    scheduleReq->set_initrequest(callRequest.SerializeAsString());

    // set InstanceInfo
    auto instanceInfo = scheduleReq->mutable_instance();
    SetInstanceInfo(instanceInfo, createReq, callRequest, parentID);
    SetAffinityOpt(*instanceInfo, createReq, scheduleReq);
    // Set Instance reliability
    instanceInfo->set_lowreliability(IsLowReliabilityInstance(*instanceInfo));
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    (*instanceInfo->mutable_extensions())[RECEIVED_TIMESTAMP] = std::to_string(
        static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count()));
    return scheduleReq;
}

/**
 * transfer from ScheduleResponse to CreateResponse
 * @param rsp: ScheudleResponse
 * @return CreateResonse
 */
[[maybe_unused]] static CreateResponse TransFromScheduleRspToCreateRsp(const messages::ScheduleResponse &rsp)
{
    CreateResponse createRsp;
    createRsp.set_code(Status::GetPosixErrorCode(
        static_cast<StatusCode>(rsp.code())));  // need to trans from functionsystem code to posix code
    createRsp.set_message(rsp.message());
    createRsp.set_instanceid(rsp.instanceid());
    return createRsp;
}

/**
 * extract proxyid from proxyaid
 * @param rsp: proxyAID
 * @return proxyID
 */
[[maybe_unused]] static std::string ExtractProxyIDFromProxyAID(std::string proxyAID)
{
    auto keyItems = litebus::strings::Split(proxyAID, functionsystem::LOCAL_SCHED_INSTANCE_CTRL_ACTOR_NAME_POSTFIX);
    if (keyItems.size() != LOCAL_SPLIT_SIZE) {
        return "";
    }
    return keyItems[0];
}

[[maybe_unused]] static std::string GenerateRuntimeID(const std::string &instanceID)
{
    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    if (instanceID.empty()) {
        return RUNTIME_UUID_PREFIX + uuid.ToString();
    }
    auto splits = litebus::strings::Split(uuid.ToString(), "-");
    return RUNTIME_UUID_PREFIX + instanceID + "-" + splits[splits.size() - 1];
}

/**
 * whether instance is app driver or not
 * @param createOpts create options of instance
 * @return true - app driver  false - not app driver
 */
[[maybe_unused]] static bool IsAppDriver(const ::google::protobuf::Map<std::string, std::string> &createOpts)
{
    if (createOpts.find(APP_ENTRYPOINT) != createOpts.end()) {
        return true;
    }
    return false;
}

// judge after BuildDeployerParameters
[[maybe_unused]] static bool ContainsWorkingDirLayer(
    const ::google::protobuf::Map<std::string, std::string> &createOpts)
{
    if (createOpts.find(UNZIPPED_WORKING_DIR) != createOpts.end()) {
        return true;
    }
    return false;
}

[[maybe_unused]] inline int64_t GetModRevisionFromInstanceInfo(const resources::InstanceInfo &instanceInfo)
{
    auto iter = instanceInfo.extensions().find(INSTANCE_MOD_REVISION);
    if (iter == instanceInfo.extensions().end()) {
        return 0;
    }
    try {
        return std::stoll(iter->second);
    } catch (std::exception &e) {
        YRLOG_WARN("failed to get mod revision {} from instance({})", instanceInfo.instanceid(), iter->second);
    }
    return 0;
}

[[maybe_unused]] inline bool IsDebugInstance(const ::google::protobuf::Map<std::string, std::string> &createOpts)
{
    // debug config key not found
    if (createOpts.find(std::string(YR_DEBUG_CONFIG)) == createOpts.end()) {
        return false;
    }
    return true;
}

[[maybe_unused]] inline bool IsInstanceIdSecure(const std::string &instanceID)
{
    // unsafe special characters：['\"', '\'', ';', '\\', '|', '&', '$', '>', '<', '`']
    std::regex unsafePattern(R"([\"';\\|&$><`])");
    return !std::regex_search(instanceID, unsafePattern);
}
}  // namespace functionsystem

#endif  // COMMON_UTILS_STRUCT_TRANSFER_H
