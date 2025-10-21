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

#include "execution_manager.h"
namespace YR {
namespace Libruntime {
ExecutionManager::ExecutionManager(size_t concurrency, std::function<void(std::function<void(void)> &&)> submitHook)
    : useCustomExecutor(submitHook != nullptr), customExecutorSubmit(submitHook)
{
}

ErrorInfo ExecutionManager::DoInit(size_t concurrency)
{
    if (useCustomExecutor) {
        return ErrorInfo();
    }
    concurrency_ = concurrency;
    if (concurrency > 1) {
        auto errMsg = this->callExecutor.Init(concurrency, "call_executor");
        if (!errMsg.empty()) {
            return ErrorInfo(ErrorCode::ERR_INNER_SYSTEM_ERROR, ModuleCode::RUNTIME, errMsg);
        }
    }
    return ErrorInfo();
}

bool ExecutionManager::isMultipleConcurrency()
{
    return concurrency_ > 1 && !useCustomExecutor;
}

void ExecutionManager::DoHandle(std::function<void()> &&hdlr, std::string reqId)
{
    if (!useCustomExecutor) {
        if (concurrency_ > 1) {
            this->callExecutor.Handle(std::move(hdlr), reqId);
        } else {
            hdlr();
        }
    } else {
        customExecutorSubmit(std::move(hdlr));
    }
}

void ExecutionManager::ErasePendingThread(const std::string &reqId)
{
    if (useCustomExecutor) {
        return;
    }
    if (concurrency_ > 1) {
        this->callExecutor.ErasePendingThread(reqId);
    }
}
}  // namespace Libruntime
}  // namespace YR