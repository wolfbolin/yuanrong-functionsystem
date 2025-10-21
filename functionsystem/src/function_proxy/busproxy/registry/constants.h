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

#ifndef BUSPROXY_REGISTRY_CONSTANTS_H
#define BUSPROXY_REGISTRY_CONSTANTS_H

namespace functionsystem {
// TTL for busProxy registry
const int DEFAULT_TTL = 60000;

const int MIN_TTL = 1000;

const int MAX_TTL = 600000;
}  // namespace functionsystem

#endif  // BUSPROXY_REGISTRY_CONSTANTS_H
