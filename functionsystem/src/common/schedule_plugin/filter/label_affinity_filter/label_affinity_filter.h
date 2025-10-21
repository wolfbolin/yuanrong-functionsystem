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

#ifndef FUNCTIONSYSTEM_LABEL_AFFINITY_FILTER_H
#define FUNCTIONSYSTEM_LABEL_AFFINITY_FILTER_H

#include "resource_type.h"
#include "common/schedule_plugin/common/preallocated_context.h"
#include "common/scheduler_framework/framework/policy.h"
#include "status/status.h"

namespace functionsystem::schedule_plugin::filter {
class LabelAffinityFilter : public schedule_framework::FilterPlugin {
public:
    explicit LabelAffinityFilter(bool isRelaxed, bool isRootDomainLevel)
        : isRelaxed_(isRelaxed), isRootDomainLevel_(isRootDomainLevel) {}
    ~LabelAffinityFilter() override = default;
    std::string GetPluginName() override;

    schedule_framework::Filtered Filter(
        const std::shared_ptr<schedule_framework::ScheduleContext> &ctx,
        const resource_view::InstanceInfo &instance,
        const resource_view::ResourceUnit &resourceUnit) override;

private:
    bool PerformScoreOptimalityCheck(
        const resource_view::ResourceUnit &resourceUnit,
        const resource_view::InstanceInfo &instance,
        const std::shared_ptr<schedule_framework::PreAllocatedContext> &preContext) const;
    bool PerformLabelFilter(
        const resource_view::InstanceInfo &instance,
        messages::AffinityContext &affinityCtx,
        const resource_view::ResourceUnit &resourceUnit,
        const std::shared_ptr<schedule_framework::PreAllocatedContext> &ctx) const;
    bool isRelaxed_;
    bool isRootDomainLevel_;
};

}

#endif // FUNCTIONSYSTEM_LABEL_AFFINITY_FILTER_H
