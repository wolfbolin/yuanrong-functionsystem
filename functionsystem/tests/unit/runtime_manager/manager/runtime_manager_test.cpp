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

#include "files.h"
#include "exec/exec.hpp"
#include "gtest/gtest.h"
#include "runtime_manager/port/port_manager.h"
#include "runtime_manager_test_actor.h"
#include "utils/future_test_helper.h"
#include "utils/port_helper.h"
#include "utils/generate_info.h"

#include "runtime_manager/manager/runtime_manager.h"
using namespace functionsystem::test;
namespace functionsystem::runtime_manager {
const uint32_t MAX_REGISTER_TEST_TIMES = 5;
const uint32_t INITIAL_PORT = 600;
const uint32_t PORT_NUM = 800;
const std::string testDeployDir = "/tmp/layer/func/bucket-test-log1/yr-test-runtime-manager";
const std::string funcObj = testDeployDir + "/" + "funcObj";
class RuntimeManagerTest : public ::testing::Test {
public:
    void SetUp() override
    {
        PortManager::GetInstance().InitPortResource(INITIAL_PORT, PORT_NUM);
        (void)litebus::os::Mkdir(testDeployDir);
        (void)TouchFile(funcObj);
        (void)system(
            "echo \"testDeployDir in runtime_manager_test\""
            "> /tmp/layer/func/bucket-test-log1/yr-test-runtime-manager/funcObj");

        runtimeManagerActorName_ = GenerateRandomName("RuntimeManagerActor");
        manager_ = std::make_shared<RuntimeManager>(runtimeManagerActorName_);
        manager_->isUnitTestSituation_ = true;
        litebus::Spawn(manager_, true);
        manager_->connected_ = true;
        manager_->isUnitTestSituation_ = true;
    }

    void TearDown() override
    {
        (void)litebus::os::Rmdir(testDeployDir);
        PortManager::GetInstance().Clear();
        litebus::Terminate(manager_->GetAID());
        litebus::Await(manager_);
    }

    static void sigHandler(int signum)
    {
        sigReceived_.SetValue(true);
    }

protected:
    std::string runtimeManagerActorName_;
    std::shared_ptr<RuntimeManager> manager_;
    std::shared_ptr<RuntimeManagerTestActor> testActor_;
    inline static litebus::Future<bool> sigReceived_;
};

TEST_F(RuntimeManagerTest, StartInstanceTest)
{
    (void)litebus::os::Rm("/tmp/cpp/bin/runtime");
    if (!litebus::os::ExistPath("/tmp/cpp/bin")) {
        litebus::os::Mkdir("/tmp/cpp/bin");
    }
    auto fd = open("/tmp/cpp/bin/runtime", O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
    EXPECT_NE(fd, -1);
    close(fd);
    std::ofstream outfile;
    outfile.open("/tmp/cpp/bin/runtime");
    outfile << "sleep 2" << std::endl;
    outfile.close();
    const char *argv[] = {
        "/runtime_manager",
        "--node_id=node1",
        "--ip=127.0.0.1",
        "--host_ip=127.0.0.1",
        "--port=32233",
        "--runtime_initial_port=500",
        "--port_num=2000",
        "--runtime_dir=/tmp"
    };
    functionsystem::runtime_manager::Flags flags;
    flags.ParseFlags(8, argv);
    manager_->SetConfig(flags);

    testActor_ = std::make_shared<RuntimeManagerTestActor>(GenerateRandomName("RuntimeManagerTestActor"));
    litebus::Spawn(testActor_, true);

    messages::StartInstanceRequest startRequest;
    startRequest.set_type(static_cast<int32_t>(EXECUTOR_TYPE::RUNTIME));
    auto runtimeInfo = startRequest.mutable_runtimeinstanceinfo();
    runtimeInfo->set_requestid("test_requestID");
    runtimeInfo->set_instanceid("test_instanceID");
    runtimeInfo->set_traceid("test_traceID");
    auto resources = runtimeInfo->mutable_runtimeconfig()->mutable_resources()->mutable_resources();
    resource_view::Resource cpuResource;
    cpuResource.set_type(::resources::Value_Type::Value_Type_SCALAR);
    cpuResource.mutable_scalar()->set_value(500.0);
    (*resources)["CPU"] = cpuResource;
    resource_view::Resource memResource;
    memResource.set_type(::resources::Value_Type::Value_Type_SCALAR);
    memResource.mutable_scalar()->set_value(500.0);
    (*resources)["Memory"] = memResource;
    auto runtimeConfig = runtimeInfo->mutable_runtimeconfig();
    runtimeConfig->set_language("cpp");
    auto userEnvs = runtimeConfig->mutable_userenvs();
    userEnvs->insert({ "user_env1", "user_env1_value" });
    userEnvs->insert({ "user_env2", "user_env2_value" });

    auto deployConfig = runtimeInfo->mutable_deploymentconfig();
    deployConfig->set_objectid("test_objectID");
    deployConfig->set_bucketid("test_bucketID");
    deployConfig->set_deploydir(testDeployDir);
    deployConfig->set_storagetype("s3");

    // lost connection with function agent
    manager_->connected_ = false;
    testActor_->StartInstance(manager_->GetAID(), startRequest);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(testActor_->GetIsReceiveStartInstanceResponse(), false);
    manager_->connected_ = true;
    // repeat
    testActor_->ResetStartInstanceTimes();
    std::unordered_set<std::string> receivedStartingReq{ "repeat-123" };
    manager_->receivedStartingReq_ = receivedStartingReq;
    messages::StartInstanceRequest repeatRequest;
    startRequest.set_type(static_cast<int32_t>(EXECUTOR_TYPE::RUNTIME));
    repeatRequest.mutable_runtimeinstanceinfo()->set_requestid("repeat-123");
    testActor_->StartInstance(manager_->GetAID(), repeatRequest);
    // success
    testActor_->StartInstance(manager_->GetAID(), startRequest);
    ASSERT_AWAIT_TRUE([=]() -> bool { return !testActor_->GetStartInstanceResponse()->message().empty(); });

    auto response = testActor_->GetStartInstanceResponse();
    EXPECT_EQ(StatusCode::SUCCESS, response->code());
    EXPECT_EQ("start instance success", response->message());
    EXPECT_EQ("test_requestID", response->requestid());

    auto instanceResponse = response->startruntimeinstanceresponse();
    EXPECT_TRUE(!instanceResponse.runtimeid().empty());
    EXPECT_EQ(std::to_string(INITIAL_PORT), instanceResponse.port());
    EXPECT_EQ(static_cast<long unsigned int>(1), testActor_->GetStartInstanceTimes());
    manager_->receivedStartingReq_ = {};
    testActor_->ResetMessage();
    testActor_->StartInstance(manager_->GetAID(), startRequest);
    ASSERT_AWAIT_TRUE([=]() -> bool { return !testActor_->GetStartInstanceResponse()->message().empty(); });
    response = testActor_->GetStartInstanceResponse();
    EXPECT_EQ(StatusCode::RUNTIME_MANAGER_INSTANCE_HAS_BEEN_DEPLOYED, response->code());
    testActor_->ResetMessage();

    startRequest.mutable_runtimeinstanceinfo()->set_requestid("req-111111");
    testActor_->StartInstance(manager_->GetAID(), startRequest);
    ASSERT_AWAIT_TRUE([=]() -> bool { return !testActor_->GetStartInstanceResponse()->message().empty(); });
    response = testActor_->GetStartInstanceResponse();
    EXPECT_EQ(StatusCode::RUNTIME_MANAGER_INSTANCE_EXIST, response->code());

    litebus::Terminate(testActor_->GetAID());
    litebus::Await(testActor_->GetAID());
}

TEST_F(RuntimeManagerTest, StartInstanceWithPreStartSuccessTest)
{
    testActor_ = std::make_shared<RuntimeManagerTestActor>(GenerateRandomName("RuntimeManagerTestActor"));
    litebus::Spawn(testActor_, true);

    messages::StartInstanceRequest startRequest;
    startRequest.set_type(static_cast<int32_t>(EXECUTOR_TYPE::RUNTIME));
    auto runtimeInfo = startRequest.mutable_runtimeinstanceinfo();
    runtimeInfo->set_requestid("test_requestID");
    runtimeInfo->set_instanceid("test_instanceID");
    runtimeInfo->set_traceid("test_traceID");

    auto runtimeConfig = runtimeInfo->mutable_runtimeconfig();
    runtimeConfig->set_language("cpp");
    auto userEnvs = runtimeConfig->mutable_userenvs();
    userEnvs->insert({ "user_env1", "user_env1_value" });
    userEnvs->insert({ "user_env2", "user_env2_value" });

    auto deployConfig = runtimeInfo->mutable_deploymentconfig();
    deployConfig->set_objectid("test_objectID");
    deployConfig->set_bucketid("test_bucketID");
    deployConfig->set_deploydir(testDeployDir);
    deployConfig->set_storagetype("s3");

    testActor_->StartInstance(manager_->GetAID(), startRequest);

    ASSERT_AWAIT_TRUE([=]() -> bool { return !testActor_->GetStartInstanceResponse()->message().empty(); });

    auto response = testActor_->GetStartInstanceResponse();
    EXPECT_EQ(StatusCode::SUCCESS, response->code());
    EXPECT_EQ("start instance success", response->message());
    EXPECT_EQ("test_requestID", response->requestid());

    auto instanceResponse = response->startruntimeinstanceresponse();
    EXPECT_TRUE(!instanceResponse.runtimeid().empty());
    EXPECT_EQ(std::to_string(INITIAL_PORT), instanceResponse.port());

    litebus::Terminate(testActor_->GetAID());
    litebus::Await(testActor_->GetAID());
}

TEST_F(RuntimeManagerTest, StartInstance_PosixCustomRuntime_WithEntryfileEmpty)
{
    testActor_ = std::make_shared<RuntimeManagerTestActor>(GenerateRandomName("RuntimeManagerTestActor"));
    litebus::Spawn(testActor_, true);

    messages::StartInstanceRequest startRequest;
    startRequest.set_type(static_cast<int32_t>(EXECUTOR_TYPE::RUNTIME));
    auto runtimeInfo = startRequest.mutable_runtimeinstanceinfo();
    runtimeInfo->set_requestid("test_requestID");
    runtimeInfo->set_instanceid("test_instanceID");
    runtimeInfo->set_traceid("test_traceID");

    auto runtimeConfig = runtimeInfo->mutable_runtimeconfig();
    runtimeConfig->set_language("posix-custom-runtime");
    auto userEnvs = runtimeConfig->mutable_userenvs();
    userEnvs->insert({ "user_env1", "user_env1_value" });
    userEnvs->insert({ "user_env2", "user_env2_value" });

    auto deployConfig = runtimeInfo->mutable_deploymentconfig();
    deployConfig->set_objectid("test_objectID");
    deployConfig->set_bucketid("test_bucketID");
    deployConfig->set_deploydir(testDeployDir);
    deployConfig->set_storagetype("s3");

    testActor_->StartInstance(manager_->GetAID(), startRequest);

    ASSERT_AWAIT_TRUE([=]() -> bool { return !testActor_->GetStartInstanceResponse()->message().empty(); });

    auto response = testActor_->GetStartInstanceResponse();
    EXPECT_EQ(StatusCode::RUNTIME_MANAGER_EXECUTABLE_PATH_INVALID, response->code());
    EXPECT_EQ("[entryFile is empty]", response->message());
    EXPECT_EQ("test_requestID", response->requestid());

    litebus::Terminate(testActor_->GetAID());
    litebus::Await(testActor_->GetAID());
}

TEST_F(RuntimeManagerTest, StartInstanceWithPreStartFailedTest)
{
    testActor_ = std::make_shared<RuntimeManagerTestActor>(GenerateRandomName("RuntimeManagerTestActor"));
    litebus::Spawn(testActor_, true);

    messages::StartInstanceRequest startRequest;
    startRequest.set_type(static_cast<int32_t>(EXECUTOR_TYPE::RUNTIME));
    auto runtimeInfo = startRequest.mutable_runtimeinstanceinfo();
    runtimeInfo->set_requestid("test_requestID");
    runtimeInfo->set_instanceid("test_instanceID");
    runtimeInfo->set_traceid("test_traceID");

    auto runtimeConfig = runtimeInfo->mutable_runtimeconfig();
    runtimeConfig->set_language("cpp");
    auto userEnvs = runtimeConfig->mutable_userenvs();
    userEnvs->insert({ "user_env1", "user_env1_value" });
    userEnvs->insert({ "user_env2", "user_env2_value" });
    runtimeConfig->mutable_posixenvs()->insert({ "POST_START_EXEC", "/usr/bin/cp a b;" });

    auto deployConfig = runtimeInfo->mutable_deploymentconfig();
    deployConfig->set_objectid("test_objectID");
    deployConfig->set_bucketid("test_bucketID");
    deployConfig->set_deploydir(testDeployDir);
    deployConfig->set_storagetype("s3");

    testActor_->StartInstance(manager_->GetAID(), startRequest);

    ASSERT_AWAIT_TRUE([=]() -> bool { return !testActor_->GetStartInstanceResponse()->message().empty(); });

    auto response = testActor_->GetStartInstanceResponse();
    EXPECT_EQ(StatusCode::RUNTIME_MANAGER_POST_START_EXEC_FAILED, response->code());
    EXPECT_TRUE(response->message().find("is not match the regular") != std::string::npos);
    EXPECT_EQ("test_requestID", response->requestid());

    auto instanceResponse = response->startruntimeinstanceresponse();
    EXPECT_TRUE(instanceResponse.runtimeid().empty());

    litebus::Terminate(testActor_->GetAID());
    litebus::Await(testActor_->GetAID());
}

// Note: this case Connection to pypi.org timed out, set `export NOT_SKIP_LONG_TESTS=1` when run it, and not run on CI by default
TEST_F(RuntimeManagerTest, StartInstance_PosixCustomRuntime_POST_START_EXEC_pip_install_SUCCESS)
{
    const char* skip_test = std::getenv("NOT_SKIP_LONG_TESTS");
    if (skip_test == nullptr || std::string(skip_test) != "1") {
        GTEST_SKIP() << "Long-running tests are skipped by default";
    }

    testActor_ = std::make_shared<RuntimeManagerTestActor>(GenerateRandomName("RuntimeManagerTestActor"));
    litebus::Spawn(testActor_, true);

    messages::StartInstanceRequest startRequest;
    startRequest.set_type(static_cast<int32_t>(EXECUTOR_TYPE::RUNTIME));
    auto runtimeInfo = startRequest.mutable_runtimeinstanceinfo();
    runtimeInfo->set_requestid("test_requestID");
    runtimeInfo->set_instanceid("test_instanceID");
    runtimeInfo->set_traceid("test_traceID");

    auto runtimeConfig = runtimeInfo->mutable_runtimeconfig();
    runtimeConfig->set_language("posix-custom-runtime");
    runtimeConfig->set_entryfile("echo hello");
    runtimeConfig->mutable_posixenvs()->insert(
        {"LD_LIBRARY_PATH", "${LD_LIBRARY_PATH}:/opt/buildtools/python3.9/lib/"});
    runtimeConfig->mutable_posixenvs()->insert(
        { "POST_START_EXEC",
          "pip3.9 install pip-licenses==5.0.0 && pip3.9 check" });
    // both UNZIPPED_WORKING_DIR and YR_WORKING_DIR are required.
    runtimeConfig->mutable_posixenvs()->insert({ "UNZIPPED_WORKING_DIR", "/tmp" });
    runtimeConfig->mutable_posixenvs()->insert({ "YR_WORKING_DIR", "file:///tmp/file.zip" });
    auto userEnvs = runtimeConfig->mutable_userenvs();
    userEnvs->insert({ "user_env1", "user_env1_value" });

    auto deployConfig = runtimeInfo->mutable_deploymentconfig();
    deployConfig->set_objectid("test_objectID");
    deployConfig->set_bucketid("test_bucketID");
    deployConfig->set_deploydir(testDeployDir);
    deployConfig->set_storagetype("s3");

    testActor_->StartInstance(manager_->GetAID(), startRequest);

    EXPECT_AWAIT_TRUE([=]() -> bool { return !testActor_->GetStartInstanceResponse()->message().empty(); });

    auto response = testActor_->GetStartInstanceResponse();
    EXPECT_EQ(StatusCode::SUCCESS, response->code());
    EXPECT_EQ("start instance success", response->message());
    EXPECT_EQ("test_requestID", response->requestid());

    auto instanceResponse = response->startruntimeinstanceresponse();
    EXPECT_TRUE(!instanceResponse.runtimeid().empty());
    EXPECT_EQ(std::to_string(INITIAL_PORT), instanceResponse.port());

    litebus::Terminate(testActor_->GetAID());
    litebus::Await(testActor_->GetAID());
}

// Note: this case cost long and failed by other tests in CI, set `export NOT_SKIP_LONG_TESTS=1` when run it, and not run on CI by default
TEST_F(RuntimeManagerTest, StartInstance_PosixCustomRuntime_POST_START_EXEC_pip_install_FAIL)
{
    const char* skip_test = std::getenv("NOT_SKIP_LONG_TESTS");
    if (skip_test == nullptr || std::string(skip_test) != "1") {
        GTEST_SKIP() << "Long-running tests are skipped by default";
    }

    testActor_ = std::make_shared<RuntimeManagerTestActor>(GenerateRandomName("RuntimeManagerTestActor"));
    litebus::Spawn(testActor_, true);

    messages::StartInstanceRequest startRequest;
    startRequest.set_type(static_cast<int32_t>(EXECUTOR_TYPE::RUNTIME));
    auto runtimeInfo = startRequest.mutable_runtimeinstanceinfo();
    runtimeInfo->set_requestid("test_requestID");
    runtimeInfo->set_instanceid("test_instanceID");
    runtimeInfo->set_traceid("test_traceID");

    auto runtimeConfig = runtimeInfo->mutable_runtimeconfig();
    runtimeConfig->set_language("posix-custom-runtime");
    runtimeConfig->set_entryfile("echo hello");
    runtimeConfig->mutable_posixenvs()->insert(
        {"LD_LIBRARY_PATH", "${LD_LIBRARY_PATH}:/opt/buildtools/python3.9/lib/"});
    runtimeConfig->mutable_posixenvs()->insert(
        { "POST_START_EXEC",
          "pip3.9 install pip-licenses==5xxx && pip3.9 check" });
    // both UNZIPPED_WORKING_DIR and YR_WORKING_DIR are required.
    runtimeConfig->mutable_posixenvs()->insert({ "UNZIPPED_WORKING_DIR", "/tmp" });
    runtimeConfig->mutable_posixenvs()->insert({ "YR_WORKING_DIR", "file:///tmp/file.zip" });

    auto deployConfig = runtimeInfo->mutable_deploymentconfig();
    deployConfig->set_objectid("test_objectID");
    deployConfig->set_bucketid("test_bucketID");
    deployConfig->set_deploydir(testDeployDir);
    deployConfig->set_storagetype("s3");

    testActor_->StartInstance(manager_->GetAID(), startRequest);

    EXPECT_AWAIT_TRUE_FOR([=]() -> bool { return !testActor_->GetStartInstanceResponse()->message().empty(); }, 30000);

    auto response = testActor_->GetStartInstanceResponse();
    EXPECT_EQ(StatusCode::RUNTIME_MANAGER_POST_START_EXEC_FAILED, response->code());
    EXPECT_TRUE(response->message().find("failed to execute POST_START_EXEC command") != std::string::npos);
    EXPECT_EQ("test_requestID", response->requestid());

    litebus::Terminate(testActor_->GetAID());
    litebus::Await(testActor_->GetAID());
}

/**
 * Feature: StartInstanceWithInvalidRequestTest
 * Description: start instance when sending invalid message
 * Steps:
 * send invalid requset to start instance
 * Expectation:
 * start instance failed
 */
TEST_F(RuntimeManagerTest, StartInstanceWithInvalidRequestTest)
{
    testActor_ = std::make_shared<RuntimeManagerTestActor>(GenerateRandomName("RuntimeManagerTestActor"));
    litebus::Spawn(testActor_, true);

    testActor_->StartInstanceWithString(manager_->GetAID(), "");

    ASSERT_AWAIT_TRUE([=]() -> bool { return testActor_->GetStartInstanceResponse()->message().empty(); });

    litebus::Terminate(testActor_->GetAID());
    litebus::Await(testActor_->GetAID());
}

/**
 * Feature: StartInstanceWithInvalidExecutorTypeTest
 * Description: start instance when sending message with invalid executor type
 * Steps:
 * start instance request with invalid executor type
 * Expectation:
 * return RUNTIME_MANAGER_PARAMS_INVALID code in response
 */
TEST_F(RuntimeManagerTest, StartInstanceWithInvalidExecutorTypeTest)
{
    testActor_ = std::make_shared<RuntimeManagerTestActor>(GenerateRandomName("RuntimeManagerTestActor"));
    litebus::Spawn(testActor_, true);

    messages::StartInstanceRequest startRequest;
    startRequest.set_type(static_cast<int32_t>(EXECUTOR_TYPE::UNKNOWN));
    auto runtimeInfo = startRequest.mutable_runtimeinstanceinfo();
    runtimeInfo->set_requestid("test_requestID");
    runtimeInfo->set_instanceid("test_instanceID");
    runtimeInfo->set_traceid("test_traceID");

    auto runtimeConfig = runtimeInfo->mutable_runtimeconfig();
    runtimeConfig->set_language("cpp");
    auto userEnvs = runtimeConfig->mutable_userenvs();
    userEnvs->insert({ "user_env1", "user_env1_value" });
    userEnvs->insert({ "user_env2", "user_env2_value" });

    auto deployConfig = runtimeInfo->mutable_deploymentconfig();
    deployConfig->set_objectid("test_objectID");
    deployConfig->set_bucketid("test_bucketID");
    deployConfig->set_deploydir(testDeployDir);
    deployConfig->set_storagetype("s3");

    testActor_->StartInstance(manager_->GetAID(), startRequest);

    ASSERT_AWAIT_TRUE([=]() -> bool { return !testActor_->GetStartInstanceResponse()->message().empty(); });

    auto response = testActor_->GetStartInstanceResponse();
    EXPECT_EQ(StatusCode::RUNTIME_MANAGER_PARAMS_INVALID, response->code());
    EXPECT_EQ("unknown instance type, cannot start instance", response->message());

    litebus::Terminate(testActor_->GetAID());
    litebus::Await(testActor_->GetAID());
}

TEST_F(RuntimeManagerTest, StartInstanceRequestPortFailTest)
{
    // can only support one runtime instance
    PortManager::GetInstance().InitPortResource(0, 1);
    testActor_ = std::make_shared<RuntimeManagerTestActor>(GenerateRandomName("RuntimeManagerTestActor"));
    litebus::Spawn(testActor_, true);

    messages::StartInstanceRequest startRequest;
    startRequest.set_type(static_cast<int32_t>(EXECUTOR_TYPE::RUNTIME));
    auto runtimeInfo = startRequest.mutable_runtimeinstanceinfo();
    runtimeInfo->set_requestid("test_requestID");
    runtimeInfo->set_instanceid("test_instanceID");
    runtimeInfo->set_traceid("test_traceID");

    auto runtimeConfig = runtimeInfo->mutable_runtimeconfig();
    runtimeConfig->set_language("cpp");

    auto deployConfig = runtimeInfo->mutable_deploymentconfig();
    deployConfig->set_objectid("test_objectID");
    deployConfig->set_bucketid("test_bucketID");
    deployConfig->set_deploydir(testDeployDir);
    deployConfig->set_storagetype("s3");
    testActor_->StartInstance(manager_->GetAID(), startRequest);

    ASSERT_AWAIT_TRUE([=]() -> bool { return !testActor_->GetStartInstanceResponse()->message().empty(); });

    auto response = testActor_->GetStartInstanceResponse();
    EXPECT_EQ(StatusCode::SUCCESS, response->code());
    EXPECT_EQ("start instance success", response->message());
    EXPECT_EQ("test_requestID", response->requestid());

    startRequest.set_type(static_cast<int32_t>(EXECUTOR_TYPE::UNKNOWN));
    testActor_->StartInstance(manager_->GetAID(), startRequest);
    ASSERT_AWAIT_TRUE([=]() -> bool { return !testActor_->GetStartInstanceResponse()->message().empty(); });

    auto instanceResponse = response->mutable_startruntimeinstanceresponse();
    EXPECT_TRUE(!instanceResponse->runtimeid().empty());
    EXPECT_EQ("0", instanceResponse->port());

    auto testActorNew = std::make_shared<RuntimeManagerTestActor>("NewRuntimeManagerTestActor");

    litebus::Spawn(testActorNew, true);

    messages::StartInstanceRequest startRequestNew;
    startRequestNew.set_type(static_cast<int32_t>(EXECUTOR_TYPE::RUNTIME));
    auto runtimeInfoNew = startRequestNew.mutable_runtimeinstanceinfo();
    runtimeInfoNew->set_requestid("test_requestIDNew");
    runtimeInfoNew->set_instanceid("test_instanceIDNew");
    runtimeInfoNew->set_traceid("test_traceIDNew");

    auto runtimeConfigNew = runtimeInfoNew->mutable_runtimeconfig();
    runtimeConfigNew->set_language("cpp");

    testActorNew->StartInstance(manager_->GetAID(), startRequestNew);

    ASSERT_AWAIT_TRUE([=]() -> bool { return !testActorNew->GetStartInstanceResponse()->message().empty(); });

    auto responseNew = testActorNew->GetStartInstanceResponse();
    EXPECT_EQ(responseNew->code(), RUNTIME_MANAGER_PORT_UNAVAILABLE);
    EXPECT_EQ(responseNew->message(), "start instance failed");

    litebus::Terminate(testActor_->GetAID());
    litebus::Await(testActor_->GetAID());
    litebus::Terminate(testActorNew->GetAID());
    litebus::Await(testActorNew->GetAID());

    PortManager::GetInstance().InitPortResource(INITIAL_PORT, PORT_NUM);
}

TEST_F(RuntimeManagerTest, StopInstanceTest)
{
    testActor_ = std::make_shared<RuntimeManagerTestActor>(GenerateRandomName("RuntimeManagerTestActor"));
    litebus::Spawn(testActor_, true);

    messages::StartInstanceRequest startRequest;
    startRequest.set_type(static_cast<int32_t>(EXECUTOR_TYPE::RUNTIME));
    auto runtimeInfo = startRequest.mutable_runtimeinstanceinfo();
    runtimeInfo->set_requestid("test_requestID");
    runtimeInfo->set_instanceid("test_instanceID");
    runtimeInfo->set_traceid("test_traceID");

    auto runtimeConfig = runtimeInfo->mutable_runtimeconfig();
    runtimeConfig->set_language("cpp");
    auto userEnvs = runtimeConfig->mutable_userenvs();
    userEnvs->insert({ "user_env1", "user_env1_value" });
    userEnvs->insert({ "user_env2", "user_env2_value" });

    auto deployConfig = runtimeInfo->mutable_deploymentconfig();
    deployConfig->set_objectid("test_objectID");
    deployConfig->set_bucketid("test_bucketID");
    deployConfig->set_deploydir(testDeployDir);
    deployConfig->set_storagetype("s3");

    testActor_->StartInstance(manager_->GetAID(), startRequest);

    ASSERT_AWAIT_TRUE([=]() -> bool { return !testActor_->GetStartInstanceResponse()->message().empty(); });

    auto response = testActor_->GetStartInstanceResponse();
    EXPECT_EQ(StatusCode::SUCCESS, response->code());
    EXPECT_EQ("start instance success", response->message());
    EXPECT_EQ("test_requestID", response->requestid());

    auto instanceResponse = response->mutable_startruntimeinstanceresponse();
    auto resRuntimeID = instanceResponse->runtimeid();
    EXPECT_TRUE(!resRuntimeID.empty());
    EXPECT_EQ(std::to_string(INITIAL_PORT), instanceResponse->port());

    const std::string stopRequestID = "test_requestID";
    messages::StopInstanceRequest request;
    request.set_runtimeid(resRuntimeID);
    request.set_requestid(stopRequestID);
    request.set_type(static_cast<int32_t>(EXECUTOR_TYPE::RUNTIME));
    // lost connection with function agent
    manager_->connected_ = false;
    testActor_->StopInstance(manager_->GetAID(), request);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(testActor_->GetIsReceiveStopInstanceResponse(), false);

    manager_->connected_ = true;
    // success
    testActor_->StopInstance(manager_->GetAID(), request);
    EXPECT_TRUE(manager_->healthCheckClient_->actor_->runtimeStatus_.find(resRuntimeID)
                != manager_->healthCheckClient_->actor_->runtimeStatus_.end());
    ASSERT_AWAIT_TRUE([=]() -> bool { return testActor_->GetStopInstanceResponse()->requestid() == stopRequestID; });
    EXPECT_EQ(testActor_->GetStopInstanceResponse()->code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_EQ(testActor_->GetStopInstanceResponse()->message(), "stop instance success");
    EXPECT_EQ(testActor_->GetStopInstanceResponse()->runtimeid(), resRuntimeID);
    EXPECT_TRUE(manager_->healthCheckClient_->actor_->runtimeStatus_.find(resRuntimeID)
                == manager_->healthCheckClient_->actor_->runtimeStatus_.end());

    litebus::Terminate(testActor_->GetAID());
    litebus::Await(testActor_->GetAID());
}


TEST_F(RuntimeManagerTest, StopInstanceFailTest)
{
    testActor_ = std::make_shared<RuntimeManagerTestActor>(GenerateRandomName("RuntimeManagerTestActor"));
    litebus::Spawn(testActor_, true);

    messages::StopInstanceRequest request;
    request.set_runtimeid("test_runtimeID");
    request.set_requestid("test_requestID");
    request.set_type(static_cast<int32_t>(EXECUTOR_TYPE::RUNTIME));
    testActor_->StopInstance(manager_->GetAID(), request);

    auto stopResponse = testActor_->GetStopInstanceResponse();
    ASSERT_AWAIT_TRUE([=]() -> bool { return testActor_->GetStopInstanceResponse()->requestid() == "test_requestID"; });
    EXPECT_EQ(testActor_->GetStopInstanceResponse()->runtimeid(), "test_runtimeID");
    EXPECT_EQ(testActor_->GetStopInstanceResponse()->code(),
              static_cast<int32_t>(RUNTIME_MANAGER_RUNTIME_PROCESS_NOT_FOUND));
    EXPECT_EQ(testActor_->GetStopInstanceResponse()->message(), "stop instance failed");

    litebus::Terminate(testActor_->GetAID());
    litebus::Await(testActor_->GetAID());
}

/**
 * Feature: StopInstanceWithInvalidRequestTest
 * Description: Stop Instance When sending invalid request
 * Steps:
 * send invalid request to stop instance
 * Expectation:
 * no response
 */
TEST_F(RuntimeManagerTest, StopInstanceWithInvalidRequestTest)
{
    testActor_ = std::make_shared<RuntimeManagerTestActor>(GenerateRandomName("RuntimeManagerTestActor"));
    litebus::Spawn(testActor_, true);

    testActor_->StartInstanceWithString(manager_->GetAID(), "");

    ASSERT_AWAIT_TRUE([=]() -> bool { return testActor_->GetStartInstanceResponse()->message().empty(); });

    litebus::Terminate(testActor_->GetAID());
    litebus::Await(testActor_->GetAID());
}

/**
 * Feature: RegisterToFunctionAgentFailedTest
 * Description: runtime manager register to function agent return error code
 * Steps:
 * runtime manager register to function agent
 * function agent return REGISTER_ERROR in response
 * Expectation:
 * runtime manager print the error in log
 */
TEST_F(RuntimeManagerTest, RegisterToFunctionAgentFailedTest)
{
    testActor_ = std::make_shared<RuntimeManagerTestActor>("AgentServiceActor-RegisterHelper");
    litebus::Spawn(testActor_, true);

    functionsystem::runtime_manager::Flags flags;
    std::string agentAddress = "--agent_address=" + testActor_->GetAID().Url();
    const char *argv[] = {
        "/runtime_manager",
        "--node_id=node1",
        "--ip=127.0.0.1",
        "--host_ip=127.0.0.1",
        "--port=32233",
        "--runtime_initial_port=500",
        "--port_num=2000",
        "--runtime_dir=/tmp",
        const_cast<char *>(agentAddress.c_str()),
        "--runtime_ld_library_path=/tmp",
        "--proc_metrics_cpu=2000",
        "--proc_metrics_memory=2000",
        R"(--log_config={"filepath": "/home/yr/log", "level": "DEBUG", "rolling": {"maxsize": 100, "maxfiles": 1},"alsologtostderr":true})"
    };
    flags.ParseFlags(13, argv);
    manager_->SetRegisterHelper(std::make_shared<RegisterHelper>("node1-RuntimeManagerSrv"));
    manager_->SetConfig(flags);
    messages::RegisterRuntimeManagerResponse registerRuntimeManagerResponse;
    registerRuntimeManagerResponse.set_code(StatusCode::REGISTER_ERROR);
    testActor_->SetRegisterRuntimeManagerResponse(registerRuntimeManagerResponse);
    manager_->Start();

    ASSERT_AWAIT_TRUE([=]() -> bool { return !testActor_->GetRegisterRuntimeManagerRequest()->address().empty(); });
    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    EXPECT_TRUE(("127.0.0.1:" + std::to_string(port)) == testActor_->GetRegisterRuntimeManagerRequest()->address());
    EXPECT_TRUE(runtimeManagerActorName_ == testActor_->GetRegisterRuntimeManagerRequest()->name());
    EXPECT_TRUE(testActor_->GetRegisterRuntimeManagerRequest()->mutable_runtimeinstanceinfos()->size() == 0);

    litebus::Terminate(testActor_->GetAID());
    litebus::Await(testActor_->GetAID());
}

/**
 * Feature: RegisterToFunctionAgentUnknownErrorTest
 * Description: runtime manager register to function agent return error code
 * Steps:
 * runtime manager register to function agent
 * function agent return unknown Error in response
 * Expectation:
 * runtime manager print the error in log
 */
TEST_F(RuntimeManagerTest, RegisterToFunctionAgentUnknownErrorTest)
{
    testActor_ = std::make_shared<RuntimeManagerTestActor>("AgentServiceActor-RegisterHelper");
    litebus::Spawn(testActor_, true);

    functionsystem::runtime_manager::Flags flags;
    std::string agentAddress = "--agent_address=" + testActor_->GetAID().Url();
    const char *argv[] = {
        "/runtime_manager",
        "--node_id=node1",
        "--ip=127.0.0.1",
        "--host_ip=127.0.0.1",
        "--port=32233",
        "--runtime_initial_port=500",
        "--port_num=2000",
        "--runtime_dir=/tmp",
        const_cast<char *>(agentAddress.c_str()),
        "--runtime_ld_library_path=/tmp",
        "--proc_metrics_cpu=2000",
        "--proc_metrics_memory=2000",
        R"(--log_config={"filepath": "/home/yr/log", "level": "DEBUG", "rolling": {"maxsize": 100, "maxfiles": 1},"alsologtostderr":true})"
    };
    flags.ParseFlags(13, argv);
    manager_->SetRegisterHelper(std::make_shared<RegisterHelper>("node1-RuntimeManagerSrv"));
    manager_->SetConfig(flags);
    messages::RegisterRuntimeManagerResponse registerRuntimeManagerResponse;
    registerRuntimeManagerResponse.set_code(StatusCode::FAILED);
    testActor_->SetRegisterRuntimeManagerResponse(registerRuntimeManagerResponse);
    manager_->Start();

    ASSERT_AWAIT_TRUE([=]() -> bool { return !testActor_->GetRegisterRuntimeManagerRequest()->address().empty(); });
    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    EXPECT_TRUE(("127.0.0.1:" + std::to_string(port)) == testActor_->GetRegisterRuntimeManagerRequest()->address());
    EXPECT_TRUE(runtimeManagerActorName_ == testActor_->GetRegisterRuntimeManagerRequest()->name());
    EXPECT_TRUE(testActor_->GetRegisterRuntimeManagerRequest()->mutable_runtimeinstanceinfos()->size() == 0);

    litebus::Terminate(testActor_->GetAID());
    litebus::Await(testActor_->GetAID());
}

TEST_F(RuntimeManagerTest, RegisterToFunctionAgentTimeoutTest)
{
    manager_->isUnitTestSituation_ = false;

    functionsystem::runtime_manager::Flags flags;
    const char *argv[] = {
        "/runtime_manager",
        "--node_id=node1",
        "--ip=127.0.0.1",
        "--host_ip=127.0.0.1",
        "--port=32233",
        "--runtime_initial_port=500",
        "--port_num=2000",
        "--runtime_dir=/tmp",
        const_cast<char *>("127.0.0.1:80"),
        "--runtime_ld_library_path=/tmp",
        "--proc_metrics_cpu=2000",
        "--proc_metrics_memory=2000",
        R"(--log_config={"filepath": "/home/yr/log", "level": "DEBUG", "rolling": {"maxsize": 100, "maxfiles": 1},"alsologtostderr":true})"
    };
    flags.ParseFlags(13, argv);
    manager_->SetRegisterHelper(std::make_shared<RegisterHelper>("node1-RuntimeManagerSrv"));
    manager_->SetConfig(flags);
    manager_->SetRegisterInterval(5);
    signal(SIGINT, sigHandler);
    manager_->Start();
    ASSERT_AWAIT_READY(sigReceived_);

    manager_->isUnitTestSituation_ = true;
}

/**
 * Feature: QueryInstanceStatusInfoTest
 * Description: function agent query the instance info
 * Steps:
 * function agent send to runtime manager to query instance info
 * Expectation:
 * get current response
 */
TEST_F(RuntimeManagerTest, QueryInstanceStatusInfoTest)
{
    testActor_ = std::make_shared<RuntimeManagerTestActor>(GenerateRandomName("RuntimeManagerTestActor"));
    litebus::Spawn(testActor_, true);

    messages::QueryInstanceStatusRequest request;
    request.set_requestid("request_id");
    request.set_instanceid("instance_id");
    request.set_runtimeid("runtime_id");

    // lost connection with function agent
    manager_->connected_ = false;
    testActor_->QueryInstanceStatusInfo(manager_->GetAID(), request);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(testActor_->GetIsReceiveQueryInstanceStatusInfoResponse(), false);
    manager_->connected_ = true;
    testActor_->QueryInstanceStatusInfo(manager_->GetAID(), request);
    ASSERT_AWAIT_TRUE([=]() -> bool { return !testActor_->GetQueryInstanceStatusResponse()->requestid().empty(); });
    EXPECT_EQ(testActor_->GetQueryInstanceStatusResponse()->instancestatusinfo().type(),
              static_cast<int32_t>(EXIT_TYPE::NONE_EXIT));

    manager_->instanceInfoMap_["runtime_id"] = messages::RuntimeInstanceInfo();
    // success
    testActor_->QueryInstanceStatusInfo(manager_->GetAID(), request);
    ASSERT_AWAIT_TRUE([=]() -> bool { return !testActor_->GetQueryInstanceStatusResponse()->requestid().empty(); });
    EXPECT_TRUE(testActor_->GetQueryInstanceStatusResponse()->requestid() == "request_id");
    litebus::Terminate(testActor_->GetAID());
    litebus::Await(testActor_->GetAID());
}

TEST_F(RuntimeManagerTest, CleanStatusTest)
{
    testActor_ = std::make_shared<RuntimeManagerTestActor>(GenerateRandomName("RuntimeManagerTestActor"));
    litebus::Spawn(testActor_, true);

    testActor_->Send(manager_->GetAID(), "CleanStatus", "invalid msg&&");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(testActor_->GetIsReceiveCleanStatusResponse());
    messages::CleanStatusRequest cleanStatusRequest;
    cleanStatusRequest.set_name("invalid RuntimeManagerID");
    testActor_->Send(manager_->GetAID(), "CleanStatus", cleanStatusRequest.SerializeAsString());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(testActor_->GetIsReceiveCleanStatusResponse());
    testActor_->ResetIsReceiveCleanStatusResponse();
    cleanStatusRequest.set_name(manager_->runtimeManagerID_);
    testActor_->Send(manager_->GetAID(), "CleanStatus", cleanStatusRequest.SerializeAsString());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(testActor_->GetIsReceiveCleanStatusResponse());

    litebus::Terminate(testActor_->GetAID());
    litebus::Await(testActor_->GetAID());
}

/**
 * Feature: UpdateTokenTest
 * Description: update token for runtime while token is expired, request from InstanceCtrlActor
 * Steps:
 * 1. agent forward UpdateToken Request to RuntimeManager, and then RuntimeManger refresh token for runtime
 * and return UpdateTokenResponse
 */
TEST_F(RuntimeManagerTest, UpdateTokenTest)
{
    testActor_ = std::make_shared<RuntimeManagerTestActor>(GenerateRandomName("RuntimeManagerTestActor"));
    litebus::Spawn(testActor_, true);

    messages::UpdateCredRequest request;
    manager_->connected_ = false;
    testActor_->Send(manager_->GetAID(), "UpdateCred", "invalid msg#");

    manager_->executorMap_.emplace(EXECUTOR_TYPE::RUNTIME, nullptr);
    testActor_->Send(manager_->GetAID(), "UpdateCred", request.SerializeAsString());
    ASSERT_AWAIT_TRUE([=]() -> bool {
        return testActor_->GetIsReceiveUpdateTokenResponse()->code() ==
               static_cast<int32_t>(RUNTIME_MANAGER_PARAMS_INVALID);
    });
    manager_->executorMap_.clear();
    testActor_->Send(manager_->GetAID(), "UpdateCred", request.SerializeAsString());
    ASSERT_AWAIT_TRUE([=]() -> bool {
        return testActor_->GetIsReceiveUpdateTokenResponse()->code() == static_cast<int32_t>(SUCCESS);
    });

    litebus::Terminate(testActor_->GetAID());
    litebus::Await(testActor_->GetAID());
}
}  // namespace functionsystem::runtime_manager