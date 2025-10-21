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

#include "metrics/sdk/immediately_export_processor.h"

#include "metrics/sdk/instruments.h"
#include "sdk/include/processor_actor.h"
#include "common/include/utils.h"

namespace observability::sdk::metrics {
ImmediatelyExportProcessor::ImmediatelyExportProcessor(std::shared_ptr<MetricsExporter::Exporter> &&exporter)

{
    auto exportConfigs = ExportConfigs { .exporterName = "immediatelyExporterDefault",
                                         .exportMode = ExportMode::IMMEDIATELY, .batchSize = 1 };
    processorActor_ = std::make_shared<ProcessorActor>(std::move(exporter), std::move(exportConfigs));
    litebus::Spawn(processorActor_);
}

ImmediatelyExportProcessor::ImmediatelyExportProcessor(
    std::shared_ptr<MetricsExporter::Exporter> &&exporter, ExportConfigs &exportConfigs)
{
    observability::metrics::ValidateExportConfigs(exportConfigs);
    exportConfigs.batchSize = 1;
    processorActor_ = std::make_shared<ProcessorActor>(std::move(exporter), std::move(exportConfigs));
    litebus::Spawn(processorActor_);
    litebus::Async(processorActor_->GetAID(), &ProcessorActor::Start);
}

AggregationTemporality ImmediatelyExportProcessor::GetAggregationTemporality(
    sdk::metrics::InstrumentType instrumentType) const noexcept
{
    return litebus::Async(processorActor_->GetAID(), &ProcessorActor::GetAggregationTemporality, instrumentType).Get();
}

void ImmediatelyExportProcessor::Export(const MetricData &data) noexcept
{
    (void)litebus::Async(processorActor_->GetAID(), &ProcessorActor::Export, data);
}

}  // namespace observability::sdk::metrics