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

#ifndef FUNCTION_PROXY_COMMON_POSIX_SERVICE_POSIX_AUTH_INTERCEPTOR_H
#define FUNCTION_PROXY_COMMON_POSIX_SERVICE_POSIX_AUTH_INTERCEPTOR_H

#include "rpc/stream/posix/posix_stream.h"

namespace functionsystem {

class PosixAuthInterceptor : public AuthInterceptor<runtime_rpc::StreamingMessage> {
public:
    PosixAuthInterceptor(const std::string &runtimeID, const std::string &instanceID)
        : runtimeID_(runtimeID), instanceID_(instanceID)
    {
    }
    ~PosixAuthInterceptor() override = default;

    litebus::Future<bool> Sign(const std::shared_ptr<runtime_rpc::StreamingMessage> &message) override;
    litebus::Future<bool> Verify(const std::shared_ptr<runtime_rpc::StreamingMessage> &message) override;

private:
    std::string runtimeID_;
    std::string instanceID_;
};
}  // namespace functionsystem

#endif  // FUNCTION_PROXY_COMMON_POSIX_SERVICE_POSIX_AUTH_INTERCEPTOR_H
