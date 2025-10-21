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

#include "meta_store_explorer.h"

#include "httpd/http_connect.hpp"
#include "utils/os_utils.hpp"
#include "logs/logging.h"

namespace functionsystem {
litebus::Future<std::string> MetaStoreDefaultExplorer::Explore()
{
    return address_;
}

bool MetaStoreDefaultExplorer::IsNeedExplore()
{
    return false;
}

void MetaStoreDefaultExplorer::UpdateAddress(const std::string &address)
{
    address_ = address;
}

}  // namespace functionsystem