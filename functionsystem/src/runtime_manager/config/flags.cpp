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

#include "flags.h"

#include <climits>

#include "common/utils/exec_utils.h"
#include "param_check.h"
#include "utils/os_utils.hpp"

namespace functionsystem::runtime_manager {

const static std::string DEFAULT_RUNTIME_PATH = "/home/snuser";
const static double DEFAULT_METRICS_CPU = 1000;
const double MIN_METRICS_CPU = 0;
const double MAX_METRICS_CPU = 1000000;

const static double DEFAULT_METRICS_MEMORY = 4000;
const double MIN_METRICS_MEMORY = 0;
const double MAX_METRICS_MEMORY = 1024 * 1024 * 1024;

const static int DEFAULT_PORT_NUM = 65535;
const int MIN_PORT_NUM = 10;
const static std::string DEFAULT_DATASYSTEM_PORT = "31501";

const static int DISK_USAGE_MONITOR_DURATION = 20;  // ms
const int MIN_DISK_USAGE_MONITOR_DURATION = 10;     // ms
const int MAX_DISK_USAGE_MONITOR_DURATION = 60000;  // ms

const int MAX_DISK_LIMIT = 1024 * 1024;
const int DEFAULT_MAX_LOG_SIZE_MB = 40;
const int DEFAULT_MAX_LOG_FILE_NUM = 20;

const uint32_t DEFAULT_RUNTIME_DS_CONNECT_TIMEOUT = 1800; // s

const int MIN_MEMORY_DETECTION_INTERVAL = 100; // ms

const int KILL_PROCESS_TIMEOUT_SECONDS = 0; // s
using namespace litebus::flag;

void Flags::AddOomFlags()
{
    AddFlag(&Flags::memoryDetectionInterval_, "memory_detection_interval",
            "memory detection interval for runtime process, unit in mili seconds, default is 1000 ms, min is 100 ms",
            DEFAULT_MEMORY_DETECTION_INTERVAL, NumCheck(MIN_MEMORY_DETECTION_INTERVAL, INT_MAX));
    AddFlag(&Flags::oomKillEnable_, "oom_kill_enable", "enable runtime oom kill base on process memory usage", false);
    AddFlag(&Flags::oomKillControlLimit_, "oom_kill_control_limit",
            "configure the control limit for the runtime OOM kill based on process memory usage, unit is MB.", 0);
    AddFlag(&Flags::oomConsecutiveDetectionCount_, "oom_consecutive_detection_count",
            "number of consecutive times the memory usage must exceed the control limit before triggering OOM kill",
            DEFAULT_OOM_CONSECUTIVE_DETECTION_COUNT, NumCheck(1, INT_MAX));
}

void Flags::AddDiskUsageMonitorFlags()
{
    AddFlag(&Flags::diskUsageMonitorNotifyFailureEnable_, "disk_usage_monitor_notify_failure_enable",
            "whether enable disk usage monitor notify instances failure first", false);
    AddFlag(&Flags::diskUsageMonitorPath_, "disk_usage_monitor_path", "disk usage monitor path", std::string("/tmp"));
    AddFlag(&Flags::diskUsageLimit_, "disk_usage_limit", "disk usage limit(MB)", -1, NumCheck(-1, MAX_DISK_LIMIT));
    AddFlag(&Flags::snuserDirSizeLimit_, "snuser_disk_usage_limit", "disk usage limit(MB) for /home/snuser", -1,
            NumCheck(-1, MAX_DISK_LIMIT));
    AddFlag(&Flags::tmpDirSizeLimit_, "tmp_disk_usage_limit", "disk usage limit(MB) for tmp dir", -1,
            NumCheck(-1, MAX_DISK_LIMIT));
    AddFlag(&Flags::diskUsageMonitorDuration_, "disk_usage_monitor_duration", "disk usage monitor duration",
            DISK_USAGE_MONITOR_DURATION, NumCheck(MIN_DISK_USAGE_MONITOR_DURATION, MAX_DISK_USAGE_MONITOR_DURATION));
}

void Flags::AddConfigFlags()
{
    AddFlag(&Flags::runtimeHomeDir_, "runtime_home_dir", "runtime home dir", litebus::os::GetEnv("HOME").Get());
    AddFlag(&Flags::nodeJsEntryPath_, "nodejs_entry", "node js entry path", "/home/snuser/runtime/nodejs/wrapper.js");
    AddFlag(&Flags::resourceLabelPath_, "resource_label_path", "resource label path", "/home/sn/podInfo/labels");
    AddFlag(&Flags::npuDeviceInfoPath_, "npu_device_info_path", "npu device info file config path",
            "/home/sn/config/topology-info.json");
    AddFlag(&Flags::runtimeDsConnectTimeout_, "runtime_ds_connect_timeout",
            "runtime ds-client connection timeout in second", DEFAULT_RUNTIME_DS_CONNECT_TIMEOUT,
            NumCheck(static_cast<uint32_t>(1), UINT_MAX));
}

Flags::Flags()
{
    AddFlag(&Flags::runtimePath_, "runtime_dir", "init runtime dir for runtimes", DEFAULT_RUNTIME_PATH, RealPath());
    AddFlag(&Flags::runtimeLogsPath_, "runtime_logs_dir", "init runtime logs dir for runtimes", DEFAULT_RUNTIME_PATH,
            RealPath());
    AddFlag(&Flags::runtimeStdLogDir_, "runtime_std_log_dir", "runtime std log dir", "");
    AddFlag(&Flags::runtimeLdLibraryPath_, "runtime_ld_library_path", "init runtime logs dir for runtimes",
            std::string(), FlagCheckWrraper(CheckIllegalChars));
    AddFlag(&Flags::runtimePrestartConfig_, "runtime_prestart_config", "runtime prestart configuration", "{}");
    AddFlag(&Flags::runtimeDefaultConfig_, "runtime_default_config", "runtime default configuration", "{}");
    AddFlag(&Flags::runtimeLogLevel_, "runtime_log_level", "init runtime log level", "DEBUG");
    AddFlag(&Flags::runtimeMaxLogSize_, "runtime_max_log_size", "runtime max log size threashold",
            DEFAULT_MAX_LOG_SIZE_MB);
    AddFlag(&Flags::runtimeMaxLogFileNum_, "runtime_max_log_file_num", "runtime max file number to reserve",
            DEFAULT_MAX_LOG_FILE_NUM);
    AddFlag(&Flags::setCmdCred_, "setCmdCred", "init runtime path for runtimes", false);
    AddFlag(&Flags::pythonDependencyPath_, "python_dependency_path", "init runtime path for runtimes", "/");
    AddFlag(&Flags::pythonLogConfigPath_, "python_log_config_path", "init python log config for runtimes",
            DEFAULT_RUNTIME_PATH + "/config/python-runtime-log.json");
    AddFlag(&Flags::javaSystemProperty_, "java_system_property", "init java system property for runtimes",
            "-Dlog4j2.configurationFile=file:" + DEFAULT_RUNTIME_PATH + "/runtime/java/log4j2.xml");
    AddFlag(&Flags::javaSystemLibraryPath_, "java_system_library_path", "java library path for libruntime",
            DEFAULT_RUNTIME_PATH + "/runtime/java/lib");
    AddFlag(&Flags::logConfig_, "log_config", "Json config file used for log initialization.",
            "{\"filepath\": \"/home/yr/log\",\"level\": \"DEBUG\",\"rolling\": {\"maxsize\": 100, \"maxfiles\": 1},"
            "\"alsologtostderr\":true}");
    AddFlag(&Flags::nodeId_, "node_id", "vm id");
    AddFlag(&Flags::ip_, "ip", "IP address to listen on.", true, FlagCheckWrraper(IsIPValid));
    AddFlag(&Flags::hostIP_, "host_ip", "host IP address.", true, FlagCheckWrraper(IsIPValid));
    AddFlag(&Flags::proxyIP_, "proxy_ip", "proxy IP address.", "");
    AddFlag(&Flags::port_, "port", "For posix server listening. example: 8080.", true, FlagCheckWrraper(IsPortValid));
    AddFlag(&Flags::agentAddress_, "agent_address", "for receiving function agent message", true,
            FlagCheckWrraper(IsAddressesValid));
    AddFlag(&Flags::initialPort_, "runtime_initial_port", "for init port manager", true, NumCheck(0, DEFAULT_PORT_NUM));
    AddFlag(&Flags::portNum_, "port_num", "for init port manager", DEFAULT_PORT_NUM,
            NumCheck(MIN_PORT_NUM, DEFAULT_PORT_NUM));
    AddFlag(&Flags::metricsCollectorType_, "metrics_collector_type", "set metrics collector type", "proc");
    AddFlag(&Flags::procMetricsCPU_, "proc_metrics_cpu", "init proc metrics cpu", DEFAULT_METRICS_CPU,
            NumCheck(MIN_METRICS_CPU, MAX_METRICS_CPU));
    AddFlag(&Flags::procMetricsMemory_, "proc_metrics_memory", "init proc metrics memory", DEFAULT_METRICS_MEMORY,
            NumCheck(MIN_METRICS_MEMORY, MAX_METRICS_MEMORY));
    AddFlag(&Flags::dataSystemPort_, "data_system_port", "init data system port", DEFAULT_DATASYSTEM_PORT,
            FlagCheckWrraper(IsPortValid));
    AddFlag(&Flags::driverServerPort_, "driver_server_port", "driver server port", std::string("22773"),
            FlagCheckWrraper(IsPortValid));
    AddDiskUsageMonitorFlags();
    AddFlag(&Flags::runtimeConfigPath_, "runtime_config_dir", "runtime config dir", "/home/snuser/config");
    AddFlag(&Flags::proxyGrpcServerPort_, "proxy_grpc_server_port", "function proxy grpc server port",
            std::string("22773"), FlagCheckWrraper(IsPortValid));
    AddFlag(&Flags::runtimeUID_, "runtime_uid", "runtime user id", DEFAULT_USER_ID);
    AddFlag(&Flags::runtimeGID_, "runtime_gid", "runtime group id", DEFAULT_GROUP_ID);
    AddFlag(&Flags::npuCollectionMode_, "npu_collection_mode", "npu collect mode", "all");
    AddFlag(&Flags::gpuCollectionEnable_, "gpu_collection_enable", "enable gpu collection", false);
    AddFlag(&Flags::isProtoMsgToRuntime_, "is_protomsg_to_runtime", "", false);
    AddFlag(&Flags::massifEnable_, "massif_enable", "valgrind massif enable", false);
    AddFlag(&Flags::inheritEnv_, "enable_inherit_env", "enable runtime to inherit env from runtime-manager", false);
    AddFlag(&Flags::logExpirationEnable_, "log_expiration_enable", "enable runtime log expiration", false);
    AddFlag(&Flags::logExpirationCleanupInterval_, "log_expiration_cleanup_interval",
            "Check the time interval for expired logs, unit in seconds, default is 10 minutes",
            DEFAULT_LOG_EXPIRATION_CLEANUP_INTERVAL, NumCheck(0, INT_MAX));
    AddFlag(&Flags::logExpirationTimeThreshold_, "log_expiration_time_threshold",
            "The maximum retention time for expired log files, in seconds, is 5 days by default",
            DEFAULT_LOG_EXPIRATION_TIME_THRESHOLD, NumCheck(0, INT_MAX));
    AddFlag(&Flags::logExpirationMaxFileCount_, "log_expiration_max_file_count",
            "The maximum number of expired log files to be retained, in units of pieces",
            DEFAULT_LOG_EXPIRATION_MAX_FILE_COUNT, NumCheck(0, INT_MAX));
    AddFlag(&Flags::customResources_, "custom_resources",
            "Json format for custom defined resource. etc: \'{\"CustomResource\": 4, \"CustomResource2\": 8}\'", "");
    AddFlag(&Flags::separatedRedirectRuntimeStd_, "enable_separated_redirect_runtime_std",
            "enable to redirect standard output of runtime separated. etc. {runtimeID}.out {runtimeID}.err", false);
    AddFlag(&Flags::runtimeDirectConnectionEnable_, "runtime_direct_connection_enable",
            "enable direct runtime connection will allocate a server port for runtime", false);
    AddOomFlags();
    AddConfigFlags();
    AddFlag(&Flags::killProcessTimeoutSeconds_, "kill_process_timeout_seconds",
            "the time interval send kill -9 after send kill -2, unit in seconds, default is 5 seconds",
            KILL_PROCESS_TIMEOUT_SECONDS, NumCheck(static_cast<uint32_t>(0), UINT_MAX));
    AddFlag(&Flags::overheadCPU_, "overhead_cpu", "Overhead node CPU resource (Only metrics type = node)", 0.0);
    AddFlag(&Flags::overheadMemory_, "overhead_memory", "Overhead node MEM resource (Only metrics type = node)", 0.0);
    AddFlag(&Flags::runtimeInstanceDebugEnable_, "runtime_instance_debug_enable", "runtime instance debug enable",
            false);
    AddFlag(&Flags::userLogExportMode_, "user_log_export_mode", "user log export mode: std/file", "file");
}
}  // namespace functionsystem::runtime_manager
