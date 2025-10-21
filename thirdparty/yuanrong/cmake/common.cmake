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

set(src_dir ${ROOT_DIR}/common/utils)
message(STATUS "logs src dir: ${src_dir}")
add_subdirectory(${ROOT_DIR}/common/utils ${CMAKE_CURRENT_BINARY_DIR}/utils)
add_subdirectory(${ROOT_DIR}/common/meta_store/client/cpp ${CMAKE_CURRENT_BINARY_DIR}/meta_store_client)
add_subdirectory(${ROOT_DIR}/common/meta_store/server ${CMAKE_CURRENT_BINARY_DIR}/meta_store_server)