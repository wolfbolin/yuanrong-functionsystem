/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
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

import lombok.Data;

import java.util.List;

/**
 * DataObject
 *
 * @since 2023/11/6
 */
@Data
public class DataObject {
    String id;
    byte[] data;
    List<String> nestedObjIds;

    public DataObject() {}

    public DataObject(String id) {
        this(id, null);
    }

    public DataObject(String id, byte[] data) {
        this.id = id;
        this.data = data;
    }

    public String getId() {
        return this.id;
    }
}