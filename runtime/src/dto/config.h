/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
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
#include <iostream>
#include <sstream>
#include "boost/core/demangle.hpp"

#include "src/utility/logger/logger.h"

namespace YR {
namespace Libruntime {
const size_t REQUEST_ACK_TIMEOUT_SEC = 10;
const char *const TRUE_STR = "true";
const char *const FALSE_STR = "false";
const char *const TRUE_NUM = "1";
const char *const FALSE_NUM = "0";
const char *const INTEGRATED = "integrated";
const char *const STANDALONE = "standalone";
template <typename Type>
Type Cast(const char *key, const std::string &value)
{
    std::string str(value);
    std::string err = std::string("failed to parse ") + boost::core::demangle(typeid(Type).name()) + (" from ") + key +
                      ", get value: " + str;
    if (std::is_unsigned_v<Type>) {
        size_t first = str.find_first_not_of(' ');
        if (first > str.size() || str[first] == '-') {
            std::cerr << ">>>>" << str << "<<<<" << first << std::endl;
            throw std::invalid_argument(err);
        }
    }
    if (std::is_same_v<Type, bool>) {
        if (str == TRUE_STR) {
            str = TRUE_NUM;
        } else if (str == FALSE_STR) {
            str = FALSE_NUM;
        } else {
            std::cerr << ">>>>" << str << "<<<<" << FALSE_STR << std::endl;
            throw std::invalid_argument(err);
        }
    }
    std::istringstream iss(str);
    Type typeValue;
    iss >> typeValue;
    if (!iss.eof()) {
        std::cerr << ">>>>" << str << "<<<<"
                  << "eof" << std::endl;
        throw std::invalid_argument(err);
    }
    return typeValue;
}

class Config {
public:
    static Config &Instance();
#define CONFIG_DECLARE(type, name, default)            \
private:                                               \
    type name##_ = ParseFromEnv<type>(#name, default); \
                                                       \
public:                                                \
    inline type &name()                                \
    {                                                  \
        return name##_;                                \
    }

#define CONFIG_DECLARE_VALID(type, name, default, validator)      \
private:                                                          \
    type name##_ = ParseFromEnv<type>(#name, default, validator); \
                                                                  \
public:                                                           \
    inline type &name()                                           \
    {                                                             \
        return name##_;                                           \
    }

#define CONFIG_DECLARE_CONDITION(type, name, default)  \
private:                                               \
    type name##_ = ParseFromEnv<type>(#name, default); \
                                                       \
public:                                                \
    inline type &name()                                \
    {                                                  \
        return name##_;                                \
    }

    CONFIG_DECLARE_VALID(size_t, REQUEST_ACK_ACC_MAX_SEC, 1800,
                         [](const size_t &val) -> bool { return val >= REQUEST_ACK_TIMEOUT_SEC; });
    CONFIG_DECLARE_VALID(size_t, DS_CONNECT_TIMEOUT_SEC, 1800,
                         [](const size_t &val) -> bool { return val >= REQUEST_ACK_TIMEOUT_SEC; });
    CONFIG_DECLARE(bool, AUTH_ENABLE, false);
    CONFIG_DECLARE(std::string, GRPC_SERVER_ADDRESS, "0.0.0.0:0");
    CONFIG_DECLARE(int, IS_PRESTART, 1);
    CONFIG_DECLARE(std::string, DATASYSTEM_ADDR, "0.0.0.0:0");
    CONFIG_DECLARE(std::string, INSTANCE_ID, "");
    CONFIG_DECLARE(std::string, FUNCTION_NAME, "");
    CONFIG_DECLARE(std::string, FUNCTION_LIB_PATH, "/dcache/layer/func");
    CONFIG_DECLARE(std::string, GLOG_log_dir, "/home/snuser/log");
    CONFIG_DECLARE(std::string, SNLIB_PATH, "/home/snuser/snlib");
    CONFIG_DECLARE(std::string, YR_LOG_LEVEL, "INFO");
    CONFIG_DECLARE(std::string, YRFUNCID, "");
    CONFIG_DECLARE(std::string, YR_PYTHON_FUNCID, "");
    CONFIG_DECLARE(std::string, YR_JAVA_FUNCID, "");
    CONFIG_DECLARE(std::string, YR_DS_ADDRESS, "");
    CONFIG_DECLARE(std::string, YR_SERVER_ADDRESS, "");
    CONFIG_DECLARE(std::string, POSIX_LISTEN_ADDR, "");
    CONFIG_DECLARE(std::string, YR_LOG_PATH, "./");
    CONFIG_DECLARE(uint32_t, YR_MAX_LOG_SIZE_MB, 40);
    CONFIG_DECLARE(uint32_t, YR_MAX_LOG_FILE_NUM, 20);
    CONFIG_DECLARE(uint32_t, YR_HTTP_CONNECTION_NUM, 10);
    CONFIG_DECLARE(bool, YR_LOG_COMPRESS, true);
    CONFIG_DECLARE(std::string, HOST_IP, "");
    CONFIG_DECLARE(uint16_t, MAX_GRPC_SIZE, 10);  // value could be 1-500 MB
    CONFIG_DECLARE(uint64_t, GRACEFUL_SHUTDOWN_TIME, 60);
    CONFIG_DECLARE(uint64_t, STREAM_RECEIVE_LIMIT, 0);
    CONFIG_DECLARE(bool, ENABLE_METRICS, false);
    CONFIG_DECLARE(std::string, METRICS_CONFIG, "");
    CONFIG_DECLARE(std::string, METRICS_CONFIG_FILE, "");
    CONFIG_DECLARE(bool, ENABLE_DS_AUTH, false);
    CONFIG_DECLARE(bool, ENABLE_SERVER_AUTH, false);
    CONFIG_DECLARE(bool, ENABLE_SERVER_MODE, true);
    CONFIG_DECLARE(bool, YR_SSL_ENABLE, false);
    CONFIG_DECLARE(std::string, YR_SSL_ROOT_FILE, "");
    CONFIG_DECLARE(std::string, YR_SSL_CERT_FILE, "");
    CONFIG_DECLARE(std::string, YR_SSL_KEY_FILE, "");
    CONFIG_DECLARE(std::string, POD_NAME, "");
    CONFIG_DECLARE(std::string, HOSTNAME, "");
    CONFIG_DECLARE(std::string, YR_RUNTIME_ID, "");
    CONFIG_DECLARE(std::string, POD_IP, "");
    CONFIG_DECLARE(bool, RUNTIME_DIRECT_CONNECTION_ENABLE, false);
    CONFIG_DECLARE(int, DERICT_RUNTIME_SERVER_PORT, 0);
    CONFIG_DECLARE(bool, YR_ENABLE_HTTP_PROXY, false);
    CONFIG_DECLARE_CONDITION(int, MAX_ARGS_IN_MSG_BYTES, [this]() -> int {
        return RUNTIME_DIRECT_CONNECTION_ENABLE() ? 10 * 1024 * 1024 : 100 * 1024;
    });
    CONFIG_DECLARE(std::string, YR_TENANT_ID, "");
    CONFIG_DECLARE(int64_t, DS_DELAY_FLUSH_TIME, 0);
    CONFIG_DECLARE(size_t, MEM_STORE_SIZE_THRESHOLD, 100 * 1024);
    CONFIG_DECLARE(size_t, FASS_SCHEDULE_TIMEOUT, 120);  // 120 seconds
    CONFIG_DECLARE(int, YR_ASYNCIO_MAX_CONCURRENCY, 1000); // 1k
    CONFIG_DECLARE_VALID(std::string, RUN_MODE, "integrated",  // integrated or standalone
        [](const std::string &val) -> bool { return (val == INTEGRATED || val == STANDALONE); });
    CONFIG_DECLARE(bool, ENABLE_FUNCTION_SCHEDULER, false);  // whether start an in-memory scheduler
    CONFIG_DECLARE(int, FUNCTION_SCHEDULER_GRPC_PORT, 23770); // allow scheduler to interact with runtime
    CONFIG_DECLARE(int, FUNCTION_SCHEDULER_HTTP_PORT, 23771);  // allow http access to function scheduler
    CONFIG_DECLARE(std::string, NODE_ID, "");  // allow http access to function scheduler

public:
    bool IsRunModeStandalone()
    {
        return (RUN_MODE() == STANDALONE);
    }

private:
    static Config c;
    template <typename Type>
    Type ParseFromEnv(
        const char *key, Type defaultValue,
        std::function<bool(const Type &)> validator = [](const Type &) { return true; })
    {
        auto value = std::getenv(key);
        if (value == nullptr) {
            return defaultValue;
        }
        Type result = Cast<Type>(key, value);
        if (!validator(result)) {
            return defaultValue;
        }
        return result;
    }

    template <typename Type>
    Type ParseFromEnv(const char *key, std::function<Type()> defaultCondition)
    {
        auto value = std::getenv(key);
        if (value == nullptr) {
            return defaultCondition();
        }
        Type result = Cast<Type>(key, value);
        return result;
    }
};
}  // namespace Libruntime
}  // namespace YR
