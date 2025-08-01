/*
 *
 * Copyright (C) 2020-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include <chrono>
#include <ctime>
#include <thread>
#include <atomic>
#include <cmath>

#include <boost/filesystem.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/sync/named_condition.hpp>

#include "gtest/gtest.h"

#include "logging/logging.hpp"
#include "utils/utils.hpp"
#include "test_harness/test_harness.hpp"
#include "test_metric_utils.hpp"

namespace lzt = level_zero_tests;
namespace fs = boost::filesystem;
namespace bp = boost::process;
namespace bi = boost::interprocess;

#include <level_zero/ze_api.h>
#include <level_zero/zet_api.h>

namespace {

static constexpr uint32_t nanoSecToSeconds = 1000000000;
std::atomic<bool> workloadThreadFlag(false);

void workloadThread(ze_command_queue_handle_t cq, uint32_t numCommandLists,
                    ze_command_list_handle_t *phCommandLists,
                    ze_fence_handle_t hFence) {
  while (workloadThreadFlag) {
    lzt::execute_command_lists(cq, 1, phCommandLists, nullptr);
    lzt::synchronize(cq, std::numeric_limits<uint64_t>::max());
  }
}

class zetMetricGroupTest : public ::testing::Test {
protected:
  ze_device_handle_t device = lzt::zeDevice::get_instance()->get_device();
  std::vector<ze_device_handle_t> devices;

  zetMetricGroupTest() {}
  ~zetMetricGroupTest() {}

  void SetUp() { devices = lzt::get_metric_test_device_list(); }

  void run_activate_deactivate_test(bool reactivate);
};

LZT_TEST_F(
    zetMetricGroupTest,
    GivenValidMetricGroupWhenReadingClockResolutionAndBitsThenResultsDependOnDomain) {

  for (auto deviceh : devices) {

    ze_device_properties_t deviceProperties = {
        ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES, nullptr};
    zeDeviceGetProperties(deviceh, &deviceProperties);
    LOG_INFO << "test device name " << deviceProperties.name << " uuid "
             << lzt::to_string(deviceProperties.uuid);
    if (deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE) {
      LOG_INFO << "test subdevice id " << deviceProperties.subdeviceId;
    } else {
      LOG_INFO << "test device is a root device";
    }

    auto differentDomainsMetricGroupHandles =
        lzt::get_metric_groups_with_different_domains(deviceh, 1);
    zet_metric_group_properties_t metricGroupProp = {};
    metricGroupProp.stype = ZET_STRUCTURE_TYPE_METRIC_GROUP_PROPERTIES;
    metricGroupProp.pNext = nullptr;

    zet_metric_global_timestamps_resolution_exp_t metricsTimestampProperties =
        {};
    metricsTimestampProperties.stype =
        ZET_STRUCTURE_TYPE_METRIC_GLOBAL_TIMESTAMPS_RESOLUTION_EXP;
    metricsTimestampProperties.pNext = nullptr;
    metricGroupProp.pNext = &metricsTimestampProperties;

    if (differentDomainsMetricGroupHandles.size() < 2) {
      GTEST_SKIP() << "Not enough metric groups in different domains";
    }
    for (auto metricGroupHandle : differentDomainsMetricGroupHandles) {
      EXPECT_ZE_RESULT_SUCCESS(
          zetMetricGroupGetProperties(metricGroupHandle, &metricGroupProp));
      LOG_INFO << "Metric group name: " << metricGroupProp.name
               << ". Metric Domain: " << metricGroupProp.domain
               << ". Timer Resolution: "
               << metricsTimestampProperties.timerResolution << ". Valid Bits "
               << metricsTimestampProperties.timestampValidBits;
    }
  }
}

LZT_TEST_F(
    zetMetricGroupTest,
    GivenValidMetricGroupWhenReadingTimestampsThenResultsDependOnDomain) {

  for (auto deviceh : devices) {

    ze_device_properties_t deviceProperties = {
        ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES, nullptr};
    zeDeviceGetProperties(deviceh, &deviceProperties);
    LOG_INFO << "test device name " << deviceProperties.name << " uuid "
             << lzt::to_string(deviceProperties.uuid);
    if (deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE) {
      LOG_INFO << "test subdevice id " << deviceProperties.subdeviceId;
    } else {
      LOG_INFO << "test device is a root device";
    }

    auto differentDomainsMetricGroupHandles =
        lzt::get_metric_groups_with_different_domains(deviceh, 1);
    zet_metric_group_properties_t metricGroupProp = {};
    metricGroupProp.stype = ZET_STRUCTURE_TYPE_METRIC_GROUP_PROPERTIES;
    metricGroupProp.pNext = nullptr;

    uint64_t globalTimestamp;
    uint64_t metricTimestamp;
    ze_bool_t synchronizedWithHost;

    if (differentDomainsMetricGroupHandles.size() < 2) {
      GTEST_SKIP() << "Not enough metric groups in different domains";
    }
    for (auto metricGroupHandle : differentDomainsMetricGroupHandles) {
      EXPECT_ZE_RESULT_SUCCESS(
          zetMetricGroupGetProperties(metricGroupHandle, &metricGroupProp));
      synchronizedWithHost = true;
      LOG_INFO << "Metric group name: " << metricGroupProp.name
               << ". Metric Domain: " << metricGroupProp.domain;
      EXPECT_ZE_RESULT_SUCCESS(zetMetricGroupGetGlobalTimestampsExp(
          metricGroupHandle, synchronizedWithHost, &globalTimestamp,
          &metricTimestamp));
      LOG_INFO << "Host timestamp " << globalTimestamp
               << ". Metrics timestamp: " << metricTimestamp;

      synchronizedWithHost = false;
      LOG_INFO << "Metric group name: " << metricGroupProp.name
               << ". Metric Domain: " << metricGroupProp.domain;
      EXPECT_ZE_RESULT_SUCCESS(zetMetricGroupGetGlobalTimestampsExp(
          metricGroupHandle, synchronizedWithHost, &globalTimestamp,
          &metricTimestamp));
      LOG_INFO << "Device timestamp " << globalTimestamp
               << ". Metrics timestamp: " << metricTimestamp;
    }
  }
}

LZT_TEST_F(
    zetMetricGroupTest,
    GivenValidEventBasedMetricGroupWhenvalidGroupNameIsrequestedThenExpectMatchingMetricHandle) {

  std::vector<std::string> groupNameList;
  groupNameList = lzt::get_metric_group_name_list(
      device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_EVENT_BASED, false);
  ASSERT_GT(groupNameList.size(), 0u) << "Metric group name list is empty";
  for (auto groupName : groupNameList) {
    zet_metric_group_handle_t testMatchedGroupHandle = lzt::find_metric_group(
        device, groupName, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_EVENT_BASED);
    EXPECT_NE(nullptr, testMatchedGroupHandle);
  }
}

LZT_TEST_F(
    zetMetricGroupTest,
    GivenValidEventBasedMetricGroupWhenvalidGroupNameIsrequestedThenExpectMetricsValidationsToSucceed) {

  std::vector<std::string> groupNameList;
  groupNameList = lzt::get_metric_group_name_list(
      device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_EVENT_BASED, false);
  ASSERT_GT(groupNameList.size(), 0u) << "Metric group name list is empty";
  for (auto groupName : groupNameList) {
    zet_metric_group_handle_t testMatchedGroupHandle = lzt::find_metric_group(
        device, groupName, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_EVENT_BASED);
    EXPECT_NE(nullptr, testMatchedGroupHandle);
    EXPECT_TRUE(lzt::validateMetricsStructures(testMatchedGroupHandle));
  }
}

LZT_TEST_F(
    zetMetricGroupTest,
    GivenValidTimeBasedMetricGroupWhenvalidGroupNameIsrequestedThenExpectMatchingMetricHandle) {

  std::vector<std::string> groupNameList;
  groupNameList = lzt::get_metric_group_name_list(
      device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED, true);
  ASSERT_GT(groupNameList.size(), 0u) << "Metric group name list is empty";
  for (auto groupName : groupNameList) {
    zet_metric_group_handle_t testMatchedGroupHandle = lzt::find_metric_group(
        device, groupName, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED);
    EXPECT_NE(nullptr, testMatchedGroupHandle);
  }
}

LZT_TEST_F(
    zetMetricGroupTest,
    GivenValidTimeBasedMetricGroupWhenvalidGroupNameIsrequestedThenExpectMatchingMetricsValidationsToSucceed) {

  std::vector<std::string> groupNameList;
  groupNameList = lzt::get_metric_group_name_list(
      device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED, true);
  LOG_INFO << "groupNameList size " << groupNameList.size();
  ASSERT_GT(groupNameList.size(), 0u) << "Metric group name list is empty";
  for (auto groupName : groupNameList) {
    LOG_INFO << "testing metric groupName " << groupName;
    zet_metric_group_handle_t testMatchedGroupHandle = lzt::find_metric_group(
        device, groupName, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED);
    EXPECT_NE(nullptr, testMatchedGroupHandle);
    EXPECT_TRUE(lzt::validateMetricsStructures(testMatchedGroupHandle));
  }
}

LZT_TEST_F(
    zetMetricGroupTest,
    GivenValidMetricGroupWhenValidGroupNameIsRequestedThenExpectGroupActivationAndDeactivationToSucceed) {

  std::vector<zet_metric_group_handle_t> groupHandleList;
  groupHandleList = lzt::get_metric_group_handles(device);
  ASSERT_GT(groupHandleList.size(), 0u) << "No metric group handles found";
  for (zet_metric_group_handle_t groupHandle : groupHandleList) {
    lzt::activate_metric_groups(device, 1, &groupHandle);
    lzt::deactivate_metric_groups(device);
  }
}

void zetMetricGroupTest::run_activate_deactivate_test(bool reactivate) {
  bool test_executed = false;
  std::vector<zet_metric_group_handle_t> groupHandleList;
  groupHandleList = lzt::get_metric_group_handles(device);
  ASSERT_GT(groupHandleList.size(), 2u)
      << "Not enough metric groups to test multiple groups activation";
  zet_metric_group_properties_t metric_group_properties = {};
  metric_group_properties.stype = ZET_STRUCTURE_TYPE_METRIC_GROUP_PROPERTIES;
  metric_group_properties.pNext = nullptr;
  EXPECT_ZE_RESULT_SUCCESS(zetMetricGroupGetProperties(
      groupHandleList[0], &metric_group_properties));
  auto domain = metric_group_properties.domain;
  auto domain_2 = 0;
  std::vector<zet_metric_group_handle_t> test_handles{groupHandleList[0]};
  for (zet_metric_group_handle_t groupHandle : groupHandleList) {
    metric_group_properties = {};
    metric_group_properties.stype = ZET_STRUCTURE_TYPE_METRIC_GROUP_PROPERTIES;
    metric_group_properties.pNext = nullptr;
    EXPECT_ZE_RESULT_SUCCESS(
        zetMetricGroupGetProperties(groupHandle, &metric_group_properties));
    if (metric_group_properties.domain != domain) {
      domain_2 = metric_group_properties.domain;
      // lzt::get_metric_group_properties(groupHandle);
      test_handles.push_back(groupHandle);
      lzt::activate_metric_groups(device, 2, test_handles.data());
      lzt::deactivate_metric_groups(device);

      if (reactivate) {
        LOG_INFO << "Deactivating then reactivating single metric group";
        lzt::activate_metric_groups(device, 1, test_handles.data());
        lzt::activate_metric_groups(device, 2, test_handles.data());
      }
      test_executed = true;
      break;
    }
  }
  if (!test_executed) {
    GTEST_SKIP() << "Not enough metric groups in different domains";
  } else {
    LOG_INFO << "Domain 1: " << domain << " Domain 2: " << domain_2;
  }
}

LZT_TEST_F(
    zetMetricGroupTest,
    GivenMetricGroupsInDifferentDomainWhenValidGroupIsActivatedThenExpectGroupActivationAndDeactivationToSucceed) {

  run_activate_deactivate_test(false);
}

LZT_TEST_F(
    zetMetricGroupTest,
    GivenMetricGroupsInDifferentDomainWhenValidGroupIsActivatedThenExpectGroupReActivationToSucceed) {

  run_activate_deactivate_test(true);
}

LZT_TEST_F(
    zetMetricGroupTest,
    GivenActiveMetricGroupsWhenActivatingSingleMetricGroupThenPreviouslyActiveGroupsAreDeactivated) {

  std::vector<zet_metric_group_handle_t> groupHandleList;
  groupHandleList = lzt::get_metric_group_handles(device);
  ASSERT_GT(groupHandleList.size(), 2u) << "Not enough metric groups available";

  zet_metric_group_properties_t metric_group_properties = {};
  metric_group_properties.stype = ZET_STRUCTURE_TYPE_METRIC_GROUP_PROPERTIES;
  metric_group_properties.pNext = nullptr;
  EXPECT_ZE_RESULT_SUCCESS(zetMetricGroupGetProperties(
      groupHandleList[0], &metric_group_properties));

  std::set<uint32_t> domains;
  domains.insert(metric_group_properties.domain);
  std::vector<zet_metric_group_handle_t> test_handles{groupHandleList[0]};

  for (zet_metric_group_handle_t groupHandle : groupHandleList) {
    metric_group_properties = {};
    metric_group_properties.stype = ZET_STRUCTURE_TYPE_METRIC_GROUP_PROPERTIES;
    metric_group_properties.pNext = nullptr;
    EXPECT_ZE_RESULT_SUCCESS(
        zetMetricGroupGetProperties(groupHandle, &metric_group_properties));
    if (domains.find(metric_group_properties.domain) == domains.end()) {
      domains.insert(metric_group_properties.domain);
      test_handles.push_back(groupHandle);
    }
  }

  if (domains.size() < 2) {
    GTEST_SKIP() << "Not enough domains, skipping the test";
  }

  LOG_DEBUG << "Activating all metrics groups selected for test";
  lzt::activate_metric_groups(device, test_handles.size(), test_handles.data());

  std::vector<zet_metric_group_handle_t> activeGroupHandleList;
  zet_metric_streamer_handle_t streamer;
  zet_metric_streamer_desc_t streamer_desc = {
      ZET_STRUCTURE_TYPE_METRIC_STREAMER_DESC, nullptr, 1000, 40000};

  LOG_DEBUG << "Verifying groups are active by attempting to open streamer";
  ASSERT_GT(test_handles.size(), 0u) << "No metric groups available to test";
  for (auto test_handle : test_handles) {
    ASSERT_ZE_RESULT_SUCCESS(
        zetMetricStreamerOpen(lzt::get_default_context(), device, test_handle,
                              &streamer_desc, nullptr, &streamer));

    ASSERT_ZE_RESULT_SUCCESS(zetMetricStreamerClose(streamer));
  }

  LOG_DEBUG << "Activating only first group";
  lzt::activate_metric_groups(device, 1, &test_handles[0]);

  LOG_DEBUG << "Verify only first group is active";
  ASSERT_ZE_RESULT_SUCCESS(
      zetMetricStreamerOpen(lzt::get_default_context(), device, test_handles[0],
                            &streamer_desc, nullptr, &streamer));
  ASSERT_ZE_RESULT_SUCCESS(zetMetricStreamerClose(streamer));

  for (auto test_handle : test_handles) {
    if (test_handle == test_handles[0]) {
      continue;
    }
    ASSERT_NE(ZE_RESULT_SUCCESS,
              zetMetricStreamerOpen(lzt::get_default_context(), device,
                                    test_handle, &streamer_desc, nullptr,
                                    &streamer));
  }

  // deactivate all groups
  lzt::deactivate_metric_groups(device);
}

LZT_TEST_F(
    zetMetricGroupTest,
    GivenValidMetricGroupWhenStreamerIsOpenedThenExpectStreamerToSucceed) {

  uint32_t notifyEveryNReports = 1000;
  uint32_t samplingPeriod = 40000;
  std::vector<std::string> groupNameList;
  groupNameList = lzt::get_metric_group_name_list(
      device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED, true);
  ASSERT_GT(groupNameList.size(), 0u) << "Metric group name list is empty";
  for (auto groupName : groupNameList) {
    zet_metric_group_handle_t groupHandle = lzt::find_metric_group(
        device, groupName, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED);
    ASSERT_NE(nullptr, groupHandle);
    LOG_DEBUG << "Activating group " << groupName;
    lzt::activate_metric_groups(device, 1, &groupHandle);
    LOG_DEBUG << "Opening streamer on Group" << groupName;
    zet_metric_streamer_handle_t streamerHandle = lzt::metric_streamer_open(
        groupHandle, nullptr, notifyEveryNReports, samplingPeriod);
    ASSERT_NE(nullptr, streamerHandle);
    lzt::metric_streamer_close(streamerHandle);
    lzt::deactivate_metric_groups(device);
  }
}

#ifdef ZET_EXPORT_METRICS_DATA_EXP_NAME
LZT_TEST_F(
    zetMetricGroupTest,
    GivenValidMetricGroupWhenMetricGroupGetExportDataExpIsCalledThenReturnSuccess) {

  for (auto deviceh : devices) {

    ze_device_properties_t deviceProperties = {
        ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES, nullptr};
    zeDeviceGetProperties(deviceh, &deviceProperties);
    LOG_INFO << "test device name " << deviceProperties.name << " uuid "
             << lzt::to_string(deviceProperties.uuid);
    if (deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE) {
      LOG_INFO << "test subdevice id " << deviceProperties.subdeviceId;
    } else {
      LOG_INFO << "test device is a root device";
    }

    std::vector<zet_metric_group_handle_t> test_metric_groups =
        lzt::get_metric_groups_with_different_domains(deviceh, 1);

    ASSERT_GT(test_metric_groups.size(), 0u);
    for (auto &test_metric_group : test_metric_groups) {

      lzt::activate_metric_groups(deviceh, 1, &test_metric_group);
      uint32_t notifyEveryNReports = 100;
      uint32_t samplingPeriod = 100000;
      zet_metric_streamer_handle_t metric_streamer_handle =
          lzt::metric_streamer_open_for_device(deviceh, test_metric_group,
                                               nullptr, notifyEveryNReports,
                                               samplingPeriod);
      ASSERT_NE(nullptr, metric_streamer_handle);
      auto report_size =
          lzt::metric_streamer_read_data_size(metric_streamer_handle, 1);
      lzt::metric_streamer_close(metric_streamer_handle);

      std::vector<uint8_t> raw_data(report_size);
      size_t export_data_size = 0;
      EXPECT_ZE_RESULT_SUCCESS(zetMetricGroupGetExportDataExp(
          test_metric_group, raw_data.data(), report_size, &export_data_size,
          nullptr));
      EXPECT_GT(export_data_size, 0);
      std::vector<uint8_t> exported_data(export_data_size);
      EXPECT_ZE_RESULT_SUCCESS(zetMetricGroupGetExportDataExp(
          test_metric_group, raw_data.data(), report_size, &export_data_size,
          exported_data.data()));
    }
  }
}
#else
#ifdef __linux__
#warning                                                                       \
    "ZET_EXPORT_METRICS_DATA_EXP_NAME support not found, not building tests for it"
#else
#pragma message(                                                               \
    "warning: ZET_EXPORT_METRICS_DATA_EXP_NAME support not found, not building tests for it")
#endif
#endif // ifdef ZET_EXPORT_METRICS_DATA_EXP_NAME

class zetMetricQueryTest : public zetMetricGroupTest {
protected:
  zet_metric_query_pool_handle_t metric_query_pool_handle{};
  zet_metric_query_handle_t metric_query_handle{};

  std::vector<std::string> groupNameList;
  zet_metric_group_handle_t matchedGroupHandle{};
  std::string groupName;

  void SetUp() override {
    groupNameList = lzt::get_metric_group_name_list(
        device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_EVENT_BASED, false);
    ASSERT_GT(groupNameList.size(), 0u) << "No query metric groups found";
    groupName = groupNameList[0];

    matchedGroupHandle = lzt::find_metric_group(
        device, groupName, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_EVENT_BASED);
    ASSERT_NE(nullptr, matchedGroupHandle);
    metric_query_pool_handle = lzt::create_metric_query_pool(
        1000, ZET_METRIC_QUERY_POOL_TYPE_PERFORMANCE, matchedGroupHandle);
    ASSERT_NE(nullptr, metric_query_pool_handle)
        << "failed to create metric query pool handle";
    metric_query_handle = lzt::metric_query_create(metric_query_pool_handle);
    ASSERT_NE(nullptr, metric_query_handle)
        << "failed to create metric query handle";
  }

  void TearDown() override {
    if (metric_query_handle != nullptr) {
      lzt::destroy_metric_query(metric_query_handle);
    }
    if (metric_query_pool_handle != nullptr) {
      lzt::destroy_metric_query_pool(metric_query_pool_handle);
    }
  }
};

LZT_TEST_F(
    zetMetricQueryTest,
    GivenValidMetricQueryPoolWhenValidMetricGroupIsPassedThenExpectQueryHandle) {
  EXPECT_NE(nullptr, metric_query_handle);
}

LZT_TEST_F(
    zetMetricQueryTest,
    GivenValidMetricQueryHandleWhenResettingQueryHandleThenExpectSuccess) {
  lzt::reset_metric_query(metric_query_handle);
}

LZT_TEST_F(
    zetMetricQueryTest,
    GivenOnlyMetricQueryWhenCommandListIsCreatedThenExpectCommandListToExecuteSuccessfully) {
  zet_command_list_handle_t commandList = lzt::create_command_list();
  lzt::activate_metric_groups(device, 1, &matchedGroupHandle);
  lzt::append_metric_query_begin(commandList, metric_query_handle);
  lzt::append_metric_query_end(commandList, metric_query_handle, nullptr);
  lzt::close_command_list(commandList);
  ze_command_queue_handle_t commandQueue = lzt::create_command_queue();
  lzt::execute_command_lists(commandQueue, 1, &commandList, nullptr);
  lzt::synchronize(commandQueue, UINT64_MAX);
  lzt::deactivate_metric_groups(device);
  lzt::destroy_command_queue(commandQueue);
  lzt::destroy_command_list(commandList);
}

LZT_TEST_F(
    zetMetricQueryTest,
    GivenOnlyMetricQueryWithMetricMemoryBarrierWhenCommandListIsCreatedThenExpectCommandListToExecuteSucessfully) {
  zet_command_list_handle_t commandList = lzt::create_command_list();
  lzt::activate_metric_groups(device, 1, &matchedGroupHandle);
  lzt::append_metric_query_begin(commandList, metric_query_handle);
  lzt::append_metric_memory_barrier(commandList);
  lzt::append_metric_query_end(commandList, metric_query_handle, nullptr);
  lzt::close_command_list(commandList);
  ze_command_queue_handle_t commandQueue = lzt::create_command_queue();
  lzt::execute_command_lists(commandQueue, 1, &commandList, nullptr);
  lzt::synchronize(commandQueue, UINT64_MAX);
  lzt::deactivate_metric_groups(device);
  lzt::destroy_command_queue(commandQueue);
  lzt::destroy_command_list(commandList);
}

class zetMetricQueryLoadTest : public ::testing::Test {
protected:
  std::vector<ze_device_handle_t> devices;

  void SetUp() override { devices = lzt::get_metric_test_device_list(); }
};

using zetMetricQueryLoadTestNoValidate = zetMetricQueryLoadTest;

LZT_TEST_F(
    zetMetricQueryLoadTestNoValidate,
    GivenValidMetricGroupWhenEventBasedQueryNoValidateIsCreatedThenExpectQueryToSucceed) {

  for (auto device : devices) {

    ze_device_properties_t deviceProperties = {
        ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES, nullptr};
    zeDeviceGetProperties(device, &deviceProperties);
    LOG_INFO << "test device name " << deviceProperties.name << " uuid "
             << lzt::to_string(deviceProperties.uuid);
    if (deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE) {
      LOG_INFO << "test subdevice id " << deviceProperties.subdeviceId;
    } else {
      LOG_INFO << "test device is a root device";
    }

    ze_command_queue_handle_t commandQueue = lzt::create_command_queue(device);
    zet_command_list_handle_t commandList = lzt::create_command_list(device);

    auto metricGroupInfo = lzt::get_metric_group_info(
        device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_EVENT_BASED, false, true);
    ASSERT_GT(metricGroupInfo.size(), 0u) << "No query metric groups found";
    metricGroupInfo = lzt::optimize_metric_group_info_list(metricGroupInfo);

    for (auto groupInfo : metricGroupInfo) {

      LOG_INFO << "test metricGroup name " << groupInfo.metricGroupName;
      zet_metric_query_pool_handle_t metric_query_pool_handle =
          lzt::create_metric_query_pool_for_device(
              device, 1000, ZET_METRIC_QUERY_POOL_TYPE_PERFORMANCE,
              groupInfo.metricGroupHandle);
      ASSERT_NE(nullptr, metric_query_pool_handle)
          << "failed to create metric query pool handle";
      zet_metric_query_handle_t metric_query_handle =
          lzt::metric_query_create(metric_query_pool_handle);
      ASSERT_NE(nullptr, metric_query_handle)
          << "failed to create metric query handle";

      lzt::activate_metric_groups(device, 1, &groupInfo.metricGroupHandle);
      lzt::append_metric_query_begin(commandList, metric_query_handle);
      lzt::append_barrier(commandList, nullptr, 0, nullptr);
      ze_event_handle_t eventHandle;
      lzt::zeEventPool eventPool;

      eventPool.create_event(eventHandle, ZE_EVENT_SCOPE_FLAG_HOST,
                             ZE_EVENT_SCOPE_FLAG_HOST);
      void *a_buffer, *b_buffer, *c_buffer;
      ze_group_count_t tg;
      ze_kernel_handle_t function = get_matrix_multiplication_kernel(
          device, &tg, &a_buffer, &b_buffer, &c_buffer);

      zeCommandListAppendLaunchKernel(commandList, function, &tg, nullptr, 0,
                                      nullptr);
      lzt::append_barrier(commandList, nullptr, 0, nullptr);
      lzt::append_metric_query_end(commandList, metric_query_handle,
                                   eventHandle);

      lzt::close_command_list(commandList);
      lzt::execute_command_lists(commandQueue, 1, &commandList, nullptr);

      lzt::synchronize(commandQueue, UINT64_MAX);

      EXPECT_ZE_RESULT_SUCCESS(zeEventQueryStatus(eventHandle));
      std::vector<uint8_t> rawData;

      lzt::metric_query_get_data(metric_query_handle, &rawData);

      eventPool.destroy_event(eventHandle);
      lzt::destroy_metric_query(metric_query_handle);
      lzt::destroy_metric_query_pool(metric_query_pool_handle);

      lzt::deactivate_metric_groups(device);
      lzt::destroy_function(function);
      lzt::free_memory(a_buffer);
      lzt::free_memory(b_buffer);
      lzt::free_memory(c_buffer);

      lzt::reset_command_list(commandList);
    }

    lzt::destroy_command_queue(commandQueue);
    lzt::destroy_command_list(commandList);
  }
}

typedef struct _metrict_query_test_options {
  bool reset;
  bool immediate;
  bool wait_event;
} metric_query_test_options;

void run_test(const ze_device_handle_t &device,
              metric_query_test_options options) {

  auto immediate = options.immediate;
  auto reset = options.reset;
  auto wait_event = options.wait_event;

  ze_device_properties_t deviceProperties = {
      ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES, nullptr};
  zeDeviceGetProperties(device, &deviceProperties);

  LOG_INFO << "test device name " << deviceProperties.name << " uuid "
           << lzt::to_string(deviceProperties.uuid);
  if (deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE) {
    LOG_INFO << "test subdevice id " << deviceProperties.subdeviceId;
  } else {
    LOG_INFO << "test device is a root device";
  }

  ze_command_queue_handle_t commandQueue = nullptr;
  ze_command_list_handle_t commandList = nullptr;
  if (immediate) {
    commandList = lzt::create_immediate_command_list(device);
  } else {
    commandQueue = lzt::create_command_queue(device);
    commandList = lzt::create_command_list(device);
  }
  auto metricGroupInfo = lzt::get_metric_group_info(
      device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_EVENT_BASED, false, true);
  ASSERT_GT(metricGroupInfo.size(), 0u) << "No query metric groups found";
  metricGroupInfo =
      lzt::optimize_metric_group_info_list(metricGroupInfo, reset ? 1 : 20);

  for (auto groupInfo : metricGroupInfo) {
    LOG_INFO << "test metricGroup name " << groupInfo.metricGroupName;

    zet_metric_query_pool_handle_t metric_query_pool_handle =
        lzt::create_metric_query_pool_for_device(
            device, 1000, ZET_METRIC_QUERY_POOL_TYPE_PERFORMANCE,
            groupInfo.metricGroupHandle);
    ASSERT_NE(nullptr, metric_query_pool_handle)
        << "failed to create metric query pool handle";
    zet_metric_query_handle_t metric_query_handle =
        lzt::metric_query_create(metric_query_pool_handle);
    ASSERT_NE(nullptr, metric_query_handle)
        << "failed to create metric query handle";

    lzt::activate_metric_groups(device, 1, &groupInfo.metricGroupHandle);
    lzt::append_metric_query_begin(commandList, metric_query_handle);
    lzt::append_barrier(commandList, nullptr, 0, nullptr);
    ze_event_handle_t eventHandle, wait_event_handle;
    lzt::zeEventPool eventPool;

    eventPool.create_event(eventHandle, ZE_EVENT_SCOPE_FLAG_HOST,
                           ZE_EVENT_SCOPE_FLAG_HOST);
    eventPool.create_event(wait_event_handle, ZE_EVENT_SCOPE_FLAG_HOST,
                           ZE_EVENT_SCOPE_FLAG_HOST);
    void *a_buffer, *b_buffer, *c_buffer;
    ze_group_count_t tg;
    ze_kernel_handle_t function = get_matrix_multiplication_kernel(
        device, &tg, &a_buffer, &b_buffer, &c_buffer);

    zeCommandListAppendLaunchKernel(commandList, function, &tg,
                                    wait_event ? wait_event_handle : nullptr, 0,
                                    nullptr);

    if (wait_event) {
      lzt::append_metric_query_end(commandList, metric_query_handle,
                                   eventHandle, 1, &wait_event_handle);
    } else {
      lzt::append_barrier(commandList);
      lzt::append_metric_query_end(commandList, metric_query_handle,
                                   eventHandle);
    }

    lzt::close_command_list(commandList);
    if (!immediate) {
      lzt::execute_command_lists(commandQueue, 1, &commandList, nullptr);
      lzt::synchronize(commandQueue, UINT64_MAX);
    }

    if (!immediate) {
      EXPECT_ZE_RESULT_SUCCESS(zeEventQueryStatus(eventHandle));
    } else {
      lzt::event_host_synchronize(eventHandle, UINT64_MAX);
    }

    std::vector<uint8_t> rawData;
    lzt::metric_query_get_data(metric_query_handle, &rawData);
    lzt::validate_metrics(groupInfo.metricGroupHandle,
                          lzt::metric_query_get_data_size(metric_query_handle),
                          rawData.data());
    if (reset && !immediate) {
      lzt::reset_metric_query(metric_query_handle);

      lzt::reset_command_list(commandList);
      lzt::append_metric_query_begin(commandList, metric_query_handle);
      lzt::append_barrier(commandList, nullptr, 0, nullptr);

      // reset buffers
      lzt::free_memory(a_buffer);
      lzt::free_memory(b_buffer);
      lzt::free_memory(c_buffer);
      lzt::destroy_function(function);

      function = get_matrix_multiplication_kernel(device, &tg, &a_buffer,
                                                  &b_buffer, &c_buffer);

      zeCommandListAppendLaunchKernel(commandList, function, &tg, nullptr, 0,
                                      nullptr);
      lzt::append_barrier(commandList, nullptr, 0, nullptr);
      lzt::append_metric_query_end(commandList, metric_query_handle,
                                   eventHandle);
      lzt::close_command_list(commandList);
      lzt::execute_command_lists(commandQueue, 1, &commandList, nullptr);

      lzt::synchronize(commandQueue, UINT64_MAX);

      EXPECT_ZE_RESULT_SUCCESS(zeEventQueryStatus(eventHandle));

      lzt::metric_query_get_data(metric_query_handle, &rawData);
      lzt::validate_metrics(
          groupInfo.metricGroupHandle,
          lzt::metric_query_get_data_size(metric_query_handle), rawData.data());
    }

    eventPool.destroy_event(eventHandle);
    lzt::destroy_metric_query(metric_query_handle);
    lzt::destroy_metric_query_pool(metric_query_pool_handle);

    lzt::deactivate_metric_groups(device);
    lzt::destroy_function(function);
    lzt::free_memory(a_buffer);
    lzt::free_memory(b_buffer);
    lzt::free_memory(c_buffer);

    lzt::reset_command_list(commandList);
  }

  if (commandQueue) {
    lzt::destroy_command_queue(commandQueue);
  }
  lzt::destroy_command_list(commandList);
}

LZT_TEST_F(
    zetMetricQueryLoadTest,
    GivenValidMetricGroupWhenEventBasedQueryIsCreatedThenExpectQueryToSucceed) {

  for (auto &device : devices) {
    run_test(device, {false, false, false});
  }
}

LZT_TEST_F(
    zetMetricQueryLoadTest,
    GivenWorkloadExecutedWithMetricQueryWhenResettingQueryHandleThenResetSucceedsAndCanReuseHandle) {
  for (auto &device : devices) {
    run_test(device, {true, false, false});
  }
}

LZT_TEST_F(
    zetMetricQueryLoadTest,
    GivenWorkloadExecutedOnImmediateCommandListWhenQueryingThenQuerySucceeds) {

  for (auto &device : devices) {
    run_test(device, {false, true, false});
  }
}

LZT_TEST_F(
    zetMetricQueryLoadTest,
    GivenWorkloadExecutedWithWaitEventWhenMakingMetricQueryThenQuerySucceeds) {

  for (auto &device : devices) {
    run_test(device, {false, false, true});
  }
}

LZT_TEST_F(
    zetMetricQueryLoadTest,
    GivenWorkloadExecutedWithWaitEventOnImmediateCommandListWhenMakingMetricQueryThenQuerySucceeds) {

  for (auto &device : devices) {
    run_test(device, {false, true, true});
  }
}

void run_multi_device_query_load_test(
    const std::vector<zet_device_handle_t> &devices) {

  LOG_INFO << "Testing multi device query load";

  if (devices.size() < 2) {
    GTEST_SKIP() << "Skipping the test as less than 2 devices are available";
  }

  auto device_0 = devices[0];
  auto device_1 = devices[1];
  auto command_queue_0 = lzt::create_command_queue(device_0);
  auto command_queue_1 = lzt::create_command_queue(device_1);

  auto command_list_0 = lzt::create_command_list(device_0);
  auto command_list_1 = lzt::create_command_list(device_1);

  auto metric_group_info_0 = lzt::get_metric_group_info(
      device_0, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_EVENT_BASED, true, true);
  ASSERT_GT(metric_group_info_0.size(), 0u)
      << "No query metric groups found on device 0";
  auto metric_group_info_1 = lzt::get_metric_group_info(
      device_1, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_EVENT_BASED, true, true);
  ASSERT_GT(metric_group_info_1.size(), 0u)
      << "No query metric groups found on device 1";

  metric_group_info_0 =
      lzt::optimize_metric_group_info_list(metric_group_info_0);
  metric_group_info_1 =
      lzt::optimize_metric_group_info_list(metric_group_info_1);

  auto groupInfo_0 = metric_group_info_0[0];
  auto groupInfo_1 = metric_group_info_1[0];

  auto metric_query_pool_handle_0 = lzt::create_metric_query_pool_for_device(
      device_0, 1000, ZET_METRIC_QUERY_POOL_TYPE_PERFORMANCE,
      groupInfo_0.metricGroupHandle);
  ASSERT_NE(nullptr, metric_query_pool_handle_0)
      << "failed to create metric query pool handle";
  auto metric_query_pool_handle_1 = lzt::create_metric_query_pool_for_device(
      device_1, 1000, ZET_METRIC_QUERY_POOL_TYPE_PERFORMANCE,
      groupInfo_1.metricGroupHandle);
  ASSERT_NE(nullptr, metric_query_pool_handle_1)
      << "failed to create metric query pool handle";
  auto metric_query_handle_0 =
      lzt::metric_query_create(metric_query_pool_handle_0);
  ASSERT_NE(nullptr, metric_query_handle_0)
      << "failed to create metric query handle 0";
  auto metric_query_handle_1 =
      lzt::metric_query_create(metric_query_pool_handle_1);
  ASSERT_NE(nullptr, metric_query_handle_1)
      << "failed to create metric query handle 1";

  lzt::activate_metric_groups(device_0, 1, &groupInfo_0.metricGroupHandle);
  lzt::activate_metric_groups(device_1, 1, &groupInfo_1.metricGroupHandle);

  lzt::append_metric_query_begin(command_list_0, metric_query_handle_0);
  lzt::append_metric_query_begin(command_list_1, metric_query_handle_1);

  lzt::append_barrier(command_list_0, nullptr, 0, nullptr);
  lzt::append_barrier(command_list_1, nullptr, 0, nullptr);

  ze_event_handle_t event_handle_0, event_handle_1;
  lzt::zeEventPool event_pool;

  event_pool.create_event(event_handle_0, ZE_EVENT_SCOPE_FLAG_HOST,
                          ZE_EVENT_SCOPE_FLAG_HOST);
  event_pool.create_event(event_handle_1, ZE_EVENT_SCOPE_FLAG_HOST,
                          ZE_EVENT_SCOPE_FLAG_HOST);

  void *a_buffer_0, *b_buffer_0, *c_buffer_0;
  ze_group_count_t tg_0;

  void *a_buffer_1, *b_buffer_1, *c_buffer_1;
  ze_group_count_t tg_1;

  auto function_0 = get_matrix_multiplication_kernel(
      device_0, &tg_0, &a_buffer_0, &b_buffer_0, &c_buffer_0);

  auto function_1 = get_matrix_multiplication_kernel(
      device_1, &tg_1, &a_buffer_1, &b_buffer_1, &c_buffer_1);

  lzt::append_launch_function(command_list_0, function_0, &tg_0, event_handle_0,
                              0, nullptr);
  lzt::append_barrier(command_list_0);
  lzt::append_metric_query_end(command_list_0, metric_query_handle_0,
                               event_handle_0);

  lzt::append_launch_function(command_list_1, function_1, &tg_1, event_handle_1,
                              0, nullptr);
  lzt::append_barrier(command_list_1);
  lzt::append_metric_query_end(command_list_1, metric_query_handle_1,
                               event_handle_1);

  lzt::close_command_list(command_list_0);
  lzt::close_command_list(command_list_1);

  lzt::execute_command_lists(command_queue_0, 1, &command_list_0, nullptr);
  lzt::execute_command_lists(command_queue_1, 1, &command_list_1, nullptr);

  lzt::synchronize(command_queue_0, UINT64_MAX);
  lzt::synchronize(command_queue_1, UINT64_MAX);

  EXPECT_ZE_RESULT_SUCCESS(zeEventQueryStatus(event_handle_0));
  EXPECT_ZE_RESULT_SUCCESS(zeEventQueryStatus(event_handle_1));

  std::vector<uint8_t> raw_data_0, raw_data_1;

  lzt::metric_query_get_data(metric_query_handle_0, &raw_data_0);
  lzt::metric_query_get_data(metric_query_handle_1, &raw_data_1);

  event_pool.destroy_event(event_handle_0);
  event_pool.destroy_event(event_handle_1);

  lzt::validate_metrics(groupInfo_0.metricGroupHandle,
                        lzt::metric_query_get_data_size(metric_query_handle_0),
                        raw_data_0.data());
  lzt::validate_metrics(groupInfo_1.metricGroupHandle,
                        lzt::metric_query_get_data_size(metric_query_handle_1),
                        raw_data_1.data());

  lzt::destroy_metric_query(metric_query_handle_0);
  lzt::destroy_metric_query(metric_query_handle_1);
  lzt::destroy_metric_query_pool(metric_query_pool_handle_0);
  lzt::destroy_metric_query_pool(metric_query_pool_handle_1);

  lzt::deactivate_metric_groups(device_0);
  lzt::deactivate_metric_groups(device_1);

  lzt::destroy_function(function_0);
  lzt::destroy_function(function_1);

  lzt::free_memory(a_buffer_0);
  lzt::free_memory(a_buffer_1);
  lzt::free_memory(b_buffer_0);
  lzt::free_memory(b_buffer_1);
  lzt::free_memory(c_buffer_0);
  lzt::free_memory(c_buffer_1);

  lzt::destroy_command_list(command_list_0);
  lzt::destroy_command_list(command_list_1);
  lzt::destroy_command_queue(command_queue_0);
  lzt::destroy_command_queue(command_queue_1);
}

LZT_TEST_F(
    zetMetricQueryLoadTest,
    GivenValidMetricGroupsWhenMultipleDevicesQueryThenExpectQueryToSucceed) {

  auto driver = lzt::get_default_driver();
  auto devices = lzt::get_devices(driver);

  run_multi_device_query_load_test(devices);
}

LZT_TEST_F(
    zetMetricQueryLoadTest,
    GivenValidMetricGroupsWhenMultipleSubDevicesQueryThenExpectQueryToSucceed) {
  auto subdevices = lzt::get_all_sub_devices();

  run_multi_device_query_load_test(subdevices);
}

class zetMetricQueryLoadStdTest : public ::testing::Test {
protected:
  std::vector<ze_device_handle_t> devices;

  void SetUp() override { devices = lzt::get_metric_test_no_subdevices_list(); }
};

LZT_TEST_F(
    zetMetricQueryLoadStdTest,
    GivenValidMetricGroupWhenEventBasedQueryWithNoSubDevicesListIsCreatedThenExpectQueryAndSpecValidateToSucceed) {

  for (auto device : devices) {

    uint32_t subDeviceCount = 0;
    EXPECT_ZE_RESULT_SUCCESS(
        zeDeviceGetSubDevices(device, &subDeviceCount, nullptr));
    if (subDeviceCount != 0) {
      continue;
    }

    ze_device_properties_t deviceProperties = {
        ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES, nullptr};
    zeDeviceGetProperties(device, &deviceProperties);

    LOG_INFO << "test device name " << deviceProperties.name << " uuid "
             << lzt::to_string(deviceProperties.uuid);
    if (deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE) {
      LOG_INFO << "test subdevice id " << deviceProperties.subdeviceId;
    } else {
      LOG_INFO << "test device is a root device";
    }

    ze_command_queue_handle_t commandQueue = lzt::create_command_queue(device);
    zet_command_list_handle_t commandList = lzt::create_command_list(device);

    auto metricGroupInfo = lzt::get_metric_group_info(
        device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_EVENT_BASED, false, true);
    ASSERT_GT(metricGroupInfo.size(), 0u) << "No query metric groups found";
    metricGroupInfo = lzt::optimize_metric_group_info_list(metricGroupInfo, 1);

    for (auto groupInfo : metricGroupInfo) {

      LOG_INFO << "test metricGroup name " << groupInfo.metricGroupName;

      zet_metric_query_pool_handle_t metric_query_pool_handle =
          lzt::create_metric_query_pool_for_device(
              device, 1000, ZET_METRIC_QUERY_POOL_TYPE_PERFORMANCE,
              groupInfo.metricGroupHandle);
      ASSERT_NE(nullptr, metric_query_pool_handle)
          << "failed to create metric query pool handle";
      zet_metric_query_handle_t metric_query_handle =
          lzt::metric_query_create(metric_query_pool_handle);
      ASSERT_NE(nullptr, metric_query_handle)
          << "failed to create metric query handle";

      lzt::activate_metric_groups(device, 1, &groupInfo.metricGroupHandle);
      lzt::append_metric_query_begin(commandList, metric_query_handle);
      lzt::append_barrier(commandList, nullptr, 0, nullptr);
      ze_event_handle_t eventHandle;
      lzt::zeEventPool eventPool;

      eventPool.create_event(eventHandle, ZE_EVENT_SCOPE_FLAG_HOST,
                             ZE_EVENT_SCOPE_FLAG_HOST);
      void *a_buffer, *b_buffer, *c_buffer;
      ze_group_count_t tg;
      ze_kernel_handle_t function = get_matrix_multiplication_kernel(
          device, &tg, &a_buffer, &b_buffer, &c_buffer);

      zeCommandListAppendLaunchKernel(commandList, function, &tg, nullptr, 0,
                                      nullptr);
      lzt::append_barrier(commandList, nullptr, 0, nullptr);
      lzt::append_metric_query_end(commandList, metric_query_handle,
                                   eventHandle);

      lzt::close_command_list(commandList);
      lzt::execute_command_lists(commandQueue, 1, &commandList, nullptr);

      lzt::synchronize(commandQueue, UINT64_MAX);

      EXPECT_ZE_RESULT_SUCCESS(zeEventQueryStatus(eventHandle));

      std::vector<uint8_t> rawData;
      lzt::metric_query_get_data(metric_query_handle, &rawData);
      lzt::validate_metrics_std(
          groupInfo.metricGroupHandle,
          lzt::metric_query_get_data_size(metric_query_handle), rawData.data());

      eventPool.destroy_event(eventHandle);
      lzt::destroy_metric_query(metric_query_handle);
      lzt::destroy_metric_query_pool(metric_query_pool_handle);

      lzt::deactivate_metric_groups(device);
      lzt::destroy_function(function);
      lzt::free_memory(a_buffer);
      lzt::free_memory(b_buffer);
      lzt::free_memory(c_buffer);

      lzt::reset_command_list(commandList);
    }
    lzt::destroy_command_queue(commandQueue);
    lzt::destroy_command_list(commandList);
  }
}

class zetMetricStreamerTest : public ::testing::Test {
protected:
  std::vector<ze_device_handle_t> devices;

  uint32_t notifyEveryNReports = 3000;
  uint32_t samplingPeriod = 1000000;
  uint64_t TimeForNReportsComplete = notifyEveryNReports * samplingPeriod;
  ze_device_handle_t device;

  void SetUp() { devices = lzt::get_metric_test_device_list(); }
};

using zetMetricStreamerTestNoValidate = zetMetricStreamerTest;

LZT_TEST_F(
    zetMetricStreamerTestNoValidate,
    GivenValidMetricGroupWhenTimerBasedStreamerNoValidateIsCreatedThenExpectStreamerToSucceed) {

  for (auto device : devices) {

    ze_device_properties_t deviceProperties = {
        ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES, nullptr};
    zeDeviceGetProperties(device, &deviceProperties);

    LOG_INFO << "test device name " << deviceProperties.name << " uuid "
             << lzt::to_string(deviceProperties.uuid);
    if (deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE) {
      LOG_INFO << "test subdevice id " << deviceProperties.subdeviceId;
    } else {
      LOG_INFO << "test device is a root device";
    }

    ze_command_queue_handle_t commandQueue = lzt::create_command_queue(device);
    zet_command_list_handle_t commandList = lzt::create_command_list(device);

    auto metricGroupInfo = lzt::get_metric_group_info(
        device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED, true, true);
    ASSERT_GT(metricGroupInfo.size(), 0u) << "No metric groups found";
    metricGroupInfo = lzt::optimize_metric_group_info_list(metricGroupInfo);

    for (auto groupInfo : metricGroupInfo) {

      LOG_INFO << "test metricGroup name " << groupInfo.metricGroupName;
      lzt::activate_metric_groups(device, 1, &groupInfo.metricGroupHandle);

      ze_event_handle_t eventHandle;
      lzt::zeEventPool eventPool;
      eventPool.create_event(eventHandle, ZE_EVENT_SCOPE_FLAG_HOST,
                             ZE_EVENT_SCOPE_FLAG_HOST);

      void *a_buffer, *b_buffer, *c_buffer;
      ze_group_count_t tg;
      ze_kernel_handle_t function = get_matrix_multiplication_kernel(
          device, &tg, &a_buffer, &b_buffer, &c_buffer);
      zeCommandListAppendLaunchKernel(commandList, function, &tg, nullptr, 0,
                                      nullptr);
      lzt::close_command_list(commandList);

      zet_metric_streamer_handle_t metricStreamerHandle =
          lzt::metric_streamer_open_for_device(
              device, groupInfo.metricGroupHandle, eventHandle,
              notifyEveryNReports, samplingPeriod);
      ASSERT_NE(nullptr, metricStreamerHandle);

      lzt::execute_command_lists(commandQueue, 1, &commandList, nullptr);
      lzt::synchronize(commandQueue, std::numeric_limits<uint64_t>::max());

      std::vector<uint8_t> rawData;
      lzt::metric_streamer_read_data(metricStreamerHandle, &rawData);
      lzt::metric_streamer_close(metricStreamerHandle);
      lzt::deactivate_metric_groups(device);
      lzt::destroy_function(function);
      lzt::free_memory(a_buffer);
      lzt::free_memory(b_buffer);
      lzt::free_memory(c_buffer);
      eventPool.destroy_event(eventHandle);
      lzt::reset_command_list(commandList);
    }
    lzt::destroy_command_queue(commandQueue);
    lzt::destroy_command_list(commandList);
  }
}

using zetMetricStreamerTestNReports = zetMetricStreamerTest;

LZT_TEST_F(
    zetMetricStreamerTestNReports,
    GivenValidMetricGroupWithTimerBasedStreamerThenEventHostSynchronizeWithNotifyOnNreportsEventSignalsDataPresent) {

  notifyEveryNReports = 50;

  for (auto device : devices) {

    ze_device_properties_t deviceProperties = {
        ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES, nullptr};
    zeDeviceGetProperties(device, &deviceProperties);

    LOG_INFO << "test device name " << deviceProperties.name << " uuid "
             << lzt::to_string(deviceProperties.uuid);
    if (deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE) {
      LOG_INFO << "test subdevice id " << deviceProperties.subdeviceId;
    } else {
      LOG_INFO << "test device is a root device";
    }

    ze_command_queue_handle_t commandQueue = lzt::create_command_queue(device);
    zet_command_list_handle_t commandList = lzt::create_command_list(device);

    auto metricGroupInfo = lzt::get_metric_group_info(
        device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED, true, true);
    ASSERT_GT(metricGroupInfo.size(), 0u) << "No metric groups found";
    metricGroupInfo = lzt::optimize_metric_group_info_list(metricGroupInfo);

    for (auto groupInfo : metricGroupInfo) {

      LOG_INFO << "test metricGroup name " << groupInfo.metricGroupName;
      lzt::activate_metric_groups(device, 1, &groupInfo.metricGroupHandle);

      ze_event_handle_t eventHandle;
      lzt::zeEventPool eventPool;
      eventPool.create_event(eventHandle, ZE_EVENT_SCOPE_FLAG_HOST,
                             ZE_EVENT_SCOPE_FLAG_HOST);

      void *a_buffer, *b_buffer, *c_buffer;
      ze_group_count_t tg;

      ze_kernel_handle_t function = get_matrix_multiplication_kernel(
          device, &tg, &a_buffer, &b_buffer, &c_buffer);
      zeCommandListAppendLaunchKernel(commandList, function, &tg, nullptr, 0,
                                      nullptr);
      lzt::close_command_list(commandList);

      zet_metric_streamer_handle_t metricStreamerHandle =
          lzt::metric_streamer_open_for_device(
              device, groupInfo.metricGroupHandle, eventHandle,
              notifyEveryNReports, samplingPeriod);
      ASSERT_NE(nullptr, metricStreamerHandle);

      lzt::execute_command_lists(commandQueue, 1, &commandList, nullptr);
      lzt::event_host_synchronize(eventHandle, UINT64_MAX);
      size_t rawDataSize = 0;
      std::vector<uint8_t> rawData;
      rawDataSize = lzt::metric_streamer_read_data_size(metricStreamerHandle,
                                                        notifyEveryNReports);
      EXPECT_GT(rawDataSize, 0);
      rawData.resize(rawDataSize);
      lzt::metric_streamer_read_data(metricStreamerHandle, notifyEveryNReports,
                                     rawDataSize, &rawData);

      LOG_INFO << "raw data size " << rawDataSize;
      EXPECT_GT(rawDataSize, 0);

      lzt::synchronize(commandQueue, std::numeric_limits<uint64_t>::max());

      lzt::validate_metrics(groupInfo.metricGroupHandle, rawDataSize,
                            rawData.data());

      lzt::metric_streamer_close(metricStreamerHandle);
      lzt::deactivate_metric_groups(device);
      lzt::destroy_function(function);
      lzt::free_memory(a_buffer);
      lzt::free_memory(b_buffer);
      lzt::free_memory(c_buffer);
      eventPool.destroy_event(eventHandle);
      lzt::reset_command_list(commandList);
    }
    lzt::destroy_command_queue(commandQueue);
    lzt::destroy_command_list(commandList);
  }
}

LZT_TEST_F(
    zetMetricStreamerTest,
    GivenValidMetricGroupWhenTimerBasedStreamerIsCreatedThenExpectStreamerToSucceed) {

  /* This test tries to validate the readData feature of streamers.
   * numberOfReportsReq is the minimum number of reports needed by this test
   * after streamerOpen(). timeBeforeReadInNanoSec is the minimum time interval
   * between streamerOpen() and readData() . using above definitions sampling
   * period needed becomes samplingPeriod = timeBeforeReadInNanoSec /
   * numberOfReportsReq
   */
  constexpr uint32_t maxReadAttempts = 20;
  constexpr uint32_t numberOfReportsReq = 100;
  constexpr uint32_t timeBeforeReadInNanoSec = 500000000;
  uint32_t samplingPeriod = timeBeforeReadInNanoSec / numberOfReportsReq;
  uint32_t notifyEveryNReports = 9000;

  for (auto device : devices) {
    ze_device_properties_t deviceProperties = {
        ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES, nullptr};
    zeDeviceGetProperties(device, &deviceProperties);

    LOG_INFO << "test device name " << deviceProperties.name << " uuid "
             << lzt::to_string(deviceProperties.uuid);
    if (deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE) {
      LOG_INFO << "test subdevice id " << deviceProperties.subdeviceId;
    } else {
      LOG_INFO << "test device is a root device";
    }

    ze_command_queue_handle_t commandQueue = lzt::create_command_queue(device);
    zet_command_list_handle_t commandList = lzt::create_command_list(device);

    auto metricGroupInfo = lzt::get_metric_group_info(
        device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED, true, true);
    ASSERT_GT(metricGroupInfo.size(), 0u) << "No metric groups found";
    metricGroupInfo = lzt::optimize_metric_group_info_list(metricGroupInfo);

    for (auto groupInfo : metricGroupInfo) {
      LOG_INFO << "test metricGroup name " << groupInfo.metricGroupName;
      lzt::activate_metric_groups(device, 1, &groupInfo.metricGroupHandle);

      void *a_buffer, *b_buffer, *c_buffer;
      ze_group_count_t tg;
      ze_kernel_handle_t function = get_matrix_multiplication_kernel(
          device, &tg, &a_buffer, &b_buffer, &c_buffer, 8192);
      zeCommandListAppendLaunchKernel(commandList, function, &tg, nullptr, 0,
                                      nullptr);
      lzt::close_command_list(commandList);

      // Spawn a thread which continuously runs a workload
      workloadThreadFlag = true;
      std::thread thread(workloadThread, commandQueue, 1, &commandList,
                         nullptr);

      zet_metric_streamer_handle_t metricStreamerHandle =
          lzt::metric_streamer_open_for_device(
              device, groupInfo.metricGroupHandle, nullptr, notifyEveryNReports,
              samplingPeriod);

      // Sleep for timeBeforeReadInNanoSec to ensure required reports are
      // generated
      std::this_thread::sleep_for(
          std::chrono::nanoseconds(timeBeforeReadInNanoSec));
      ASSERT_NE(nullptr, metricStreamerHandle);

      size_t rawDataSize = 0;
      std::vector<uint8_t> rawData;
      rawDataSize = lzt::metric_streamer_read_data_size(metricStreamerHandle,
                                                        notifyEveryNReports);
      EXPECT_GT(rawDataSize, 0);
      rawData.resize(rawDataSize);
      for (uint32_t count = 0; count < maxReadAttempts; count++) {
        lzt::metric_streamer_read_data(
            metricStreamerHandle, notifyEveryNReports, rawDataSize, &rawData);
        if (rawDataSize > 0) {
          break;
        } else {
          std::this_thread::sleep_for(std::chrono::nanoseconds(samplingPeriod));
        }
      }

      LOG_INFO << "rawDataSize " << rawDataSize;
      // Stop the worker thread running the workload
      workloadThreadFlag = false;
      thread.join();

      lzt::validate_metrics(groupInfo.metricGroupHandle, rawDataSize,
                            rawData.data());
      lzt::metric_streamer_close(metricStreamerHandle);
      lzt::deactivate_metric_groups(device);
      lzt::destroy_function(function);
      lzt::free_memory(a_buffer);
      lzt::free_memory(b_buffer);
      lzt::free_memory(c_buffer);
      lzt::reset_command_list(commandList);
    }
    lzt::destroy_command_queue(commandQueue);
    lzt::destroy_command_list(commandList);
  }
}

LZT_TEST_F(
    zetMetricStreamerTest,
    GivenValidMetricGroupWhenTimerBasedStreamerIsCreatedThenExpectStreamerToNotifyEventAtProperTimeAndSucceed) {
  /* This test computes the expected time before which events are generated by
   * multiplying notifyEveryNReports and samplingPeriod. It then loops inside
   * the do-while loop for the expected time and checks for event status to be
   * ZE_RESULT_NOT_READY. Once the expected time has elapsed it will come out of
   * the loop and expect the event to be generated.
   */

  uint32_t notifyEveryNReports = 4500;
  for (auto device : devices) {

    ze_device_properties_t deviceProperties = {
        ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES, nullptr};
    zeDeviceGetProperties(device, &deviceProperties);

    LOG_INFO << "test device name " << deviceProperties.name << " uuid "
             << lzt::to_string(deviceProperties.uuid);
    if (deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE) {
      LOG_INFO << "test subdevice id " << deviceProperties.subdeviceId;
    } else {
      LOG_INFO << "test device is a root device";
    }

    ze_command_queue_handle_t commandQueue = lzt::create_command_queue(device);
    zet_command_list_handle_t commandList = lzt::create_command_list(device);

    auto metricGroupInfo = lzt::get_metric_group_info(
        device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED, true, true);
    ASSERT_GT(metricGroupInfo.size(), 0u) << "No metric groups found";
    metricGroupInfo = lzt::optimize_metric_group_info_list(metricGroupInfo);

    void *a_buffer, *b_buffer, *c_buffer;
    ze_group_count_t tg;
    ze_kernel_handle_t function = get_matrix_multiplication_kernel(
        device, &tg, &a_buffer, &b_buffer, &c_buffer, 8192);
    zeCommandListAppendLaunchKernel(commandList, function, &tg, nullptr, 0,
                                    nullptr);
    lzt::close_command_list(commandList);

    for (auto groupInfo : metricGroupInfo) {
      LOG_INFO << "test metricGroup name " << groupInfo.metricGroupName;
      lzt::activate_metric_groups(device, 1, &groupInfo.metricGroupHandle);

      ze_event_handle_t eventHandle;
      lzt::zeEventPool eventPool;
      eventPool.create_event(eventHandle, ZE_EVENT_SCOPE_FLAG_HOST,
                             ZE_EVENT_SCOPE_FLAG_HOST);

      auto startTime = std::chrono::system_clock::now();
      zet_metric_streamer_handle_t metricStreamerHandle =
          lzt::metric_streamer_open_for_device(
              device, groupInfo.metricGroupHandle, eventHandle,
              notifyEveryNReports, samplingPeriod);
      ASSERT_NE(nullptr, metricStreamerHandle);

      // Spawn a thread which continuously runs a workload till the event is
      // generated in the main thread.
      workloadThreadFlag = true;
      std::thread thread(workloadThread, commandQueue, 1, &commandList,
                         nullptr);

      double minimumTimeBeforeEventIsExpected =
          notifyEveryNReports *
          (static_cast<double>(samplingPeriod) / nanoSecToSeconds);
      // Initializing the error buffer to prevent corner cases
      double errorBuffer = 0.05 * minimumTimeBeforeEventIsExpected;
      LOG_DEBUG << "minimumTimeBeforeEventIsExpected "
                << minimumTimeBeforeEventIsExpected;
      LOG_DEBUG << "errorBuffer " << errorBuffer;

      ze_result_t eventResult;
      auto currentTime = std::chrono::system_clock::now();
      std::chrono::duration<double> elapsedSeconds = currentTime - startTime;

      while (elapsedSeconds.count() <
             (minimumTimeBeforeEventIsExpected - errorBuffer)) {
        eventResult = zeEventQueryStatus(eventHandle);
        EXPECT_EQ(eventResult, ZE_RESULT_NOT_READY);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        currentTime = std::chrono::system_clock::now();
        elapsedSeconds = currentTime - startTime;
      }

      // Sleep again for the error buffer time to ensure corner cases are
      // avoided.
      uint32_t sleep = std::ceil(2 * errorBuffer);
      LOG_DEBUG << "additional sleep before expecting event to be ready "
                << sleep;
      std::this_thread::sleep_for(std::chrono::seconds(sleep));

      eventResult = zeEventQueryStatus(eventHandle);
      EXPECT_ZE_RESULT_SUCCESS(eventResult);

      // signal the worker thread to stop running the workload.
      workloadThreadFlag = false;
      thread.join();
      lzt::metric_streamer_close(metricStreamerHandle);
      lzt::deactivate_metric_groups(device);
      eventPool.destroy_event(eventHandle);
    }
    lzt::destroy_function(function);
    lzt::free_memory(a_buffer);
    lzt::free_memory(b_buffer);
    lzt::free_memory(c_buffer);
    lzt::reset_command_list(commandList);
    lzt::destroy_command_queue(commandQueue);
    lzt::destroy_command_list(commandList);
  }
}

LZT_TEST_F(
    zetMetricStreamerTest,
    GivenValidMetricGroupWhenTimerBasedStreamerIsCreatedThenExpectStreamerToGenrateCorrectNumberOfReports) {
  /* This test computes the expected time before which events are generated by
   * multiplying notifyEveryNReports and samplingPeriod. It then loops inside
   * the do-while loop for the expected time and checks for event status to be
   * ZE_RESULT_NOT_READY. Once the expected time has elapsed it will come out of
   * the loop and expect the event to be generated and checks if correct number
   * of reports have been generated.
   */
  uint32_t notifyEveryNReports = 4500;
  for (auto device : devices) {

    ze_device_properties_t deviceProperties = {
        ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES, nullptr};
    zeDeviceGetProperties(device, &deviceProperties);

    LOG_INFO << "test device name " << deviceProperties.name << " uuid "
             << lzt::to_string(deviceProperties.uuid);
    if (deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE) {
      LOG_INFO << "test subdevice id " << deviceProperties.subdeviceId;
    } else {
      LOG_INFO << "test device is a root device";
    }

    auto metricGroupInfo = lzt::get_metric_group_info(
        device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED, true, true);
    ASSERT_GT(metricGroupInfo.size(), 0u) << "No metric groups found";
    metricGroupInfo = lzt::optimize_metric_group_info_list(metricGroupInfo);

    void *a_buffer, *b_buffer, *c_buffer;
    ze_group_count_t tg;
    ze_command_queue_handle_t commandQueue = lzt::create_command_queue(device);
    zet_command_list_handle_t commandList = lzt::create_command_list(device);

    ze_kernel_handle_t function = get_matrix_multiplication_kernel(
        device, &tg, &a_buffer, &b_buffer, &c_buffer, 8192);
    zeCommandListAppendLaunchKernel(commandList, function, &tg, nullptr, 0,
                                    nullptr);
    lzt::close_command_list(commandList);

    for (auto groupInfo : metricGroupInfo) {
      LOG_INFO << "test metricGroup name " << groupInfo.metricGroupName;
      lzt::activate_metric_groups(device, 1, &groupInfo.metricGroupHandle);

      ze_event_handle_t eventHandle;
      lzt::zeEventPool eventPool;
      eventPool.create_event(eventHandle, ZE_EVENT_SCOPE_FLAG_HOST,
                             ZE_EVENT_SCOPE_FLAG_HOST);

      auto startTime = std::chrono::system_clock::now();
      zet_metric_streamer_handle_t metricStreamerHandle =
          lzt::metric_streamer_open_for_device(
              device, groupInfo.metricGroupHandle, eventHandle,
              notifyEveryNReports, samplingPeriod);
      ASSERT_NE(nullptr, metricStreamerHandle);

      // Spawn a thread which continuously runs a workload till the event is
      // generated in the main thread.
      workloadThreadFlag = true;
      std::thread thread(workloadThread, commandQueue, 1, &commandList,
                         nullptr);

      double minimumTimeBeforeEventIsExpected =
          notifyEveryNReports *
          (static_cast<double>(samplingPeriod) / nanoSecToSeconds);
      // Initializing the error buffer to prevent corner cases
      double errorBuffer = 0.05 * minimumTimeBeforeEventIsExpected;
      LOG_DEBUG << "minimumTimeBeforeEventIsExpected "
                << minimumTimeBeforeEventIsExpected;
      LOG_DEBUG << "errorBuffer " << errorBuffer;

      // Sleep until event is generated.
      auto currentTime = std::chrono::system_clock::now();
      std::chrono::duration<double> elapsedSeconds = currentTime - startTime;
      int32_t timeLeft = std::ceil(minimumTimeBeforeEventIsExpected +
                                   errorBuffer - elapsedSeconds.count());
      if (timeLeft > 0) {
        LOG_DEBUG << "additional sleep before expecting event to be ready "
                  << timeLeft;
        std::this_thread::sleep_for(std::chrono::seconds(timeLeft));
      }

      EXPECT_ZE_RESULT_SUCCESS(zeEventQueryStatus(eventHandle));

      // signal the worker thread to stop running the workload.
      workloadThreadFlag = false;
      thread.join();

      size_t oneReportSize, allReportsSize;
      oneReportSize =
          lzt::metric_streamer_read_data_size(metricStreamerHandle, 1);
      allReportsSize =
          lzt::metric_streamer_read_data_size(metricStreamerHandle, UINT32_MAX);
      LOG_DEBUG << "Event triggered. Single report size: " << oneReportSize
                << ". All reports size:" << allReportsSize;

      EXPECT_GE(allReportsSize / oneReportSize, notifyEveryNReports);

      lzt::metric_streamer_close(metricStreamerHandle);
      lzt::deactivate_metric_groups(device);
      eventPool.destroy_event(eventHandle);
    }
    lzt::destroy_function(function);
    lzt::free_memory(a_buffer);
    lzt::free_memory(b_buffer);
    lzt::free_memory(c_buffer);
    lzt::reset_command_list(commandList);
    lzt::destroy_command_queue(commandQueue);
    lzt::destroy_command_list(commandList);
  }
}

void metric_validate_stall_sampling_data(
    std::vector<zet_metric_properties_t> &metricProperties,
    std::vector<zet_typed_value_t> &totalMetricValues,
    std::vector<uint32_t> &metricValueSets) {

  uint32_t activeOffset = UINT32_MAX;
  uint32_t controlStallOffset = UINT32_MAX;
  uint32_t pipeStallOffset = UINT32_MAX;
  uint32_t sendStallOffset = UINT32_MAX;
  uint32_t distStallOffset = UINT32_MAX;
  uint32_t sbidStallOffset = UINT32_MAX;
  uint32_t syncStallOffset = UINT32_MAX;
  uint32_t instrFetchStallOffset = UINT32_MAX;
  uint32_t otherStallOffset = UINT32_MAX;

  for (size_t i = 0; i < metricProperties.size(); i++) {

    if (strcmp("Active", metricProperties[i].name) == 0) {
      activeOffset = i;
      continue;
    }
    if (strcmp("ControlStall", metricProperties[i].name) == 0) {
      controlStallOffset = i;
      continue;
    }
    if (strcmp("PipeStall", metricProperties[i].name) == 0) {
      pipeStallOffset = i;
      continue;
    }
    if (strcmp("SendStall", metricProperties[i].name) == 0) {
      sendStallOffset = i;
      continue;
    }
    if (strcmp("DistStall", metricProperties[i].name) == 0) {
      distStallOffset = i;
      continue;
    }
    if (strcmp("SbidStall", metricProperties[i].name) == 0) {
      sbidStallOffset = i;
      continue;
    }
    if (strcmp("SyncStall", metricProperties[i].name) == 0) {
      syncStallOffset = i;
      continue;
    }
    if (strcmp("InstrFetchStall", metricProperties[i].name) == 0) {
      instrFetchStallOffset = i;
      continue;
    }
    if (strcmp("OtherStall", metricProperties[i].name) == 0) {
      otherStallOffset = i;
      continue;
    }
  }

  uint32_t ActiveCount = 0;
  uint32_t ControlStallCount = 0;
  uint32_t PipeStallCount = 0;
  uint32_t SendStallCount = 0;
  uint32_t DistStallCount = 0;
  uint32_t SbidStallCount = 0;
  uint32_t SyncStallCount = 0;
  uint32_t InstrFetchStallCount = 0;
  uint32_t OtherStallCount = 0;

  uint32_t metricSetStartIndex = 0;

  EXPECT_GT(metricValueSets.size(), 0u);
  for (uint32_t metricValueSetIndex = 0;
       metricValueSetIndex < metricValueSets.size(); metricValueSetIndex++) {

    const uint32_t metricCountForDataIndex =
        metricValueSets[metricValueSetIndex];
    const uint32_t reportCount =
        metricCountForDataIndex / metricProperties.size();

    LOG_INFO << "for metricValueSetIndex " << metricValueSetIndex
             << " metricCountForDataIndex " << metricCountForDataIndex
             << " reportCount " << reportCount;

    EXPECT_GT(reportCount, 1);

    uint32_t tmpStallCount;
    bool reportCompleteFlag;

    for (uint32_t report = 0; report < reportCount; report++) {

      tmpStallCount = 0;
      reportCompleteFlag = false;

      auto getStallCount = [&totalMetricValues](uint32_t metricReport,
                                                uint32_t metricPropertiesSize,
                                                uint32_t metricOffset,
                                                uint32_t metricStartIndex) {
        uint32_t metricIndex =
            metricReport * metricPropertiesSize + metricOffset;
        zet_typed_value_t metricTypedValue =
            totalMetricValues[metricStartIndex + metricIndex];
        uint64_t metricValue = metricTypedValue.value.ui64;
        return metricValue;
      };

      tmpStallCount = getStallCount(report, metricProperties.size(),
                                    activeOffset, metricSetStartIndex);
      reportCompleteFlag |= (tmpStallCount != 0);
      ActiveCount += tmpStallCount;

      tmpStallCount = getStallCount(report, metricProperties.size(),
                                    controlStallOffset, metricSetStartIndex);
      reportCompleteFlag |= (tmpStallCount != 0);
      ControlStallCount += tmpStallCount;

      tmpStallCount = getStallCount(report, metricProperties.size(),
                                    pipeStallOffset, metricSetStartIndex);
      reportCompleteFlag |= (tmpStallCount != 0);
      PipeStallCount += tmpStallCount;

      tmpStallCount = getStallCount(report, metricProperties.size(),
                                    sendStallOffset, metricSetStartIndex);
      reportCompleteFlag |= (tmpStallCount != 0);
      SendStallCount += tmpStallCount;

      tmpStallCount = getStallCount(report, metricProperties.size(),
                                    distStallOffset, metricSetStartIndex);
      reportCompleteFlag |= (tmpStallCount != 0);
      DistStallCount += tmpStallCount;

      tmpStallCount = getStallCount(report, metricProperties.size(),
                                    sbidStallOffset, metricSetStartIndex);
      reportCompleteFlag |= (tmpStallCount != 0);
      SbidStallCount += tmpStallCount;

      tmpStallCount = getStallCount(report, metricProperties.size(),
                                    syncStallOffset, metricSetStartIndex);
      reportCompleteFlag |= (tmpStallCount != 0);
      SyncStallCount += tmpStallCount;

      tmpStallCount = getStallCount(report, metricProperties.size(),
                                    instrFetchStallOffset, metricSetStartIndex);
      reportCompleteFlag |= (tmpStallCount != 0);
      InstrFetchStallCount += tmpStallCount;

      tmpStallCount = getStallCount(report, metricProperties.size(),
                                    otherStallOffset, metricSetStartIndex);
      reportCompleteFlag |= (tmpStallCount != 0);
      OtherStallCount += tmpStallCount;

      EXPECT_TRUE(reportCompleteFlag)
          << "Report number " << report << " has zero for all stall counts";
    }

    metricSetStartIndex += metricCountForDataIndex;
  }

  LOG_DEBUG << "ActiveCount " << ActiveCount;
  LOG_DEBUG << "ControlStallCount " << ControlStallCount;
  LOG_DEBUG << "PipeStallCount " << PipeStallCount;
  LOG_DEBUG << "SendStallCount " << SendStallCount;
  LOG_DEBUG << "DistStallCount " << DistStallCount;
  LOG_DEBUG << "SbidStallCount " << SbidStallCount;
  LOG_DEBUG << "SyncStallCount " << SyncStallCount;
  LOG_DEBUG << "InstrFetchStallCount " << InstrFetchStallCount;
  LOG_DEBUG << "OtherStallCount " << OtherStallCount;
}

void run_ip_sampling_with_validation(
    bool enableOverflow, const std::vector<ze_device_handle_t> &devices,
    uint32_t notifyEveryNReports, uint32_t samplingPeriod,
    uint32_t timeForNReportsComplete) {

  uint32_t numberOfFunctionCalls;
  if (enableOverflow) {
    numberOfFunctionCalls = 8;
  } else {
    numberOfFunctionCalls = 1;
  }

  typedef struct _function_data {
    ze_kernel_handle_t function;
    ze_group_count_t tg;
    void *a_buffer, *b_buffer, *c_buffer;
  } function_data_t;

  std::vector<function_data_t> functionDataBuf(numberOfFunctionCalls);

  for (auto device : devices) {

    ze_device_properties_t deviceProperties = {
        ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES, nullptr};
    zeDeviceGetProperties(device, &deviceProperties);

    LOG_INFO << "test device name " << deviceProperties.name << " uuid "
             << lzt::to_string(deviceProperties.uuid);
    if (deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE) {
      LOG_INFO << "test subdevice id " << deviceProperties.subdeviceId;
    } else {
      LOG_INFO << "test device is a root device";
    }

    ze_command_queue_handle_t commandQueue = lzt::create_command_queue(device);
    zet_command_list_handle_t commandList = lzt::create_command_list(device);

    auto metricGroupInfo = lzt::get_metric_type_ip_group_info(
        device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED);
    if (metricGroupInfo.size() == 0) {
      GTEST_SKIP()
          << "No IP metric groups are available to test on this platform";
    }
    metricGroupInfo = lzt::optimize_metric_group_info_list(metricGroupInfo);

    for (auto groupInfo : metricGroupInfo) {

      LOG_INFO << "test metricGroup name " << groupInfo.metricGroupName;

      lzt::activate_metric_groups(device, 1, &groupInfo.metricGroupHandle);

      ze_event_handle_t eventHandle;
      lzt::zeEventPool eventPool;
      eventPool.create_event(eventHandle, ZE_EVENT_SCOPE_FLAG_HOST,
                             ZE_EVENT_SCOPE_FLAG_HOST);

      for (auto &fData : functionDataBuf) {
        fData.function = get_matrix_multiplication_kernel(
            device, &fData.tg, &fData.a_buffer, &fData.b_buffer,
            &fData.c_buffer, 8192);
        zeCommandListAppendLaunchKernel(commandList, fData.function, &fData.tg,
                                        nullptr, 0, nullptr);
      }

      lzt::close_command_list(commandList);
      std::chrono::steady_clock::time_point startTime =
          std::chrono::steady_clock::now();

      zet_metric_streamer_handle_t metricStreamerHandle =
          lzt::metric_streamer_open_for_device(
              device, groupInfo.metricGroupHandle, eventHandle,
              notifyEveryNReports, samplingPeriod);
      ASSERT_NE(nullptr, metricStreamerHandle);

      lzt::execute_command_lists(commandQueue, 1, &commandList, nullptr);
      lzt::synchronize(commandQueue, std::numeric_limits<uint64_t>::max());

      std::chrono::steady_clock::time_point endTime =
          std::chrono::steady_clock::now();
      uint64_t elapsedTime =
          std::chrono::duration_cast<std::chrono::nanoseconds>(endTime -
                                                               startTime)
              .count();

      LOG_INFO << "elapsed time for workload completion " << elapsedTime
               << " time for NReports to complete " << timeForNReportsComplete;
      if (elapsedTime < timeForNReportsComplete) {
        LOG_WARNING << "elapsed time for workload completion is too short";
      }

      const char *sleep_in_buffer_overflow_test_environment_variable =
          std::getenv("LZT_METRICS_BUFFER_OVERFLOW_SLEEP_MS");

      if (sleep_in_buffer_overflow_test_environment_variable != nullptr) {
        uint32_t value =
            atoi(sleep_in_buffer_overflow_test_environment_variable);
        std::this_thread::sleep_for(std::chrono::milliseconds(value));
      }

      size_t rawDataSize = 0;
      std::vector<uint8_t> rawData;
      rawDataSize = lzt::metric_streamer_read_data_size(metricStreamerHandle,
                                                        notifyEveryNReports);
      EXPECT_GT(rawDataSize, 0);
      rawData.resize(rawDataSize);
      lzt::metric_streamer_read_data(metricStreamerHandle, notifyEveryNReports,
                                     rawDataSize, &rawData);
      lzt::validate_metrics(groupInfo.metricGroupHandle, rawDataSize,
                            rawData.data(), false);
      rawData.resize(rawDataSize);

      std::vector<zet_typed_value_t> metricValues;
      std::vector<uint32_t> metricValueSets;
      ze_result_t result =
          level_zero_tests::metric_calculate_metric_values_from_raw_data(
              groupInfo.metricGroupHandle, rawData, metricValues,
              metricValueSets);

      if (enableOverflow) {
        ASSERT_EQ(ZE_RESULT_WARNING_DROPPED_DATA, result);
      } else {
        ASSERT_ZE_RESULT_SUCCESS(result);
      }

      std::vector<zet_metric_handle_t> metricHandles;
      lzt::metric_get_metric_handles_from_metric_group(
          groupInfo.metricGroupHandle, metricHandles);
      std::vector<zet_metric_properties_t> metricProperties(
          metricHandles.size());
      lzt::metric_get_metric_properties_for_metric_group(metricHandles,
                                                         metricProperties);

      metric_validate_stall_sampling_data(metricProperties, metricValues,
                                          metricValueSets);

      lzt::metric_streamer_close(metricStreamerHandle);
      lzt::deactivate_metric_groups(device);

      for (auto &fData : functionDataBuf) {
        lzt::destroy_function(fData.function);
        lzt::free_memory(fData.a_buffer);
        lzt::free_memory(fData.b_buffer);
        lzt::free_memory(fData.c_buffer);
      }

      eventPool.destroy_event(eventHandle);
      lzt::reset_command_list(commandList);
    }
    lzt::destroy_command_queue(commandQueue);
    lzt::destroy_command_list(commandList);
  }
}

LZT_TEST_F(
    zetMetricStreamerTest,
    GivenValidTypeIpMetricGroupWhenTimerBasedStreamerIsCreatedAndOverflowTriggeredThenExpectStreamerValidateError) {
  run_ip_sampling_with_validation(true, devices, notifyEveryNReports,
                                  samplingPeriod, TimeForNReportsComplete);
}

LZT_TEST_F(
    zetMetricStreamerTest,
    GivenValidTypeIpMetricGroupWhenTimerBasedStreamerIsCreatedWithNoOverflowThenValidateStallSampleData) {
  run_ip_sampling_with_validation(false, devices, notifyEveryNReports,
                                  samplingPeriod, TimeForNReportsComplete);
}

LZT_TEST_F(
    zetMetricStreamerTest,
    GivenValidTypeIpMetricGroupWhenTimerBasedStreamerIsCreatedAndBufferOverflowIsTriggeredThenProperErrorIsReturned) {

  samplingPeriod = 100; // use fastest possible rate;

  for (auto device : devices) {
    ze_device_properties_t deviceProperties = {
        ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES, nullptr};
    zeDeviceGetProperties(device, &deviceProperties);

    LOG_INFO << "test device name " << deviceProperties.name << " uuid "
             << lzt::to_string(deviceProperties.uuid);
    if (deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE) {
      LOG_INFO << "test subdevice id " << deviceProperties.subdeviceId;
    } else {
      LOG_INFO << "test device is a root device";
    }

    ze_command_queue_handle_t commandQueue = lzt::create_command_queue(device);
    zet_command_list_handle_t commandList = lzt::create_command_list(device);

    auto metricGroupInfo = lzt::get_metric_type_ip_group_info(
        device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED);
    if (metricGroupInfo.size() == 0) {
      GTEST_SKIP()
          << "No IP metric groups are available to test on this platform";
    }
    metricGroupInfo = lzt::optimize_metric_group_info_list(metricGroupInfo);

    for (auto groupInfo : metricGroupInfo) {
      LOG_INFO << "test metricGroup name " << groupInfo.metricGroupName;
      lzt::activate_metric_groups(device, 1, &groupInfo.metricGroupHandle);

      ze_event_handle_t eventHandle;
      lzt::zeEventPool eventPool;
      eventPool.create_event(eventHandle, ZE_EVENT_SCOPE_FLAG_HOST,
                             ZE_EVENT_SCOPE_FLAG_HOST);

      void *a_buffer, *b_buffer, *c_buffer;
      ze_group_count_t tg;
      ze_kernel_handle_t function = get_matrix_multiplication_kernel(
          device, &tg, &a_buffer, &b_buffer, &c_buffer, 8192);
      zeCommandListAppendLaunchKernel(commandList, function, &tg, nullptr, 0,
                                      nullptr);
      lzt::close_command_list(commandList);

      zet_metric_streamer_handle_t metricStreamerHandle =
          lzt::metric_streamer_open_for_device(
              device, groupInfo.metricGroupHandle, eventHandle,
              notifyEveryNReports, samplingPeriod);
      ASSERT_NE(nullptr, metricStreamerHandle);

      // Spawn a thread which continuously runs a workload
      workloadThreadFlag = true;
      std::thread thread(workloadThread, commandQueue, 1, &commandList,
                         nullptr);

      size_t rawDataSize = 0;
      std::vector<uint8_t> rawData;
      ze_result_t result;
      constexpr uint32_t maxAttempts = 6;
      uint64_t timeForNextIterationSec = 10;

      for (uint32_t i = 0; i < maxAttempts; i++) {
        // Busy wait before trying to read to increase chanceof  buffer overflow
        LOG_INFO << "Busy waiting for " << timeForNextIterationSec
                 << " in iteration " << i;
        time_t begin = std::time(0);
        while (1) {
          time_t end = std::time(0);
          if (end >= (begin + timeForNextIterationSec)) {
            break;
          }
        }
        rawDataSize = lzt::metric_streamer_read_data_size(metricStreamerHandle,
                                                          notifyEveryNReports);
        EXPECT_GT(rawDataSize, 0);
        rawData.resize(rawDataSize);
        result =
            zetMetricStreamerReadData(metricStreamerHandle, notifyEveryNReports,
                                      &rawDataSize, rawData.data());
        LOG_DEBUG << "read data is " << rawDataSize;

        if (result == ZE_RESULT_WARNING_DROPPED_DATA) {
          break;
        }

        ASSERT_ZE_RESULT_SUCCESS(result);
        std::vector<zet_typed_value_t> metricValues;
        std::vector<uint32_t> metricValueSets;
        result = level_zero_tests::metric_calculate_metric_values_from_raw_data(
            groupInfo.metricGroupHandle, rawData, metricValues,
            metricValueSets);
        if (result == ZE_RESULT_WARNING_DROPPED_DATA) {
          break;
        }
        ASSERT_ZE_RESULT_SUCCESS(result);
        timeForNextIterationSec += timeForNextIterationSec;
      }

      workloadThreadFlag = false;
      thread.join();

      lzt::metric_streamer_close(metricStreamerHandle);
      lzt::deactivate_metric_groups(device);
      lzt::destroy_function(function);
      lzt::free_memory(a_buffer);
      lzt::free_memory(b_buffer);
      lzt::free_memory(c_buffer);
      eventPool.destroy_event(eventHandle);
      lzt::reset_command_list(commandList);
    }
    lzt::destroy_command_queue(commandQueue);
    lzt::destroy_command_list(commandList);
  }
}

using zetMetricStreamerAppendMarkerTestNoValidate = zetMetricStreamerTest;

LZT_TEST_F(
    zetMetricStreamerAppendMarkerTestNoValidate,
    GivenValidMetricGroupWhenTimerBasedStreamerIsCreatedWithAppendStreamerMarkerNoValidateThenExpectStreamerToSucceed) {

  for (auto device : devices) {

    ze_device_properties_t deviceProperties = {
        ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES, nullptr};
    zeDeviceGetProperties(device, &deviceProperties);

    LOG_INFO << "test device name " << deviceProperties.name << " uuid "
             << lzt::to_string(deviceProperties.uuid);
    if (deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE) {
      LOG_INFO << "test subdevice id " << deviceProperties.subdeviceId;
    } else {
      LOG_INFO << "test device is a root device";
    }

    ze_command_queue_handle_t commandQueue = lzt::create_command_queue(device);
    zet_command_list_handle_t commandList = lzt::create_command_list(device);

    auto metricGroupInfo = lzt::get_metric_group_info(
        device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED, false, true);
    ASSERT_GT(metricGroupInfo.size(), 0u) << "No metric groups found";
    metricGroupInfo = lzt::optimize_metric_group_info_list(metricGroupInfo);

    uint32_t markerGroupCount{};

    for (auto groupInfo : metricGroupInfo) {

      LOG_INFO << "test metricGroup name " << groupInfo.metricGroupName;

      lzt::activate_metric_groups(device, 1, &groupInfo.metricGroupHandle);

      ze_event_handle_t eventHandle{};
      lzt::zeEventPool eventPool{};
      ze_result_t markerResult;
      eventPool.create_event(eventHandle, ZE_EVENT_SCOPE_FLAG_HOST,
                             ZE_EVENT_SCOPE_FLAG_HOST);

      void *a_buffer, *b_buffer, *c_buffer;
      ze_group_count_t tg{};
      ze_kernel_handle_t function = get_matrix_multiplication_kernel(
          device, &tg, &a_buffer, &b_buffer, &c_buffer);

      zet_metric_streamer_handle_t metricStreamerHandle =
          lzt::metric_streamer_open_for_device(
              device, groupInfo.metricGroupHandle, eventHandle,
              notifyEveryNReports, samplingPeriod);
      ASSERT_NE(nullptr, metricStreamerHandle);

      uint32_t streamerMarker = 0;
      markerResult = lzt::commandlist_append_streamer_marker(
          commandList, metricStreamerHandle, ++streamerMarker);
      lzt::append_barrier(commandList);

      if (ZE_RESULT_SUCCESS == markerResult) {
        markerGroupCount++;
        zeCommandListAppendLaunchKernel(commandList, function, &tg, nullptr, 0,
                                        nullptr);
        lzt::append_barrier(commandList);
        markerResult = lzt::commandlist_append_streamer_marker(
            commandList, metricStreamerHandle, ++streamerMarker);
        lzt::append_barrier(commandList);
        lzt::close_command_list(commandList);
        lzt::execute_command_lists(commandQueue, 1, &commandList, nullptr);
        lzt::synchronize(commandQueue, std::numeric_limits<uint64_t>::max());
        ze_result_t eventResult;
        eventResult = zeEventQueryStatus(eventHandle);

        if (ZE_RESULT_SUCCESS == eventResult) {
          size_t oneReportSize, allReportsSize;
          oneReportSize =
              lzt::metric_streamer_read_data_size(metricStreamerHandle, 1);
          allReportsSize = lzt::metric_streamer_read_data_size(
              metricStreamerHandle, UINT32_MAX);
          LOG_DEBUG << "Event triggered. Single report size: " << oneReportSize
                    << ". All reports size:" << allReportsSize;

          EXPECT_GE(allReportsSize / oneReportSize, notifyEveryNReports);

        } else if (ZE_RESULT_NOT_READY == eventResult) {
          LOG_WARNING << "wait on event returned ZE_RESULT_NOT_READY";
        } else {
          FAIL() << "zeEventQueryStatus() FAILED with " << eventResult;
        }

        std::vector<uint8_t> rawData;
        lzt::metric_streamer_read_data(metricStreamerHandle, &rawData);
      } else if (ZE_RESULT_ERROR_UNSUPPORTED_FEATURE == markerResult) {
        LOG_INFO << "metricGroup " << groupInfo.metricGroupName
                 << " doesn't support streamer marker";
      } else {
        FAIL() << "zetCommandListAppendMetricStreamerMarker() FAILED with "
               << markerResult;
      }
      lzt::metric_streamer_close(metricStreamerHandle);
      lzt::deactivate_metric_groups(device);
      lzt::destroy_function(function);
      lzt::free_memory(a_buffer);
      lzt::free_memory(b_buffer);
      lzt::free_memory(c_buffer);
      eventPool.destroy_event(eventHandle);
      lzt::reset_command_list(commandList);
    }
    lzt::destroy_command_queue(commandQueue);
    lzt::destroy_command_list(commandList);
    EXPECT_NE(0, markerGroupCount);
  }
}

using zetMetricStreamerAppendMarkerTest = zetMetricStreamerTest;

LZT_TEST_F(
    zetMetricStreamerAppendMarkerTest,
    GivenValidMetricGroupWhenTimerBasedStreamerIsCreatedWithAppendStreamerMarkerThenExpectStreamerToSucceed) {

  for (auto device : devices) {

    ze_device_properties_t deviceProperties = {
        ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES, nullptr};
    zeDeviceGetProperties(device, &deviceProperties);

    LOG_INFO << "test device name " << deviceProperties.name << " uuid "
             << lzt::to_string(deviceProperties.uuid);
    if (deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE) {
      LOG_INFO << "test subdevice id " << deviceProperties.subdeviceId;
    } else {
      LOG_INFO << "test device is a root device";
    }

    ze_command_queue_handle_t commandQueue = lzt::create_command_queue(device);
    zet_command_list_handle_t commandList = lzt::create_command_list(device);

    auto metricGroupInfo = lzt::get_metric_group_info(
        device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED, false, true);
    ASSERT_GT(metricGroupInfo.size(), 0u) << "No metric groups found";
    metricGroupInfo = lzt::optimize_metric_group_info_list(metricGroupInfo);

    uint32_t markerGroupCount{};

    for (auto groupInfo : metricGroupInfo) {

      LOG_INFO << "test metricGroup name " << groupInfo.metricGroupName;

      std::vector<uint32_t> streamerMarkerValues = {10, 20};

      lzt::activate_metric_groups(device, 1, &groupInfo.metricGroupHandle);

      std::vector<zet_metric_handle_t> metricHandles;
      lzt::metric_get_metric_handles_from_metric_group(
          groupInfo.metricGroupHandle, metricHandles);
      std::vector<zet_metric_properties_t> metricProperties(
          metricHandles.size());
      lzt::metric_get_metric_properties_for_metric_group(metricHandles,
                                                         metricProperties);

      ze_event_handle_t eventHandle{};
      lzt::zeEventPool eventPool{};
      ze_result_t markerResult;
      eventPool.create_event(eventHandle, ZE_EVENT_SCOPE_FLAG_HOST,
                             ZE_EVENT_SCOPE_FLAG_HOST);

      zet_metric_streamer_handle_t metricStreamerHandle =
          lzt::metric_streamer_open_for_device(
              device, groupInfo.metricGroupHandle, eventHandle,
              notifyEveryNReports, samplingPeriod);
      ASSERT_NE(nullptr, metricStreamerHandle);

      void *a_buffer, *b_buffer, *c_buffer;
      ze_group_count_t tg{};
      ze_kernel_handle_t function = get_matrix_multiplication_kernel(
          device, &tg, &a_buffer, &b_buffer, &c_buffer);

      markerResult = lzt::commandlist_append_streamer_marker(
          commandList, metricStreamerHandle, streamerMarkerValues[0]);
      lzt::append_barrier(commandList);

      if (ZE_RESULT_SUCCESS == markerResult) {
        markerGroupCount++;
        zeCommandListAppendLaunchKernel(commandList, function, &tg, nullptr, 0,
                                        nullptr);
        lzt::append_barrier(commandList);
        markerResult = lzt::commandlist_append_streamer_marker(
            commandList, metricStreamerHandle, streamerMarkerValues[1]);
        lzt::append_barrier(commandList);
        lzt::close_command_list(commandList);
        lzt::execute_command_lists(commandQueue, 1, &commandList, nullptr);
        lzt::synchronize(commandQueue, std::numeric_limits<uint64_t>::max());
        ze_result_t eventResult;
        eventResult = zeEventQueryStatus(eventHandle);

        if (ZE_RESULT_SUCCESS == eventResult) {
          size_t oneReportSize, allReportsSize;
          oneReportSize =
              lzt::metric_streamer_read_data_size(metricStreamerHandle, 1);
          allReportsSize = lzt::metric_streamer_read_data_size(
              metricStreamerHandle, UINT32_MAX);
          LOG_DEBUG << "Event triggered. Single report size: " << oneReportSize
                    << ". All reports size:" << allReportsSize;

          EXPECT_GE(allReportsSize / oneReportSize, notifyEveryNReports);

        } else if (ZE_RESULT_NOT_READY == eventResult) {
          LOG_WARNING << "wait on event returned ZE_RESULT_NOT_READY";
        } else {
          FAIL() << "zeEventQueryStatus() FAILED with " << eventResult;
        }

        const char *value_string =
            std::getenv("LZT_METRIC_READ_DATA_MAX_DURATION_MS");
        uint32_t max_wait_time_in_milliseconds = 10;
        if (value_string != nullptr) {
          uint32_t value = atoi(value_string);
          max_wait_time_in_milliseconds =
              value != 0 ? value : max_wait_time_in_milliseconds;
          max_wait_time_in_milliseconds =
              std::min(max_wait_time_in_milliseconds, 100u);
        }
        auto start_time = std::chrono::high_resolution_clock::now();
        auto duration =
            std::chrono::milliseconds(max_wait_time_in_milliseconds);
        uint32_t streamer_marker_values_index = 0;

        // Since there can be a delay in reading data from the hardware buffer,
        // all data may not be available at once and may need to be fetched in
        // batches. This loop will continuously read, calculate and validate
        // until either the specified wait time is reached or all the markers
        // have been validated.
        while ((std::chrono::high_resolution_clock::now() - start_time) <
                   duration &&
               (streamer_marker_values_index < streamerMarkerValues.size())) {
          std::vector<uint8_t> rawData{};
          uint32_t rawDataSize{};
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
          lzt::metric_streamer_read_data(metricStreamerHandle, rawDataSize,
                                         &rawData);
          // Keep retrying until raw data (complete or in batches) is available.
          if (rawDataSize == 0) {
            continue;
          }
          rawData.resize(rawDataSize);
          std::vector<zet_typed_value_t> metricValues;
          std::vector<uint32_t> metricValueSets;
          ze_result_t result =
              lzt::metric_calculate_metric_values_from_raw_data(
                  groupInfo.metricGroupHandle, rawData, metricValues,
                  metricValueSets);
          ASSERT_ZE_RESULT_SUCCESS(result);

          lzt::metric_validate_streamer_marker_data(
              metricProperties, metricValues, metricValueSets,
              streamerMarkerValues, streamer_marker_values_index);
        }
        // Expecting that all streamer marker values have been validated by this
        // point.
        EXPECT_EQ(streamerMarkerValues.size(), streamer_marker_values_index);
      } else if (ZE_RESULT_ERROR_UNSUPPORTED_FEATURE == markerResult) {
        LOG_INFO << "metricGroup " << groupInfo.metricGroupName
                 << " doesn't support streamer marker";
      } else {
        FAIL() << "zetCommandListAppendMetricStreamerMarker() FAILED with "
               << markerResult;
      }
      lzt::metric_streamer_close(metricStreamerHandle);
      lzt::deactivate_metric_groups(device);
      lzt::destroy_function(function);
      lzt::free_memory(a_buffer);
      lzt::free_memory(b_buffer);
      lzt::free_memory(c_buffer);
      eventPool.destroy_event(eventHandle);
      lzt::reset_command_list(commandList);
    }
    lzt::destroy_command_queue(commandQueue);
    lzt::destroy_command_list(commandList);
    EXPECT_NE(0, markerGroupCount);
  }
}

class zetMetricStreamerStdTest : public ::testing::Test {
protected:
  std::vector<ze_device_handle_t> devices;

  uint32_t notifyEveryNReports = 3000;
  uint32_t samplingPeriod = 1000000;
  ze_device_handle_t device;

  void SetUp() override { devices = lzt::get_metric_test_no_subdevices_list(); }
};

LZT_TEST_F(
    zetMetricStreamerStdTest,
    GivenValidMetricGroupWhenTimerBasedStreamerWithNoSubDevicesListIsCreatedWithAppendStreamerMarkerThenExpectStreamerAndSpecValidateToSucceed) {

  for (auto device : devices) {

    uint32_t subDeviceCount = 0;
    EXPECT_ZE_RESULT_SUCCESS(
        zeDeviceGetSubDevices(device, &subDeviceCount, nullptr));
    if (subDeviceCount != 0) {
      continue;
    }

    ze_device_properties_t deviceProperties = {
        ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES, nullptr};
    zeDeviceGetProperties(device, &deviceProperties);

    LOG_INFO << "test device name " << deviceProperties.name << " uuid "
             << lzt::to_string(deviceProperties.uuid);
    if (deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE) {
      LOG_INFO << "test subdevice id " << deviceProperties.subdeviceId;
    } else {
      LOG_INFO << "test device is a root device";
    }

    ze_command_queue_handle_t commandQueue = lzt::create_command_queue(device);
    zet_command_list_handle_t commandList = lzt::create_command_list(device);

    auto metricGroupInfo = lzt::get_metric_group_info(
        device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED, false, true);
    ASSERT_GT(metricGroupInfo.size(), 0u) << "No metric groups found";
    metricGroupInfo = lzt::optimize_metric_group_info_list(metricGroupInfo, 1);

    uint32_t markerGroupCount{};

    for (auto groupInfo : metricGroupInfo) {

      LOG_INFO << "test metricGroup name " << groupInfo.metricGroupName;

      lzt::activate_metric_groups(device, 1, &groupInfo.metricGroupHandle);

      ze_event_handle_t eventHandle{};
      lzt::zeEventPool eventPool{};
      ze_result_t markerResult;
      eventPool.create_event(eventHandle, ZE_EVENT_SCOPE_FLAG_HOST,
                             ZE_EVENT_SCOPE_FLAG_HOST);

      zet_metric_streamer_handle_t metricStreamerHandle =
          lzt::metric_streamer_open_for_device(
              device, groupInfo.metricGroupHandle, eventHandle,
              notifyEveryNReports, samplingPeriod);
      ASSERT_NE(nullptr, metricStreamerHandle);

      void *a_buffer, *b_buffer, *c_buffer;
      ze_group_count_t tg{};
      ze_kernel_handle_t function = get_matrix_multiplication_kernel(
          device, &tg, &a_buffer, &b_buffer, &c_buffer);

      uint32_t streamerMarker = 0;
      markerResult = lzt::commandlist_append_streamer_marker(
          commandList, metricStreamerHandle, ++streamerMarker);

      if (ZE_RESULT_SUCCESS == markerResult) {
        markerGroupCount++;
        zeCommandListAppendLaunchKernel(commandList, function, &tg, nullptr, 0,
                                        nullptr);
        lzt::append_barrier(commandList);
        markerResult = lzt::commandlist_append_streamer_marker(
            commandList, metricStreamerHandle, ++streamerMarker);
        lzt::append_barrier(commandList);
        lzt::close_command_list(commandList);
        lzt::execute_command_lists(commandQueue, 1, &commandList, nullptr);
        lzt::synchronize(commandQueue, std::numeric_limits<uint64_t>::max());
        ze_result_t eventResult;
        eventResult = zeEventQueryStatus(eventHandle);

        if (ZE_RESULT_SUCCESS == eventResult) {
          size_t oneReportSize, allReportsSize;
          oneReportSize =
              lzt::metric_streamer_read_data_size(metricStreamerHandle, 1);
          allReportsSize = lzt::metric_streamer_read_data_size(
              metricStreamerHandle, UINT32_MAX);
          LOG_DEBUG << "Event triggered. Single report size: " << oneReportSize
                    << ". All reports size:" << allReportsSize;

          EXPECT_GE(allReportsSize / oneReportSize, notifyEveryNReports);

        } else if (ZE_RESULT_NOT_READY == eventResult) {
          LOG_WARNING << "wait on event returned ZE_RESULT_NOT_READY";
        } else {
          FAIL() << "zeEventQueryStatus() FAILED with " << eventResult;
        }

        std::vector<uint8_t> rawData;
        uint32_t rawDataSize = 0;
        lzt::metric_streamer_read_data(metricStreamerHandle, rawDataSize,
                                       &rawData);
        lzt::validate_metrics_std(groupInfo.metricGroupHandle, rawDataSize,
                                  rawData.data());
      } else if (ZE_RESULT_ERROR_UNSUPPORTED_FEATURE == markerResult) {
        LOG_INFO << "metricGroup " << groupInfo.metricGroupName
                 << " doesn't support streamer marker";
      } else {
        FAIL() << "zetCommandListAppendMetricStreamerMarker() FAILED with "
               << markerResult;
      }
      lzt::metric_streamer_close(metricStreamerHandle);
      lzt::deactivate_metric_groups(device);
      lzt::destroy_function(function);
      lzt::free_memory(a_buffer);
      lzt::free_memory(b_buffer);
      lzt::free_memory(c_buffer);
      eventPool.destroy_event(eventHandle);
      lzt::reset_command_list(commandList);
    }
    lzt::destroy_command_queue(commandQueue);
    lzt::destroy_command_list(commandList);
    EXPECT_NE(0, markerGroupCount);
  }
}

LZT_TEST(
    zetMetricStreamProcessTest,
    GivenWorkloadExecutingInSeparateProcessWhenStreamingSingleMetricsThenExpectValidMetrics) {

  auto driver = lzt::get_default_driver();
  auto device = lzt::get_default_device(driver);

  // setup monitor
  auto metricGroupInfo = lzt::get_metric_group_info(
      device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED, true, true);
  ASSERT_GT(metricGroupInfo.size(), 0u) << "No metric groups found";
  metricGroupInfo = lzt::optimize_metric_group_info_list(metricGroupInfo);

  // pick a metric group
  auto groupInfo = metricGroupInfo[0];

  LOG_INFO << "Selected metric group: " << groupInfo.metricGroupName
           << " domain: " << groupInfo.domain;

  LOG_INFO << "Activating metric group: " << groupInfo.metricGroupName;
  lzt::activate_metric_groups(device, 1, &groupInfo.metricGroupHandle);

  ze_event_handle_t eventHandle;
  lzt::zeEventPool eventPool;
  eventPool.create_event(eventHandle, ZE_EVENT_SCOPE_FLAG_HOST,
                         ZE_EVENT_SCOPE_FLAG_HOST);

  uint32_t notifyEveryNReports = 4500;
  uint32_t samplingPeriod = 1000000;
  zet_metric_streamer_handle_t metricStreamerHandle =
      lzt::metric_streamer_open_for_device(device, groupInfo.metricGroupHandle,
                                           eventHandle, notifyEveryNReports,
                                           samplingPeriod);
  ASSERT_NE(nullptr, metricStreamerHandle);

  //================================================================================
  LOG_INFO << "Starting workload in separate process";
  fs::path helper_path(fs::current_path() / "metrics");
  std::vector<fs::path> paths;
  paths.push_back(helper_path);
  fs::path helper = bp::search_path("test_metric_helper", paths);
  bp::child metric_helper(helper);

  // start monitor
  do {
    LOG_DEBUG << "Waiting for data (event synchronize)...";
    lzt::event_host_synchronize(eventHandle,
                                std::numeric_limits<uint64_t>::max());
    lzt::event_host_reset(eventHandle);

    // read data
    size_t oneReportSize, allReportsSize;
    oneReportSize =
        lzt::metric_streamer_read_data_size(metricStreamerHandle, 1);
    allReportsSize =
        lzt::metric_streamer_read_data_size(metricStreamerHandle, UINT32_MAX);

    LOG_DEBUG << "Event triggered. Single report size: " << oneReportSize
              << ". All reports size:" << allReportsSize;

    EXPECT_GE(allReportsSize / oneReportSize, notifyEveryNReports);

    size_t rawDataSize = 0;
    std::vector<uint8_t> rawData;
    rawDataSize = lzt::metric_streamer_read_data_size(metricStreamerHandle,
                                                      notifyEveryNReports);
    EXPECT_GT(rawDataSize, 0);
    rawData.resize(rawDataSize);
    lzt::metric_streamer_read_data(metricStreamerHandle, notifyEveryNReports,
                                   rawDataSize, &rawData);
    lzt::validate_metrics(groupInfo.metricGroupHandle, rawDataSize,
                          rawData.data());

  } while (metric_helper.running());
  LOG_INFO << "Waiting for process to finish...";
  metric_helper.wait();

  EXPECT_EQ(metric_helper.exit_code(), 0);

  // cleanup
  lzt::metric_streamer_close(metricStreamerHandle);
  lzt::deactivate_metric_groups(device);
  eventPool.destroy_event(eventHandle);
}

LZT_TEST(
    zetMetricStreamProcessTest,
    GivenWorkloadExecutingInSeparateProcessWhenStreamingMetricsAndSendInterruptThenExpectValidMetrics) {
  auto driver = lzt::get_default_driver();
  auto device = lzt::get_default_device(driver);

  // setup monitor
  auto metricGroupInfo = lzt::get_metric_group_info(
      device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED, true, true);
  ASSERT_GT(metricGroupInfo.size(), 0u) << "No metric groups found";
  metricGroupInfo = lzt::optimize_metric_group_info_list(metricGroupInfo);

  // pick a metric group
  auto groupInfo = metricGroupInfo[0];

  LOG_INFO << "Selected metric group: " << groupInfo.metricGroupName
           << " domain: " << groupInfo.domain;

  LOG_INFO << "Activating metric group: " << groupInfo.metricGroupName;
  lzt::activate_metric_groups(device, 1, &groupInfo.metricGroupHandle);

  ze_event_handle_t eventHandle;
  lzt::zeEventPool eventPool;
  eventPool.create_event(eventHandle, ZE_EVENT_SCOPE_FLAG_HOST,
                         ZE_EVENT_SCOPE_FLAG_HOST);

  uint32_t notifyEveryNReports = 4500;
  uint32_t samplingPeriod = 1000000;
  zet_metric_streamer_handle_t metricStreamerHandle =
      lzt::metric_streamer_open_for_device(device, groupInfo.metricGroupHandle,
                                           eventHandle, notifyEveryNReports,
                                           samplingPeriod);
  ASSERT_NE(nullptr, metricStreamerHandle);

  //================================================================================
  LOG_INFO << "Starting workload in separate process";
  fs::path helper_path(fs::current_path() / "metrics");
  std::vector<fs::path> paths;
  paths.push_back(helper_path);
  fs::path helper = bp::search_path("test_metric_helper", paths);
  bp::opstream child_input;
  bp::child metric_helper(helper, "-i", bp::std_in < child_input);

  // start monitor
  LOG_DEBUG << "Waiting for data (event synchronize)...";
  lzt::event_host_synchronize(eventHandle,
                              std::numeric_limits<uint64_t>::max());
  lzt::event_host_reset(eventHandle);

  // read data
  size_t oneReportSize, allReportsSize;
  oneReportSize = lzt::metric_streamer_read_data_size(metricStreamerHandle, 1);
  allReportsSize =
      lzt::metric_streamer_read_data_size(metricStreamerHandle, UINT32_MAX);

  LOG_DEBUG << "Event triggered. Single report size: " << oneReportSize
            << ". All reports size:" << allReportsSize;

  EXPECT_GE(allReportsSize / oneReportSize, notifyEveryNReports);

  size_t rawDataSize = 0;
  std::vector<uint8_t> rawData;
  rawDataSize = lzt::metric_streamer_read_data_size(metricStreamerHandle,
                                                    notifyEveryNReports);
  EXPECT_GT(rawDataSize, 0);
  rawData.resize(rawDataSize);
  lzt::metric_streamer_read_data(metricStreamerHandle, notifyEveryNReports,
                                 rawDataSize, &rawData);
  lzt::validate_metrics(groupInfo.metricGroupHandle, rawDataSize,
                        rawData.data());

  // send interrupt
  LOG_DEBUG << "Sending interrupt to process";
  child_input << "stop" << std::endl;

  // wait 1 second
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // expect helper has exited
  EXPECT_FALSE(metric_helper.running());

  if (metric_helper.running()) {
    LOG_DEBUG << "Ending Helper";
    metric_helper.terminate();
  }
  LOG_DEBUG << "Process exited";

  // cleanup
  lzt::deactivate_metric_groups(device);
  lzt::metric_streamer_close(metricStreamerHandle);
  eventPool.destroy_event(eventHandle);
}

LZT_TEST_F(
    zetMetricStreamerAppendMarkerTest,
    GivenValidMetricGroupWhenTimerBasedStreamerIsCreatedWithAppendStreamerMarkerToImmediateCommandListThenExpectStreamerToSucceed) {

  for (auto device : devices) {

    ze_device_properties_t deviceProperties = {
        ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES, nullptr};
    zeDeviceGetProperties(device, &deviceProperties);

    LOG_INFO << "test device name " << deviceProperties.name << " uuid "
             << lzt::to_string(deviceProperties.uuid);
    if (deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE) {
      LOG_INFO << "test subdevice id " << deviceProperties.subdeviceId;
    } else {
      LOG_INFO << "test device is a root device";
    }

    zet_command_list_handle_t commandList =
        lzt::create_immediate_command_list(device);

    auto metricGroupInfo = lzt::get_metric_group_info(
        device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED, false, true);
    ASSERT_GT(metricGroupInfo.size(), 0u) << "No metric groups found";
    metricGroupInfo = lzt::optimize_metric_group_info_list(metricGroupInfo, 1);

    uint32_t markerGroupCount{};

    for (auto groupInfo : metricGroupInfo) {

      LOG_INFO << "test metricGroup name " << groupInfo.metricGroupName;

      lzt::activate_metric_groups(device, 1, &groupInfo.metricGroupHandle);

      ze_event_handle_t eventHandle{};
      lzt::zeEventPool eventPool{};
      ze_result_t markerResult;
      eventPool.create_event(eventHandle, ZE_EVENT_SCOPE_FLAG_HOST,
                             ZE_EVENT_SCOPE_FLAG_HOST);

      zet_metric_streamer_handle_t metricStreamerHandle =
          lzt::metric_streamer_open_for_device(
              device, groupInfo.metricGroupHandle, eventHandle,
              notifyEveryNReports, samplingPeriod);
      ASSERT_NE(nullptr, metricStreamerHandle);

      void *a_buffer, *b_buffer, *c_buffer;
      ze_group_count_t tg{};
      auto device_properties = lzt::get_device_properties(device);
      const auto max_threads = device_properties.numSlices *
                               device_properties.numSubslicesPerSlice *
                               device_properties.numEUsPerSubslice *
                               device_properties.numThreadsPerEU;
      LOG_INFO << "Available threads: " << max_threads;
      const auto dimensions = (max_threads > 4096 ? 1024 : 2);
      ze_kernel_handle_t function = get_matrix_multiplication_kernel(
          device, &tg, &a_buffer, &b_buffer, &c_buffer, dimensions);

      LOG_INFO << "Dimensions: " << dimensions;

      // Since immediate command list is used, using repeated command list
      // updates to capture metric data
      const uint32_t max_repeat_count = (dimensions > 2) ? 200 : 1;
      for (uint32_t repeat_count = 0; repeat_count < max_repeat_count;
           repeat_count++) {
        uint32_t streamerMarker = 0;
        markerResult = lzt::commandlist_append_streamer_marker(
            commandList, metricStreamerHandle, ++streamerMarker);
        lzt::append_barrier(commandList);
        if (ZE_RESULT_SUCCESS == markerResult) {
          break;
        }
        zeCommandListAppendLaunchKernel(commandList, function, &tg, nullptr, 0,
                                        nullptr);
        lzt::append_barrier(commandList);
        markerResult = lzt::commandlist_append_streamer_marker(
            commandList, metricStreamerHandle, ++streamerMarker);
        lzt::append_barrier(commandList);
        if (ZE_RESULT_SUCCESS == markerResult) {
          break;
        }
      }

      if (ZE_RESULT_SUCCESS == markerResult) {
        markerGroupCount++;
        ze_result_t eventResult;
        eventResult = zeEventHostSynchronize(eventHandle, 5000000000);

        if (ZE_RESULT_SUCCESS == eventResult) {
          size_t oneReportSize, allReportsSize;
          oneReportSize =
              lzt::metric_streamer_read_data_size(metricStreamerHandle, 1);
          allReportsSize = lzt::metric_streamer_read_data_size(
              metricStreamerHandle, UINT32_MAX);
          LOG_DEBUG << "Event triggered. Single report size: " << oneReportSize
                    << ". All reports size:" << allReportsSize;

          EXPECT_GE(allReportsSize / oneReportSize, notifyEveryNReports);

        } else if (ZE_RESULT_NOT_READY == eventResult) {
          LOG_WARNING << "wait on event returned ZE_RESULT_NOT_READY";
        } else {
          FAIL() << "zeEventQueryStatus() FAILED with " << eventResult;
        }

        std::vector<uint8_t> rawData;
        uint32_t rawDataSize = 0;
        lzt::metric_streamer_read_data(metricStreamerHandle, rawDataSize,
                                       &rawData);
        lzt::validate_metrics(groupInfo.metricGroupHandle, rawDataSize,
                              rawData.data());
      } else if (ZE_RESULT_ERROR_UNSUPPORTED_FEATURE == markerResult) {
        LOG_INFO << "metricGroup " << groupInfo.metricGroupName
                 << " doesn't support streamer marker";
      } else {
        FAIL() << "zetCommandListAppendMetricStreamerMarker() FAILED with "
               << markerResult;
      }
      lzt::metric_streamer_close(metricStreamerHandle);
      lzt::deactivate_metric_groups(device);
      lzt::destroy_function(function);
      lzt::free_memory(a_buffer);
      lzt::free_memory(b_buffer);
      lzt::free_memory(c_buffer);
      eventPool.destroy_event(eventHandle);
      lzt::reset_command_list(commandList);
    }
    lzt::destroy_command_list(commandList);
    EXPECT_NE(0, markerGroupCount);
  }
}

void run_multi_device_streamer_test(
    const std::vector<ze_device_handle_t> &devices) {

  LOG_INFO << "Testing multi-device metrics streamer";

  if (devices.size() < 2) {
    GTEST_SKIP() << "Skipping the test as less than 2 devices are available";
  }

  auto device_0 = devices[0];
  auto device_1 = devices[1];
  auto command_queue_0 = lzt::create_command_queue(device_0);
  auto command_queue_1 = lzt::create_command_queue(device_1);

  auto command_list_0 = lzt::create_command_list(device_0);
  auto command_list_1 = lzt::create_command_list(device_1);

  auto metric_group_info_0 = lzt::get_metric_group_info(
      device_0, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED, true, true);
  ASSERT_GT(metric_group_info_0.size(), 0u)
      << "No metric groups found on device 0";
  auto metric_group_info_1 = lzt::get_metric_group_info(
      device_1, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED, true, true);
  ASSERT_GT(metric_group_info_1.size(), 0u)
      << "No metric groups found on device 1";

  metric_group_info_0 =
      lzt::optimize_metric_group_info_list(metric_group_info_0);
  metric_group_info_1 =
      lzt::optimize_metric_group_info_list(metric_group_info_1);

  auto groupInfo_0 = metric_group_info_0[0];
  auto groupInfo_1 = metric_group_info_1[0];

  lzt::activate_metric_groups(device_0, 1, &groupInfo_0.metricGroupHandle);
  lzt::activate_metric_groups(device_1, 1, &groupInfo_1.metricGroupHandle);

  LOG_INFO << "test metricGroup names " << groupInfo_0.metricGroupName << " "
           << groupInfo_1.metricGroupName;

  ze_event_handle_t eventHandle_0, eventHandle_1;
  lzt::zeEventPool eventPool;
  eventPool.create_event(eventHandle_0, ZE_EVENT_SCOPE_FLAG_HOST,
                         ZE_EVENT_SCOPE_FLAG_HOST);
  eventPool.create_event(eventHandle_1, ZE_EVENT_SCOPE_FLAG_HOST,
                         ZE_EVENT_SCOPE_FLAG_HOST);

  uint32_t notifyEveryNReports = 3000;
  uint32_t samplingPeriod = 1000000;
  auto metricStreamerHandle_0 = lzt::metric_streamer_open_for_device(
      device_0, groupInfo_0.metricGroupHandle, eventHandle_0,
      notifyEveryNReports, samplingPeriod);

  auto metricStreamerHandle_1 = lzt::metric_streamer_open_for_device(
      device_1, groupInfo_1.metricGroupHandle, eventHandle_1,
      notifyEveryNReports, samplingPeriod);

  ASSERT_NE(nullptr, metricStreamerHandle_0);
  ASSERT_NE(nullptr, metricStreamerHandle_1);

  void *a_buffer_0, *b_buffer_0, *c_buffer_0;
  ze_group_count_t tg_0;

  void *a_buffer_1, *b_buffer_1, *c_buffer_1;
  ze_group_count_t tg_1;

  auto function_0 = get_matrix_multiplication_kernel(
      device_0, &tg_0, &a_buffer_0, &b_buffer_0, &c_buffer_0);

  auto function_1 = get_matrix_multiplication_kernel(
      device_1, &tg_1, &a_buffer_1, &b_buffer_1, &c_buffer_1);

  lzt::append_launch_function(command_list_0, function_0, &tg_0, nullptr, 0,
                              nullptr);
  lzt::append_launch_function(command_list_1, function_1, &tg_1, nullptr, 0,
                              nullptr);

  lzt::close_command_list(command_list_0);
  lzt::close_command_list(command_list_1);

  lzt::execute_command_lists(command_queue_0, 1, &command_list_0, nullptr);
  lzt::execute_command_lists(command_queue_1, 1, &command_list_1, nullptr);

  lzt::synchronize(command_queue_0, std::numeric_limits<uint64_t>::max());
  lzt::synchronize(command_queue_1, std::numeric_limits<uint64_t>::max());

  auto eventResult_0 = zeEventQueryStatus(eventHandle_0);
  auto eventResult_1 = zeEventQueryStatus(eventHandle_1);

  if (ZE_RESULT_SUCCESS == eventResult_0 &&
      ZE_RESULT_SUCCESS == eventResult_1) {

  } else {
    LOG_WARNING << "Non-Success zeEventQueryStatus: event_0: " << eventResult_0
                << " event_1: " << eventResult_1;
  }

  std::vector<uint8_t> rawData_0, rawData_1;
  uint32_t rawDataSize_0 = 0, rawDataSize_1 = 0;
  lzt::metric_streamer_read_data(metricStreamerHandle_0, rawDataSize_0,
                                 &rawData_0);
  lzt::metric_streamer_read_data(metricStreamerHandle_1, rawDataSize_1,
                                 &rawData_1);

  lzt::validate_metrics(groupInfo_0.metricGroupHandle, rawDataSize_0,
                        rawData_0.data());
  lzt::validate_metrics(groupInfo_1.metricGroupHandle, rawDataSize_1,
                        rawData_1.data());

  // cleanup
  lzt::deactivate_metric_groups(device_0);
  lzt::deactivate_metric_groups(device_1);
  lzt::metric_streamer_close(metricStreamerHandle_0);
  lzt::metric_streamer_close(metricStreamerHandle_1);
  lzt::destroy_function(function_0);
  lzt::destroy_function(function_1);
  lzt::free_memory(a_buffer_0);
  lzt::free_memory(b_buffer_0);
  lzt::free_memory(c_buffer_0);
  lzt::free_memory(a_buffer_1);
  lzt::free_memory(b_buffer_1);
  lzt::free_memory(c_buffer_1);
  lzt::destroy_command_list(command_list_0);
  lzt::destroy_command_list(command_list_1);
  lzt::destroy_command_queue(command_queue_0);
  lzt::destroy_command_queue(command_queue_1);
  eventPool.destroy_event(eventHandle_0);
  eventPool.destroy_event(eventHandle_1);
}

LZT_TEST_F(
    zetMetricStreamerTest,
    GivenValidMetricGroupsWhenMultipleDevicesExecutingThenExpectValidMetrics) {

  auto driver = lzt::get_default_driver();
  auto devices = lzt::get_devices(driver);

  run_multi_device_streamer_test(devices);
}

LZT_TEST_F(
    zetMetricStreamerTest,
    GivenValidMetricGroupsWhenMultipleSubDevicesExecutingThenExpectValidMetrics) {
  auto sub_devices = lzt::get_all_sub_devices();

  run_multi_device_streamer_test(sub_devices);
}

class zetMetricsEnableDisableTest : public ::testing::Test {
protected:
  std::vector<ze_device_handle_t> devices;
  ze_device_handle_t device;

  void SetUp() {
    if (!lzt::check_if_extension_supported(
            lzt::get_default_driver(),
            ZET_METRICS_RUNTIME_ENABLE_DISABLE_EXP_NAME)) {

      GTEST_SKIP() << "Extension "
                   << ZET_METRICS_RUNTIME_ENABLE_DISABLE_EXP_NAME
                   << " is not supported";
    }
    devices = lzt::get_metric_test_device_list();
  }
};

LZT_TEST_F(
    zetMetricsEnableDisableTest,
    GivenMetricsEnabledByEnvironmentWhenMetricsRuntimeAlsoEnabledThenMetricGroupGetAndGetPropertiesSucceed) {

  for (auto device : devices) {
    lzt::enable_metrics_runtime(device);

    ze_device_properties_t deviceProperties = {
        ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES, nullptr};
    zeDeviceGetProperties(device, &deviceProperties);

    LOG_INFO << "test device name " << deviceProperties.name << " uuid "
             << lzt::to_string(deviceProperties.uuid);
    if (deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE) {
      LOG_INFO << "test subdevice id " << deviceProperties.subdeviceId;
    } else {
      LOG_INFO << "test device is a root device";
    }

    auto metricGroupInfo = lzt::get_metric_group_info(
        device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED, true, true);
    EXPECT_GT(metricGroupInfo.size(), 0u) << "No metric groups found";
  }
}

LZT_TEST_F(
    zetMetricsEnableDisableTest,
    GivenMetricsEnabledByEnvironmentWhenMetricsRuntimeDisabledThenMetricGroupGetAndGetPropertiesSucceed) {

  for (auto device : devices) {
    lzt::disable_metrics_runtime(device);

    ze_device_properties_t deviceProperties = {
        ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES, nullptr};
    zeDeviceGetProperties(device, &deviceProperties);

    LOG_INFO << "test device name " << deviceProperties.name << " uuid "
             << lzt::to_string(deviceProperties.uuid);
    if (deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE) {
      LOG_INFO << "test subdevice id " << deviceProperties.subdeviceId;
    } else {
      LOG_INFO << "test device is a root device";
    }

    auto metricGroupInfo = lzt::get_metric_group_info(
        device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED, true, true);
    EXPECT_GT(metricGroupInfo.size(), 0u) << "No metric groups found";
  }
}

class zetMetricsEnableDisableStreamerTest : public zetMetricsEnableDisableTest {
protected:
  static constexpr uint32_t maxReadAttempts = 20;
  static constexpr uint32_t numberOfReportsReq = 100;
  static constexpr uint32_t timeBeforeReadInNanoSec = 500000000;
  uint32_t samplingPeriod = timeBeforeReadInNanoSec / numberOfReportsReq;
  uint32_t notifyEveryNReports = 9000;
};

LZT_TEST_F(
    zetMetricsEnableDisableStreamerTest,
    GivenMetricsEnabledByEnvironmentWhenMetricsRuntimeAlsoEnabledThenMetricStreamerSucceeds) {

  for (auto device : devices) {
    lzt::enable_metrics_runtime(device);

    ze_device_properties_t deviceProperties = {
        ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES, nullptr};
    zeDeviceGetProperties(device, &deviceProperties);

    LOG_INFO << "test device name " << deviceProperties.name << " uuid "
             << lzt::to_string(deviceProperties.uuid);
    if (deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE) {
      LOG_INFO << "test subdevice id " << deviceProperties.subdeviceId;
    } else {
      LOG_INFO << "test device is a root device";
    }

    ze_command_queue_handle_t commandQueue = lzt::create_command_queue(device);
    zet_command_list_handle_t commandList = lzt::create_command_list(device);

    auto metricGroupInfo = lzt::get_metric_group_info(
        device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED, true, true);
    ASSERT_GT(metricGroupInfo.size(), 0u) << "No metric groups found";
    metricGroupInfo = lzt::optimize_metric_group_info_list(metricGroupInfo);

    for (auto groupInfo : metricGroupInfo) {
      LOG_INFO << "test metricGroup name " << groupInfo.metricGroupName;
      lzt::activate_metric_groups(device, 1, &groupInfo.metricGroupHandle);

      void *a_buffer, *b_buffer, *c_buffer;
      ze_group_count_t tg;
      ze_kernel_handle_t function = get_matrix_multiplication_kernel(
          device, &tg, &a_buffer, &b_buffer, &c_buffer, 8192);
      zeCommandListAppendLaunchKernel(commandList, function, &tg, nullptr, 0,
                                      nullptr);
      lzt::close_command_list(commandList);

      // Spawn a thread which continuously runs a workload
      workloadThreadFlag = true;
      std::thread thread(workloadThread, commandQueue, 1, &commandList,
                         nullptr);

      zet_metric_streamer_handle_t metricStreamerHandle =
          lzt::metric_streamer_open_for_device(
              device, groupInfo.metricGroupHandle, nullptr, notifyEveryNReports,
              samplingPeriod);

      // Sleep for timeBeforeReadInNanoSec to ensure required reports are
      // generated
      std::this_thread::sleep_for(
          std::chrono::nanoseconds(timeBeforeReadInNanoSec));
      ASSERT_NE(nullptr, metricStreamerHandle);

      size_t rawDataSize = 0;
      std::vector<uint8_t> rawData;
      rawDataSize = lzt::metric_streamer_read_data_size(metricStreamerHandle,
                                                        notifyEveryNReports);
      EXPECT_GT(rawDataSize, 0);
      rawData.resize(rawDataSize);
      for (uint32_t count = 0; count < maxReadAttempts; count++) {
        lzt::metric_streamer_read_data(
            metricStreamerHandle, notifyEveryNReports, rawDataSize, &rawData);
        if (rawDataSize > 0) {
          break;
        } else {
          std::this_thread::sleep_for(std::chrono::nanoseconds(samplingPeriod));
        }
      }

      LOG_INFO << "rawDataSize " << rawDataSize;
      // Stop the worker thread running the workload
      workloadThreadFlag = false;
      thread.join();

      lzt::validate_metrics(groupInfo.metricGroupHandle, rawDataSize,
                            rawData.data());
      lzt::metric_streamer_close(metricStreamerHandle);
      lzt::deactivate_metric_groups(device);
      lzt::destroy_function(function);
      lzt::free_memory(a_buffer);
      lzt::free_memory(b_buffer);
      lzt::free_memory(c_buffer);
      lzt::reset_command_list(commandList);
    }
    lzt::destroy_command_queue(commandQueue);
    lzt::destroy_command_list(commandList);
    EXPECT_ZE_RESULT_SUCCESS(zetDeviceDisableMetricsExp(device));
  }
}

LZT_TEST_F(
    zetMetricsEnableDisableStreamerTest,
    GivenMetricsEnabledByEnvironmentWhenMetricGroupisActivatedThenMetricsRuntimeDisableFailsUntilMetricGroupIsDeactivated) {

  for (auto device : devices) {

    ze_device_properties_t deviceProperties = {
        ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES, nullptr};
    zeDeviceGetProperties(device, &deviceProperties);

    LOG_INFO << "test device name " << deviceProperties.name << " uuid "
             << lzt::to_string(deviceProperties.uuid);
    if (deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE) {
      LOG_INFO << "test subdevice id " << deviceProperties.subdeviceId;
    } else {
      LOG_INFO << "test device is a root device";
    }

    ze_command_queue_handle_t commandQueue = lzt::create_command_queue(device);
    zet_command_list_handle_t commandList = lzt::create_command_list(device);

    auto metricGroupInfo = lzt::get_metric_group_info(
        device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED, true, true);
    ASSERT_GT(metricGroupInfo.size(), 0u) << "No metric groups found";
    metricGroupInfo = lzt::optimize_metric_group_info_list(metricGroupInfo);

    for (auto groupInfo : metricGroupInfo) {
      lzt::enable_metrics_runtime(device);
      LOG_INFO << "test metricGroup name " << groupInfo.metricGroupName;
      lzt::activate_metric_groups(device, 1, &groupInfo.metricGroupHandle);

      EXPECT_EQ(zetDeviceDisableMetricsExp(device),
                ZE_RESULT_ERROR_HANDLE_OBJECT_IN_USE);

      void *a_buffer, *b_buffer, *c_buffer;
      ze_group_count_t tg;
      ze_kernel_handle_t function = get_matrix_multiplication_kernel(
          device, &tg, &a_buffer, &b_buffer, &c_buffer, 8192);
      zeCommandListAppendLaunchKernel(commandList, function, &tg, nullptr, 0,
                                      nullptr);
      lzt::close_command_list(commandList);

      // Spawn a thread which continuously runs a workload
      workloadThreadFlag = true;
      std::thread thread(workloadThread, commandQueue, 1, &commandList,
                         nullptr);

      zet_metric_streamer_handle_t metricStreamerHandle =
          lzt::metric_streamer_open_for_device(
              device, groupInfo.metricGroupHandle, nullptr, notifyEveryNReports,
              samplingPeriod);
      EXPECT_EQ(zetDeviceDisableMetricsExp(device),
                ZE_RESULT_ERROR_HANDLE_OBJECT_IN_USE);

      // Sleep for timeBeforeReadInNanoSec to ensure required reports are
      // generated
      std::this_thread::sleep_for(
          std::chrono::nanoseconds(timeBeforeReadInNanoSec));
      ASSERT_NE(nullptr, metricStreamerHandle);

      size_t rawDataSize = 0;
      std::vector<uint8_t> rawData;
      rawDataSize = lzt::metric_streamer_read_data_size(metricStreamerHandle,
                                                        notifyEveryNReports);
      EXPECT_EQ(zetDeviceDisableMetricsExp(device),
                ZE_RESULT_ERROR_HANDLE_OBJECT_IN_USE);
      EXPECT_GT(rawDataSize, 0);
      rawData.resize(rawDataSize);
      for (uint32_t count = 0; count < maxReadAttempts; count++) {
        lzt::metric_streamer_read_data(
            metricStreamerHandle, notifyEveryNReports, rawDataSize, &rawData);
        if (rawDataSize > 0) {
          std::vector<zet_typed_value_t> metricValues;
          std::vector<uint32_t> metricValueSets;
          ze_result_t result =
              lzt::metric_calculate_metric_values_from_raw_data(
                  groupInfo.metricGroupHandle, rawData, metricValues,
                  metricValueSets);
          ASSERT_ZE_RESULT_SUCCESS(result);
          ASSERT_EQ(zetDeviceDisableMetricsExp(device),
                    ZE_RESULT_ERROR_HANDLE_OBJECT_IN_USE);
          break;
        } else {
          std::this_thread::sleep_for(std::chrono::nanoseconds(samplingPeriod));
        }
      }

      LOG_INFO << "rawDataSize " << rawDataSize;
      // Stop the worker thread running the workload
      workloadThreadFlag = false;
      thread.join();

      lzt::validate_metrics(groupInfo.metricGroupHandle, rawDataSize,
                            rawData.data());
      lzt::metric_streamer_close(metricStreamerHandle);
      lzt::deactivate_metric_groups(device);
      EXPECT_ZE_RESULT_SUCCESS(zetDeviceDisableMetricsExp(device));
      lzt::destroy_function(function);
      lzt::free_memory(a_buffer);
      lzt::free_memory(b_buffer);
      lzt::free_memory(c_buffer);
      lzt::reset_command_list(commandList);
    }
    lzt::destroy_command_queue(commandQueue);
    lzt::destroy_command_list(commandList);
  }
}

using zetMetricsEnableDisableQueryTest = zetMetricsEnableDisableTest;

LZT_TEST_F(
    zetMetricsEnableDisableQueryTest,
    GivenMetricsEnabledByEnvironmentWhenMetricsRuntimeAlsoEnabledThenMetricQuerySucceeds) {

  for (auto device : devices) {
    lzt::enable_metrics_runtime(device);
    ze_device_properties_t deviceProperties = {
        ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES, nullptr};
    zeDeviceGetProperties(device, &deviceProperties);
    LOG_INFO << "test device name " << deviceProperties.name << " uuid "
             << lzt::to_string(deviceProperties.uuid);
    if (deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE) {
      LOG_INFO << "test subdevice id " << deviceProperties.subdeviceId;
    } else {
      LOG_INFO << "test device is a root device";
    }

    ze_command_queue_handle_t commandQueue = lzt::create_command_queue(device);
    zet_command_list_handle_t commandList = lzt::create_command_list(device);

    auto metricGroupInfo = lzt::get_metric_group_info(
        device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_EVENT_BASED, false, true);
    ASSERT_GT(metricGroupInfo.size(), 0u) << "No query metric groups found";
    metricGroupInfo = lzt::optimize_metric_group_info_list(metricGroupInfo);

    for (auto groupInfo : metricGroupInfo) {

      LOG_INFO << "test metricGroup name " << groupInfo.metricGroupName;
      zet_metric_query_pool_handle_t metric_query_pool_handle =
          lzt::create_metric_query_pool_for_device(
              device, 1000, ZET_METRIC_QUERY_POOL_TYPE_PERFORMANCE,
              groupInfo.metricGroupHandle);
      ASSERT_NE(nullptr, metric_query_pool_handle)
          << "failed to create metric query pool handle";
      zet_metric_query_handle_t metric_query_handle =
          lzt::metric_query_create(metric_query_pool_handle);
      ASSERT_NE(nullptr, metric_query_handle)
          << "failed to create metric query handle";

      lzt::activate_metric_groups(device, 1, &groupInfo.metricGroupHandle);
      lzt::append_metric_query_begin(commandList, metric_query_handle);
      lzt::append_barrier(commandList, nullptr, 0, nullptr);
      ze_event_handle_t eventHandle;
      lzt::zeEventPool eventPool;

      eventPool.create_event(eventHandle, ZE_EVENT_SCOPE_FLAG_HOST,
                             ZE_EVENT_SCOPE_FLAG_HOST);
      void *a_buffer, *b_buffer, *c_buffer;
      ze_group_count_t tg;
      ze_kernel_handle_t function = get_matrix_multiplication_kernel(
          device, &tg, &a_buffer, &b_buffer, &c_buffer);

      zeCommandListAppendLaunchKernel(commandList, function, &tg, nullptr, 0,
                                      nullptr);
      lzt::append_barrier(commandList, nullptr, 0, nullptr);
      lzt::append_metric_query_end(commandList, metric_query_handle,
                                   eventHandle);

      lzt::close_command_list(commandList);
      lzt::execute_command_lists(commandQueue, 1, &commandList, nullptr);

      lzt::synchronize(commandQueue, UINT64_MAX);

      EXPECT_ZE_RESULT_SUCCESS(zeEventQueryStatus(eventHandle));
      std::vector<uint8_t> rawData;

      lzt::metric_query_get_data(metric_query_handle, &rawData);

      eventPool.destroy_event(eventHandle);
      lzt::destroy_metric_query(metric_query_handle);
      lzt::destroy_metric_query_pool(metric_query_pool_handle);

      lzt::deactivate_metric_groups(device);
      lzt::destroy_function(function);
      lzt::free_memory(a_buffer);
      lzt::free_memory(b_buffer);
      lzt::free_memory(c_buffer);

      lzt::reset_command_list(commandList);
    }

    lzt::destroy_command_queue(commandQueue);
    lzt::destroy_command_list(commandList);
    EXPECT_ZE_RESULT_SUCCESS(zetDeviceDisableMetricsExp(device));
  }
}

LZT_TEST_F(
    zetMetricsEnableDisableQueryTest,
    GivenMetricsEnabledByEnvironmentWhenMetricsRuntimeAlsoEnabledThenRuntimeDisableFailsUntilMetricGroupIsDeactivated) {

  for (auto device : devices) {
    ze_device_properties_t deviceProperties = {
        ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES, nullptr};
    zeDeviceGetProperties(device, &deviceProperties);
    LOG_INFO << "test device name " << deviceProperties.name << " uuid "
             << lzt::to_string(deviceProperties.uuid);
    if (deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE) {
      LOG_INFO << "test subdevice id " << deviceProperties.subdeviceId;
    } else {
      LOG_INFO << "test device is a root device";
    }

    ze_command_queue_handle_t commandQueue = lzt::create_command_queue(device);
    zet_command_list_handle_t commandList = lzt::create_command_list(device);

    auto metricGroupInfo = lzt::get_metric_group_info(
        device, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_EVENT_BASED, false, true);
    ASSERT_GT(metricGroupInfo.size(), 0u) << "No query metric groups found";
    metricGroupInfo = lzt::optimize_metric_group_info_list(metricGroupInfo);

    for (auto groupInfo : metricGroupInfo) {
      lzt::enable_metrics_runtime(device);
      LOG_INFO << "test metricGroup name " << groupInfo.metricGroupName;
      zet_metric_query_pool_handle_t metric_query_pool_handle =
          lzt::create_metric_query_pool_for_device(
              device, 1000, ZET_METRIC_QUERY_POOL_TYPE_PERFORMANCE,
              groupInfo.metricGroupHandle);
      ASSERT_NE(nullptr, metric_query_pool_handle)
          << "failed to create metric query pool handle";
      zet_metric_query_handle_t metric_query_handle =
          lzt::metric_query_create(metric_query_pool_handle);
      ASSERT_NE(nullptr, metric_query_handle)
          << "failed to create metric query handle";

      lzt::activate_metric_groups(device, 1, &groupInfo.metricGroupHandle);
      EXPECT_EQ(zetDeviceDisableMetricsExp(device),
                ZE_RESULT_ERROR_HANDLE_OBJECT_IN_USE);
      lzt::append_metric_query_begin(commandList, metric_query_handle);
      EXPECT_EQ(zetDeviceDisableMetricsExp(device),
                ZE_RESULT_ERROR_HANDLE_OBJECT_IN_USE);
      lzt::append_barrier(commandList, nullptr, 0, nullptr);
      ze_event_handle_t eventHandle;
      lzt::zeEventPool eventPool;

      eventPool.create_event(eventHandle, ZE_EVENT_SCOPE_FLAG_HOST,
                             ZE_EVENT_SCOPE_FLAG_HOST);
      void *a_buffer, *b_buffer, *c_buffer;
      ze_group_count_t tg;
      ze_kernel_handle_t function = get_matrix_multiplication_kernel(
          device, &tg, &a_buffer, &b_buffer, &c_buffer);

      zeCommandListAppendLaunchKernel(commandList, function, &tg, nullptr, 0,
                                      nullptr);
      lzt::append_barrier(commandList, nullptr, 0, nullptr);
      lzt::append_metric_query_end(commandList, metric_query_handle,
                                   eventHandle);
      EXPECT_EQ(zetDeviceDisableMetricsExp(device),
                ZE_RESULT_ERROR_HANDLE_OBJECT_IN_USE);

      lzt::close_command_list(commandList);
      lzt::execute_command_lists(commandQueue, 1, &commandList, nullptr);

      lzt::synchronize(commandQueue, UINT64_MAX);

      EXPECT_ZE_RESULT_SUCCESS(zeEventQueryStatus(eventHandle));
      std::vector<uint8_t> rawData;

      lzt::metric_query_get_data(metric_query_handle, &rawData);

      eventPool.destroy_event(eventHandle);
      lzt::destroy_metric_query(metric_query_handle);
      lzt::destroy_metric_query_pool(metric_query_pool_handle);

      lzt::deactivate_metric_groups(device);
      EXPECT_ZE_RESULT_SUCCESS(zetDeviceDisableMetricsExp(device));
      lzt::destroy_function(function);
      lzt::free_memory(a_buffer);
      lzt::free_memory(b_buffer);
      lzt::free_memory(c_buffer);

      lzt::reset_command_list(commandList);
    }

    lzt::destroy_command_queue(commandQueue);
    lzt::destroy_command_list(commandList);
    EXPECT_ZE_RESULT_SUCCESS(zetDeviceDisableMetricsExp(device));
  }
}

} // namespace
