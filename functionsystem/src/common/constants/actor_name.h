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

#ifndef COMMON_CONSTANTS_ACTOR_NAME_H
#define COMMON_CONSTANTS_ACTOR_NAME_H

#include <string>

namespace functionsystem {
// FunctionProxy
const std::string FUNCTION_PROXY_OBSERVER_ACTOR_NAME = "FunctionProxyObserverActor";

// LocalScheduler
const std::string LOCAL_SCHED_SRV_ACTOR_NAME = "LocalSchedSrvActor";
const std::string LOCAL_SCHED_INSTANCE_CTRL_ACTOR_NAME_POSTFIX = "-LocalSchedInstanceCtrlActor";
const std::string LOCAL_SCHED_FUNC_AGENT_MGR_ACTOR_NAME_POSTFIX = "-LocalSchedFuncAgentMgrActor";
const std::string LOCAL_GROUP_CTRL_ACTOR_NAME = "LocalGroupCtrlActor";
const std::string BUNDLE_MGR_ACTOR_NAME = "BundleMgrActor";
const std::string SUBSCRIPTION_MGR_ACTOR_NAME_POSTFIX = "-SubscriptionMgrActor";

// GlobalScheduler
const std::string GLOBAL_SCHED_ACTOR_NAME = "GlobalSchedActor";
const std::string DOMAIN_SCHED_MGR_ACTOR_NAME = "DomainSchedulerManager";
const std::string LOCAL_SCHED_MGR_ACTOR_NAME = "LocalSchedulerManager";

// DomainScheduler
const std::string DOMAIN_SCHEDULER_SRV_ACTOR_NAME_POSTFIX = "-DomainSchedulerSrv";
const std::string DOMAIN_UNDERLAYER_SCHED_MGR_ACTOR_NAME_POSTFIX = "-UnderlayerSchedMgr";
const std::string DOMAIN_GROUP_CTRL_ACTOR_NAME = "DomainGroupCtrlActor";

// FunctionAgent
const std::string FUNCTION_AGENT_AGENT_SERVICE_ACTOR_NAME = "AgentServiceActor";

// RuntimeManager
const std::string RUNTIME_MANAGER_PINGPONG_ACTOR_NAME = "RuntimeManagerPong";
const std::string RUNTIME_MANAGER_SRV_ACTOR_NAME = "-RuntimeManagerSrv";
const std::string RUNTIME_MANAGER_HEALTH_CHECK_ACTOR_NAME = "HealthCheckActor";
const std::string RUNTIME_MANAGER_LOG_MANAGER_ACTOR_NAME = "LogManagerActor";
const std::string RUNTIME_MANAGER_DEBUG_SERVER_MGR_ACTOR_NAME = "DebugServerMgrActor";

// FunctionAccessor
const std::string FUNCTION_ACCESSOR_HTTP_SERVER = "FunctionAccessorHttpServer";
const std::string FUNCTION_ACCESSOR_CONTROL_ACTOR = "FunctionAccessorControlActor";
const std::string FUNCTION_ACCESSOR_INVOKE_ACTOR = "FunctionAccessorInvokeActor";
const std::string FUNCTION_ACCESSOR_SCHEDULE_ACTOR = "FunctionAccessorScheduleActor";

// SystemFunctionLoader
const std::string SYSTEM_FUNCTION_LOADER_BOOTSTRAP_ACTOR = "SystemFunctionLoaderBootstrapActor";

// Scaler
const std::string SCALER_ACTOR = "ScalerActor";

// TraceActor
const std::string TRACE_ACTOR = "TraceActor";

// InstanceManager
const std::string INSTANCE_MANAGER_ACTOR_NAME = "InstanceManagerActor";
// GroupManager
const std::string GROUP_MANAGER_ACTOR_NAME = "GroupManagerActor";
// TokenManagerActor
const std::string TOKEN_MANAGER_ACTOR_NAME = "TokenManagerActor";
// AKSKManagerActor
const std::string AKSK_MANAGER_ACTOR_NAME = "AKSKManagerActor";

// IAMServer
const std::string IAM_ACTOR = "IAMActor";

// resource group manager
const std::string RESOURCE_GROUP_MANAGER = "ResourceGroupManager";

}  // namespace functionsystem
#endif  // COMMON_CONSTANTS_ACTOR_NAME_H
