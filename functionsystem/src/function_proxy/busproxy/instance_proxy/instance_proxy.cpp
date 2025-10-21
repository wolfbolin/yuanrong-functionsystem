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
#include "instance_proxy.h"

#include "async/defer.hpp"
#include "metrics/metrics_adapter.h"
#include "status/status.h"
#include "busproxy/invocation_handler/invocation_handler.h"

namespace functionsystem::busproxy {

const std::string INSTANCE_EXIT_MESSAGE = "instance has been killed or exited.";
const std::string YR_ROUTE_KEY = "YR_ROUTE";
const uint32_t MAX_CALL_RESULT_RETRY_TIMES = 3;
void InstanceProxy::Init()
{
    ActorBase::Init();
    Receive("ForwardCall", &InstanceProxy::ForwardCall);
    Receive("ResponseForwardCall", &InstanceProxy::ResponseForwardCall);
    Receive("ForwardCallResult", &InstanceProxy::ForwardCallResult);
    Receive("ResponseForwardCallResult", &InstanceProxy::ResponseForwardCallResult);
}

litebus::Future<std::string> InstanceProxy::GetTenantID()
{
    ASSERT_FS(selfDispatcher_);
    return selfDispatcher_->GetTenantID();
}

litebus::Future<SharedStreamMsg> InstanceProxy::Call(const CallerInfo &callerInfo,
                                                     const std::string &dstInstanceID, const SharedStreamMsg &request,
                                                     const std::shared_ptr<TimePoint> &time)
{
    ASSERT_FS(request->has_callreq());
    const auto &callReq = request->callreq();
    YRLOG_INFO("{}|{}|received call request from {} to {}", callReq.traceid(), callReq.requestid(),
               callerInfo.instanceID, dstInstanceID);
    perf_->Record(callReq, dstInstanceID, time);
    // which means the invocation is happening without cross node
    // else it should be transferred by remote dispatcher
    if (dstInstanceID == instanceID_) {
        ASSERT_FS(selfDispatcher_);
        return selfDispatcher_->Call(request, callerInfo)
            .Then([aid(GetAID()), request, selfDispatcher(selfDispatcher_)](const SharedStreamMsg &callRsp) {
                litebus::Async(aid, &InstanceProxy::OnLocalCall, callRsp, request, selfDispatcher);
                return callRsp;
            });
    }
    // If the corresponding instance is not found in the dispatcher,
    // the instance information needs to be subscribed to from the observer.
    if (remoteDispatchers_.find(dstInstanceID) == remoteDispatchers_.end()) {
        auto dispatcher = std::make_shared<RequestDispatcher>(dstInstanceID, false, "", shared_from_this(), perf_);
        if (const auto it = callReq.createoptions().find(YR_ROUTE_KEY);
             it != callReq.createoptions().end() && !it->second.empty()) {
            auto info = std::make_shared<InstanceRouterInfo>();
            info->isLocal = false;
            info->remote = litebus::AID(dstInstanceID, it->second);
            info->isReady = true;
            info->isLowReliability = true;
            dispatcher->UpdateInfo(info);
        }
        ASSERT_FS(observer_);
        (void)observer_->SubscribeInstanceEvent(instanceID_, dstInstanceID);
        remoteDispatchers_[dstInstanceID] = std::move(dispatcher);
    }
    const auto &dispatcher = remoteDispatchers_[dstInstanceID];
    // remote response received by this actor, so the callback can be called in this actor thread
    auto func = [traceID(callReq.traceid()), requestID(callReq.requestid()),
                 dispatcher](const SharedStreamMsg &callRsp) {
        dispatcher->OnCall(callRsp, traceID, requestID);
        return callRsp;
    };
    return dispatcher->Call(request, callerInfo).Then(func);
}

void InstanceProxy::OnLocalCall(const litebus::Future<SharedStreamMsg> &callRspFut, const SharedStreamMsg &callReq,
                                const std::shared_ptr<RequestDispatcher> &dispatcher)
{
    ASSERT_FS(!callRspFut.IsError());
    const auto &callRsp = callRspFut.Get();
    auto &requestID = callReq->callreq().requestid();
    perf_->RecordReceivedCallRsp(requestID);
    dispatcher->OnCall(callRsp, callReq->callreq().traceid(), callReq->callreq().requestid());
}

void InstanceProxy::ForwardCall(const litebus::AID &from, std::string &&, std::string &&msg)
{
    auto srcInstanceID = from.Name();
    auto request = std::make_shared<runtime_rpc::StreamingMessage>();
    (void)request->ParseFromString(msg);
    ASSERT_FS(request->has_callreq());
    const auto &callReq = request->callreq();
    std::string srcTenantID = "";
    // if enable multi tenant, messageid contains tenantID of src instance, {tenantID}{requestID}
    if (request->messageid().length() > callReq.requestid().length()) {
        srcTenantID = request->messageid().substr(0, request->messageid().length() - callReq.requestid().length());
        request->set_messageid(callReq.requestid());
    }
    YRLOG_INFO("{}|{}|received forward Call instance from {} to {}, function name is {}", callReq.traceid(),
               callReq.requestid(), srcInstanceID, instanceID_, callReq.function());
    perf_->Record(callReq, instanceID_, nullptr);

    std::map<std::string, std::string> callCreateOptMap;
    for (const auto &ite : callReq.createoptions()) {
        callCreateOptMap[ite.first] = ite.second;
    }
    functionsystem::metrics::MetricsAdapter::GetInstance().GetMetricsContext().SetBillingInvokeOptions(
        callReq.requestid(), callCreateOptMap, callReq.function(), instanceID_);

    ASSERT_FS(selfDispatcher_);
    (void)selfDispatcher_->Call(request, CallerInfo{ .instanceID = srcInstanceID, .tenantID = srcTenantID })
        .OnComplete(litebus::Defer(GetAID(), &InstanceProxy::OnForwardCall, std::placeholders::_1, from, request,
                                   selfDispatcher_));
    // If the remote dispatcher does not have a corresponding sender instance,
    // we need to generate one and subscribe to it from the observer.
    if ((remoteDispatchers_.find(srcInstanceID) == remoteDispatchers_.end() ||
         remoteDispatchers_[srcInstanceID] == nullptr) &&
        srcInstanceID != instanceID_) {
        auto dispatcher = std::make_shared<RequestDispatcher>(srcInstanceID, false, "", shared_from_this(), perf_);
        ASSERT_FS(observer_);
        (void)observer_->SubscribeInstanceEvent(instanceID_, srcInstanceID, true);
        remoteDispatchers_[srcInstanceID] = dispatcher;
    }

    if (srcInstanceID != instanceID_) {
        // during recover, from.Name == instanceID_, dispatcher will be null
        auto dispatcher = remoteDispatchers_[srcInstanceID];
        dispatcher->UpdateRemoteAID(from);
    }
}

void InstanceProxy::Reject(const std::string &instanceID, const std::string &message, const StatusCode &code)
{
    if (instanceID == instanceID_) {
        ASSERT_FS(selfDispatcher_);
        selfDispatcher_->Reject(message, code);
        return;
    }
    if (remoteDispatchers_.find(instanceID) != remoteDispatchers_.end() && remoteDispatchers_[instanceID]) {
        remoteDispatchers_[instanceID]->Reject(message, code);
    }
}

void InstanceProxy::OnForwardCall(const litebus::Future<SharedStreamMsg> &callRspFut, const litebus::AID &from,
                                  const SharedStreamMsg &callReq, const std::shared_ptr<RequestDispatcher> &dispatcher)
{
    ASSERT_FS(!callRspFut.IsError());
    const auto &callRsp = callRspFut.Get();
    ASSERT_FS(callReq->has_callreq());
    auto call = callReq->callreq();
    auto &requestID = callReq->callreq().requestid();
    perf_->RecordReceivedCallRsp(requestID);
    dispatcher->OnCall(callRsp, call.traceid(), call.requestid());
    callRsp->set_messageid(call.requestid());
    YRLOG_INFO("{}|{}|ready to forward call response", call.traceid(), call.requestid());
    Send(from, "ResponseForwardCall", callRsp->SerializeAsString());
}

void InstanceProxy::ResponseForwardCall(const litebus::AID &from, std::string &&, std::string &&msg)
{
    auto request = std::make_shared<runtime_rpc::StreamingMessage>();
    (void)request->ParseFromString(msg);
    ASSERT_FS(request->has_callrsp());
    auto &requestID = request->messageid();
    perf_->RecordReceivedCallRsp(requestID);
    YRLOG_INFO("receive forward call response {} from {}", request->messageid(), std::string(from));
    if (auto promise(forwardCallPromises_.find(request->messageid())); promise != forwardCallPromises_.end()) {
        promise->second->SetValue(request);
        (void)forwardCallPromises_.erase(promise);
        return;
    }
    YRLOG_WARN("no request {} is waiting for forward call response, ignore it.", request->messageid());
}

litebus::Future<SharedStreamMsg> InstanceProxy::CallResult(const std::string &srcInstanceID,
                                                           const std::string &dstInstanceID,
                                                           const SharedStreamMsg &request,
                                                           const std::shared_ptr<TimePoint> &time)
{
    ASSERT_FS(request->has_callresultreq());
    const auto &callresult = request->callresultreq();
    auto &requestID = callresult.requestid();
    perf_->RecordCallResult(requestID, time);
    // which means the invocation is happening without cross node
    // else it should be transferred by remote dispatcher
    if (dstInstanceID == instanceID_) {
        ASSERT_FS(selfDispatcher_);
        return selfDispatcher_->CallResult(request)
            .Then([aid(GetAID()), request, dstInstanceID, srcInstanceID](const SharedStreamMsg &callResultAck) {
                litebus::Async(aid, &InstanceProxy::OnLocalCallResult, callResultAck, request, dstInstanceID,
                               srcInstanceID);
                return callResultAck;
            });
    }
    if (remoteDispatchers_.find(dstInstanceID) == remoteDispatchers_.end() ||
        remoteDispatchers_[dstInstanceID] == nullptr) {
        auto dispatcher = std::make_shared<RequestDispatcher>(dstInstanceID, false, "", shared_from_this(), perf_);
        ASSERT_FS(observer_);
        // If the destination instance is not found (usually after the proxy is restarted), subscribe to the
        // corresponding instance information and try again.
        auto future = observer_->SubscribeInstanceEvent(instanceID_, dstInstanceID)
                          .Then([aid(GetAID()), srcInstanceID, dstInstanceID, request, time](const Status &) {
                              return litebus::Async(aid, &InstanceProxy::RetryCallResult, srcInstanceID, dstInstanceID,
                                                    request, time);
                          });
        remoteDispatchers_[dstInstanceID] = dispatcher;
        return future;
    }
    auto dispatcher = remoteDispatchers_[dstInstanceID];
    auto callResultCode = callresult.code();
    // remote response received by this actor, so the callback can be called in this actor thread
    auto func = [requestID(callresult.requestid()), dispatcher(selfDispatcher_), callResultCode]
            (const SharedStreamMsg &callResultAck) {
                dispatcher->OnCallResult(callResultAck, requestID, callResultCode);
                return callResultAck;
            };
    return dispatcher->CallResult(request).Then(func);
}

litebus::Future<SharedStreamMsg> InstanceProxy::RetryCallResult(const std::string &srcInstanceID,
                                                                const std::string &dstInstanceID,
                                                                const SharedStreamMsg &request,
                                                                const std::shared_ptr<TimePoint> &time)
{
    if (remoteDispatchers_.find(dstInstanceID) != remoteDispatchers_.end()) {
        failedSubDstRouteOnCallResult_.erase(dstInstanceID);
        return CallResult(srcInstanceID, dstInstanceID, request, time);
    }
    static const uint32_t MAX_FAILED_TIMES = 3;
    static const int64_t DEFER_RETRY = 1000;
    if (failedSubDstRouteOnCallResult_[dstInstanceID] < MAX_FAILED_TIMES) {
        failedSubDstRouteOnCallResult_[dstInstanceID]++;
        YRLOG_WARN("subscribe dstInstance({}) for call result from {} failed {} times, retry again", dstInstanceID,
                   srcInstanceID, failedSubDstRouteOnCallResult_[dstInstanceID]);
        auto promise = std::make_shared<litebus::Promise<SharedStreamMsg>>();
        litebus::AsyncAfter(DEFER_RETRY, GetAID(), &InstanceProxy::DeferRetryCallResult, srcInstanceID,
                            dstInstanceID, request, time, promise);
        return promise->GetFuture();
    }
    YRLOG_ERROR("subscribe dstInstance({}) for call result from {} failed {} times, instance not found", dstInstanceID,
                srcInstanceID, failedSubDstRouteOnCallResult_[dstInstanceID]);
    failedSubDstRouteOnCallResult_.erase(dstInstanceID);
    auto response = std::make_shared<runtime_rpc::StreamingMessage>();
    response->set_messageid(request->messageid());
    auto callResultAck = response->mutable_callresultack();
    callResultAck->set_code(Status::GetPosixErrorCode(StatusCode::ERR_INSTANCE_NOT_FOUND));
    callResultAck->set_message("instance not found or instance may not be recovered");
    return response;
}

void InstanceProxy::DeferRetryCallResult(const std::string &srcInstanceID, const std::string &dstInstanceID,
                                         const SharedStreamMsg &request, const std::shared_ptr<TimePoint> &time,
                                         const std::shared_ptr<litebus::Promise<SharedStreamMsg>> &promise)
{
    auto future = CallResult(srcInstanceID, dstInstanceID, request, time);
    promise->Associate(future);
}

void InstanceProxy::OnLocalCallResult(const litebus::Future<SharedStreamMsg> &callResultAckFut,
                                      const SharedStreamMsg &callResult, const std::string &dstInstance,
                                      const std::string &srcInstance)
{
    ASSERT_FS(!callResultAckFut.IsError());
    const auto &callResultAck = callResultAckFut.Get();

    auto &requestID = callResult->callresultreq().requestid();
    perf_->EndRecord(requestID);
    if (remoteDispatchers_.find(dstInstance) != remoteDispatchers_.end() && remoteDispatchers_[dstInstance]) {
        remoteDispatchers_[dstInstance]->OnCallResult(callResultAck, callResult->callresultreq().requestid(),
                                                      callResult->callresultreq().code());
    }
    if (remoteDispatchers_.find(srcInstance) != remoteDispatchers_.end() && remoteDispatchers_[srcInstance]) {
        remoteDispatchers_[srcInstance]->OnCallResult(callResultAck, callResult->callresultreq().requestid(),
                                                      callResult->callresultreq().code());
    }
    if (srcInstance == instanceID_) {
        ASSERT_FS(selfDispatcher_);
        selfDispatcher_->OnCallResult(callResultAck, callResult->callresultreq().requestid(),
                                      callResult->callresultreq().code());
        InvocationHandler::ReleaseEstimateMemory(srcInstance, callResult->callresultreq().requestid());
        return;
    }
    litebus::AID aid(srcInstance, GetAID().Url());
    litebus::Async(aid, &InstanceProxy::OnLocalCallResult, callResultAckFut, callResult, dstInstance,
        srcInstance);
}

void InstanceProxy::ForwardCallResult(const litebus::AID &from, std::string &&, std::string &&msg)
{
    auto srcInstanceID = from.Name();
    auto request = std::make_shared<runtime_rpc::StreamingMessage>();
    (void)request->ParseFromString(msg);
    ASSERT_FS(request->has_callresultreq());
    auto callResult = request->callresultreq();
    auto &requestID = request->messageid();
    perf_->RecordCallResult(requestID, nullptr);
    YRLOG_INFO("{}|receive forward call result from {}", callResult.requestid(), std::string(from));
    ASSERT_FS(selfDispatcher_);
    (void)selfDispatcher_->CallResult(request).OnComplete(litebus::Defer(
        GetAID(), &InstanceProxy::OnForwardCallResult, std::placeholders::_1, from, request, srcInstanceID));
}

void InstanceProxy::OnForwardCallResult(const litebus::Future<SharedStreamMsg> &callResultAckFut,
                                        const litebus::AID &from, const SharedStreamMsg &callResult,
                                        const std::string &srcInstance)
{
    ASSERT_FS(!callResultAckFut.IsError());
    const auto &callResultAck = callResultAckFut.Get();
    ASSERT_FS(callResultAck->has_callresultack());
    auto &requestID = callResult->callresultreq().requestid();
    perf_->EndRecord(requestID);
    if (remoteDispatchers_.find(srcInstance) != remoteDispatchers_.end() && remoteDispatchers_[srcInstance]) {
        remoteDispatchers_[srcInstance]->OnCallResult(callResultAck, callResult->callresultreq().requestid(),
                                                      callResult->callresultreq().code());
    }
    if (callResult->callresultreq().instanceid() == instanceID_) {
        InvocationHandler::ReleaseEstimateMemory(from.Name(), callResult->callresultreq().requestid());
    }
    callResultAck->set_messageid(callResult->callresultreq().requestid());
    YRLOG_INFO("{}|ready send forward call result response", callResult->callresultreq().requestid());
    Send(from, "ResponseForwardCallResult", callResultAck->SerializeAsString());
}

void InstanceProxy::ResponseForwardCallResult(const litebus::AID &from, std::string &&, std::string &&msg)
{
    auto request = std::make_shared<runtime_rpc::StreamingMessage>();
    (void)request->ParseFromString(msg);
    ASSERT_FS(request->has_callresultack());
    auto &requestID = request->messageid();
    perf_->EndRecord(requestID);
    YRLOG_INFO("receive forward call result response {} from {}", request->messageid(), std::string(from));
    if (auto promise(forwardCallResultPromises_.find(request->messageid()));
        promise != forwardCallResultPromises_.end()) {
        promise->second->SetValue(request);
        (void)forwardCallResultPromises_.erase(promise);
        return;
    }
    YRLOG_WARN("no request {} is waiting for forward callresult ack, ignore it.", request->messageid());
}

litebus::Future<SharedStreamMsg> InstanceProxy::SendForwardCall(const litebus::AID &aid,
                                                                const std::string &callerTenantID,
                                                                const SharedStreamMsg &request)
{
    ASSERT_FS(request->has_callreq());
    auto promise = std::make_shared<litebus::Promise<SharedStreamMsg>>();
    if (callerTenantID.empty()) {
        request->set_messageid(request->callreq().requestid());
    } else {
        // if enable multi tenant, messageid contains tenantID of src instance, {tenantID}{requestID}
        request->set_messageid(callerTenantID + request->callreq().requestid());
    }
    forwardCallPromises_[request->callreq().requestid()] = promise;
    YRLOG_INFO("{}|{}|(forwardInvoke)send forward call", request->callreq().traceid(), request->callreq().requestid());
    // send forwardCall request to another proxy actor
    (void)Send(aid, "ForwardCall", request->SerializeAsString());
    return promise->GetFuture();
}

litebus::Future<SharedStreamMsg> InstanceProxy::SendForwardCallResult(const litebus::AID &aid,
                                                                      const SharedStreamMsg &request)
{
    ASSERT_FS(request->has_callresultreq());
    auto promise = std::make_shared<litebus::Promise<SharedStreamMsg>>();
    request->set_messageid(request->callresultreq().requestid());
    forwardCallResultPromises_[request->messageid()] = promise;
    YRLOG_INFO("{}|(forwardCallResult)send forward callresult to {}", request->callresultreq().requestid(),
               aid.HashString());
    // send forwardCallResult request to another proxy actor
    Send(aid, "ForwardCallResult", request->SerializeAsString());
    return promise->GetFuture();
}

void InstanceProxy::NotifyChanged(const std::string &instanceID, const std::shared_ptr<InstanceRouterInfo> &info)
{
    ASSERT_IF_NULL(info);
    if (instanceID == instanceID_) {
        ASSERT_FS(selfDispatcher_);
        selfDispatcher_->UpdateInfo(info);
        if (!info->isLocal && info->isReady) {
            // MigratingRequest is already running,
            // notify observer to terminate actor because of instance has been remote
            YRLOG_INFO("instance {} is already migrated to {}, instance proxy on local should be terminate", instanceID,
                       info->proxyID);
            RETURN_IF_NULL(observer_);
            observer_->NotifyMigratingRequest(instanceID_);
        }
        return;
    }
    if (remoteDispatchers_.find(instanceID) == remoteDispatchers_.end()) {
        auto dispatcher = std::make_shared<RequestDispatcher>(instanceID, false, "", shared_from_this(), perf_);
        remoteDispatchers_[instanceID] = dispatcher;
    }
    remoteDispatchers_[instanceID]->UpdateInfo(info);
}

void InstanceProxy::Fatal(const std::string &instanceID, const std::string &message, const StatusCode &code)
{
    if (instanceID == instanceID_) {
        ASSERT_FS(selfDispatcher_);
        selfDispatcher_->Fatal(message, code);
        return;
    }
    if (remoteDispatchers_.find(instanceID) != remoteDispatchers_.end() && remoteDispatchers_[instanceID]) {
        remoteDispatchers_[instanceID]->Fatal(message, code);
    }
}

std::list<litebus::Future<SharedStreamMsg>> InstanceProxy::GetOnRespFuture()
{
    ASSERT_FS(selfDispatcher_);
    return selfDispatcher_->GetOnRespFuture();
}

void InstanceProxy::DeleteRemoteDispatcher(const std::string &instanceID)
{
    if (remoteDispatchers_.find(instanceID) != remoteDispatchers_.end() && remoteDispatchers_[instanceID]) {
        remoteDispatchers_[instanceID]->Fatal(INSTANCE_EXIT_MESSAGE, StatusCode::ERR_INSTANCE_EXITED);
    }
    (void)remoteDispatchers_.erase(instanceID);
}

bool InstanceProxy::Delete()
{
    ASSERT_FS(selfDispatcher_);
    selfDispatcher_->Fatal(INSTANCE_EXIT_MESSAGE, StatusCode::ERR_INSTANCE_EXITED);
    return true;
}

void InstanceProxy::InitDispatcher()
{
    selfDispatcher_ = std::make_shared<RequestDispatcher>(instanceID_, true, tenantID_, shared_from_this(), perf_);
}

litebus::Future<SharedStreamMsg> InstanceProxyWrapper::Call(const litebus::AID &to,
                                                            const CallerInfo &callerInfo,
                                                            const std::string &instanceID,
                                                            const SharedStreamMsg &request,
                                                            const std::shared_ptr<TimePoint> &time)
{
    return litebus::Async(to, &InstanceProxy::Call, callerInfo, instanceID, request, time);
}

litebus::Future<SharedStreamMsg> InstanceProxyWrapper::CallResult(const litebus::AID &to,
                                                                  const std::string &srcInstanceID,
                                                                  const std::string &dstInstanceID,
                                                                  const SharedStreamMsg &request,
                                                                  const std::shared_ptr<TimePoint> &time)
{
    return litebus::Async(to, &InstanceProxy::CallResult, srcInstanceID, dstInstanceID, request, time);
}

litebus::Future<std::string> InstanceProxyWrapper::GetTenantID(const litebus::AID &to)
{
    return litebus::Async(to, &InstanceProxy::GetTenantID);
}

}  // namespace functionsystem::busproxy