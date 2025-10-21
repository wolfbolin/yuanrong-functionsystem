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

#include "global_sched_driver.h"

#include <gtest/gtest.h>

#include "common/constants/metastore_keys.h"
#include "common/explorer/explorer.h"
#include "common/resource_view/view_utils.h"
#include "global_sched.h"
#include "httpd/http.hpp"
#include "httpd/http_connect.hpp"
#include "mocks/mock_global_schd.h"
#include "mocks/mock_meta_store_client.h"
#include "utils/generate_info.h"

namespace functionsystem::test {
const std::string HEALTHY_URL = "/healthy";
const std::string GLOBAL_SCHEDULER = "global-scheduler";
const std::string QUERY_AGENTS_URL = "/queryagents";
const std::string EVICT_AGENT_URL = "/evictagent";
const std::string QUERY_AGENT_COUNT_URL = "/queryagentcount";
const std::string QUERY_RESOURCES_URL = "/resources";
const std::string GET_SCHEDULING_QUEUE_URL = "/scheduling_queue";

using namespace ::testing;

class GlobalSchedDriverTest : public ::testing::Test {
public:
    void SetUp() override
    {
        mockGlobalSched_ = std::make_shared<MockGlobalSched>();
        mockMetaStoreClient_ = std::make_shared<MockMetaStoreClient>("");

        const char *argv[] = { "./function_master",
                           "--log_config={\"filepath\": \"/home/yr/log\",\"level\": \"DEBUG\",\"rolling\": "
                           "{\"maxsize\": 100, \"maxfiles\": 1}}",
                           "--node_id=aaa",
                           "--ip=127.0.0.1:8080",
                           "--meta_store_address=127.0.0.1:32209",
                           "--d1=2",
                           "--d2=2",
                           "--election_mode=standalone" };
        flags_.ParseFlags(8, argv);

        explorer::Explorer::NewStandAloneExplorerActorForMaster(explorer::ElectionInfo{},
            GetLeaderInfo(litebus::AID("function_master", "127.0.0.1:8080")));
    }

    void TearDown() override
    {
        explorer::Explorer::GetInstance().Clear();
        mockGlobalSched_ = nullptr;
        globalSchedDriver_ = nullptr;
    }

protected:
    std::shared_ptr<MockGlobalSched> mockGlobalSched_;
    std::shared_ptr<MockMetaStoreClient> mockMetaStoreClient_;
    std::shared_ptr<global_scheduler::GlobalSchedDriver> globalSchedDriver_;
    functionsystem::functionmaster::Flags flags_;
};

TEST_F(GlobalSchedDriverTest, StartAndStopGlobalSchedulerDriver)
{
    EXPECT_CALL(*mockGlobalSched_, Start(_)).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*mockGlobalSched_, Stop).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*mockGlobalSched_, InitManager).WillOnce(Return());
    auto globalSchedDriver_ =
        std::make_shared<global_scheduler::GlobalSchedDriver>(mockGlobalSched_, flags_, mockMetaStoreClient_);
    auto status = globalSchedDriver_->Start();
    ASSERT_TRUE(status == Status::OK());
    globalSchedDriver_->Stop();
    globalSchedDriver_->Await();
}

TEST_F(GlobalSchedDriverTest, QueryHealthyRouter)
{
    auto globalSchedDriver_ =
        std::make_shared<global_scheduler::GlobalSchedDriver>(mockGlobalSched_, flags_, mockMetaStoreClient_);
    EXPECT_CALL(*mockGlobalSched_, Start(_)).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*mockGlobalSched_, Stop).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*mockGlobalSched_, InitManager).WillOnce(Return());
    auto status = globalSchedDriver_->Start();
    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    litebus::http::URL urlHealthy("http", "127.0.0.1", port, GLOBAL_SCHEDULER + HEALTHY_URL);
    std::unordered_map<std::string, std::string> headers = {
        {"Node-ID", "aaa"},
        {"PID", std::to_string(getpid())}
    };
    litebus::Future<HttpResponse> response = litebus::http::Get(urlHealthy, headers);
    response.Wait();
    ASSERT_EQ(response.Get().retCode, litebus::http::ResponseCode::OK);
    globalSchedDriver_->Stop();
    globalSchedDriver_->Await();
}

messages::FunctionSystemStatus ParseResponse(const std::string &body)
{
    messages::FunctionSystemStatus status;
    YRLOG_INFO("body: {}", body);
    google::protobuf::util::JsonStringToMessage(body, &status) ;
    return status;
}

resource_view::InstanceInfo GetInstanceInfo(std::string instanceId)
{
    Resources resources;
    Resource resource_cpu = view_utils::GetCpuResource();
    (*resources.mutable_resources())["CPU"] = resource_cpu;
    Resource resource_memory = view_utils::GetMemResource();
    (*resources.mutable_resources())["Memory"] = resource_memory;

    InstanceInfo instanceInfo;
    instanceInfo.set_instanceid(instanceId);
    instanceInfo.set_requestid("requestIdIdId");
    instanceInfo.set_parentid("parentidIdId");
    instanceInfo.mutable_resources()->CopyFrom(resources);

    return instanceInfo;
}

// test query resource info
// case1: invalid method
// case2: query successful (not empty)
TEST_F(GlobalSchedDriverTest, QueryResourcesRouter)
{
    auto globalSchedDriver_ =
        std::make_shared<global_scheduler::GlobalSchedDriver>(mockGlobalSched_, flags_, mockMetaStoreClient_);
    EXPECT_CALL(*mockGlobalSched_, Start(_)).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*mockGlobalSched_, Stop).WillOnce(Return(Status::OK()));
    EXPECT_CALL(*mockGlobalSched_, InitManager).WillOnce(Return());
    auto status = globalSchedDriver_->Start();
    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    litebus::http::URL urlQueryResource("http", "127.0.0.1", port, GLOBAL_SCHEDULER + QUERY_RESOURCES_URL);
    std::string resourceId = "id1";

    // query resource info case1: invalid method
    {
        auto response = litebus::http::Post(urlQueryResource, litebus::None(), litebus::None(), litebus::None());
        response.Wait();
        ASSERT_EQ(response.Get().retCode, litebus::http::ResponseCode::METHOD_NOT_ALLOWED);
    }

    // query resource info case2: query successful (empty header)
    {
        auto resp = messages::QueryResourcesInfoResponse();
        (*resp.mutable_resource()) = std::move(view_utils::Get1DResourceUnit(resourceId));
        EXPECT_CALL(*mockGlobalSched_, QueryResourcesInfo(_)).WillOnce(Return(resp));
        auto response = litebus::http::Get(urlQueryResource, litebus::None());
        response.Wait();
        EXPECT_EQ(response.Get().retCode, litebus::http::ResponseCode::OK);
        auto body = response.Get().body;
        auto infos = messages::QueryResourcesInfoResponse();
        EXPECT_EQ(google::protobuf::util::JsonStringToMessage(body, &infos).ok(), true);
        EXPECT_EQ(infos.resource().id(), resourceId);
    }

    // query resource info case3: query successful (header: type is json)
    {
        auto resp = messages::QueryResourcesInfoResponse();
        (*resp.mutable_resource()) = std::move(view_utils::Get1DResourceUnit(resourceId));
        EXPECT_CALL(*mockGlobalSched_, QueryResourcesInfo(_)).WillOnce(Return(resp));

        std::unordered_map<std::string, std::string> headers = {
            {"Type", "json"},
        };

        auto response = litebus::http::Get(urlQueryResource, headers);
        response.Wait();
        EXPECT_EQ(response.Get().retCode, litebus::http::ResponseCode::OK);
        auto body = response.Get().body;
        auto infos = messages::QueryResourcesInfoResponse();
        EXPECT_EQ(google::protobuf::util::JsonStringToMessage(body, &infos).ok(), true);
        EXPECT_EQ(infos.resource().id(), resourceId);
    }

    // query resource info case4: query successful (header: type is protobuf)
    {
        auto resp = messages::QueryResourcesInfoResponse();
        (*resp.mutable_resource()) = std::move(view_utils::Get1DResourceUnit("id1"));
        EXPECT_CALL(*mockGlobalSched_, QueryResourcesInfo(_)).WillOnce(Return(resp));
        std::unordered_map<std::string, std::string> headers = {
            {"Type", "protobuf"},
        };

        auto response = litebus::http::Get(urlQueryResource, headers);
        response.Wait();
        EXPECT_EQ(response.Get().retCode, litebus::http::ResponseCode::OK);
        auto body = response.Get().body;
        auto infos = messages::QueryResourcesInfoResponse();
        EXPECT_EQ(infos.ParseFromString(body), true);
        EXPECT_EQ(infos.resource().id(), resourceId);
    }

    // query resource info case3: query successful (header: invalid type)
    {
        std::unordered_map<std::string, std::string> headers = {
                {"Type", "invalidType"},
        };

        auto response = litebus::http::Get(urlQueryResource, headers);
        response.Wait();
        ASSERT_EQ(response.Get().retCode, litebus::http::ResponseCode::BAD_REQUEST);
    }

    globalSchedDriver_->Stop();
    globalSchedDriver_->Await();
}
}  // namespace functionsystem::test
