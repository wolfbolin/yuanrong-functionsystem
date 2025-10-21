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

#ifndef UT_MOCKS_MOCK_ETCD_LEASE_SERVICE_H
#define UT_MOCKS_MOCK_ETCD_LEASE_SERVICE_H

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "etcd/api/etcdserverpb/rpc.grpc.pb.h"

namespace functionsystem::test {
class MockEtcdLeaseService final : public etcdserverpb::Lease::Service {
public:
    MockEtcdLeaseService() = default;
    ~MockEtcdLeaseService() override = default;

    MOCK_METHOD(::grpc::Status, LeaseGrant,
                (::grpc::ServerContext *, const ::etcdserverpb::LeaseGrantRequest *,
                 ::etcdserverpb::LeaseGrantResponse *),
                (override));

    MOCK_METHOD(::grpc::Status, LeaseRevoke,
                (::grpc::ServerContext *, const ::etcdserverpb::LeaseRevokeRequest *,
                 ::etcdserverpb::LeaseRevokeResponse *),
                (override));
};
}  // namespace functionsystem::test

#endif  // UT_MOCKS_MOCK_ETCD_LEASE_SERVICE_H
