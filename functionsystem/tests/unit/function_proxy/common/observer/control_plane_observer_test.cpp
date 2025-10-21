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

#include "function_proxy/common/observer/control_plane_observer/control_plane_observer.h"

#include <gtest/gtest.h>

#include <memory>

#include "common/constants/actor_name.h"
#include "common/etcd_service/etcd_service_driver.h"
#include "meta_storage_accessor/meta_storage_accessor.h"
#include "meta_store_client/meta_store_client.h"
#include "resource_type.h"
#include "common/types/instance_state.h"
#include "common/utils/struct_transfer.h"
#include "function_proxy/common/observer/observer_actor.h"
#include "function_proxy/common/posix_client/shared_client/shared_client_manager.h"
#include "function_proxy/common/posix_client/shared_client/posix_stream_manager_proxy.h"
#include "litebus.hpp"
#include "mocks/mock_meta_store_client.h"
#include "utils/future_test_helper.h"
#include "utils/generate_info.h"
#include "utils/port_helper.h"

namespace functionsystem::test {
using namespace resource_view;
using namespace function_proxy;

class ObserverTest : public ::testing::Test {
protected:
    inline static std::unique_ptr<meta_store::test::EtcdServiceDriver> etcdSrvDriver_;
    inline static std::string metaStoreServerHost_;

    [[maybe_unused]] static void SetUpTestCase()
    {
        etcdSrvDriver_ = std::make_unique<meta_store::test::EtcdServiceDriver>();
        int metaStoreServerPort = functionsystem::test::FindAvailablePort();
        metaStoreServerHost_ = "127.0.0.1:" + std::to_string(metaStoreServerPort);
        etcdSrvDriver_->StartServer(metaStoreServerHost_);
        sharedClientMgr_ = std::make_shared<SharedClientManager>("SharedPosixClientManager");
        litebus::Spawn(sharedClientMgr_);
        auto sharedPosixClientManager = std::make_shared<PosixStreamManagerProxy>(sharedClientMgr_->GetAID());
        auto config = MetaStoreConfig{ .etcdAddress = metaStoreServerHost_  };
        config.etcdTablePrefix = "/test";
        metaStoreClient_ = MetaStoreClient::Create(config);
        metaStorageAccessor_ = std::make_shared<MetaStorageAccessor>(metaStoreClient_);

        function_proxy::ObserverParam param;
        param.servicesPath = "/tmp/services.yaml";
        param.libPath = "/tmp/";
        param.functionMetaPath = "/tmp/executor-meta";
        observerActor_ = std::make_shared<function_proxy::ObserverActor>(FUNCTION_PROXY_OBSERVER_ACTOR_NAME, nodeID_,
                                                                         metaStorageAccessor_, std::move(param));

        observerActor_->BindDataInterfaceClientManager(sharedPosixClientManager);
        litebus::Spawn(observerActor_);
        controlPlaneObserver_ = std::make_shared<function_proxy::ControlPlaneObserver>(observerActor_);
        controlPlaneObserver_->Register();
        // one from meta json, three from services.yaml
        ASSERT_AWAIT_TRUE([&]() -> bool { return observerActor_->funcMetaMap_.size() == 4; });
    }

    [[maybe_unused]] static void TearDownTestCase()
    {
        YRLOG_INFO("TearDownTestCase......");
        metaStorageAccessor_->metaClient_ = metaStoreClient_;
        YRLOG_INFO("TearDownTestCase......Finish");

        litebus::Terminate(observerActor_->GetAID());
        litebus::Await(observerActor_);

        litebus::Terminate(sharedClientMgr_->GetAID());
        litebus::Await(sharedClientMgr_);

        observerActor_ = nullptr;
        sharedClientMgr_ = nullptr;
        controlPlaneObserver_ = nullptr;
        metaStorageAccessor_ = nullptr;
        metaStoreClient_ = nullptr;
        etcdSrvDriver_->StopServer();
    }

    void SetUp() override
    {
    }

    void TearDown() override
    {
        YRLOG_INFO("TearDown......");
        metaStorageAccessor_->metaClient_ = metaStoreClient_;
        YRLOG_INFO("TearDown......Finish");
    }

protected:
    inline static const std::string nodeID_ = "nodeA";
    inline static std::shared_ptr<MetaStorageAccessor> metaStorageAccessor_;
    inline static std::shared_ptr<function_proxy::ControlPlaneObserver> controlPlaneObserver_;
    inline static std::shared_ptr<SharedClientManager> sharedClientMgr_;
    inline static std::shared_ptr<function_proxy::ObserverActor> observerActor_;
    inline static std::shared_ptr<MetaStoreClient> metaStoreClient_;
};

void CheckInstanceInfo(const InstanceInfo &l, const InstanceInfo &r)
{
    EXPECT_EQ(l.instanceid(), r.instanceid());
    EXPECT_EQ(l.functionagentid(), r.functionagentid());
    EXPECT_EQ(l.function(), r.function());
    EXPECT_EQ(l.instancestatus().code(), r.instancestatus().code());
}

std::vector<WatchEvent> GetProxyEventRsp(const EventType &eventType, const std::string &proxyID)
{
    auto key = BUSPROXY_PATH_PREFIX + "/0/node/" + proxyID;
    auto jsonStr = R"({"node":")" + proxyID + R"(","aid":")" + proxyID + R"("})";
    KeyValue kv;
    kv.set_key(key);
    kv.set_value(jsonStr);
    auto event = WatchEvent{ eventType, kv, {} };

    std::vector<WatchEvent> events{ event };
    return events;
}

TEST_F(ObserverTest, GetInstanceInfo)
{
    // get non exited instance info
    std::string nonExistedInstanceID = "nonExistedInstanceId";
    controlPlaneObserver_->DelInstance(nonExistedInstanceID).Get();
    controlPlaneObserver_->observerActor_->instanceInfoMap_.clear();
    auto res = controlPlaneObserver_->GetInstanceInfoByID(nonExistedInstanceID);
    ASSERT_AWAIT_TRUE([&]() { return res.Get().IsNone(); });

    std::string instanceID = "instanceA";
    std::string funcAgentID = "funcAgentM";
    std::string function = "123/helloworld/$latest";
    auto instanceStatus = InstanceState::RUNNING;
    auto instanceInfo = GenInstanceInfo(instanceID, funcAgentID, function, instanceStatus);

    // put instance to meta store
    auto status = controlPlaneObserver_->PutInstance(instanceInfo).Get();
    EXPECT_TRUE(status.IsOk());

    ASSERT_AWAIT_TRUE([&]() -> bool { return controlPlaneObserver_->GetInstanceInfoByID(instanceID).Get().IsSome(); });

    // get instanceInfo by instanceID
    auto getInsInfoOption = controlPlaneObserver_->GetInstanceInfoByID(instanceID).Get();
    EXPECT_TRUE(getInsInfoOption.IsSome());
    auto getInsInfo = getInsInfoOption.Get();
    CheckInstanceInfo(getInsInfo, instanceInfo);

    // instance in map
    res = controlPlaneObserver_->GetInstanceInfoByID(instanceID);
    ASSERT_AWAIT_TRUE([&]() { return res.Get().IsSome(); });

    // delete instance info
    status = controlPlaneObserver_->DelInstance(instanceID).Get();
    EXPECT_TRUE(status.IsOk());

    ASSERT_AWAIT_TRUE([&]() -> bool { return controlPlaneObserver_->GetInstanceInfoByID(instanceID).Get().IsNone(); });
}

TEST_F(ObserverTest, GetAgentInstanceInfo)
{
    std::string funcAgentID = "funcAgent";

    std::string instanceIDA = "instanceA";
    std::string functionA = "123/helloworld-A/$latest";
    InstanceState instanceStatusA = InstanceState::RUNNING;
    auto instanceInfoA = GenInstanceInfo(instanceIDA, funcAgentID, functionA, instanceStatusA);

    std::string instanceIDB = "instanceB";
    std::string functionB = "123/helloworld-B/$latest";
    InstanceState instanceStatusB = InstanceState::RUNNING;
    auto instanceInfoB = GenInstanceInfo(instanceIDB, funcAgentID, functionB, instanceStatusB);

    // put instance to meta store
    auto status = controlPlaneObserver_->PutInstance(instanceInfoA).Get();
    EXPECT_TRUE(status.IsOk());
    status = controlPlaneObserver_->PutInstance(instanceInfoB).Get();
    EXPECT_TRUE(status.IsOk());

    ASSERT_AWAIT_TRUE([&]() -> bool {
        auto opt = controlPlaneObserver_->GetAgentInstanceInfoByID(funcAgentID).Get();
        return opt.IsSome() && opt.Get().size() == 2;
    });

    // get instanceInfo by functionAgentID
    auto agentInsInfoOption = controlPlaneObserver_->GetAgentInstanceInfoByID(funcAgentID).Get();
    EXPECT_TRUE(agentInsInfoOption.IsSome());
    auto agentInsInfo = agentInsInfoOption.Get();
    EXPECT_TRUE(agentInsInfo.find(instanceIDA) != agentInsInfo.end());
    EXPECT_TRUE(agentInsInfo.find(instanceIDB) != agentInsInfo.end());
    // check element num
    EXPECT_TRUE(agentInsInfo.size() == 2);
    // check instanceInfo param value
    auto getInsInfo = agentInsInfo[instanceIDA];
    CheckInstanceInfo(getInsInfo, instanceInfoA);
    getInsInfo = agentInsInfo[instanceIDB];
    CheckInstanceInfo(getInsInfo, instanceInfoB);

    // delete instance info
    status = controlPlaneObserver_->DelInstance(instanceIDA).Get();
    EXPECT_TRUE(status.IsOk());
    status = controlPlaneObserver_->DelInstance(instanceIDB).Get();
    EXPECT_TRUE(status.IsOk());

    ASSERT_AWAIT_TRUE([&]() -> bool {
        return controlPlaneObserver_->GetInstanceInfoByID(instanceIDA).Get().IsNone()
               && controlPlaneObserver_->GetInstanceInfoByID(instanceIDB).Get().IsNone();
    });

    ASSERT_AWAIT_TRUE(
        [&]() -> bool { return controlPlaneObserver_->GetAgentInstanceInfoByID(funcAgentID).Get().IsNone(); });
}

TEST_F(ObserverTest, GetLocalInstanceInfo)
{
    std::string funcAgentID = "funcAgent";
    std::string function = "123/helloworld/$latest";

    std::string instanceIDA = "instanceA";
    InstanceState instanceStatusA = InstanceState::RUNNING;
    auto instanceInfoA = GenInstanceInfo(instanceIDA, funcAgentID, function, instanceStatusA);
    instanceInfoA.set_functionproxyid(nodeID_);

    std::string instanceIDB = "instanceB";
    InstanceState instanceStatusB = InstanceState::RUNNING;
    auto instanceInfoB = GenInstanceInfo(instanceIDB, funcAgentID, function, instanceStatusB);
    instanceInfoB.set_functionproxyid("proxyID");

    // put instance to meta store
    auto status = controlPlaneObserver_->PutInstance(instanceInfoA).Get();
    EXPECT_TRUE(status.IsOk());
    status = controlPlaneObserver_->PutInstance(instanceInfoB).Get();
    EXPECT_TRUE(status.IsOk());

    // get local instanceInfo
    auto localInstanceInfoFuture = controlPlaneObserver_->GetLocalInstanceInfo();
    ASSERT_AWAIT_READY(localInstanceInfoFuture);
    auto localInstanceInfoOpt = localInstanceInfoFuture.Get();
    EXPECT_TRUE(localInstanceInfoOpt.IsSome());
    auto localInfo = localInstanceInfoOpt.Get();
    EXPECT_TRUE(localInfo.size() == 1);
    EXPECT_EQ(localInfo.at("instanceA").functionproxyid(), nodeID_);
    EXPECT_EQ(localInfo.at("instanceA").instanceid(), "instanceA");

    // delete instance info
    status = controlPlaneObserver_->DelInstance(instanceIDA).Get();
    EXPECT_TRUE(status.IsOk());
    status = controlPlaneObserver_->DelInstance(instanceIDB).Get();
    EXPECT_TRUE(status.IsOk());

    ASSERT_AWAIT_TRUE([&]() -> bool {
        auto future = controlPlaneObserver_->GetLocalInstanceInfo();
        return future.Get().IsNone();
    });
}

/**
 * Feature:
 * Description: MetaStorageAccessor is null, failed to put and delete instance
 * Steps:
 * 1. Create PosixStreamManagerProxy
 * 2. Set MetaStorageAccessor null, spawn ObserverActor
 * 3. Put, delete instance
 * Expectation:
 * 1. Status is Failed.
 */
TEST_F(ObserverTest, ErrMetaStorageAccessor)
{
    auto sharedClientMgr1_ = std::make_shared<SharedClientManager>("ErrSharedPosixClientManager");
    litebus::Spawn(sharedClientMgr1_);
    auto sharedPosixClientManager1 = std::make_shared<PosixStreamManagerProxy>(sharedClientMgr1_->GetAID());

    std::shared_ptr<MetaStorageAccessor> metaStorageAccessor1_ = nullptr;
    function_proxy::ObserverParam param;
    param.servicesPath = "/tmp/services.yaml";
    param.libPath = "/tmp/";
    auto observerActor1_ = std::make_shared<function_proxy::ObserverActor>("err_observer", "node",
                                                                           metaStorageAccessor1_, std::move(param));
    observerActor1_->BindDataInterfaceClientManager(sharedPosixClientManager1);
    litebus::Spawn(observerActor1_);

    std::string funcAgentID = "funcAgent";
    std::string function = "0-yrjava-yr-smoke/version/$latest";
    std::string funcKey = "12345678901234561234567890123456/0-yrjava-yr-smoke/$latest";

    std::string instanceIDA = "instanceA";
    InstanceState instanceStatusA = InstanceState::RUNNING;
    auto instanceInfoA = GenInstanceInfo(instanceIDA, funcAgentID, funcKey, instanceStatusA);

    // put function meta to meta store
    auto status = observerActor1_->PutInstance(instanceInfoA).Get();
    EXPECT_TRUE(status.IsError());

    status = observerActor1_->DelInstance(instanceIDA).Get();
    EXPECT_TRUE(status.IsError());

    litebus::Terminate(observerActor1_->GetAID());
    litebus::Await(observerActor1_);
    litebus::Terminate(sharedClientMgr1_->GetAID());
    litebus::Await(sharedClientMgr1_);
    observerActor1_ = nullptr;
    sharedClientMgr1_ = nullptr;
}

/**
 * Feature:ObserverTest
 * Description:  function accessor/driver instance event
 * Steps:
 * 1. Add function accessor event callback
 * 2. Add driver event callback
 * 3. Put function accessor event
 * 4. Put driver event
 * Expectation:
 * 1. CallBack function is called.
 */
TEST_F(ObserverTest, DriverCallBack)
{
    auto driverPromise = std::make_shared<litebus::Promise<Status>>();
    controlPlaneObserver_->SetDriverEventCbFunc(
        [driverPromise](const resource_view::InstanceInfo &instanceInfo) { driverPromise->SetValue(Status::OK()); });
    std::string funcAgentID = "funcAgent";
    std::string funcKey = "/sn/instance/business/yrk/tenant/0/function/functionaccessor/version/$latest/defaultaz";
    std::string instanceIDA = "10.10.10.10";
    InstanceState instanceStatusA = InstanceState::RUNNING;
    auto instanceInfoA = GenInstanceInfo(instanceIDA, funcAgentID, funcKey, instanceStatusA);
    instanceInfoA.set_functionproxyid(nodeID_);
    (*instanceInfoA.mutable_extensions())["source"] = "driver";
    controlPlaneObserver_->PutInstanceEvent(instanceInfoA, false, 1);
    EXPECT_TRUE(driverPromise->GetFuture().Get().IsOk());
    controlPlaneObserver_->DelInstanceEvent(instanceIDA);
}

/**
 * Feature:ObserverTest
 * Description:  fast published remote instance event
 */
TEST_F(ObserverTest, FastPutRemoteInstanceEvent)
{
    auto mockMetaStoreClient  = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
    metaStorageAccessor_->metaClient_ = mockMetaStoreClient;
    std::string funcKey = "12345678901234561234567890123456/0-system-faasExecutorPython3.9/$latest";
    std::string funcAgentID = "funcAgent";
    InstanceState instanceStatusA = InstanceState::RUNNING;
    auto instanceInfo1 = GenInstanceInfo("instance0001", funcAgentID, funcKey, instanceStatusA);
    instanceInfo1.set_functionproxyid("dggpalpha00001");
    EXPECT_EQ(GetModRevisionFromInstanceInfo(instanceInfo1), 0);
    (*instanceInfo1.mutable_extensions())["modRevision"] = "AA";
    EXPECT_EQ(GetModRevisionFromInstanceInfo(instanceInfo1), 0);
    (*instanceInfo1.mutable_extensions())["modRevision"] = "10";
    EXPECT_EQ(GetModRevisionFromInstanceInfo(instanceInfo1), 10);

    auto key1 = R"(/yr/route/business/yrk/instance0001)";
    auto value1status3 = R"({"instanceID":"instance0001","runtimeAddress":"127.0.0.1:22771","functionAgentID":"function-agent-poolx-2","function":"12345678901234561234567890123456/0-system-faasExecutorPython3.9/$latest","functionProxyID":"dggpalpha00001","instanceStatus":{"code":3,"msg":"running"},"jobID":"job-12345678","parentID":"d94bd8af-e8d7-42ed-90e3-b6cd59bc6dc9","requestID":"requestID1","tenantID":"12345678901234561234567890123456","version":"3"})";
    litebus::Future<std::shared_ptr<GetResponse>> getResponseFuture;
    std::shared_ptr<GetResponse> rep = std::make_shared<GetResponse>();
    rep->header.revision = 10;
    rep->status = Status::OK();
    KeyValue inst1;
    inst1.set_key(key1) ;
    inst1.set_value(value1status3);
    inst1.set_mod_revision(10);
    rep->kvs.emplace_back(inst1);
    getResponseFuture.SetValue(rep);
    EXPECT_CALL(*mockMetaStoreClient, Get).Times(1).WillOnce(testing::Return(getResponseFuture));
    controlPlaneObserver_->FastPutRemoteInstanceEvent(instanceInfo1, false, GetModRevisionFromInstanceInfo(instanceInfo1));
    ASSERT_AWAIT_TRUE([&]() -> bool { return observerActor_->instanceInfoMap_.count("instance0001") != 0; });
    (*instanceInfo1.mutable_extensions())["modRevision"] = "11";
    controlPlaneObserver_->FastPutRemoteInstanceEvent(instanceInfo1, false, GetModRevisionFromInstanceInfo(instanceInfo1));
    ASSERT_AWAIT_TRUE([&]() -> bool { return observerActor_->instanceModRevisionMap_.count("instance0001") != 0 && observerActor_->instanceModRevisionMap_["instance0001"] == 11;});
    metaStorageAccessor_->metaClient_ = metaStoreClient_;
}

/**
 * Feature:
 * Description: Set wrong function key, put failed
 * Steps:
 * 1. Set wrong function key
 * 2. Put instance whose info is wrong
 * 3. Put failed
 * Expectation:
 * 1. Status is Failed.
 */
TEST_F(ObserverTest, PutDeleteEvent)
{
    std::string funcAgentID = "funcAgent";
    std::string funcKey = "12345678901234561234567890123456/0-yrjava-yr-smoke/$latest/err";

    std::string instanceIDA = "instanceA";
    InstanceState instanceStatusA = InstanceState::RUNNING;
    auto instanceInfoA = GenInstanceInfo(instanceIDA, funcAgentID, funcKey, instanceStatusA);
    // put instance to meta store
    auto status = controlPlaneObserver_->PutInstance(instanceInfoA).Get();
    EXPECT_TRUE(status.IsError());

    status = controlPlaneObserver_->DelInstance(instanceIDA).Get();
    EXPECT_TRUE(status.IsOk());
}

/**
 * Feature:
 * Description: Get delete event whose node is not owner
 * Steps:
 * 1. Put an instance to control plane observer
 * 2. Delete the instance
 * 3. Get the deleted instance
 * Expectation:
 * 1. Status is Ok.
 */
TEST_F(ObserverTest, ProcFuncMetaEvent)
{
    std::string funcMetaJson =
        R"({"funcMetaData":{"layers":[{"appId":"appA","bucketId":"bucketA","objectId":"objectA","bucketUrl":"bucketUrlA","sha256":"1a2b3c"}],"name":"0-yrjava-yr-smoke","description":"","functionUrn":"sn:cn:yrk:12345678901234561234567890123456:function:0-yrjava-yr-smoke","functionVersionUrn":"sn:cn:yrk:12345678901234561234567890123456:function:0-yrjava-yr-smoke:$latest","codeSize":22029378,"codeSha256":"1211a06","handler":"fusion_computation_handler.fusion_computation_handler","runtime":"java1.8","timeout":900,"tenantId":"12345678901234561234567890123456","hookHandler":{"call":"com.actorTaskCallHandler"}},"codeMetaData":{"storage_type":"s3","appId":"61022","bucketId":"bucket-test-log1","objectId":"yr-smoke-1667888605803","bucketUrl":"http://bucket-test-log1.hwcloudtest.cn:18085"},"envMetaData":{"envKey":"1d34ef","environment":"e819e3","encrypted_user_data":""},"resourceMetaData":{"cpu":500,"memory":500,"customResources":""}})";
    std::string path =
        "/yr/functions/business/yrk/tenant/12345678901234561234567890123456/function/0-yrjava-yr-smoke/version/$latest";
    std::string funcKey = "12345678901234561234567890123456/0-yrjava-yr-smoke/$latest";

    // put function meta to meta store
    auto status = metaStorageAccessor_->Put(path, funcMetaJson).Get();
    EXPECT_TRUE(status.IsOk());

    std::string funcAgentID = "funcAgent";

    std::string instanceIDA = "instanceA";
    InstanceState instanceStatusA = InstanceState::RUNNING;
    auto instanceInfoA = GenInstanceInfo(instanceIDA, funcAgentID, funcKey, instanceStatusA);
    instanceInfoA.set_functionproxyid(nodeID_);
    // put instance to meta store
    status = controlPlaneObserver_->PutInstance(instanceInfoA).Get();
    EXPECT_TRUE(status.IsOk());

    // delete function meta in meta store
    metaStorageAccessor_->Delete(path).Get();
    status = metaStorageAccessor_->Delete(path).Get();
    EXPECT_TRUE(status.IsOk());
    ASSERT_AWAIT_TRUE([&]() -> bool { return controlPlaneObserver_->GetFuncMeta(funcKey).Get().IsNone(); });
}

TEST_F(ObserverTest, GetFuncMetaInfo)
{
    std::string funcMetaJson =
        R"({"funcMetaData":{"layers":[{"appId":"appA","bucketId":"bucketA","objectId":"objectA","bucketUrl":"bucketUrlA","sha256":"1a2b3c"}],"name":"0-yrjava-yr-smoke","description":"","functionUrn":"sn:cn:yrk:12345678901234561234567890123456:function:0-yrjava-yr-smoke","functionVersionUrn":"sn:cn:yrk:12345678901234561234567890123456:function:0-yrjava-yr-smoke:$latest","codeSize":22029378,"codeSha256":"1211a06","handler":"fusion_computation_handler.fusion_computation_handler","runtime":"java1.8","timeout":900,"tenantId":"12345678901234561234567890123456","hookHandler":{"call":"com.actorTaskCallHandler"}},"codeMetaData":{"storage_type":"s3","appId":"61022","bucketId":"bucket-test-log1","objectId":"yr-smoke-1667888605803","bucketUrl":"http://bucket-test-log1.hwcloudtest.cn:18085"},"envMetaData":{"envKey":"1d34ef","environment":"e819e3","encrypted_user_data":""},"resourceMetaData":{"cpu":500,"memory":500,"customResources":""}})";
    std::string path =
        "/yr/functions/business/yrk/tenant/12345678901234561234567890123456/function/0-yrjava-yr-smoke/version/$latest";

    // put function meta to meta store
    auto status = metaStorageAccessor_->Put(path, funcMetaJson).Get();
    EXPECT_TRUE(status.IsOk());

    auto funcKey = "12345678901234561234567890123456/0-yrjava-yr-smoke/$latest";
    ASSERT_AWAIT_TRUE([&]() -> bool { return controlPlaneObserver_->GetFuncMeta(funcKey).Get().IsSome(); });
    auto getFuncMetaOpt = controlPlaneObserver_->GetFuncMeta(funcKey).Get();

    auto funcMeta = getFuncMetaOpt.Get();
    // check FuncMataData
    EXPECT_TRUE(funcMeta.funcMetaData.urn
                == "sn:cn:yrk:12345678901234561234567890123456:function:0-yrjava-yr-smoke:$latest");
    EXPECT_TRUE(funcMeta.funcMetaData.runtime == "java1.8");
    EXPECT_TRUE(funcMeta.funcMetaData.entryFile == "fusion_computation_handler.fusion_computation_handler");
    EXPECT_TRUE(funcMeta.funcMetaData.handler.empty());
    EXPECT_TRUE(funcMeta.funcMetaData.codeSha256 == "1211a06");
    EXPECT_TRUE(funcMeta.funcMetaData.hookHandler["call"] == "com.actorTaskCallHandler");

    // check CodeMetaData
    EXPECT_TRUE(funcMeta.codeMetaData.storageType == "s3");
    EXPECT_TRUE(funcMeta.codeMetaData.bucketID == "bucket-test-log1");
    EXPECT_TRUE(funcMeta.codeMetaData.objectID == "yr-smoke-1667888605803");
    EXPECT_TRUE(funcMeta.codeMetaData.deployDir == "/dcache");
    EXPECT_TRUE(funcMeta.codeMetaData.layers.size() == 1);
    EXPECT_TRUE(funcMeta.codeMetaData.layers[0].appID == "appA");
    EXPECT_TRUE(funcMeta.codeMetaData.layers[0].bucketID == "bucketA");
    EXPECT_TRUE(funcMeta.codeMetaData.layers[0].objectID == "objectA");
    EXPECT_TRUE(funcMeta.codeMetaData.layers[0].bucketURL == "bucketUrlA");
    EXPECT_TRUE(funcMeta.codeMetaData.layers[0].sha256 == "1a2b3c");

    // check EnvMetaData
    EXPECT_TRUE(funcMeta.envMetaData.envKey == "1d34ef");
    EXPECT_TRUE(funcMeta.envMetaData.envInfo == "e819e3");
    EXPECT_TRUE(funcMeta.envMetaData.encryptedUserData.empty());

    // delete function meta in meta store
    metaStorageAccessor_->Delete(path).Get();
    status = metaStorageAccessor_->Delete(path).Get();
    EXPECT_TRUE(status.IsOk());

    ASSERT_AWAIT_TRUE([&]() -> bool { return controlPlaneObserver_->GetFuncMeta(funcKey).Get().IsNone(); });
}

TEST_F(ObserverTest, GetFuncMetaWithOutCache)
{
    function_proxy::ObserverParam param;
    param.servicesPath = "/tmp/services.yaml";
    param.libPath = "/tmp/";
    const auto observerActor = std::make_shared<ObserverActor>(FUNCTION_PROXY_OBSERVER_ACTOR_NAME + "123", nodeID_,
                                                               metaStorageAccessor_, std::move(param));
    const auto &aid = litebus::Spawn(observerActor);

    const std::string funcMetaJson =
        R"({"funcMetaData":{"layers":[{"appId":"appA","bucketId":"bucketA","objectId":"objectA","bucketUrl":"bucketUrlA","sha256":"1a2b3c"}],"name":"0-yrjava-yr-smoke","description":"","functionUrn":"sn:cn:yrk:12345678901234561234567890123456:function:0-yrjava-yr-smoke","functionVersionUrn":"sn:cn:yrk:12345678901234561234567890123456:function:0-yrjava-yr-smoke:$latest","codeSize":22029378,"codeSha256":"1211a06","handler":"fusion_computation_handler.fusion_computation_handler","runtime":"java1.8","timeout":900,"tenantId":"12345678901234561234567890123456","hookHandler":{"call":"com.actorTaskCallHandler"}},"codeMetaData":{"storage_type":"s3","appId":"61022","bucketId":"bucket-test-log1","objectId":"yr-smoke-1667888605803","bucketUrl":"http://bucket-test-log1.hwcloudtest.cn:18085"},"envMetaData":{"envKey":"1d34ef","environment":"e819e3","encrypted_user_data":""},"resourceMetaData":{"cpu":500,"memory":500,"customResources":""}})";
    const std::string path =
        "/yr/functions/business/yrk/tenant/12345678901234561234567890123456/function/0-yrjava-yr-smoke/version/$latest";

    const auto f = metaStorageAccessor_->Put(path, funcMetaJson);
    EXPECT_AWAIT_READY(f);  // wait for ready
    EXPECT_TRUE(f.Get().IsOk());

    auto future = litebus::Async(aid, &ObserverActor::GetFuncMeta, "0/1223");
    EXPECT_AWAIT_READY(future);  // wait for ready
    EXPECT_TRUE(future.Get().IsNone());

    auto funcKey = "12345678901234561234567890123456/0-yrjava-yr-smoke/$latest";
    future = litebus::Async(aid, &ObserverActor::GetFuncMeta, funcKey);
    EXPECT_AWAIT_READY(future);

    EXPECT_EQ(future.Get().Get().funcMetaData.urn,
              "sn:cn:yrk:12345678901234561234567890123456:function:0-yrjava-yr-smoke:$latest");

    litebus::Terminate(observerActor->GetAID());
    litebus::Await(observerActor);
}

class MockUpdateFuncMetasFunc {
public:
    MOCK_METHOD(void, UpdateFuncMetas, (bool isAdd, (const std::unordered_map<std::string, FunctionMeta> &funcMetas)));
};

TEST_F(ObserverTest, SetUpdateFuncMetasFunc)
{
    std::string funcMetaJson =
        R"({"funcMetaData":{"layers":[{"appId":"appA","bucketId":"bucketA","objectId":"objectA","bucketUrl":"bucketUrlA","sha256":"1a2b3c"}],"name":"0-yrjava-yr-smoke","description":"","functionUrn":"sn:cn:yrk:12345678901234561234567890123456:function:0-yrjava-yr-smoke","functionVersionUrn":"sn:cn:yrk:12345678901234561234567890123456:function:0-yrjava-yr-smoke:$latest","codeSize":22029378,"codeSha256":"1211a06","handler":"fusion_computation_handler.fusion_computation_handler","runtime":"java1.8","timeout":900,"tenantId":"12345678901234561234567890123456","hookHandler":{"call":"com.actorTaskCallHandler"}},"codeMetaData":{"storage_type":"s3","appId":"61022","bucketId":"bucket-test-log1","objectId":"yr-smoke-1667888605803","bucketUrl":"http://bucket-test-log1.hwcloudtest.cn:18085"},"envMetaData":{"envKey":"1d34ef","environment":"e819e3","encrypted_user_data":""},"resourceMetaData":{"cpu":500,"memory":500,"customResources":""}})";
    std::string path =
        "/yr/functions/business/yrk/tenant/12345678901234561234567890123456/function/0-yrjava-yr-smoke/version/$latest";
    std::string funcKey = "12345678901234561234567890123456/0-yrjava-yr-smoke/$latest";

    bool isFinished = false;
    auto mockUpdateFuncMetasFunc = std::make_shared<MockUpdateFuncMetasFunc>();
    EXPECT_CALL(*mockUpdateFuncMetasFunc, UpdateFuncMetas)
        .WillOnce(testing::Return())
        .WillOnce(testing::DoAll(testing::Assign(&isFinished, true), testing::Return()));
    controlPlaneObserver_->SetUpdateFuncMetasFunc(
        [&](bool isAdd, const std::unordered_map<std::string, FunctionMeta> &funcMetas) {
            mockUpdateFuncMetasFunc->UpdateFuncMetas(isAdd, funcMetas);
        });

    ASSERT_AWAIT_TRUE([&]() { return isFinished; });

    litebus::Future<bool> isAdd;
    litebus::Future<std::unordered_map<std::string, FunctionMeta>> funcMetas;
    EXPECT_CALL(*mockUpdateFuncMetasFunc, UpdateFuncMetas)
        .WillOnce(testing::DoAll(FutureArg<0>(&isAdd), FutureArg<1>(&funcMetas), testing::Return()));
    // put function meta to meta store
    auto status = metaStorageAccessor_->Put(path, funcMetaJson).Get();
    EXPECT_TRUE(status.IsOk());

    ASSERT_AWAIT_READY(isAdd);
    EXPECT_TRUE(isAdd.Get());
    ASSERT_AWAIT_READY(funcMetas);
    EXPECT_EQ(funcMetas.Get().size(), static_cast<long unsigned int>(1));
    EXPECT_FALSE(funcMetas.Get().at(funcKey).funcMetaData.isSystemFunc);

    litebus::Future<bool> isAdd2;
    litebus::Future<std::unordered_map<std::string, FunctionMeta>> funcMetas2;
    EXPECT_CALL(*mockUpdateFuncMetasFunc, UpdateFuncMetas)
        .WillOnce(testing::DoAll(FutureArg<0>(&isAdd2), FutureArg<1>(&funcMetas2), testing::Return()));
    // delete function meta in meta store
    metaStorageAccessor_->Delete(path).Get();
    status = metaStorageAccessor_->Delete(path).Get();

    EXPECT_TRUE(status.IsOk());
    ASSERT_AWAIT_READY(isAdd2);
    EXPECT_FALSE(isAdd2.Get());
    ASSERT_AWAIT_READY(funcMetas2);
    EXPECT_EQ(funcMetas2.Get().size(), static_cast<long unsigned int>(1));
    EXPECT_TRUE(funcMetas2.Get().find(funcKey) != funcMetas2.Get().end());

    controlPlaneObserver_->SetUpdateFuncMetasFunc(nullptr);

    ASSERT_AWAIT_TRUE([&]() -> bool { return controlPlaneObserver_->GetFuncMeta(funcKey).Get().IsNone(); });
}

TEST_F(ObserverTest, SetUpdateSysFuncMetasFunc)
{
    std::string funcMetaJson =
        R"({"funcMetaData":{"layers":[{"appId":"appA","bucketId":"bucketA","objectId":"objectA","bucketUrl":"bucketUrlA","sha256":"1a2b3c"}],"name":"0-yrjava-yr-smoke","description":"","functionUrn":"sn:cn:yrk:12345678901234561234567890123456:function:0-yrjava-yr-smoke","functionVersionUrn":"sn:cn:yrk:12345678901234561234567890123456:function:0-yrjava-yr-smoke:$latest","codeSize":22029378,"codeSha256":"1211a06","handler":"fusion_computation_handler.fusion_computation_handler","runtime":"java1.8","timeout":900,"tenantId":"0","hookHandler":{"call":"com.actorTaskCallHandler"}},"codeMetaData":{"storage_type":"s3","appId":"61022","bucketId":"bucket-test-log1","objectId":"yr-smoke-1667888605803","bucketUrl":"http://bucket-test-log1.hwcloudtest.cn:18085"},"envMetaData":{"envKey":"1d34ef","environment":"e819e3","encrypted_user_data":""},"resourceMetaData":{"cpu":500,"memory":500,"customResources":""}})";
    std::string path =
        "/yr/functions/business/yrk/tenant/0/function/0-yrjava-yr-smoke/version/$latest";
    std::string funcKey = "0/0-yrjava-yr-smoke/$latest";

    bool isFinished = false;
    auto mockUpdateFuncMetasFunc = std::make_shared<MockUpdateFuncMetasFunc>();
    EXPECT_CALL(*mockUpdateFuncMetasFunc, UpdateFuncMetas)
        .WillOnce(testing::Return())
        .WillOnce(testing::DoAll(testing::Assign(&isFinished, true), testing::Return()));
    controlPlaneObserver_->SetUpdateFuncMetasFunc(
        [&](bool isAdd, const std::unordered_map<std::string, FunctionMeta> &funcMetas) {
            mockUpdateFuncMetasFunc->UpdateFuncMetas(isAdd, funcMetas);
        });

    ASSERT_AWAIT_TRUE([&]() { return isFinished; });

    litebus::Future<bool> isAdd;
    litebus::Future<std::unordered_map<std::string, FunctionMeta>> funcMetas;
    EXPECT_CALL(*mockUpdateFuncMetasFunc, UpdateFuncMetas)
        .WillOnce(testing::DoAll(FutureArg<0>(&isAdd), FutureArg<1>(&funcMetas), testing::Return()));
    // put function meta to meta store
    auto status = metaStorageAccessor_->Put(path, funcMetaJson).Get();
    EXPECT_TRUE(status.IsOk());

    ASSERT_AWAIT_READY(isAdd);
    EXPECT_TRUE(isAdd.Get());
    ASSERT_AWAIT_READY(funcMetas);
    EXPECT_EQ(funcMetas.Get().size(), static_cast<long unsigned int>(1));

    litebus::Future<bool> isAdd2;
    litebus::Future<std::unordered_map<std::string, FunctionMeta>> funcMetas2;
    EXPECT_CALL(*mockUpdateFuncMetasFunc, UpdateFuncMetas)
        .WillOnce(testing::DoAll(FutureArg<0>(&isAdd2), FutureArg<1>(&funcMetas2), testing::Return()));
    // delete function meta in meta store
    metaStorageAccessor_->Delete(path).Get();
    status = metaStorageAccessor_->Delete(path).Get();

    EXPECT_TRUE(status.IsOk());
    ASSERT_AWAIT_READY(isAdd2);
    EXPECT_FALSE(isAdd2.Get());
    ASSERT_AWAIT_READY(funcMetas2);
    EXPECT_EQ(funcMetas2.Get().size(), 1);
    EXPECT_TRUE(funcMetas2.Get().find(funcKey) != funcMetas2.Get().end());

    controlPlaneObserver_->SetUpdateFuncMetasFunc(nullptr);
}

TEST_F(ObserverTest, GetLocalSchedulerAID)
{
    const std::string proxyID = "proxyID";
    auto proxyPutRsp = GetProxyEventRsp(EVENT_TYPE_PUT, proxyID);
    litebus::Async(observerActor_->GetAID(), &function_proxy::ObserverActor::UpdateProxyEvent, proxyPutRsp);

    auto future = controlPlaneObserver_->GetLocalSchedulerAID(proxyID);
    auto AIDOption = future.Get();
    ASSERT_TRUE(AIDOption.IsSome());
    EXPECT_STREQ(AIDOption.Get().Name().c_str(),
                 std::string(proxyID + LOCAL_SCHED_INSTANCE_CTRL_ACTOR_NAME_POSTFIX).c_str());

    auto proxyDelRsp = GetProxyEventRsp(EVENT_TYPE_DELETE, proxyID);
    litebus::Async(observerActor_->GetAID(), &function_proxy::ObserverActor::UpdateProxyEvent, proxyDelRsp);

    future = controlPlaneObserver_->GetLocalSchedulerAID(proxyID);
    AIDOption = future.Get();
    ASSERT_TRUE(AIDOption.IsNone());
}

class TestTenantListener : public TenantListener {
public:
    TestTenantListener() : updateCount(0), deleteCount(0)
    {}
    void OnTenantUpdateInstance(const TenantEvent &event) override
    {
        ++updateCount;
    }
    void OnTenantDeleteInstance(const TenantEvent &event) override
    {
        ++deleteCount;
    }
    int GetUpdateCount() const
    {
        return updateCount;
    }
    int GetDeleteCount() const
    {
        return deleteCount;
    }

private:
    int updateCount;
    int deleteCount;
};  // class TestTenantListener

TEST_F(ObserverTest, NotifyTenantEvent)
{
    TenantEvent event;
    auto listener = std::make_shared<TestTenantListener>();
    controlPlaneObserver_->AttachTenantListener(listener);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    controlPlaneObserver_->NotifyUpdateTenantInstance(event);
    controlPlaneObserver_->NotifyDeleteTenantInstance(event);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(listener->GetUpdateCount(), 1);
    EXPECT_EQ(listener->GetDeleteCount(), 1);
    controlPlaneObserver_->DetachTenantListener(listener);
}

TEST_F(ObserverTest, NotifyTenantEvent_EmptyFunctionAgentID_When_ResourcesNotEnough)
{
    auto listener = std::make_shared<TestTenantListener>();
    controlPlaneObserver_->AttachTenantListener(listener);
    std::unordered_map<std::string, TenantEvent> lastTenantEventCacheMap;
    std::string mockEventKvKey =
        "/sn/instance/business/yrk/tenant/12345678901234561234567890123456/function/0-yrcpp-yr-tenantid/version/"
        "$latest/defaultaz/c81bdbb95673c89300/db690100-0000-4000-8018-320280e3b05f";

    TenantEvent event1;
    event1.tenantID = "tenantA";
    event1.functionProxyID = "dggphispra26945";
    event1.instanceID = "db690100-0000-4000-8018-320280e3b05f";
    event1.code = static_cast<int>(InstanceState::SCHEDULING);
    controlPlaneObserver_->NotifyUpdateTenantInstance(event1);
    lastTenantEventCacheMap[mockEventKvKey] = event1;

    TenantEvent event2;
    event2.tenantID = "tenantA";
    event2.functionProxyID = "dggphispra26945";
    event2.instanceID = "db690100-0000-4000-8018-320280e3b05f";
    event2.code = static_cast<int>(InstanceState::FATAL);
    controlPlaneObserver_->NotifyUpdateTenantInstance(event2);
    lastTenantEventCacheMap[mockEventKvKey] = event2;


    TenantEvent event3;
    event3.tenantID = "tenantA";
    event3.functionProxyID = "dggphispra26945";
    event3.instanceID = "db690100-0000-4000-8018-320280e3b05f";
    event3.code = static_cast<int>(InstanceState::EXITING);
    controlPlaneObserver_->NotifyUpdateTenantInstance(event3);
    lastTenantEventCacheMap[mockEventKvKey] = event3;

    std::string mockDeleteEventKvKey = mockEventKvKey;
    controlPlaneObserver_->NotifyDeleteTenantInstance(lastTenantEventCacheMap[mockDeleteEventKvKey]);

    ASSERT_AWAIT_TRUE([&]() { return listener->GetUpdateCount() == 3; });
    ASSERT_AWAIT_TRUE([&]() { return listener->GetDeleteCount() == 1; });

    controlPlaneObserver_->DetachTenantListener(listener);
}

TEST_F(ObserverTest, FailedOrEmptySyncerTest)
{
    auto mockMetaStoreClient  = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
    metaStorageAccessor_->metaClient_ = mockMetaStoreClient;
    {
        litebus::Future<std::shared_ptr<GetResponse>> getResponseFuture;
        std::shared_ptr<GetResponse> rep = std::make_shared<GetResponse>();
        rep->status = Status(StatusCode::FAILED, "");
        getResponseFuture.SetValue(rep);
        EXPECT_CALL(*mockMetaStoreClient, Get).WillRepeatedly(testing::Return(getResponseFuture));
        auto future = observerActor_->FunctionMetaSyncer();
        ASSERT_AWAIT_READY(future);
        ASSERT_FALSE(future.Get().status.IsOk());

        future = observerActor_->InstanceInfoSyncer();
        ASSERT_AWAIT_READY(future);
        ASSERT_FALSE(future.Get().status.IsOk());

        future = observerActor_->BusProxySyncer();
        ASSERT_AWAIT_READY(future);
        ASSERT_FALSE(future.Get().status.IsOk());
    }

    {
        litebus::Future<std::shared_ptr<GetResponse>> getResponseFuture;
        std::shared_ptr<GetResponse> rep = std::make_shared<GetResponse>();
        rep->status = Status::OK();
        getResponseFuture.SetValue(rep);
        EXPECT_CALL(*mockMetaStoreClient, Get).WillRepeatedly(testing::Return(getResponseFuture));
        auto future = observerActor_->FunctionMetaSyncer();
        ASSERT_AWAIT_READY(future);
        ASSERT_TRUE(future.Get().status.IsOk());

        future = observerActor_->InstanceInfoSyncer();
        ASSERT_AWAIT_READY(future);
        ASSERT_TRUE(future.Get().status.IsOk());

        future = observerActor_->BusProxySyncer();
        ASSERT_AWAIT_READY(future);
        ASSERT_TRUE(future.Get().status.IsOk());
    }
    metaStorageAccessor_->metaClient_ = metaStoreClient_;
}

TEST_F(ObserverTest, BusProxySyncerTest)
{
    auto mockMetaStoreClient  = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
    metaStorageAccessor_->metaClient_ = mockMetaStoreClient;

    auto key = R"(/yr/busproxy/business/yrk/tenant/0/node/siaphisprg00912)";
    auto value = R"({"aid":"function_proxysiaphisprg00912@127.0.0.1:22772","node":"siaphisprg00912"})";

    KeyValue getKeyValue;
    getKeyValue.set_key(key) ;
    getKeyValue.set_value(value);

    litebus::Future<std::shared_ptr<GetResponse>> getResponseFuture;
    std::shared_ptr<GetResponse> rep = std::make_shared<GetResponse>();
    rep->status = Status::OK();
    rep->kvs.emplace_back(getKeyValue);
    getResponseFuture.SetValue(rep);
    EXPECT_CALL(*mockMetaStoreClient, Get).WillOnce(testing::Return(getResponseFuture));

    auto future = observerActor_->BusProxySyncer();
    ASSERT_AWAIT_READY(future);
    ASSERT_TRUE(future.Get().status.IsOk());

    auto proxyMeta = GetProxyMeta(value);
    EXPECT_TRUE(observerActor_->proxyView_->Get(proxyMeta.node) != nullptr);
    EXPECT_TRUE(observerActor_->localSchedulerView_->Get(proxyMeta.node) != nullptr);

    metaStorageAccessor_->metaClient_ = metaStoreClient_;
}

TEST_F(ObserverTest, FunctionMetaSyncerTest)
{
    auto mockMetaStoreClient  = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
    metaStorageAccessor_->metaClient_ = mockMetaStoreClient;

    auto key = R"(/yr/functions/business/yrk/tenant/12345678901234561234567890123456/function/0@faaspy@hello/version/latest)";
    auto meta = R"({"funcMetaData":{"layers":[],"name":"0-yrcc0260e787-test-func-serialization","description":"","functionUrn":"sn:cn:yrk:12345678901234561234567890123456:function:0-yrcc0260e787-test-func-serialization","reversedConcurrency":0,"tags":null,"functionUpdateTime":"","functionVersionUrn":"sn:cn:yrk:12345678901234561234567890123456:function:0-yrcc0260e787-test-func-serialization:$latest","codeSize":3020,"codeSha256":"","codeSha512":"123","handler":"fusion_computation_handler.fusion_computation_handler","runtime":"python3.9","timeout":900,"version":"$latest","versionDescription":"$latest","deadLetterConfig":"","latestVersionUpdateTime":"","publishTime":"","businessId":"yrk","tenantId":"12345678901234561234567890123456","domain_id":"","project_name":"","revisionId":"20240822042544986","created":"2024-08-13 08:27:19.912 UTC","statefulFlag":false,"hookHandler":{"call":"yrlib_handler.call","checkpoint":"yrlib_handler.checkpoint","init":"yrlib_handler.init","recover":"yrlib_handler.recover","shutdown":"yrlib_handler.shutdown","signal":"yrlib_handler.signal"}}})";
    auto key1 = R"(/yr/functions/business/yrk/tenant/12345678901234561234567890123456/function/0-system-faasExecutorGo1.x/version/$latest)";
    std::string meta1Json = R"({"funcMetaData":{"layers":[],"name":"0-system-faasExecutorGo1.x","description":"","functionUrn":"sn:cn:yrk:12345678901234561234567890123456:function:0-system-faasExecutorGo1.x","reversedConcurrency":0,"tags":null,"functionUpdateTime":"","functionVersionUrn":"sn:cn:yrk:12345678901234561234567890123456:function:0-system-faasExecutorGo1.x:$latest","codeSize":0,"codeSha256":"0","handler":"","runtime":"go1.13","timeout":900,"version":"$latest","versionDescription":"$latest","deadLetterConfig":"","latestVersionUpdateTime":"","publishTime":"","businessId":"yrk","tenantId":"12345678901234561234567890123456","domain_id":"","project_name":"","revisionId":"20230116102015135","created":"2023-01-1610:20:15.135UTC","statefulFlag":false,"hookHandler":{"call":"faas-executor.CallHandler","checkpoint":"faas-executor.CheckPointHandler","init":"faas-executor.InitHandler","recover":"faas-executor.RecoverHandler","shutdown":"faas-executor.ShutDownHandler","signal":"faas-executor.SignalHandler","health":"faas-executor.HealthCheckHandler"}},"codeMetaData":{"storage_type":"local","code_path":"/home/sn/system-function-packages/executor-function/go1.x"},"envMetaData":{"envKey":"","environment":"","encrypted_user_data":""},"resourceMetaData":{"cpu":500,"memory":500,"customResources":""},"extendedMetaData":{"image_name":"","role":{"xrole":"","app_xrole":""},"mount_config":{"mount_user":{"user_id":0,"user_group_id":0},"func_mounts":null},"strategy_config":{"concurrency":0},"extend_config":"","initializer":{"initializer_handler":"","initializer_timeout":0},"enterprise_project_id":"","log_tank_service":{"logGroupId":"","logStreamId":""},"tracing_config":{"tracing_ak":"","tracing_sk":"","project_name":""},"user_type":"","instance_meta_data":{"maxInstance":100,"minInstance":0,"concurrentNum":100,"cacheInstance":0},"extended_handler":null,"extended_timeout":null}})";
    auto key2 = R"(/yr/functions/business/yrk/tenant/12345678901234561234567890123456/function/0-system-faasExecutorPython3.9/version/$latest)";
    std::string meta2Json = R"({"funcMetaData":{"layers":[],"name":"0-system-faasExecutorPython3.9","description":"","functionUrn":"sn:cn:yrk:12345678901234561234567890123456:function:0-system-faasExecutorPython3.9","reversedConcurrency":0,"tags":null,"functionUpdateTime":"","functionVersionUrn":"sn:cn:yrk:12345678901234561234567890123456:function:0-system-faasExecutorPython3.9:$latest","codeSize":0,"codeSha256":"0","handler":"","runtime":"python3.9","timeout":900,"version":"$latest","versionDescription":"$latest","deadLetterConfig":"","latestVersionUpdateTime":"","publishTime":"","businessId":"yrk","tenantId":"12345678901234561234567890123456","domain_id":"","project_name":"","revisionId":"20230116102015135","created":"2023-01-1610:20:15.135UTC","statefulFlag":false,"hookHandler":{"call":"faas_executor.faasCallHandler","checkpoint":"faas_executor.faasCheckPointHandler","init":"faas_executor.faasInitHandler","recover":"faas_executor.faasRecoverHandler","shutdown":"faas_executor.faasShutDownHandler","signal":"faas_executor.faasSignalHandler"}},"codeMetaData":{"storage_type":"local","code_path":"/home/sn/system-function-packages/executor-function/python3.8"},"envMetaData":{"envKey":"","environment":"","encrypted_user_data":""},"resourceMetaData":{"cpu":500,"memory":500,"customResources":""},"extendedMetaData":{"image_name":"","role":{"xrole":"","app_xrole":""},"mount_config":{"mount_user":{"user_id":0,"user_group_id":0},"func_mounts":null},"strategy_config":{"concurrency":0},"extend_config":"","initializer":{"initializer_handler":"","initializer_timeout":0},"enterprise_project_id":"","log_tank_service":{"logGroupId":"","logStreamId":""},"tracing_config":{"tracing_ak":"","tracing_sk":"","project_name":""},"user_type":"","instance_meta_data":{"maxInstance":100,"minInstance":0,"concurrentNum":100,"cacheInstance":0},"extended_handler":null,"extended_timeout":null}})";
    auto key3 = R"(/yr/functions/business/yrk/tenant/0/function/0-system-faascontroller/version/$latest)";
    std::string meta3Json = R"({"funcMetaData":{"layers":[],"name":"0-system-faascontroller","description":"","functionUrn":"sn:cn:yrk:0:function:0-system-faascontroller","reversedConcurrency":0,"tags":null,"functionUpdateTime":"","functionVersionUrn":"sn:cn:yrk:0:function:0-system-faascontroller:$latest","codeSize":14391796,"codeSha256":"0","handler":"","runtime":"go1.13","timeout":900,"version":"$latest","versionDescription":"$latest","deadLetterConfig":"","latestVersionUpdateTime":"","publishTime":"","businessId":"yrk","tenantId":"0","domain_id":"","project_name":"","revisionId":"20230116102015135","created":"2023-01-16 10:20:15.135 UTC","statefulFlag":false,"hookHandler":{"call":"faascontroller.CallHandler","init":"faascontroller.InitHandler","checkpoint":"faascontroller.CheckpointHandler","recover":"faascontroller.RecoverHandler","shutdown":"faascontroller.ShutdownHandler","signal":"faascontroller.SignalHandler"}},"codeMetaData":{"storage_type":"local","code_path":"/home/sn/system-function-packages/faascontroller"},"envMetaData":{"envKey":"","environment":"","encrypted_user_data":""},"resourceMetaData":{"cpu":500,"memory":500,"customResources":""},"extendedMetaData":{"image_name":"","role":{"xrole":"","app_xrole":""},"mount_config":{"mount_user":{"user_id":0,"user_group_id":0},"func_mounts":null},"strategy_config":{"concurrency":0},"extend_config":"","initializer":{"initializer_handler":"","initializer_timeout":0},"enterprise_project_id":"","log_tank_service":{"logGroupId":"","logStreamId":""},"tracing_config":{"tracing_ak":"","tracing_sk":"","project_name":""},"user_type":"","instance_meta_data":{"maxInstance":100,"minInstance":0,"concurrentNum":100,"cacheInstance":0},"extended_handler":null,"extended_timeout":null}})";
    FunctionMeta meta1 = GetFuncMetaFromJson(meta1Json);
    FunctionMeta meta2 = GetFuncMetaFromJson(meta2Json);
    FunctionMeta meta3 = GetFuncMetaFromJson(meta3Json);

    auto funcKey1 = GetFuncName(meta1.funcMetaData.name, meta1.funcMetaData.version, meta1.funcMetaData.tenantId);
    auto funcKey2 = GetFuncName(meta2.funcMetaData.name, meta2.funcMetaData.version, meta2.funcMetaData.tenantId);
    auto funcKey3 = GetFuncName(meta3.funcMetaData.name, meta3.funcMetaData.version, meta3.funcMetaData.tenantId);

    observerActor_->localFuncMetaSet_.emplace(funcKey1.Get());
    observerActor_->OnPutMeta(false, funcKey2.Get(), meta2);
    observerActor_->OnPutMeta(true, funcKey3.Get(), meta3);

    EXPECT_TRUE(observerActor_->localFuncMetaSet_.count(funcKey1.Get()) == 1);
    EXPECT_TRUE(observerActor_->funcMetaMap_.count(funcKey2.Get()) == 1);
    EXPECT_TRUE(observerActor_->systemFuncMetaMap_.count(funcKey3.Get()) == 1);

    observerActor_->funcMetaMap_["deleteKey"] = FunctionMeta(); // mock not local cache key, need to delete

    KeyValue getKeyValue;
    getKeyValue.set_key(key) ;
    getKeyValue.set_value(meta);

    litebus::Future<std::shared_ptr<GetResponse>> getResponseFuture;
    std::shared_ptr<GetResponse> rep = std::make_shared<GetResponse>();
    rep->status = Status::OK();
    rep->kvs.emplace_back(getKeyValue);
    getResponseFuture.SetValue(rep);
    EXPECT_CALL(*mockMetaStoreClient, Get).WillOnce(testing::Return(getResponseFuture));
    auto counter = std::make_shared<std::atomic<int>>(0);
    observerActor_->SetUpdateFuncMetasFunc(
        [cnt(counter)](bool isAdd, const std::unordered_map<std::string, FunctionMeta> &funcMetas) {
            if (!isAdd) {
                cnt->fetch_add(funcMetas.size());
            }
        });
    auto future = observerActor_->FunctionMetaSyncer();
    ASSERT_AWAIT_READY(future);
    ASSERT_TRUE(future.Get().status.IsOk());

    auto funcKey = GetFuncKeyFromFuncMetaPath(key);
    EXPECT_TRUE(observerActor_->funcMetaMap_.count(funcKey) == 1);
    EXPECT_TRUE(observerActor_->funcMetaMap_.count(funcKey2.Get()) == 0);
    EXPECT_TRUE(observerActor_->funcMetaMap_.count(funcKey3.Get()) == 0);
    EXPECT_TRUE(observerActor_->funcMetaMap_.count("deleteKey") == 0); // not local cache key, need to delete
    EXPECT_TRUE(observerActor_->localFuncMetaSet_.count(funcKey1.Get()) == 1);
    EXPECT_TRUE(observerActor_->systemFuncMetaMap_.count(funcKey3.Get()) == 0);
    EXPECT_EQ(*counter, 3);
    observerActor_->SetUpdateFuncMetasFunc(nullptr);
}

std::vector<WatchEvent> GenerateResponseRouteEvent(std::string NodeID) {

    // write into cache and need to update
    auto key1 = R"(/yr/route/business/yrk/InstanceID1)";
    auto value1status1 = R"({"instanceID":"InstanceID1","runtimeAddress":"127.0.0.1:22771","functionAgentID":"function-agent-poolx-1","function":"12345678901234561234567890123456/0-system-faasExecutorPython3.9/$latest","functionProxyID":"dggpalpha00001","instanceStatus":{"code":1,"msg":"scheduling"},"jobID":"job-12345678","parentID":"d94bd8af-e8d7-42ed-90e3-b6cd59bc6dc9","requestID":"requestID1","tenantID":"12345678901234561234567890123456","version":"1"})";
    auto value1status3 = R"({"instanceID":"InstanceID1","runtimeAddress":"127.0.0.1:22771","functionAgentID":"function-agent-poolx-2","function":"12345678901234561234567890123456/0-system-faasExecutorPython3.9/$latest","functionProxyID":"dggpalpha00001","instanceStatus":{"code":3,"msg":"running"},"jobID":"job-12345678","parentID":"d94bd8af-e8d7-42ed-90e3-b6cd59bc6dc9","requestID":"requestID1","tenantID":"12345678901234561234567890123456","version":"3"})";

    // get from etcd and need to write into cache
    auto key2 = R"(/yr/route/business/yrk/InstanceID2)";
    auto value2  = R"({"instanceID":"InstanceID2","runtimeAddress":"127.0.0.1:22771","functionAgentID":"function-agent-poolx-2","function":"12345678901234561234567890123456/0-system-faasExecutorPython3.9/$latest","functionProxyID":"dggpalpha00001","instanceStatus":{"code":3,"msg":"running"},"jobID":"job-12345678","parentID":"d94bd8af-e8d7-42ed-90e3-b6cd59bc6dc9","requestID":"requestID2","tenantID":"12345678901234561234567890123456","version":"3"})";

    // not in etcd but in cache need to delete
    auto key3 = R"(/yr/route/business/yrk/InstanceID3)";
    auto value3 = R"({"instanceID":"InstanceID3","runtimeAddress":"127.0.0.1:22771","functionAgentID":"function-agent-poolx-3","function":"12345678901234561234567890123456/0-system-faasExecutorPython3.9/$latest","functionProxyID":"dggpalpha00001","instanceStatus":{"code":3,"msg":"running"},"jobID":"job-12345678","parentID":"d94bd8af-e8d7-42ed-90e3-b6cd59bc6dc9","requestID":"requestID3","tenantID":"12345678901234561234567890123456","version":"3"})";

    // belong to self, need to update by self
    auto key4 = R"(/yr/route/business/yrk/InstanceID4)";
    std::string value4 = R"({"instanceID":"InstanceID4","runtimeAddress":"127.0.0.1:22771","functionAgentID":"function-agent-poolx-4","function":"12345678901234561234567890123456/0-system-faasExecutorPython3.9/$latest","functionProxyID":"XXXXXXX","instanceStatus":{"code":3,"msg":"running"},"jobID":"job-12345678","parentID":"d94bd8af-e8d7-42ed-90e3-b6cd59bc6dc9","requestID":"requestID4","tenantID":"12345678901234561234567890123456","version":"3"})";

    std::string from = "XXXXXXX";
    size_t start_pos = 0;
    while ((start_pos = value4.find(from, start_pos)) != std::string::npos) {
        value4.replace(start_pos, from.length(), NodeID);
        start_pos += NodeID.length(); // Move past the replaced substring
    }

    std::vector<WatchEvent> events;
    std::list<std::pair<std::string, std::string>> kvMap = {
        {key1, value1status1},
        {key1, value1status3},
        {key2, value2},
        {key3, value3},
        {key4, value4},
    };
    auto mod = 0;
    for (auto elem : kvMap) {
        KeyValue inst1;
        inst1.set_key(elem.first) ;
        inst1.set_value(elem.second);
        inst1.set_mod_revision(mod++);
        WatchEvent event{ .eventType = EVENT_TYPE_PUT, .kv = inst1, .prevKv = {} };
        events.emplace_back(event);
    }
    return events;

}

TEST_F(ObserverTest, InstanceInfoSyncerTest)
{
    auto mockMetaStoreClient  = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
    metaStorageAccessor_->metaClient_ = mockMetaStoreClient;

    observerActor_->instanceInfoMap_.clear();
    auto events = GenerateResponseRouteEvent(observerActor_->nodeID_);
    std::vector<WatchEvent> putEvents({events[0], events[3], events[4]}); // put key1(status 1), key3 in cache
    observerActor_->UpdateInstanceRouteEvent(putEvents, true);
    EXPECT_TRUE(observerActor_->instanceInfoMap_.count("InstanceID1") != 0);
    EXPECT_TRUE(observerActor_->instanceInfoMap_["InstanceID1"].instancestatus().code() == static_cast<int32_t>(InstanceState::SCHEDULING));
    EXPECT_TRUE(observerActor_->instanceInfoMap_.count("InstanceID3") != 0);
    EXPECT_TRUE(observerActor_->instanceInfoMap_.count("InstanceID4") != 0);

    // get key1(status 3), key2, key4 in etcd
    litebus::Future<std::shared_ptr<GetResponse>> getResponseFuture;
    std::shared_ptr<GetResponse> rep = std::make_shared<GetResponse>();
    rep->header.revision = 4;
    rep->status = Status::OK();
    rep->kvs.emplace_back(events[1].kv);
    rep->kvs.emplace_back(events[2].kv);
    getResponseFuture.SetValue(rep);

    EXPECT_CALL(*mockMetaStoreClient, Get).WillOnce(testing::Return(getResponseFuture));
    std::string cbFuncInstanceID;
    controlPlaneObserver_->SetInstanceInfoSyncerCbFunc([&cbFuncInstanceID](const resource_view::RouteInfo &routeInfo) {
        YRLOG_DEBUG("{}|{}execute instance info sync callback function, create client for instance({}), job({})",
                    routeInfo.requestid(), routeInfo.instanceid());
        cbFuncInstanceID = routeInfo.instanceid();
        return Status::OK();
    });
    auto future = observerActor_->InstanceInfoSyncer();
    ASSERT_AWAIT_READY(future);
    ASSERT_TRUE(future.Get().status.IsOk());

    // test exist in etcd and in cache, need update by etcd
    ASSERT_TRUE(observerActor_->instanceInfoMap_.count("InstanceID1") != 0);
    EXPECT_TRUE(observerActor_->instanceInfoMap_["InstanceID1"].instancestatus().code() == static_cast<int32_t>(InstanceState::RUNNING));

    // test exist in etcd but not in cache, need update
    EXPECT_TRUE(observerActor_->instanceInfoMap_.count("InstanceID2") != 0);

    // test not in etcd but in cache, need delete
    EXPECT_TRUE(observerActor_->instanceInfoMap_.count("InstanceID3") == 0);

    // test belong to self, not found in remote, don't delete
    EXPECT_TRUE(observerActor_->instanceInfoMap_.count("InstanceID4") == 1);

    // test belong to self, need to update by callback function
    rep = std::make_shared<GetResponse>();
    rep->status = Status::OK();
    rep->header.revision = 2;
    rep->kvs.emplace_back(events[2].kv);
    rep->kvs.emplace_back(events[4].kv);
    EXPECT_CALL(*mockMetaStoreClient, Get).WillOnce(testing::Return(rep));

    cbFuncInstanceID = "";
    EXPECT_EQ(cbFuncInstanceID, "");
    future = observerActor_->InstanceInfoSyncer();
    ASSERT_AWAIT_READY(future);
    ASSERT_TRUE(future.Get().status.IsOk());
    EXPECT_TRUE(observerActor_->instanceInfoMap_.count("InstanceID1") == 0); // rep don't have key1, so need to delete
    EXPECT_TRUE(observerActor_->instanceInfoMap_.count("InstanceID4") == 1);
    EXPECT_EQ(cbFuncInstanceID, "InstanceID4");
}

TEST_F(ObserverTest, WatchInstanceTest)
{
    controlPlaneObserver_->WatchInstance("InstanceID1");
    EXPECT_TRUE(observerActor_->instanceWatchers_.find("InstanceID1") == observerActor_->instanceWatchers_.end());

    auto events = GenerateResponseRouteEvent(observerActor_->nodeID_);
    metaStorageAccessor_->Put(GenInstanceRouteKey("InstanceID1"), events[0].kv.value());
    observerActor_->isPartialWatchInstances_ = true;

    controlPlaneObserver_->WatchInstance("InstanceID1");
    ASSERT_AWAIT_TRUE([&]() {
        return observerActor_->instanceWatchers_.find("InstanceID1") != observerActor_->instanceWatchers_.end();
    });

    controlPlaneObserver_->WatchInstance("InstanceID1");
    metaStorageAccessor_->Delete(GenInstanceRouteKey("InstanceID1"));
    ASSERT_AWAIT_TRUE([&]() {
        return observerActor_->instanceWatchers_.find("InstanceID1") == observerActor_->instanceWatchers_.end();
    });

    controlPlaneObserver_->WatchInstance("InstanceID1");
    ASSERT_AWAIT_TRUE([&]() {
        return observerActor_->instanceWatchers_.find("InstanceID1") != observerActor_->instanceWatchers_.end();
    });
    controlPlaneObserver_->CancelWatchInstance("InstanceID1");
    ASSERT_AWAIT_TRUE([&]() {
        return observerActor_->instanceWatchers_.find("InstanceID1") == observerActor_->instanceWatchers_.end();
    });

    observerActor_->isPartialWatchInstances_ = false;
}

TEST_F(ObserverTest, GetAndWatchInstanceTest)
{
    auto future = controlPlaneObserver_->GetAndWatchInstance("InstanceID1");
    ASSERT_AWAIT_TRUE([&]() { return future.IsError(); });

    auto events = GenerateResponseRouteEvent(observerActor_->nodeID_);
    metaStorageAccessor_->Put(GenInstanceRouteKey("InstanceID1"), events[0].kv.value());
    ASSERT_AWAIT_TRUE([&](){return observerActor_->instanceInfoMap_.count("InstanceID1") == 1;});

    future = controlPlaneObserver_->GetAndWatchInstance("InstanceID1");
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().instanceid(), "InstanceID1");

    metaStorageAccessor_->Delete(GenInstanceRouteKey("InstanceID1"));
    ASSERT_AWAIT_TRUE([&](){return observerActor_->instanceInfoMap_.count("InstanceID1") == 0;});

    observerActor_->isPartialWatchInstances_ = true;
    future = controlPlaneObserver_->GetAndWatchInstance("InstanceID1");
    ASSERT_AWAIT_TRUE([&]() { return future.IsError(); });

    metaStorageAccessor_->Put(GenInstanceRouteKey("InstanceID1"), events[0].kv.value());
    ASSERT_AWAIT_TRUE([&](){return observerActor_->instanceInfoMap_.count("InstanceID1") == 1;});

    future = controlPlaneObserver_->GetAndWatchInstance("InstanceID1");
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().instanceid(), "InstanceID1");

    metaStorageAccessor_->Delete(GenInstanceRouteKey("InstanceID1"));
    ASSERT_AWAIT_TRUE([&](){return observerActor_->instanceInfoMap_.count("InstanceID1") == 0;});

    observerActor_->isPartialWatchInstances_ = false;
}


TEST_F(ObserverTest, SubscribeInstanceEventTest)
{
    auto future = controlPlaneObserver_->GetAndWatchInstance("InstanceID1");
    ASSERT_AWAIT_TRUE([&]() { return future.IsError(); });

    auto events = GenerateResponseRouteEvent(observerActor_->nodeID_);
    metaStorageAccessor_->Put(GenInstanceRouteKey("InstanceID1"), events[0].kv.value());
    ASSERT_AWAIT_TRUE([&](){return observerActor_->instanceInfoMap_.count("InstanceID1") == 1;});
    controlPlaneObserver_->DelInstanceEvent("InstanceID1");
    ASSERT_AWAIT_TRUE([&](){return observerActor_->instanceInfoMap_.count("InstanceID1") == 0;});
    observerActor_->isPartialWatchInstances_ = false;
    auto future1 = observerActor_->TrySubscribeInstanceEvent("InstanceID-NotExist", "InstanceID1", false);
    ASSERT_AWAIT_READY(future1);
    EXPECT_EQ(future1.Get().StatusCode(), StatusCode::ERR_INSTANCE_EXITED);
    metaStorageAccessor_->Delete(GenInstanceRouteKey("InstanceID1")).Get();
    ASSERT_AWAIT_TRUE([&](){return observerActor_->instanceInfoMap_.count("InstanceID1") == 0;});
    auto future2 = observerActor_->TrySubscribeInstanceEvent("InstanceID-NotExist", "InstanceID1", false);
    ASSERT_AWAIT_READY(future2);
    EXPECT_TRUE(future2.Get().IsOk());
    EXPECT_TRUE(observerActor_->instanceView_->subscribedInstances_.find("InstanceID1") ==
                observerActor_->instanceView_->subscribedInstances_.end());
}

TEST_F(ObserverTest, PartialInstanceInfoSyncerTest)
{
    auto mockMetaStoreClient  = std::make_shared<MockMetaStoreClient>(metaStoreServerHost_);
    metaStorageAccessor_->metaClient_ = mockMetaStoreClient;

    observerActor_->instanceInfoMap_.clear();
    observerActor_->instanceModRevisionMap_.clear();
    auto events = GenerateResponseRouteEvent(observerActor_->nodeID_);
    std::vector<WatchEvent> putEvents({events[0], events[3], events[4]}); // put key1(status 1), key3 in cache
    observerActor_->UpdateInstanceRouteEvent(putEvents, true);
    EXPECT_TRUE(observerActor_->instanceInfoMap_.count("InstanceID1") != 0);
    EXPECT_TRUE(observerActor_->instanceInfoMap_["InstanceID1"].instancestatus().code()
                == static_cast<int32_t>(InstanceState::SCHEDULING));
    EXPECT_TRUE(observerActor_->instanceInfoMap_.count("InstanceID3") != 0);
    EXPECT_TRUE(observerActor_->instanceInfoMap_.count("InstanceID4") != 0);

    // get key1(status 3) in etcd
    litebus::Future<std::shared_ptr<GetResponse>> getResponseFuture;
    std::shared_ptr<GetResponse> rep = std::make_shared<GetResponse>();
    rep->header.revision = 5;
    rep->status = Status::OK();
    rep->kvs.emplace_back(events[1].kv);
    getResponseFuture.SetValue(rep);

    EXPECT_CALL(*mockMetaStoreClient, Get).WillOnce(testing::Return(getResponseFuture));
    controlPlaneObserver_->SetInstanceInfoSyncerCbFunc(
        [](const resource_view::RouteInfo &routeInfo) { return Status::OK(); });

    auto future = observerActor_->PartialInstanceInfoSyncer("InstanceID1");
    ASSERT_AWAIT_READY(future);
    ASSERT_TRUE(future.Get().status.IsOk());

    // test exist in etcd and in cache, need update by etcd
    ASSERT_TRUE(observerActor_->instanceInfoMap_.count("InstanceID1") != 0);
    EXPECT_TRUE(observerActor_->instanceInfoMap_["InstanceID1"].instancestatus().code()
                == static_cast<int32_t>(InstanceState::RUNNING));

    // get key2 in etcd
    getResponseFuture = litebus::Future<std::shared_ptr<GetResponse>>();
    rep->kvs.clear();
    rep->kvs.emplace_back(events[2].kv);
    getResponseFuture.SetValue(rep);
    EXPECT_CALL(*mockMetaStoreClient, Get).WillOnce(testing::Return(getResponseFuture));
    future = observerActor_->PartialInstanceInfoSyncer("InstanceID2");
    ASSERT_AWAIT_READY(future);
    ASSERT_TRUE(future.Get().status.IsOk());

    // test exist in etcd but not in cache, need update
    EXPECT_TRUE(observerActor_->instanceInfoMap_.count("InstanceID2") != 0);

    // get key3 not in etcd
    getResponseFuture = litebus::Future<std::shared_ptr<GetResponse>>();
    rep->kvs.clear();
    getResponseFuture.SetValue(rep);
    EXPECT_CALL(*mockMetaStoreClient, Get).WillOnce(testing::Return(getResponseFuture));
    future = observerActor_->PartialInstanceInfoSyncer("InstanceID3");
    ASSERT_AWAIT_READY(future);
    ASSERT_TRUE(future.Get().status.IsOk());
    // test not in etcd but in cache, need delete
    EXPECT_TRUE(observerActor_->instanceInfoMap_.count("InstanceID3") == 0);

    // get key4 not in etcd
    EXPECT_CALL(*mockMetaStoreClient, Get).WillOnce(testing::Return(getResponseFuture));
    future = observerActor_->PartialInstanceInfoSyncer("InstanceID4");
    ASSERT_AWAIT_READY(future);
    ASSERT_TRUE(future.Get().status.IsOk());
    // test belong to self, not found in remote, don't delete
    EXPECT_TRUE(observerActor_->instanceInfoMap_.count("InstanceID4") == 1);
}
}  // namespace functionsystem::test
