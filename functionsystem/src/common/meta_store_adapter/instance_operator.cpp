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

#include "instance_operator.h"

#include <nlohmann/json.hpp>

#include "common/utils/exec_utils.h"
#include "logs/logging.h"
#include "meta_store_client/txn_transaction.h"
#include "meta_store_kv_operation.h"

namespace functionsystem {

bool TransactionFailedForEtcd(int32_t errCode)
{
    return !(TRANSACTION_ERROR_START <= errCode && errCode < TRANSACTION_ERROR_END);
}

InstanceOperator::InstanceOperator(const std::shared_ptr<MetaStoreClient> &metaStoreClient)
    : client_(metaStoreClient), isCentOS_(IsCentos())
{
}

OperateResult InstanceOperator::OnCreate(const OperateInfo &operateInfo)
{
    if (operateInfo.response->status.IsError()) {
        YRLOG_ERROR("failed to execute transaction command while creating, key: {}, error: {}", operateInfo.key,
                    operateInfo.response->status.GetMessage());
        // only raise to kill self while Centos
        // most of the current scenarios occur in large-scale
        // more appropriate methods need to be applied to ensure consistency in the future
        if (operateInfo.isCentOS && operateInfo.response->status.StatusCode() == StatusCode::GRPC_DEADLINE_EXCEEDED) {
            YR_EXIT("etcd operation error");
        }
        return OperateResult{ operateInfo.response->status, "", 0, 0 };
    }
    if (operateInfo.response->success) {
        if (operateInfo.response->responses.size() != operateInfo.keySize) {
            YRLOG_ERROR("the size of responses transaction return is incorrect while creating, key: {}, size is {}",
                        operateInfo.key, operateInfo.response->responses.size());
            PrintResponse(operateInfo);
            return OperateResult{ Status(StatusCode::INSTANCE_TRANSACTION_WRONG_RESPONSE_SIZE,
                                         "the size of responses transaction return is incorrect"), "", 0, 0 };
        }
        YRLOG_DEBUG("create instance success: {}, key: {}, revision: {}", operateInfo.response->success,
                    operateInfo.key, operateInfo.response->header.revision);
        // use last response's revision, as current revision
        return OperateResult{ Status::OK(), "", 0, operateInfo.response->header.revision };
    }
    if (operateInfo.response->responses[0].operationType != TxnOperationType::OPERATION_GET) {
        YRLOG_ERROR("operation type({}) is not right, key: {}", static_cast<std::underlying_type_t<TxnOperationType>>
            (operateInfo.response->responses[0].operationType), operateInfo.key);
        PrintResponse(operateInfo);
        return OperateResult{ Status(StatusCode::INSTANCE_TRANSACTION_GET_INFO_FAILED, "operation type is wrong"), "",
                              0, 0 };
    }
    auto getResponse = std::get<GetResponse>(operateInfo.response->responses[0].response);
    if (getResponse.kvs.empty()) {
        YRLOG_ERROR("get response KV is empty while creating, key: {}", operateInfo.key);
        PrintResponse(operateInfo);
        return OperateResult{ Status(StatusCode::INSTANCE_TRANSACTION_GET_INFO_FAILED, "get response KV is empty"), "",
                              0, 0 };
    }
    if (operateInfo.value == getResponse.kvs.front().value()) {
        YRLOG_INFO("create instance success but txn fail, key: {} revision: {}", operateInfo.key,
                   operateInfo.response->responses.back().header.revision);
        return OperateResult{ Status::OK(), "", 0, operateInfo.response->responses.back().header.revision };
    }
    PrintResponse(operateInfo);
    return OperateResult{ Status(StatusCode::INSTANCE_TRANSACTION_WRONG_VERSION, "version is incorrect"),
                          getResponse.kvs.front().value(), 0, getResponse.kvs.front().mod_revision() };
}

OperateResult InstanceOperator::OnModify(const OperateInfo &operateInfo)
{
    if (operateInfo.response->status.IsError()) {
        YRLOG_ERROR("failed to execute transaction command while modifying, key: {}, error: {}", operateInfo.key,
                    operateInfo.response->status.GetMessage());
        if (operateInfo.isCentOS && operateInfo.response->status.StatusCode() == StatusCode::GRPC_DEADLINE_EXCEEDED) {
            YR_EXIT("etcd operation error");
        }
        return OperateResult{ operateInfo.response->status, "", 0, 0 };
    }

    if (operateInfo.response->success) {
        if (operateInfo.response->responses.size() != operateInfo.keySize) {
            YRLOG_ERROR("the size of responses transaction return is incorrect while modifying, key: {}, size is {}",
                        operateInfo.key, operateInfo.response->responses.size());
            PrintResponse(operateInfo);
            return OperateResult{ Status(StatusCode::INSTANCE_TRANSACTION_WRONG_RESPONSE_SIZE,
                                         "the size of responses transaction return is incorrect"), "", 0, 0 };
        }
        YRLOG_DEBUG("modify instance success: {}, key: {}, revision: {}", operateInfo.response->success,
                    operateInfo.key, operateInfo.response->header.revision);
        return OperateResult{ Status::OK(), "", operateInfo.version, operateInfo.response->header.revision };
    }
    if (operateInfo.response->responses[0].operationType != TxnOperationType::OPERATION_GET) {
        YRLOG_ERROR("operation type({}) is not right, key: {}", static_cast<std::underlying_type_t<TxnOperationType>>
                    (operateInfo.response->responses[0].operationType), operateInfo.key);
        PrintResponse(operateInfo);
        return OperateResult{ Status(StatusCode::INSTANCE_TRANSACTION_GET_INFO_FAILED, "operation type is wrong"), "",
                              0, 0 };
    }
    auto getResponse = std::get<GetResponse>(operateInfo.response->responses[0].response);
    if (getResponse.kvs.empty()) {
        YRLOG_ERROR("get response KV is empty while modifying, key: {}", operateInfo.key);
        PrintResponse(operateInfo);
        return OperateResult{ Status(StatusCode::INSTANCE_TRANSACTION_GET_INFO_FAILED, "get response KV is empty"), "",
                              0, 0 };
    }
    if (operateInfo.value == getResponse.kvs.front().value()) {
        YRLOG_INFO("modify instance success but txn fail, key: {}", operateInfo.key);
        return OperateResult{ Status::OK(), getResponse.kvs.front().value(), getResponse.kvs.front().version() - 1,
                              operateInfo.response->responses.back().header.revision };
    }
    PrintResponse(operateInfo);
    return OperateResult{ Status(StatusCode::INSTANCE_TRANSACTION_WRONG_VERSION, "version is incorrect"),
                          getResponse.kvs.front().value(), 0, getResponse.kvs.front().mod_revision() };
}

OperateResult InstanceOperator::OnDelete(const OperateInfo &operateInfo)
{
    if (operateInfo.response->status.IsError()) {
        YRLOG_ERROR("failed to execute transaction command, key: {}, error: {}", operateInfo.key,
                    operateInfo.response->status.GetMessage());
        if (operateInfo.isCentOS && operateInfo.response->status.StatusCode() == StatusCode::GRPC_DEADLINE_EXCEEDED) {
            YR_EXIT("etcd operation error");
        }
        return OperateResult{ operateInfo.response->status, "", 0, 0 };
    }
    if (operateInfo.response->success) {
        if (operateInfo.response->responses.size() != operateInfo.keySize) {
            YRLOG_ERROR("the size of responses transaction return is incorrect while deleting, key: {}, size is {}",
                        operateInfo.key, operateInfo.response->responses.size());
            PrintResponse(operateInfo);
            return OperateResult{ Status(StatusCode::INSTANCE_TRANSACTION_WRONG_RESPONSE_SIZE,
                                         "the size of responses transaction return is incorrect"), "", 0, 0 };
        }
        if (operateInfo.response->responses[0].operationType != TxnOperationType::OPERATION_DELETE) {
            YRLOG_ERROR("operation type({}) is not right, key: {}",
                        static_cast<std::underlying_type_t<TxnOperationType>>
                        (operateInfo.response->responses[0].operationType), operateInfo.key);
            PrintResponse(operateInfo);
            return OperateResult{ Status(StatusCode::INSTANCE_TRANSACTION_GET_INFO_FAILED, "operation type is wrong"),
                                  "", 0, 0 };
        }
        if (!std::get<DeleteResponse>(operateInfo.response->responses[0].response).deleted) {
            YRLOG_ERROR("failed to delete KV, key: {}", operateInfo.key);
            PrintResponse(operateInfo);
            return OperateResult{ Status(StatusCode::INSTANCE_TRANSACTION_DELETE_FAILED, "failed to delete KV"), "",
                                  0, 0 };
        }
        YRLOG_DEBUG("delete instance success: {}, key: {}", operateInfo.response->success, operateInfo.key);
        return OperateResult{ Status::OK(), "", 0, operateInfo.response->header.revision };
    }
    PrintResponse(operateInfo);
    if (operateInfo.response->responses[0].operationType != TxnOperationType::OPERATION_GET) {
        YRLOG_ERROR("operation type({}) is not right, key: {}", static_cast<std::underlying_type_t<TxnOperationType>>
                    (operateInfo.response->responses[0].operationType), operateInfo.key);
        return OperateResult{ Status(StatusCode::INSTANCE_TRANSACTION_GET_INFO_FAILED, "operation type is wrong"), "",
                              0, 0 };
    }
    auto getResponse = std::get<GetResponse>(operateInfo.response->responses[0].response);
    if (getResponse.kvs.empty()) {
        YRLOG_ERROR("get response KV is empty, key: {}", operateInfo.key);
        return OperateResult{ Status(StatusCode::INSTANCE_TRANSACTION_GET_INFO_FAILED, "get response KV is empty"), "",
                              0, 0 };
    }
    return OperateResult{ Status(StatusCode::INSTANCE_TRANSACTION_WRONG_VERSION, "version is incorrect"),
                          getResponse.kvs.front().value(), 0, getResponse.kvs.front().mod_revision() };
}

OperateResult InstanceOperator::OnForceDelete(const OperateInfo &operateInfo)
{
    if (operateInfo.response->status.IsError()) {
        YRLOG_ERROR("failed to execute transaction command, key: {}, error: {}", operateInfo.key,
                    operateInfo.response->status.GetMessage());
        if (operateInfo.isCentOS && operateInfo.response->status.StatusCode() == StatusCode::GRPC_DEADLINE_EXCEEDED) {
            YR_EXIT("etcd operation error");
        }
        return OperateResult{ operateInfo.response->status, "", 0, 0 };
    }

    if (operateInfo.response->responses.size() != operateInfo.keySize) {
        YRLOG_ERROR("the size of responses transaction return is incorrect while deleting, key: {}, size is {}",
                    operateInfo.key, operateInfo.response->responses.size());
        PrintResponse(operateInfo);
        return OperateResult{ Status(StatusCode::INSTANCE_TRANSACTION_WRONG_RESPONSE_SIZE),
                              "the size of responses transaction return is incorrect", 0, 0 };
    }

    return OperateResult{ Status::OK(), "", 0, operateInfo.response->header.revision };
}

litebus::Future<OperateResult> InstanceOperator::Create(std::shared_ptr<StoreInfo> instanceInfo,
                                                        std::shared_ptr<StoreInfo> routeInfo, bool isLowReliability)
{
    if (instanceInfo == nullptr) {
        YRLOG_ERROR("instance info must be exist");
        return OperateResult{ Status(StatusCode::INSTANCE_TRANSACTION_WRONG_PARAMETER), "instance info must be exist",
                              0, 0 };
    }

    auto transaction = client_->BeginTransaction();
    transaction->If(meta_store::TxnCompare::OfVersion(instanceInfo->key, meta_store::CompareOperator::EQUAL, 0));
    if (routeInfo != nullptr) {
        transaction->If(meta_store::TxnCompare::OfVersion(routeInfo->key, meta_store::CompareOperator::EQUAL, 0));
    }
    PutOption putOption = { .leaseId = 0, .prevKv = false, .asyncBackup = isLowReliability };
    std::string debugKeys;
    uint64_t keySize = 1;
    debugKeys += "(" + instanceInfo->key + ")";
    transaction->Then(meta_store::TxnOperation::Create(instanceInfo->key, instanceInfo->value, putOption));

    if (routeInfo != nullptr) {
        keySize++;
        debugKeys += "(" + routeInfo->key + ")";
        transaction->Then(meta_store::TxnOperation::Create(routeInfo->key, routeInfo->value, putOption));
    }

    GetOption getOption = { false, false, false, 1, SortOrder::NONE, SortTarget::KEY };
    transaction->Else(meta_store::TxnOperation::Create(instanceInfo->key, getOption));

    YRLOG_DEBUG("create instance for key: {}", debugKeys);
    return transaction->Commit().Then([debugKeys, value(instanceInfo->value), keySize,
                                       isCentOS(isCentOS_)](const std::shared_ptr<TxnResponse> &response) {
        OperateInfo operateInfo = OperateInfo{ debugKeys, value, keySize, 0, isCentOS, response };
        return OnCreate(operateInfo);
    });
}

litebus::Future<OperateResult> InstanceOperator::Modify(std::shared_ptr<StoreInfo> instanceInfo,
                                                        std::shared_ptr<StoreInfo> routeInfo, const int64_t version,
                                                        bool isLowReliability)
{
    if (instanceInfo == nullptr) {
        YRLOG_ERROR("instance info must be exist");
        return OperateResult{ Status(StatusCode::INSTANCE_TRANSACTION_WRONG_PARAMETER), "instance info must be exist",
                              0, 0 };
    }

    auto transaction = client_->BeginTransaction();
    transaction->If(meta_store::TxnCompare::OfVersion(instanceInfo->key, meta_store::CompareOperator::EQUAL, version));
    PutOption option = { .leaseId = 0, .prevKv = false, .asyncBackup = isLowReliability };
    std::string debugKeys;
    uint64_t keySize = 1;
    debugKeys += "(" + instanceInfo->key + ")";
    transaction->Then(meta_store::TxnOperation::Create(instanceInfo->key, instanceInfo->value, option));

    if (routeInfo != nullptr) {
        keySize++;
        debugKeys += "(" + routeInfo->key + ")";
        transaction->Then(meta_store::TxnOperation::Create(routeInfo->key, routeInfo->value, option));
    }

    GetOption getOption = { false, false, false, 1, SortOrder::NONE, SortTarget::KEY };
    transaction->Else(meta_store::TxnOperation::Create(instanceInfo->key, getOption));

    YRLOG_DEBUG("modify instance for key: {}, version: {}", debugKeys, version);
    return transaction->Commit().Then([debugKeys, value(instanceInfo->value), version, keySize,
                                       isCentOS(isCentOS_)](const std::shared_ptr<TxnResponse> &response) {
        OperateInfo operateInfo = OperateInfo{ debugKeys, value, keySize, version, isCentOS, response };
        return OnModify(operateInfo);
    });
}

litebus::Future<OperateResult> InstanceOperator::Delete(std::shared_ptr<StoreInfo> instanceInfo,
                                                        std::shared_ptr<StoreInfo> routeInfo,
                                                        std::shared_ptr<StoreInfo> debugInstPutInfo,
                                                        const int64_t version,
                                                        bool isLowReliability)
{
    if (instanceInfo == nullptr) {
        YRLOG_ERROR("instance info must be exist");
        return OperateResult{ Status(StatusCode::INSTANCE_TRANSACTION_WRONG_PARAMETER), "instance info must be exist",
                              0, 0 };
    }

    auto transaction = client_->BeginTransaction();
    transaction->If(meta_store::TxnCompare::OfVersion(instanceInfo->key, meta_store::CompareOperator::EQUAL, version));
    DeleteOption delOption = { .prevKv = false, .prefix = false, .asyncBackup = isLowReliability };
    std::string debugKeys;
    uint64_t keySize = 1;
    debugKeys += "(" + instanceInfo->key + ")";
    transaction->Then(meta_store::TxnOperation::Create(instanceInfo->key, delOption));

    if (routeInfo != nullptr) {
        keySize++;
        debugKeys += "(" + routeInfo->key + ")";
        transaction->Then(meta_store::TxnOperation::Create(routeInfo->key, delOption));
    }

    if (debugInstPutInfo != nullptr) {
        keySize++;
        debugKeys += "(" + debugInstPutInfo->key + ")";
        transaction->Then(meta_store::TxnOperation::Create(debugInstPutInfo->key, delOption));
    }

    GetOption getOption = { false, false, false, 0, SortOrder::NONE, SortTarget::KEY };
    transaction->Else(meta_store::TxnOperation::Create(instanceInfo->key, getOption));

    YRLOG_DEBUG("delete instance for key: {}, version: {}", debugKeys, version);
    return transaction->Commit().Then(
        [debugKeys, keySize, version, isCentOS(isCentOS_)](const std::shared_ptr<TxnResponse> &response) {
            OperateInfo operateInfo = OperateInfo{ debugKeys, "", keySize, version, isCentOS, response };
            return OnDelete(operateInfo);
        });
}

litebus::Future<OperateResult> InstanceOperator::ForceDelete(std::shared_ptr<StoreInfo> instanceInfo,
                                                             std::shared_ptr<StoreInfo> routeInfo,
                                                             std::shared_ptr<StoreInfo> debugInstPutInfo,
                                                             bool isLowReliability)
{
    if (instanceInfo == nullptr) {
        YRLOG_ERROR("instance info must be exist");
        return OperateResult{ Status(StatusCode::INSTANCE_TRANSACTION_WRONG_PARAMETER), "instance info must be exist",
                              0, 0 };
    }

    auto transaction = client_->BeginTransaction();
    transaction->If(meta_store::TxnCompare::OfValue(instanceInfo->key, meta_store::CompareOperator::NOT_EQUAL, ""));
    DeleteOption delOption = { .prevKv = false, .prefix = false, .asyncBackup = isLowReliability };
    std::string debugKeys;
    uint64_t keySize = 1;
    debugKeys += "(" + instanceInfo->key + ")";
    transaction->Then(meta_store::TxnOperation::Create(instanceInfo->key, delOption));
    transaction->Else(meta_store::TxnOperation::Create(instanceInfo->key, delOption));

    if (routeInfo != nullptr) {
        keySize++;
        debugKeys += "(" + routeInfo->key + ")";
        transaction->Then(meta_store::TxnOperation::Create(routeInfo->key, delOption));
        transaction->Else(meta_store::TxnOperation::Create(routeInfo->key, delOption));
    }

    if (debugInstPutInfo != nullptr) {
        keySize++;
        debugKeys += "(" + debugInstPutInfo->key + ")";
        transaction->Then(meta_store::TxnOperation::Create(debugInstPutInfo->key, delOption));
        transaction->Else(meta_store::TxnOperation::Create(debugInstPutInfo->key, delOption));
    }
    YRLOG_DEBUG("force delete instance for key: {}", debugKeys);
    return transaction->Commit().Then(
        [debugKeys, keySize, isCentOS(isCentOS_)](const std::shared_ptr<TxnResponse> &response) {
            OperateInfo operateInfo = OperateInfo{ debugKeys, "", keySize, 0, isCentOS, response };
            return OnForceDelete(operateInfo);
        });
}

litebus::Future<OperateResult> InstanceOperator::GetInstance(const std::string &key)
{
    return client_->Get(key, {}).Then(
        [key](const std::shared_ptr<GetResponse> &response) -> litebus::Future<OperateResult> {
            if (response->status.IsError()) {
                YRLOG_WARN("failed to GetInstance, key: {}", key);
                return OperateResult{ Status(StatusCode::FAILED), "", 0, 0 };
            }

            if (response->count == 0) {
                YRLOG_WARN("get response kv is empty, key: {}", key);
                return OperateResult{ Status(StatusCode::FAILED), "", 0, 0 };
            }

            return OperateResult{ Status::OK(), response->kvs[0].value(), 0, response->kvs[0].mod_revision() };
        });
}

void InstanceOperator::OnPrintResponse(const KeyValue& kv)
{
    nlohmann::json bodyJson;
    try {
        bodyJson = nlohmann::json::parse(kv.value());
        if (!bodyJson.empty()) {
            YRLOG_DEBUG("{}| instance status ({}), create_revision ({}), mod_revision ({}), version ({}),",
                        bodyJson["instanceID"], bodyJson["instanceStatus"]["code"].dump(), kv.create_revision(),
                        kv.mod_revision(), kv.version());
        }
    } catch (const std::exception &exc) {
        YRLOG_DEBUG("{}| create_revision ({}), mod_revision ({}), version ({}), value ({})", kv.key(),
                    kv.create_revision(), kv.mod_revision(), kv.version(), kv.value());
    }
}

bool InstanceOperator::PrintResponse(const OperateInfo &operateInfo)
{
    for (size_t i = 0; i < operateInfo.response->responses.size(); i++) {
        auto &resp = operateInfo.response->responses[i];
        switch (resp.operationType) {
            case TxnOperationType::OPERATION_DELETE: {
                auto delResponse = std::get<DeleteResponse>(resp.response);
                YRLOG_DEBUG("the delete response for [{}],  status ({}), reversion ({})", operateInfo.key,
                            delResponse.status.IsOk(), delResponse.header.revision);
                for (size_t j = 0; j < delResponse.prevKvs.size(); j++) {
                    OnPrintResponse(delResponse.prevKvs[j]);
                }
                break;
            }
            case TxnOperationType::OPERATION_PUT: {
                auto putResponse = std::get<PutResponse>(resp.response);
                YRLOG_DEBUG("the put response for [{}], status ({}), reversion ({})", operateInfo.key,
                            putResponse.status.IsOk(), putResponse.header.revision);
                OnPrintResponse(putResponse.prevKv);
                break;
            }
            case TxnOperationType::OPERATION_GET: {
                auto getResponse = std::get<GetResponse>(resp.response);
                YRLOG_DEBUG("the get response for [{}], status ({}), reversion ({})", operateInfo.key,
                            getResponse.status.IsOk(), getResponse.header.revision);
                for (size_t j = 0; j < getResponse.kvs.size(); j++) {
                    OnPrintResponse(getResponse.kvs[j]);
                }
                break;
            }
        }
    }
    return true;
}

}  // namespace functionsystem
