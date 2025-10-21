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

#ifndef TEST_INTEGRATION_MOCKS_MOCK_POSIX_STREAM_HANDLER_H
#define TEST_INTEGRATION_MOCKS_MOCK_POSIX_STREAM_HANDLER_H

#include <gmock/gmock.h>

#include <async/future.hpp>

#include "proto/pb/posix_pb.h"

namespace functionsystem::test {

class MockPosixStreamHandler {
public:
    MOCK_METHOD(litebus::Future<std::shared_ptr<runtime_rpc::StreamingMessage>>, InvokeHandler,
                (const std::string &from, const std::shared_ptr<runtime_rpc::StreamingMessage> &request));

    MOCK_METHOD(litebus::Future<std::shared_ptr<runtime_rpc::StreamingMessage>>, KillHandler,
                (const std::string &from, const std::shared_ptr<runtime_rpc::StreamingMessage> &request));
};

}  // namespace functionsystem::test

#endif  // TEST_INTEGRATION_MOCKS_MOCK_POSIX_STREAM_HANDLER_H
