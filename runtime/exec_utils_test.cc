/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "exec_utils.h"

#include <unistd.h>

#include "android-base/stringprintf.h"
#include "base/file_utils.h"
#include "base/memory_tool.h"
#include "common_runtime_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace art {

std::string PrettyArguments(const char* signature);
std::string PrettyReturnType(const char* signature);

bool IsPidfdSupported() {
#ifdef __BIONIC__
  return true;
#else
  constexpr int SYS_pidfd_open = 434;
  int pidfd = syscall(SYS_pidfd_open, getpid(), /*flags=*/0);
  if (pidfd < 0) {
    return false;
  }
  close(pidfd);
  return true;
#endif
}

class ExecUtilsTest : public CommonRuntimeTest {};

TEST_F(ExecUtilsTest, ExecSuccess) {
  std::vector<std::string> command;
  if (kIsTargetBuild) {
    std::string android_root(GetAndroidRoot());
    command.push_back(android_root + "/bin/id");
  } else {
    command.push_back("/usr/bin/id");
  }
  std::string error_msg;
  // Historical note: Running on Valgrind failed due to some memory
  // that leaks in thread alternate signal stacks.
  EXPECT_TRUE(Exec(command, &error_msg));
  EXPECT_EQ(0U, error_msg.size()) << error_msg;
}

TEST_F(ExecUtilsTest, ExecError) {
  std::vector<std::string> command;
  command.push_back("bogus");
  std::string error_msg;
  // Historical note: Running on Valgrind failed due to some memory
  // that leaks in thread alternate signal stacks.
  EXPECT_FALSE(Exec(command, &error_msg));
  EXPECT_FALSE(error_msg.empty());
}

TEST_F(ExecUtilsTest, EnvSnapshotAdditionsAreNotVisible) {
  static constexpr const char* kModifiedVariable = "EXEC_SHOULD_NOT_EXPORT_THIS";
  static constexpr int kOverwrite = 1;
  // Set an variable in the current environment.
  EXPECT_EQ(setenv(kModifiedVariable, "NEVER", kOverwrite), 0);
  // Test that it is not exported.
  std::vector<std::string> command;
  if (kIsTargetBuild) {
    std::string android_root(GetAndroidRoot());
    command.push_back(android_root + "/bin/printenv");
  } else {
    command.push_back("/usr/bin/printenv");
  }
  command.push_back(kModifiedVariable);
  std::string error_msg;
  // Historical note: Running on Valgrind failed due to some memory
  // that leaks in thread alternate signal stacks.
  EXPECT_FALSE(Exec(command, &error_msg));
  EXPECT_NE(0U, error_msg.size()) << error_msg;
}

TEST_F(ExecUtilsTest, EnvSnapshotDeletionsAreNotVisible) {
  static constexpr const char* kDeletedVariable = "PATH";
  static constexpr int kOverwrite = 1;
  // Save the variable's value.
  const char* save_value = getenv(kDeletedVariable);
  EXPECT_NE(save_value, nullptr);
  // Delete the variable.
  EXPECT_EQ(unsetenv(kDeletedVariable), 0);
  // Test that it is not exported.
  std::vector<std::string> command;
  if (kIsTargetBuild) {
    std::string android_root(GetAndroidRoot());
    command.push_back(android_root + "/bin/printenv");
  } else {
    command.push_back("/usr/bin/printenv");
  }
  command.push_back(kDeletedVariable);
  std::string error_msg;
  // Historical note: Running on Valgrind failed due to some memory
  // that leaks in thread alternate signal stacks.
  EXPECT_TRUE(Exec(command, &error_msg));
  EXPECT_EQ(0U, error_msg.size()) << error_msg;
  // Restore the variable's value.
  EXPECT_EQ(setenv(kDeletedVariable, save_value, kOverwrite), 0);
}

static std::vector<std::string> SleepCommand(int sleep_seconds) {
  std::vector<std::string> command;
  if (kIsTargetBuild) {
    command.push_back(GetAndroidRoot() + "/bin/sleep");
  } else {
    command.push_back("/bin/sleep");
  }
  command.push_back(android::base::StringPrintf("%d", sleep_seconds));
  return command;
}

TEST_F(ExecUtilsTest, ExecTimeout) {
  if (!IsPidfdSupported()) {
    GTEST_SKIP() << "pidfd not supported";
  }

  static constexpr int kSleepSeconds = 5;
  static constexpr int kWaitSeconds = 1;
  std::vector<std::string> command = SleepCommand(kSleepSeconds);
  std::string error_msg;
  bool timed_out;
  ASSERT_EQ(ExecAndReturnCode(command, kWaitSeconds, &timed_out, &error_msg), -1);
  EXPECT_TRUE(timed_out);
}

TEST_F(ExecUtilsTest, ExecNoTimeout) {
  if (!IsPidfdSupported()) {
    GTEST_SKIP() << "pidfd not supported";
  }

  static constexpr int kSleepSeconds = 1;
  static constexpr int kWaitSeconds = 5;
  std::vector<std::string> command = SleepCommand(kSleepSeconds);
  std::string error_msg;
  bool timed_out;
  ASSERT_EQ(ExecAndReturnCode(command, kWaitSeconds, &timed_out, &error_msg), 0);
  EXPECT_FALSE(timed_out);
}

TEST_F(ExecUtilsTest, ExecTimeoutNotSupported) {
  if (IsPidfdSupported()) {
    GTEST_SKIP() << "pidfd supported";
  }

  std::string error_msg;
  bool timed_out;
  ASSERT_EQ(ExecAndReturnCode({"command"}, /*timeout_sec=*/0, &timed_out, &error_msg), -1);
  EXPECT_THAT(error_msg, testing::HasSubstr("pidfd_open failed for pid"));
}

}  // namespace art
