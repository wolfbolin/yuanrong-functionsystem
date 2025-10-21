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

#include "function_proxy/local_scheduler/ds_healthy_checker/ds_healthy_checker.h"

#include <fcntl.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <utility>

#include "logs/logging.h"
#include "mocks/mock_distributed_cache_client.h"
#include "utils/future_test_helper.h"

namespace functionsystem::test {
using namespace local_scheduler;
using namespace ::testing;

class DsHealthyCheckerTest : public ::testing::Test {
public:
    static void SetUpTestCase()
    {
        mockDistributedCacheClient_ = std::make_shared<MockDistributedCacheClient>();
        dsHealthyChecker_ = std::make_shared<DsHealthyChecker>(1000, 5, mockDistributedCacheClient_);
    }

    static void TearDownTestCase()
    {
        dsHealthyChecker_ = nullptr;
        mockDistributedCacheClient_ = nullptr;
    }

    inline static std::shared_ptr<MockDistributedCacheClient> mockDistributedCacheClient_;
    inline static std::shared_ptr<DsHealthyChecker> dsHealthyChecker_;
};

/**
 * Feature: DsHealthyCheckerTest check healthy
 * Description: When healthy check return success, healthy checker running normally else executing unhealthy call back
 * Steps:
 * 1. Create DsHealthyChecker
 * 2. make healthy check return success once and then always failed
 * 3. start DsHealthyChecker
 * Expectation:
 * 1. when healthy check return success, DsHealthyChecker is normal, failedTimes == 0
 * 2. when healthy check return failed, callback is executed
 */
TEST_F(DsHealthyCheckerTest, CheckHealthy)
{
    EXPECT_CALL(*mockDistributedCacheClient_, GetHealthStatus)
        .WillOnce(Return(Status(SUCCESS)))
        .WillRepeatedly(Return(Status(FAILED)));

    auto promise = std::make_shared<litebus::Promise<bool>>();
    auto cb = [promise](const bool healthy) {
        if (!healthy)
            promise->SetValue(true);
    };
    dsHealthyChecker_->SubscribeDsHealthy(cb);
    EXPECT_FALSE(dsHealthyChecker_->GetIsUnhealthy());

    (void)litebus::Spawn(dsHealthyChecker_);
    YRLOG_WARN("checker->Start");

    ASSERT_AWAIT_READY(promise->GetFuture());
    EXPECT_TRUE(promise->GetFuture().Get());

    EXPECT_TRUE(dsHealthyChecker_->GetIsUnhealthy());

    litebus::Terminate(dsHealthyChecker_->GetAID());
    litebus::Await(dsHealthyChecker_->GetAID());
}

}  // namespace functionsystem::test