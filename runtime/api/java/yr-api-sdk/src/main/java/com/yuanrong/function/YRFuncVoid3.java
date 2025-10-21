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

package com.yuanrong.function;

/**
 * The interface Yr func void 3.
 *
 * @param <T0> the type parameter
 * @param <T1> the type parameter
 * @param <T2> the type parameter
 * @since 2022 /08/30
 */
@FunctionalInterface
public interface YRFuncVoid3<T0, T1, T2> extends YRFuncVoid {
    /**
     * Apply.
     *
     * @param t0 the t 0
     * @param t1 the t 1
     * @param t2 the t 2
     * @throws Exception the exception
     */
    void apply(T0 t0, T1 t1, T2 t2) throws Exception;
}
