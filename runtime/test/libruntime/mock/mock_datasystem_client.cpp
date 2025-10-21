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

#include "mock_datasystem_client.h"

#include <cstdlib>
#include <msgpack.hpp>
#include <string>

#define private public
#include "datasystem/hetero_cache/hetero_client.h"
#include "datasystem/object_cache.h"
#include "datasystem/kv_cache/kv_client.h"
#include "datasystem/kv_cache.h"

namespace datasystem {
class ThreadPool {
};

class ObjectClientImpl {
};

ObjectClient::ObjectClient(const ConnectOptions &connectOptions) {}

ObjectClient::~ObjectClient() {}

Status ObjectClient::Init()
{
    return Status::OK();
}

Status ObjectClient::Create(const std::string &objectId, uint64_t size, const CreateParam &param,
                            std::shared_ptr<Buffer> &buffer)
{
    if (objectId == "repeatedObjId") {
        return Status(StatusCode::K_OC_ALREADY_SEALED, "repeated seal");
    } else if (objectId == "errObjId") {
        return Status(StatusCode::K_RPC_DEADLINE_EXCEEDED, "error");
    }
    buffer = std::make_shared<Buffer>();
    return Status::OK();
}

Status ObjectClient::Get(const std::vector<std::string> &objectIds, int32_t timeout,
                         std::vector<Optional<Buffer>> &buffer)
{
    // To test the if branch of partial get,
    // if a vector of len = 1, successfully get
    // if a vector of len > 1, only store the first element
    buffer.clear();
    auto buf = std::make_shared<Buffer>();
    if (objectIds.size() == 1) {
        buffer.emplace_back(std::move(*buf));
        return Status::OK();
    }
    // if length >= 2, only store the first object
    buffer.emplace_back(std::move(*buf));            // only first one is non-empty
    for (size_t i = 1; i < objectIds.size(); ++i) {  // others are empty
        buffer.emplace_back();
    }
    auto status = Status(StatusCode::K_OUT_OF_MEMORY, "mock test runtime error");
    return status;
}

Status ObjectClient::GIncreaseRef(const std::vector<std::string> &objectIds, std::vector<std::string> &failedObjectIds)
{
    if (objectIds.size() == 2) {
        return Status(StatusCode::K_RPC_DEADLINE_EXCEEDED, "error");
    }
    return Status::OK();
}

Status ObjectClient::GDecreaseRef(const std::vector<std::string> &objectIds, std::vector<std::string> &failedObjectIds)
{
    return Status::OK();
}

int ObjectClient::QueryGlobalRefNum(const std::string &id)
{
    return 1;
}

Status ObjectClient::GenerateObjectKey(const std::string &prefix, std::string &key)
{
    key = prefix;
    return Status::OK();
}

Status ObjectClient::ShutDown()
{
    return Status::OK();
}
constexpr int64_t bufferSize = 32;
void *mutableData = nullptr;

Status Buffer::WLatch(uint64_t timeout)
{
    return Status::OK();
}

Status Buffer::MemoryCopy(const void *data, uint64_t length)
{
    return Status::OK();
}

Status Buffer::Seal(const std::unordered_set<std::string> &nestedIds)
{
    return Status::OK();
}

Status Buffer::UnWLatch()
{
    if (mutableData != nullptr) {
        free(mutableData);
    }
    mutableData = nullptr;
    return Status::OK();
}

Status Buffer::RLatch(uint64_t timeout)
{
    return Status::OK();
}

const void *Buffer::ImmutableData()
{
    return static_cast<const void *>(MutableData());
}

void *Buffer::MutableData()
{
    if (mutableData == nullptr) {
        mutableData = (void *)std::malloc(bufferSize);
        return mutableData;
    }
    return mutableData;
}

int64_t Buffer::GetSize() const
{
    return bufferSize;
}

Status Buffer::UnRLatch()
{
    return Status::OK();
}

Status Buffer::Publish(const std::unordered_set<std::string> &nestedIds)
{
    return Status::OK();
}

class StateClientImpl {
};

KVClient::KVClient(const ConnectOptions &connectOptions){};

Status KVClient::Init()
{
    return Status::OK();
}

Status KVClient::Set(const std::string &key, const StringView &val, const SetParam &param)
{
    if (param.writeMode == WriteMode::NONE_L2_CACHE_EVICT) {
        return Status(StatusCode::K_OUT_OF_MEMORY, "mock test runtime error");
    }
    if (key == "wrongKey") {
        return Status(StatusCode::K_RUNTIME_ERROR, "ERROR MESSAGE");
    }
    return Status::OK();
}

std::string KVClient::Set(const StringView &val, const SetParam &setParam)
{
    if (val.data() == nullptr || val.size() == 0) {
        return "";
    }
    return "returnKey";
}

Status KVClient::GenerateKey(const std::string &prefixKey, std::string &key)
{
    return Status::OK();
}

Status KVClient::Get(const std::string &key, std::string &val, int32_t timeoutMs)
{
    if (key == "wrongKey") {
        return Status(StatusCode::K_RUNTIME_ERROR, "ERROR MESSAGE");
    }
    val = key;
    return Status::OK();
}

Status KVClient::Get(const std::vector<std::string> &keys, std::vector<Optional<ReadOnlyBuffer>> &readOnlyBuffers,
                        int32_t timeoutMs)
{
    // To test the if branch of partial get,
    // if a vector of len = 1, successfully get
    // if a vector of len > 1, only store the first element
    readOnlyBuffers.clear();
    auto buf = std::make_shared<Buffer>();
    auto rdBuf = std::make_shared<ReadOnlyBuffer>(buf);
    if (keys.size() == 1) {
        if (keys[0] == "wrongKey") {
            readOnlyBuffers.emplace_back();
            return Status(StatusCode::K_OUT_OF_MEMORY, "mock test runtime error");
        }
        readOnlyBuffers.emplace_back(std::move(*rdBuf));
        Status rt = Status::OK();
        return rt;
    }
    // if length >= 2, only store the first object
    readOnlyBuffers.emplace_back(std::move(*rdBuf));  // only first one is non-empty
    for (size_t i = 1; i < keys.size(); ++i) {        // others are empty
        readOnlyBuffers.emplace_back();
    }
    return Status(StatusCode::K_OUT_OF_MEMORY, "mock test runtime error");
}

Status KVClient::Get(const std::vector<std::string> &keys, std::vector<std::string> &vals, int32_t timeoutMs)
{
    char v = 'v';
    vals.emplace_back(reinterpret_cast<const char *>(&v), 1);
    return Status::OK();
}

Status KVClient::Del(const std::string &key)
{
    if (key == "wrongKey") {
        return Status(StatusCode::K_RUNTIME_ERROR, "ERROR MESSAGE");
    }
    return Status::OK();
}

Status KVClient::Del(const std::vector<std::string> &keys, std::vector<std::string> &failedKeys)
{
    std::string wrongKey = "wrongKey";
    auto it = std::find(keys.begin(), keys.end(), wrongKey);
    if (it != keys.end()) {
        failedKeys.emplace_back(wrongKey);
        return Status(StatusCode::K_RUNTIME_ERROR, "ERROR MESSAGE");
    }
    return Status::OK();
}

Status KVClient::ShutDown()
{
    return Status::OK();
}

Status::Status() noexcept : code_(StatusCode::K_OK) {}

Status::Status(StatusCode code, std::string msg)
{
    code_ = code;
    errMsg_ = msg;
}

std::string Status::ToString() const
{
    return "code: [" + std::to_string(code_) + "], msg: [" + errMsg_ + "]";
}

StatusCode Status::GetCode() const
{
    return code_;
}

Status &Status::operator=(const Status &other)
{
    if (this == &other) {
        return *this;
    }
    code_ = other.code_;
    errMsg_ = other.errMsg_;
    return *this;
}

Status::Status(Status &&other) noexcept
{
    code_ = other.code_;
    errMsg_ = std::move(other.errMsg_);
}

Status &Status::operator=(Status &&other) noexcept
{
    if (this == &other) {
        return *this;
    }
    code_ = other.code_;
    errMsg_ = std::move(other.errMsg_);
    return *this;
}

SensitiveValue::SensitiveValue(char const *) {}

SensitiveValue::~SensitiveValue() {}

SensitiveValue &SensitiveValue::operator=(const std::string &str)
{
    return *this;
}

Buffer::Buffer(Buffer &&other) noexcept {}

Buffer::~Buffer() {}

class HeteroClientImpl {
};

HeteroClient::HeteroClient(const ConnectOptions &connectOptions) {}

HeteroClient::~HeteroClient() {}

Status HeteroClient::Init()
{
    return Status::OK();
}

Status HeteroClient::ShutDown()
{
    return Status::OK();
}

Status HeteroClient::MGetH2D(const std::vector<std::string> &keys, const std::vector<DeviceBlobList> &devBlobList,
                          std::vector<std::string> &failedKeys, int32_t subTimeoutMs)
{
    return Status::OK();
}

Status HeteroClient::Delete(const std::vector<std::string> &objectIds, std::vector<std::string> &failedObjectIds)
{
    return Status::OK();
}

Status HeteroClient::DevLocalDelete(const std::vector<std::string> &objectIds, std::vector<std::string> &failedObjectIds)
{
    return Status::OK();
}

Status HeteroClient::DevSubscribe(const std::vector<std::string> &keys, const std::vector<DeviceBlobList> &blob2dList,
                                  std::vector<Future> &futureVec)
{
    std::promise<Status> promise;
    auto future = promise.get_future().share();
    std::shared_ptr<AclRtEventWrapper> event;
    Future f(future, event, "obj1");
    futureVec.emplace_back(f);
    return Status::OK();
}

Status HeteroClient::DevPublish(const std::vector<std::string> &keys, const std::vector<DeviceBlobList> &blob2dList,
                                std::vector<Future> &futureVec)
{
    std::promise<Status> promise;
    auto future = promise.get_future().share();
    std::shared_ptr<AclRtEventWrapper> event;
    Future f(future, event, "obj1");
    futureVec.emplace_back(f);
    return Status::OK();
}

Status HeteroClient::DevMSet(const std::vector<std::string> &keys, const std::vector<DeviceBlobList> &blob2dList,
                             std::vector<std::string> &failedKeys)
{
    return Status::OK();
}

Status HeteroClient::DevMGet(const std::vector<std::string> &keys, const std::vector<DeviceBlobList> &devBlobList,
                             std::vector<std::string> &failedKeys, int32_t subTimeoutMs)
{
    return Status::OK();
}
}  // namespace datasystem
