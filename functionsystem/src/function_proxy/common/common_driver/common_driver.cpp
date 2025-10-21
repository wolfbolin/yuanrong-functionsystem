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
#include "common_driver.h"

#include "common/constants/actor_name.h"
#include "meta_store_monitor/meta_store_monitor_factory.h"
#include "common/posix_client/shared_client/shared_client_manager.h"
#include "common/posix_client/shared_client/posix_stream_manager_proxy.h"
#include "function_proxy/common/state_handler/state_handler.h"

namespace functionsystem::function_proxy {

void PrepareMetaStoreConfigs(const function_proxy::Flags &flags, MetaStoreTimeoutOption &option,
                             MetaStoreMonitorParam &param, MetaStoreConfig &metaStoreConfig)
{
    param.maxTolerateFailedTimes = flags.GetMaxTolerateMetaStoreFailedTimes();
    param.checkIntervalMs = flags.GetMetaStoreCheckInterval();
    param.timeoutMs = flags.GetMetaStoreCheckTimeout();

    // if enable, metastore address is master ip + global scheduler port; etcd address is used for persistence
    // else metastore address is etcd ip
    metaStoreConfig.enableMetaStore = flags.GetEnableMetaStore();
    metaStoreConfig.etcdTablePrefix = flags.GetETCDTablePrefix();
    metaStoreConfig.excludedKeys = flags.GetMetaStoreExcludedKeys();
    if (metaStoreConfig.enableMetaStore) {
        metaStoreConfig.etcdAddress = flags.GetEtcdAddress();
        metaStoreConfig.metaStoreAddress = flags.GetMetaStoreAddress();
    } else {
        metaStoreConfig.etcdAddress = flags.GetMetaStoreAddress();
    }

    // retries must take longer than health check
    option.operationRetryTimes =
        static_cast<int64_t>((param.maxTolerateFailedTimes + 1) * (param.checkIntervalMs + param.timeoutMs) /
                             KV_OPERATE_RETRY_INTERVAL_LOWER_BOUND);
}

std::string GetMonitorAddress(const function_proxy::Flags &flags)
{
    // if enabled, return master address; else return etcd address
    return flags.GetMetaStoreAddress();
}

void CommonDriver::BindStateActor()
{
    distributedCacheClient_->EnableDSClient(true);
    auto stateClient = std::make_shared<StateClient>(distributedCacheClient_);
    auto stateActor = std::make_shared<function_proxy::StateActor>(stateClient);
    litebus::Spawn(stateActor);
    StateHandler::BindStateActor(stateActor);
}

void CommonDriver::CreateDistributedCacheClient()
{
    datasystem::ConnectOptions connectOptions;
    DSCacheClientImpl::GetAuthConnectOptions(dsAuthConfig_, connectOptions);
    connectOptions.host = flags_.GetCacheStorageHost();
    connectOptions.port = flags_.GetCacheStoragePort();

    distributedCacheClient_ = std::make_shared<DSCacheClientImpl>(connectOptions);
    distributedCacheClient_->SetDSAuthEnable(flags_.GetCacheStorageAuthEnable());
}

void CommonDriver::InitDistributedCache()
{
    CreateDistributedCacheClient();
    if (flags_.GetStateStorageType() == function_proxy::DATA_SYSTEM_STORE) {
        BindStateActor();
    }
}

Status CommonDriver::InitMetaStoreClient()
{
    YRLOG_INFO("start to init meta store client");
    MetaStoreTimeoutOption option;
    MetaStoreMonitorParam param;
    MetaStoreConfig metaStoreConfig;
    PrepareMetaStoreConfigs(flags_, option, param, metaStoreConfig);

    while (true) {
        metaStoreClient_ = MetaStoreClient::Create(metaStoreConfig, GetGrpcSSLConfig(flags_), option, true, param);
        auto metaStoreMonitor = MetaStoreMonitorFactory::GetInstance().GetMonitor(GetMonitorAddress(flags_));
        if (metaStoreClient_ == nullptr || metaStoreMonitor == nullptr
            || metaStoreMonitor->CheckMetaStoreConnected().IsError()) {
            return Status(StatusCode::FAILED, "meta store connected failed");
        }
        YRLOG_INFO("successful to init meta store client");
        return Status::OK();
    }
}

void CommonDriver::CreateDataAndControlInterfaceClient()
{
    YRLOG_INFO("start to create posix interface client");
    static std::shared_ptr<SharedClientManager> sharedClientMgr =
        std::make_shared<SharedClientManager>("SharedPosixClientManager");
    litebus::Spawn(sharedClientMgr);
    auto posixStreamManagerProxy = std::make_shared<PosixStreamManagerProxy>(sharedClientMgr->GetAID());
    posixService_->RegisterUpdatePosixClientCallback(
        std::bind(&PosixStreamManagerProxy::UpdateControlInterfacePosixClient, posixStreamManagerProxy,
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    dataInterfaceClient_ = posixStreamManagerProxy;
    controlInterfaceClient_ = posixStreamManagerProxy;
    return;
}

void CommonDriver::InitObserver(const std::shared_ptr<MetaStorageAccessor> &metaStorageAccessor)
{
    YRLOG_INFO("start to init observer");
    // create observer actor
    observerActor_ = std::make_shared<function_proxy::ObserverActor>(
        FUNCTION_PROXY_OBSERVER_ACTOR_NAME, flags_.GetNodeID(), metaStorageAccessor,
        ObserverParam{ flags_.GetServicesPath(), flags_.GetLibPath(), flags_.GetFunctionMetaPath(),
                       flags_.GetEnableTenantAffinity(),
                       flags_.GetEnableMetaStore(), flags_.IsPartialWatchInstances(), flags_.GetServiceTTL() });
    observerActor_->BindDataInterfaceClientManager(dataInterfaceClient_);
    YRLOG_INFO("successful to init observer");
}

Status CommonDriver::Init()
{
    if (auto status = InitMetaStoreClient(); status.IsError()) {
        YRLOG_ERROR("failed to init meta store client. err: {}", status.ToString());
        return status;
    }

    metaStorageAccessor_ = std::make_shared<MetaStorageAccessor>(metaStoreClient_);
    InitDistributedCache();
    // posix stream interface
    CreateDataAndControlInterfaceClient();
    InitObserver(metaStorageAccessor_);
    return Status::OK();
}

Status CommonDriver::Start()
{
    if (observerActor_ != nullptr) {
        litebus::Spawn(observerActor_);
    }
    return Status::OK();
}

Status CommonDriver::Sync()
{
    if (observerActor_ == nullptr) {
        return Status(StatusCode::FAILED, "observer is not init");
    }
    YRLOG_INFO("start to sync observer");
    auto status = litebus::Async(observerActor_->GetAID(), &function_proxy::ObserverActor::Register).Get();
    if (status.IsError()) {
        YRLOG_ERROR("failed to register observer");
        return status;
    }
    YRLOG_INFO("successful to sync observer");
    return Status::OK();
}

Status CommonDriver::Stop()
{
    MetaStoreMonitorFactory::GetInstance().Clear();
    if (observerActor_ != nullptr) {
        litebus::Terminate(observerActor_->GetAID());
    }
    return Status::OK();
}

void CommonDriver::Await()
{
    if (observerActor_ != nullptr) {
        litebus::Await(observerActor_->GetAID());
    }
}

}  // namespace functionsystem::function_proxy
