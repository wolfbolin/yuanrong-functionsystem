#!/usr/bin/env python3
# coding=UTF-8
# Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
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

"""function handler"""

import traceback
import inspect
from typing import List, Tuple

from yr.libruntime_pb2 import InvokeType

import yr
from yr import log
from yr.code_manager import CodeManager
from yr.common.utils import err_to_str
from yr.err_type import ErrorCode, ErrorInfo, ModuleCode
from yr.exception import YRInvokeError
from yr.executor.handler_intf import HandlerIntf
from yr.executor.instance_manager import InstanceManager
from yr.signature import recover_args

USER_SHUTDOWN_FUNC_NAME = "__yr_shutdown__"


class FunctionHandler(HandlerIntf):
    """
    Function handler class
    """

    def execute_function(
            self, func_meta, args: List, invoke_type, return_num: int,
            is_actor_async: bool) -> Tuple[List[object], ErrorInfo]:
        """execute function"""
        result = None
        try:
            if invoke_type == InvokeType.CreateInstance:
                self.__create_instance(func_meta, args)
            elif invoke_type == InvokeType.InvokeFunction:
                result = self.__invoke_instance(func_meta, args, is_actor_async)
            elif invoke_type == InvokeType.CreateInstanceStateless:
                self.__initialize_stateless_instance(func_meta)
            elif invoke_type == InvokeType.InvokeFunctionStateless:
                result = self.__invoke_stateless_function(func_meta, args)
            elif invoke_type == InvokeType.GetNamedInstanceMeta:
                result = InstanceManager().class_code
            else:
                msg = f"invalid invoke type {invoke_type}"
                log.get_logger().warning(msg)
                return [], ErrorInfo(ErrorCode.ERR_EXTENSION_META_ERROR, ModuleCode.RUNTIME, msg)
        except Exception as err:
            log.get_logger().warning("failed to execute user function, err: %s", err_to_str(err))
            if isinstance(err, YRInvokeError):
                result = [YRInvokeError(err.cause, traceback.format_exc())]
            else:
                result = [YRInvokeError(err, traceback.format_exc())]
            return result, ErrorInfo(ErrorCode.ERR_USER_FUNCTION_EXCEPTION, ModuleCode.RUNTIME,
                                     f"failed to execute user function, err: {repr(err)}")

        try:
            result_new = self.__check_return_list(result, return_num)
        except (TypeError, ValueError) as e:
            result_new = [YRInvokeError(e, traceback.format_exc())]
            log.get_logger().error("Errors found during checking return values list, error: %s", e)
            return result_new, ErrorInfo(ErrorCode.ERR_USER_FUNCTION_EXCEPTION, ModuleCode.RUNTIME,
                                         f"failed to execute user function, err: {repr(e)}")

        return result_new, ErrorInfo()

    def shutdown(self, grace_period_second: int) -> ErrorInfo:
        """shutdown"""
        log.get_logger().debug("Start to call user shutdown function __yr_shutdown__")
        instance = InstanceManager().instance()
        if instance is None:
            return ErrorInfo(
                ErrorCode.ERR_INNER_SYSTEM_ERROR,
                ModuleCode.RUNTIME,
                f"Failed to invoke instance function [{USER_SHUTDOWN_FUNC_NAME}], instance has not been initialized",
            )

        shutdown_func = None
        try:
            shutdown_func = getattr(instance, USER_SHUTDOWN_FUNC_NAME)
        except AttributeError:
            return ErrorInfo()
        try:
            shutdown_func(grace_period_second)
            log.get_logger().info("Succeeded to call user shutdown function __yr_shutdown__")
        except Exception as e:
            log.get_logger().exception(e)
            return ErrorInfo(ErrorCode.ERR_INNER_SYSTEM_ERROR, ModuleCode.RUNTIME, err_to_str(e))
        return ErrorInfo()

    def __create_instance(self, func_meta, args) -> None:
        class_code = CodeManager().load_code(func_meta, True)
        if class_code is None:
            raise RuntimeError(f"Failed to load code from data system, code id: [{func_meta.codeID}]")
        InstanceManager().class_code = class_code
        InstanceManager().set_code_ref(func_meta.codeID, False)
        InstanceManager().is_async = func_meta.isAsync

        args, kwargs = self.__get_param(args)
        instance = class_code(*args, **kwargs)
        InstanceManager().init(instance)

    def __invoke_instance(self, func_meta, args, is_actor_async) -> object:
        instance_function_name = func_meta.functionName
        args, kwargs = self.__get_param(args)

        instance = InstanceManager().instance()
        if instance is None:
            raise RuntimeError(
                f"Failed to invoke instance function [{instance_function_name}], instance has not been initialized")

        def sync_to_async(func):
            if inspect.iscoroutinefunction(func) or inspect.isasyncgenfunction(func):
                return func

            async def wrapper(*args, **kwargs):
                res = func(*args, **kwargs)
                if inspect.isawaitable(res):
                    return await res
                return res
            return wrapper

        func = getattr(instance, instance_function_name)
        if not func_meta.isAsync and is_actor_async:
            func = sync_to_async(func)
        return func(*args, **kwargs)

    def __initialize_stateless_instance(self, func_meta):
        if func_meta.initializerCodeID:
            initializer_code = CodeManager().load_code_from_datasystem(func_meta.initializerCodeID)
            initializer_code()

    def __invoke_stateless_function(self, func_meta, args):
        code = CodeManager().load_code(func_meta)
        if code is None:
            raise RuntimeError(f"Failed to load code from data system, code id: [{func_meta.codeID}]")

        args, kwargs = self.__get_param(args)
        result = code(*args, **kwargs)
        return result

    def __get_param(self, args: List) -> Tuple[object, object]:
        params = yr.serialization.Serialization().deserialize(args)
        args, kwargs = recover_args(params)
        return args, kwargs

    def __check_return_list(self, result, return_num: int):
        if isinstance(result, YRInvokeError) or return_num == 1:
            return [result] * return_num
        if return_num > 1:
            if not hasattr(result, "__len__"):
                raise TypeError(f"cannot unpack non-iterable {type(result)} object")
            if len(result) != return_num:
                raise ValueError(f"not enough values to unpack (expected {return_num}, got {len(result)})")
        return result
