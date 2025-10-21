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

#ifndef LOCAL_SCHEDULER_INSTANCE_CTRL_ACTOR_H
#define LOCAL_SCHEDULER_INSTANCE_CTRL_ACTOR_H

#include <actor/actor.hpp>
#include <async/future.hpp>
#include <unordered_set>
#include <chrono>

#include "proto/pb/message_pb.h"
#include "proto/pb/posix_pb.h"
#include "resource_type.h"
#include "common/resource_view/resource_view_mgr.h"
#include "rpc/stream/posix/control_client.h"
#include "common/schedule_decision/scheduler.h"
#include "common/schedule_decision/scheduler_common.h"
#include "common/state_machine/instance_control_view.h"
#include "common/state_machine/instance_state_machine.h"
#include "common/state_machine/instance_context.h"
#include "status/status.h"
#include "common/types/instance_state.h"
#include "function_agent_manager/function_agent_mgr.h"
#include "function_proxy/common/observer/control_plane_observer/control_plane_observer.h"
#include "function_proxy/common/posix_client/control_plane_client/control_interface_client_manager_proxy.h"
#include "function_proxy/common/rate_limiter/token_bucket_rate_limiter.h"
// for remove rgroup, easy for facilitates authentication, which should be extracted in the future
#include "local_scheduler/resource_group_controller/resource_group_ctrl.h"
#include "local_scheduler/subscription_manager/subscription_mgr.h"

namespace functionsystem::local_scheduler {
using CtrlClientPromise = litebus::Promise<std::shared_ptr<ControlInterfacePosixClient>>;
using InstanceReadyCallBack = std::function<litebus::Future<Status>(const Status &status)>;
using ClearGroupInstanceCallBack = std::function<void(const InstanceInfo &instanceInfo)>;
using CreateCallResultCallBack =
    std::function<litebus::Future<CallResultAck>(const std::shared_ptr<functionsystem::CallResult> &callResult)>;

class FunctionAgentMgr;
class LocalSchedSrv;

struct RuntimeConfig {
    std::string runtimeHeartbeatEnable;
    uint32_t runtimeMaxHeartbeatTimeoutTimes;
    uint32_t runtimeHeartbeatTimeoutMS;
    uint32_t runtimeInitCallTimeoutMS;
    uint32_t runtimeShutdownTimeoutSeconds;
    bool runtimeDsAuthEnable;
    bool runtimeDsEncryptEnable;
    std::string dataSystemAccessKey;
    SensitiveValue dataSystemSecurityKey;
    SensitiveValue runtimeDsClientPublicKey;
    SensitiveValue runtimeDsClientPrivateKey;
    SensitiveValue runtimeDsServerPublicKey;
};

struct AddressInfo {
    std::string instanceID;
    std::string runtimeID;
    std::string address;
    bool isDriver;
};

const uint8_t DEFAULT_MAX_INSTANCE_RECONNECT_TIMES = 3;
const uint8_t DEFAULT_MAX_INSTANCE_REDEPLOY_TIMES = 5;
const uint8_t DEFAULT_RECONNECT_TIMEOUT = 5;
const uint32_t DEFAULT_RECONNECT_INTERVAL = 100;
const uint32_t DEFAULT_CONNECT_TIMEOUT = 30;
const int32_t DEFAULT_REDEPLOY_TIMES = 1;
const uint32_t DEFAULT_WAIT_STATUS_CODE_UPDATED_MS = 10000;
const uint64_t MAX_DEPLOY_INTERVAL_MS = 5000;
const uint64_t MIN_DEPLOY_INTERVAL_MS = 1000;
const uint32_t MAX_GET_LOCAL_AID_TIMES = 30;

const uint32_t MAX_FORWARD_KILL_RETRY_TIMES = 30;
const uint32_t MAX_FORWARD_KILL_RETRY_CYCLE_MS = 2000;
const uint32_t MAX_FORWARD_KILL_RETRY_CYCLE_SYNC_MS = 3 * 60 * 1000;

const uint32_t MAX_FORWARD_SCHEDULE_RETRY_TIMES = 3;
const uint32_t MAX_NOTIFICATION_SIGNAL_RETRY_TIMES = 3;

struct InstanceCtrlConfig {
    // maxInstanceReconnectTimes is max number of times to reconnect an instance.
    uint8_t maxInstanceReconnectTimes{ DEFAULT_MAX_INSTANCE_RECONNECT_TIMES };
    // maxInstanceDeployTimes is max number of times to deploy an instance.
    uint8_t maxInstanceRedeployTimes{ DEFAULT_MAX_INSTANCE_REDEPLOY_TIMES };
    // reconnectTimeout is the default timeout for the reconnetion to an instance (S).
    uint32_t reconnectTimeout{ DEFAULT_RECONNECT_TIMEOUT };
    // reconnectInterval is the time interval to trigger reconnection(MS).
    uint32_t reconnectInterval{ DEFAULT_RECONNECT_INTERVAL };
    // connectTimeout is the timeout for the first connection to an instance (S).
    uint32_t connectTimeout{ DEFAULT_CONNECT_TIMEOUT };
    // maxGrpcSize is the Configuration of the size of the GPRS message.
    int32_t maxGrpcSize{ grpc::DEFAULT_MAX_GRPC_SIZE };
    // redeployTimes is the number of times of redeploying an instance when it status is FAILED.
    int32_t redeployTimes{ DEFAULT_REDEPLOY_TIMES };
    // waitStatusCodeUpdateMs is the waiting time for the async update of status code finished (in ms).
    uint32_t waitStatusCodeUpdateMs{ DEFAULT_WAIT_STATUS_CODE_UPDATED_MS };
    // minDeployIntervalMs is the min interval waiting time between deploy (in ms).
    uint64_t minDeployIntervalMs{ MIN_DEPLOY_INTERVAL_MS };
    // maxDeployIntervalMs is the max interval waiting time between deploy (in ms).
    uint64_t maxDeployIntervalMs{ MAX_DEPLOY_INTERVAL_MS };
    uint32_t maxGetLocalAidTimes{ MAX_GET_LOCAL_AID_TIMES };

    // host ip of cache storege
    std::string cacheStorageHost;
    // runtimeConfig is the heartbeat and recover setting for runtime.
    RuntimeConfig runtimeConfig;
    // isPseudoDataPlane is means the local does not have any resource to schedule.
    bool isPseudoDataPlane;
    // instance cpu and memory limit
    InstanceLimitResource limitResource;
    // ServerMode means grpc server is in proxy
    bool enableServerMode{ false };
    // Enable SSL means grpc client in runtime should verify server cert from proxy
    bool enableSSL{ false };
    std::string serverRootCert;
    // server name override is for runtime to verify server credential
    std::string serverNameOverride;
    // ServerMode means grpc server port
    std::string posixPort;
    // plugins need to be registered
    std::string schedulePlugins;
    // Enable SSL means grpc client in runtime should verify server cert from proxy
    bool enableTenantAffinity{ true };
    // Enable POSIX Create request rate limit
    bool createLimitationEnable{ false };
    // capacity of the token bucket rate limiting algorithm
    uint32_t tokenBucketCapacity{ DEFAULT_TENANT_TOKEN_BUCKET_CAPACITY };
    // is meta store enabled
    bool isMetaStoreEnabled {false};
    // is partial watch instances
    bool isPartialWatchInstances {false};
    // schedule max priority
    uint16_t maxPriority {0};
    bool enablePreemption {false};
};

class InstanceCtrlActor : public BasisActor {
public:
    InstanceCtrlActor(const std::string &name, const std::string &nodeID, const InstanceCtrlConfig &config);

    ~InstanceCtrlActor() override;

    void Init() override;

    /**
     * receive schedule instance request from client
     * @param scheduleReq
     * @return
     */
    litebus::Future<messages::ScheduleResponse> Schedule(
        const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
        const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> &runtimePromise);

    /**
     * receive kill instance from client
     * @param killReq: kill request
     * @return kill instance response
     */
    litebus::Future<KillResponse> Kill(const std::string &srcInstanceID, const std::shared_ptr<KillRequest> &killReq,
                                       bool isSkipAuth = false);

    /**
     * receive update instance status from client
     * @param info: instance status info need to update
     * @return update result
     */
    litebus::Future<Status> UpdateInstanceStatus(const std::shared_ptr<InstanceExitStatus> &info);

    litebus::Future<Status> UpdateInstanceStatusPromise(const std::string &instanceID, const std::string &errMsg);

    /**
     * receive request of forward custom signal from other local scheduler
     * @param from: AID of instance ctrl
     * @param name: function name
     * @param msg: request data, type is ForwardKillRequest
     */
    void ForwardCustomSignalRequest(const litebus::AID &from, std::string &&, std::string &&msg);

    /**
     * receive response of forward custom signal from other local scheduler
     * @param from: AID of instance ctrl
     * @param name: function name
     * @param msg: response data, type is ForwardKillResponse
     */
    void ForwardCustomSignalResponse(const litebus::AID &from, std::string &&, std::string &&msg);

    void ForwardCallResultRequest(const litebus::AID &from, std::string &&, std::string &&msg);

    void ForwardCallResultResponse(const litebus::AID &from, std::string &&, std::string &&msg);

    litebus::Future<CallResultAck> CallResult(const std::string &from,
                                              const std::shared_ptr<functionsystem::CallResult> &callResult);

    litebus::Future<bool> WaitClientConnected(const std::string &dstInstance);
    void CheckClientConnected(const std::string &dstInstance, const std::shared_ptr<litebus::Promise<bool>> &promise);

    litebus::Future<CallResultAck> ClearCreateCallResultPromises(const litebus::Future<CallResultAck> &future,
                                                                 const std::string &from);

    virtual litebus::Future<CallResultAck> SendCallResult(
        const std::string &srcInstance,
        const std::string &dstInstance, const std::string &dstProxyID,
        const std::shared_ptr<functionsystem::CallResult> &callResult);

    litebus::Future<CallResultAck> SendNotifyResult(const std::shared_ptr<ControlInterfacePosixClient> &instanceClient,
                                                    const std::string &instanceID, const std::string &requestID,
                                                    const std::shared_ptr<functionsystem::CallResult> &callResult);

    litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> CreateInstanceClient(
        const std::string &instanceID, const std::string &runtimeID, const std::string &address,
        const std::function<void()> &customCb = nullptr, bool isDriver = false);

    bool CheckIsCreateRequestExist(const std::string &instanceID)
    {
        return syncCreateCallResultPromises_.find(instanceID) != syncCreateCallResultPromises_.end();
    }

    void BindScheduler(const std::shared_ptr<schedule_decision::Scheduler> &scheduler)
    {
        scheduler_ = scheduler;
    }

    void BindObserver(const std::shared_ptr<function_proxy::ControlPlaneObserver> &observer);

    void BindFunctionAgentMgr(const std::shared_ptr<local_scheduler::FunctionAgentMgr> &functionAgentMgr)
    {
        functionAgentMgr_ = functionAgentMgr;
    }

    void BindResourceView(const std::shared_ptr<resource_view::ResourceViewMgr> &resourceViewMgr)
    {
        resourceViewMgr_ = resourceViewMgr;
    }

    void OnHealthyStatus(const Status &status);
    litebus::Future<Status> InstanceRouteInfoSyncer(const resource_view::RouteInfo &routeInfo);
    void UpdateFuncMetas(bool isAdd, const std::unordered_map<std::string, FunctionMeta> &funcMetas);

    // for test
    [[maybe_unused]] void BindInstanceControlView(const std::shared_ptr<InstanceControlView> &view)
    {
        ASSERT_IF_NULL(view);
        instanceControlView_ = view;
    }

    litebus::Future<Status> SyncInstance(const std::shared_ptr<resource_view::ResourceUnit> &resourceUnit);

    litebus::Future<Status> SyncAgent(const std::unordered_map<std::string, messages::FuncAgentRegisInfo> &agentMap);

    litebus::Future<KillResponse> KillInstancesOfJob(const std::shared_ptr<KillRequest> &killReq);

    void BindControlInterfaceClientManager(const std::shared_ptr<ControlInterfaceClientManagerProxy> &mgr)
    {
        ASSERT_IF_NULL(mgr);
        clientManager_ = mgr;
    }

    void BindMetaStoreClient(const std::shared_ptr<MetaStoreClient> &metaStoreClient)
    {
        ASSERT_IF_NULL(instanceControlView_);
        instanceControlView_->BindMetaStoreClient(metaStoreClient);
        instanceOpt_ = std::make_shared<InstanceOperator>(metaStoreClient);
    }

    /**
     * bind local scheduler service
     * @param localSchedSrv: local scheduler service
     */
    void BindLocalSchedSrv(const std::shared_ptr<LocalSchedSrv> &localSchedSrv)
    {
        localSchedSrv_ = localSchedSrv;
    }

    void BindResourceGroupCtrl(const std::shared_ptr<ResourceGroupCtrl> &rGroupCtrl)
    {
        rGroupCtrl_ = rGroupCtrl;
    }

    void BindSubscriptionMgr(const std::shared_ptr<SubscriptionMgr> &subscriptionMgr)
    {
        subscriptionMgr_ = subscriptionMgr;
        ASSERT_IF_NULL(instanceControlView_);
        subscriptionMgr_->BindInstanceControlView(instanceControlView_);
        ASSERT_IF_NULL(observer_);
        subscriptionMgr_->BindObserver(observer_);
    }

    litebus::Future<Status> Checkpoint(const std::string &instanceID);
    litebus::Future<Status> Recover(const resource_view::InstanceInfo &instance);

    litebus::Future<Status> RedeployDecision(const Status &status,
                                             const std::shared_ptr<::messages::ScheduleRequest> &request);

    litebus::Future<Status> Reschedule(const Status &status, const std::shared_ptr<messages::ScheduleRequest> &request);

    litebus::Future<Status> RescheduleWithID(const std::string &instanceID);

    litebus::Future<Status> RescheduleAfterJudgeRecoverable(const std::string &instanceID,
                                                            const std::string &funcAgentID);

    litebus::Future<Status> RecoverInstance(const std::string &instanceID);

    litebus::Future<messages::ScheduleResponse> DoAuthorizeCreate(
        const litebus::Option<FunctionMeta> &functionMeta,
        const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
        const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> &runtimePromise);

    litebus::Future<Status> AuthorizeKill(const std::string &callerInstanceID,
                                          const std::shared_ptr<KillRequest> &killReq, bool isSkipAuth = false);

    void PutFailedInstanceStatusByAgentId(const std::string &funcAgentID);

    virtual void HandleRuntimeHeartbeatLost(const std::string &instanceID, const std::string &runtimeID);

    virtual void HandleInstanceHealthChange(const std::string &instanceID, const StatusCode &code);

    void SendHeartbeat(const std::string &instanceID, uint32_t timeoutTimes, const std::string &runtimeID,
                       const StatusCode &prevStatus = StatusCode::SUCCESS);

    void SendHeartbeatCallback(const std::string &instanceID, uint32_t timeoutTimes, const StatusCode &prevStatus,
                               const std::string &runtimeID, const litebus::Future<Status> &status);

    void StartHeartbeat(const std::string &instanceID, uint32_t timeoutTimes, const std::string &runtimeID,
                        const StatusCode &prevStatus = StatusCode::SUCCESS);

    bool CheckHeartbeatExist(const std::string &instanceID);

    void StopHeartbeat(const std::string &instanceID);

    litebus::Future<Status> ShutDownInstance(const InstanceInfo &instanceInfo, uint32_t shutdownTimeoutSec);

    void SetAbnormal();

    void NotifyDsHealthy(bool healthy);

    litebus::Future<Status> EvictInstanceOnAgent(const std::shared_ptr<messages::EvictAgentRequest> &req);

    litebus::Future<Status> EvictInstances(const std::unordered_set<std::string> &instanceSet,
                                           const std::shared_ptr<messages::EvictAgentRequest> &req,
                                           bool isEvictForReuse = false);

    litebus::Future<litebus::Option<FunctionMeta>> GetFuncMeta(const std::string &funcKey);

    litebus::Future<Status> GetAffinity(const Status &authorizeStatus,
                                        const std::shared_ptr<messages::ScheduleRequest> &scheduleReq);

    litebus::Future<KillResponse> KillGroup(const std::string &srcInstanceID,
                                            const std::shared_ptr<KillRequest> &killReq);
    void PrepareParam(const FunctionMeta &funcMeta, const std::shared_ptr<messages::ScheduleRequest> &scheduleReq);
    Status CheckParam(const Status &authorizeStatus, const litebus::Option<FunctionMeta> &functionMeta,
                                       const std::shared_ptr<messages::ScheduleRequest> &scheduleReq);
    litebus::Future<Status> TransScheduling(const Status &authorizeStatus,
                                            const litebus::Option<FunctionMeta> &functionMeta,
                                            const std::shared_ptr<messages::ScheduleRequest> &scheduleReq);
    litebus::Future<Status> ToScheduling(const std::shared_ptr<messages::ScheduleRequest> &req);
    litebus::Future<Status> ToCreating(const std::shared_ptr<messages::ScheduleRequest> &req,
                                       const schedule_decision::ScheduleResult &result);
    void RegisterReadyCallback(const std::string &instanceID,
                               const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
                               InstanceReadyCallBack callback);
    litebus::Future<Status> ForceDeleteInstance(const std::string &instanceID);
    inline void RegisterClearGroupInstanceCallBack(ClearGroupInstanceCallBack callback)
    {
        groupInstanceClear_ = callback;
    }
    Status FetchedFunctionMeta(const litebus::Option<FunctionMeta> &functionMeta,
                               const std::shared_ptr<messages::ScheduleRequest> &scheduleReq);
    void TryRecoverExistedInstanceWithoutAgent(const InstanceInfo &info);

    litebus::Future<Status> OnQueryInstanceStatusInfo(const litebus::Future<::messages::InstanceStatusInfo> &future,
                                                      const std::shared_ptr<InstanceStateMachine> &stateMachine,
                                                      const std::string &errMsg, const std::string &runtimeID,
                                                      bool isRuntimeRecoverEnable);

    litebus::Future<KillResponse> KillResourceGroup(const std::string &srcInstanceID,
                                                     const std::shared_ptr<KillRequest> &killReq);
    /**
     * Used For GroupSchedule
     * when group schedule is delete, need to kill the scheduling instance on proxy
     *
     * @param instanceID
     * @return
     */
    litebus::Future<Status> DeleteSchedulingInstance(const std::string &instanceID, const std::string &requestID);

    // only for test
    std::unordered_map<std::string, litebus::Timer> GetHeartbeatTimers()
    {
        return runtimeHeartbeatTimers_;
    }

    void SetNodeLabelsToMetricsContext(const std::string &functionAgentID,
                                       std::map<std::string, resources::Value::Counter> nodeLabels);

    // only for test
    void AddHeartbeatTimer(const std::string &instanceID)
    {
        litebus::Timer timer;
        runtimeHeartbeatTimers_[instanceID] = timer;
    }

    // only for test
    void SetMaxForwardKillRetryTimes(uint32_t times)
    {
        maxForwardKillRetryTimes_ = times;
    }

    // only for test
    void SetMaxForwardKillRetryCycleMs(uint32_t cycleMs)
    {
        maxForwardKillRetryCycleMs_ = cycleMs;
    }

    [[maybe_unused]] static void SetGetLocalInterval(uint64_t interval);

    void SetTenantAffinityOpt(const std::shared_ptr<messages::ScheduleRequest> &scheduleReq);

    bool DoRateLimit(const std::shared_ptr<messages::ScheduleRequest> &scheduleReq);

    void ClearRateLimiterRegularly();

    // only for test
    [[maybe_unused]] bool HasForwardSignalRequested(const std::string &requestID)
    {
        if (auto iter(forwardCustomSignalNotifyPromise_.find(requestID));
            iter != forwardCustomSignalNotifyPromise_.end()) {
            return true;
        }
        return false;
    }

    litebus::Future<KillResponse> SendNotificationSignal(const std::shared_ptr<KillContext> &killCtx,
                                                         const std::string &srcInstanceID,
                                                         const std::shared_ptr<KillRequest> &killReq, uint32_t cnt);

    litebus::Future<KillResponse> RetrySendNotificationSignal(const KillResponse &killResponse,
                                                              const std::shared_ptr<KillContext> &killCtx,
                                                              const std::string &srcInstanceID,
                                                              const std::shared_ptr<KillRequest> &killReq,
                                                              uint32_t cnt);

    litebus::Future<KillResponse> ProcessSubscribeRequest(const std::string &srcInstanceID,
                                                          const std::shared_ptr<KillRequest> &killReq);

    litebus::Future<KillResponse> UnsubscribeInstanceTermination(const std::shared_ptr<KillContext> &killCtx);

    litebus::Future<KillResponse> ProcessUnsubscribeRequest(const std::string &srcInstanceID,
                                                            const std::shared_ptr<KillRequest> &killReq);

    litebus::Future<Status> GracefulShutdown();

    litebus::Future<KillResponse> ForwardSubscriptionEvent(const std::shared_ptr<KillContext> &ctx);

private:
    Status CheckSchedRequestValid(const std::shared_ptr<messages::ScheduleRequest> &scheduleReq);

    Status CheckHeteroResourceValid(const std::shared_ptr<messages::ScheduleRequest> &scheduleReq);

    litebus::Future<Status> DispatchSchedule(const std::shared_ptr<messages::ScheduleRequest> &request);

    litebus::Future<messages::ScheduleResponse> DoDispatchSchedule(
        const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
        const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> &runtimePromise,
        const TransitionResult &result);

    litebus::Future<messages::ScheduleResponse> DoCreateInstance(
        const Status &authorizeStatus, const litebus::Option<FunctionMeta> &functionMeta,
        const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
        const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> &runtimePromise);

    messages::ScheduleResponse PrepareCreateInstance(
        const Status &authorizeStatus, const litebus::Option<FunctionMeta> &functionMeta,
        const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
        const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> &runtimePromise);

    Status VerifyTenantID(const std::shared_ptr<messages::ScheduleRequest> &scheduleReq, const std::string &traceID,
                          const std::string &requestID);

    Status VerifyAffinityWithoutTenantKey(const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
                                          const std::string &key);

    void EraseTenantFromScheduleAffinity(const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
                                         const std::string &key);

    void AddTenantToScheduleAffinity(const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
                                     const std::string &tenantID);

    litebus::Future<Status> ScheduleConfirmed(const Status &status,
                                              const std::shared_ptr<messages::ScheduleRequest> &request);

    litebus::Future<Status> DeployInstance(const std::shared_ptr<messages::ScheduleRequest> &request,
                                           uint32_t retriedTimes, const litebus::Option<TransitionResult> &result,
                                           bool isRecovering = false);

    litebus::Future<Status> UpdateInstance(const messages::DeployInstanceResponse &response,
                                           const std::shared_ptr<messages::ScheduleRequest> &request,
                                           uint32_t retriedTimes, bool isRecovering = false);

    /**
     * after app driver instance is deployed, just update instance to running in meta store and send callResult
     * @param request
     * @return
     */
    litebus::Future<Status> OnAppDriverDeployed(const std::shared_ptr<messages::ScheduleRequest> &request);

    litebus::Future<Status> CheckReadiness(const std::shared_ptr<ControlInterfacePosixClient> &instanceClient,
                                           const std::shared_ptr<messages::ScheduleRequest> &request,
                                           uint32_t retriedTimes, bool isRecovering = false);

    litebus::Future<Status> SendInitRuntime(const std::shared_ptr<ControlInterfacePosixClient> &instanceClient,
                                            const std::shared_ptr<messages::ScheduleRequest> &request);

    void ScheduleEnd(const litebus::Future<Status> &status, const std::shared_ptr<messages::ScheduleRequest> &request);

    litebus::Future<std::shared_ptr<KillContext>> SignalRoute(const std::shared_ptr<KillContext> &killCtx);
    litebus::Future<KillResponse> Exit(const std::shared_ptr<KillContext> &killCtx, bool isSynchronized);
    litebus::Future<KillResponse> SendSignal(const std::shared_ptr<KillContext> &killCtx,
                                             const std::string &srcInstanceID,
                                             const std::shared_ptr<KillRequest> &killReq);
    /**
     * stop app driver: kill app driver instance and set it fatal
     * @param killCtx
     * @return
     */
    litebus::Future<KillResponse> StopAppDriver(const std::shared_ptr<KillContext> &killCtx);

    /*
     * @brief Send request to agent to kill instance.
     * @param instanceInfo: the instance which is going to be killed
     * @param isRecovering: specify the kill request was from recovering or not
     * @param forRedeploy: specify the kill request was for redeploy or not
     */
    litebus::Future<messages::KillInstanceResponse> SendKillRequestToAgent(const InstanceInfo &instanceInfo,
                                                                           bool isRecovering = false,
                                                                           bool forRedeploy = false);

    litebus::Future<Status> DoSync(const litebus::Option<function_proxy::InstanceInfoMap> &instanceInfo,
                                   const std::string &funcAgentID);

    litebus::Future<Status> CheckSyncKill(const messages::KillInstanceResponse &killStatus,
                                          const std::string &funcAgentID, const std::string &instanceID);

    litebus::Future<Status> CheckSyncInstance(const litebus::Future<Status> &status, const std::string &funcAgentID);

    litebus::Future<Status> CheckSyncKillInstance(const litebus::Future<Status> &future,
                                                  const std::shared_ptr<litebus::Promise<Status>> &killPromise,
                                                  const std::string &funcAgentID);

    litebus::Future<Status> AddCredToDeployInstanceReq(
        const std::string &tenantID, const std::shared_ptr<messages::DeployInstanceRequest> &deployInstanceReq);

    void AddDsAuthToDeployInstanceReq(const std::shared_ptr<messages::ScheduleRequest> &scheduleRequest,
                                      const std::shared_ptr<messages::DeployInstanceRequest> &deployInstanceReq);

    litebus::Future<Status> KillAgentInstance(const Status &status, const std::shared_ptr<ResourceUnit> &resourceUnit);

    litebus::Future<Status> RecoverAgentInstance(const Status &status,
                                                 const std::shared_ptr<ResourceUnit> &resourceUnit);

    litebus::Future<Status> CheckSyncRecoverInstance(const litebus::Future<Status> &future,
                                                     const std::string &funcAgentID, const std::string &instanceID,
                                                     const std::string &tenantID);

    litebus::Future<Status> SyncCreateResult(
        const litebus::Future<runtime::CallResponse> &callFuture,
        const litebus::Future<std::shared_ptr<functionsystem::CallResult>> &resultFuture,
        const std::shared_ptr<messages::ScheduleRequest> &request);

    litebus::Future<KillResponse> SendForwardCustomSignalRequest(const litebus::Option<litebus::AID> &option,
                                                                 const std::string &srcInstanceID,
                                                                 const std::shared_ptr<KillRequest> &killRequest,
                                                                 const std::string &dstInstanceRequestID,
                                                                 bool isSynchronized);

    litebus::Future<Status> SendForwardCustomSignalResponse(const KillResponse &killResponse, const litebus::AID &from,
                                                            const std::string &requestID);

    void RetrySendForwardCustomSignalRequest(const litebus::AID &aid,
                                             const std::shared_ptr<internal::ForwardKillRequest> forwardKillRequest,
                                             uint32_t cnt, bool isSynchronized);

    void CreateClientWithRetry(const AddressInfo &info, const std::shared_ptr<CtrlClientPromise> &promise,
                               uint32_t alreadyFailed, uint64_t timeoutSec, const std::function<void()> &customCb);

    litebus::Future<internal::ForwardCallResultResponse> SendForwardCallResultRequest(
        const litebus::AID &proxyAID,
        const std::shared_ptr<internal::ForwardCallResultRequest> &forwardCallResultRequest);

    litebus::Future<Status> SendForwardCallResultResponse(const CallResultAck &ack, const litebus::AID &from,
                                                          const std::string &requestID, const std::string &instanceID);

    litebus::Future<Status> KillRuntime(const InstanceInfo &instanceInfo, bool isRecovering = false);
    inline bool IsValidKillParam(
        const Status &status, std::shared_ptr<KillContext> &killCtx, const std::shared_ptr<KillRequest> &killReq,
        std::shared_ptr<InstanceStateMachine> &stateMachine);

    litebus::Future<std::shared_ptr<KillContext>> CheckKillParam(const Status &status, const std::string &srcInstanceID,
                                                                 const std::shared_ptr<KillRequest> &killReq);

    litebus::Future<std::shared_ptr<KillContext>> PrepareKillByInstanceState(
        const std::shared_ptr<KillContext> &killCtx);

    litebus::Future<std::shared_ptr<KillContext>> ProcessKillCtxByInstanceState(
        const std::shared_ptr<KillContext> &killCtx);

    litebus::Future<Status> CheckInstanceExist(const std::string &srcInstanceID,
                                               const std::shared_ptr<KillRequest> &killReq);
    litebus::Future<Status> DeleteInstanceInResourceView(const Status &status, const InstanceInfo &instanceInfo);
    litebus::Future<Status> DeleteInstanceInControlView(const Status &status, const InstanceInfo &instanceInfo);

    litebus::Future<Status> DoLocalRedeploy(const Status &status,
                                            const std::shared_ptr<::messages::ScheduleRequest> &request,
                                            const std::shared_ptr<InstanceStateMachine> &stateMachine);

    litebus::Future<Status> SyncFailedAgentInstance(
        const std::unordered_map<std::string, messages::FuncAgentRegisInfo> &agentMap,
        const litebus::Option<function_proxy::InstanceInfoMap> &instanceInfoMap);

    litebus::Future<Status> Redeploy(const Status &status, const std::shared_ptr<::messages::ScheduleRequest> &request);

    litebus::Option<std::shared_ptr<messages::ScheduleRequest>> GetScheReqByInstID(const std::string instanceID);

    litebus::Future<Status> DoReschedule(const std::shared_ptr<messages::ScheduleRequest> &request, uint32_t code,
                                         const std::string &msg);

    void AsyncDeployInstance(const std::shared_ptr<litebus::Promise<Status>> &promise,
                             const std::shared_ptr<messages::ScheduleRequest> &request, uint32_t retriedTimes,
                             bool isRecovering = false);

    litebus::Future<Status> RescheduleConfirm(const messages::ScheduleResponse &response,
                                              const std::shared_ptr<messages::ScheduleRequest> &request);

    litebus::Future<Status> CheckFuncMeta(const litebus::Option<FunctionMeta> &funcMeta,
                                          const std::shared_ptr<messages::ScheduleRequest> &request);

    void CollectInstanceResources(const InstanceInfo &instance);

    litebus::Future<messages::ScheduleResponse> ConfirmScheduleDecisionAndDispatch(
        const std::shared_ptr<messages::ScheduleRequest> &scheduleReq, const schedule_decision::ScheduleResult &result,
        const InstanceState &prevState);

    litebus::Future<messages::ScheduleResponse> RetryForwardSchedule(
        const std::shared_ptr<messages::ScheduleRequest> &scheduleReq, const messages::ScheduleResponse &resp,
        uint32_t retryTimes, const std::shared_ptr<InstanceStateMachine> &stateMachine);

    void TryClearStateMachineCache(const std::shared_ptr<messages::ScheduleRequest> &scheduleReq);

    litebus::Future<messages::ScheduleResponse> HandleForwardResponseAndNotifyCreator(
        const std::shared_ptr<messages::ScheduleRequest> &scheduleReq, const messages::ScheduleResponse &resp);

    litebus::Future<Status> HandleFailedInstance(const std::string &instanceID, const std::string &runtimeID,
                                                 const std::string &errMsg);

    litebus::Future<Status> SendRecoverReq(const std::shared_ptr<InstanceStateMachine> &stateMachine,
                                           const std::shared_ptr<messages::ScheduleRequest> &request);

    litebus::Future<Status> SendCheckpointReq(const std::shared_ptr<messages::ScheduleRequest> &request);

    litebus::Future<Status> HandleCheckReadinessFailure(const std::shared_ptr<messages::ScheduleRequest> &request,
                                                        uint32_t retriedTimes, const std::string &errMsg,
                                                        bool isRecovering = false);

    litebus::Future<messages::ScheduleResponse> CheckGeneratedInstanceID(
        const GeneratedInstanceStates &genStatus, const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
        const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> &runtimePromise);

    void SubscribeInstanceStatusChanged(const InstanceInfo &instanceInfo, const std::string &currentRequestID);
    void SubscribeStateChangedByInstMgr(const InstanceInfo &instanceInfo);

    litebus::Future<Status> HandleCallResultTimeout(const std::shared_ptr<messages::ScheduleRequest> &request);

    void RegisterStateChangeCallback(
        const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
        const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> &runtimePromise);

    litebus::Future<Status> DeleteInstanceStatusPromise(const litebus::Future<Status> &status,
                                                        const std::string &instanceID)
    {
        if (instanceStatusPromises_.find(instanceID) != instanceStatusPromises_.end()) {
            (void)instanceStatusPromises_.erase(instanceID);
        }
        return status;
    }

    litebus::Future<Status> RecoverRunningInstance(const std::shared_ptr<messages::ScheduleRequest> &request,
                                                   const std::shared_ptr<InstanceStateMachine> &stateMachine);
    litebus::Future<Status> RecoverCreatingInstance(const std::shared_ptr<messages::ScheduleRequest> &request,
                                                    const std::shared_ptr<InstanceStateMachine> &stateMachine);
    litebus::Future<Status> RecoverSchedulingInstance(const std::shared_ptr<messages::ScheduleRequest> &request);

    litebus::Future<Status> RecoverExitingInstance(const std::shared_ptr<messages::ScheduleRequest> &request);

    litebus::Future<messages::ScheduleResponse> DeleteRequestFuture(
        const litebus::Future<messages::ScheduleResponse> &scheduleResponse, const std::string &requestID,
        const std::shared_ptr<messages::ScheduleRequest> &scheduleReq);

    void DeleteDriverClient(const std::string &instanceID, const std::string &jobID);

    litebus::Future<TransitionResult> TransInstanceState(const std::shared_ptr<InstanceStateMachine> machine,
                                                         const TransContext &context);

    litebus::Future<Status> TryExitInstance(const std::shared_ptr<InstanceStateMachine> stateMachine,
                                            const std::shared_ptr<KillContext> &killCtx,
                                            bool isSynchronized);

    Status UpdateInstanceInfo(const resources::InstanceInfo &instanceInfo);

    litebus::Future<litebus::Option<litebus::AID>> GetLocalSchedulerAID(const std::string &proxyID);
    void RetryGetLocalSchedulerAID(const std::string &proxyID,
                                   const std::shared_ptr<litebus::Promise<litebus::Option<litebus::AID>>> &promise,
                                   const uint32_t retryTimes = 0);

    litebus::Future<Status> DoEvictInstances(const litebus::Option<function_proxy::InstanceInfoMap> &instanceInfoMapOpt,
                                             const std::shared_ptr<messages::EvictAgentRequest> &req);

    litebus::Future<Status> EvictInstance(const std::string &instanceID,
                                          const std::shared_ptr<messages::EvictAgentRequest> &req,
                                          bool isEvictForReuse);

    litebus::Future<Status> DoEvictInstance(const std::shared_ptr<InstanceStateMachine> &stateMachine,
                                            const std::string &instanceID,
                                            const std::shared_ptr<messages::EvictAgentRequest> &req,
                                            bool isEvictForReuse);

    litebus::Future<Status> SendInitCall(const std::shared_ptr<ControlInterfacePosixClient> &instanceClient,
                                         const std::shared_ptr<messages::ScheduleRequest> &request,
                                         const std::shared_ptr<InstanceStateMachine> &stateMachine,
                                         const std::shared_ptr<runtime_service::CallRequest> &callRequest);

    void SetGracefulShutdownTime(const std::shared_ptr<messages::ScheduleRequest> &scheduleReq);

    void SyncFailedInitResult(const std::string &instanceID, const common::ErrorCode &errCode, const std::string &msg);

    litebus::Future<KillResponse> SetInstanceFatal(const std::shared_ptr<KillContext> &killCtx);

    litebus::Future<Status> SetDataAffinity(const std::shared_ptr<messages::ScheduleRequest> &scheduleReq);

    void AddDataAffinity(const litebus::Future<std::vector<std::string>> &nodeListFut,
                         const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
                         const litebus::Promise<Status> &promise);

    litebus::Future<TransitionResult> ToTransCreating(const std::shared_ptr<InstanceStateMachine> &stateMachineRef,
                                                      const std::shared_ptr<messages::ScheduleRequest> &scheduleReq);

    void SetBillingMetrics(const std::shared_ptr<messages::ScheduleRequest> &request,
                           const messages::DeployInstanceResponse &response);
    void SetInstanceBillingContext(const resource_view::InstanceInfo &instance);

    litebus::Future<resources::InstanceInfo> TransFailedInstanceState(
        const resources::InstanceInfo &info, const std::shared_ptr<InstanceStateMachine> &stateMachine,
        const InstanceState &failedInstanceState);

    litebus::Future<messages::ScheduleResponse> TryDispatchOnLocal(
        const Status &status, const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
        const schedule_decision::ScheduleResult &result, const InstanceState &prevState,
        const std::shared_ptr<InstanceStateMachine> &stateMachineRef);

    litebus::Option<TransitionResult> OnTryDispatchOnLocal(
        const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> scheduleResp,
        const std::shared_ptr<messages::ScheduleRequest> &scheduleReq, const schedule_decision::ScheduleResult &result,
        const TransitionResult &transResult);

    CreateCallResultCallBack RegisterCreateCallResultCallback(
        const std::shared_ptr<messages::ScheduleRequest> &request);

    bool CheckExistInstanceState(const InstanceState &state,
                                 const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> &runtimePromise,
                                 const std::shared_ptr<messages::ScheduleRequest> &scheduleReq);

    void OnDriverEvent(const resource_view::InstanceInfo &instanceInfo);
    void OnDriverConnected(const litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> &instanceClient,
                           const resource_view::InstanceInfo &instanceInfo);

    litebus::Future<KillResponse> OnExitInstance(const resource_view::InstanceInfo &instanceInfo, const Status &status);

    void ClearLocalDriver();
private:
    litebus::Future<Status> FcAccessorHeartbeatEnable(bool enable)
    {
        fcAccessorHeartbeat_ = enable;
        return Status::OK();
    }

    std::string nodeID_;
    InstanceCtrlConfig config_;

    std::shared_ptr<schedule_decision::Scheduler> scheduler_;
    std::shared_ptr<FunctionAgentMgr> functionAgentMgr_;
    std::shared_ptr<function_proxy::ControlPlaneObserver> observer_;
    std::shared_ptr<resource_view::ResourceViewMgr> resourceViewMgr_;
    std::shared_ptr<ControlInterfaceClientManagerProxy> clientManager_;
    std::shared_ptr<InstanceControlView> instanceControlView_;

    std::shared_ptr<LocalSchedSrv> localSchedSrv_;

    std::unordered_map<std::string, std::shared_ptr<litebus::Promise<std::shared_ptr<functionsystem::CallResult>>>>
        syncCreateCallResultPromises_;

    using KillResponsePromise = litebus::Promise<KillResponse>;
    std::unordered_map<std::string, std::shared_ptr<KillResponsePromise>> forwardCustomSignalNotifyPromise_;
    std::unordered_map<std::string, litebus::Future<core_service::KillResponse>> forwardCustomSignalRequestIDs_;

    using ForwardCallResultPromise = litebus::Promise<internal::ForwardCallResultResponse>;
    std::unordered_map<std::string, std::shared_ptr<ForwardCallResultPromise>> forwardCallResultPromise_;

    using SyncPromise = std::shared_ptr<litebus::Promise<Status>>;

    // first level key is functionAgentID, second level key is instanceID
    std::unordered_map<std::string, std::shared_ptr<function_proxy::InstanceInfoMap>> funcAgentMap_;
    std::unordered_map<std::string, std::pair<SyncPromise, int32_t>> syncRecoverPromises_;
    std::unordered_map<std::string, std::pair<SyncPromise, int32_t>> syncKillPromises_;
    std::unordered_map<std::string, std::pair<SyncPromise, int32_t>> syncDeployPromises_;
    std::unordered_map<std::string, int32_t> redeployTimesMap_;

    std::unordered_map<std::string, litebus::Promise<Status>> instanceStatusPromises_;
    std::unordered_map<std::string, InstanceReadyCallBack> instanceRegisteredReadyCallback_;

    std::unordered_map<std::string, CreateCallResultCallBack> createCallResultCallback_;

    // key: tenant id
    std::unordered_map<std::string, std::shared_ptr<TokenBucketRateLimiter>> rateLimiterMap_;

    std::unordered_map<std::string, FunctionMeta> funcMetaMap_;

    uint32_t maxForwardKillRetryTimes_ = MAX_FORWARD_KILL_RETRY_TIMES;
    uint32_t maxForwardKillRetryCycleMs_ = MAX_FORWARD_KILL_RETRY_CYCLE_MS;

    uint32_t maxForwardScheduleRetryTimes_ = MAX_FORWARD_SCHEDULE_RETRY_TIMES;

    std::set<std::string> concernedInstance_;
    bool fcAccessorHeartbeat_;
    bool isAbnormal_ = false;

    std::unordered_map<std::string, litebus::Timer> runtimeHeartbeatTimers_;

    inline static ExitHandler exitHandler_;

    friend class InstanceCtrlTest;
    friend class InstanceCtrlActorTest;
    litebus::Future<Status> TryRecover(const std::string &instanceID, const std::string &runtimeID,
                                       const std::string &errMsg, std::shared_ptr<InstanceStateMachine> &stateMachine,
                                       InstanceInfo &instanceInfo);

    std::shared_ptr<InstanceOperator> instanceOpt_;
    std::set<std::string> connectingDriver_;
    std::unordered_map<std::string, std::string> connectedDriver_;

    std::unordered_map<std::string, litebus::Promise<KillResponse>> exiting_;
    litebus::Future<KillResponse> HandleRemoteInstanceKill(const std::shared_ptr<KillContext> &killCtx,
                                                           bool isSynchronized);
    ClearGroupInstanceCallBack groupInstanceClear_;

    std::shared_ptr<ResourceGroupCtrl> rGroupCtrl_;

    std::shared_ptr<SubscriptionMgr> subscriptionMgr_;
};
}  // namespace functionsystem::local_scheduler
#endif  // LOCAL_SCHEDULER_INSTANCE_CTRL_ACTOR_H
