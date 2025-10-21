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

package com.yuanrong.api;

import com.yuanrong.Config;
import com.yuanrong.ConfigManager;
import com.yuanrong.CreateParam;
import com.yuanrong.YRCall;
import com.yuanrong.errorcode.ErrorCode;
import com.yuanrong.errorcode.ModuleCode;
import com.yuanrong.exception.YRException;
import com.yuanrong.jni.LibRuntime;
import com.yuanrong.jni.LibRuntimeConfig;
import com.yuanrong.jni.YRAutoInitInfo;
import com.yuanrong.runtime.ClusterModeRuntime;
import com.yuanrong.runtime.Runtime;
import com.yuanrong.runtime.client.KVManager;
import com.yuanrong.runtime.client.ObjectRef;
import com.yuanrong.runtime.config.RuntimeContext;
import com.yuanrong.runtime.util.Constants;
import com.yuanrong.storage.InternalWaitResult;
import com.yuanrong.storage.WaitResult;
import com.yuanrong.utils.SdkUtils;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Instance function utility api class, also considered to be the interface of YuanRong java api.
 * <p>
 * Public API.
 * Please note that it should not be modified without careful consideration.
 * Any modifications require strict review and testing.
 * </p>
 *
 * @since 2023/10/16
 */
public class YR extends YRCall {
    private static AtomicBoolean isGlobalInit = new AtomicBoolean(false);

    private static Logger LOGGER = LoggerFactory.getLogger(YR.class);

    private static ConcurrentHashMap<String, Runtime> runtimeCache = new ConcurrentHashMap<>();

    private static final String YR_NOT_INIT_MSG = "the current yr init status is abnormal, please init YR first";

    private static final String YR_INIT_TWICE_MSG = "Cannot init YR twice";

    private static final int DEFAULT_SAVE_LOAD_STATE_TIMEOUT = 30;

    private static final String YR_CFG_INVALID_MSG = "invalid config: isThreadLocal and enableSetContext cannot "
        + "be set to true at the same time";

    /**
     * Gets runtime.
     *
     * @return the runtime
     * @throws YRException the YR exception
     */
    public static Runtime getRuntime() throws YRException {
        String runtimeCtx = RuntimeContext.RUNTIME_CONTEXT.get();
        if (!runtimeCache.containsKey(runtimeCtx)) {
            if (LibRuntime.IsInitialized()) {
                Runtime rt = new ClusterModeRuntime();
                runtimeCache.put(runtimeCtx, rt);
                return rt;
            } else {
                throw new YRException(ErrorCode.ERR_INCORRECT_INIT_USAGE, ModuleCode.RUNTIME, YR_NOT_INIT_MSG);
            }
        } else {
            return runtimeCache.get(runtimeCtx);
        }
    }

    /**
     * The Yuanrong initialization interface is used to configure parameters.
     * For parameter descriptions, see the data structure Config.
     *
     * @param conf Initialization parameter configuration of Yuanrong.
     * @return Information returned to the user after init ends. ClientInfo class description:
     *         jobID, String type, the jobID of init, used for subsequent tracking and management of the current job
     *         context, String type, The tenant context is not empty only when `enableSetContext` is ``true``.
     * @throws YRException The Yuanrong cluster initialization failed due to configuration errors or
     *                            network connection issues.
     *
     * @snippet{trimleft} InitExample.java init conf 样例代码
     */
    public static ClientInfo init(Config conf) throws YRException {
        if (conf.isThreadLocal() && conf.isEnableSetContext()) {
            throw new YRException(ErrorCode.ERR_PARAM_INVALID, ModuleCode.RUNTIME, YR_CFG_INVALID_MSG);
        }
        String threadID = String.valueOf(Thread.currentThread().getId());
        if (conf.isEnableSetContext()) {
            String tenantContext = SdkUtils.getTenantContext(conf, threadID);
            return initInternal(tenantContext, conf);
        }
        if (conf.isThreadLocal()) {
            return initInternal(threadID, conf);
        }

        boolean success = isGlobalInit.compareAndSet(false, true);
        if (!success) {
            throw new YRException(ErrorCode.ERR_INCORRECT_INIT_USAGE, ModuleCode.RUNTIME, YR_INIT_TWICE_MSG);
        }
        try {
            return initInternal("", conf);
        } catch (YRException e) {
            isGlobalInit.set(false);
            runtimeCache.remove(RuntimeContext.RUNTIME_CONTEXT.get());
            throw e;
        }
    }

    /**
     * The Yuanrong initialization interface.
     *
     * @return ClientInfo
     * @throws YRException the YR exception
     */
    public static ClientInfo init() throws YRException {
        Config conf = new Config();
        return init(conf);
    }

    /**
     * put
     *
     * @param obj the T obj
     * @param <T> T type
     * @return ObjectRef
     * @throws YRException the YR exception.
     */
    public static <T> ObjectRef put(T obj) throws YRException {
        if (obj instanceof ObjectRef) {
            throw new IllegalArgumentException("put an objectRef is not allowed. But you can put a list of objectRefs");
        }
        return YR.getRuntime().put(obj, Constants.DEFAULT_PUT_INTERVAL_TIME, Constants.DEFAULT_RETRY_TIME);
    }

    /**
     * put
     *
     * @param obj the T obj.
     * @param <T> T type
     * @param createParam the create param of datasystem.
     * @return ObjectRef
     * @throws YRException the YR exception.
     */
    public static <T> ObjectRef put(T obj, CreateParam createParam) throws YRException {
        if (obj instanceof ObjectRef) {
            throw new IllegalArgumentException("put an objectRef is not allowed. But you can put a list of objectRefs");
        }
        return YR.getRuntime().put(obj, Constants.DEFAULT_PUT_INTERVAL_TIME, Constants.DEFAULT_RETRY_TIME, createParam);
    }

    /**
     * Get the value stored in the data system. This method will block for default timeout(300s) until the result is
     * obtained. If the result is not obtained within the timeout period, an exception is thrown.
     *
     * @param ref invoke method or YR.put return value, required.
     * @return Object The obtained object.
     * @throws YRException Error retrieving data, timeout or data system service unavailable.
     */
    public static Object get(ObjectRef ref) throws YRException {
        return YR.getRuntime().get(ref, Constants.DEFAULT_GET_TIMEOUT_SEC);
    }

    /**
     * Get the value stored in the data system. This method will block for timeoutSec seconds until the result is
     * obtained. If the result is not obtained within the timeout period, an exception is thrown. The timeout period
     * should be greater than 0, otherwise, an YRException("timeout is invalid, it needs greater than 0") will
     * be thrown immediately.
     *
     * @param ref invoke method or YR.put return value, required.
     * @param timeoutSec Timeout time for obtaining data， required， The value must be greater than 0.
     * @return Object The obtained object.
     * @throws YRException Error retrieving data, timeout or data system service unavailable.
     *
     * @snippet{trimleft} GetExample.java get example
     */
    public static Object get(ObjectRef ref, int timeoutSec) throws YRException {
        return YR.getRuntime().get(ref, timeoutSec);
    }

    /**
     * Get list.
     *
     * @param refs the refs.
     * @param timeoutSec the timeout.
     * @return the list.
     * @throws YRException the YR exception.
     */
    public static List<Object> get(List<ObjectRef> refs, int timeoutSec) throws YRException {
        return YR.getRuntime().get(refs, timeoutSec, false);
    }

    /**
     * Get list.
     *
     * @param refs         the refs
     * @param timeoutSec      the timeout
     * @param allowPartial allow partial success get
     * @return the list
     * @throws YRException the YR exception
     */
    public static List<Object> get(List<ObjectRef> refs, int timeoutSec, boolean allowPartial)
            throws YRException {
        return YR.getRuntime().get(refs, timeoutSec, allowPartial);
    }

    /**
     * Wait for the results to return. This method returns when the number of returned results reaches waitNum or the
     * wait time exceeds timeoutSec.
     *
     * @param refs ObjectRef list.
     * @param waitNum The number of results to be returned. The value must be greater than 0.
     * @param timeoutSec The timeout time for waiting, in seconds. The value must be greater than or equal to 0 or
     *                   equal to -1.
     * @return Store the returned results. Use getReady() to get a list of ObjectRef that can be retrieved,
     *         and use getUnready() to get a list of ObjectRef that cannot be retrieved.
     * @throws YRException Unified exception types thrown..
     *
     * @snippet{trimleft} WaitExample.java wait result
     */
    public static WaitResult wait(List<ObjectRef> refs, int waitNum, int timeoutSec) throws YRException {
        if (refs == null || refs.isEmpty()) {
            throw new YRException(ErrorCode.ERR_PARAM_INVALID, ModuleCode.RUNTIME,
                "refs cannot be null or empty");
        }
        Map<String, ObjectRef> objectRefMap = new HashMap<>();
        List<String> objIds = new ArrayList<>();
        for (ObjectRef ref : refs) {
            objectRefMap.put(ref.getObjectID(), ref);
            objIds.add(ref.getObjectID());
        }
        InternalWaitResult internalWaitResult = YR.getRuntime().wait(objIds, waitNum, timeoutSec);
        List<ObjectRef> readyList = new ArrayList<>();
        List<ObjectRef> unreadyList = new ArrayList<>();
        for (String objId : internalWaitResult.getReadyIds()) {
            readyList.add(objectRefMap.get(objId));
        }
        for (String objId : internalWaitResult.getUnreadyIds()) {
            unreadyList.add(objectRefMap.get(objId));
        }
        return new WaitResult(readyList, unreadyList);
    }

    /**
     * Switch tenant context.
     *
     * @note The instance handle created in tenant context ctx1 can only be called and get results in tenant context
     *       ctx1. If you switch to tenant context ctx2 (same cluster required), you can transfer the handle and call it
     *       through the exportHandler and importHandler methods. For details, refer to the sample code.
     *
     * @param ctx The tenant context that needs to be switched (the tenant context can be obtained through the return
     *            value of YR.init).
     * @throws YRException The switched tenant context must exist and be initialized, otherwise an
     *                            YRException is thrown.
     *
     * @snippet{trimleft} SetContextExample.java change context
     *
     * @snippet{trimleft} SetContextExample.java change context and invoke
     *
     * @snippet{trimleft} SetContextExample.java change context and invoke with export and import
     */
    public static void setContext(String ctx) throws YRException {
        if (!runtimeCache.containsKey(ctx)) {
            throw new YRException(ErrorCode.ERR_INCORRECT_INIT_USAGE, ModuleCode.RUNTIME, YR_NOT_INIT_MSG);
        }
        RuntimeContext.RUNTIME_CONTEXT.set(ctx);
        String jobID = ConfigManager.getInstance().getJobId();
        LibRuntime.setRuntimeContext(jobID);
    }

    /**
     * Deprecated. The Yuanrong termination interface releases resources.
     *
     * @param ctx Optional parameter: The tenant context to be terminated
     * @throws YRException Calling `Finalize` before `init`,
     * will throw an exception: YRException ("cannot Finalize before init").
     */
    @Deprecated
    public static synchronized void Finalize(String ctx) throws YRException {
        finalize(ctx);
    }

    /**
     * The Yuanrong termination interface releases resources,
     * including created function instances, to prevent resource leaks.
     *
     * @param ctx Optional parameter: The tenant context to be terminated
     * (can be obtained from the return value of YR.init).
     * @throws YRException Calling `finalize` before `init`, will throw an exception:
     * YRException ("cannot finalize before init").
     *
     * @snippet{trimleft} FinalizeExample.java finalize ctx 样例代码
     */
    public static synchronized void finalize(String ctx) throws YRException {
        if (!runtimeCache.containsKey(ctx)) {
            throw new YRException(ErrorCode.ERR_INCORRECT_INIT_USAGE, ModuleCode.RUNTIME, YR_NOT_INIT_MSG);
        }
        Runtime rt = runtimeCache.get(ctx);
        rt.Finalize(ctx, runtimeCache.size() - 1);
        runtimeCache.remove(ctx);
    }

    /**
     * The Yuanrong termination interface releases resources,
     * including created function instances, to prevent resource leaks.
     *
     * @throws YRException Calling `Finalize` before `init`,
     * or passing a tenant context parameter when the tenant context is not initialized,
     * will both throw an exception: YRException ("cannot Finalize before init").
     *
     * @snippet{trimleft} FinalizeExample.java Finalize class
     * @snippet{trimleft} FinalizeExample.java Finalize 样例代码
     */
    public static void Finalize() throws YRException {
        if (RuntimeContext.RUNTIME_CONTEXT.get().isEmpty()) {
            boolean success = isGlobalInit.compareAndSet(true, false);
            if (!success) {
                throw new YRException(ErrorCode.ERR_FINALIZED, ModuleCode.RUNTIME, YR_NOT_INIT_MSG);
            }
        }
        getRuntime().Finalize();
        runtimeCache.remove(RuntimeContext.RUNTIME_CONTEXT.get());
    }

    /**
     * Exit the current instance. This function only supports in runtime.
     *
     * @throws YRException if this function is called not in a cluster
     */
    @Deprecated
    public static void Exit() throws YRException {
        exit();
    }

    /**
     * Exit the current instance. This function only supports in runtime.
     *
     * @throws YRException if this function is called not in a cluster
     */
    public static void exit() throws YRException {
        ConfigManager configManager = ConfigManager.getInstance();
        if (configManager.isInitialized() && !configManager.isInCluster()) {
            throw new YRException(ErrorCode.ERR_INCORRECT_FUNCTION_USAGE, ModuleCode.RUNTIME,
                "Not support exit out of cluster");
        }
        YR.getRuntime().exit();
    }

    /**
     * Returns the KVManager instance from the current runtime.
     *
     * @return The KVManager instance from the current runtime
     * @throws YRException If an error occurs when Failed to get runtime
     */
    public static KVManager kv() throws YRException {
        return YR.getRuntime().getKVManager();
    }

    /**
     * Save the state of the runtime with a timeout.
     *
     * @param timeoutSec the timeout in seconds.
     * @throws YRException if there is an exception during state saving.
     */
    public static void saveState(int timeoutSec) throws YRException {
        YR.getRuntime().saveState(timeoutSec);
    }

    /**
     * Save the state of the runtime with Defualt timeout(30s).
     *
     * @throws YRException if there is an exception during state saving.
     */
    public static void saveState() throws YRException {
        YR.getRuntime().saveState(DEFAULT_SAVE_LOAD_STATE_TIMEOUT);
    }

    /**
     * Load state of the runtime with a timeout.
     *
     * @param timeoutSec the timeout in seconds.
     * @throws YRException if there is an exception during state loading.
     */
    public static void loadState(int timeoutSec) throws YRException {
        YR.getRuntime().loadState(timeoutSec);
    }

    /**
     * Load state of the runtime with default timeout(30s).
     *
     * @throws YRException if there is an exception during state loading.
     */
    public static void loadState() throws YRException {
        YR.getRuntime().loadState(DEFAULT_SAVE_LOAD_STATE_TIMEOUT);
    }

    private static ClientInfo initInternal(String runtimeCtx, Config conf) throws YRException {
        RuntimeContext.RUNTIME_CONTEXT.set(runtimeCtx);
        if (runtimeCache.get(runtimeCtx) != null) {
            throw new YRException(ErrorCode.ERR_INCORRECT_INIT_USAGE, ModuleCode.RUNTIME, YR_INIT_TWICE_MSG);
        }
        // Check whether yr master needs to be started
        if (conf.isDriver()) {
            YRAutoInitInfo info = conf.buildClusterAccessInfo();
            YRAutoInitInfo autoinfo = LibRuntime.AutoInitYR(info);
            if (autoinfo != null) {
                conf.setServerAddress(autoinfo.getFunctionSystemServerIpAddr());
                conf.setServerAddressPort(autoinfo.getFunctionSystemServerPort());
                conf.setDataSystemAddress(autoinfo.getDataSystemIpAddr());
                conf.setDataSystemAddressPort(autoinfo.getDataSystemPort());
            }
        }

        String codePathConf = System.getProperty("yr.codePath");
        if (codePathConf != null && !codePathConf.isEmpty()) {
            List<String> pathsList = new ArrayList<>();
            String[] paths = codePathConf.split(",");
            for (String path : paths) {
                pathsList.add(path.trim());
            }
            conf.setCodePath(pathsList);
        }

        conf.checkParameter();
        ConfigManager configManager = ConfigManager.getInstance();
        configManager.init(conf);
        // init LibRuntime
        LibRuntimeConfig libConfig = SdkUtils.getLibRuntimeConfig(configManager);
        LOGGER.debug("server ip: {} and port: {} from libConfig", libConfig.getFunctionSystemRtServerIpAddr(),
                libConfig.getFunctionSystemRtServerPort());
        LOGGER.debug("datasystem server ip: {} and port:{} from libConfig", libConfig.getDataSystemIpAddr(),
                libConfig.getDataSystemPort());

        Runtime localRt = new ClusterModeRuntime();
        localRt.init(libConfig);
        runtimeCache.put(runtimeCtx, localRt);

        String jobID = configManager.getJobId();
        String ctx = conf.isEnableSetContext() ? runtimeCtx : "";
        ClientInfo info = new ClientInfo();
        info.setJobID(jobID);
        info.setContext(ctx);
        LOGGER.debug("Succeeded to init YR, jobID is {}, tenant context is {}", jobID, ctx);
        return info;
    }
}
