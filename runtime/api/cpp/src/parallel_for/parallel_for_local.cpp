/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
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

#include "thread_pool.h"
#include "yr/parallel/parallel_for.h"

namespace YR {
namespace Parallel {
std::once_flag threadPoolInitFlag;
int LocalSubmit(std::function<void()> &&func)
{
    std::call_once(threadPoolInitFlag, []() { ThreadPool::GetInstance().Init(GetThreadPoolSize()); });
    return ThreadPool::GetInstance().SubmitTaskToPool(std::move(func));
}
}  // namespace Parallel
}  // namespace YR