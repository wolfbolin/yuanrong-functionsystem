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

#ifndef FUNCTION_PROXY_BUSPROXY_INSTANCE_PROXY_INSTANCE_PROXY_H
#define FUNCTION_PROXY_BUSPROXY_INSTANCE_PROXY_INSTANCE_PROXY_H
#include <memory>
#include <unordered_map>

#include "actor/actor.hpp"
#include "async/future.hpp"
#include "proto/pb/posix_pb.h"
#include "request_sync_helper.h"
#include "function_proxy/busproxy/instance_proxy/forward_interface.h"
#include "function_proxy/busproxy/instance_proxy/request_dispatcher.h"
#include "function_proxy/busproxy/instance_proxy/perf.h"
#include "function_proxy/common/observer/data_plane_observer/data_plane_observer.h"

namespace functionsystem::busproxy {
class InstanceProxy : public litebus::ActorBase,
                      public std::enable_shared_from_this<InstanceProxy>,
                      public ForwardInterface {
public:
    InstanceProxy(const std::string &instanceID, const std::string &tenantID)
        : litebus::ActorBase(instanceID), instanceID_(instanceID), tenantID_(tenantID), perf_(std::make_shared<Perf>())
    {
    }
    ~InstanceProxy() override = default;

    litebus::Future<std::string> GetTenantID();

    litebus::Future<SharedStreamMsg> Call(const CallerInfo &callerInfo, const std::string &instanceID,
                                          const SharedStreamMsg &request, const std::shared_ptr<TimePoint> &time);

    void InitDispatcher();

    void ForwardCall(const litebus::AID &from, std::string &&, std::string &&msg);

    void ResponseForwardCall(const litebus::AID &from, std::string &&, std::string &&msg);

    litebus::Future<SharedStreamMsg> CallResult(const std::string &srcInstanceID, const std::string &dstInstanceID,
                                                const SharedStreamMsg &request, const std::shared_ptr<TimePoint> &time);

    void ForwardCallResult(const litebus::AID &from, std::string &&, std::string &&msg);

    void ResponseForwardCallResult(const litebus::AID &from, std::string &&, std::string &&msg);

    litebus::Future<SharedStreamMsg> SendForwardCall(const litebus::AID &aid, const std::string &callerTenantID,
                                                     const SharedStreamMsg &request) override;

    litebus::Future<SharedStreamMsg> SendForwardCallResult(const litebus::AID &aid,
                                                           const SharedStreamMsg &request) override;

    void NotifyChanged(const std::string &instanceID, const std::shared_ptr<InstanceRouterInfo> &info);

    void Fatal(const std::string &instanceID, const std::string &message, const StatusCode &code);

    void Reject(const std::string &instanceID, const std::string &message, const StatusCode &code);

    std::list<litebus::Future<SharedStreamMsg>> GetOnRespFuture();

    void DeleteRemoteDispatcher(const std::string &instanceID);

    bool Delete();

    static void BindObserver(const std::shared_ptr<function_proxy::DataPlaneObserver> &observer)
    {
        observer_ = observer;
    }

protected:
    void Init() override;

private:
    void OnLocalCall(const litebus::Future<SharedStreamMsg> &callRspFut, const SharedStreamMsg &callReq,
                     const std::shared_ptr<RequestDispatcher> &dispatcher);
    void OnForwardCall(const litebus::Future<SharedStreamMsg> &callRspFut, const litebus::AID &from,
                       const SharedStreamMsg &callReq, const std::shared_ptr<RequestDispatcher> &dispatcher);

    void OnForwardCallResult(const litebus::Future<SharedStreamMsg> &callResultAckFut, const litebus::AID &from,
                             const SharedStreamMsg &callResult, const std::string &srcInstance);
    void OnLocalCallResult(const litebus::Future<SharedStreamMsg> &callResultAckFut, const SharedStreamMsg &callResult,
                           const std::string &dstInstance, const std::string &srcInstance);

    litebus::Future<SharedStreamMsg> RetryCallResult(const std::string &srcInstanceID, const std::string &dstInstanceID,
                                                     const SharedStreamMsg &request,
                                                     const std::shared_ptr<TimePoint> &time);

    void DeferRetryCallResult(const std::string &srcInstanceID, const std::string &dstInstanceID,
                              const SharedStreamMsg &request, const std::shared_ptr<TimePoint> &time,
                              const std::shared_ptr<litebus::Promise<SharedStreamMsg>> &promise);

private:
    inline static std::shared_ptr<function_proxy::DataPlaneObserver> observer_ { nullptr };
    std::string instanceID_;
    std::string tenantID_;
    std::shared_ptr<RequestDispatcher> selfDispatcher_ { nullptr };
    std::unordered_map<std::string, std::shared_ptr<RequestDispatcher>> remoteDispatchers_;
    std::map<std::string, std::shared_ptr<litebus::Promise<SharedStreamMsg>>> forwardCallPromises_;
    std::map<std::string, std::shared_ptr<litebus::Promise<SharedStreamMsg>>> forwardCallResultPromises_;
    std::unordered_map<std::string, std::shared_ptr<PerfContext>> perfMap_;
    std::shared_ptr<Perf> perf_;
    // callresult subscribe failed times
    std::unordered_map<std::string, uint32_t> failedSubDstRouteOnCallResult_;
};

class InstanceProxyWrapper {
public:
    InstanceProxyWrapper() = default;
    virtual ~InstanceProxyWrapper() = default;
    virtual litebus::Future<SharedStreamMsg> Call(const litebus::AID &to,
                                                  const CallerInfo &callerInfo,
                                                  const std::string &instanceID, const SharedStreamMsg &request,
                                                  const std::shared_ptr<TimePoint> &time);
    virtual litebus::Future<SharedStreamMsg> CallResult(const litebus::AID &to, const std::string &srcInstanceID,
                                                        const std::string &dstInstanceID,
                                                        const SharedStreamMsg &request,
                                                        const std::shared_ptr<TimePoint> &time);
    virtual litebus::Future<std::string> GetTenantID(const litebus::AID &to);
};
}  // namespace functionsystem::busproxy

#endif  // FUNCTION_PROXY_BUSPROXY_INSTANCE_PROXY_INSTANCE_PROXY_H
