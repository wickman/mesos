/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <pthread.h>

#include <string>
#include <vector>

#include <mesos/hook.hpp>

#include <mesos/module/hook.hpp>

#include <stout/check.hpp>
#include <stout/foreach.hpp>
#include <stout/nothing.hpp>
#include <stout/strings.hpp>
#include <stout/try.hpp>

#include "common/lock.hpp"
#include "hook/manager.hpp"
#include "module/manager.hpp"

using std::string;
using std::vector;

using mesos::modules::ModuleManager;

namespace mesos {
namespace internal {

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static hashmap<string, Hook*> availableHooks;


Try<Nothing> HookManager::initialize(const string& hookList)
{
  Lock lock(&mutex);

  const vector<string> hooks = strings::split(hookList, ",");
  foreach (const string& hook, hooks) {
    if (availableHooks.contains(hook)) {
      return Error("Hook module '" + hook + "' already loaded");
    }

    if (!ModuleManager::contains<Hook>(hook)) {
      return Error("No hook module named '" + hook + "' available");
    }

    // Let's create an instance of the hook module.
    Try<Hook*> module = ModuleManager::create<Hook>(hook);
    if (module.isError()) {
      return Error(
          "Failed to instantiate hook module '" + hook + "': " +
          module.error());
    }

    // Add the hook module to the list of available hooks.
    availableHooks[hook] = module.get();
  }
  return Nothing();
}


Try<Nothing> HookManager::unload(const std::string& hookName)
{
  Lock lock(&mutex);
  if (!availableHooks.contains(hookName)) {
    return Error(
        "Error unloading hook module '" + hookName + "': module not loaded");
  }

  // Now remove the hook from the list of available hooks.
  availableHooks.erase(hookName);
  return Nothing();
}


Labels HookManager::masterLaunchTaskLabelDecorator(
    const TaskInfo& taskInfo,
    const FrameworkInfo& frameworkInfo,
    const SlaveInfo& slaveInfo)
{
  Lock lock(&mutex);
  Labels labels;

  foreachpair (const string& name, Hook* hook, availableHooks) {
    const Result<Labels>& result =
      hook->masterLaunchTaskLabelDecorator(taskInfo, frameworkInfo, slaveInfo);
    if (result.isSome()) {
      labels.MergeFrom(result.get());
    } else if (result.isError()) {
      LOG(WARNING) << "Master label decorator hook failed for module '"
                   << name << "': " << result.error();
    }
  }

  return labels;
}


Environment HookManager::slaveExecutorEnvironmentDecorator(
    ExecutorInfo executorInfo)
{
  Lock lock(&mutex);

  foreachpair (const string& name, Hook* hook, availableHooks) {
    const Result<Environment>& result =
      hook->slaveExecutorEnvironmentDecorator(executorInfo);
    if (result.isSome()) {
      // Update executorInfo to include newer environment variables
      // so that the next hook module can extend the environment
      // variables instead of simply overwriting them.
      executorInfo.mutable_command()->mutable_environment()->MergeFrom(
          result.get());
    } else if (result.isError()) {
      LOG(WARNING) << "Slave environment decorator hook failed for module '"
                   << name << "': " << result.error();
    }
  }

  return executorInfo.command().environment();
}


void HookManager::slaveRemoveExecutorHook(
    const FrameworkInfo& frameworkInfo,
    const ExecutorInfo& executorInfo)
{
  foreachpair (const string& name, Hook* hook, availableHooks) {
    const Try<Nothing>& result =
      hook->slaveRemoveExecutorHook(frameworkInfo, executorInfo);
    if (result.isError()) {
      LOG(WARNING) << "Slave remove executor hook failed for module '"
                   << name << "': " << result.error();
    }
  }
}

} // namespace internal {
} // namespace mesos {
