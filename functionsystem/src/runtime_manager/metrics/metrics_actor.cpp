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

#include "metrics_actor.h"

#include <async/asyncafter.hpp>
#include <async/async.hpp>
#include <async/defer.hpp>
#include <utils/os_utils.hpp>
#include <nlohmann/json.hpp>

#include "collector/custom_resource_collector.h"
#include "collector/instance_cpu_collector.h"
#include "collector/instance_memory_collector.h"
#include "collector/resource_labels_collector.h"
#include "collector/system_cpu_collector.h"
#include "collector/system_memory_collector.h"
#include "collector/system_proc_cpu_collector.h"
#include "collector/system_proc_memory_collector.h"
#include "collector/system_xpu_collector.h"
#include "collector/node_cpu_collector.h"
#include "collector/node_memory_collector.h"
#include "logs/logging.h"
#include "proto/pb/message_pb.h"
#include "proto/pb/posix/resource.pb.h"
#include "common/utils/exec_utils.h"
#include "manager/runtime_manager.h"

namespace functionsystem::runtime_manager {

const int32_t UPDATE_RUNTIME_STATUS_RETRY_DURATION = 1000;  // MS
const std::set<std::string> NPU_COLLECT_SET = { NPU_COLLECT_COUNT, NPU_COLLECT_HBM, NPU_COLLECT_SFMD,
                                                NPU_COLLECT_TOPO, NPU_COLLECT_ALL };

bool IsValidMonitorPath(const std::string &path)
{
    if (std::regex_match(path, std::regex("^/[a-zA-Z/]+"))) {
        return true;
    }
    YRLOG_WARN("invalid monitor path: {}", path);
    return false;
}

MetricsActor::MetricsActor(const std::string &name) : ActorBase(name)
{
}

void MetricsActor::Init()
{
    YRLOG_INFO("init MetricsActor {}", ActorBase::GetAID().Name());
    procFSTools_ = std::make_shared<ProcFSTools>();
    ActorBase::Receive("UpdateRuntimeStatusResponse", &MetricsActor::UpdateRuntimeStatusResponse);
}

void MetricsActor::Finalize()
{
    YRLOG_INFO("finalize MetricsActor {}", ActorBase::GetAID().Name());
}

void MetricsActor::AddSystemMetricsCollector(const Flags &flags)
{
    YRLOG_INFO("create system resource collectors.");
    std::shared_ptr<BaseMetricsCollector> systemCPUCollector;
    std::shared_ptr<BaseMetricsCollector> systemMemoryCollector;

    if (metricsConfig_.metricsCollectorType == "proc") {
        auto callback = [func = std::bind(&MetricsActor::GenAllMetricsWithoutSystem,
                                          this)]() -> std::vector<litebus::Future<Metrics>> { return func(); };
        systemCPUCollector = std::make_shared<SystemProcCPUCollector>(metricsConfig_.procMetricsCPU, callback);
        systemMemoryCollector = std::make_shared<SystemProcMemoryCollector>(metricsConfig_.procMetricsMemory, callback);
    } else if (metricsConfig_.metricsCollectorType == "node") {
        systemCPUCollector = std::make_shared<NodeCPUCollector>(procFSTools_, metricsConfig_.overheadCPU);
        systemMemoryCollector = std::make_shared<NodeMemoryCollector>(procFSTools_, metricsConfig_.overheadMemory);
    } else {
        systemCPUCollector = std::make_shared<SystemCPUCollector>(procFSTools_);
        systemMemoryCollector = std::make_shared<SystemMemoryCollector>(procFSTools_);
    }
    auto resourceLabelCollector = std::make_shared<ResourceLabelsCollector>(flags.GetResourceLabelPath());

    filter_[systemCPUCollector->GenFilter()] = systemCPUCollector;
    filter_[systemMemoryCollector->GenFilter()] = systemMemoryCollector;
    filter_[resourceLabelCollector->GenFilter()] = resourceLabelCollector;

    if (flags.GetGpuCollectionEnable()) {
        auto params = std::make_shared<XPUCollectorParams>();
        params->ldLibraryPath = metricsConfig_.heteroLdLibraryPath;
        params->deviceInfoPath = flags.GetNpuDeviceInfoPath();
        std::shared_ptr<BaseMetricsCollector> systemGpuCollector =
            std::make_shared<SystemXPUCollector>(nodeID, metrics_type::GPU, procFSTools_, params);
        filter_[systemGpuCollector->GenFilter()] = std::move(systemGpuCollector);
    }
    if (NPU_COLLECT_SET.find(flags.GetNpuCollectionMode()) != NPU_COLLECT_SET.end()) {
        auto params = std::make_shared<XPUCollectorParams>();
        params->ldLibraryPath = metricsConfig_.heteroLdLibraryPath;
        params->deviceInfoPath = flags.GetNpuDeviceInfoPath();
        params->collectMode = flags.GetNpuCollectionMode();
        std::shared_ptr<BaseMetricsCollector> systemNpuCollector =
            std::make_shared<SystemXPUCollector>(nodeID, metrics_type::NPU, procFSTools_, params);
        filter_[systemNpuCollector->GenFilter()] = std::move(systemNpuCollector);
    }
    ResolveCustomResourceMetricsCollector(flags.GetCustomResources());
}

void MetricsActor::ResolveCustomResourceMetricsCollector(const std::string &customResource)
{
    if (customResource.empty()) {
        return;
    }
    nlohmann::json parser;
    try {
        parser = nlohmann::json::parse(customResource);
    } catch (nlohmann::json::parse_error &e) {
        YRLOG_WARN("failed to parse custom to json, error: {}", e.what());
        return;
    }
    for (const auto &item : parser.items()) {
        try {
            auto value = item.value().get<double>();
            auto collector = std::make_shared<CustomResourceCollector>(item.key(), value);
            if (filter_.find(collector->GenFilter()) != filter_.end()) {
                YRLOG_WARN("custom resource key({}) is duplicated.", item.key());
                continue;
            }
            YRLOG_INFO("resolved custom resource key({}) value({}).", item.key(), value);
            filter_[collector->GenFilter()] = collector;
        } catch (std::exception &e) {
            YRLOG_WARN("invalid custom resource key({}) value type, error: {}", item.key(), e.what());
            continue;
        }
    }
}

Status MetricsActor::AddInstance(const messages::RuntimeInstanceInfo &instanceInfo, const pid_t &pid,
                                 const double &cpuLimit, const double &memLimit)
{
    // map["deployDir-instanceId-Cpu"] = InstanceCPUCollector
    const std::string &deployDir = instanceInfo.deploymentconfig().deploydir();
    const std::string &instanceID = instanceInfo.instanceid();
    auto instanceCPUCollector =
        std::make_shared<InstanceCPUCollector>(pid, instanceID, cpuLimit, deployDir, procFSTools_);
    filter_[instanceCPUCollector->GenFilter()] = instanceCPUCollector;

    // map["deployDir-instanceId-Memory"] = InstanceMemoryCollector
    // if enable, instance memory collected and reported by runtime OOM monitor at different frequencies.
    auto instanceMemoryCollector =
        std::make_shared<InstanceMemoryCollector>(pid, instanceID, memLimit, deployDir, procFSTools_);
    filter_[instanceMemoryCollector->GenFilter()] = instanceMemoryCollector;
    if (runtimeOomMonitorConfig_.enable) {
        runtimeMemoryLimitCollector_  = instanceMemoryCollector;
    }

    instanceInfos_[instanceID] = instanceInfo;
    return Status(StatusCode::SUCCESS);
}

Status MetricsActor::DeleteInstance(const std::string &deployDir, const std::string &instanceID)
{
    YRLOG_INFO("delete instance collectors.");
    for (const auto &type : metrics_type::METRICS_TYPES) {
        auto filter = litebus::os::Join(litebus::os::Join(deployDir, instanceID, '-'), type, '-');
        if (filter_.find(filter) != filter_.end() && !filter_.erase(filter)) {
            YRLOG_ERROR("filter {} is not found or erase failed.", filter);
        }
    }
    (void)instanceInfos_.erase(instanceID);
    return Status(StatusCode::SUCCESS);
}

void MetricsActor::StartUpdateMetrics()
{
    YRLOG_DEBUG_COUNT_60("update metrics.");
    auto allMetrics = GenAllMetrics();
    (void)Send(agentAid_, "UpdateResources", BuildUpdateMetricsRequest(allMetrics));
    // UPDATE_METRICS_DURATION should be configurable
    updateMetricsTimer_ = litebus::AsyncAfter(UPDATE_METRICS_DURATION, GetAID(), &MetricsActor::StartUpdateMetrics);
}

void MetricsActor::StopUpdateMetrics()
{
    (void)litebus::TimerTools::Cancel(updateMetricsTimer_);
}

resources::ResourceUnit MetricsActor::GetResourceUnit()
{
    return BuildResourceUnit(GenAllMetrics());
}

void MetricsActor::SetConfig(const Flags &flags)
{
    metricsConfig_.metricsCollectorType = flags.GetMetricsCollectorType();
    metricsConfig_.procMetricsCPU = flags.GetProcMetricsCPU();
    metricsConfig_.procMetricsMemory = flags.GetProcMetricsMemory();
    metricsConfig_.overheadCPU = flags.GetOverheadCPU();
    metricsConfig_.overheadMemory = flags.GetOverheadMemory();
    metricsConfig_.heteroLdLibraryPath = flags.GetRuntimeLdLibraryPath();
    if (flags.GetSnuserDirSizeLimit() >= 0) {
        DiskUsageMonitorConfig config{
            .description = "snuser dir",
            .checkDiskUsageLimit = flags.GetSnuserDirSizeLimit(),
        };
        if (CheckIllegalChars(flags.GetRuntimeHomeDir())) {
            config.checkDiskUsageDirs.push_back(flags.GetRuntimeHomeDir());
        }
        diskUsageMonitorConfigs_.push_back(std::move(config));
    }
    if (flags.GetTmpDirSizeLimit() >= 0) {
        DiskUsageMonitorConfig config{
            .description = "tmp dir",
            .checkDiskUsageLimit = flags.GetTmpDirSizeLimit(),
        };
        config.checkDiskUsageDirs.push_back("/tmp");
        config.checkDiskUsageDirs.push_back("/var/tmp");
        diskUsageMonitorConfigs_.push_back(std::move(config));
    }
    if (!flags.GetDiskUsageMonitorPath().empty() && flags.GetDiskUsageLimit() >= 0) {
        DiskUsageMonitorConfig config{
            .description = flags.GetDiskUsageMonitorPath(),
            .checkDiskUsageLimit = flags.GetDiskUsageLimit(),
        };
        auto filePaths = litebus::strings::Split(flags.GetDiskUsageMonitorPath(), ";");
        for (auto file : filePaths) {
            if (!file.empty() && IsValidMonitorPath(file)) {
                config.checkDiskUsageDirs.push_back(file);
                YRLOG_INFO("add dir {} to monitor", file);
            }
        }
        diskUsageMonitorConfigs_.push_back(std::move(config));
    }
    checkDiskUsageMonitorDuration_ = flags.GetDiskUsageMonitorDuration();
    diskUsageMonitorNotifyFailureEnable_ = flags.GetDiskUsageMonitorNotifyFailureEnable();
    if (flags.GetOomKillEnable()) {
        runtimeOomMonitorConfig_.memoryDetectionInterval = flags.GetMemoryDetectionInterval();
        runtimeOomMonitorConfig_.enable = true;
        runtimeOomMonitorConfig_.controlLimit = flags.GetOomKillControlLimit();
        runtimeOomMonitorConfig_.consecutiveDetectionCount = flags.GetOomConsecutiveDetectionCount();
    }
    nodeID = flags.GetNodeID();
    AddSystemMetricsCollector(flags);
}

void MetricsActor::SetRuntimeMemoryExceedLimitCallback(
    const RuntimeMemoryExceedLimitCallbackFunc &runtimeMemoryExceedLimitCallback)
{
    runtimeMemoryExceedLimitCallback_ = runtimeMemoryExceedLimitCallback;
}

std::vector<litebus::Future<Metrics>> MetricsActor::GenAllMetrics() const
{
    std::vector<litebus::Future<Metrics>> metrics;
    for (const auto &iter : filter_) {
        metrics.push_back(iter.second->GetMetrics());
    }
    return metrics;
}

std::vector<litebus::Future<Metrics>> MetricsActor::GenAllMetricsWithoutSystem() const
{
    std::vector<litebus::Future<Metrics>> metrics;
    for (const auto &iter : filter_) {
        if (iter.first.find(collector_type::SYSTEM) != std::string::npos) {
            continue;
        }
        metrics.push_back(iter.second->GetMetrics());
    }
    return metrics;
}

std::string MetricsActor::BuildUpdateMetricsRequest(const std::vector<litebus::Future<Metrics>> &metricses)
{
    messages::UpdateResourcesRequest req;
    auto unit = req.mutable_resourceunit();
    unit->CopyFrom(BuildResourceUnit(metricses));
    for (auto &ins : unit->instances()) {
        auto instanceID = ins.first;
        if (instanceInfos_.find(instanceID) == instanceInfos_.end()) {
            YRLOG_WARN("failed to find instance({}) in instance info map", instanceID);
        } else {
            auto runtimeID = instanceInfos_[instanceID].runtimeid();
            auto storageType = instanceInfos_[instanceID].deploymentconfig().storagetype();
            auto requestID = instanceInfos_[instanceID].requestid();
            auto address = instanceInfos_[instanceID].address();
            unit->mutable_instances()->at(instanceID).set_runtimeid(runtimeID);
            unit->mutable_instances()->at(instanceID).set_storagetype(storageType);
            unit->mutable_instances()->at(instanceID).set_requestid(requestID);
            unit->mutable_instances()->at(instanceID).set_runtimeaddress(address);
        }
    }
    return req.SerializeAsString();
}

resources::ResourceUnit MetricsActor::BuildResourceUnit(const std::vector<litebus::Future<Metrics>> &allMetrics)
{
    auto unit = BuildResourceUnitWithInstance(allMetrics);
    auto systemUnit = BuildResourceUnitWithSystem(allMetrics);
    unit.mutable_actualuse()->CopyFrom(systemUnit.actualuse());
    unit.mutable_capacity()->CopyFrom(systemUnit.capacity());
    unit.mutable_allocatable()->CopyFrom(systemUnit.allocatable());

    auto nodeLabels = unit.mutable_nodelabels();
    for (const auto &futureMetrics : allMetrics) {
        // this future.Get() is from OnComplete
        auto metrics = futureMetrics.Get();
        // resource owner is one system property
        if (metrics.initLabels.IsSome()) {
            for (const auto &label : metrics.initLabels.Get()) {
                // init labels: { urpc:true, nodeclass:egg }
                resources::Value::Counter cnter;
                (void)cnter.mutable_items()->insert({ label.second, 1 });
                // nodeLabels: { urpc:{true:1}, nodeclass:{egg:1} }
                (void)nodeLabels->insert({ label.first, cnter });
            }
            continue;
        }
    }
    return unit;
}

resources::ResourceUnit MetricsActor::BuildResourceUnitWithInstance(
    const std::vector<litebus::Future<Metrics>> &allMetrics) const
{
    resources::ResourceUnit unit;
    auto instances = unit.mutable_instances();
    for (const auto &futureMetrics : allMetrics) {
        // this future.Get() is from OnComplete
        auto metrics = futureMetrics.Get();
        if (metrics.instanceID.IsNone() || metrics.metricsType == metrics_type::LABELS) {
            continue;
        }

        if (instances->find(metrics.instanceID.Get()) == instances->end()) {
            (void)instances->insert({ metrics.instanceID.Get(), resources::InstanceInfo{} });
        }
        auto &instanceInfo = instances->find(metrics.instanceID.Get())->second;
        instanceInfo.set_instanceid(metrics.instanceID.Get());

        // actual use
        resources::Resource resource;
        auto scalar = resource.mutable_scalar();
        if (metrics.usage.IsSome()) {
            scalar->set_value(metrics.usage.Get());
        }
        resource.set_name(metrics.metricsType);
        resource.set_type(resources::Value_Type_SCALAR);
        (void)instanceInfo.mutable_actualuse()->mutable_resources()->insert({ metrics.metricsType, resource });
    }
    return unit;
}

resources::ResourceUnit MetricsActor::BuildResourceUnitWithSystem(
    const std::vector<litebus::Future<Metrics>> &allMetrics)
{
    resources::ResourceUnit unit;
    auto actualUse = unit.mutable_actualuse()->mutable_resources();
    auto capacity = unit.mutable_capacity()->mutable_resources();
    auto allocatable = unit.mutable_allocatable()->mutable_resources();
    for (const auto &futureMetrics : allMetrics) {
        // this future.Get() is from OnComplete
        auto metrics = futureMetrics.Get();
        if (metrics.instanceID.IsSome() || metrics.metricsType == metrics_type::LABELS) {
            continue;
        }

        auto resourceValueType = resources::Value_Type_SCALAR;
        // GPU/NPU's type is SET, if device ID is empty, then ignore information of GPU/NPU
        if (metrics.metricsType == metrics_type::GPU || metrics.metricsType == metrics_type::NPU) {
            if (metrics.devClusterMetrics.IsNone()) {
                // Failed to collect XPU information. Invalid metrics.
                continue;
            }
            resourceValueType = resources::Value_Type_VECTORS;
        }

        // actual use
        resources::Resource resource;
        auto scalar = resource.mutable_scalar();
        if (metrics.usage.IsSome()) {
            scalar->set_value(metrics.usage.Get());
        } else {
            scalar->set_value(0);
        }
        BuildResource(metrics, resource, resourceValueType);
        (void)actualUse->insert({ resource.name(), resource }); // [CPU|Memory|GPU|NPU|InitLabels]

        // capacity
        resource = resources::Resource{};
        scalar = resource.mutable_scalar();
        if (metrics.limit.IsSome()) {
            scalar->set_value(metrics.limit.Get());
        } else {
            scalar->set_value(0);
        }
        BuildResource(metrics, resource, resourceValueType);
        (void)capacity->insert({ resource.name(), resource }); // [CPU|Memory|GPU|NPU|InitLabels]

        // allocatable
        resource = resources::Resource{};
        scalar = resource.mutable_scalar();
        if (metrics.limit.IsSome()) {
            scalar->set_value(metrics.limit.Get());
        } else {
            scalar->set_value(0);
        }
        BuildResource(metrics, resource, resourceValueType);
        (void)allocatable->insert({ resource.name(), resource });
    }
    return unit;
}

void MetricsActor::BuildResource(Metrics &metrics, resources::Resource &resource,
                                 const resources::Value_Type &type)
{
    resource.set_name(metrics.metricsType);
    resource.set_type(type);
    BuildDevClusterResource(metrics, resource);
}

std::vector<int> MetricsActor::GetCardIDs()
{
    return cardIDs_;
}

void MetricsActor::BuildDevClusterResource(Metrics &metrics, resources::Resource &resource)
{
    if (metrics.devClusterMetrics.IsSome()) {
        if (metrics.devClusterMetrics.Get().strInfo.find(dev_metrics_type::PRODUCT_MODEL_KEY)
            != metrics.devClusterMetrics.Get().strInfo.end()) {
            resource.set_name(metrics.metricsType + "/"
                              + metrics.devClusterMetrics.Get().strInfo.at(dev_metrics_type::PRODUCT_MODEL_KEY));
        }
        if (auto iter = metrics.devClusterMetrics.Get().intsInfo.find(resource_view::IDS_KEY);
            iter != metrics.devClusterMetrics.Get().intsInfo.end()) {
            cardIDs_ = iter->second;
        }
        for (auto &pair : metrics.devClusterMetrics.Get().intsInfo) {
            TransitionToVectors(pair.first, metrics, resource);
        }
        for (auto &pair : metrics.devClusterMetrics.Get().strInfo) {
            resource.mutable_heterogeneousinfo()->insert({ pair.first, pair.second });
        }
    }
}

void MetricsActor::TransitionToVectors(const std::string &key, Metrics &metrics, resources::Resource &resource) const
{
    auto categories = resource.mutable_vectors()->mutable_values();
    auto &infos = metrics.devClusterMetrics.Get().intsInfo;
    if (infos.find(key) != infos.end()) {
        auto hbmArr = infos.at(key);
        if (hbmArr.empty()) {
            return;
        }
        auto &vectors = (*categories)[key];
        auto &vector = (*vectors.mutable_vectors())[nodeID];
        for (const auto &value : hbmArr) {
            vector.mutable_values()->Add(value);
        }
    }
}

void MetricsActor::UpdateAgentInfo(const litebus::AID &agent)
{
    agentAid_ = agent;
}

void MetricsActor::UpdateRuntimeManagerInfo(const litebus::AID &runtimeManagerAID)
{
    runtimeManagerAID_ = runtimeManagerAID;
}

litebus::Future<Status> MetricsActor::NotifyInstancesDiskUsageExceedLimit(const DiskUsageMonitorConfig &config) const
{
    if (!diskUsageMonitorNotifyFailureEnable_) {
        return Status::OK();
    }
    return litebus::Async(runtimeManagerAID_, &RuntimeManager::NotifyInstancesDiskUsageExceedLimit, config.description,
                          config.checkDiskUsageLimit);
}

void MetricsActor::SendAgentDiskUsageExceedLimit(const DiskUsageMonitorConfig &config)
{
    YRLOG_ERROR(
        "The disk usage of the directory "
        "for which the user has write permissions exceeds the limit: {} MB.",
        config.checkDiskUsageLimit);
    messages::UpdateRuntimeStatusRequest req;
    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    req.set_requestid(uuid.ToString());
    req.set_status(static_cast<int32_t>(RUNTIME_MANAGER_DISK_USAGE_EXCEED_LIMIT));
    req.set_message("The disk usage of the directory for which the user has write permissions exceeds the limit: "
                    + std::to_string(config.checkDiskUsageLimit) + " MB, for " + config.description);
    (void)Send(agentAid_, "UpdateRuntimeStatus", req.SerializeAsString());
    updateRuntimeStatusRetryTimer_ = litebus::AsyncAfter(UPDATE_RUNTIME_STATUS_RETRY_DURATION, GetAID(),
                                                         &MetricsActor::RetryUpdateRuntimeStatus, req, 0);
}

void MetricsActor::StartDiskUsageMonitor()
{
    if (diskUsageMonitorConfigs_.empty()) {
        return;
    }
    for (auto &config : diskUsageMonitorConfigs_) {
        if (config.checkDiskUsageLimit < 0) {
            YRLOG_DEBUG("no need to start monitor disk({}) usage, limit({}) < 0", config.description,
                       config.checkDiskUsageLimit);
            continue;
        }
        if (!IsDiskUsageBelowLimit(config)) {
            NotifyInstancesDiskUsageExceedLimit(config).OnComplete(
                litebus::Defer(GetAID(), &MetricsActor::SendAgentDiskUsageExceedLimit, config));
            return;
        }
    }
    // only need one timer, cancel previous unused timer
    (void)litebus::TimerTools::Cancel(diskUsageMonitorTimer_);
    diskUsageMonitorTimer_ =
        litebus::AsyncAfter(checkDiskUsageMonitorDuration_, GetAID(), &MetricsActor::StartDiskUsageMonitor);
}

void MetricsActor::StopDiskUsageMonitor()
{
    (void)litebus::TimerTools::Cancel(diskUsageMonitorTimer_);
}

void MetricsActor::RuntimeMemoryMetricsProcess(const std::vector<litebus::Future<Metrics>> &metrics)
{
    // First, judge whether the instance memory metric in the current metric exceeds the limit
    for (const auto &futureMetrics : metrics) {
        const auto metrics = futureMetrics.Get();
        if (metrics.instanceID.IsNone() || metrics.limit.IsNone() || metrics.metricsType != metrics_type::MEMORY
            || metrics.collectorType != collector_type::INSTANCE) {
            continue;
        }

        double usage = 0;
        if (metrics.usage.IsSome()) {
            usage = metrics.usage.Get(); // in MB
        }

        double limit = metrics.limit.Get();
        const std::string &instanceID = metrics.instanceID.Get();
        YRLOG_DEBUG("instance({}) memory usage: {} MB, limit: {} MB, anomalyCounts: {}", instanceID, usage, limit,
                    anomalyCounts_[instanceID]);
        if (usage > limit + runtimeOomMonitorConfig_.controlLimit) {
            YRLOG_DEBUG("exceed control limit, instance({}) limit({} MB) usage({} MB)", instanceID, limit, usage);
            // Then combine historical consecutive anomaly counts
            if (++anomalyCounts_[instanceID] >= runtimeOomMonitorConfig_.consecutiveDetectionCount) {
                YRLOG_DEBUG(
                    "exceed consecutive anomaly count({}), instance({}) will trigger runtime memory exceed limit Kill",
                    runtimeOomMonitorConfig_.consecutiveDetectionCount, instanceID);

                // Interacting with the RuntimeManager to stop the instance
                const auto &runtimeID = instanceInfos_[instanceID].runtimeid();
                const auto &requestID = instanceInfos_[instanceID].requestid();
                if (runtimeMemoryExceedLimitCallback_ != nullptr) {
                    runtimeMemoryExceedLimitCallback_(instanceID, runtimeID, requestID);
                }
                if (anomalyCounts_.count(instanceID) > 0) {
                    (void)anomalyCounts_.erase(instanceID);
                }
            }
        } else {
            anomalyCounts_[instanceID] = 0;
        }
    }
}

void MetricsActor::StartRuntimeMemoryLimitMonitor()
{
    if (!runtimeOomMonitorConfig_.enable) {
        return;
    }

    if (runtimeMemoryLimitCollector_ != nullptr) {
        std::vector<litebus::Future<Metrics>> metrics;
        metrics.push_back(runtimeMemoryLimitCollector_->GetMetrics());
        RuntimeMemoryMetricsProcess(metrics);
    }

    // only need one timer, cancel previous timer
    (void)litebus::TimerTools::Cancel(runtimeMemoryLimitMonitorTimer_);
    runtimeMemoryLimitMonitorTimer_ = litebus::AsyncAfter(runtimeOomMonitorConfig_.memoryDetectionInterval, GetAID(),
                                                          &MetricsActor::StartRuntimeMemoryLimitMonitor);
}

void MetricsActor::StopRuntimeMemoryLimitMonitor()
{
    if (!runtimeOomMonitorConfig_.enable) {
        return;
    }
    (void)litebus::TimerTools::Cancel(runtimeMemoryLimitMonitorTimer_);
}

bool MetricsActor::IsDiskUsageBelowLimit(const DiskUsageMonitorConfig &config) const
{
    int totalUsage = 0;
    for (auto &path : config.checkDiskUsageDirs) {
        if (!litebus::os::ExistPath(path)) {
            // path don't exist for now, keep monitoring
            YRLOG_DEBUG("path {} don't exist for now, keep monitoring", path);
            continue;
        }
        std::string command = "/usr/bin/du -sh -m " + path + " 2>/dev/null";
        auto result = ExecuteCommand(command);
        if (!result.error.empty()) {
            YRLOG_ERROR("get disk({}) usage failed. error message: {}", path, result.error);
            return false;
        }
        auto outStrs = litebus::strings::Split(result.output, "\t");
        if (outStrs.empty()) {
            YRLOG_ERROR("failed to get disk({}) usage, empty output", path);
            return false;
        }
        int usage = 0;
        try {
            usage = std::stoi(outStrs[0]);
        } catch (std::invalid_argument const &ex) {
            YRLOG_ERROR("failed to get disk({}) usage,  value({}) is not INT", path, usage);
            return false;
        }
        if (INT_MAX - totalUsage < usage) {
            YRLOG_ERROR("total usage is overflow for path {}", path);
            return false;
        }
        totalUsage += usage;
        if (usage > config.checkDiskUsageLimit) {
            YRLOG_ERROR("disk({}) usage({}) is above limit({})", path, usage, config.checkDiskUsageLimit);
            return false;
        }
    }
    return true;
}

void MetricsActor::UpdateRuntimeStatusResponse(const litebus::AID &from, std::string &&, std::string &&msg)
{
    messages::UpdateRuntimeStatusResponse rsp;
    if (msg.empty() || !rsp.ParseFromString(msg)) {
        YRLOG_ERROR("message from {} is invalid", std::string(from));
        return;
    }
    (void)litebus::TimerTools::Cancel(updateRuntimeStatusRetryTimer_);

    // restart monitoring
    (void)litebus::TimerTools::Cancel(diskUsageMonitorTimer_);
    diskUsageMonitorTimer_ =
        litebus::AsyncAfter(checkDiskUsageMonitorDuration_, GetAID(), &MetricsActor::StartDiskUsageMonitor);
}

void MetricsActor::RetryUpdateRuntimeStatus(const messages::UpdateRuntimeStatusRequest &req, int retryTime)
{
    YRLOG_DEBUG("retry send update runtime status request to {}, retry times({})", std::string(agentAid_), retryTime);
    (void)Send(agentAid_, "UpdateRuntimeStatus", req.SerializeAsString());
    updateRuntimeStatusRetryTimer_ = litebus::AsyncAfter(UPDATE_RUNTIME_STATUS_RETRY_DURATION, GetAID(),
                                                         &MetricsActor::RetryUpdateRuntimeStatus, req, ++retryTime);
}
}  // namespace functionsystem::runtime_manager