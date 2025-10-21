/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */
#include <string>
#include "base/utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "user_common_func.h"
#include "yr/yr.h"
#include "utils.h"


class AlwaysLocalModeTest : public testing::Test {
public:
    AlwaysLocalModeTest(){};
    ~AlwaysLocalModeTest(){};
    static void SetUpTestCase(){};
    static void TearDownTestCase(){};
    void SetUp()
    {
        YR::Config config;
        config.mode = YR::Config::Mode::CLUSTER_MODE;
        config.logLevel = "DEBUG";
        auto info = YR::Init(config);
        std::cout << "job id: " << info.jobId << std::endl;
    };
    void TearDown()
    {
        YR::Finalize();
    };
};


/*
* @precondition: cluster mode模式
* @step:  1.clustermode中部分invoke设置alwaysLocalMode，创建无状态函数，将不设置的和设置的分开wait、get
* @step:  2.验证设置true的是否在本地线程池执行
* @expect:  1.调用成功，校验成功
* @title: 有状态alwaysLocalMode打开，进行wait、get
*/
TEST_F(AlwaysLocalModeTest, cpp_actor_alwayslocalmode_true)
{
    YR::InvokeOptions opt;
    opt.alwaysLocalMode = true;
    auto instance = YR::Instance(Counter::FactoryCreate).Options(std::move(opt)).Invoke(1);
    auto ref = instance.Function(&Counter::Add).Invoke(10);
    EXPECT_EQ(*YR::Get(ref), 11);
    EXPECT_NO_THROW(instance.Terminate());
}
