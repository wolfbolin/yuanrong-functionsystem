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

#ifndef RUNTIME_MANAGER_METRICS_COLLECTOR_INSTANCE_CPU_COLLECTOR_H
#define RUNTIME_MANAGER_METRICS_COLLECTOR_INSTANCE_CPU_COLLECTOR_H

#include "base_metrics_collector.h"
#include "base_instance_collector.h"

namespace functionsystem::runtime_manager {

namespace instance_metrics {
const std::string PROCESS_STAT_PATH_EXPRESS = "/proc/?/stat";
const int8_t CPU_UTIME_INDEX = 13;
const int8_t CPU_STIME_INDEX = 14;
const int8_t CPU_CUTIME_INDEX = 15;
const int8_t CPU_CSTIME_INDEX = 16;
const int8_t PROCESS_CPU_STAT_LEN = 52;
const uint8_t CPU_JIFFIES_INTERVAL = 10;
const uint8_t CPU_CAL_INTERVAL = 100;
const uint32_t CPU_SCALE = 1000;
}

class InstanceCPUCollector : public BaseInstanceCollector, public BaseMetricsCollector {
public:
    InstanceCPUCollector(const pid_t &pid, const std::string &instanceID, const double &limit,
                         const std::string &deployDir);
    InstanceCPUCollector(const pid_t &pid, const std::string &instanceID, const double &limit,
                         const std::string &deployDir, const std::shared_ptr<ProcFSTools> &procFSTools);
    ~InstanceCPUCollector() override = default;

    std::string GenFilter() const override;
    litebus::Future<Metric> GetUsage() const override;
    Metric GetLimit() const override;

private:
    static litebus::Option<double> GetCpuJiffies(const pid_t &pid, const std::shared_ptr<ProcFSTools> procFSTools);
    static litebus::Option<double> CalJiffiesForCPUProcess(const std::string &stat);
};

}

#endif // RUNTIME_MANAGER_METRICS_COLLECTOR_INSTANCE_CPU_COLLECTOR_H