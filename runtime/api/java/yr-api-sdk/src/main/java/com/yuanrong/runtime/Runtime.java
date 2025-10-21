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

package com.yuanrong.runtime;

import com.yuanrong.CreateParam;
import com.yuanrong.FunctionWrapper;
import com.yuanrong.GetParams;
import com.yuanrong.GroupOptions;
import com.yuanrong.InvokeOptions;
import com.yuanrong.MSetParam;
import com.yuanrong.SetParam;
import com.yuanrong.api.InvokeArg;
import com.yuanrong.call.CppInstanceHandler;
import com.yuanrong.call.InstanceHandler;
import com.yuanrong.call.JavaInstanceHandler;
import com.yuanrong.exception.YRException;
import com.yuanrong.jni.LibRuntimeConfig;
import com.yuanrong.libruntime.generated.Libruntime.FunctionMeta;
import com.yuanrong.runtime.client.KVManager;
import com.yuanrong.runtime.client.ObjectRef;
import com.yuanrong.storage.InternalWaitResult;

import java.util.List;

/**
 * The interface Runtime.
 *
 * @since 2023 /10/16
 */
public interface Runtime {
    /**
     * Runtime init
     *
     * @param config LibRuntimeConfig
     * @throws YRException  the YR exception.
     */
    void init(LibRuntimeConfig config) throws YRException;

    /**
     * Create instance string.
     *
     * @param functionMeta the function meta
     * @param args         the args
     * @param opt          the opt
     * @return instanceId
     * @throws YRException the YR exception.
     */
    String createInstance(FunctionMeta functionMeta, List<InvokeArg> args, InvokeOptions opt) throws YRException;


    /**
     * Get list.
     *
     * @param refs         the refs
     * @param timeoutMs      the timeout
     * @param allowPartial the allow partial
     * @return the list
     * @throws YRException the YR exception
     */
    List<Object> get(List<ObjectRef> refs, int timeoutMs, boolean allowPartial) throws YRException;


    /**
     * Get the object from the specified object reference.
     *
     * @param ref          The object reference to retrieve the object from
     * @param timeoutMS    The maximum time in milliseconds to wait for the object
     *                     to be retrieved
     * @return the retrieved object
     * @throws YRException if an error occurs while retrieving the object
     */
    Object get(ObjectRef ref, int timeoutMS) throws YRException;

    /**
     * put obj
     *
     * @param obj obj
     * @param interval interval time to continuously put when spill may occur in milliseconds
     * @param retryTime retry time to continuously put when spill may occurretry time to continuously put
     *                  when spill may occur
     * @return the object ref
     * @throws YRException the YR exception
     */
    ObjectRef put(Object obj, long interval, long retryTime) throws YRException;

    /**
     * put obj
     *
     * @param obj obj
     * @param interval interval time to continuously put when spill may occur in milliseconds
     * @param retryTime retry time to continuously put when spill may occurretry time to continuously put
     *                  when spill may occur
     * @param createParam create param of datasystem
     * @return the object ref
     * @throws YRException the YR exception
     */
    ObjectRef put(Object obj, long interval, long retryTime, CreateParam createParam) throws YRException;

    /**
     * wait obj
     *
     * @param objIds obj ids
     * @param waitNum wait num
     * @param timeoutSec timeout second
     * @return ready and unready objectRef ids
     * @throws YRException the YR exception.
     */
    InternalWaitResult wait(List<String> objIds, int waitNum, int timeoutSec) throws YRException;

    /**
     * Invokes a function by its name.
     *
     * @param functionMeta The meta information of the function to be invoked
     * @param args         The list of arguments to be passed to the function
     * @param opt          The options for the invocation
     * @return A string representing the result of the invocation
     * @throws YRException the YR exception.
     */
    String invokeByName(FunctionMeta functionMeta, List<InvokeArg> args, InvokeOptions opt) throws YRException;

    /**
     * Invokes a java function by its name.
     *
     * @param functionMeta The meta information of the function to be invoked
     * @param args         The list of arguments to be passed to the function
     * @param opt          The options for the invocation
     * @return A string representing the result of the invocation
     * @throws YRException the YR exception.
     */
    ObjectRef invokeJavaByName(FunctionMeta functionMeta, InvokeOptions opt, Object... args) throws YRException;

    /**
     * Invokes a function on an instance with the given arguments and options.
     *
     * @param functionMeta The metadata of the function to be invoked
     * @param instanceId   The ID of the instance on which the function will be
     *                     invoked
     * @param args         The list of arguments to be passed to the function
     * @param opt          The options for the invocation
     * @return A string representing the result of the invocation
     * @throws YRException if an error occurs during the invocation
     */
    String invokeInstance(FunctionMeta functionMeta, String instanceId, List<InvokeArg> args, InvokeOptions opt)
        throws YRException;

    /**
     * Cancel.
     *
     * @param objectId the object id
     */
    void cancel(String objectId);

    /**
     * Terminate instance.
     *
     * @param instanceId the instance id
     * @throws YRException the YR exception.
     */
    void terminateInstance(String instanceId) throws YRException;

    /**
     * Is on cloud boolean.
     *
     * @return the boolean
     */
    boolean isOnCloud();

    /**
     * Get real instance id.
     *
     * @param objectId the object id
     * @return the string representing the real instance id
     */
    String getRealInstanceId(String objectId);

    /**
     * Save real instance id.
     *
     * @param objectId the object id
     * @param instanceId the instance id
     * @param opts the invoke options
     */
    void saveRealInstanceId(String objectId, String instanceId, InvokeOptions opts);

    /**
     * Collect instance handler info.
     *
     * @param instanceHandler the instance handler
     */
    void collectInstanceHandlerInfo(InstanceHandler instanceHandler);

    /**
     * Collect instance handler info.
     *
     * @param javaInstanceHandler the java instance handler
     */
    void collectInstanceHandlerInfo(JavaInstanceHandler javaInstanceHandler);

    /**
     * Collect instance handler info.
     *
     * @param cppInstanceHandler the cpp instance handler
     */
    void collectInstanceHandlerInfo(CppInstanceHandler cppInstanceHandler);

    /**
     * Returns an InstanceHandler object that contains the Java instance handler
     * which is NOT terminated and associated with the specified instanceID.
     *
     * @param instanceID The instanceID that identifies the Java instance handler
     * @return An InstanceHandler object associated with the specified instanceID
     */
    InstanceHandler getInstanceHandlerInfo(String instanceID);

    /**
     * Finalizes all actors and tasks and release any resources associated with
     * them.
     */
    void Finalize();

    /**
     * In the runtimeCtx, finalizes all actors and tasks and release any resources
     * associated with them.
     *
     * @param runtimeCtx the runtimeCtx
     * @param leftRuntimeNum the leftRuntimeNum
     */
    void Finalize(String runtimeCtx, int leftRuntimeNum);

    /**
     * Exits the current instance. This function only supports in runtime.
     */
    void exit();

    /**
     * Returns the key-value manager instance.
     *
     * @return The key-value manager instance
     */
    KVManager getKVManager();

    /**
     * Get java function
     *
     * @param functionMeta function meta.
     * @return the function.
     * @throws YRException the YR exception.
     */
    FunctionWrapper getJavaFunction(FunctionMeta functionMeta) throws YRException;

    /**
     * Writes a key-value pair to the key-value store with the specified existence
     * option.
     *
     * @param key       The key of the key-value pair
     * @param value     The value of the key-value pair
     * @param setParam  The setParam option for datasystem stateClient, contain writeMode,
     *                  ttlSecond, existence and cacheType.
     * @throws YRException if an error occurs while performing the write operation
     */
    void KVWrite(String key, byte[] value, SetParam setParam) throws YRException;

    /**
     * Set Multiple key-value pairs.
     * Operations on all key-value pairs must either succeed or fail completely.
     *
     * @param keys      The keys of the key-value pair
     * @param values    The values of the key-value pair
     * @param mSetParam The mSetParam option for datasystem stateClient, contain writeMode,
     *                  ttlSecond, existence and cacheType.
     * @throws YRException if an error occurs while performing the write operation
     */
    void KVMSetTx(List<String> keys, List<byte[]> values, MSetParam mSetParam) throws YRException;

    /**
     * Reads the value associated with the specified key from the key-value store.
     *
     * @param key       The key to read value for
     * @param timeoutMS The maximum time to wait for the read operation to complete, in
     *                  milliseconds
     * @return A byte array representing the value associated with the given key
     * @throws YRException if an error occurs while performing the read operation
     */
    byte[] KVRead(String key, int timeoutMS) throws YRException;

    /**
     * Reads the values associated with the given keys from the key-value store.
     *
     * @param keys         The list of keys to read values for
     * @param timeoutMS    The maximum time to wait for the read operation to
     *                     complete, in milliseconds
     * @param allowPartial Specifies whether to allow partial reads or not
     * @return A list of byte arrays representing the values associated with the
     *         given keys
     * @throws YRException If an error occurs while performing the read operation
     */
    List<byte[]> KVRead(List<String> keys, int timeoutMS, boolean allowPartial) throws YRException;

    /**
     * Reads the values associated with the given keys and get params from the key-value store.
     *
     * @param keys The keys to retrieve values for
     * @param params The get params for datasystem
     * @param timeoutMS    The maximum time to wait for the read operation to
     *                     complete, in milliseconds
     * @return A list of byte arrays representing the values associated with the
     *         keys
     * @throws YRException if an error occurs while retrieving the values
     */
    List<byte[]> KVGetWithParam(List<String> keys, GetParams params, int timeoutMS) throws YRException;

    /**
     * Deletes a key-value pair.
     *
     * @param key The key of the pair to be deleted.
     * @throws YRException If an error occurs while performing the delete operation
     */
    void KVDel(String key) throws YRException;

    /**
     * Deletes multiple key-value pairs from the key-value store.
     *
     * @param keys A list of keys of the pairs to be deleted
     * @return A list of keys that were failed to be deleted
     */
    List<String> KVDel(List<String> keys);

    /**
     * loadState
     *
     * @param timeoutMs the timeoutMs
     * @throws YRException the YR exception.
     */
    void loadState(int timeoutMs) throws YRException;

    /**
     * saveState
     *
     * @param timeoutMs the timeoutMs
     * @throws YRException the YR exception.
     */
    void saveState(int timeoutMs) throws YRException;

    /**
     * groupCreate
     *
     * @param groupName the groupName
     * @param opts the GroupOptions
     * @throws YRException the YR exception.
     */
    void groupCreate(String groupName, GroupOptions opts) throws YRException;

    /**
     * groupTerminate
     *
     * @param groupName the groupName
     */
    void groupTerminate(String groupName);

    /**
     * groupWait
     *
     * @param groupName the group name
     * @throws YRException the YR exception.
     */
    void groupWait(String groupName) throws YRException;

    /**
     * Get instance route.
     *
     * @param objectId the object id
     * @return the string representing the instance route
     */
    String getInstanceRoute(String objectId);

    /**
     * Save instance route.
     *
     * @param objectId the object id
     * @param instanceRoute the instance route
     */
    void saveInstanceRoute(String objectId, String instanceRoute);

    /**
     * Sync terminate instance.
     *
     * @param instanceId the instance id
     * @throws YRException the YR exception.
     */
    void terminateInstanceSync(String instanceId) throws YRException;
}
