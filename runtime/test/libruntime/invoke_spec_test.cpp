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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#define private public
#include "src/libruntime/invoke_spec.h"
#include "src/proto/libruntime.pb.h"
#include "src/utility/logger/logger.h"

using namespace testing;
using namespace YR::Libruntime;
using namespace YR::utility;
namespace YR {
namespace test {

class InvokeSpecTest : public testing::Test {
public:
    InvokeSpecTest(){};
    ~InvokeSpecTest(){};
    void SetUp() override
    {
        Mkdir("/tmp/log");
        LogParam g_logParam = {
            .logLevel = "DEBUG",
            .logDir = "/tmp/log",
            .nodeName = "test-runtime",
            .modelName = "test",
            .maxSize = 100,
            .maxFiles = 1,
            .logFileWithTime = false,
            .logBufSecs = 30,
            .maxAsyncQueueSize = 1048510,
            .asyncThreadCount = 1,
            .alsoLog2Stderr = true,
        };
        InitLog(g_logParam);
        spec = std::make_shared<InvokeSpec>();
        spec->instanceId = "instanceId";
        spec->jobId = "jobId";
        spec->functionMeta = {
            "",   "", "funcname", "classname", libruntime::LanguageType::Cpp, "", "", "", libruntime::ApiType::Function,
            "cid"};
        InvokeOptions opts;
        spec->opts = opts;
    }
    void TearDown() override
    {
        this->spec.reset();
    }
    std::shared_ptr<InvokeSpec> spec;
};

TEST_F(InvokeSpecTest, BuildRequestPbOptions)
{
    CreateRequest req;
    std::list<std::shared_ptr<YR::Libruntime::Affinity>> libAffinities;
    auto libAffinity = std::make_shared<YR::Libruntime::ResourcePreferredAffinity>();
    libAffinity->SetPreferredAntiOtherLabels(true);
    libAffinities.push_back(libAffinity);
    spec->opts.scheduleAffinities = libAffinities;
    LibruntimeConfig conf;
    conf.workingDir = "file:///usr1/deploy/file.zip";
    std::string envKey = "LD_LIBRARY_PATH";
    std::string envValue = "${LD_LIBRARY_PATH}:${YR_FUNCTION_LIB_PATH}/depend";
    conf.customEnvs.insert({envKey, envValue});
    conf.isLowReliabilityTask = true;
    spec->opts.customExtensions["DELEGATE_DIRECTORY_QUOTA"] = "/tmp1";
    spec->opts.customExtensions["DELEGATE_DIRECTORY_INFO"] = "1024";
    spec->opts.recoverRetryTimes = 3;
    spec->invokeType = libruntime::InvokeType::CreateInstanceStateless;
    spec->BuildRequestPbOptions(spec->opts, conf, req);
    ASSERT_EQ(req.createoptions().at("DELEGATE_DIRECTORY_QUOTA"), "/tmp1");
    ASSERT_EQ(req.createoptions().at("DELEGATE_DIRECTORY_INFO"), "1024");
    ASSERT_EQ(req.createoptions().at(RECOVER_RETRY_TIMES), "3");
    const auto &jsonString = req.createoptions().at(DELEGATE_ENV_VAR);
    nlohmann::json jsonObj = nlohmann::json::parse(jsonString);
    auto envsMap = jsonObj.get<std::unordered_map<std::string, std::string>>();
    ASSERT_EQ(envsMap[envKey], envValue);
    ASSERT_EQ(req.createoptions().at("ReliabilityType"), "low");
    auto delegateDownloadValue = req.createoptions().at("DELEGATE_DOWNLOAD");
    auto delegateDownloadJson = nlohmann::json::parse(delegateDownloadValue);
    ASSERT_EQ(delegateDownloadJson["storage_type"], "working_dir");
    ASSERT_EQ(delegateDownloadJson["code_path"], conf.workingDir);
}

TEST_F(InvokeSpecTest, BuildRequestPbArgs)
{
    LibruntimeConfig config;
    CreateRequest req;
    spec->BuildRequestPbArgs(config, req, true);

    libruntime::MetaData meta;
    if (!meta.ParseFromArray(req.args(0).value().data(), req.args(0).value().size())) {
        ASSERT_EQ(0, 1);
    }
    ASSERT_EQ(meta.config().functionids(0).functionid(), "cid");
}

uint64_t g_strSize1M = 1000 * 1000;
std::string g_str1M(g_strSize1M, 'a');

TEST_F(InvokeSpecTest, BuildRequestPbArgsStringBuffer)
{
    LibruntimeConfig config;
    CreateRequest req;
    auto arg = InvokeArg();
    arg.dataObj = std::make_shared<DataObject>();
    auto buf = std::make_shared<StringNativeBuffer>(g_strSize1M);
    buf->MemoryCopy(g_str1M.data(), g_strSize1M);
    arg.dataObj->SetBuffer(buf);
    spec->invokeArgs.push_back(arg);
    spec->BuildRequestPbArgs(config, req, true);

    EXPECT_TRUE(buf->GetSize() == 0);  // use move semantic
}

TEST_F(InvokeSpecTest, BuildInvokeRequestPbOptionsTest)
{
    LibruntimeConfig config;
    InvokeSpec spec;
    spec.functionMeta.functionId = "testFunctionId";
    spec.opts.customExtensions["testKey"] = "testValue";
    spec.BuildInstanceInvokeRequest(config);
    EXPECT_EQ(spec.requestInvoke->Immutable().function(), "testFunctionId");
    auto invokeOptions = spec.requestInvoke->Mutable().invokeoptions();
    EXPECT_EQ(invokeOptions.customtag().at("testKey"), "testValue");

    spec.functionMeta.functionId = "";
    config.functionIds[libruntime::LanguageType::Cpp] = "testFunctionId1";
    spec.functionMeta.languageType = libruntime::LanguageType::Cpp;
    spec.BuildInstanceInvokeRequest(config);
    EXPECT_EQ(spec.requestInvoke->Immutable().function(), "testFunctionId1");
}

TEST_F(InvokeSpecTest, RequestResourceEqualTest)
{
    RequestResource r1;
    r1.opts.instanceSession = std::make_shared<YR::Libruntime::InstanceSession>();
    RequestResource r2;
    r2.opts.instanceSession = std::make_shared<YR::Libruntime::InstanceSession>();
    RequestResource r3;
    r3.opts.instanceSession = std::make_shared<YR::Libruntime::InstanceSession>();
    r1.opts.instanceSession->sessionID = "";
    r2.opts.instanceSession->sessionID = "123";
    r3.opts.instanceSession->sessionID = "123";
    ASSERT_EQ(r1 == r2, false);

    RequestResource r4;
    RequestResource r5;
    RequestResource r6;
    std::unordered_map<std::string, std::string> invokelabels1;
    invokelabels1["xxx"] = "xxx";
    std::unordered_map<std::string, std::string> invokelabels2;
    invokelabels2["zzz"] = "zzz";
    r4.opts.invokeLabels = invokelabels1;
    r5.opts.invokeLabels = invokelabels2;
    ASSERT_EQ(r4 == r5, false);
    ASSERT_EQ(r4 == r6, false);
}

TEST_F(InvokeSpecTest, GetInstanceIdTest)
{
    auto config = std::make_shared<LibruntimeConfig>();
    ASSERT_EQ(spec->GetInstanceId(config).empty(), true);
    spec->returnIds = {DataObject{"objId"}};
    ASSERT_EQ(spec->GetInstanceId(config), "objId");
    spec->functionMeta.name = "name";
    ASSERT_EQ(spec->GetInstanceId(config), "name");
    spec->functionMeta.ns = "ns";
    ASSERT_EQ(spec->GetInstanceId(config), "ns-name");
}

TEST_F(InvokeSpecTest, ConsumeRetryTimeAndInceaseSeqTest)
{
    spec->ConsumeRetryTime();
    ASSERT_EQ(spec->opts.retryTimes, 0);
    spec->opts.retryTimes = 1;
    spec->ConsumeRetryTime();
    ASSERT_EQ(spec->opts.retryTimes, 0);
    spec->IncrementSeq();
    ASSERT_EQ(spec->seq, 1);
    CreateRequest req;
    spec->IncrementRequestID(req);
    ASSERT_EQ(spec->seq, 2);
}

TEST_F(InvokeSpecTest, IsStaleDuplicateNotifyTest)
{
    spec->seq = 1;
    ASSERT_EQ(spec->IsStaleDuplicateNotify(0), true);
    ASSERT_EQ(spec->IsStaleDuplicateNotify(1), false);
}

TEST_F(InvokeSpecTest, RequestResourceTest)
{
    RequestResource resourceOne;
    RequestResource resourceTwo;
    ASSERT_NO_THROW(resourceOne.Print());

    resourceOne.opts.invokeLabels["label1"] = "1";
    resourceTwo.opts.invokeLabels["label2"] = "2";
    ASSERT_EQ(resourceOne == resourceTwo, false);

    resourceOne.opts.scheduleAffinities.push_back(std::make_shared<ResourcePreferredAffinity>());
    resourceTwo.opts.scheduleAffinities.push_back(std::make_shared<InstancePreferredAffinity>());
    ASSERT_EQ(resourceOne == resourceTwo, false);

    resourceOne.opts.customResources["cpu"] = 100;
    resourceTwo.opts.customResources["cpu"] = 200;
    ASSERT_EQ(resourceOne == resourceTwo, false);
}

TEST_F(InvokeSpecTest, BuildInstanceCreateRequestTest)
{
    spec->opts.customResources["cpu"] = 1000;
    spec->opts.scheduleAffinities.push_back(std::make_shared<ResourcePreferredAffinity>());
    spec->opts.affinity["affinity"] = "affinity";
    spec->opts.instanceRange.min = 1;
    spec->opts.needOrder = true;
    spec->opts.envVars["env"] = "env";
    spec->opts.createOptions["crateOption"] = "createOption";
    spec->functionMeta.name = "name";
    spec->functionMeta.functionId = "functionId";
    LibruntimeConfig config;
    spec->BuildInstanceCreateRequest(config);
    ASSERT_EQ(spec->requestCreate.mutable_schedulingops()->mutable_resources()->size() != 0, true);
}
}  // namespace test
}  // namespace YR