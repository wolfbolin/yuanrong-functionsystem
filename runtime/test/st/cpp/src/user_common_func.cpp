/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include "user_common_func.h"

int AddOne(int x)
{
    extern char **environ;
    for (char **env = environ; *env != 0; env++) {
        char *thisEnv = *env;
        std::cout << thisEnv << std::endl << std::flush;
    }
    return x + 1;
}

std::vector<char> BigBox(std::vector<char> bigParam)
{
    return bigParam;
}

int PlusOne(int x)
{
    return x + 1;
}

int RaiseRuntimeError()
{
    throw std::runtime_error("FAILED");
}

int Add(int x, int y)
{
    return x + y;
}

int AddAfterSleep(int x)
{
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return x + 1;
}

int AddAfterSleepTen(int x)
{
    std::this_thread::sleep_for(std::chrono::seconds(10));
    return x + 1;
}

int InvokeAndCancel_AddAfterSleepTen(int x)
{
    auto r = YR::Function(AddAfterSleepTen).Invoke(x);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    YR::Cancel(r);
    try {
        YR::Get(r);
    } catch (...) {
        return 1;
    }
    return 0;
}

int AfterSleepSec(int x)
{
    std::this_thread::sleep_for(std::chrono::seconds(x));
    return x + 1;
}

const std::string execBigArgsError = "exec big args error";

int ExecBigArgsAndFailed(std::vector<char> v)
{
    throw std::runtime_error(execBigArgsError);
    return 0;
}

int AddTwo(int x)
{
    return x + 2;
}

int SumVec(std::vector<YR::ObjectRef<int>> vec)
{
    int sum = 0;
    for (YR::ObjectRef<int> obj : vec) {
        sum += *YR::Get(obj);
    }
    return sum;
}

int RemoteAdd(std::vector<YR::ObjectRef<int>> vec)
{
    auto r2 = YR::Function(SumVec).Invoke(vec);
    return *YR::Get(r2);
}

YR_INVOKE(AddOne, PlusOne, RaiseRuntimeError, Add, AddAfterSleep, AddAfterSleepTen, InvokeAndCancel_AddAfterSleepTen,
    AddTwo, SumVec, RemoteAdd, AfterSleepSec, ExecBigArgsAndFailed, BigBox)

int Counter::Add(int x)
{
    std::cout << "start to add" << std::endl;
    count += x;
    std::cout << "end to add" << std::endl;
    return count;
}

int Counter::AddTwo(int x, int y)
{
    std::cout << "start to add" << std::endl;
    count += x;
    count += y;
    std::cout << "end to add" << std::endl;
    return count;
}

int Counter::AddRef(std::vector<YR::ObjectRef<int>> x)
{
    std::cout << "start to add" << std::endl;
    for (auto &m : x) {
        count += *YR::Get(m);
    }
    std::cout << "end to add" << std::endl;
    return count;
}

int Counter::Sleep()
{
    std::cout << "start to add" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(10));
    std::cout << "end to add" << std::endl;
    return count;
}

int Counter::Raise()
{
    throw std::runtime_error("FAILED");
}

int Counter::SEGV()
{
    char *p = nullptr;
    std::cout << *p << std::endl;
    return 0;
}

std::string Counter::GetDir()
{
    return std::getenv("INSTANCE_WORK_DIR");
}

std::string Counter::returnActorEnvVar(std::string key)
{
    if (const char *env = std::getenv(key.c_str())) {
        return std::string(env);
    }
    return "";
}

void Counter::Shutdown(uint64_t gracePeriodSecond)
{
    YR::KV().Set("shutdownKey", "shutdownValue");
}

int Counter::GetSigterm()
{
    auto r = YR::Function(ExcSIGTERM).Invoke();
    return *YR::Get(r);
}

YR_INVOKE(&Counter::GetSigterm);

int Counter::SaveState()
{
    YR::SaveState();
    return count;
}

int Counter::SaveGroupState()
{
    YR::InvokeOptions option;
    option.cpu = 500;
    option.memory = 500;
    option.preferredPriority = false;
    option.customExtensions["Concurrency"] = "100";
    // range schedule
    YR::InstanceRange range;
    range.max = 5;
    range.min = 5;
    range.step = 1;
    option.instanceRange = range;
    option.recoverRetryTimes = 100;
    option.labels.push_back("anti_label");
    // anti affinity
    auto anti = YR::InstancePreferredAntiAffinity(YR::LabelExistsOperator("anti_label"));
    option.AddAffinity(anti);
    auto instances = YR::Instance(CounterB::FactoryCreate).Options(std::move(option)).Invoke(1);
    ranges.emplace_back(instances);
    YR::SaveState();
    return count;
}

int Counter::LoadState()
{
    YR::LoadState();
    return count;
}

YR_INVOKE(&Counter::SaveState, &Counter::SaveGroupState, &Counter::LoadState, &Counter::returnActorEnvVar);

void Counter::Recover()
{
    std::cout << "recover" << std::endl;
    recoverFlag++;
}

YR_RECOVER(&Counter::Recover);

int Counter::GetRecoverFlag()
{
    return recoverFlag;
}

int Counter::GetGroupRecoverFlag()
{
    int results = 0;
    for (auto &instances:ranges) {
        auto instanceList = instances.GetInstances();
        for (auto &instance:instanceList) {
            auto member = instance.Function(&CounterB::Add).Invoke(1);
            results += *YR::Get(member);
        }
    }
    return recoverFlag + results;
}

YR_INVOKE(&Counter::GetRecoverFlag, &Counter::GetGroupRecoverFlag);

size_t Counter::GetPid()
{
    return getpid();
}

YR_INVOKE(&Counter::GetPid);

int Counter::AddRange(int max, int min, int step, bool sameLifecycle, int timeout, int getTimeout)
{
    std::cout << "start to add" << std::endl;
    YR::InstanceRange range;
    range.max = max;
    range.min = min;
    range.step = step;
    range.sameLifecycle = sameLifecycle;
    YR::RangeOptions rangeOpts;
    rangeOpts.timeout = timeout;
    range.rangeOpts = rangeOpts;
    YR::InvokeOptions opt;
    opt.instanceRange = range;
    auto instances = YR::Instance(Counter::FactoryCreate).Options(opt).Invoke(1);
    try {
        auto insList = instances.GetInstances(getTimeout);
        for (auto ins : insList) {
            auto res = ins.Function(&Counter::Add).Invoke(1);
            int ret = *YR::Get(res, 200);
            count += ret * 10;
            std::cout << "res is " << ret << std::endl;
        }
    } catch (YR::Exception &e) {
        instances.Terminate();
        throw e;
    }
    instances.Terminate();
    std::cout << "end to add" << std::endl;
    return count;
}

YR_INVOKE(Counter::FactoryCreate, &Counter::Add, &Counter::AddRef, &Counter::Sleep, &Counter::SEGV, &Counter::Raise,
    &Counter::AddTwo, &Counter::GetDir, &Counter::AddRange)
YR_SHUTDOWN(&Counter::Shutdown);

int CounterB::Add(int x)
{
    count = count + x;
    return count;
}
int CounterB::GetCount()
{
    return count;
}

int CounterB::ParallelFor()
{
    auto f = [this](size_t start, size_t end, const YR::Parallel::Context &ctx) {
        for (size_t i = start; i < end; i++) {
            std::lock_guard<std::mutex> lock(ctxIdSetMutex);
            ctxIdSet.emplace(ctx.id);
        }
        std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    };
    YR::Parallel::ParallelFor<size_t>(0, 10000, f, 1);
    return 0;
}

YR_INVOKE(
    CounterB::FactoryCreate, &CounterB::Add, &CounterB::GetCount, &CounterB::ParallelFor, &CounterB::GetCtxIdsSize);

int CounterA::Add(int x)
{
    countA = countA + x;
    auto r = instance.Function(&CounterB::Add).Invoke(x);
    return countA + *YR::Get(r);
}
int CounterA::TerminateB(int x)
{
    instance.Terminate();
    return 1;
}
int CounterA::GetCountB()
{
    auto r = instance.Function(&CounterB::GetCount).Invoke();
    return *YR::Get(r);
}
int CounterA::GetCountA()
{
    return countA;
}

YR_INVOKE(CounterA::FactoryCreate, &CounterA::Add, &CounterA::GetCountB, &CounterA::GetCountA, &CounterA::TerminateB)

// C->A->B
int CounterC::Add(int x)
{
    countA = countA + x;
    auto r = instanceA.Function(&CounterA::Add).Invoke(x);
    return 1;
}
int CounterC::GetCountA()
{
    auto r = instanceA.Function(&CounterA::GetCountA).Invoke();
    return *YR::Get(r);
}
int CounterC::GetCountB()
{
    auto r = instanceA.Function(&CounterA::GetCountB).Invoke();
    return *YR::Get(r);
}
int CounterC::GetCountC()
{
    return countC;
}
int CounterC::ChainTerminate()
{
    auto r = instanceA.Function(&CounterA::TerminateB).Invoke(1);
    return *YR::Get(r);
}
int CounterC::TerminateA(int x)
{
    instanceA.Terminate();
    return 1;
}
YR_INVOKE(CounterC::FactoryCreate, &CounterC::Add, &CounterC::GetCountC, &CounterC::GetCountA, &CounterC::GetCountB,
    &CounterC::ChainTerminate, &CounterC::TerminateA);

int ExcDivision()
{
    raise(SIGFPE);
}
YR_INVOKE(ExcDivision);

int ExcExit()
{
    abort();
}
YR_INVOKE(ExcExit);

int ExcChain()
{
    auto r1 = YR::Function(ExcDivision).Invoke();
    return *YR::Get(r1);
}
YR_INVOKE(ExcChain);

int Excdying()
{
    auto r1 = YR::Function(ExcExit).Invoke();
    return *YR::Get(r1);
}
YR_INVOKE(Excdying);

int ExcfailMethod()
{
    auto error_message1 = "dependent task failed";
    throw error_message1;
}
YR_INVOKE(ExcfailMethod);

int ExcMethod()
{
    auto r1 = YR::Function(ExcfailMethod).Invoke();
    return *YR::Get(r1);
}
YR_INVOKE(ExcMethod);

int PlusOneFinalize(int x)
{
    YR::Finalize();
    return x + 1;
}
YR_INVOKE(PlusOneFinalize);

int ExcSIGILL()
{
    raise(SIGILL);
}
YR_INVOKE(ExcSIGILL);

int ExcSIGINT()
{
    raise(SIGINT);
}
YR_INVOKE(ExcSIGINT);

int ExcSIGSEGV()
{
    raise(SIGSEGV);
}
YR_INVOKE(ExcSIGSEGV);

int ExcSIGTERM()
{
    raise(SIGTERM);
}
YR_INVOKE(ExcSIGTERM);

int Sum(std::vector<int> a)
{
    int sum = 0;
    for (int &i : a) {
        sum += i;
    }
    return sum;
}

int SumWithObjectRef(std::vector<YR::ObjectRef<int>> a)
{
    int sum = 0;
    for (auto &i : a) {
        sum += *YR::Get(i);
    }
    return sum;
}

YR_INVOKE(Sum, SumWithObjectRef);

std::vector<YR::ObjectRef<int>> DirectReturn()
{
    return std::vector<YR::ObjectRef<int>>{YR::Function(Add).Invoke(1, 1)};
}

YR_INVOKE(DirectReturn);

int Actor::get_sigill()
{
    auto r1 = YR::Function(ExcSIGILL).Invoke();
    return *YR::Get(r1);
}

int Actor::get_sigint()
{
    auto r1 = YR::Function(ExcSIGINT).Invoke();
    return *YR::Get(r1);
}

int Actor::get_sigsegv()
{
    auto r1 = YR::Function(ExcSIGSEGV).Invoke();
    return *YR::Get(r1);
}

int Actor::get_sigterm()
{
    auto r1 = YR::Function(ExcSIGTERM).Invoke();
    return *YR::Get(r1);
}
YR_INVOKE(Actor::FactoryCreate, &Actor::get_sigill, &Actor::get_sigint, &Actor::get_sigsegv, &Actor::get_sigterm);

int FailedForNTimesAndThenSuccess(int n)
{
    std::string key = "counter";
    std::string val;
    try {
        val = YR::KV().Get(key, 0);
    } catch (YR::Exception &e) {
        auto n_str = std::to_string(1);
        YR::KV().Set(key, n_str);
        throw std::runtime_error("failed for " + n_str + " times");
    }

    int counter = std::stoi(val);
    if (counter < n) {
        auto n_str = std::to_string(counter + 1);
        YR::KV().Set(key, n_str);
        throw std::runtime_error("failed for " + n_str + " times");
    }
    return 0;
}

YR_INVOKE(FailedForNTimesAndThenSuccess)

YR_INVOKE(Adder::FactoryCreate, &Adder::Add, &Adder::Get)
YR_INVOKE(AdderProxy::FactoryCreate, &AdderProxy::SetAdder, &AdderProxy::SetAdderByName, &AdderProxy::Add)
YR_INVOKE(CounterProxy::FactoryCreate, &CounterProxy::Add, &CounterProxy::GetCounterAndInvoke)

int FunctionNotRegistered()
{
    return 0;
}

int FunctionRegistered()
{
    auto ret = YR::Function(&FunctionNotRegistered).Invoke();
    return *YR::Get(ret);
}

int KVGetPartKeysSuccess(bool x)
{
    printf("====kv.get多个key部分成功=====\n");
    std::string key;
    std::string value;
    std::string resVal;
    std::vector<std::string> keys;
    for (int i = 0; i < 10; ++i) {
        key = "key" + std::to_string(i);
        value = "value" + std::to_string(i);
        try {
            YR::KV().Set(key, value);
        } catch (YR::Exception &e) {
            std::cout << e.what() << std::endl;
            return 0;
        }
        keys.push_back(key);
    }
    keys.push_back("noValueKey1");
    keys.push_back("noValueKey2");
    keys.push_back("noValueKey3");
    std::vector<std::string> returnVal = YR::KV().Get(keys, 1, x);
    for (int i = 0; i < 10; ++i) {
        resVal = "value" + std::to_string(i);
        std::cout << i << "-> kv get value is: " << returnVal[i] << std::endl;
        if (returnVal[i] != resVal) {
            return 0;
        }
    }
    for (int i = 10; i < returnVal.size(); ++i) {
        std::cout << i << "-> kv get value is: " << returnVal[i] << std::endl;
        if (returnVal[i] != "") {
            return 0;
        }
    }
    YR::KV().Del(keys);
    return 1;
}

std::string InvokeReturnCustomEnvs(std::string key)
{
    auto ref = YR::Function(ReturnCustomEnvs).Invoke(key);
    auto runtimeEnv = *YR::Get(ref);
    auto curEnv = ReturnCustomEnvs(key);
    if (runtimeEnv == curEnv) {
        return curEnv;
    }
    return "";
}

std::string ReturnCustomEnvs(std::string key)
{
    if (const char *env = std::getenv(key.c_str())) {
        return std::string(env);
    }
    return "";
}

std::string PutOneData()
{
    std::string data(1024, 'a');
    auto r1 = YR::Put(data);
    auto res = *YR::Get(r1);
    if (res != data) {
        return "";
    }
    return res;
}

YR_INVOKE(FunctionRegistered)

YR_INVOKE(KVGetPartKeysSuccess);

YR_INVOKE(ReturnCustomEnvs, InvokeReturnCustomEnvs)

YR_INVOKE(PutOneData)

int CallLocal(int x)
{
    YR::InvokeOptions opt;
    opt.alwaysLocalMode = true;
    auto obj = YR::Function(AddOne).Options(opt).Invoke(x);
    int ret = *YR::Get(obj);
    std::cout << "CallLocal result: " << ret << std::endl;
    return ret;
}

int CallCluster(int x)
{
    auto obj = YR::Function(AddOne).Invoke(x);
    int ret = *YR::Get(obj);
    std::cout << "CallCluster result: " << ret << std::endl;
    return ret;
}

YR_INVOKE(CallLocal, CallCluster)
