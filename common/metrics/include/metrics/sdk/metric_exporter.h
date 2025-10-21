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

#ifndef OBSERVABILITY_SDK_METRICS_METRIC_EXPORTER_H
#define OBSERVABILITY_SDK_METRICS_METRIC_EXPORTER_H

#include <chrono>

#include "metrics/sdk/instruments.h"
#include "metrics/sdk/metric_data.h"

namespace observability::sdk::metrics {

/**
 * ExportResult is returned as result of exporting a batch of Records.
 */
enum class ExportResult {
    // Batch was exported successfully.
    SUCCESS = 0,

    // Batch exporting failed, caller must not retry exporting the same batch
    // and the batch must be dropped.
    FAILURE = 1,

    // The collection does not have enough space to receive the export batch.
    FAILURE_FULL = 2,

    // The export() function was passed an invalid argument.
    FAILURE_INVALID_ARGUMENT = 3,
};

class PushExporter {
public:
    virtual ~PushExporter() = default;

    /**
     * Exports a batch of metrics data. This method must not be called
     * concurrently for the same exporter instance.
     * @param data metrics data
     */
    virtual ExportResult Export(const MetricData &data) noexcept = 0;

    /**
     * Get the AggregationTemporality for given Instrument Type for this exporter.
     *
     * @return AggregationTemporality
     */
    virtual AggregationTemporality GetAggregationTemporality(InstrumentType instrumentType) const noexcept = 0;

    /**
     * Force flush the exporter.
     */
    virtual bool ForceFlush(std::chrono::microseconds timeout = (std::chrono::microseconds::max)()) noexcept = 0;

    /**
     * Shut down the metric exporter.
     * @param timeout an optional timeout.
     * @return return the status of the operation.
     */
    virtual bool Shutdown(std::chrono::microseconds timeout = std::chrono::microseconds(0)) noexcept = 0;
};
}  // namespace observability::sdk::metrics

#endif