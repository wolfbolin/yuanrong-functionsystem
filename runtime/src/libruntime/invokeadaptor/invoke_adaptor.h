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

#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <unordered_set>

#include "execution_manager.h"
#include "request_manager.h"
#include "src/dto/config.h"
#include "src/dto/status.h"
#include "src/libruntime/dependency_resolver.h"
#include "src/libruntime/err_type.h"
#include "src/libruntime/fiber.h"
#include "src/libruntime/fsclient/fs_client.h"
#include "src/libruntime/fmclient/fm_client.h"
#include "src/libruntime/groupmanager/function_group.h"
#include "src/libruntime/groupmanager/group_manager.h"
#include "src/libruntime/invoke_order_manager.h"
#include "src/libruntime/invoke_spec.h"
#include "src/libruntime/invokeadaptor/task_submitter.h"
#include "src/libruntime/libruntime_config.h"
#include "src/libruntime/metricsadaptor/metrics_adaptor.h"
#include "src/libruntime/objectstore/memory_store.h"
#include "src/libruntime/objectstore/object_store.h"
#include "src/libruntime/runtime_context.h"
#include "src/libruntime/utils/constants.h"
#include "src/libruntime/utils/exception.h"
#include "src/libruntime/utils/utils.h"
#include "src/libruntime/rgroupmanager/resource_group_create_spec.h"
#include "src/libruntime/rgroupmanager/resource_group_manager.h"
#include "src/utility/notification_utility.h"

namespace YR {
namespace Libruntime {
using FinalizeCallback = std::function<void()>;
using SetTenantIdCallback = std::function<void()>;
using RawCallback = std::function<void(const ErrorInfo &err, std::shared_ptr<Buffer> resultRaw)>;

class InvokeAdaptor : public std::enable_shared_from_this<InvokeAdaptor> {
public:
    InvokeAdaptor() = default;
    InvokeAdaptor(std::shared_ptr<LibruntimeConfig> config, std::shared_ptr<DependencyResolver> dependencyResolver,
                  std::shared_ptr<FSClient> &fsClient, std::shared_ptr<MemoryStore> memStore,
                  std::shared_ptr<RuntimeContext> rtCtx, FinalizeCallback cb,
                  std::shared_ptr<WaitingObjectManager> waitManager, std::shared_ptr<InvokeOrderManager> invokeOrderMgr,
                  std::shared_ptr<ClientsManager> clientsMgr, std::shared_ptr<MetricsAdaptor> metricsAdaptor);

    std::pair<std::string, ErrorInfo> Init(RuntimeContext &runtimeContext, std::shared_ptr<Security> security);

    void SetRGroupManager(std::shared_ptr<ResourceGroupManager> rGroupManager);

    void SetCallbackOfSetTenantId(SetTenantIdCallback cb);

    virtual void ReceiveRequestLoop(void);

    void CreateInstance(std::shared_ptr<InvokeSpec> spec);

    void RetryCreateInstance(std::shared_ptr<InvokeSpec> spec, bool isConsumeRetryTime);

    void InvokeInstanceFunction(std::shared_ptr<InvokeSpec> spec);

    void SubmitFunction(std::shared_ptr<InvokeSpec> spec);

    void InvokeStatelessFunction(const std::shared_ptr<InvokeSpec> spec, InvokeCallBack callback);

    bool IsIdValid(const std::string &id);

    void CreateInstanceRaw(std::shared_ptr<Buffer> reqRaw, RawCallback cb);

    void InvokeByInstanceIdRaw(std::shared_ptr<Buffer> reqRaw, RawCallback cb);

    void KillRaw(std::shared_ptr<Buffer> reqRaw, RawCallback cb);

    void RetryInvokeInstanceFunction(std::shared_ptr<InvokeSpec> spec, bool isConsumeRetryTime);

    NotifyResponse NotifyHandler(const NotifyRequest &resp);

    virtual ErrorInfo Cancel(const std::vector<std::string> &objids, bool isForce, bool isRecursive);

    virtual void Exit(void);

    virtual void Finalize(bool isDriver = true);

    virtual ErrorInfo Kill(const std::string &instanceId, const std::string &payload, int signal);

    virtual void KillAsync(const std::string &instanceId, const std::string &payload, int signal);

    CallResponse CallReqProcess(const CallRequest &req);

    void SetCreatePromise(const CallRequest &req, const CallResult &callResult);

    bool NeedRetry(ErrorCode code, const std::shared_ptr<const InvokeSpec> spec, bool &isConsumeRetryTime);

    virtual ErrorInfo GroupCreate(const std::string &groupName, GroupOpts &opts);

    virtual ErrorInfo RangeCreate(const std::string &groupName, InstanceRange &range);

    ErrorInfo CreateFunctionGroup(const std::shared_ptr<InvokeSpec> &createSpec,
                                  const std::shared_ptr<InvokeSpec> &invokeSpec);

    virtual ErrorInfo GroupWait(const std::string &groupName);

    virtual void GroupTerminate(const std::string &groupName);

    virtual std::pair<std::vector<std::string>, ErrorInfo> GetInstanceIds(const std::string &objId,
                                                                  const std::string &groupName);

    virtual ErrorInfo SaveState(const std::shared_ptr<Buffer> data, const int &timeout);

    virtual ErrorInfo LoadState(std::shared_ptr<Buffer> &data, const int &timeout);

    virtual ErrorInfo ExecShutdownCallback(uint64_t gracePeriodSec);
    void InitHandler(const std::shared_ptr<CallMessageSpec> &req);
    void CallHandler(const std::shared_ptr<CallMessageSpec> &req);
    CheckpointResponse CheckpointHandler(const CheckpointRequest &req);
    RecoverResponse RecoverHandler(const RecoverRequest &req);

    void CreateResourceGroup(std::shared_ptr<ResourceGroupCreateSpec> spec);
    virtual std::pair<YR::Libruntime::FunctionMeta, ErrorInfo> GetInstance(const std::string &name,
                                                                   const std::string &nameSpace, int timeoutSec);
    void SubscribeAll();
    void Subscribe(const std::string &insId);
    ErrorInfo Accelerate(const std::string &groupName, const AccelerateMsgQueueHandle &handle,
                         HandleReturnObjectCallback callback);

    virtual std::pair<ErrorInfo, std::string> GetNodeIpAddress();

    virtual std::pair<ErrorInfo, std::string> GetNodeId();
    virtual std::pair<ErrorInfo, std::vector<ResourceUnit>> GetResources(void);
    virtual std::pair<ErrorInfo, ResourceGroupUnit> GetResourceGroupTable(const std::string &resourceGroupId);\
    virtual std::pair<ErrorInfo, QueryNamedInsResponse> QueryNamedInstances();
    void PushInvokeSpec(std::shared_ptr<InvokeSpec> spec);
    void SubscribeActiveMaster();
private:
    void CreateResponseHandler(std::shared_ptr<InvokeSpec> spec, const CreateResponse &resp);
    void CreateNotifyHandler(const NotifyRequest &req);
    ErrorInfo WriteDataToState(const std::string &instanceId, const std::shared_ptr<Buffer> data, std::string *state);
    ErrorInfo ReadDataFromState(const std::string &instanceId, const std::string &state, std::shared_ptr<Buffer> &data);
    SignalResponse SignalHandler(const SignalRequest &req);
    SignalResponse ExecSignalCallback(const SignalRequest &req);
    ShutdownResponse ShutdownHandler(const ShutdownRequest &req);
    HeartbeatResponse HeartbeatHandler(const HeartbeatRequest &req);
    void ExecUserShutdownCallback(uint64_t gracePeriodSec,
                                  const std::shared_ptr<utility::NotificationUtility> &notification);

    template <typename ResponseType>
    static ErrorInfo WaitAndCheckResp(std::shared_future<ResponseType> &future, const std::string &instanceId,
                                      const int &timeout);

    void InvokeNotifyHandler(const NotifyRequest &req, const ErrorInfo &err);

    void HandleReturnedObject(const NotifyRequest &req, const std::shared_ptr<InvokeSpec> &spec);

    template <typename RequestType>
    std::pair<common::ErrorCode, std::string> PrepareCallExecutor(const RequestType &req);

    CallResult CallProcess(const CallRequest &req);

    CallResult Call(const CallRequest &request, const libruntime::MetaData &metaData, const LibruntimeOptions &options,
                    std::vector<std::string> &objectsInDs);
    // load user function libraries
    CallResult InitCall(const CallRequest &req, const libruntime::MetaData &metaData);

    void InitMetricsAdaptor(bool userEnable);
    void ReportMetrics(const std::string &requestId, const std::string &traceId, int value);

    std::shared_ptr<InvokeSpec> BuildCreateSpec(std::shared_ptr<InvokeSpec> spec);

    void ProcessErr(const std::shared_ptr<InvokeSpec> &spec, const ErrorInfo &errInfo);

    void UpdateAndSubcribeInsStatus(const std::string &insId, libruntime::FunctionMeta &funcMeta);
    void RemoveInsMetaInfo(const std::string &insId);
    std::pair<libruntime::FunctionMeta, bool> GetCachedInsMeta(const std::string &insId);

    std::shared_ptr<FSClient> fsClient;
    std::shared_ptr<DependencyResolver> dependencyResolver;
    std::shared_ptr<RuntimeContext> runtimeContext;
    std::shared_ptr<MemoryStore> memStore;
    std::shared_ptr<LibruntimeConfig> librtConfig;
    std::shared_ptr<RequestManager> requestManager;
    std::shared_ptr<TaskSubmitter> taskSubmitter;
    std::atomic<bool> isRunning{true};
    FinalizeCallback finalizeCb_;
    std::shared_ptr<GroupManager> groupManager;
    std::shared_ptr<WaitingObjectManager> waitingObjectManager;
    std::shared_ptr<InvokeOrderManager> invokeOrderMgr;
    std::shared_ptr<ExecutionManager> execMgr;
    std::shared_ptr<ClientsManager> clientsMgr;
    std::shared_ptr<MetricsAdaptor> metricsAdaptor;
    std::shared_ptr<FiberPool> fiberPool_;
    std::shared_ptr<ResourceGroupManager> rGroupManager_;
    std::mutex finishTaskMtx;
    SetTenantIdCallback setTenantIdCb_;
    std::mutex metaMapMtx;
    std::unordered_map<std::string, libruntime::FunctionMeta> metaMap;
    std::atomic<bool> accelerateRunFlag_{false};
    std::shared_ptr<FMClient> functionMasterClient_;
};

const static std::unordered_map<common::ErrorCode, std::string> ErrMsgMap = {
    {common::ERR_NONE, "request success"},
    {common::ERR_PARAM_INVALID, "parram is invalid"},
    {common::ERR_RESOURCE_NOT_ENOUGH, "resource not enough"},
    {common::ERR_INSTANCE_NOT_FOUND, "instance not found"},
    {common::ERR_USER_CODE_LOAD, "failed to load user code"},
    {common::ERR_USER_FUNCTION_EXCEPTION, "user function exception"},
    {common::ERR_REQUEST_BETWEEN_RUNTIME_BUS, "failed to send request between bus and runtime"},
    {common::ERR_INNER_COMMUNICATION, "err inner communication"},
    {common::ERR_INNER_SYSTEM_ERROR, "inner system error"}};

}  // namespace Libruntime
}  // namespace YR
