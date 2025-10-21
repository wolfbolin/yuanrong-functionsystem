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
 * The interface Yr func 1.
 *
 * @param <T0> the type parameter
 * @param <R> the type parameter
 * @since 2022 /08/30
 */
@FunctionalInterface
public interface YRFunc1<T0, R> extends YRFuncR<R> {
    /**
     * Apply r.
     *
     * @param t0 the t 0
     * @return the r
     * @throws Exception the exception
     */
    R apply(T0 t0) throws Exception;
}
