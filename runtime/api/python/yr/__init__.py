#!/usr/bin/env python3
# coding=UTF-8
# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
yr api
"""

__all__ = [
    "init", "finalize", "Config", "UserTLSConfig",
    "put", "get",
    "wait", "cancel", "invoke", "instance", "method", "InvokeOptions", "exit",
    "Context", "GetParam", "GetParams",
    "Affinity", "AffinityType", "AffinityKind", "LabelOperator", "OperatorType",
    "kv_read", "kv_write", "kv_set", "kv_get", "kv_get_with_param", "kv_del", "kv_m_write_tx",
    "ExistenceOpt", "WriteMode", "CacheType", "SetParam", "MSetParam", "CreateParam", "ConsistencyType",
    "save_state", "load_state", "get_instance", "is_initialized",
    "Gauge", "Alarm", "java_instance_class", "go_instance_class", "create_function_group",
    "AlarmSeverity", "AlarmInfo", "UInt64Counter", "DoubleCounter",
    "FunctionGroupOptions", "SchedulingAffinityType", "FunctionGroupContext", "ServerInfo", "DeviceInfo",
    "get_function_group_context", "create_resource_group", "remove_resource_group", "ResourceGroup",
    "FunctionProxy", "InstanceCreator", "InstanceProxy", "MethodProxy", "FunctionGroupHandler",
    "FunctionGroupMethodProxy", "get_node_ip_address", "list_named_instances"
]

import os
import ctypes

for so_path in [
    "libsecurec.so",  # securec must before libdatasystem
    "libtbb.so.2",
    "libssl.so.1.1",
    "libds-spdlog.so.1.12.0",
    "libzmq.so.5.2.5",
    "libcrypto.so.1.1",
    "libaddress_sorting.so.42.0.0",
    "libdatasystem.so",
    "libspdlog.so.1.12.0",
    "libyrlogs.so",
    "liblitebus.so.0.0.1",
    "libobservability-metrics-exporter-ostream.so",
    "libobservability-metrics-file-exporter.so",
    "libobservability-prometheus-push-exporter.so",
    "libobservability-metrics.so",
    "libobservability-metrics-sdk.so",
]:
    # load current dir's
    so_path = os.path.join(os.path.dirname(
        os.path.realpath(__file__)), so_path)
    try:
        ctypes.cdll.LoadLibrary(so_path)
    except OSError:
        pass

# E402: import not at top of file
# We must load so before import datasystem, so the lint is not really useful
from yr.apis import (  # noqa: E402
    init, finalize,
    put, get,
    invoke, instance, wait, cancel, method, exit,
    kv_read, kv_write, kv_del, kv_set, kv_get, kv_get_with_param,
    kv_m_write_tx, kv_write_with_param, get_instance, is_initialized, save_state, load_state,
    cpp_function, java_function, go_function, cpp_instance_class, java_instance_class,
    go_instance_class, resources, create_resource_group, remove_resource_group, get_node_ip_address,
    list_named_instances
)
from yr.fcc import (  # noqa: E402
    create_function_group, get_function_group_context
)
from yr.resource_group import ResourceGroup  # noqa: E402
from yr.runtime import (  # noqa: E402
    ExistenceOpt, WriteMode, CacheType, SetParam, MSetParam, CreateParam, AlarmSeverity, AlarmInfo,
    ConsistencyType, GetParams, GetParam
)
from yr.config import (  # noqa: E402
    Config, InvokeOptions, UserTLSConfig, FunctionGroupOptions, SchedulingAffinityType,
    FunctionGroupContext, ServerInfo, DeviceInfo, ResourceGroupOptions
)

from yr.affinity import Affinity, AffinityType, AffinityKind, LabelOperator, OperatorType  # noqa: E402
from yr.metrics import Gauge, Alarm, UInt64Counter, DoubleCounter  # noqa: E402
from yr.decorator.function_proxy import FunctionProxy  # noqa: E402
from yr.decorator.instance_proxy import (  # noqa: E402
    InstanceCreator, InstanceProxy, MethodProxy, FunctionGroupHandler, FunctionGroupMethodProxy
)
