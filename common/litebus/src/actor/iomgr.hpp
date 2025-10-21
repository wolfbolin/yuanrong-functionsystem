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

#ifndef __LITEBUS_IOMGR_H__
#define __LITEBUS_IOMGR_H__
#include <memory>
#include "actor/aid.hpp"

#include "actor/msg.hpp"

namespace litebus {

// Server socket listen backlog.
static const int SOCKET_LISTEN_BACKLOG = 2048;

static const int SOCKET_KEEPALIVE = 1;

// Send first probe after `interval' seconds.
static const int SOCKET_KEEPIDLE = 600;

// Send next probes after the specified interval.
static const int SOCKET_KEEPINTERVAL = 5;

// Consider the socket in error state after we send three ACK
// probes without getting a reply.
static const int SOCKET_KEEPCOUNT = 3;

static const std::string BUS_MAGICID = "BUS0";

static const std::string URL_PROTOCOL_IP_SEPARATOR = "://";

static const std::string URL_IP_PORT_SEPARATOR = ":";

static const std::string UDP_EVLOOP_THREADNAME = "HARES_LB_Udp";

static const std::string TCP_RECV_EVLOOP_THREADNAME = "HARES_LB_TcpR";
static const std::string TCP_SEND_EVLOOP_THREADNAME = "HARES_LB_TcpS";

static const std::string HTTP_CLIENT_EVLOOP_THREADNAME = "HARES_LB_Htp";

class IOMgr {
public:
    using MsgHandler = void (*)(std::unique_ptr<MessageBase> &&msg);
    /**
     * remoteLink and isExactNotRemote are flags to tell us which link should be used. There are several cases:
     * 1. remoteLink is false and isExactNotRemote is false : callers can reuse remote link when threr are no links
     * created before.
     * 2. remoteLink is true and isExactNotRemote is false : callers can only use remote link
     * 3. remoteLink is true and isExactNotRemote is true : as the same as case 2
     * 4. remoteLink is false and isExactNotRemote is true : callers can't reuse remote link. if no link,
     *    we will create a new one for callers.
     */
    virtual int Send(std::unique_ptr<MessageBase> &&msg, bool remoteLink = false, bool isExactNotRemote = false) = 0;
    virtual void Link(const AID &sAid, const AID &dAid) = 0;
    // close the socket,and send exitedEvent to all linkers.
    virtual void UnLink(const AID &dAid) = 0;
    virtual void Reconnect(const AID &sAid, const AID &dAid) = 0;
    virtual void RegisterMsgHandle(MsgHandler handle) = 0;
    virtual bool Init() = 0;                                                                    // once
    virtual void Finish() = 0;                                                                  // once
    virtual bool StartIOServer(const std::string &url, const std::string &advertiseUrl) = 0;    // multicalledable
    virtual uint64_t GetOutBufSize() = 0;
    virtual uint64_t GetInBufSize() = 0;
    virtual void CollectMetrics() = 0;
    virtual int AddRuleUdp(std::string peer, int recordNum) = 0;
    virtual void DelRuleUdp(std::string peer, bool outputLog) = 0;
    virtual void LinkRecycleCheck(int recyclePeroid) = 0;
    IOMgr()
    {
    }
    virtual ~IOMgr()
    {
    }
};

};    // namespace litebus

#endif
