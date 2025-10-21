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

package com.yuanrong.instance;

import com.yuanrong.libruntime.generated.Libruntime.ApiType;
import com.yuanrong.libruntime.generated.Libruntime.FunctionMeta;
import com.yuanrong.libruntime.generated.Libruntime.LanguageType;

import org.junit.Assert;
import org.junit.Test;

public class TestFunctionMetaFactory {
    @Test
    public void testInitFunctionMetaFactory() {
        FunctionMetaFactory functionMetaFactory = new FunctionMetaFactory();
        FunctionMeta functionMeta = FunctionMetaFactory.getFunctionMeta("testAppName", "testModelName", "testFuncName",
            "testClassName", LanguageType.Java, ApiType.Function, "testSignature");
        Assert.assertNotNull(functionMeta);
    }
}
