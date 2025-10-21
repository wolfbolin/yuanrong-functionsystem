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

#ifndef __LITEBUS_STATUS_HPP__
#define __LITEBUS_STATUS_HPP__

namespace litebus {

class Status {
public:
    using Code = int32_t;

    static const Code KINIT = 1;
    static const Code KOK = 0;
    static const Code KERROR = -1;

    // Create a success status.
    Status(int32_t c) : code(c)
    {
    }

    Status() : code(KINIT)
    {
    }

    virtual ~Status()
    {
    }

    // Returns true iff the status indicates success.
    bool IsInit() const
    {
        return (code == KINIT);
    }

    bool IsOK() const
    {
        return (code == KOK);
    }

    bool IsError() const
    {
        return (code != KINIT && code != KOK);
    }

    // Return a success status.
    Status OK() const
    {
        return Status(KOK);
    }

    Status Error() const
    {
        return Status(KERROR);
    }

    void SetError()
    {
        code = KERROR;
        return;
    }

    void SetOK()
    {
        code = KOK;
        return;
    }

    Code GetCode() const
    {
        return code;
    }

    void SetCode(Code c)
    {
        code = c;
    }

private:
    Code code;
};

}    // namespace litebus

#endif
