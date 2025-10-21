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
common tools
"""
import binascii
import enum
import gc
import inspect
import json
import os
import re
import sys
import traceback
import uuid
from dataclasses import dataclass, field
from typing import Dict

import asyncio
import cloudpickle

from yr import log
from yr.common import constants
from yr.libruntime_pb2 import LanguageType, FunctionMeta

try:
    import uvloop
except ImportError:
    uvloop = None


_URN_SEPARATOR = ":"
_NO_ORDER_SUFFIX = "-x"
_OBJECT_ID_PREFIX = "yr-api-obj-"
_TRACE_ID = "-trace-"
_JOB_ID = ""
_RGROUP_PREFIX = "rgroup-"
_RUNTIME_ID_INDEX = -2
_SERIAL_NUM_INDEX = -1
_REQUEST_ID_SPLIT_LEN = 10  # The request id length, split by "-"
_JOB_ID_PREFIX = "job-"


def create_new_event_loop():
    """
    If uvloop is not existed, use asyncio to create event loop, else use uvloop
    :return:
    """
    if uvloop is None:
        return asyncio.new_event_loop()
    return uvloop.new_event_loop()


def try_install_uvloop():
    """
    If uvloop is existed, need to install to replace asyncio
    :return:
    """
    if uvloop:
        uvloop.install()


def validate_ip(input_ip: str):
    """
    This is a checker for input ip string

    Checks validity of input ip string

    Returns:
        True, the input ip string is valid
        False, the input ip string is invalid
    """
    ip_regex = \
        r"^((25[0-5]|2[0-4]\d|[01]?\d\d?)\.){3}(25[0-5]|2[0-4]\d|[01]?\d\d?)$"
    compile_ip = re.compile(ip_regex)
    return compile_ip.match(input_ip)


def validate_address(address, localhost_pass=False):
    """
    Validates address parameter
    Args:
        address: string address of the full <host:port> address

    Returns:
        ip: string of ip
        port: integer of port

    """
    address_parts = address.split(":")
    if len(address_parts) != 2:
        raise ValueError("address format is wrong, '<ip>:<port>' is expected.")
    ip = address_parts[0]
    try:
        port = int(address_parts[1])
    except ValueError as err:
        raise ValueError("port format is wrong, must be an integer.") from err
    if not 1 <= port <= 65535:
        raise ValueError(f"port value {port} is out of range.")
    if localhost_pass and ip in ("127.0.0.1"):
        return ip, port
    if not validate_ip(ip):
        raise ValueError(f"invalid ip {ip}")
    return ip, port


def set_job_id(job_id):
    """set global job id"""
    global _JOB_ID
    _JOB_ID = job_id


def generate_job_id():
    """
    Generate a unique job ID.

    Returns:
        str: A string representing the unique job ID.
    """
    return _JOB_ID_PREFIX + str(uuid.uuid4()).replace('-', '')[:8]


def generate_random_id():
    """
    This is a wrapper generating random id for user functions and objects

    Gets a random id string

    Example: yrobj-433ec3c1-ba11-5a16-ad97-ee4e68db67d5

    Returns:
        Unique uuid string with prefix for user functions and objects
    """
    uuid_str = str(uuid.uuid4())
    return _OBJECT_ID_PREFIX + uuid_str


def generate_task_id():
    """
    This is a wrapper generating random task id for user stateless invoke functions

    Gets a random id string

    Example: job-xxx-task-433ec3c1-ba11-5a16-ad97-ee4e68db67d5-x

    Returns:
        Unique uuid string with prefix for stateless invoke functions
    """
    uuid_str = str(uuid.uuid4())
    return _JOB_ID + "-task-" + uuid_str + _NO_ORDER_SUFFIX


def generate_runtime_id():
    """
    Generating random runtime id

    :return: Unique 8-bit uuid string
    """
    return str(uuid.uuid4().hex)[:8]


def generate_task_id_with_serial_num(runtime_id, serial_num):
    """
    Generating random task id with last 8 characters recording serial number
    for user class methods

    Example: job-xxx-task-433ec3c1-ba11-5a16-ad97-ee4e68db67d5-0
             job-xxx-task-433ec3c1-ba11-5a16-ad97-ee4e68db67d5-1

    :param runtime_id: runtime id
    :param serial_num: serial number of invoke task
    :return: Unique uuid string with prefix and serial number
             for user class methods
    """
    uuid_str = str(uuid.uuid4())
    return f"{_JOB_ID}-task-{uuid_str}-{runtime_id}-{str(serial_num)}"


def check_request_id_in_order(request_id) -> bool:
    """
    Check request id is in-order
        in-order: id end suffix is number
        no-order: id end suffix is constant 'x'
    """
    parts = request_id.split("-")
    if parts[-1] == 'x' or len(parts) != _REQUEST_ID_SPLIT_LEN:
        return False
    return True


def generate_resource_group_name():
    """
    This is a wrapper generating resource group name when create virtual 
    cluster with no name input

    Gets a random name string

    Example: rgroup-433ec3c1-ba11-5a16-ad97-ee4e68db67d5

    Returns:
        Unique uuid string with prefix for resource group
    """
    uuid_str = str(uuid.uuid4())
    return _RGROUP_PREFIX + uuid_str


def extract_serial_num(task_id):
    """
    Extract serial number from task id

    :param task_id: string of task id
    :return: serial number of the task (int)
    """
    return int(task_id.split("-")[_SERIAL_NUM_INDEX])


def extract_runtime_id(task_id):
    """
    Extract runtime id from task id

    :param task_id: string of task id
    :return: runtime id of the task (str)
    """
    return task_id.split("-")[_RUNTIME_ID_INDEX]


def generate_trace_id(job_id: str) -> str:
    """
    TraceID is used to analyze the call chain between functions

    Example: job-fa60ccbb-trace-adc3f0b94c89457e8fedce36c0d0dc20

    Returns:
        Unique uuid string with prefix for stateless invoke functions
    """
    trace_id = str(uuid.uuid4().hex)
    return job_id + _TRACE_ID + trace_id


def get_module_name(obj):
    """
    Get the module name from object. If the module is __main__,
    get the module name from file.

    Returns:
        Module name of object.
    """
    module_name = obj.__module__
    n = None
    if module_name == "__main__":
        try:
            file_path = inspect.getfile(obj)
            n = inspect.getmodulename(file_path)
        except (TypeError, OSError):
            pass
    if n:
        module_name = n
    return module_name


def binary_to_hex(value):
    """
    bytes to hex
    """
    hex_id = binascii.hexlify(value)
    if sys.version_info >= (3, 0):
        hex_id = hex_id.decode()
    return hex_id


def hex_to_binary(hex_id):
    """
    hex to bytes
    """
    return binascii.unhexlify(hex_id)


def package_args(args, kwargs):
    """
    Package user invoke args.
    """
    send_param = {
        "args": binary_to_hex(cloudpickle.dumps(args)),
        "kwargs": binary_to_hex(cloudpickle.dumps(kwargs)),
    }

    param_str = json.dumps(send_param)

    return param_str


def make_cross_language_args(args, kwargs):
    """
    make cross language args
    """
    args_list = list(args)
    for key, value in kwargs.items():
        args_list += [key, value]
    return args_list


class ObjectDescriptor:
    """
    object descriptor
    """
    src_language = LanguageType.Python
    target_language = LanguageType.Python

    def __init__(self,
                 module_name="",
                 class_name="",
                 function_name="",
                 target_language=LanguageType.Python,
                 src_language=LanguageType.Python,
                 is_generator=False,
                 is_async=False):
        self.module_name = module_name
        self.class_name = class_name
        self.function_name = function_name
        self.target_language = target_language
        self.src_language = src_language
        self.is_generator = is_generator
        self.is_async = is_async

    @classmethod
    def get_from_function(cls, func):
        """
        get the function descriptor
        """
        self = ObjectDescriptor.__new__(cls)
        self.module_name = get_module_name(func)
        self.function_name = func.__qualname__
        self.class_name = ""
        return self

    @classmethod
    def get_from_class(cls, obj):
        """
        get the class descriptor
        """
        self = ObjectDescriptor.__new__(cls)
        self.module_name = get_module_name(obj)
        self.class_name = obj.__qualname__
        self.function_name = ""
        return self

    @classmethod
    def get_from_func_meta(cls, meta: FunctionMeta):
        """get from function meta"""
        self = ObjectDescriptor.__new__(cls)
        self.module_name = meta.moduleName
        self.class_name = meta.className
        self.function_name = meta.functionName
        self.target_language = meta.language
        return self


    @classmethod
    def parse(cls, data):
        """parse from json or dict"""
        tmp = data
        if isinstance(tmp, bytes):
            tmp = tmp.decode()
        if isinstance(tmp, str):
            tmp = json.loads(tmp)
        if isinstance(tmp, dict):
            self = cls(module_name=tmp.get(constants.MODULE_NAME, ""),
                       class_name=tmp.get(constants.CLASS_NAME, ""),
                       function_name=tmp.get(constants.FUNC_NAME, ""),
                       target_language=tmp.get(constants.TARGET_LANGUAGE, ""),
                       src_language=tmp.get(constants.SRC_LANGUAGE, ""))
        else:
            raise TypeError(f"can not parse from type {type(data)}")
        return self

    def to_dict(self):
        """
        export the object descriptor to dict
        """
        return {constants.MODULE_NAME: self.module_name,
                constants.CLASS_NAME: self.class_name,
                constants.FUNC_NAME: self.function_name,
                constants.TARGET_LANGUAGE: self.target_language,
                constants.SRC_LANGUAGE: self.src_language}


def is_function_or_method(obj):
    """
    judge the obj type
    """
    return inspect.isfunction(obj) or inspect.ismethod(obj)


class _URNIndex(enum.IntEnum):
    PREFIX = 0
    ZONE = 1
    BUSINESS_ID = 2
    TENANT_ID = 3
    NAME = 5
    VERRSION = 6


def get_function_from_urn(urn: str) -> str:
    """
    get function name which used by posix
    example: 7e1ad6a6cc5c44fabd5425873f72a86a/0-test-helloclass/$latest
    """
    if urn == "":
        return urn
    parts = urn.split(_URN_SEPARATOR)
    name = [parts[_URNIndex.TENANT_ID], parts[_URNIndex.NAME], parts[_URNIndex.VERRSION]]
    return constants.SEPARATOR_SLASH.join(name)


@dataclass
class CrossLanguageInfo:
    """
    CrossLanguageFunctionInfo
    """
    function_name: str = ""
    function_key: str = ""
    target_language: str = ""
    class_name: str = ""


def to_json_string(obj: object, indent=None, sort_keys=False) -> str:
    """Method to_json_string"""
    if isinstance(obj, dict):
        return json.dumps(obj, indent=indent, sort_keys=sort_keys)
    if hasattr(obj, '__dict__'):
        return json.dumps(obj, indent=indent, default=obj.__dict__, sort_keys=sort_keys)
    raise RuntimeError(f"Failed to convert object {obj} to json string")


def err_to_str(err: Exception) -> str:
    """
    This function takes in an Exception object and returns a formatted string containing
    the error message and traceback information.

    Instead of using 'format_exc', 'format_exception' doesn't truncate the output when it is too long in logs.

    Args:
        err (Exception): The Exception object to be formatted.

    Returns:
        str: A string containing the formatted error message and traceback information.
    """
    return " ".join(traceback.format_exception(type(err), err, err.__traceback__))


class NoGC:
    """A context manager that disables garbage collection during its context.

    Usage:
        with NoGC():
            # code block where garbage collection is disabled

    Attributes:
        gc_isenabled (bool): whether garbage collection is currently enabled or not.
    """

    def __init__(self):
        self.gc_isenabled = gc.isenabled()

    def __enter__(self):
        if self.gc_isenabled:
            gc.disable()

    def __exit__(self, exc_type, exc_val, exc_tb):
        if self.gc_isenabled:
            gc.enable()


def get_environment_variable(variable_name: str, default_value: str = None) -> str:
    """
    Get the value of an environment variable.
    Args:
        variable_name (str): The name of the environment variable.
        default_value (str, optional): The default value to return if the variable is not set. Defaults to None.
    Returns:
        str: The value of the environment variable.
    Raises:
        RuntimeError: If the environment variable is not set and no default value is provided.
    """
    value = os.environ.get(variable_name)

    if value is None or value == "":
        if default_value:
            return default_value

        raise RuntimeError(f"Environment variable {variable_name} is not set")
    log.get_logger().debug("Succeed to get environment variable: %s, value: %s", variable_name, value)
    return value


@dataclass
class UInt64CounterData:
    """uint64 counter data"""
    name: str = ""
    description: str = ""
    unit: str = ""
    labels: Dict[str, str] = field(default_factory=dict)
    value: int = 0


@dataclass
class DoubleCounterData:
    """double counter data"""
    name: str = ""
    description: str = ""
    unit: str = ""
    labels: Dict[str, str] = field(default_factory=dict)
    value: float = 0.0


@dataclass
class GaugeData:
    """gauge data"""
    name: str = ""
    description: str = ""
    unit: str = ""
    labels: Dict[str, str] = field(default_factory=dict)
    value: float = 0


class Validator:
    """
    Features: Parse arguments
    """

    INT32_MAX_SIZE = int("0x7FFFFFFF", 16)  # Int32.MAX_VALUE
    UINT64_MAX_SIZE = 2 ** 64 - 1  # UInt64.MAX_VALUE

    @staticmethod
    def check_args_types(args):
        """ Check the types of the input arguments

        Args:
            args(list): The input arguments, which is a list of lists. Each list inside contains an argument name,
            the argument value and its expected valid types.
            Example: args = [["value", value, bytes, memoryview], ["timeout", timeout, int]]. Which means the argument
            value should have the type of bytes or memoryview and the timeout argument should be an integer.

        Raises:
            TypeError: Raise a type error if the input parameter is invalid.
        """
        if not isinstance(args, list):
            raise TypeError(r"The input of args should be a list, error type: {err}".format(err=type(args)))
        for arguments in args:
            if not isinstance(arguments, list):
                raise TypeError(r"Each element of the input of args should be a list, error type: {err}".format(
                    err=type(arguments)))
            if len(arguments) < 3:
                raise TypeError(r"Each element of the input of args should have the length at least 3, which "
                                r"contains an argument name, the argument value and its expected valid types.")
            arg_name = arguments[0]
            arg_value = arguments[1]
            arg_types = arguments[2:]
            valid = False
            for arg_type in arg_types:
                if isinstance(arg_value, arg_type):
                    valid = True
                    break
            if valid is False:
                raise TypeError(r"The input of {name} has invalid type, valid type: {type}".format(name=arg_name,
                                                                                                   type=arg_types))

    @staticmethod
    def check_key_exists(args, keys):
        """ Check the types of the input arguments

        Args:
            args(dict): The input arguments.
            keys(list): a list of strings

        Raises:
            TypeError: Raise a type error if the input parameter is invalid.

        Returns:
            res: A list of the values of the given keys
        """
        if not isinstance(args, dict):
            raise TypeError(r"The input of args should be dict, error type: {err}".format(err=type(args)))
        if not isinstance(keys, list):
            raise TypeError(r"The input of keys should be list, error type: {err}".format(err=type(keys)))
        res = []
        for key in keys:
            k = args.get(key)
            if k is None:
                raise TypeError(r"The key '{k_val}' of the input param does not exist".format(k_val=key))
            res.append(k)
        return res

    @staticmethod
    def check_param_range(param_name, param_value, min_limit, max_limit):
        """ Check the range of the input arguments

        Args:
            param_name(str): The name of param
            param_value(str): The value of param
            min_limit(int): The Maximum limit
            max_limit(int): The Minimum limit

        Raises:
            RuntimeError: Raise a RuntimeError if the range of the input parameter is invalid.
        """
        if not min_limit <= param_value <= max_limit:
            raise RuntimeError(r"Invalid {} size {} is set, which should be between [{},{}]".format(
                param_name, param_value, min_limit, max_limit))

    @staticmethod
    def check_timeout(time_out):
        """check time_out type"""
        if not isinstance(time_out, int):
            raise TypeError("an integer is required")


def is_class_method(f):
    """Returns whether the given method is a class_method."""
    return hasattr(f, "__self__") and f.__self__ is not None


def is_static_method(base_cls, f_name):
    """Returns whether the class has a static method with the given name.

    Args:
        base_cls: The Python class (i.e. object of type `type`) to
            search for the method in.
        f_name: The name of the method to look up in this class
            and check whether or not it is static.
    """
    for base in base_cls:
        if f_name in base.__dict__:
            return isinstance(base.__dict__[f_name], staticmethod)
    return False
