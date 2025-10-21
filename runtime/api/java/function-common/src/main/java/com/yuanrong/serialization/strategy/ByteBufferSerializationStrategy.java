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

package com.yuanrong.serialization.strategy;

import java.nio.ByteBuffer;

/**
 * ByteBuffer class serialization strategy.
 *
 * @since 2023 /10/16
 */
public class ByteBufferSerializationStrategy implements SerializationStrategy<ByteBuffer, ByteBuffer, ByteBuffer> {
    @Override
    public ByteBuffer serialize(ByteBuffer value) {
        return value;
    }

    @Override
    public ByteBuffer deserialize(ByteBuffer data, Class<ByteBuffer> type) {
        return data;
    }
}
