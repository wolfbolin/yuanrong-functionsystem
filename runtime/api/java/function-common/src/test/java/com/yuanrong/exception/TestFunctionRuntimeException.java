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

package com.yuanrong.exception;

import org.junit.Assert;
import org.junit.Test;

public class TestFunctionRuntimeException {
    @Test
    public void testInitException() {
        FunctionRuntimeException functionRuntimeException = new FunctionRuntimeException();
        FunctionRuntimeException functionRuntimeException1 = new FunctionRuntimeException("test-functionException");
        FunctionRuntimeException functionRuntimeException2 = new FunctionRuntimeException(new Throwable());
        FunctionRuntimeException functionRuntimeException3 = new FunctionRuntimeException("test-functionException",
            new Throwable());

        functionRuntimeException.setCause(new Throwable());
        functionRuntimeException.setMessage("test");

        functionRuntimeException.getMessage();
        functionRuntimeException.getCause();

        functionRuntimeException1.hashCode();
        functionRuntimeException1.toString();

        functionRuntimeException2.canEqual(functionRuntimeException3);
        Assert.assertTrue(functionRuntimeException2.equals(functionRuntimeException3));
    }
}
