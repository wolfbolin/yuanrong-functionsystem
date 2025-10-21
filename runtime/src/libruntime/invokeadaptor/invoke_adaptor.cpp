/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2025. All rights reserved.
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

#include "invoke_adaptor.h"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "absl/synchronization/notification.h"
#include "general_execution_manager.h"
#include "ordered_execution_manager.h"
#include "src/libruntime/fsclient/protobuf/common.pb.h"
#include "src/libruntime/fsclient/protobuf/runtime_service.pb.h"
#include "src/libruntime/groupmanager/function_group.h"
#include "src/libruntime/metricsadaptor/metrics_adaptor.h"
#include "src/proto/libruntime.pb.h"
#include "src/utility/logger/logger.h"
#include "src/utility/string_utility.h"

namespace YR {
namespace Libruntime {
using namespace std::placeholders;
using namespace YR::utility;
using json = nlohmann::json;

const int METADATA_INDEX = 0;
const int ARGS_INDEX = 1;
const int FLAG_OF_REQUEST_NO_TIMEOUT = -1;
constexpr size_t FIBER_STACK_SIZE = 1024 * 256;
int g_killTimeout = 30000;
const static std::string DEFAULT_FUNCTION_LIB_PATH = "/dcache/layer/func";
const static std::string HETERO_NAME = "device";
const static int SCHEDULER_DATA_INDEX = 2;

template <typename T>
void SetResponse(T &response, int code)
{
    response.set_code(static_cast<common::ErrorCode>(code));
    response.set_message(ErrMsgMap.at(static_cast<common::ErrorCode>(code)));
}

template <typename T>
void SetResponse(T &response, int code, const std::string &msg)
{
    response.set_code(static_cast<common::ErrorCode>(code));
    response.set_message(msg);
}

libruntime::FunctionMeta convertFuncMetaToProto(std::shared_ptr<InvokeSpec> spec)
{
    libruntime::FunctionMeta meta;
    meta.set_applicationname(spec->functionMeta.appName);
    meta.set_apitype(spec->functionMeta.apiType);
    meta.set_classname(spec->functionMeta.className);
    meta.set_codeid(spec->functionMeta.codeId);
    meta.set_functionid(spec->functionMeta.functionId);
    meta.set_functionname(spec->functionMeta.funcName);
    meta.set_initializercodeid(spec->functionMeta.initializerCodeId);
    meta.set_isgenerator(spec->functionMeta.isGenerator);
    meta.set_isasync(spec->functionMeta.isAsync);
    meta.set_language(spec->functionMeta.languageType);
    meta.set_modulename(spec->functionMeta.moduleName);
    meta.set_signature(spec->functionMeta.signature);
    meta.set_name(spec->functionMeta.name.value_or(""));
    meta.set_ns(spec->functionMeta.ns.value_or(""));
    return meta;
}

YR::Libruntime::FunctionMeta convertProtoToFuncMeta(const libruntime::FunctionMeta &funcMetaProto)
{
    YR::Libruntime::FunctionMeta funcMeta;
    funcMeta.appName = funcMetaProto.applicationname();
    funcMeta.moduleName = funcMetaProto.modulename();
    funcMeta.funcName = funcMetaProto.functionname();
    funcMeta.functionId = funcMetaProto.functionid();
    funcMeta.className = funcMetaProto.classname();
    funcMeta.languageType = funcMetaProto.language();
    funcMeta.name = funcMetaProto.name();
    funcMeta.ns = funcMetaProto.ns();
    funcMeta.isAsync = funcMetaProto.isasync();
    funcMeta.isGenerator = funcMetaProto.isgenerator();
    funcMeta.codeId = funcMetaProto.codeid();
    funcMeta.needOrder = funcMetaProto.needorder();
    return funcMeta;
}

bool ParseFunctionGroupRunningInfo(const CallRequest &request, bool isPosix,
                                   common::FunctionGroupRunningInfo &runningInfo)
{
    if (isPosix) {
        return true;
    }
    if (request.createoptions().find("FUNCTION_GROUP_RUNNING_INFO") == request.createoptions().end()) {
        return true;
    }
    if (!google::protobuf::util::JsonStringToMessage(request.createoptions().at("FUNCTION_GROUP_RUNNING_INFO"),
                                                     &runningInfo)
             .ok()) {
        YRLOG_ERROR("parse function group info failed! request id: {}", request.requestid());
        return false;
    }
    return true;
}

bool ParseMetaData(const CallRequest &request, bool isPosix, libruntime::MetaData &metaData)
{
    if (isPosix) {
        if (request.iscreate()) {
            metaData.set_invoketype(libruntime::InvokeType::CreateInstance);
        } else {
            metaData.set_invoketype(libruntime::InvokeType::InvokeFunction);
        }
        return true;
    }

    if (request.args_size() == 0) {
        return false;
    }

    if (!metaData.ParseFromArray(request.args(METADATA_INDEX).value().data(),
                                 request.args(METADATA_INDEX).value().size())) {
        YRLOG_ERROR("Parse metadata failed! request ID: {}", request.requestid());
        return false;
    }

    return true;
}

bool ParseRequest(const CallRequest &request, std::vector<std::shared_ptr<DataObject>> &rawArgs,
                  std::shared_ptr<MemoryStore> memStore, bool isPosix)
{
    int argStart = isPosix ? METADATA_INDEX : ARGS_INDEX;
    for (int i = argStart; i < request.args_size(); i++) {
        std::shared_ptr<DataObject> rawArg;
        if (request.args(i).type() == common::Arg::OBJECT_REF) {
            // get arg by argid from ds
            std::string argId = std::string(request.args(i).value().data(), request.args(i).value().size());
            auto [err, argBuf] = memStore->GetBuffer(argId, NO_TIMEOUT);
            if (err.Code() != ErrorCode::ERR_OK || argBuf == nullptr) {
                YRLOG_ERROR("Get arg {} from DS err! Code {}, MCode {}, info {}.", argId, err.Code(), err.MCode(),
                            err.Msg());
                return false;
            }
            rawArg = std::make_shared<DataObject>(argId, argBuf);
        } else {
            auto argBuf =
                std::make_shared<ReadOnlyNativeBuffer>(request.args(i).value().data(), request.args(i).value().size());
            rawArg = std::make_shared<DataObject>("", argBuf);
        }
        rawArgs.emplace_back(rawArg);
    }
    return true;
}

InvokeAdaptor::InvokeAdaptor(std::shared_ptr<LibruntimeConfig> config,
                             std::shared_ptr<DependencyResolver> dependencyResolver,
                             std::shared_ptr<FSClient> &fsClient, std::shared_ptr<MemoryStore> memStore,
                             std::shared_ptr<RuntimeContext> rtCtx, FinalizeCallback cb,
                             std::shared_ptr<WaitingObjectManager> waitManager,
                             std::shared_ptr<InvokeOrderManager> invokeOrderMgr,
                             std::shared_ptr<ClientsManager> clientsMgr, std::shared_ptr<MetricsAdaptor> metricsAdaptor)
    : dependencyResolver(dependencyResolver),
      runtimeContext(rtCtx),
      finalizeCb_(cb),
      invokeOrderMgr(invokeOrderMgr),
      clientsMgr(clientsMgr),
      metricsAdaptor(metricsAdaptor)
{
    this->fsClient = fsClient;
    this->librtConfig = config;
    this->memStore = memStore;
    this->requestManager = std::make_shared<RequestManager>();
    this->taskSubmitter =
        std::make_shared<TaskSubmitter>(config, memStore, fsClient, requestManager,
                                        std::bind(&InvokeAdaptor::KillAsync, this, std::placeholders::_1,
                                                  std::placeholders::_2, std::placeholders::_3));
    this->groupManager = std::make_shared<GroupManager>();
    this->waitingObjectManager = waitManager;
    this->functionMasterClient_ = std::make_shared<FMClient>();
    this->functionMasterClient_->SetSubscribeActiveMasterCb(std::bind(&InvokeAdaptor::SubscribeActiveMaster, this));
}

void InvokeAdaptor::SetRGroupManager(std::shared_ptr<ResourceGroupManager> rGroupManager)
{
    this->rGroupManager_ = rGroupManager;
}

void InvokeAdaptor::SetCallbackOfSetTenantId(SetTenantIdCallback cb)
{
    this->setTenantIdCb_ = cb;
}

void InvokeAdaptor::InitHandler(const std::shared_ptr<CallMessageSpec> &req)
{
    auto result = std::make_shared<CallResultMessageSpec>();
    auto &callResult = result->Mutable();
    callResult.set_requestid(req->Immutable().requestid());
    callResult.set_instanceid(req->Immutable().senderid());
    auto callResultCallback = [](const CallResultAck &resp) {
        if (resp.code() != common::ERR_NONE) {
            YRLOG_WARN("failed to send CallResult, code: {}, message: {}", resp.code(), resp.message());
        }
    };

    auto [code, msg] = PrepareCallExecutor(req->Mutable());
    if (code != common::ERR_NONE) {
        callResult.set_code(code);
        callResult.set_message(msg);
        fsClient->ReturnCallResult(result, true, callResultCallback);
        return;
    }
    libruntime::MetaData metaData;
    common::FunctionGroupRunningInfo runningInfo;
    bool isPosix = this->librtConfig->selfApiType == libruntime::ApiType::Posix;
    if (!ParseMetaData(req->Immutable(), isPosix, metaData) ||
        !(ParseFunctionGroupRunningInfo(req->Immutable(), isPosix, runningInfo))) {
        callResult.set_code(common::ERR_INNER_SYSTEM_ERROR);
        std::string errMsg = "Invalid request, requestid:" + req->Immutable().requestid() +
                             ", traceid:" + req->Immutable().traceid() + ", senderid:" + req->Immutable().senderid() +
                             ", function:" + req->Immutable().function();
        callResult.set_message(errMsg);
        fsClient->ReturnCallResult(result, true, callResultCallback);
        return;
    }
    if (metaData.functionmeta().isasync() && !fiberPool_) {
        this->fiberPool_ = std::make_shared<FiberPool>(FIBER_STACK_SIZE,
                                                       YR::Libruntime::Config::Instance().YR_ASYNCIO_MAX_CONCURRENCY());
    }
    if (this->librtConfig->selfApiType != libruntime::ApiType::Posix) {
        auto res = InitCall(req->Immutable(), metaData);
        if (res.code() != common::ERR_NONE) {
            result->Mutable() = std::move(res);
            fsClient->ReturnCallResult(result, true, callResultCallback);
            return;
        }
        librtConfig->InitFunctionGroupRunningInfo(runningInfo);
    }
    librtConfig->funcMeta = metaData.functionmeta();
    librtConfig->funcMeta.set_needorder(librtConfig->needOrder);
    YRLOG_DEBUG("update instance function meta, req id is {}, value is {}", req->Immutable().requestid(),
                librtConfig->funcMeta.DebugString());
    this->execMgr->Handle(
        metaData.invocationmeta(),
        [this, req, metaData]() {
            std::vector<std::string> objectsInDs;
            auto res = Call(req->Immutable(), metaData, librtConfig->libruntimeOptions, objectsInDs);
            auto result = std::make_shared<CallResultMessageSpec>();
            result->Mutable() = std::move(res);
            fsClient->ReturnCallResult(result, true, [](const CallResultAck &resp) {
                if (resp.code() != common::ERR_NONE) {
                    YRLOG_WARN("failed to send CallResult, code: {}, message: {}", resp.code(), resp.message());
                }
            });
        },
        req->Immutable().requestid());
}

void InvokeAdaptor::CallHandler(const std::shared_ptr<CallMessageSpec> &req)
{
    auto result = std::make_shared<CallResultMessageSpec>();
    auto &callResult = result->Mutable();
    callResult.set_requestid(req->Immutable().requestid());
    callResult.set_instanceid(req->Immutable().senderid());
    auto callResultCallback = [](const CallResultAck &resp) {
        if (resp.code() != common::ERR_NONE) {
            YRLOG_WARN("failed to send CallResult, code: {}, message: {}", resp.code(), resp.message());
        }
    };
    if (!this->execMgr) {
        auto [code, msg] = PrepareCallExecutor(req->Mutable());
        if (code != common::ERR_NONE) {
            callResult.set_code(code);
            callResult.set_message(msg);
            fsClient->ReturnCallResult(result, true, callResultCallback);
            return;
        }
    }
    libruntime::MetaData metaData;
    bool isPosix = this->librtConfig->selfApiType == libruntime::ApiType::Posix;
    if (!ParseMetaData(req->Immutable(), isPosix, metaData)) {
        std::string errMsg = "Invalid request, requestid:" + req->Immutable().requestid() +
                             ", traceid:" + req->Immutable().traceid() + ", senderid:" + req->Immutable().senderid() +
                             ", function:" + req->Immutable().function();
        callResult.set_message(errMsg);
        callResult.set_code(common::ERR_INNER_SYSTEM_ERROR);
        fsClient->ReturnCallResult(result, true, callResultCallback);
        return;
    }
    if (metaData.functionmeta().isasync() && !fiberPool_) {
        this->fiberPool_ = std::make_shared<FiberPool>(FIBER_STACK_SIZE,
                                                       YR::Libruntime::Config::Instance().YR_ASYNCIO_MAX_CONCURRENCY());
    }
    if (metaData.invoketype() == libruntime::InvokeType::GetNamedInstanceMeta) {
        std::shared_ptr<CallResultMessageSpec> result = std::make_shared<CallResultMessageSpec>();
        auto &callResult = result->Mutable();
        callResult.set_requestid(req->Immutable().requestid());
        callResult.set_instanceid(req->Immutable().senderid());
        std::string serializeFuncMeta;
        librtConfig->funcMeta.SerializeToString(&serializeFuncMeta);
        common::SmallObject *smallObj = callResult.add_smallobjects();
        smallObj->set_id(req->Immutable().requestid());
        smallObj->set_value(serializeFuncMeta);
        this->fsClient->ReturnCallResult(result, false, callResultCallback);
        return;
    }
    this->execMgr->Handle(
        metaData.invocationmeta(),
        [this, req, metaData]() {
            std::function<void()> handler = [this, req, metaData]() {
                auto startTime = std::chrono::high_resolution_clock::now();
                std::vector<std::string> objectsInDs;
                auto res = Call(req->Immutable(), metaData, librtConfig->libruntimeOptions, objectsInDs);
                auto endTime = std::chrono::high_resolution_clock::now();
                auto durationCast = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
                YRLOG_INFO("funcname: {}, call elapsed time: {}ms, requestid: {}, traceid: {}",
                           metaData.functionmeta().functionname(), durationCast, req->Immutable().requestid(),
                           req->Immutable().traceid());
                ReportMetrics(req->Immutable().requestid(), req->Immutable().traceid(), durationCast);
                auto result = std::make_shared<CallResultMessageSpec>();
                result->Mutable() = std::move(res);
                result->existObjInDs = !objectsInDs.empty();
                fsClient->ReturnCallResult(result, false, [this, objectsInDs](const CallResultAck &resp) {
                    if (resp.code() != common::ERR_NONE) {
                        YRLOG_WARN("failed to send CallResult, code: {}, message: {}", resp.code(), resp.message());
                    }
                    this->memStore->DecreGlobalReference(objectsInDs);
                    return;
                });
            };
            YRLOG_DEBUG("start exec user func, req id is {}, is async {}, func name is {}",
                        req->Immutable().requestid(), metaData.functionmeta().isasync(),
                        metaData.functionmeta().functionname());
            if (metaData.functionmeta().isasync() && !req->Immutable().iscreate()) {
                fiberPool_->Handle([handler]() mutable { handler(); });
            } else {
                handler();
            }
        },
        req->Immutable().requestid());
}

CheckpointResponse InvokeAdaptor::CheckpointHandler(const CheckpointRequest &req)
{
    CheckpointResponse resp;
    std::string checkpointId = req.checkpointid();
    if (!librtConfig->libruntimeOptions.checkpointCallback) {
        YRLOG_WARN("Failed to make a checkpoint of instance ID: {}, checkpointCallback has not been registered yet.",
                   checkpointId);
        return resp;
    }
    std::shared_ptr<Buffer> data;
    auto err = librtConfig->libruntimeOptions.checkpointCallback(checkpointId, data);
    if (!err.OK()) {
        YRLOG_ERROR("checkpoint exception: {}", err.Msg());
        resp.set_code(common::ERR_INNER_SYSTEM_ERROR);
        resp.set_message(err.Msg());
        return resp;
    }

    std::string *state = resp.mutable_state();
    auto errInfo = WriteDataToState(checkpointId, data, state);
    if (!errInfo.OK()) {
        YRLOG_ERROR("Failed to make checkpoint of instance({}), err: {}", checkpointId, errInfo.Msg());
        resp.set_code(common::ERR_INNER_SYSTEM_ERROR);
        resp.set_message(err.Msg());
        return resp;
    }
    resp.set_code(static_cast<common::ErrorCode>(err.Code()));
    return resp;
}

RecoverResponse InvokeAdaptor::RecoverHandler(const RecoverRequest &req)
{
    PrepareCallExecutor(req);
    RecoverResponse resp;
    std::string instanceId = Config::Instance().INSTANCE_ID();
    if (!librtConfig->libruntimeOptions.recoverCallback) {
        YRLOG_WARN("Failed to recover instance({}), recoverCallback has not been registered yet.", instanceId);
        return resp;
    }
    auto state = req.state();
    std::shared_ptr<Buffer> buf;
    auto errInfo = ReadDataFromState(instanceId, state, buf);
    if (!errInfo.OK()) {
        auto outErrMsg = "Failed to recover instance({" + instanceId + "}), err: " + errInfo.Msg();
        YRLOG_ERROR(outErrMsg);
        resp.set_code(common::ERR_USER_FUNCTION_EXCEPTION);
        resp.set_message(outErrMsg);
        return resp;
    }
    if (Config::Instance().ENABLE_METRICS()) {
        InitMetricsAdaptor(librtConfig->enableMetrics);
    }
    common::FunctionGroupRunningInfo runningInfo;
    bool isPosix = this->librtConfig->selfApiType == libruntime::ApiType::Posix;
    if (!isPosix) {
        if (req.createoptions().find("FUNCTION_GROUP_RUNNING_INFO") != req.createoptions().end()) {
            if (!google::protobuf::util::JsonStringToMessage(req.createoptions().at("FUNCTION_GROUP_RUNNING_INFO"),
                                                             &runningInfo)
                     .ok()) {
                resp.set_code(common::ERR_INNER_SYSTEM_ERROR);
                auto outErrMsg =
                    "Failed to recover instance({" + instanceId + "}), parse function group running info failed";
                YRLOG_ERROR(outErrMsg);
                resp.set_message(outErrMsg);
                return resp;
            }
        }
        librtConfig->InitFunctionGroupRunningInfo(runningInfo);
    }
    std::vector<std::string> libPaths;
    if (librtConfig->loadPaths.size() == 0) {
        libPaths.push_back(Config::Instance().FUNCTION_LIB_PATH());
    } else {
        libPaths = librtConfig->loadPaths;
    }
    auto loadErr = librtConfig->libruntimeOptions.loadFunctionCallback(libPaths);
    if (!loadErr.OK()) {
        YRLOG_ERROR("Failed to recover instance({}), err: {}", instanceId, loadErr.Msg());
        resp.set_code(static_cast<common::ErrorCode>(loadErr.Code()));
        resp.set_message(loadErr.Msg());
        return resp;
    }

    auto err = librtConfig->libruntimeOptions.recoverCallback(buf);
    if (!err.OK()) {
        YRLOG_ERROR("Failed to recover instance({}), err: {}", instanceId, err.Msg());
        resp.set_code(static_cast<common::ErrorCode>(err.Code()));
        resp.set_message(err.Msg());
        return resp;
    }
    return resp;
}

std::pair<std::string, ErrorInfo> InvokeAdaptor::Init(RuntimeContext &runtimeContext,
                                                      std::shared_ptr<Security> security)
{
    FSIntfHandlers handlers;
    handlers.init = std::bind(&InvokeAdaptor::InitHandler, this, _1);
    handlers.call = std::bind(&InvokeAdaptor::CallHandler, this, _1);
    handlers.checkpoint = std::bind(&InvokeAdaptor::CheckpointHandler, this, _1);
    handlers.recover = std::bind(&InvokeAdaptor::RecoverHandler, this, _1);
    handlers.shutdown = std::bind(&InvokeAdaptor::ShutdownHandler, this, _1);
    handlers.signal = std::bind(&InvokeAdaptor::SignalHandler, this, _1);
    if (librtConfig->libruntimeOptions.healthCheckCallback) {
        handlers.heartbeat = std::bind(&InvokeAdaptor::HeartbeatHandler, this, _1);
    }
    this->librtConfig->enableServerMode = true;
    YRLOG_DEBUG("when start fsclient isDriver {}, enableServerMode {}", this->librtConfig->isDriver,
                this->librtConfig->enableServerMode);
    // If this process is pulled up by function system, server listening address is specified by runtime-manager;
    // If this process is driver, user specify function system address,
    // and driver will connect to funtion system to do discovery
    auto ipAddr = this->librtConfig->functionSystemRtServerIpAddr;
    int port = this->librtConfig->functionSystemRtServerPort;
    if (this->librtConfig->isDriver) {
        ipAddr = this->librtConfig->functionSystemIpAddr;
        port = this->librtConfig->functionSystemPort;
    }
    auto clientType = FSClient::ClientType::GRPC_SERVER;
    if (!librtConfig->inCluster) {
        clientType = FSClient::ClientType::GW_CLIENT;
    } else if (this->librtConfig->enableServerMode) {
        clientType = FSClient::ClientType::GRPC_CLIENT;
    }
    std::string instanceId;
    if (this->librtConfig->isDriver) {
        instanceId = this->librtConfig->instanceId;
    } else {
        instanceId = Config::Instance().INSTANCE_ID();
    }
    auto functionName = this->librtConfig->functionName;
    if (functionName.empty()) {
        functionName = Config::Instance().FUNCTION_NAME();
    }
    auto err = this->fsClient->Start(ipAddr, port, handlers, clientType, this->librtConfig->isDriver, security,
                                     clientsMgr, runtimeContext.GetJobId(), instanceId, this->librtConfig->runtimeId,
                                     functionName, std::bind(&InvokeAdaptor::SubscribeAll, this));
    if (err.OK()) {
        return std::make_pair(this->fsClient->GetServerVersion(), err);
    }
    return std::make_pair("", err);
}

CallResult InvokeAdaptor::Call(const CallRequest &req, const libruntime::MetaData &metaData,
                               const LibruntimeOptions &options, std::vector<std::string> &objectsInDs)
{
    CallResult callResult;
    callResult.set_requestid(req.requestid());
    callResult.set_instanceid(req.senderid());

    std::vector<std::shared_ptr<DataObject>> rawArgs;
    bool isPosix = this->librtConfig->selfApiType == libruntime::ApiType::Posix;
    bool returnByMsg = req.returnobjectids_size() == 0;
    if (setTenantIdCb_) {
        setTenantIdCb_();
    }
    bool ok = ParseRequest(req, rawArgs, memStore, isPosix);
    if (!ok) {
        callResult.set_code(common::ERR_NONE);
        callResult.set_message(ErrMsgMap.at(common::ERR_NONE));
        return callResult;
    }

    std::string genId;

    size_t returnIdsize = req.returnobjectids_size() == 0 ? 1 : req.returnobjectids_size();

    std::vector<std::shared_ptr<DataObject>> returnObjects(returnIdsize);
    for (int i = 0; i < req.returnobjectids_size(); i++) {
        returnObjects[i] = std::make_shared<DataObject>();
        returnObjects[i]->id = req.returnobjectids(i);
    }
    if (returnByMsg) {
        returnObjects[0] = std::make_shared<DataObject>();
        returnObjects[0]->id = "returnByMsg";
    }

    FunctionMeta functionMeta;
    functionMeta.appName = metaData.functionmeta().applicationname();
    functionMeta.moduleName = metaData.functionmeta().modulename();
    functionMeta.funcName = metaData.functionmeta().functionname();
    functionMeta.className = metaData.functionmeta().classname();
    functionMeta.codeId = metaData.functionmeta().codeid();
    functionMeta.initializerCodeId = metaData.functionmeta().initializercodeid();
    functionMeta.signature = metaData.functionmeta().signature();
    functionMeta.languageType = metaData.functionmeta().language();
    functionMeta.apiType = metaData.functionmeta().apitype();
    functionMeta.isGenerator = metaData.functionmeta().isgenerator();
    functionMeta.isAsync = metaData.functionmeta().isasync();
    if (functionMeta.apiType != libruntime::ApiType::Function) {
        returnObjects[0]->alwaysNative = true;
    }

    auto err = options.functionExecuteCallback(functionMeta, metaData.invoketype(), rawArgs, returnObjects);
    for (size_t i = 0; i < returnObjects.size(); i++) {
        if (returnObjects[i]->buffer != nullptr && returnObjects[i]->buffer->IsNative() &&
            returnObjects[i]->putDone == false) {
            auto smallObject = callResult.add_smallobjects();
            smallObject->set_id(returnObjects[i]->id);
            smallObject->set_value(returnObjects[i]->buffer->ImmutableData(), returnObjects[i]->buffer->GetSize());
        } else {
            objectsInDs.emplace_back(returnObjects[i]->id);
        }
    }

    if (!err.OK()) {
        callResult.set_code(static_cast<common::ErrorCode>(err.Code()));
        callResult.set_message(err.Msg());
        std::vector<YR::Libruntime::StackTraceInfo> infos = err.GetStackTraceInfos();
        YR::SetCallResultWithStackTraceInfo(infos, callResult);

        YRLOG_DEBUG("set stackTraceInfo to CallResult {}, size after set:{}", callResult.DebugString(),
                    err.GetStackTraceInfos().size());
        return callResult;
    }

    if (returnByMsg) {
        callResult.set_code(static_cast<common::ErrorCode>(err.Code()));
        std::string ret = "";
        if (returnObjects[0]->data != nullptr && returnObjects[0]->data->GetSize() != 0) {
            ret = std::string(reinterpret_cast<char *>(returnObjects[0]->data->MutableData()),
                              returnObjects[0]->data->GetSize());
        }
        callResult.set_message(ret);
        return callResult;
    }

    callResult.set_code(static_cast<common::ErrorCode>(err.Code()));
    callResult.set_message(err.Msg());
    return callResult;
}

CallResult InvokeAdaptor::InitCall(const CallRequest &req, const libruntime::MetaData &metaData)
{
    CallResult callResult;
    callResult.set_requestid(req.requestid());
    callResult.set_instanceid(req.senderid());
    std::vector<std::string> libPaths;
    if (metaData.config().codepaths_size() == 0) {
        libPaths.push_back(Config::Instance().FUNCTION_LIB_PATH());
    } else {
        libPaths.resize(metaData.config().codepaths_size());
        for (int i = 0; i < metaData.config().codepaths_size(); i++) {
            libPaths[i] = metaData.config().codepaths(i);
        }
    }
    auto err = librtConfig->libruntimeOptions.loadFunctionCallback(libPaths);
    if (!err.OK()) {
        callResult.set_code(static_cast<common::ErrorCode>(err.Code()));
        callResult.set_message(err.Msg());
    } else {
        librtConfig->InitConfig(metaData.config());
        taskSubmitter->UpdateConfig();
        if (Config::Instance().ENABLE_METRICS()) {
            InitMetricsAdaptor(metaData.config().enablemetrics());
            return callResult;
        }
    }
    return callResult;
}

template <typename RequestType>
std::pair<common::ErrorCode, std::string> InvokeAdaptor::PrepareCallExecutor(const RequestType &req)
{
    size_t concurrency = 1;
    if (req.createoptions().find(CONCURRENT_NUM) != req.createoptions().end()) {
        size_t recvConcurrency = static_cast<unsigned short>(std::stoull(req.createoptions().at(CONCURRENT_NUM)));
        if (recvConcurrency < MIN_CONCURRENCY || recvConcurrency > MAX_CONCURRENCY) {
            std::string err = std::string("Invalid concurrency:") + std::to_string(recvConcurrency) +
                              ", it should be range from " + std::to_string(MIN_CONCURRENCY) + " to " +
                              std::to_string(MAX_CONCURRENCY);
            YRLOG_ERROR(err);
            return std::make_pair(common::ERR_PARAM_INVALID, err);
        }
        concurrency = recvConcurrency;
    }

    this->librtConfig->needOrder = req.createoptions().find(NEED_ORDER) != req.createoptions().end();
    if (this->librtConfig->needOrder && concurrency > 1) {
        std::string err("Cannot set need order and concurrency > 1 at same time!");
        YRLOG_ERROR("{}, concurrency: {}", err, concurrency);
        return std::make_pair(common::ERR_PARAM_INVALID, err);
    }

    YRLOG_INFO("Call executor pool size: {}, need order: {}", concurrency, this->librtConfig->needOrder);
    if (this->librtConfig->needOrder) {
        this->execMgr = std::make_shared<OrderedExecutionManager>(concurrency, librtConfig->funcExecSubmitHook);
    } else {
        this->execMgr = std::make_shared<GeneralExecutionManager>(concurrency, librtConfig->funcExecSubmitHook);
    }
    auto err = this->execMgr->DoInit(concurrency);
    return std::make_pair(static_cast<common::ErrorCode>(err.Code()), err.Msg());
}

SignalResponse InvokeAdaptor::SignalHandler(const SignalRequest &req)
{
    YRLOG_DEBUG("receive signal {}", req.signal());
    SignalResponse resp;
    switch (req.signal()) {
        case libruntime::Signal::Cancel: {
            auto objIds = requestManager->GetObjIds();
            Cancel(objIds, true, true);
            Exit();
            break;
        }
        case libruntime::Signal::ErasePendingThread: {
            if (execMgr && execMgr->isMultipleConcurrency()) {
                YRLOG_DEBUG("recive erase pending signal req, pay load is {}", req.payload());
                execMgr->ErasePendingThread(req.payload());
            }
            break;
        }
        case libruntime::Signal::Update: {
            const std::string &payload = req.payload();
            NotificationPayload notifyPayload;
            notifyPayload.ParseFromString(payload);
            if (notifyPayload.has_instancetermination()) {
                this->RemoveInsMetaInfo(notifyPayload.instancetermination().instanceid());
            } else if (notifyPayload.has_functionmasterevent()) {
                this->functionMasterClient_->UpdateActiveMaster(notifyPayload.functionmasterevent().address());
            }
            break;
        }
        case libruntime::Signal::UpdateManager: {
            resp = ExecSignalCallback(req);
            break;
        }
        case libruntime::Signal::QueryDsAddress: {
            resp.set_message(YR::Libruntime::Config::Instance().DATASYSTEM_ADDR());
            break;
        }
        case libruntime::Signal::Accelerate: {
            if (accelerateRunFlag_) {
                break;
            }
            accelerateRunFlag_ = true;
            auto payload = req.payload();
            AccelerateMsgQueueHandle outputHandle;
            auto err = this->librtConfig->libruntimeOptions.accelerateCallback(
                AccelerateMsgQueueHandle::FromJson(payload), outputHandle);
            if (!err.OK()) {
                resp.set_code(static_cast<common::ErrorCode>(err.Code()));
                YRLOG_WARN("execute accelerate callback err code: {}, msg: {}", err.Code(), err.Msg());
                resp.set_message(err.Msg());
            } else {
                resp.set_message(outputHandle.ToJson());
            }
            break;
        }
        case libruntime::Signal::GetInstance: {
            std::string serializedMeta;
            if (!this->librtConfig->funcMeta.SerializeToString(&serializedMeta)) {
                resp.set_code(::common::ErrorCode::ERR_INNER_SYSTEM_ERROR);
                resp.set_message("Failed to serialize FunctionMeta");
            } else {
                resp.set_code(::common::ErrorCode::ERR_NONE);
                resp.set_message(serializedMeta);
            }
            break;
        }
        default: {
            resp = ExecSignalCallback(req);
            break;
        }
    }
    return resp;
}

HeartbeatResponse InvokeAdaptor::HeartbeatHandler(const HeartbeatRequest &req)
{
    HeartbeatResponse resp;
    if (librtConfig->libruntimeOptions.healthCheckCallback) {
        auto err = librtConfig->libruntimeOptions.healthCheckCallback();
        if (err.Code() == ErrorCode::ERR_HEALTH_CHECK_HEALTHY) {
            resp.set_code(common::HealthCheckCode::HEALTHY);
        } else if (err.Code() == ErrorCode::ERR_HEALTH_CHECK_FAILED) {
            resp.set_code(common::HealthCheckCode::HEALTH_CHECK_FAILED);
        } else if (err.Code() == ErrorCode::ERR_HEALTH_CHECK_SUBHEALTH) {
            resp.set_code(common::HealthCheckCode::SUB_HEALTH);
        }
    }
    return resp;
}

SignalResponse InvokeAdaptor::ExecSignalCallback(const SignalRequest &req)
{
    SignalResponse resp;

    if (!librtConfig->libruntimeOptions.signalCallback) {
        return resp;
    }

    auto payload =
        std::make_shared<ReadOnlyNativeBuffer>(static_cast<const void *>(req.payload().data()), req.payload().size());
    auto err = librtConfig->libruntimeOptions.signalCallback(req.signal(), payload);
    if (!err.OK()) {
        resp.set_code(static_cast<common::ErrorCode>(err.Code()));
        resp.set_message(err.Msg());
    }
    return resp;
}

ShutdownResponse InvokeAdaptor::ShutdownHandler(const ShutdownRequest &req)
{
    ShutdownResponse resp;
    auto err = ExecShutdownCallback(req.graceperiodsecond());
    if (!err.OK()) {
        resp.set_code(static_cast<common::ErrorCode>(err.Code()));
        resp.set_message(err.Msg());
    }

    return resp;
}

ErrorInfo InvokeAdaptor::ExecShutdownCallback(uint64_t gracePeriodSec)
{
    YRLOG_DEBUG("graceful shutdown period is {}", gracePeriodSec);

    auto notification = std::make_shared<utility::NotificationUtility>();
    auto thread = std::thread(&InvokeAdaptor::ExecUserShutdownCallback, this, gracePeriodSec, notification);
    thread.detach();
    // The shutdown callback consists of two parts, namely the UserShutdownCallback and the libruntime callback.
    // When executing the shutdown callback, both the UserShutdownCallback and the libruntime callback are executed
    // simultaneously, and at the end, wait for the UserShutdowncallback to complete before exec finalizeCb_.
    auto remainTimeSec = fsClient->WaitRequestEmpty(gracePeriodSec);
    ErrorInfo err;
    if (remainTimeSec > 0) {
        err = notification->WaitForNotificationWithTimeout(
            absl::Seconds(remainTimeSec), ErrorInfo(ErrorCode::ERR_INNER_SYSTEM_ERROR, ModuleCode::RUNTIME,
                                                    "Execute user shutdown callback timeout"));
    } else {
        err.SetErrCodeAndMsg(ErrorCode::ERR_INNER_SYSTEM_ERROR, ModuleCode::RUNTIME,
                             "Execute user shutdown callback timeout");
    }
    auto instanceIds = taskSubmitter->GetInstanceIds();
    auto creatingInsIds = taskSubmitter->GetCreatingInsIds();
    instanceIds.insert(instanceIds.end(), creatingInsIds.begin(), creatingInsIds.end());
    for (const auto &instanceId : instanceIds) {
        auto killErr = this->Kill(instanceId, "", libruntime::Signal::KillInstance);
        if (killErr.OK()) {
            YRLOG_DEBUG("succeed to kill instance {}", instanceId);
            continue;
        }
        YRLOG_INFO("Failed to kill instance {}, msg: {}", instanceId, killErr.Msg());
    }

    if (finalizeCb_) {
        finalizeCb_();
    }

    return err;
}

void InvokeAdaptor::ExecUserShutdownCallback(uint64_t gracePeriodSec,
                                             const std::shared_ptr<utility::NotificationUtility> &notification)
{
    ErrorInfo err;
    if (librtConfig->libruntimeOptions.shutdownCallback) {
        YRLOG_DEBUG("Start to call user shutdown callback, graceful shutdown time: {}", gracePeriodSec);
        err = librtConfig->libruntimeOptions.shutdownCallback(gracePeriodSec);
        if (!err.OK()) {
            YRLOG_ERROR("Failed to call user shutdown callback, error: {}, error code: {}, error message: {}",
                        err.Msg(), err.Code(), static_cast<common::ErrorCode>(err.Code()));
        } else {
            YRLOG_DEBUG("Succeeded to call user shutdown callback");
        }
    } else {
        YRLOG_DEBUG("No user shutdown callback is found");
    }
    notification->Notify(err);
}

void InvokeAdaptor::CreateInstance(std::shared_ptr<InvokeSpec> spec)
{
    if (InstanceRangeEnabled(spec->opts.instanceRange)) {
        YRLOG_DEBUG("Begin to create instances by range scheduling, request ID: {}, group name is {}", spec->requestId,
                    spec->opts.groupName);
        this->groupManager->AddSpec(spec);
        auto err = RangeCreate(spec->opts.groupName, spec->opts.instanceRange);
        if (!err.OK()) {
            ProcessErr(spec, err);
        }
        return;
    }
    if (FunctionGroupEnabled(spec->opts.functionGroupOpts)) {
        YRLOG_DEBUG("Begin to create instances by function group scheduling, request ID: {}, group name is {}",
                    spec->requestId, spec->opts.groupName);
        this->groupManager->AddSpec(spec);
        auto err = CreateFunctionGroup(spec, nullptr);
        if (!err.OK()) {
            this->memStore->SetError(spec->returnIds[0].id, err);
        }
        return;
    }
    if (!spec->opts.groupName.empty()) {
        YRLOG_DEBUG("Begin to add group into group manager, request ID: {}, group name is {}", spec->requestId,
                    spec->opts.groupName);
        this->groupManager->AddSpec(spec);
        return;
    }
    YRLOG_DEBUG("Begin to create instance, request ID: {}", spec->requestId);
    requestManager->PushRequest(spec);
    auto weak_this = weak_from_this();
    auto rspHandler = [weak_this, spec](const CreateResponse &rsp) -> void {
        if (auto this_ptr = weak_this.lock(); this_ptr) {
            this_ptr->CreateResponseHandler(spec, rsp);
        }
    };
    fsClient->CreateAsync(spec->requestCreate, rspHandler, std::bind(&InvokeAdaptor::CreateNotifyHandler, this, _1));
    YRLOG_DEBUG("Create request has been sent, req id is {}, Details: {}", spec->requestId,
                spec->requestCreate.DebugString());
}

void InvokeAdaptor::RetryCreateInstance(std::shared_ptr<InvokeSpec> spec, bool isConsumeRetryTime)
{
    spec->IncrementRequestID(spec->requestCreate);
    if (isConsumeRetryTime) {
        spec->ConsumeRetryTime();
        YRLOG_DEBUG("consumed create retry time to {}", spec->opts.retryTimes);
    }
    auto weak_this = weak_from_this();
    auto rspHandler = [weak_this, spec](const CreateResponse &rsp) -> void {
        if (auto this_ptr = weak_this.lock(); this_ptr) {
            this_ptr->CreateResponseHandler(spec, rsp);
        }
    };
    fsClient->CreateAsync(spec->requestCreate, rspHandler, std::bind(&InvokeAdaptor::CreateNotifyHandler, this, _1));
}

void InvokeAdaptor::InvokeInstanceFunction(std::shared_ptr<InvokeSpec> spec)
{
    if (!spec->opts.groupName.empty()) {
        auto isReady = this->groupManager->IsInsReady(spec->opts.groupName);
        if (!isReady) {
            YRLOG_WARN("instance: {} of reqid: {} belongs group: {} is not ready, can not execute invoke req",
                       spec->invokeInstanceId, spec->requestId, spec->opts.groupName);
            return;
        }
    }
    requestManager->PushRequest(spec);
    fsClient->InvokeAsync(spec->requestInvoke, std::bind(&InvokeAdaptor::InvokeNotifyHandler, this, _1, _2),
                          spec->opts.timeout == 0 ? FLAG_OF_REQUEST_NO_TIMEOUT : spec->opts.timeout);
}

void InvokeAdaptor::SubmitFunction(std::shared_ptr<InvokeSpec> spec)
{
    if (FunctionGroupEnabled(spec->opts.functionGroupOpts)) {
        YRLOG_DEBUG("Begin to create instances by function group scheduling, request ID: {}, group name is {}",
                    spec->requestId, spec->opts.groupName);
        auto createSpec = BuildCreateSpec(spec);
        this->groupManager->AddSpec(createSpec);
        auto weakPtr = weak_from_this();
        auto err = CreateFunctionGroup(createSpec, spec);
        if (!err.OK()) {
            for (const auto &returnId : spec->returnIds) {
                this->memStore->SetError(returnId.id, err);
            }
            return;
        }
        return;
    }
    taskSubmitter->SubmitFunction(spec);
}

bool InvokeAdaptor::IsIdValid(const std::string &id)
{
    return !id.empty();
}

void InvokeAdaptor::CreateInstanceRaw(std::shared_ptr<Buffer> reqRaw, RawCallback cb)
{
    CreateRequest req;
    req.ParseFromString(std::string(static_cast<char *>(reqRaw->MutableData()), reqRaw->GetSize()));
    YRLOG_DEBUG("start create instance raw request, req id is {}", req.requestid());
    if (!IsIdValid(req.requestid())) {
        YRLOG_ERROR("create raw req id: {} is invalid", req.requestid());
        cb(ErrorInfo(ErrorCode::ERR_PARAM_INVALID, ModuleCode::RUNTIME, "invalid req param"),
           std::make_shared<NativeBuffer>(0));
        return;
    }
    std::shared_ptr<std::string> insId = std::make_shared<std::string>();
    this->fsClient->CreateAsync(
        req,
        [insId, cb](const CreateResponse &resp) -> void {
            YRLOG_DEBUG("recieve create raw response, code is {}, instance id is {}, msg is {}", resp.code(),
                        resp.instanceid(), resp.message());
            if (resp.code() != common::ERR_NONE) {
                YRLOG_ERROR("start handle failed raw create response, code is {}, instance id is {}, msg is {}",
                            resp.code(), resp.instanceid(), resp.message());
                NotifyRequest notify;
                notify.set_code(resp.code());
                notify.set_message(resp.message());
                notify.set_instanceid(resp.instanceid());
                size_t size = notify.ByteSizeLong();
                auto respRaw = std::make_shared<NativeBuffer>(size);
                notify.SerializeToArray(respRaw->MutableData(), size);
                cb(ErrorInfo(), respRaw);
                return;
            }
            *insId = resp.instanceid();
        },
        [insId, cb](const NotifyRequest &req) -> void {
            YRLOG_DEBUG("recieve create raw notify, code is {}, req id is {}, msg is {}, instance id is {}", req.code(),
                        req.requestid(), req.message(), *insId);
            NotifyRequest notify;
            notify.set_code(req.code());
            notify.set_message(req.message());
            notify.set_instanceid(*insId);
            if (req.has_runtimeinfo()) {
                notify.mutable_runtimeinfo()->set_route(req.runtimeinfo().route());
            }
            size_t size = notify.ByteSizeLong();
            auto respRaw = std::make_shared<NativeBuffer>(size);
            notify.SerializeToArray(respRaw->MutableData(), size);
            cb(ErrorInfo(), respRaw);
        });
}

void InvokeAdaptor::InvokeByInstanceIdRaw(std::shared_ptr<Buffer> reqRaw, RawCallback cb)
{
    InvokeRequest req;
    req.ParseFromString(std::string(static_cast<char *>(reqRaw->MutableData()), reqRaw->GetSize()));
    if (!IsIdValid(req.requestid())) {
        YRLOG_ERROR("invoke raw req id: {} is invalid", req.requestid());
        cb(ErrorInfo(ErrorCode::ERR_PARAM_INVALID, ModuleCode::RUNTIME, "invalid req param"),
           std::make_shared<NativeBuffer>(0));
        return;
    }
    auto messageSpec = std::make_shared<InvokeMessageSpec>(std::move(req));
    this->fsClient->InvokeAsync(messageSpec, [this, cb](const NotifyRequest &req, const ErrorInfo &err) -> void {
        YRLOG_DEBUG("recieve invoke raw notify, code is {}, req id is {}, msg is {}", req.code(), req.requestid(),
                    req.message());
        size_t size = req.ByteSizeLong();
        auto respRaw = std::make_shared<NativeBuffer>(size);
        req.SerializeToArray(respRaw->MutableData(), size);
        cb(ErrorInfo(), respRaw);
    });
}

void InvokeAdaptor::KillRaw(std::shared_ptr<Buffer> reqRaw, RawCallback cb)
{
    KillRequest req;
    req.ParseFromString(std::string(static_cast<char *>(reqRaw->MutableData()), reqRaw->GetSize()));
    this->fsClient->KillAsync(req, [this, cb](const KillResponse &resp) -> void {
        YRLOG_DEBUG("recieve kill raw response, code is {}", resp.code());
        size_t size = resp.ByteSizeLong();
        auto respRaw = std::make_shared<NativeBuffer>(size);
        resp.SerializeToArray(respRaw->MutableData(), size);
        cb(ErrorInfo(), respRaw);
    });
}

void InvokeAdaptor::RetryInvokeInstanceFunction(std::shared_ptr<InvokeSpec> spec, bool isConsumeRetryTime)
{
    spec->IncrementRequestID(spec->requestInvoke->Mutable());
    if (isConsumeRetryTime) {
        spec->ConsumeRetryTime();
        YRLOG_DEBUG("consumed invoke retry time to {}", spec->opts.retryTimes);
    }
    fsClient->InvokeAsync(spec->requestInvoke, std::bind(&InvokeAdaptor::InvokeNotifyHandler, this, _1, _2));
}

void InvokeAdaptor::CreateResponseHandler(std::shared_ptr<InvokeSpec> spec, const CreateResponse &resp)
{
    auto instanceId = resp.instanceid();
    if (resp.code() == common::ERR_NONE) {
        YRLOG_DEBUG("start handle success create response, req id is {}, instance id is {}", spec->requestId,
                    instanceId);
        memStore->SetInstanceId(spec->returnIds[0].id, instanceId);
    } else if (resp.code() == common::ERR_INSTANCE_DUPLICATED) {
        YRLOG_WARN("start handle duplicated create response, req id is {}, instance id is {}", spec->requestId,
                   instanceId);
        invokeOrderMgr->NotifyInvokeSuccess(spec);
        memStore->SetInstanceId(spec->returnIds[0].id, instanceId);
        memStore->SetReady(spec->returnIds[0].id);
    } else {
        bool isConsumeRetryTime = false;
        if (!NeedRetry(static_cast<ErrorCode>(resp.code()), spec, isConsumeRetryTime)) {
            YRLOG_ERROR("create instance failed, start set error, req id is {}, instance id is {}", spec->requestId,
                        instanceId);
            memStore->SetInstanceId(spec->returnIds[0].id, instanceId);
            ProcessErr(spec, ErrorInfo(static_cast<ErrorCode>(resp.code()), ModuleCode::CORE, resp.message(), true));
        } else {
            YRLOG_ERROR(
                "create instance failed, need retry, req id is {}, instance id is {}, seq is {}, complete req id: {}",
                spec->requestId, instanceId, spec->seq, spec->requestInvoke->Immutable().requestid());
            RetryCreateInstance(spec, isConsumeRetryTime);
        }
    }
}

void InvokeAdaptor::CreateNotifyHandler(const NotifyRequest &req)
{
    if (!isRunning) {
        return;
    }
    auto [rawRequestId, seq] = YR::utility::IDGenerator::DecodeRawRequestId(req.requestid());
    std::shared_ptr<InvokeSpec> spec = requestManager->GetRequest(rawRequestId);
    if (spec == nullptr) {
        YRLOG_WARN("Invoke spec not found, request ID: {}", req.requestid());
        return;
    }
    if (spec->IsStaleDuplicateNotify(seq)) {
        return;
    }
    if (req.code() != common::ERR_NONE) {
        bool isConsumeRetryTime = false;
        if (!NeedRetry(static_cast<ErrorCode>(req.code()), spec, isConsumeRetryTime)) {
            YRLOG_ERROR("Failed to create instance, request ID: {}, code: {}, message: {}", req.requestid(), req.code(),
                        req.message());
            auto isCreate = spec->invokeType == libruntime::InvokeType::CreateInstanceStateless ||
                            spec->invokeType == libruntime::InvokeType::CreateInstance;
            std::vector<YR::Libruntime::StackTraceInfo> stackTraceInfos = GetStackTraceInfos(req);
            ProcessErr(spec, ErrorInfo(static_cast<ErrorCode>(req.code()), ModuleCode::CORE, req.message(), isCreate,
                                       stackTraceInfos));
        } else {
            YRLOG_ERROR("Failed to create instance, need retry, request ID: {}, code: {}, message: {}", req.requestid(),
                        req.code(), req.message());
            RetryCreateInstance(spec, isConsumeRetryTime);
            return;
        }
    } else {
        YRLOG_DEBUG("Succeed to create instance, request ID: {}, instance ID: {}", req.requestid(), spec->instanceId);
        invokeOrderMgr->NotifyInvokeSuccess(spec);
        if (req.has_runtimeinfo() && !req.runtimeinfo().route().empty()) {
            memStore->SetInstanceRoute(spec->returnIds[0].id, req.runtimeinfo().route());
        }
        memStore->SetReady(spec->returnIds[0].id);
        if (spec->functionMeta.apiType != libruntime::ApiType::Posix) {
            if (auto insId = spec->GetNamedInstanceId(); !insId.empty()) {
                auto meta = convertFuncMetaToProto(spec);
                this->UpdateAndSubcribeInsStatus(insId, meta);
            }
        }
    }
    auto ids = memStore->UnbindObjRefInReq(rawRequestId);
    auto errorInfo = memStore->DecreGlobalReference(ids);
    if (!errorInfo.OK()) {
        YRLOG_WARN("failed to decrease by requestid {}. Code: {}, MCode: {}, Msg: {}", req.requestid(),
                   errorInfo.Code(), errorInfo.MCode(), errorInfo.Msg());
    }

    (void)requestManager->RemoveRequest(rawRequestId);
}

void InvokeAdaptor::HandleReturnedObject(const NotifyRequest &req, const std::shared_ptr<InvokeSpec> &spec)
{
    std::vector<std::string> dsObjs;
    size_t curPos = 0;
    int smallObjSize = req.smallobjects_size();
    if (smallObjSize <= 0) {
        for (size_t j = 0; j < spec->returnIds.size(); j++) {
            dsObjs.emplace_back(spec->returnIds[j].id);
        }
    } else {
        for (int i = 0; i < smallObjSize; i++) {
            // fetch small object from protobuf, store to memStore
            auto &smallObj = req.smallobjects(i);
            const std::string &bufStr = smallObj.value();
            std::shared_ptr<Buffer> buf = std::make_shared<NativeBuffer>(bufStr.size());
            buf->MemoryCopy(bufStr.data(), bufStr.size());
            memStore->Put(buf, smallObj.id(), {}, false);

            for (size_t j = curPos; j < spec->returnIds.size(); j++) {
                if (spec->returnIds[j].id == smallObj.id()) {
                    curPos++;
                    break;
                }
                dsObjs.emplace_back(spec->returnIds[j].id);
                curPos++;
            }
        }
    }
    if (librtConfig->inCluster) {
        auto err = memStore->IncreDSGlobalReference(dsObjs);
        if (!err.OK()) {
            YRLOG_WARN("failed to increase obj ref [{},...] by requestid {}, Code: {}, Msg: {}", dsObjs[0],
                       req.requestid(), err.Code(), err.Msg());
        }
    }
    memStore->SetReady(spec->returnIds);
}

// handle actor task
void InvokeAdaptor::InvokeNotifyHandler(const NotifyRequest &req, const ErrorInfo &err)
{
    if (!isRunning) {
        return;
    }
    auto [rawRequestId, seq] = YR::utility::IDGenerator::DecodeRawRequestId(req.requestid());
    YRLOG_DEBUG("start handle instance function invoke notify, req id is {}", req.requestid());
    std::shared_ptr<InvokeSpec> spec = requestManager->GetRequest(rawRequestId);
    if (spec == nullptr) {
        YRLOG_WARN("Invoke spec not found, request ID: {}", req.requestid());
        return;
    }
    if (spec->IsStaleDuplicateNotify(seq)) {
        return;
    }
    if (req.code() != common::ERR_NONE) {
        bool isConsumeRetryTime = false;
        if (!NeedRetry(static_cast<ErrorCode>(req.code()), spec, isConsumeRetryTime)) {
            YRLOG_ERROR(
                "instance invoke failed, do not retry, request id: {}, instance id: {}, return id: {}, err msg is: {}, "
                "is invoke timeout: {}, invoke instance id is: {}",
                req.requestid(), spec->invokeInstanceId, spec->returnIds[0].id, req.message(), err.IsTimeout(),
                spec->invokeInstanceId);
            auto isCreate = spec->invokeType == libruntime::InvokeType::CreateInstanceStateless ||
                            spec->invokeType == libruntime::InvokeType::CreateInstance;
            std::vector<YR::Libruntime::StackTraceInfo> stackTraceInfos = GetStackTraceInfos(req);
            ProcessErr(spec, ErrorInfo(static_cast<ErrorCode>(req.code()), ModuleCode::CORE, req.message(), isCreate,
                                       stackTraceInfos));
            if (err.IsTimeout() && !spec->invokeInstanceId.empty()) {
                // if timeout, then send cancel req to runtime for erase pending thread
                (void)this->KillAsync(spec->invokeInstanceId, req.requestid(), libruntime::Signal::ErasePendingThread);
            }
        } else {
            YRLOG_ERROR(
                "instance invoke failed and retry, request id: {}, instance id: {}, return id: {}, seq: {}, complete "
                "request id: {}",
                spec->requestId, spec->invokeInstanceId, spec->returnIds[0].id, spec->seq,
                spec->requestInvoke->Immutable().requestid());
            RetryInvokeInstanceFunction(spec, isConsumeRetryTime);
            return;
        }
    } else {
        invokeOrderMgr->NotifyInvokeSuccess(spec);
        HandleReturnedObject(req, spec);
    }
    auto ids = memStore->UnbindObjRefInReq(rawRequestId);
    auto errorInfo = memStore->DecreGlobalReference(ids);
    if (!errorInfo.OK()) {
        YRLOG_WARN("failed to decrease by requestid {}. Code: {}, MCode: {}, Msg: {}", req.requestid(),
                   errorInfo.Code(), errorInfo.MCode(), errorInfo.Msg());
    }
    (void)requestManager->RemoveRequest(rawRequestId);
}

void InvokeAdaptor::ProcessErr(const std::shared_ptr<InvokeSpec> &spec, const ErrorInfo &errInfo)
{
    memStore->SetError(spec->returnIds, errInfo);
}

bool InvokeAdaptor::NeedRetry(ErrorCode code, const std::shared_ptr<const InvokeSpec> spec, bool &isConsumeRetryTime)
{
    if (spec->opts.retryTimes <= 0) {
        isConsumeRetryTime = false;
        return false;
    }
    switch (spec->invokeType) {
        case libruntime::InvokeType::InvokeFunction: {
            static const std::unordered_set<ErrorCode> codesWorthRetry = {
                ErrorCode::ERR_REQUEST_BETWEEN_RUNTIME_BUS, ErrorCode::ERR_INNER_COMMUNICATION,
                ErrorCode::ERR_SHARED_MEMORY_LIMITED,       ErrorCode::ERR_OPERATE_DISK_FAILED,
                ErrorCode::ERR_INSUFFICIENT_DISK_SPACE,
            };
            isConsumeRetryTime = (codesWorthRetry.find(code) != codesWorthRetry.end());
            return isConsumeRetryTime;
        }
        case libruntime::InvokeType::CreateInstance: {
            static const std::unordered_set<ErrorCode> codesWorthRetry = {
                ErrorCode::ERR_RESOURCE_NOT_ENOUGH,
                ErrorCode::ERR_INNER_COMMUNICATION,
                ErrorCode::ERR_REQUEST_BETWEEN_RUNTIME_BUS,
                ErrorCode::ERR_INSUFFICIENT_DISK_SPACE,
            };
            isConsumeRetryTime = (codesWorthRetry.find(code) != codesWorthRetry.end());
            return isConsumeRetryTime;
        }
        default:
            break;
    }
    isConsumeRetryTime = false;
    return false;
}

ErrorInfo InvokeAdaptor::Cancel(const std::vector<std::string> &objids, bool isForce, bool isRecursive)
{
    auto f = std::bind(&InvokeAdaptor::Kill, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    return taskSubmitter->CancelStatelessRequest(objids, f, isForce, isRecursive);
}

void InvokeAdaptor::Exit(void)
{
    absl::Notification notification;
    ExitRequest req;
    fsClient->ExitAsync(req, [&notification](const ExitResponse &resp) { notification.Notify(); });
    // default to wait 30s
    notification.WaitForNotificationWithTimeout(absl::Seconds(30));
}

ErrorInfo InvokeAdaptor::SaveState(const std::shared_ptr<Buffer> data, const int &timeout)
{
    std::string instanceId = Config::Instance().INSTANCE_ID();
    YRLOG_DEBUG("Begin to save state of instance({})", instanceId);

    if (timeout <= 0 && timeout != -1) {
        return ErrorInfo(ErrorCode::ERR_PARAM_INVALID, ModuleCode::RUNTIME, "Timeout must be positive or equal to -1");
    }

    StateSaveRequest req;
    std::string *state = req.mutable_state();
    auto errInfo = WriteDataToState(instanceId, data, state);
    if (!errInfo.OK()) {
        YRLOG_ERROR("Failed to save state of instance({}), err: {}", instanceId, errInfo.Msg());
        return errInfo;
    }
    // wait for the response and check it
    auto promise = std::make_shared<std::promise<StateSaveResponse>>();
    std::shared_future<StateSaveResponse> future = promise->get_future().share();
    fsClient->StateSaveAsync(req, [promise](StateSaveResponse resp) -> void { promise->set_value(resp); });
    errInfo = WaitAndCheckResp(future, instanceId, timeout);
    if (!errInfo.OK()) {
        YRLOG_ERROR("Failed to save state of instance({}), response err: {}", instanceId, errInfo.Msg());
        return errInfo;
    }
    YRLOG_INFO("Succeeded to save state of instance({})", instanceId);
    return errInfo;
}

ErrorInfo InvokeAdaptor::LoadState(std::shared_ptr<Buffer> &data, const int &timeout)
{
    std::string instanceId = Config::Instance().INSTANCE_ID();
    YRLOG_DEBUG("Start to load state of instance({})", instanceId);
    if (timeout <= 0 && timeout != -1) {
        return ErrorInfo(ErrorCode::ERR_PARAM_INVALID, ModuleCode::RUNTIME, "Timeout must be positive or equal to -1");
    }

    StateLoadRequest req;
    req.set_checkpointid(instanceId);
    // wait for the response and check it
    auto promise = std::make_shared<std::promise<StateLoadResponse>>();
    std::shared_future<StateLoadResponse> future = promise->get_future().share();
    fsClient->StateLoadAsync(req, [promise](StateLoadResponse resp) -> void { promise->set_value(resp); });
    auto errInfo = WaitAndCheckResp(future, instanceId, timeout);
    if (!errInfo.OK()) {
        YRLOG_ERROR("Failed to load state of instance({}), response err: {}", instanceId, errInfo.Msg());
        return errInfo;
    }
    auto resp = future.get();

    errInfo = ReadDataFromState(instanceId, resp.state(), data);
    if (!errInfo.OK()) {
        YRLOG_ERROR("Failed to load state of instance({}), err: {}", instanceId, errInfo.Msg());
    }
    YRLOG_DEBUG("Succeeded to load state of instance({})", instanceId);
    return errInfo;
}

void InvokeAdaptor::Finalize(bool isDriver)
{
    if (isDriver) {
        auto err = this->Kill(runtimeContext->GetJobId(), "", libruntime::Signal::KillAllInstances);
        if (!err.OK()) {
            YRLOG_WARN("Failed to kill all instance, msg: {}", err.Msg());
        }
    }
    if (groupManager != nullptr) {
        groupManager->Stop();
    }
    if (fiberPool_) {
        fiberPool_->Shutdown();
    }
    if (functionMasterClient_) {
        functionMasterClient_->Stop();
    }
    isRunning = false;
    taskSubmitter->Finalize();
    if (isDriver) {
        fsClient->Stop();
    }
}

void InvokeAdaptor::PushInvokeSpec(std::shared_ptr<InvokeSpec> spec)
{
    this->requestManager->PushRequest(spec);
}

ErrorInfo InvokeAdaptor::Kill(const std::string &instanceId, const std::string &payload, int signal)
{
    invokeOrderMgr->ClearInsOrderMsg(instanceId, signal);
    if (instanceId.empty()) {
        return ErrorInfo(YR::Libruntime::ERR_INSTANCE_ID_EMPTY, YR::Libruntime::ModuleCode::RUNTIME,
                         "instance id is empty.");
    }
    YRLOG_DEBUG("start kill instance, instance id is {}, signal is {}", instanceId, signal);
    KillRequest killReq;
    killReq.set_instanceid(instanceId);
    killReq.set_payload(payload);
    killReq.set_signal(signal);

    auto killPromise = std::make_shared<std::promise<KillResponse>>();
    std::shared_future<KillResponse> killFuture = killPromise->get_future().share();
    this->fsClient->KillAsync(killReq, [killPromise](KillResponse rsp) -> void { killPromise->set_value(rsp); });
    ErrorInfo errInfo;
    if (signal == libruntime::Signal::killInstanceSync) {
        errInfo = WaitAndCheckResp(killFuture, instanceId, NO_TIMEOUT);
    } else {
        errInfo = WaitAndCheckResp(killFuture, instanceId, g_killTimeout);
    }
    return errInfo;
}

void InvokeAdaptor::KillAsync(const std::string &instanceId, const std::string &payload, int signal)
{
    YRLOG_DEBUG("start kill instance async, instance id is {}, signal is {}, payload is {}", instanceId, signal,
                payload);
    KillRequest killReq;
    killReq.set_instanceid(instanceId);
    killReq.set_payload(payload);
    killReq.set_signal(signal);
    this->fsClient->KillAsync(killReq, [killReq](KillResponse rsp) -> void {
        if (rsp.code() != common::ERR_NONE) {
            YRLOG_WARN("kill request failed, ins id is {}, signal is {}, err code is {}, err msg is {}",
                       killReq.instanceid(), killReq.signal(), rsp.code(), rsp.message());
        }
    });
}

void InvokeAdaptor::ReceiveRequestLoop(void)
{
    fsClient->ReceiveRequestLoop();
}

ErrorInfo InvokeAdaptor::GroupCreate(const std::string &groupName, GroupOpts &opts)
{
    if (!this->groupManager->IsGroupExist(groupName)) {
        auto group = std::make_shared<NamedGroup>(groupName, librtConfig->tenantId, opts, this->fsClient,
                                                  this->waitingObjectManager, this->memStore);
        this->groupManager->AddGroup(group);
        return this->groupManager->GroupCreate(groupName);
    }
    return ErrorInfo(ErrorCode::ERR_PARAM_INVALID, ModuleCode::RUNTIME,
                     "duplicated group invoke by same group name, group name is: " + groupName);
}

ErrorInfo InvokeAdaptor::RangeCreate(const std::string &groupName, InstanceRange &range)
{
    if (!this->groupManager->IsGroupExist(groupName)) {
        auto group = std::make_shared<RangeGroup>(groupName, librtConfig->tenantId, range, this->fsClient,
                                                  this->waitingObjectManager, this->memStore, this->invokeOrderMgr);
        this->groupManager->AddGroup(group);
        return this->groupManager->GroupCreate(groupName);
    }
    std::string msg = "duplicated group invoke by same group name, group name is: " + groupName;
    YRLOG_ERROR(msg);
    return ErrorInfo(ErrorCode::ERR_PARAM_INVALID, ModuleCode::RUNTIME, msg);
}

ErrorInfo InvokeAdaptor::CreateFunctionGroup(const std::shared_ptr<InvokeSpec> &createSpec,
                                             const std::shared_ptr<InvokeSpec> &invokeSpec)
{
    if (this->groupManager->IsGroupExist(createSpec->opts.groupName)) {
        std::string msg = "duplicated group invoke by same group name, group name is: " + createSpec->opts.groupName;
        YRLOG_ERROR(msg);
        return ErrorInfo(ErrorCode::ERR_PARAM_INVALID, ModuleCode::RUNTIME, msg);
    }
    auto group = std::make_shared<FunctionGroup>(
        createSpec->opts.groupName, librtConfig->tenantId, createSpec->opts.functionGroupOpts, this->fsClient,
        this->waitingObjectManager, this->memStore, this->invokeOrderMgr, this->requestManager,
        std::bind(&InvokeAdaptor::HandleReturnedObject, this, _1, _2));
    group->SetInvokeSpec(invokeSpec);
    this->groupManager->AddGroup(group);
    return this->groupManager->GroupCreate(createSpec->opts.groupName);
}

ErrorInfo InvokeAdaptor::Accelerate(const std::string &groupName, const AccelerateMsgQueueHandle &handle,
                                    HandleReturnObjectCallback callback)
{
    return this->groupManager->Accelerate(groupName, handle, callback);
}

ErrorInfo InvokeAdaptor::GroupWait(const std::string &groupName)
{
    return this->groupManager->Wait(groupName);
}

void InvokeAdaptor::GroupTerminate(const std::string &groupName)
{
    return this->groupManager->Terminate(groupName);
}

std::pair<std::vector<std::string>, ErrorInfo> InvokeAdaptor::GetInstanceIds(const std::string &objId,
                                                                             const std::string &groupName)
{
    auto group = this->groupManager->GetGroup(groupName);
    if (group == nullptr) {
        std::vector<std::string> emptyInstances;
        std::string msg = "failed to get group, group (name: " + groupName + ") does not exist in the group manager.";
        return std::make_pair(emptyInstances, ErrorInfo(ErrorCode::ERR_INNER_SYSTEM_ERROR, ModuleCode::RUNTIME, msg));
    }
    auto timeout = InstanceRangeEnabled(group->GetInstanceRange()) ? group->GetInstanceRange().rangeOpts.timeout
                                                                   : group->GetFunctionGroupOptions().timeout;
    return this->memStore->GetInstanceIds(objId, timeout);
}

ErrorInfo InvokeAdaptor::WriteDataToState(const std::string &instanceId, const std::shared_ptr<Buffer> data,
                                          std::string *state)
{
    if (!data || !data->ImmutableData()) {
        YRLOG_ERROR("Instance data is null, instance ID: {}", instanceId);
        return ErrorInfo(ErrorCode::ERR_INNER_SYSTEM_ERROR, ModuleCode::RUNTIME, "Instance data is null");
    }

    // state buffer format: [header uint_8(size of bufInstance)|bufInstance(instance)|bufMeta(metaConfig)]
    auto bufInstanceSize = data->GetSize();
    auto headerSize = sizeof(size_t);
    libruntime::MetaConfig metaConfig;
    librtConfig->BuildMetaConfig(metaConfig);
    std::string serializedMetaConfig = metaConfig.SerializeAsString();
    auto bufMetaSize = serializedMetaConfig.size();

    if (WillSizeOverFlow(headerSize, bufInstanceSize)) {
        return ErrorInfo(ErrorCode::ERR_INNER_SYSTEM_ERROR,
                         "size overflow " + std::to_string(headerSize) + "+" + std::to_string(bufInstanceSize));
    }
    size_t stateSize = headerSize + bufInstanceSize;
    if (WillSizeOverFlow(stateSize, bufMetaSize)) {
        return ErrorInfo(ErrorCode::ERR_INNER_SYSTEM_ERROR,
                         "size overflow " + std::to_string(stateSize) + "+" + std::to_string(bufMetaSize));
    }
    stateSize = stateSize + bufMetaSize;

    state->reserve(stateSize);
    state->append(reinterpret_cast<const char *>(&bufInstanceSize), headerSize);
    state->append(reinterpret_cast<const char *>(data->ImmutableData()), bufInstanceSize);
    state->append(serializedMetaConfig.c_str(), bufMetaSize);
    YRLOG_DEBUG("Succeeded to write instance data to state, instance ID: {}", instanceId);
    return ErrorInfo();
}

ErrorInfo InvokeAdaptor::ReadDataFromState(const std::string &instanceId, const std::string &state,
                                           std::shared_ptr<Buffer> &data)
{
    // deserialize state buffer. format: [uint_8(size of buf1)|buf1|buf2]
    YRLOG_DEBUG("Start to read instance state, instance ID: {}", instanceId);
    const char *statePtr = state.data();
    size_t stateSize = state.size();
    if (stateSize == 0) {
        YRLOG_ERROR("invalid stateSize {}  in recover of: {}", stateSize, instanceId);
        return ErrorInfo(ErrorCode::ERR_INNER_SYSTEM_ERROR, ModuleCode::RUNTIME,
                         "Failed to recover state of instance(" + instanceId + "), error state length");
    }
    size_t headerSize = sizeof(size_t);
    size_t bufInstanceSize = *reinterpret_cast<const size_t *>(statePtr);
    size_t bufMetaSize = stateSize - headerSize - bufInstanceSize;
    auto bufInstance = std::make_shared<NativeBuffer>(bufInstanceSize);
    bufInstance->MemoryCopy(static_cast<const void *>(statePtr + headerSize), bufInstanceSize);
    data = bufInstance;

    std::string metaConfigStr(statePtr + headerSize + bufInstanceSize, bufMetaSize);
    libruntime::MetaConfig metaConf;
    metaConf.ParseFromString(metaConfigStr);

    try {
        librtConfig->InitConfig(metaConf);
        taskSubmitter->UpdateConfig();
    } catch (std::exception &e) {
        YRLOG_ERROR("Failed to recover config of instance({}), exception: {}", instanceId, std::string(e.what()));
        return ErrorInfo(
            ErrorCode::ERR_USER_FUNCTION_EXCEPTION, ModuleCode::RUNTIME,
            "Failed to recover config of instance(" + instanceId + "), exception: " + std::string(e.what()));
    }
    YRLOG_DEBUG("Succeeded to read instance data from state, instance ID: {}", instanceId);
    return ErrorInfo();
}

template <typename ResponseType>
ErrorInfo InvokeAdaptor::WaitAndCheckResp(std::shared_future<ResponseType> &future, const std::string &instanceId,
                                          const int &timeout)
{
    std::string operation;
    if constexpr (std::is_same<ResponseType, StateSaveResponse>::value) {
        operation = "save";
    } else if constexpr (std::is_same<ResponseType, StateLoadResponse>::value) {
        operation = "load";
    } else if constexpr (std::is_same<ResponseType, KillResponse>::value) {
        operation = "kill";
    } else {
        YRLOG_ERROR("Unsupported response type: {}", typeid(ResponseType).name());
        return ErrorInfo(ErrorCode::ERR_INNER_SYSTEM_ERROR, ModuleCode::RUNTIME, "Unsupported response type");
    }

    if (timeout != NO_TIMEOUT) {
        auto status = future.wait_for(std::chrono::milliseconds(timeout));
        if (status != std::future_status::ready) {
            YRLOG_ERROR("Request timeout, failed to {} instance with instanceId: {}", operation, instanceId);
            return ErrorInfo(ErrorCode::ERR_INIT_CONNECTION_FAILED, ModuleCode::RUNTIME,
                             "Request timeout, failed to " + operation + " instance with instanceId: " + instanceId);
        }
    }
    ResponseType rsp = future.get();
    if (rsp.code() != common::ERR_NONE) {
        YRLOG_ERROR("Failed to {} instance: {}, err is: {}", operation, instanceId, rsp.message());
        return ErrorInfo(static_cast<ErrorCode>(rsp.code()), ModuleCode::CORE,
                         "Failed to " + operation + " instance: " + instanceId + " , err is : " + rsp.message());
    }
    YRLOG_DEBUG("Succeeded to receive {} instance response, instance id is {}", operation, instanceId);
    return ErrorInfo(ErrorCode::ERR_OK, rsp.message());
}

void InvokeAdaptor::ReportMetrics(const std::string &requestId, const std::string &traceId, int value)
{
    if (!Config::Instance().ENABLE_METRICS()) {
        return;
    }
    YR::Libruntime::GaugeData data;
    data.name = "call_metric";
    data.labels["requestid"] = requestId;
    data.labels["traceid"] = traceId;
    data.value = value;
    auto err = metricsAdaptor->ReportMetrics(data);
    if (!err.OK()) {
        YRLOG_WARN("failed to report metrics, requestid: {}, traceid: {}, value: {}", requestId, traceId, value);
    }
}

std::shared_ptr<InvokeSpec> InvokeAdaptor::BuildCreateSpec(std::shared_ptr<InvokeSpec> spec)
{
    auto createSpec = std::make_shared<InvokeSpec>();
    createSpec->jobId = spec->jobId;
    createSpec->functionMeta = spec->functionMeta;
    createSpec->opts = spec->opts;
    createSpec->invokeType = libruntime::InvokeType::CreateInstanceStateless;
    createSpec->traceId = YR::utility::IDGenerator::GenTraceId(spec->jobId);
    createSpec->requestId = YR::utility::IDGenerator::GenRequestId();
    std::vector<DataObject> returnObjs{DataObject("")};
    memStore->GenerateReturnObjectIds(createSpec->requestId, returnObjs);
    memStore->AddReturnObject(returnObjs);
    createSpec->returnIds = returnObjs;
    createSpec->BuildInstanceCreateRequest(*librtConfig);
    return createSpec;
}

void InvokeAdaptor::InitMetricsAdaptor(bool userEnable)
{
    if (!Config::Instance().METRICS_CONFIG().empty()) {
        try {
            metricsAdaptor->Init(nlohmann::json::parse(Config::Instance().METRICS_CONFIG()), userEnable);
            return;
        } catch (std::exception &e) {
            YRLOG_ERROR("parse config json failed, error: {}", e.what());
            return;
        }
    }
    if (Config::Instance().METRICS_CONFIG_FILE().empty()) {
        YRLOG_WARN("metrics config is empty");
        return;
    }
    std::ifstream f(Config::Instance().METRICS_CONFIG_FILE());
    if (!f.is_open()) {
        YRLOG_ERROR("failed to open file {}", Config::Instance().METRICS_CONFIG_FILE());
        return;
    }
    nlohmann::json config;
    f >> config;
    metricsAdaptor->Init(config, userEnable);
    f.close();
}

void InvokeAdaptor::CreateResourceGroup(std::shared_ptr<ResourceGroupCreateSpec> spec)
{
    this->rGroupManager_->StoreRGDetail(spec->rGroupSpec.name, spec->requestId, spec->rGroupSpec.bundles.size());
    auto weak_this = weak_from_this();
    auto rspHandler = [weak_this, spec](const CreateResourceGroupResponse &resp) -> void {
        if (auto this_ptr = weak_this.lock(); this_ptr) {
            ErrorInfo err;
            if (resp.code() != common::ERR_NONE) {
                err = ErrorInfo(static_cast<ErrorCode>(resp.code()), ModuleCode::CORE, resp.message());
            }
            this_ptr->rGroupManager_->SetRGCreateErrInfo(spec->rGroupSpec.name, spec->requestId, err);
        }
    };
    fsClient->CreateRGroupAsync(spec->requestCreateRGroup, rspHandler);
    YRLOG_DEBUG("Create resource group request has been sent, req id is {}, Details: {}", spec->requestId,
                spec->requestCreateRGroup.DebugString());
}

std::pair<YR::Libruntime::FunctionMeta, ErrorInfo> InvokeAdaptor::GetInstance(const std::string &name,
                                                                              const std::string &nameSpace,
                                                                              int timeoutSec)
{
    auto insId = nameSpace.empty() ? this->librtConfig->ns + "-" + name : nameSpace + "-" + name;
    YRLOG_DEBUG("start get instance, instance id is {}", insId);
    if (insId == this->librtConfig->GetInstanceId()) {
        return std::make_pair(
            YR::Libruntime::FunctionMeta(),
            YR::Libruntime::ErrorInfo(ErrorCode::ERR_PARAM_INVALID, ModuleCode::RUNTIME,
                                      insId + " cannot obtain its own instance handle by get_instance method"));
    }
    auto [metaCached, isExist] = GetCachedInsMeta(insId);
    if (isExist) {
        YRLOG_DEBUG("get cached meta info of instance: {}, return directly", insId);
        return std::make_pair(convertProtoToFuncMeta(metaCached), ErrorInfo());
    }
    KillRequest killReq;
    killReq.set_instanceid(insId);
    killReq.set_signal(libruntime::Signal::GetInstance);
    auto promise = std::promise<std::pair<libruntime::FunctionMeta, ErrorInfo>>();
    auto future = promise.get_future();
    this->fsClient->KillAsync(killReq, [&promise](const KillResponse &rsp) -> void {
        if (rsp.code() != common::ERR_NONE) {
            YR::Libruntime::ErrorInfo errInfo(static_cast<ErrorCode>(rsp.code()), ModuleCode::RUNTIME,
                                              rsp.message());
            promise.set_value(std::make_pair(libruntime::FunctionMeta{}, errInfo));
        } else {
            libruntime::FunctionMeta funcMeta;
            funcMeta.ParseFromString(rsp.message());
            promise.set_value(std::make_pair(funcMeta, YR::Libruntime::ErrorInfo()));
        }
    });
    auto [funcMeta, errorInfo] = future.get();
    YRLOG_DEBUG("get instance finished, err code is {}, err msg is {}, function meta is {}", errorInfo.Code(),
                errorInfo.Msg(), funcMeta.DebugString());
    if (errorInfo.OK()) {
        this->UpdateAndSubcribeInsStatus(insId, funcMeta);
    } else {
        this->RemoveInsMetaInfo(insId);
    }
    return std::make_pair(convertProtoToFuncMeta(funcMeta), errorInfo);
}

std::pair<libruntime::FunctionMeta, bool> InvokeAdaptor::GetCachedInsMeta(const std::string &insId)
{
    std::unique_lock<std::mutex> lock(metaMapMtx);
    if (metaMap.find(insId) == metaMap.end()) {
        return std::make_pair(libruntime::FunctionMeta(), false);
    }
    return std::make_pair(metaMap[insId], true);
}

void InvokeAdaptor::UpdateAndSubcribeInsStatus(const std::string &insId, libruntime::FunctionMeta &funcMeta)
{
    {
        std::unique_lock<std::mutex> lock(metaMapMtx);
        if (metaMap.find(insId) != metaMap.end()) {
            YRLOG_DEBUG("there is alreay cache meta for instance: {}, no need to update and subsrcibe", insId);
            return;
        }
        YRLOG_DEBUG(
            "start add ins meta into metamap, ins id is: {}, class name is {}, module name is {}, function id is {}, "
            "language is {}",
            insId, funcMeta.classname(), funcMeta.modulename(), funcMeta.functionid(), funcMeta.language());
        if (!funcMeta.name().empty() && funcMeta.ns().empty()) {
            funcMeta.set_ns(DEFAULT_YR_NAMESPACE);
        }
        metaMap[insId] = funcMeta;
    }
    this->Subscribe(insId);
}

void InvokeAdaptor::SubscribeAll()
{
    for (auto it = metaMap.begin(); it != metaMap.end(); it++) {
        this->Subscribe(it->first);
    }
    this->SubscribeActiveMaster();
}

void InvokeAdaptor::Subscribe(const std::string &insId)
{
    KillRequest killReq;
    killReq.set_instanceid(insId);
    killReq.set_signal(libruntime::Signal::Subsribe);
    SubscriptionPayload subscription;
    InstanceTermination *termination = subscription.mutable_instancetermination();
    termination->set_instanceid(insId);
    std::string serializedPayload;
    subscription.SerializeToString(&serializedPayload);
    killReq.set_payload(serializedPayload);
    auto weakThis = weak_from_this();
    YRLOG_DEBUG("start send subscribe req of instance: {}", insId);
    this->fsClient->KillAsync(killReq, [insId, weakThis](KillResponse rsp) -> void {
        if (rsp.code() != common::ERR_NONE) {
            YRLOG_WARN("subcribe ins status failed, ins id is : {}, code is {}, msg is {},", insId, rsp.code(),
                       rsp.message());
        }
        if (rsp.code() == common::ERR_SCHEDULE_PLUGIN_CONFIG || rsp.code() == common::ERR_SUB_STATE_INVALID) {
            if (auto thisPtr = weakThis.lock(); thisPtr) {
                thisPtr->RemoveInsMetaInfo(insId);
            }
        }
    });
}

void InvokeAdaptor::RemoveInsMetaInfo(const std::string &insId)
{
    std::unique_lock<std::mutex> lock(metaMapMtx);
    if (metaMap.find(insId) == metaMap.end()) {
        YRLOG_DEBUG("there is no meta info of ins: {}, no need remove", insId);
        return;
    }
    YRLOG_DEBUG("start remove meta info of instance : {}", insId);
    metaMap.erase(insId);
}

std::pair<ErrorInfo, std::string> InvokeAdaptor::GetNodeIpAddress()
{
    return fsClient->GetNodeIp();
}

std::pair<ErrorInfo, std::string> InvokeAdaptor::GetNodeId()
{
    return fsClient->GetNodeId();
}

std::pair<ErrorInfo, std::vector<ResourceUnit>> InvokeAdaptor::GetResources(void)
{
    return functionMasterClient_->GetResources();
}

std::pair<ErrorInfo, ResourceGroupUnit> InvokeAdaptor::GetResourceGroupTable(const std::string &resourceGroupId)
{
    return functionMasterClient_->GetResourceGroupTable(resourceGroupId);
}

std::pair<ErrorInfo, QueryNamedInsResponse> InvokeAdaptor::QueryNamedInstances()
{
    auto ret = functionMasterClient_->QueryNamedInstances();
    return ret;
}

void InvokeAdaptor::SubscribeActiveMaster()
{
    auto insId = Config::Instance().INSTANCE_ID();
    auto instanceId = insId.empty() ? "driver-" + runtimeContext->GetJobId() : insId;
    KillRequest killReq;
    killReq.set_instanceid(instanceId);
    killReq.set_signal(libruntime::Signal::Subsribe);
    SubscriptionPayload subscription;
    FunctionMasterObserve functionMaster;
    subscription.mutable_functionmaster()->CopyFrom(functionMaster);
    std::string serializedPayload;
    subscription.SerializeToString(&serializedPayload);
    killReq.set_payload(serializedPayload);
    auto weakThis = weak_from_this();
    YRLOG_DEBUG("start send subscribe function master req of instance: {}", instanceId);
    this->fsClient->KillAsync(killReq, [instanceId](KillResponse rsp) -> void {
        YRLOG_DEBUG("get subcribe function master response, ins id is : {}, code is {},", instanceId, rsp.code());
    });
}
}  // namespace Libruntime
}  // namespace YR
