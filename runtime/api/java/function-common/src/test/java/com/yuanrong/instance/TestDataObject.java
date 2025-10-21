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

import org.junit.Assert;
import org.junit.Test;

public class TestDataObject {
    @Test
    public void testInitDataObject() {
        DataObject dataObject = new DataObject();
        DataObject dataObject1 = new DataObject("testID");
        DataObject dataObject2 = new DataObject("", null);
        dataObject1.getId();
        dataObject.getData();
        dataObject2.setId("test1");
        Assert.assertFalse(dataObject.equals(dataObject2));
    }
}
