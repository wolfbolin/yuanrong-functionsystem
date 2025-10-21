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

#ifndef RUNTIME_MANAGER_METRICS_COLLECTOR_INSTANCE_MEMORY_COLLECTOR_H
#define RUNTIME_MANAGER_METRICS_COLLECTOR_INSTANCE_MEMORY_COLLECTOR_H

#include "base_instance_collector.h"

namespace functionsystem::runtime_manager {

namespace instance_metrics {
const std::string PROCESS_STATUS_PATH_EXPRESS = "/proc/?/status";
const std::string MEMORY_SIZE_KEY = "VmRSS:";
const uint64_t MEMORY_SCALE = 1 << 10; // KB
}

class InstanceMemoryCollector : public BaseInstanceCollector, public BaseMetricsCollector {
public:
    InstanceMemoryCollector(const pid_t &pid, const std::string &instanceID, const double &limit,
                            const std::string &deployDir);
    InstanceMemoryCollector(const pid_t &pid, const std::string &instanceID, const double &limit,
                            const std::string &deployDir, const std::shared_ptr<ProcFSTools> procFSTools);
    ~InstanceMemoryCollector() override = default;
    Metric GetLimit() const override;
    virtual litebus::Future<Metric> GetUsage() const override;
    std::string GenFilter() const override;
};

}

#endif // RUNTIME_MANAGER_METRICS_COLLECTOR_INSTANCE_MEMORY_COLLECTOR_H