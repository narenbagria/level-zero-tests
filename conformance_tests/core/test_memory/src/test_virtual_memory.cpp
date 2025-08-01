/*
 *
 * Copyright (C) 2022-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "gtest/gtest.h"

#include "utils/utils.hpp"
#include "test_harness/test_harness.hpp"

#ifdef __linux__
#include <unistd.h>
#endif

namespace lzt = level_zero_tests;

#include <level_zero/ze_api.h>

namespace {

class zeVirtualMemoryTests : public ::testing::Test {
protected:
  void SetUp() override {
    context = lzt::get_default_context();
    device = lzt::zeDevice::get_instance()->get_device();
  }
  void TearDown() override {}

public:
  ze_context_handle_t context;
  ze_device_handle_t device;
  size_t pageSize = 1ul << 21;
  size_t allocationSize = (1024 * 1024);
  void *reservedVirtualMemory = nullptr;
  ze_physical_mem_handle_t reservedPhysicalDeviceMemory = nullptr;
  ze_physical_mem_handle_t reservedPhysicalHostMemory = nullptr;
};

LZT_TEST_F(
    zeVirtualMemoryTests,
    GivenNullStartAddressAndValidSizeTheVirtualMemoryReserveReturnsSuccess) {

  lzt::query_page_size(context, device, allocationSize, &pageSize);
  allocationSize = lzt::create_page_aligned_size(allocationSize, pageSize);
  lzt::virtual_memory_reservation(context, nullptr, allocationSize,
                                  &reservedVirtualMemory);
  EXPECT_NE(nullptr, reservedVirtualMemory);
  lzt::virtual_memory_free(context, reservedVirtualMemory, allocationSize);
}

LZT_TEST_F(
    zeVirtualMemoryTests,
    GivenValidStartAddressAndNewValidSizeThenResizingVirtualReservationSucceeds) {

  size_t largeAllocSize = allocationSize * 4;
  lzt::query_page_size(context, device, largeAllocSize, &pageSize);
  largeAllocSize = lzt::create_page_aligned_size(largeAllocSize, pageSize);
  lzt::virtual_memory_reservation(context, nullptr, largeAllocSize,
                                  &reservedVirtualMemory);
  EXPECT_NE(nullptr, reservedVirtualMemory);
  lzt::virtual_memory_free(context, reservedVirtualMemory, largeAllocSize);
  size_t smallerAllocSize = allocationSize * 2;
  lzt::query_page_size(context, device, smallerAllocSize, &pageSize);
  smallerAllocSize = lzt::create_page_aligned_size(smallerAllocSize, pageSize);
  lzt::virtual_memory_reservation(context, reservedVirtualMemory,
                                  smallerAllocSize, &reservedVirtualMemory);
  EXPECT_NE(nullptr, reservedVirtualMemory);
  lzt::virtual_memory_free(context, reservedVirtualMemory, smallerAllocSize);
}

LZT_TEST_F(
    zeVirtualMemoryTests,
    GivenVirtualReservationWithCustomStartAddressThenResizedPtrAllocated) {

  void *originalPtr = nullptr;
  lzt::query_page_size(context, device, allocationSize, &pageSize);
  allocationSize = lzt::create_page_aligned_size(allocationSize, pageSize);
  lzt::virtual_memory_reservation(context, nullptr, allocationSize,
                                  &originalPtr);
  EXPECT_NE(nullptr, originalPtr);

  void *newUpdatedPtr = reinterpret_cast<void *>(
      reinterpret_cast<size_t>(originalPtr) + allocationSize);
  void *recievedPtr = nullptr;
  lzt::virtual_memory_reservation(context, newUpdatedPtr, allocationSize,
                                  &recievedPtr);
  EXPECT_NE(nullptr, recievedPtr);
  if (recievedPtr != newUpdatedPtr) {
    lzt::virtual_memory_free(context, recievedPtr, allocationSize);
    size_t updatedSize = allocationSize + allocationSize;
    lzt::query_page_size(context, device, updatedSize, &pageSize);
    updatedSize = lzt::create_page_aligned_size(updatedSize, pageSize);
    void *largerPtr = nullptr;
    lzt::virtual_memory_reservation(context, nullptr, updatedSize, &largerPtr);
    EXPECT_NE(nullptr, largerPtr);
    lzt::virtual_memory_free(context, originalPtr, allocationSize);
  }
}

void RunGivenVirtualMemoryReservationThenSettingTheMemoryAccessAttribute(
    zeVirtualMemoryTests &test, bool is_host_memory, bool is_immediate) {
  ze_memory_access_attribute_t access = ZE_MEMORY_ACCESS_ATTRIBUTE_FORCE_UINT32;
  size_t memorySize = 0;
  lzt::query_page_size(test.context, test.device, test.allocationSize,
                       &test.pageSize);
  test.allocationSize =
      lzt::create_page_aligned_size(test.allocationSize, test.pageSize);
  lzt::virtual_memory_reservation(test.context, nullptr, test.allocationSize,
                                  &test.reservedVirtualMemory);
  ze_physical_mem_handle_t reservedPhysicalMemory = nullptr;
  if (is_host_memory) {
    lzt::physical_host_memory_allocation(test.context, test.allocationSize,
                                         &reservedPhysicalMemory);
  } else {
    lzt::physical_device_memory_allocation(test.context, test.device,
                                           test.allocationSize,
                                           &reservedPhysicalMemory);
  }

  EXPECT_NE(nullptr, reservedPhysicalMemory);
  EXPECT_NE(nullptr, test.reservedVirtualMemory);
  lzt::virtual_memory_reservation_get_access(
      test.context, test.reservedVirtualMemory, test.allocationSize, &access,
      &memorySize);
  EXPECT_EQ(access, ZE_MEMORY_ACCESS_ATTRIBUTE_NONE);
  EXPECT_GE(memorySize, test.allocationSize);

  void *memory_in =
      lzt::allocate_shared_memory(test.allocationSize, test.pageSize);
  void *memory_out =
      lzt::allocate_shared_memory(test.allocationSize, test.pageSize);

  const uint32_t zero_pattern = 0;
  const uint32_t input_pattern = 0x99999999;
  const uint32_t output_pattern = 0x66666666;

  auto bundle = lzt::create_command_bundle(test.device, is_immediate);
  lzt::reset_command_list(bundle.list);

  std::vector<ze_memory_access_attribute_t> memoryAccessFlags = {
      ZE_MEMORY_ACCESS_ATTRIBUTE_READONLY, ZE_MEMORY_ACCESS_ATTRIBUTE_READWRITE,
      ZE_MEMORY_ACCESS_ATTRIBUTE_NONE};

  lzt::virtual_memory_map(test.context, test.reservedVirtualMemory,
                          test.allocationSize, reservedPhysicalMemory, 0,
                          ZE_MEMORY_ACCESS_ATTRIBUTE_NONE);

  for (auto accessFlags : memoryAccessFlags) {
    lzt::virtual_memory_reservation_set_access(
        test.context, test.reservedVirtualMemory, test.allocationSize,
        accessFlags);
    lzt::virtual_memory_reservation_get_access(
        test.context, test.reservedVirtualMemory, test.allocationSize, &access,
        &memorySize);
    EXPECT_EQ(accessFlags, access);

    lzt::append_memory_fill(bundle.list, test.reservedVirtualMemory,
                            &zero_pattern, sizeof(zero_pattern),
                            test.allocationSize, nullptr);
    lzt::append_memory_fill(bundle.list, memory_out, &output_pattern,
                            sizeof(output_pattern), test.allocationSize,
                            nullptr);
    lzt::append_memory_fill(bundle.list, memory_in, &input_pattern,
                            sizeof(input_pattern), test.allocationSize,
                            nullptr);
    lzt::append_barrier(bundle.list, nullptr, 0, nullptr);
    lzt::append_memory_copy(bundle.list, test.reservedVirtualMemory, memory_in,
                            test.allocationSize, nullptr);
    lzt::append_barrier(bundle.list, nullptr, 0, nullptr);
    lzt::append_memory_copy(bundle.list, memory_out, test.reservedVirtualMemory,
                            test.allocationSize, nullptr);
    lzt::close_command_list(bundle.list);
    lzt::execute_and_sync_command_bundle(bundle, UINT64_MAX);

    uint32_t output_value = reinterpret_cast<uint32_t *>(memory_out)[0];

    switch (accessFlags) {
    case ZE_MEMORY_ACCESS_ATTRIBUTE_READWRITE:
      EXPECT_EQ(output_value, input_pattern);
      break;
    case ZE_MEMORY_ACCESS_ATTRIBUTE_READONLY:
      EXPECT_EQ(output_value, zero_pattern);
      break;
    default:
      EXPECT_EQ(output_value, ~input_pattern);
      break;
    };
  }

  lzt::virtual_memory_unmap(test.context, test.reservedVirtualMemory,
                            test.allocationSize);

  lzt::free_memory(memory_in);
  lzt::free_memory(memory_out);
  lzt::physical_memory_destroy(test.context, reservedPhysicalMemory);
  lzt::virtual_memory_free(test.context, test.reservedVirtualMemory,
                           test.allocationSize);
}

LZT_TEST_F(
    zeVirtualMemoryTests,
    GivenHostVirtualMemoryReservationThenSettingTheMemoryAccessAttributeReturnsSuccess) {
  RunGivenVirtualMemoryReservationThenSettingTheMemoryAccessAttribute(
      *this, true, false);
}

LZT_TEST_F(
    zeVirtualMemoryTests,
    GivenHostVirtualMemoryReservationThenSettingTheMemoryAccessAttributeOnImmediateCmdListReturnsSuccess) {
  RunGivenVirtualMemoryReservationThenSettingTheMemoryAccessAttribute(
      *this, true, true);
}

LZT_TEST_F(
    zeVirtualMemoryTests,
    GivenDeviceVirtualMemoryReservationThenSettingTheMemoryAccessAttributeReturnsSuccess) {
  RunGivenVirtualMemoryReservationThenSettingTheMemoryAccessAttribute(
      *this, false, false);
}

LZT_TEST_F(
    zeVirtualMemoryTests,
    GivenDeviceVirtualMemoryReservationThenSettingTheMemoryAccessAttributeOnImmediateCmdListReturnsSuccess) {
  RunGivenVirtualMemoryReservationThenSettingTheMemoryAccessAttribute(
      *this, false, true);
}

LZT_TEST_F(
    zeVirtualMemoryTests,
    GivenPageAlignedSizeThenVirtualAndPhysicalMemoryReservedSuccessfully) {
  lzt::query_page_size(context, device, allocationSize, &pageSize);
  allocationSize = lzt::create_page_aligned_size(allocationSize, pageSize);
  lzt::physical_device_memory_allocation(context, device, allocationSize,
                                         &reservedPhysicalDeviceMemory);
  lzt::physical_memory_destroy(context, reservedPhysicalDeviceMemory);
}

LZT_TEST_F(
    zeVirtualMemoryTests,
    GivenPageAlignedSizeThenVirtualAndPhysicalHostMemoryReservedSuccessfully) {
#ifdef __linux__
  const long os_page_size = sysconf(_SC_PAGE_SIZE);
  if (os_page_size > 0) {
    pageSize = static_cast<size_t>(os_page_size);
  }
  allocationSize = lzt::create_page_aligned_size(allocationSize, pageSize);
  lzt::physical_host_memory_allocation(context, allocationSize,
                                       &reservedPhysicalHostMemory);
  lzt::physical_memory_destroy(context, reservedPhysicalHostMemory);
#else
  GTEST_SKIP() << "Physical host memory is unsupported on Windows";
#endif
}

LZT_TEST_F(
    zeVirtualMemoryTests,
    GivenPageAlignedSizeThenPhysicalMemoryisSuccessfullyReservedForAllAccessTypes) {
  lzt::query_page_size(context, device, allocationSize, &pageSize);
  allocationSize = lzt::create_page_aligned_size(allocationSize, pageSize);
  lzt::physical_device_memory_allocation(context, device, allocationSize,
                                         &reservedPhysicalDeviceMemory);
#ifdef __linux__
  lzt::physical_host_memory_allocation(context, allocationSize,
                                       &reservedPhysicalHostMemory);
#endif
  lzt::virtual_memory_reservation(context, nullptr, allocationSize,
                                  &reservedVirtualMemory);
  EXPECT_NE(nullptr, reservedVirtualMemory);

  std::vector<ze_memory_access_attribute_t> memoryAccessFlags = {
      ZE_MEMORY_ACCESS_ATTRIBUTE_NONE, ZE_MEMORY_ACCESS_ATTRIBUTE_READWRITE,
      ZE_MEMORY_ACCESS_ATTRIBUTE_READONLY};

  for (auto accessFlags : memoryAccessFlags) {
    lzt::virtual_memory_map(context, reservedVirtualMemory, allocationSize,
                            reservedPhysicalDeviceMemory, 0, accessFlags);
    lzt::virtual_memory_unmap(context, reservedVirtualMemory, allocationSize);
  }
#ifdef __linux__
  for (auto accessFlags : memoryAccessFlags) {
    lzt::virtual_memory_map(context, reservedVirtualMemory, allocationSize,
                            reservedPhysicalHostMemory, 0, accessFlags);
    lzt::virtual_memory_unmap(context, reservedVirtualMemory, allocationSize);
  }

  lzt::physical_memory_destroy(context, reservedPhysicalHostMemory);
#endif
  lzt::physical_memory_destroy(context, reservedPhysicalDeviceMemory);
  lzt::virtual_memory_free(context, reservedVirtualMemory, allocationSize);
}

void RunGivenMappedReadWriteMemoryThenFillAndCopyWithMappedVirtualMemory(
    zeVirtualMemoryTests &test, bool is_host_memory, bool is_immediate) {
  auto bundle = lzt::create_command_bundle(test.device, is_immediate);

  if (is_host_memory) {
#ifdef __linux__
    const long os_page_size = sysconf(_SC_PAGE_SIZE);
    if (os_page_size > 0) {
      test.pageSize = static_cast<size_t>(os_page_size);
    }
#endif
  } else {
    lzt::query_page_size(test.context, test.device, test.allocationSize,
                         &test.pageSize);
  }

  test.allocationSize =
      lzt::create_page_aligned_size(test.allocationSize, test.pageSize);
  lzt::virtual_memory_reservation(test.context, nullptr, test.allocationSize,
                                  &test.reservedVirtualMemory);

  EXPECT_NE(nullptr, test.reservedVirtualMemory);
  if (is_host_memory) {
    lzt::physical_host_memory_allocation(test.context, test.allocationSize,
                                         &test.reservedPhysicalHostMemory);
    EXPECT_NE(nullptr, test.reservedPhysicalHostMemory);
    ASSERT_ZE_RESULT_SUCCESS(
        zeVirtualMemMap(test.context, test.reservedVirtualMemory,
                        test.allocationSize, test.reservedPhysicalHostMemory, 0,
                        ZE_MEMORY_ACCESS_ATTRIBUTE_READWRITE));
  } else {
    lzt::physical_device_memory_allocation(test.context, test.device,
                                           test.allocationSize,
                                           &test.reservedPhysicalDeviceMemory);
    EXPECT_NE(nullptr, test.reservedPhysicalDeviceMemory);
    ASSERT_ZE_RESULT_SUCCESS(
        zeVirtualMemMap(test.context, test.reservedVirtualMemory,
                        test.allocationSize, test.reservedPhysicalDeviceMemory,
                        0, ZE_MEMORY_ACCESS_ATTRIBUTE_READWRITE));
  }

  int8_t pattern = 9;
  void *memory =
      lzt::allocate_shared_memory(test.allocationSize, test.pageSize);
  lzt::append_memory_fill(bundle.list, test.reservedVirtualMemory, &pattern,
                          sizeof(pattern), test.allocationSize, nullptr);
  lzt::append_barrier(bundle.list, nullptr, 0, nullptr);
  lzt::append_memory_copy(bundle.list, memory, test.reservedVirtualMemory,
                          test.allocationSize, nullptr);
  lzt::close_command_list(bundle.list);
  lzt::execute_and_sync_command_bundle(bundle, UINT64_MAX);
  uint8_t *data = reinterpret_cast<uint8_t *>(memory);
  for (int i = 0; i < test.allocationSize; i++) {
    ASSERT_EQ(data[i], pattern);
  }

  lzt::virtual_memory_unmap(test.context, test.reservedVirtualMemory,
                            test.allocationSize);
  if (is_host_memory) {
    lzt::physical_memory_destroy(test.context, test.reservedPhysicalHostMemory);
  } else {
    lzt::physical_memory_destroy(test.context,
                                 test.reservedPhysicalDeviceMemory);
  }
  lzt::virtual_memory_free(test.context, test.reservedVirtualMemory,
                           test.allocationSize);
  lzt::free_memory(memory);
  lzt::destroy_command_bundle(bundle);
}

LZT_TEST_F(
    zeVirtualMemoryTests,
    GivenMappedReadWriteMemoryThenFillAndCopyWithMappedVirtualMemorySucceeds) {
  RunGivenMappedReadWriteMemoryThenFillAndCopyWithMappedVirtualMemory(
      *this, false, false);
}

LZT_TEST_F(
    zeVirtualMemoryTests,
    GivenMappedReadWriteMemoryThenFillAndCopyWithMappedVirtualMemoryOnImmediateCommandListSucceeds) {
  RunGivenMappedReadWriteMemoryThenFillAndCopyWithMappedVirtualMemory(
      *this, false, true);
}

LZT_TEST_F(
    zeVirtualMemoryTests,
    GivenMappedReadWriteMemoryThenFillAndCopyWithMappedVirtualHostMemorySucceeds) {
#ifdef __linux__
  RunGivenMappedReadWriteMemoryThenFillAndCopyWithMappedVirtualMemory(
      *this, true, false);
#else
  GTEST_SKIP() << "Physical host memory is unsupported on Windows";
#endif
}

LZT_TEST_F(
    zeVirtualMemoryTests,
    GivenMappedReadWriteMemoryThenFillAndCopyWithMappedVirtualHostMemoryOnImmediateCommandListSucceeds) {
#ifdef __linux__
  RunGivenMappedReadWriteMemoryThenFillAndCopyWithMappedVirtualMemory(
      *this, true, true);
#else
  GTEST_SKIP() << "Physical host memory is unsupported on Windows";
#endif
}

void RunGivenMappedMultiplePhysicalMemoryAcrossAvailableDevicesWhenFillAndCopyWithSingleMappedVirtualMemory(
    zeVirtualMemoryTests &test, bool is_immediate) {
  int numDevices = lzt::get_ze_device_count();
  std::vector<ze_device_handle_t> devices;
  std::vector<ze_physical_mem_handle_t> reservedPhysicalMemoryArray;
  devices = lzt::get_ze_devices(numDevices);
  reservedPhysicalMemoryArray.resize(numDevices);
  if (numDevices == 1) {
    reservedPhysicalMemoryArray.resize(2);
    devices.resize(2);
    devices[0] = test.device;
    devices[1] = test.device;
  }
  auto bundle =
      lzt::create_command_bundle(test.context, devices[0], is_immediate);

  lzt::query_page_size(test.context, test.device, 0, &test.pageSize);
  test.allocationSize = test.pageSize;
  test.allocationSize =
      lzt::create_page_aligned_size(test.allocationSize, test.pageSize);
  for (int i = 0; i < devices.size(); i++) {
    lzt::physical_device_memory_allocation(test.context, devices[i],
                                           test.allocationSize,
                                           &reservedPhysicalMemoryArray[i]);
  }

  size_t totalAllocationSize = test.allocationSize * devices.size();
  size_t virtualReservationSize = lzt::nextPowerOfTwo(totalAllocationSize);

  lzt::virtual_memory_reservation(test.context, nullptr, virtualReservationSize,
                                  &test.reservedVirtualMemory);
  EXPECT_NE(nullptr, test.reservedVirtualMemory);

  size_t offset = 0;
  for (int i = 0; i < devices.size(); i++) {
    void *reservedVirtualMemoryOffset = reinterpret_cast<void *>(
        reinterpret_cast<uint64_t>(test.reservedVirtualMemory) + offset);
    ASSERT_ZE_RESULT_SUCCESS(
        zeVirtualMemMap(test.context, reservedVirtualMemoryOffset,
                        test.allocationSize, reservedPhysicalMemoryArray[i], 0,
                        ZE_MEMORY_ACCESS_ATTRIBUTE_READWRITE));
    offset += test.allocationSize;
  }

  int8_t pattern = 9;
  void *memory =
      lzt::allocate_shared_memory(totalAllocationSize, test.pageSize);
  lzt::append_memory_fill(bundle.list, test.reservedVirtualMemory, &pattern,
                          sizeof(pattern), totalAllocationSize, nullptr);
  lzt::append_barrier(bundle.list, nullptr, 0, nullptr);
  lzt::append_memory_copy(bundle.list, memory, test.reservedVirtualMemory,
                          totalAllocationSize, nullptr);
  lzt::close_command_list(bundle.list);
  lzt::execute_and_sync_command_bundle(bundle, UINT64_MAX);
  uint8_t *data = reinterpret_cast<uint8_t *>(memory);
  for (int i = 0; i < totalAllocationSize; i++) {
    ASSERT_EQ(data[i], pattern);
  }
  offset = 0;
  for (int i = 0; i < devices.size(); i++) {
    void *reservedVirtualMemoryOffset = reinterpret_cast<void *>(
        reinterpret_cast<uint64_t>(test.reservedVirtualMemory) + offset);
    lzt::virtual_memory_unmap(test.context, reservedVirtualMemoryOffset,
                              test.allocationSize);
    lzt::physical_memory_destroy(test.context, reservedPhysicalMemoryArray[i]);
    offset += test.allocationSize;
  }
  lzt::virtual_memory_free(test.context, test.reservedVirtualMemory,
                           virtualReservationSize);
  lzt::free_memory(memory);
  lzt::destroy_command_bundle(bundle);
}

LZT_TEST_F(
    zeVirtualMemoryTests,
    GivenMappedMultiplePhysicalMemoryAcrossAvailableDevicesWhenFillAndCopyWithSingleMappedVirtualMemoryThenMemoryCheckSucceeds) {
  RunGivenMappedMultiplePhysicalMemoryAcrossAvailableDevicesWhenFillAndCopyWithSingleMappedVirtualMemory(
      *this, false);
}

LZT_TEST_F(
    zeVirtualMemoryTests,
    GivenMappedMultiplePhysicalMemoryAcrossAvailableDevicesWhenFillAndCopyWithSingleMappedVirtualMemoryOnImmediateCmdListThenMemoryCheckSucceeds) {
  RunGivenMappedMultiplePhysicalMemoryAcrossAvailableDevicesWhenFillAndCopyWithSingleMappedVirtualMemory(
      *this, true);
}

void RunGivenVirtualMemoryMappedToMultipleAllocationsWhenFullAddressUsageInKernel(
    zeVirtualMemoryTests &test, bool is_immediate) {
  int numDevices = lzt::get_ze_device_count();
  std::vector<ze_device_handle_t> devices;
  std::vector<ze_physical_mem_handle_t> reservedPhysicalMemoryArray;
  devices = lzt::get_ze_devices(numDevices);
  reservedPhysicalMemoryArray.resize(numDevices);
  if (numDevices == 1) {
    reservedPhysicalMemoryArray.resize(2);
    devices.resize(2);
    devices[0] = test.device;
    devices[1] = test.device;
  }
  auto bundle =
      lzt::create_command_bundle(test.context, devices[0], is_immediate);

  lzt::query_page_size(test.context, test.device, 0, &test.pageSize);
  test.allocationSize = test.pageSize;
  test.allocationSize =
      lzt::create_page_aligned_size(test.allocationSize, test.pageSize);
  for (int i = 0; i < devices.size(); i++) {
    lzt::physical_device_memory_allocation(test.context, devices[i],
                                           test.allocationSize,
                                           &reservedPhysicalMemoryArray[i]);
  }
  size_t totalAllocationSize = test.allocationSize * devices.size();
  size_t virtualReservationSize = lzt::nextPowerOfTwo(totalAllocationSize);

  lzt::virtual_memory_reservation(test.context, nullptr, virtualReservationSize,
                                  &test.reservedVirtualMemory);
  EXPECT_NE(nullptr, test.reservedVirtualMemory);

  size_t offset = 0;
  for (int i = 0; i < devices.size(); i++) {
    void *reservedVirtualMemoryOffset = reinterpret_cast<void *>(
        reinterpret_cast<uint64_t>(test.reservedVirtualMemory) + offset);
    ASSERT_ZE_RESULT_SUCCESS(
        zeVirtualMemMap(test.context, reservedVirtualMemoryOffset,
                        test.allocationSize, reservedPhysicalMemoryArray[i], 0,
                        ZE_MEMORY_ACCESS_ATTRIBUTE_READWRITE));
    offset += test.allocationSize;
  }
  void *memory =
      lzt::allocate_shared_memory(totalAllocationSize, test.pageSize);
  lzt::write_data_pattern(memory, totalAllocationSize, 1);
  std::string module_name = "write_memory_pattern.spv";
  ze_module_handle_t module = lzt::create_module(
      lzt::zeDevice::get_instance()->get_device(), module_name);
  std::string func_name = "write_memory_pattern";
  lzt::FunctionArg arg;
  std::vector<lzt::FunctionArg> args;

  arg.arg_size = sizeof(uint8_t *);
  arg.arg_value = &test.reservedVirtualMemory;
  args.push_back(arg);
  arg.arg_size = sizeof(int);
  int size = static_cast<int>(totalAllocationSize);
  arg.arg_value = &size;
  args.push_back(arg);

  ze_kernel_handle_t function = lzt::create_function(module, func_name);
  uint32_t group_size_x = 1;
  uint32_t group_size_y = 1;
  uint32_t group_size_z = 1;
  EXPECT_ZE_RESULT_SUCCESS(zeKernelSuggestGroupSize(
      function, 1, 1, 1, &group_size_x, &group_size_y, &group_size_z));

  EXPECT_ZE_RESULT_SUCCESS(
      zeKernelSetGroupSize(function, group_size_x, group_size_y, group_size_z));

  int i = 0;
  for (auto arg : args) {
    EXPECT_ZE_RESULT_SUCCESS(
        zeKernelSetArgumentValue(function, i++, arg.arg_size, arg.arg_value));
  }

  ze_group_count_t thread_group_dimensions;
  thread_group_dimensions.groupCountX = 1;
  thread_group_dimensions.groupCountY = 1;
  thread_group_dimensions.groupCountZ = 1;

  uint8_t pattern = 1;
  lzt::append_memory_fill(bundle.list, test.reservedVirtualMemory, &pattern,
                          sizeof(pattern), totalAllocationSize, nullptr);
  lzt::append_barrier(bundle.list, nullptr, 0, nullptr);

  EXPECT_ZE_RESULT_SUCCESS(zeCommandListAppendLaunchKernel(
      bundle.list, function, &thread_group_dimensions, nullptr, 0, nullptr));

  EXPECT_ZE_RESULT_SUCCESS(
      zeCommandListAppendBarrier(bundle.list, nullptr, 0, nullptr));

  lzt::append_memory_copy(bundle.list, memory, test.reservedVirtualMemory,
                          totalAllocationSize, nullptr);

  lzt::close_command_list(bundle.list);
  lzt::execute_and_sync_command_bundle(bundle, UINT64_MAX);

  lzt::validate_data_pattern(memory, totalAllocationSize, -1);

  lzt::destroy_function(function);
  lzt::destroy_module(module);
  offset = 0;
  for (int i = 0; i < devices.size(); i++) {
    void *reservedVirtualMemoryOffset = reinterpret_cast<void *>(
        reinterpret_cast<uint64_t>(test.reservedVirtualMemory) + offset);
    lzt::virtual_memory_unmap(test.context, reservedVirtualMemoryOffset,
                              test.allocationSize);
    lzt::physical_memory_destroy(test.context, reservedPhysicalMemoryArray[i]);
    offset += test.allocationSize;
  }
  lzt::virtual_memory_free(test.context, test.reservedVirtualMemory,
                           virtualReservationSize);
  lzt::free_memory(memory);
  lzt::destroy_command_bundle(bundle);
}

LZT_TEST_F(
    zeVirtualMemoryTests,
    GivenVirtualMemoryMappedToMultipleAllocationsWhenFullAddressUsageInKernelThenResultsinValidData) {
  RunGivenVirtualMemoryMappedToMultipleAllocationsWhenFullAddressUsageInKernel(
      *this, false);
}

LZT_TEST_F(
    zeVirtualMemoryTests,
    GivenVirtualMemoryMappedToMultipleAllocationsWhenFullAddressUsageInKernelOnImmediateCmdListThenResultsinValidData) {
  RunGivenVirtualMemoryMappedToMultipleAllocationsWhenFullAddressUsageInKernel(
      *this, true);
}

enum MemoryReservationTestType {
  MEMORY_RESERVATION_SINGLE_DEVICE,
  MEMORY_RESERVATION_SINGLE_ROOT_DEVICE_MULTI_SUB_DEVICES,
  MEMORY_RESERVATION_MULTI_ROOT_DEVICES
};

void dataCheckMemoryReservations(enum MemoryReservationTestType type,
                                 bool is_immediate) {
  ze_context_handle_t context = lzt::get_default_context();
  ze_device_handle_t rootDevice = lzt::zeDevice::get_instance()->get_device();
  std::vector<ze_device_handle_t> devices;
  size_t pageSize = 0;
  void *reservedVirtualMemory = nullptr;
  std::vector<ze_physical_mem_handle_t> reservedPhysicalMemory;
  size_t allocationSize = (1024 * 1024);
  uint32_t numDevices, numSubDevices;

  switch (type) {
  case MemoryReservationTestType::MEMORY_RESERVATION_MULTI_ROOT_DEVICES:
    numDevices = lzt::get_ze_device_count();
    if (numDevices < 2) {
      GTEST_SKIP() << "Multi Root Devices not found, skipping test";
    }
    allocationSize = allocationSize * numDevices;
    devices = lzt::get_ze_devices(numDevices);
    reservedPhysicalMemory.resize(numDevices);
    break;
  case MemoryReservationTestType::
      MEMORY_RESERVATION_SINGLE_ROOT_DEVICE_MULTI_SUB_DEVICES:
    numSubDevices = lzt::get_ze_sub_device_count(rootDevice);
    if (numSubDevices < 2) {
      GTEST_SKIP() << "Multi Sub Devices not found, skipping test";
    }
    allocationSize = allocationSize * numSubDevices;
    devices = lzt::get_ze_sub_devices(rootDevice);
    reservedPhysicalMemory.resize(numSubDevices);
    break;
  case MemoryReservationTestType::MEMORY_RESERVATION_SINGLE_DEVICE:
    reservedPhysicalMemory.resize(2);
    devices.resize(2);
    devices[0] = rootDevice;
    devices[1] = rootDevice;
    break;
  default:
    FAIL() << "Invalid Memory Reservation Test Type";
  }

  auto bundle = lzt::create_command_bundle(rootDevice, is_immediate);

  lzt::query_page_size(context, rootDevice, allocationSize, &pageSize);
  allocationSize = lzt::create_page_aligned_size(allocationSize, pageSize);
  for (int i = 0; i < devices.size(); i++) {
    lzt::physical_device_memory_allocation(context, devices[i], allocationSize,
                                           &reservedPhysicalMemory[i]);
  }
  size_t virtualReservationSize =
      lzt::nextPowerOfTwo(allocationSize * devices.size());
  lzt::virtual_memory_reservation(context, nullptr, virtualReservationSize,
                                  &reservedVirtualMemory);
  EXPECT_NE(nullptr, reservedVirtualMemory);

  size_t offset = 0;
  for (int i = 0; i < devices.size(); i++) {
    uint64_t offsetAddr =
        reinterpret_cast<uint64_t>(reservedVirtualMemory) + offset;
    ASSERT_ZE_RESULT_SUCCESS(zeVirtualMemMap(
        context, reinterpret_cast<void *>(offsetAddr), allocationSize,
        reservedPhysicalMemory[i], 0, ZE_MEMORY_ACCESS_ATTRIBUTE_READWRITE));
    offset += allocationSize;
  }

  int8_t pattern = 9;
  void *memory =
      lzt::allocate_host_memory(allocationSize * devices.size(), pageSize);

  offset = 0;
  for (int i = 0; i < devices.size(); i++) {
    uint64_t offsetAddr =
        reinterpret_cast<uint64_t>(reservedVirtualMemory) + offset;
    lzt::append_memory_fill(bundle.list, reinterpret_cast<void *>(offsetAddr),
                            &pattern, sizeof(pattern), allocationSize, nullptr);
    offset += allocationSize;
  }

  lzt::append_barrier(bundle.list, nullptr, 0, nullptr);

  offset = 0;
  for (int i = 0; i < devices.size(); i++) {
    uint64_t offsetAddr =
        reinterpret_cast<uint64_t>(reservedVirtualMemory) + offset;
    uint64_t offsetHostAddr = reinterpret_cast<uint64_t>(memory) + offset;
    lzt::append_memory_copy(
        bundle.list, reinterpret_cast<void *>(offsetHostAddr),
        reinterpret_cast<void *>(offsetAddr), allocationSize, nullptr);
    offset += allocationSize;
  }

  lzt::close_command_list(bundle.list);
  lzt::execute_and_sync_command_bundle(bundle, UINT64_MAX);
  uint8_t *data = reinterpret_cast<uint8_t *>(memory);
  for (int i = 0; i < allocationSize * devices.size(); i++) {
    ASSERT_EQ(data[i], pattern);
  }

  lzt::virtual_memory_unmap(context, reservedVirtualMemory, allocationSize);

  offset = 0;
  for (int i = 0; i < devices.size(); i++) {
    uint64_t offsetAddr =
        reinterpret_cast<uint64_t>(reservedVirtualMemory) + offset;
    lzt::virtual_memory_unmap(context, reinterpret_cast<void *>(offsetAddr),
                              allocationSize);
    lzt::physical_memory_destroy(context, reservedPhysicalMemory[i]);
    offset += allocationSize;
  }

  lzt::virtual_memory_free(context, reservedVirtualMemory,
                           virtualReservationSize);
  lzt::free_memory(memory);
  lzt::destroy_command_bundle(bundle);
}

LZT_TEST_F(
    zeVirtualMemoryTests,
    GivenMultiMappedReadWriteMemoryOnOneDeviceThenFillAndCopyWithMappedVirtualMemorySucceeds) {
  dataCheckMemoryReservations(
      MemoryReservationTestType::MEMORY_RESERVATION_SINGLE_DEVICE, false);
}

LZT_TEST_F(
    zeVirtualMemoryTests,
    GivenMultiMappedReadWriteMemoryOnOneDeviceThenFillAndCopyWithMappedVirtualMemoryOnImmediateCmdListSucceeds) {
  dataCheckMemoryReservations(
      MemoryReservationTestType::MEMORY_RESERVATION_SINGLE_DEVICE, true);
}

LZT_TEST_F(
    zeVirtualMemoryTests,
    GivenMultiMappedReadWriteMemoryOnSingleRootDeviceButAcrossSubDevicesThenFillAndCopyWithMappedVirtualMemorySucceeds) {
  dataCheckMemoryReservations(
      MemoryReservationTestType::
          MEMORY_RESERVATION_SINGLE_ROOT_DEVICE_MULTI_SUB_DEVICES,
      false);
}

LZT_TEST_F(
    zeVirtualMemoryTests,
    GivenMultiMappedReadWriteMemoryOnSingleRootDeviceButAcrossSubDevicesThenFillAndCopyWithMappedVirtualMemoryOnImmediateCmdListSucceeds) {
  dataCheckMemoryReservations(
      MemoryReservationTestType::
          MEMORY_RESERVATION_SINGLE_ROOT_DEVICE_MULTI_SUB_DEVICES,
      true);
}

LZT_TEST_F(
    zeVirtualMemoryTests,
    GivenMultiMappedReadWriteMemoryOnMultipleRootDevicesThenFillAndCopyWithMappedVirtualMemorySucceeds) {
  dataCheckMemoryReservations(
      MemoryReservationTestType::MEMORY_RESERVATION_MULTI_ROOT_DEVICES, false);
}
LZT_TEST_F(
    zeVirtualMemoryTests,
    GivenMultiMappedReadWriteMemoryOnMultipleRootDevicesThenFillAndCopyWithMappedVirtualMemoryOnImmediateCmdListSucceeds) {
  dataCheckMemoryReservations(
      MemoryReservationTestType::MEMORY_RESERVATION_MULTI_ROOT_DEVICES, true);
}

class zeVirtualMemoryMultiMappingTests
    : public ::testing::Test,
      public ::testing::WithParamInterface<std::tuple<ze_memory_type_t, bool>> {
protected:
  void SetUp() override {
    device = lzt::get_default_device(lzt::get_default_driver());
    context = lzt::get_default_context();
  }
  void TearDown() override {}

public:
  ze_context_handle_t context = nullptr;
  ze_device_handle_t device = nullptr;
};

LZT_TEST_P(
    zeVirtualMemoryMultiMappingTests,
    givenSinglePhysicalHostMemoryMappedToMultipleVirtualMemoryRangeThenReadAndWriteResultsAreCorrect) {
#ifdef __linux__
  const ze_memory_type_t aux_buffer_type = std::get<0>(GetParam());
  const bool is_immediate = std::get<1>(GetParam());

  constexpr size_t alloc_size = 1ul << 26;

  void *aux_buffer = nullptr;
  switch (aux_buffer_type) {
  case ZE_MEMORY_TYPE_HOST:
    aux_buffer = lzt::allocate_host_memory(alloc_size, sizeof(int64_t));
    break;
  case ZE_MEMORY_TYPE_DEVICE:
    aux_buffer = lzt::allocate_device_memory(alloc_size, sizeof(int64_t));
    break;
  default:
    aux_buffer = lzt::allocate_shared_memory(alloc_size, sizeof(int64_t));
    break;
  }
  EXPECT_NE(nullptr, aux_buffer);

  ze_physical_mem_handle_t physical_host_memory = nullptr;
  lzt::physical_host_memory_allocation(context, alloc_size,
                                       &physical_host_memory);
  EXPECT_NE(nullptr, physical_host_memory);

  void *virtual_memory_0 = nullptr;
  void *virtual_memory_1 = nullptr;
  void *virtual_memory_2 = nullptr;
  lzt::virtual_memory_reservation(context, nullptr, alloc_size,
                                  &virtual_memory_0);
  lzt::virtual_memory_reservation(context, nullptr, alloc_size,
                                  &virtual_memory_1);
  lzt::virtual_memory_reservation(context, nullptr, alloc_size,
                                  &virtual_memory_2);
  EXPECT_NE(nullptr, virtual_memory_0);
  EXPECT_NE(nullptr, virtual_memory_1);
  EXPECT_NE(nullptr, virtual_memory_2);
  EXPECT_NE(virtual_memory_0, virtual_memory_1);
  EXPECT_NE(virtual_memory_0, virtual_memory_2);
  EXPECT_NE(virtual_memory_1, virtual_memory_2);

  lzt::virtual_memory_map(context, virtual_memory_0, alloc_size,
                          physical_host_memory, 0,
                          ZE_MEMORY_ACCESS_ATTRIBUTE_READWRITE);
  lzt::virtual_memory_map(context, virtual_memory_1, alloc_size,
                          physical_host_memory, 0,
                          ZE_MEMORY_ACCESS_ATTRIBUTE_READWRITE);

  std::fill_n(static_cast<uint8_t *>(virtual_memory_0), alloc_size, 0);
  std::fill_n(static_cast<uint8_t *>(virtual_memory_1), alloc_size, 0);

  // Simple read-write test with cross check
  static_cast<int64_t *>(virtual_memory_0)[(alloc_size / sizeof(int64_t)) / 2] =
      0xdeadbeef;
  EXPECT_EQ(0xdeadbeef,
            static_cast<int64_t *>(
                virtual_memory_1)[(alloc_size / sizeof(int64_t)) / 2]);

  static_cast<int64_t *>(virtual_memory_1)[(alloc_size / sizeof(int64_t)) / 3] =
      0xcafecafe;
  EXPECT_EQ(0xcafecafe,
            static_cast<int64_t *>(
                virtual_memory_0)[(alloc_size / sizeof(int64_t)) / 3]);

  // GPU copy test with cross check
  int8_t seven = 7;
  auto bundle = lzt::create_command_bundle(device, is_immediate);
  lzt::append_memory_fill(bundle.list, aux_buffer, &seven, sizeof(seven),
                          alloc_size, nullptr);
  lzt::append_barrier(bundle.list, nullptr, 0, nullptr);
  lzt::append_memory_copy(bundle.list, virtual_memory_0, aux_buffer, alloc_size,
                          nullptr, 0, nullptr);
  lzt::close_command_list(bundle.list);
  lzt::execute_and_sync_command_bundle(bundle, UINT64_MAX);
  lzt::destroy_command_bundle(bundle);

  for (size_t i = 0; i < alloc_size; i++) {
    if (static_cast<int8_t *>(virtual_memory_1)[i] != seven) {
      FAIL() << "Verification failed";
      break;
    }
  }

  lzt::virtual_memory_unmap(context, virtual_memory_0, alloc_size);
  lzt::virtual_memory_free(context, virtual_memory_0, alloc_size);

  lzt::virtual_memory_unmap(context, virtual_memory_1, alloc_size);
  lzt::virtual_memory_free(context, virtual_memory_1, alloc_size);

  // Make sure data in physical host memory is persistent
  lzt::virtual_memory_map(context, virtual_memory_2, alloc_size,
                          physical_host_memory, 0,
                          ZE_MEMORY_ACCESS_ATTRIBUTE_READONLY);
  for (size_t i = 0; i < alloc_size; i++) {
    if (static_cast<int8_t *>(virtual_memory_2)[i] != seven) {
      FAIL() << "Verification failed";
      break;
    }
  }
  lzt::virtual_memory_unmap(context, virtual_memory_2, alloc_size);
  lzt::virtual_memory_free(context, virtual_memory_2, alloc_size);

  lzt::physical_memory_destroy(context, physical_host_memory);

  lzt::free_memory(context, aux_buffer);
#else
  GTEST_SKIP() << "Physical host memory is unsupported on Windows";
#endif
}

INSTANTIATE_TEST_SUITE_P(
    VirtualHostMemoryMultiMappingParams, zeVirtualMemoryMultiMappingTests,
    ::testing::Combine(::testing::Values(ZE_MEMORY_TYPE_HOST,
                                         ZE_MEMORY_TYPE_DEVICE,
                                         ZE_MEMORY_TYPE_SHARED),
                       ::testing::Bool()));

} // namespace
