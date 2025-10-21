/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2025. All rights reserved.
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

#pragma once

#include <mutex>
#include <thread>
#include <vector>

#include "src/libruntime/gwclient/http/http_client.h"
#include "src/libruntime/libruntime_config.h"

namespace asio = boost::asio;

namespace YR {
namespace Libruntime {
class ClientManager : public HttpClient {
public:
    ClientManager(const std::shared_ptr<LibruntimeConfig> &librtCfg);
    ~ClientManager() override;

    ErrorInfo Init(const ConnectionParam &param) override;
    void SubmitInvokeRequest(const http::verb &method, const std::string &target,
                             const std::unordered_map<std::string, std::string> &headers, const std::string &body,
                             const std::shared_ptr<std::string> requestId,
                             const HttpCallbackFunction &receiver) override;

private:
    ErrorInfo InitCtxAndIocThread();
    std::shared_ptr<asio::io_context> ioc;
    std::unique_ptr<asio::io_context::work> work;
    std::vector<std::unique_ptr<std::thread>> asyncRunners;

    ConnectionParam connParam;
    std::vector<std::shared_ptr<HttpClient>> clients;
    uint32_t connectedClientsCnt;
    std::shared_ptr<LibruntimeConfig> librtCfg;
    std::mutex connMtx;
    uint32_t maxIocThread;
    bool enableMTLS;
};
}  // namespace Libruntime
}  // namespace YR
