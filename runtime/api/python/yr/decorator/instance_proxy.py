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
Instance decorator
"""

import inspect
import logging
import os
import threading
import weakref
import uuid
from typing import List
import yr
from yr import signature
from yr.code_manager import CodeManager
from yr.common import constants, utils
from yr.common.types import GroupInfo
from yr.config import InvokeOptions, function_group_enabled
from yr.libruntime_pb2 import FunctionMeta, LanguageType
from yr.object_ref import ObjectRef
from yr.runtime_holder import global_runtime, save_real_instance_id
from yr.serialization import register_pack_hook, register_unpack_hook
from yr.accelerate.shm_broadcast import MessageQueue, STOP_EVENT

_logger = logging.getLogger(__name__)


class InstanceCreator:
    """
    User instance creator.
    """

    def __init__(self):
        """
        Initialize the InstanceCreator instance.
        """
        self.__user_class__ = None
        self.__user_class_descriptor__ = None
        self.__user_class_methods__ = {}
        self.__base_cls__ = None
        self.__target_function_key__ = None
        self._code_ref = None
        self._lock = threading.Lock()
        self.__invoke_options__ = InvokeOptions()
        self.__is_async__ = False
        self.function_group_size = 0

    def __getstate__(self):
        attrs = self.__dict__.copy()
        del attrs["_lock"]
        del attrs["_code_ref"]
        return attrs

    def __setstate__(self, state):
        self.__dict__.update(state)
        self.__dict__["_lock"] = threading.Lock()
        self.__dict__["_code_ref"] = None

    @classmethod
    def create_from_user_class(cls, user_class, invoke_options):
        """
        Create from user class.

        Args:
            user_class (class): The user class.
            invoke_options (InvokeOptions): The invoke options.

        Returns:
            The InstanceCreator object itself. Data type is InstanceCreator.
        """

        class DerivedInstanceCreator(cls, user_class):
            """derived instance creator"""
            pass

        name = f"YRInstance({user_class.__name__})"
        DerivedInstanceCreator.__module__ = user_class.__module__
        DerivedInstanceCreator.__name__ = name
        DerivedInstanceCreator.__qualname__ = name
        self = DerivedInstanceCreator.__new__(DerivedInstanceCreator)
        self.__user_class__ = user_class
        if invoke_options is not None:
            self.__invoke_options__ = invoke_options
        else:
            self.__invoke_options__ = InvokeOptions()
        class_methods = inspect.getmembers(user_class,
                                           utils.is_function_or_method)
        self.__is_async__ = len([
            name for name, method in class_methods
            if inspect.iscoroutinefunction(method) or inspect.isasyncgenfunction(method)
        ]) > 0
        if self.__invoke_options__.concurrency == 1 and not self.__is_async__:
            self.__invoke_options__.need_order = True
        self.__user_class_descriptor__ = utils.ObjectDescriptor.get_from_class(user_class)
        self.__user_class_descriptor__.target_language = LanguageType.Python
        self.__target_function_key__ = ""
        self.__user_class_methods__ = dict(class_methods)
        self.__base_cls__ = inspect.getmro(user_class)
        self._code_ref = None
        self._lock = threading.Lock()
        self.function_group_size = 0
        return self

    @classmethod
    def create_cpp_user_class(cls, cpp_class):
        """
        Create a cpp user class.

        Args:
            cpp_class (class): The cpp class.

        Returns:
            The InstanceCreator object itself. Data type is InstanceCreator.
        """
        self = cls()
        self.__user_class_descriptor__ = utils.ObjectDescriptor(class_name=cpp_class.get_class_name(),
                                                                function_name=cpp_class.get_factory_name(),
                                                                target_language=LanguageType.Cpp)
        self.__user_class_methods__ = None
        self.__target_function_key__ = cpp_class.get_function_key()
        return self

    @classmethod
    def create_cross_user_class(cls, user_class):
        """
        Create a java user class

        Args:
            user_class (class): The java class.

        Returns:
            The InstanceCreator object itself. Data type is InstanceCreator.
        """
        self = cls()
        self.__user_class_descriptor__ = utils.ObjectDescriptor(class_name=user_class.class_name,
                                                                target_language=user_class.target_language)
        self.__user_class_methods__ = None
        self.__target_function_key__ = user_class.function_key
        return self

    def options(self, *args, **kwargs):
        """
        Options YR.

        Args:
            *args: Variable position parameters. YR mode is triggered only when a single parameter is of type
                InvokeOptions.
            **kwargs: Variable keyword arguments. Pass execution parameters.

        Returns:
            Instance option wrapper.
        """
        if (len(args) == 1 and isinstance(args[0], InvokeOptions)):
            return self._options_yr(args[0])
        return self._options_wrapper(**kwargs)

    def set_function_group_size(self, function_group_size: int):
        """
        Set function group size.

        Args:
            function_group_size (int): The function group size.

        Returns:
            The InstanceCreator object itself. Data type is InstanceCreator.
        """
        self.function_group_size = function_group_size

        return self

    def get_original_cls(self):
        """
        Get the original class.

        Returns:
           The original user class type before encapsulation.
        """
        return self.__user_class__

    def invoke(self, *args, **kwargs):
        """
        Create an instance in cluster.

        Args:
            *args: Variable arguments, used to pass non-keyword arguments.
            **kwargs: Variable arguments, used to pass keyword arguments.

        Returns:
           InstanceProxy.
        """
        return self._invoke(args=args, kwargs=kwargs)

    def get_instance(self, name, *args, **kwargs):
        """
        Get an instance in cluster.

        Args:
            name (str): The instance name.
            *args: Variable arguments, used to pass non-keyword arguments.
            **kwargs: Variable arguments, used to pass keyword arguments.

        Returns:
            InstanceProxy.
        """
        return self._invoke(name=name, args=args, kwargs=kwargs)

    def _inner_create_instance(self, invoke_options, function_id, name, args_list, group_info):
        func_meta = FunctionMeta(functionID=function_id,
                                 moduleName=self.__user_class_descriptor__.module_name,
                                 className=self.__user_class_descriptor__.class_name,
                                 functionName=self.__user_class_descriptor__.function_name,
                                 language=self.__user_class_descriptor__.target_language,
                                 codeID=self._code_ref.id if self._code_ref is not None else "",
                                 name=name if name is not None else invoke_options.name,
                                 ns=invoke_options.namespace,
                                 isAsync=self.__is_async__)
        runtime = global_runtime.get_runtime()
        if invoke_options.name:
            try:
                runtime.get_instance_by_name(invoke_options.name, invoke_options.namespace, timeout=30)
            except Exception as _:
                pass

        instance_id = runtime.create_instance(func_meta=func_meta,
                                              args=args_list,
                                              opt=invoke_options,
                                              group_info=group_info)
        return instance_id

    def _invoke(self, name=None, args=None, kwargs=None, invoke_options=None):
        if invoke_options is None:
            invoke_options = self.__invoke_options__
        invoke_options.check_options_valid()
        is_cross_invoke = self.__user_class_descriptor__.target_language != LanguageType.Python
        with self._lock:
            if not is_cross_invoke and (
                    self._code_ref is None
                    or not global_runtime.get_runtime().is_object_existing_in_local(self._code_ref.id)
            ):
                self._code_ref = yr.put(self.__user_class__)
                _logger.info("[Reference Counting] put code with id = %s, className = %s",
                             self._code_ref.id, self.__user_class_descriptor__.class_name)
        # __init__ existed when user-defined
        if self.__user_class_methods__ is not None and '__init__' in self.__user_class_methods__:
            sig = signature.get_signature(self.__user_class_methods__.get('__init__'),
                                          ignore_first=True)
        else:
            sig = None
        function_id = ""
        if self.__user_class_descriptor__.target_language == LanguageType.Python:
            args_list = signature.package_args(sig, args, kwargs)
        else:
            args_list = utils.make_cross_language_args(args, kwargs)
            function_id = self.__target_function_key__
        group_info = None
        if function_group_enabled(invoke_options.function_group_options, self.function_group_size):
            group_info = GroupInfo()
            group_info.group_name = global_runtime.get_runtime().generate_group_name()
            group_info.group_size = self.function_group_size

        instance_id = self._inner_create_instance(invoke_options, function_id, name, args_list, group_info)
        group_name = "" if group_info is None else group_info.group_name
        return InstanceProxy(instance_id, self.__user_class_descriptor__,
                             self.__user_class_methods__,
                             self.__base_cls__, function_id,
                             invoke_options.need_order, group_name,
                             self.__is_async__,
                             name if name is not None else invoke_options.name,
                             invoke_options.namespace, self._code_ref)

    def _options_wrapper(self, **actor_options):
        """
        options wrapper, Set user invoke options
        """
        name = actor_options.get("name")
        namespace = actor_options.get("namespace")
        if name is not None:
            if not isinstance(name, str):
                raise TypeError(
                    f"name must be None or a string, got: '{type(name)}'.")
            if name == "":
                raise ValueError("stateful function name cannot be an empty string.")
        if namespace is not None:
            if not isinstance(namespace, str):
                raise TypeError("namespace must be None or a string.")
            if namespace == "":
                raise ValueError('"" is not a valid namespace. '
                                 "Pass None to not specify a namespace.")

        self.__invoke_options__.name = name
        self.__invoke_options__.namespace = namespace
        return self._options_yr(self.__invoke_options__)

    def _options_yr(self, invoke_options: InvokeOptions):
        """
        Set user invoke options
        Args:
            invoke_options: invoke options for users to set resources
        """
        instance_cls = self
        invoke_options.check_options_valid()
        if invoke_options.concurrency == 1 and not self.__is_async__:
            invoke_options.need_order = True
        else:
            invoke_options.need_order = False

        class InstanceOptionWrapper:
            """instance option wrapper"""

            def invoke(self, *args, **kwargs):
                """invoke"""
                return instance_cls._invoke(args=args,
                                            kwargs=kwargs,
                                            invoke_options=invoke_options)

            def get_instance(self, name: str, *args, **kwargs):
                """
                Create an instance in cluster
                name: str, the instance name
                """
                return instance_cls._invoke(name=name,
                                            args=args,
                                            kwargs=kwargs,
                                            invoke_options=invoke_options)

        return InstanceOptionWrapper()


class InstanceProxy:
    """
    Use to decorate a user class.
    """

    def __init__(self,
                 instance_id,
                 class_descriptor,
                 class_methods,
                 base_cls,
                 function_id,
                 need_order=True,
                 group_name="",
                 is_async=False,
                 instance_name=None,
                 namespace=None,
                 code_ref=None):
        """
        Initialize the InstanceProxy instance.
        """
        self._class_descriptor = class_descriptor
        self.instance_id = instance_id
        self._class_methods = class_methods
        self._base_cls = base_cls
        self._method_descriptor = {}
        self.__instance_activate__ = True
        self._function_id = function_id
        self.need_order = need_order
        self.group_name = group_name
        self._is_async = is_async
        self._instance_name = instance_name
        self._ns = namespace
        self._code_ref = code_ref


        if self._class_methods is not None:
            for method_name, value in self._class_methods.items():
                is_async_ = False if inspect.isgeneratorfunction(value) else self._is_async
                function_descriptor = utils.ObjectDescriptor(
                    module_name=self._class_descriptor.module_name,
                    function_name=method_name,
                    class_name=self._class_descriptor.class_name,
                    is_generator=inspect.isgeneratorfunction(value) or inspect.isasyncgenfunction(value),
                    is_async=is_async_)
                self._method_descriptor[method_name] = function_descriptor
                is_bound = utils.is_class_method(value) or utils.is_static_method(self._base_cls, method_name)
                sig = signature.get_signature(value, ignore_first=not is_bound)
                return_nums = value.__return_nums__ if hasattr(
                    value, "__return_nums__") else 1
                method = MethodProxy(self, self.instance_id,
                                     self._method_descriptor.get(method_name),
                                     sig, return_nums, function_id, is_async_, self._instance_name, self._ns)
                setattr(self, method_name, method)

    def __getattr__(self, method_name):
        if self._class_descriptor.target_language == LanguageType.Python:
            raise AttributeError(f"'{self._class_descriptor.class_name}' object has "
                                 f"no attribute '{method_name}'")

        function_name = method_name
        if self._class_descriptor.target_language == LanguageType.Cpp:
            function_name = "&" + self._class_descriptor.class_name + "::" + method_name
        method_descriptor = utils.ObjectDescriptor(module_name=self._class_descriptor.module_name,
                                                   function_name=function_name,
                                                   class_name=self._class_descriptor.class_name,
                                                   target_language=self._class_descriptor.target_language,
                                                   is_generator=self.__get_method_generator(method_name))
        return MethodProxy(
            self,
            self.instance_id,
            method_descriptor,
            None,
            1,
            self._function_id, self._is_async, self._instance_name, self._ns)

    def __reduce__(self):
        state = self.serialization_(False)
        return InstanceProxy.deserialization_, (state,)

    @classmethod
    def deserialization_(cls, state):
        """
        Deserialization to rebuild instance proxy.

        Args:
            state (dict): Contains serialized state information.

        Returns:
            Returns a class instance. Data type is InstanceProxy.
        """
        class_method = None
        if constants.CLASS_METHOD in state:
            class_method = state[constants.CLASS_METHOD]
        function_name = state[constants.FUNC_NAME] if constants.FUNC_NAME in state else ""
        need_order = state[constants.NEED_ORDER] if constants.NEED_ORDER in state else False
        save_real_instance_id(state[constants.INSTANCE_ID], need_order)
        return cls(instance_id=state[constants.INSTANCE_ID],
                   class_descriptor=utils.ObjectDescriptor(state[constants.MODULE_NAME], state[constants.CLASS_NAME],
                                                           function_name, state[constants.TARGET_LANGUAGE]),
                   class_methods=class_method,
                   base_cls=state[constants.BASE_CLS],
                   function_id="",
                   need_order=need_order)

    def serialization_(self, is_cross_language: False):
        """
        Serialization of instance proxy.

        Args:
            is_cross_language (bool, optional): Whether cross-language, default to ``False``.

        Returns:
            Serialized instance proxy information. Data type is dict.
        """
        info_ = {constants.INSTANCE_ID: global_runtime.get_runtime().get_real_instance_id(self.instance_id)}
        if is_cross_language is False:
            info_[constants.CLASS_METHOD] = self._class_methods

        info_[constants.NEED_ORDER] = self.need_order
        info_[constants.BASE_CLS] = self._base_cls
        self._class_descriptor.to_dict()
        state = {**info_, **self._class_descriptor.to_dict()}
        return state

    def terminate(self, is_sync: bool = False):
        """
        Terminate the instance.

        Supports synchronous or asynchronous termination. When synchronous termination is not enabled,
        the default timeout for the current kill request is 30 seconds.
        In scenarios such as high disk load or etcd failure, the kill request processing time may exceed 30 seconds,
        causing the interface to throw a timeout exception. Since the kill request has a retry mechanism,
        users can choose not to handle or retry after capturing the timeout exception.
        When synchronous termination is enabled, the interface will block until the instance completely exits.

        Args:
            is_sync (bool, optional): Whether to enable synchronization. If true, it indicates sending a kill request
                with the signal quantity of killInstanceSync to the function-proxy, and the kernel synchronously kills
                the instance. If false, it indicates sending a kill request with the signal quantity of killInstance
                to the function-proxy, and the kernel asynchronously kills the instance. Default to ``False``.
        """
        if not self.is_activate():
            return
        if self.group_name != "":
            global_runtime.get_runtime().terminate_group(self.group_name)
        elif is_sync:
            global_runtime.get_runtime().terminate_instance_sync(self.instance_id)
        else:
            global_runtime.get_runtime().terminate_instance(self.instance_id)
        self.__instance_activate__ = False
        _logger.info("%s is terminated", self.instance_id)

    def is_activate(self):
        """
        Return the instance status.

        Returns:
            The instance status. Data type is bool.
        """
        return self.__instance_activate__

    def get_function_group_handler(self) -> "FunctionGroupHandler":
        """
        Get the FunctionGroupHandler.

        Returns:
            The function group handler. Data type is FunctionGroupHandler.
        """
        if self.group_name == "":
            raise RuntimeError(
                "unsupported function type: this function can only be used for group instance handler"
            )
        instance_ids = global_runtime.get_runtime().get_instances(
            self.instance_id, self.group_name)
        handler = FunctionGroupHandler(instance_ids, self._class_descriptor,
                                       self._class_methods, self._base_cls, self._function_id,
                                       self.need_order, self.group_name, self._is_async,
                                       self._instance_name, self._ns)
        return handler

    def __get_method_generator(self, method_name):
        """
        get method generator feature
        """
        if method_name in self._method_descriptor:
            return self._method_descriptor[method_name].is_generator
        return False


@register_pack_hook
def msgpack_encode_hook(obj):
    """
    register msgpack encode hook
    """
    if isinstance(obj, InstanceProxy):
        return obj.serialization_(True)
    return obj


@register_unpack_hook
def msgpack_decode_hook(obj):
    """
    register msgpack decode hook
    """
    if constants.INSTANCE_ID in obj:
        return InstanceProxy.deserialization_(obj)
    return obj


class MethodProxy:
    """
    Use to decorate a user class method.
    """

    def __init__(self,
                 instance,
                 instance_id,
                 method_descriptor,
                 sig,
                 return_nums=1,
                 function_id="",
                 is_async=False,
                 instance_name="",
                 namespace=""):
        """
        Initialize the MethodProxy instance.
        """
        self._instance_ref = weakref.ref(instance)
        self._instance_id = instance_id
        self._method_descriptor = method_descriptor
        self._function_id = function_id
        self._signature = sig
        self._return_nums = return_nums
        self._is_async = is_async
        self._instance_name = instance_name
        self._ns = namespace
        if return_nums < 0 or return_nums > 100:
            raise RuntimeError(f"invalid return_nums: {return_nums}, should be an integer between 0 and 100")


    def invoke(self, *args, **kwargs) -> "yr.ObjectRef":
        """
        Execute remote invoke to user functions.

        Args:
            *args: Variable arguments, used to pass non-keyword arguments.
            **kwargs: Variable arguments, used to pass keyword arguments.

        Returns:
            Reference to a data object. Data type is ObjectRef.

        Raises:
            TypeError: If the parameter type is incorrect.

        Example:
            >>> import yr
            >>> yr.init()
            >>>
            >>> @yr.instance
            ... class Instance:
            ...     sum = 0
            ...
            ...     def add(self, a):
            ...         self.sum += a
            ...
            ...     def get(self):
            ...         return self.sum
            ...
            >>> ins = Instance.invoke()
            >>> yr.get(ins.add.invoke(1))
            >>>
            >>> print(yr.get(ins.get.invoke()))
            >>>
            >>> ins.terminate()
            >>>
            >>> yr.finalize()
        """
        return self._invoke(args, kwargs)

    def options(self, invoke_options: InvokeOptions):
        """
        Set user invoke options.

        Args:
            invoke_options (InvokeOptions): Invoke options for users to set resources.

        Returns:
            Method proxy wrapper. Data type is FuncWrapper.
        """
        func_cls = self
        invoke_options.check_options_valid()

        class FuncWrapper:
            """ FuncWrapper wrapper method proxy """

            @classmethod
            def invoke(cls, *args, **kwargs):
                """ invoke a class method in cluster """
                return func_cls._invoke(args, kwargs, invoke_options)

        return FuncWrapper()

    def _invoke(self, args, kwargs, invoke_options=InvokeOptions()):
        if not self._instance_ref().is_activate():
            raise RuntimeError("this instance is terminated")
        if self._method_descriptor.target_language == LanguageType.Python:
            args_list = signature.package_args(self._signature, args, kwargs)
        else:
            args_list = utils.make_cross_language_args(args, kwargs)
        func_meta = FunctionMeta(moduleName=self._method_descriptor.module_name,
                                 className=self._method_descriptor.class_name,
                                 functionName=self._method_descriptor.function_name,
                                 language=self._method_descriptor.target_language,
                                 functionID=self._function_id,
                                 isGenerator=self._method_descriptor.is_generator,
                                 isAsync=self._is_async,
                                 name=self._instance_name,
                                 ns=self._ns
                                 )
        runtime = global_runtime.get_runtime()
        return_nums = 1 if (self._return_nums == 0 or self._method_descriptor.is_generator) else self._return_nums
        obj_list = runtime.invoke_instance(func_meta=func_meta, instance_id=self._instance_id,
                                           args=args_list,
                                           opt=invoke_options,
                                           return_nums=return_nums)
        # each invoke should have its own InvokeOptions,
        # therefore self.__invoke_options is going to be reset

        if self._return_nums == 0:
            return None
        objref_list = []
        for i in obj_list:
            objref_list.append(ObjectRef(i, need_incre=False))
        return objref_list[0] if self._return_nums == 1 else objref_list


def make_decorator(invoke_options=None):
    """
    Make decorator for invoke function
    """

    def decorator(cls):
        if inspect.isclass(cls):
            return InstanceCreator.create_from_user_class(cls, invoke_options)
        raise RuntimeError("@yr.instance decorator must be applied to a class")

    return decorator


def make_cpp_instance_creator(cpp_class):
    """
    Make cpp_instance creator for invoke function
    """

    return InstanceCreator.create_cpp_user_class(cpp_class)


def get_instance_by_name(name, namespace, timeout) -> InstanceProxy:
    """
    Get instance by name
    """

    runtime = global_runtime.get_runtime()
    function_meta = runtime.get_instance_by_name(name, namespace, timeout)
    if function_meta.language == LanguageType.Python:
        user_class = CodeManager().load_code(function_meta, True)
        user_class_descriptor = utils.ObjectDescriptor.get_from_class(user_class)
        class_methods = inspect.getmembers(user_class, utils.is_function_or_method)
        user_class_methods = dict(class_methods)
        base_cls = inspect.getmro(user_class)
        ins_proxy = InstanceProxy(namespace + "-" + name if namespace else name,
                                  user_class_descriptor,
                                  user_class_methods,
                                  base_cls,
                                  "",
                                  is_async=function_meta.isAsync,
                                  instance_name=name,
                                  namespace=namespace)

        return ins_proxy
    user_class_descriptor = utils.ObjectDescriptor.get_from_func_meta(function_meta)
    ins_proxy = InstanceProxy(namespace + "-" + name if namespace else name,
                              user_class_descriptor, None, None, function_id=function_meta.functionID,
                              instance_name=name,
                              namespace=namespace,
                              is_async=function_meta.isAsync)
    return ins_proxy


class FunctionGroupMethodProxy:
    """
    Use to invoke instance proxy.
    """
    #: This flag enables shared memory.Default is ``False``.
    use_shared_memory: bool = False
    #: Message queue instance used for RPC broadcasting.
    rpc_broadcast_mq: "MessageQueue"

    def __init__(self, method_name: str, class_descriptor: utils.ObjectDescriptor, proxy_list: List[InstanceProxy]):
        """Initialization method, used to create instances of a class."""
        self._method_name = method_name
        self._class_descriptor = class_descriptor
        self._instance_proxy_list = proxy_list

    def invoke(self, *args, **kwargs) -> List[ObjectRef]:
        """
        Perform remote calls to user functions.

        Returns:
            A reference to a group of data objects.
        """
        if self.use_shared_memory:
            task_id = str(uuid.uuid4())
            return_objs = []
            obj_ids = []
            for i in range(len(self._instance_proxy_list)):
                obj_id = task_id + "_" + str(i)
                obj_ids.append(obj_id)
                return_objs.append(ObjectRef(obj_id, need_incre=False))
            global_runtime.get_runtime().add_return_object(obj_ids)
            _logger.debug(f"start to invoke member function: {self._method_name}")
            self.rpc_broadcast_mq.enqueue((task_id, self._method_name, args, kwargs))
            _logger.debug(f"finish to send member function request: {self._method_name}, task id {task_id}")
            return return_objs
        for proxy in self._instance_proxy_list:
            if not hasattr(proxy, self._method_name):
                raise AttributeError(f"'{self._class_descriptor.class_name}' object has "
                                     f"no attribute '{self._method_name}'")
        result = []
        for proxy in self._instance_proxy_list:
            objs = getattr(proxy, self._method_name).invoke(*args, **kwargs)
            if isinstance(objs, List):
                result.extend(objs)
            else:
                result.append(objs)
        return result

    def set_rpc_broadcast_mq(self, rpc_broadcast_mq: "MessageQueue"):
        """set rpc broadcast message queue."""
        self.rpc_broadcast_mq = rpc_broadcast_mq
        self.use_shared_memory = True


class FunctionGroupHandler:
    """
    Use to decorate a list of instance proxy.
    """

    def __init__(self,
                 instance_ids,
                 class_descriptor,
                 class_methods,
                 base_cls,
                 function_id,
                 need_order=True,
                 group_name="",
                 is_async=False,
                 instance_name=None,
                 namespace=None):
        """
        Initialization method, used to create instances of a class.
        """
        self._instance_ids = instance_ids
        self._group_name = group_name
        self._class_descriptor = class_descriptor
        self.__instance_activate__ = True
        self._class_methods = class_methods
        self.executor = None
        self.rpc_broadcast_mq = None
        if class_methods is not None:
            for method_name, _ in class_methods.items():
                method = FunctionGroupMethodProxy(method_name, class_descriptor, [
                    InstanceProxy(instance_id, class_descriptor,
                                  class_methods, base_cls, function_id,
                                  need_order, group_name, is_async,
                                  instance_name, namespace)
                    for instance_id in instance_ids
                ])
                setattr(self, method_name, method)

    def terminate(self):
        """
        Terminate the function group.
        """
        if not self.__instance_activate__:
            return
        STOP_EVENT.set()
        if self.executor is not None:
            self.executor.shutdown(wait=False)
        global_runtime.get_runtime().terminate_group(self._group_name)
        self.__instance_activate__ = False

    def accelerate(self):
        """
        Acceleration method, used to perform acceleration operations on local instances.
        """
        is_local = global_runtime.get_runtime().is_local_instances(self._instance_ids)
        if not is_local:
            return
        _logger.debug(f"group all are {len(self._instance_ids)} local instances")
        fcc_max_chunk_bytes = int(os.environ.get("FCC_MAX_CHUNK_BYTES", 1024 * 1024 * 10))
        fcc_max_chunks = int(os.environ.get("FCC_MAX_CHUNKS", 10))
        fcc_use_async_loop = True
        fcc_use_sync_loop = os.environ.get("FCC_USE_SYNC_LOOP")
        if fcc_use_sync_loop or fcc_use_sync_loop in ['1', 'true', 'True', 'yes']:
            fcc_use_async_loop = False
        self.rpc_broadcast_mq = MessageQueue(len(self._instance_ids), fcc_max_chunk_bytes, fcc_max_chunks,
                                             fcc_use_async_loop)
        handle = self.rpc_broadcast_mq.export_handle()
        global_runtime.get_runtime().accelerate(self._group_name, handle)
        _logger.debug("finish accelerate")
        if self._class_methods is not None:
            for method_name, _ in self._class_methods.items():
                method_proxy = getattr(self, method_name)
                method_proxy.set_rpc_broadcast_mq(self.rpc_broadcast_mq)
