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

#ifndef FUNCTION_PROXY_CONSTANTS_H
#define FUNCTION_PROXY_CONSTANTS_H

namespace functionsystem::function_proxy {
const std::string WHITE_LIST = "white_list";
const uint32_t REQUEST_IAM_INTERVAL = 3000;
const uint32_t REQUEST_IAM_MAX_RETRY = 5;
// unit: ms, check token, ak/sk every 85 second (it will scan token at least twice before token, ak/sk expired)
const uint32_t CHECK_EXPIRED_INTERVAL = 85 * 1000;
// unit: s, update token, ak/sk 3 min before token, ak/sk expired
const uint32_t TIME_AHEAD_OF_EXPIRED = 3 * 60;
}

#endif  // FUNCTION_PROXY_CONSTANTS_H
