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

package com.yuanrong.call;

import com.yuanrong.Config;
import com.yuanrong.InvokeOptions;
import com.yuanrong.api.YR;
import com.yuanrong.exception.YRException;
import com.yuanrong.function.CppInstanceMethod;
import com.yuanrong.runtime.ClusterModeRuntime;

import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.powermock.api.mockito.PowerMockito;
import org.powermock.core.classloader.annotations.PowerMockIgnore;
import org.powermock.core.classloader.annotations.PrepareForTest;
import org.powermock.modules.junit4.PowerMockRunner;

import java.io.IOException;

@RunWith(PowerMockRunner.class)
@PowerMockIgnore({"javax.net.ssl.*", "javax.management.*"})
@PrepareForTest(YR.class)
public class TestCppInstanceFunctionHandler {
    ClusterModeRuntime runtime = PowerMockito.mock(ClusterModeRuntime.class);

    @Before
    public void initYR() throws Exception {
        Config conf = new Config(
            "sn:cn:yrk:12345678901234561234567890123456:function:0-crossyrlib-helloworld:$latest",
            "127.0.0.0",
            "127.0.0.0",
            "");
        PowerMockito.whenNew(ClusterModeRuntime.class).withAnyArguments().thenReturn(runtime);
        YR.init(conf);
    }

    @After
    public void finalizeYR() throws YRException {
        YR.Finalize();
    }

    @Test
    public void testInitCppInstanceFunctionHandler() {
        String instanceId = "instanceId";
        String functionId = "functionId";
        String className = "Counter";
        CppInstanceHandler instance = new CppInstanceHandler(instanceId, functionId, className);
        CppInstanceFunctionHandler<Integer> functionHandler = instance.function(
            CppInstanceMethod.of("Add", int.class));
        InvokeOptions invokeOptions = new InvokeOptions();
        invokeOptions.setCpu(1500);
        invokeOptions.setMemory(2500);
        functionHandler.options(invokeOptions);
        Assert.assertNotNull(functionHandler);
    }

    @Test
    public void testInvokeCppInstanceFunctionHandler() throws IOException, YRException {
        String instanceId = "instanceId";
        String functionId = "functionId";
        String className = "Counter";
        CppInstanceHandler instance = new CppInstanceHandler(instanceId, functionId, className);
        CppInstanceFunctionHandler<Integer> functionHandler = instance.function(
            CppInstanceMethod.of("Add", int.class));
        functionHandler.invoke(instance);
        Assert.assertNotNull(functionHandler);
    }
}
