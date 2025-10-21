/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
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

#include "hetero_future.h"
#include "src/libruntime/utils/datasystem_utils.h"

namespace YR {
namespace Libruntime {
YR::Libruntime::AsyncResult ConverDsStatusToAsyncRes(datasystem::Status dsStatus)
{
    YR::Libruntime::AsyncResult result;
    YRLOG_DEBUG("convert async result from status, code is {}, msg is {}", dsStatus.GetCode(), dsStatus.GetMsg());
    if (dsStatus.IsOk()) {
        result.error = YR::Libruntime::ErrorInfo();
    } else {
        result.error = ErrorInfo(YR::Libruntime::ConvertDatasystemErrorToCore(dsStatus.GetCode()), dsStatus.GetMsg());
    }
    result.failedList = {};
    return result;
}

HeteroFuture::HeteroFuture(std::shared_ptr<datasystem::Future> dsFuture)
{
    this->dsFuture_ = dsFuture;
    this->isDsFuture_ = true;
}

bool HeteroFuture::IsDsFuture()
{
    return this->isDsFuture_;
}

YR::Libruntime::AsyncResult HeteroFuture::Get()
{
    datasystem::Status status = this->dsFuture_->Get();
    return ConverDsStatusToAsyncRes(status);
}

}  // namespace Libruntime
}  // namespace YR