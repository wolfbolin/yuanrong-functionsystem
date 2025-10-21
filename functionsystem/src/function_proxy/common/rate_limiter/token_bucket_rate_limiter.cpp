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

#include "token_bucket_rate_limiter.h"

#include <algorithm>

namespace functionsystem {
const int MILLISECONDS = 1000;

bool TokenBucketRateLimiter::TryAcquire()
{
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRefillTime_);
    uint64_t tokensToAdd = static_cast<uint64_t>(elapsedTime.count() * refillRate_ / MILLISECONDS);

    tokens_ = std::min(static_cast<uint64_t>(tokens_ + tokensToAdd), capacity_);
    lastRefillTime_ = now;

    if (tokens_ > 0) {
        --tokens_;
        return true;
    }
    return false;
}

}  // namespace functionsystem
