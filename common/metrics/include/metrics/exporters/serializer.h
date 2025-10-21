/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
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

#ifndef OBSERVABILITY_EXPORTERS_METRICS_SERIALIZER_H
#define OBSERVABILITY_EXPORTERS_METRICS_SERIALIZER_H

#include "metrics/sdk/metric_data.h"

namespace observability::exporters::metrics {
class Serializer {
public:
    virtual ~Serializer() = default;
    virtual void Serialize(std::ostream &ost, const observability::sdk::metrics::MetricData &metric) const = 0;
};
}  // namespace observability::exporters::metrics
#endif // OBSERVABILITY_EXPORTERS_METRICS_SERIALIZER_H
