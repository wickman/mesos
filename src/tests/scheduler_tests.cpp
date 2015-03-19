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

#include <gmock/gmock.h>

#include <string>
#include <queue>
#include <vector>

#include <mesos/executor.hpp>
#include <mesos/scheduler.hpp>

#include <process/clock.hpp>
#include <process/future.hpp>
#include <process/gmock.hpp>
#include <process/gtest.hpp>
#include <process/http.hpp>
#include <process/owned.hpp>
#include <process/pid.hpp>
#include <process/queue.hpp>

#include <process/metrics/metrics.hpp>

#include <stout/json.hpp>
#include <stout/lambda.hpp>
#include <stout/memory.hpp>
#include <stout/try.hpp>
#include <stout/uuid.hpp>

#include "master/master.hpp"

#include "tests/containerizer.hpp"
#include "tests/mesos.hpp"

using mesos::internal::master::Master;

using mesos::internal::slave::Containerizer;
using mesos::internal::slave::Slave;

using mesos::scheduler::Call;
using mesos::scheduler::Event;

using process::Clock;
using process::Future;
using process::Owned;
using process::PID;
using process::Promise;
using process::Queue;

using process::http::OK;

using process::metrics::internal::MetricsProcess;

using std::string;
using std::queue;
using std::vector;

using testing::_;
using testing::AtMost;
using testing::DoAll;
using testing::Return;

namespace mesos {
namespace internal {
namespace tests {


class SchedulerTest : public MesosTest
{
protected:
  // Helper class for using EXPECT_CALL since the Mesos scheduler API
  // is callback based.
  class Callbacks
  {
  public:
    MOCK_METHOD0(connected, void(void));
    MOCK_METHOD0(disconnected, void(void));
    MOCK_METHOD1(received, void(const std::queue<Event>&));
  };
};


// Enqueues all received events into a libprocess queue.
ACTION_P(Enqueue, queue)
{
  std::queue<Event> events = arg0;
  while (!events.empty()) {
    queue->put(events.front());
    events.pop();
  }
}


TEST_F(SchedulerTest, TaskRunning)
{
  Try<PID<Master> > master = StartMaster();
  ASSERT_SOME(master);

  MockExecutor exec(DEFAULT_EXECUTOR_ID);

  TestContainerizer containerizer(&exec);

  Try<PID<Slave> > slave = StartSlave(&containerizer);
  ASSERT_SOME(slave);

  Callbacks callbacks;

  Future<Nothing> connected;
  EXPECT_CALL(callbacks, connected())
    .WillOnce(FutureSatisfy(&connected));

  scheduler::Mesos mesos(
      master.get(),
      DEFAULT_CREDENTIAL,
      lambda::bind(&Callbacks::connected, lambda::ref(callbacks)),
      lambda::bind(&Callbacks::disconnected, lambda::ref(callbacks)),
      lambda::bind(&Callbacks::received, lambda::ref(callbacks), lambda::_1));

  AWAIT_READY(connected);

  Queue<Event> events;

  EXPECT_CALL(callbacks, received(_))
    .WillRepeatedly(Enqueue(&events));

  {
    Call call;
    call.mutable_framework_info()->CopyFrom(DEFAULT_FRAMEWORK_INFO);
    call.set_type(Call::REGISTER);

    mesos.send(call);
  }

  Future<Event> event = events.get();
  AWAIT_READY(event);
  EXPECT_EQ(Event::REGISTERED, event.get().type());

  FrameworkID id(event.get().registered().framework_id());

  event = events.get();
  AWAIT_READY(event);
  EXPECT_EQ(Event::OFFERS, event.get().type());
  EXPECT_NE(0, event.get().offers().offers().size());

  EXPECT_CALL(exec, registered(_, _, _, _))
    .Times(1);

  EXPECT_CALL(exec, launchTask(_, _))
    .WillOnce(SendStatusUpdateFromTask(TASK_RUNNING));

  Future<Nothing> update;
  EXPECT_CALL(containerizer, update(_, _))
    .WillOnce(DoAll(FutureSatisfy(&update),
                    Return(Nothing())))
    .WillRepeatedly(Return(Future<Nothing>())); // Ignore subsequent calls.

  TaskInfo taskInfo;
  taskInfo.set_name("");
  taskInfo.mutable_task_id()->set_value("1");
  taskInfo.mutable_slave_id()->CopyFrom(
      event.get().offers().offers(0).slave_id());
  taskInfo.mutable_resources()->CopyFrom(
      event.get().offers().offers(0).resources());
  taskInfo.mutable_executor()->CopyFrom(DEFAULT_EXECUTOR_INFO);

  // TODO(benh): Enable just running a task with a command in the tests:
  //   taskInfo.mutable_command()->set_value("sleep 10");

  {
    Call call;
    call.mutable_framework_info()->CopyFrom(DEFAULT_FRAMEWORK_INFO);
    call.mutable_framework_info()->mutable_id()->CopyFrom(id);
    call.set_type(Call::LAUNCH);
    call.mutable_launch()->add_task_infos()->CopyFrom(taskInfo);
    call.mutable_launch()->add_offer_ids()->CopyFrom(
        event.get().offers().offers(0).id());

    mesos.send(call);
  }

  event = events.get();
  AWAIT_READY(event);
  EXPECT_EQ(Event::UPDATE, event.get().type());
  EXPECT_EQ(TASK_RUNNING, event.get().update().status().state());

  AWAIT_READY(update);

  EXPECT_CALL(exec, shutdown(_))
    .Times(AtMost(1));

  Shutdown(); // Must shutdown before 'containerizer' gets deallocated.
}


// TODO(benh): Write test for sending Call::Acknowledgement through
// master to slave when Event::Update was generated locally.


class MesosSchedulerDriverTest : public MesosTest {};


TEST_F(MesosSchedulerDriverTest, MetricsEndpoint)
{
  Try<PID<Master> > master = StartMaster();
  ASSERT_SOME(master);

  MockScheduler sched;
  MesosSchedulerDriver driver(
      &sched, DEFAULT_FRAMEWORK_INFO, master.get(), DEFAULT_CREDENTIAL);

  Future<Nothing> registered;
  EXPECT_CALL(sched, registered(&driver, _, _))
    .WillOnce(FutureSatisfy(&registered));

  ASSERT_EQ(DRIVER_RUNNING, driver.start());

  AWAIT_READY(registered);

  Future<process::http::Response> response =
    process::http::get(MetricsProcess::instance()->self(), "/snapshot");

  AWAIT_EXPECT_RESPONSE_STATUS_EQ(OK().status, response);

  EXPECT_SOME_EQ(
      "application/json",
      response.get().headers.get("Content-Type"));

  Try<JSON::Object> parse = JSON::parse<JSON::Object>(response.get().body);

  ASSERT_SOME(parse);

  JSON::Object metrics = parse.get();

  EXPECT_EQ(1u, metrics.values.count("scheduler/event_queue_messages"));
  EXPECT_EQ(1u, metrics.values.count("scheduler/event_queue_dispatches"));

  driver.stop();
  driver.join();

  Shutdown();
}


// This action calls driver stop() followed by abort().
ACTION(StopAndAbort)
{
  arg0->stop();
  arg0->abort();
}


// This test verifies that when the scheduler calls stop() before
// abort(), no pending acknowledgements are sent.
TEST_F(MesosSchedulerDriverTest, DropAckIfStopCalledBeforeAbort)
{
  Try<PID<Master> > master = StartMaster();
  ASSERT_SOME(master);

  MockExecutor exec(DEFAULT_EXECUTOR_ID);
  TestContainerizer containerizer(&exec);
  Try<PID<Slave> > slave = StartSlave(&containerizer);
  ASSERT_SOME(slave);

  MockScheduler sched;
  MesosSchedulerDriver driver(
      &sched, DEFAULT_FRAMEWORK_INFO, master.get(), DEFAULT_CREDENTIAL);

  EXPECT_CALL(sched, registered(&driver, _, _));

  EXPECT_CALL(sched, resourceOffers(&driver, _))
    .WillOnce(LaunchTasks(DEFAULT_EXECUTOR_INFO, 1, 1, 16, "*"))
    .WillRepeatedly(Return()); // Ignore subsequent offers.

  // When an update is received, stop the driver and then abort it.
  Future<Nothing> statusUpdate;
  EXPECT_CALL(sched, statusUpdate(&driver, _))
    .WillOnce(DoAll(StopAndAbort(),
                    FutureSatisfy(&statusUpdate)));

  // Ensure no status update acknowledgements are sent from the driver
  // to the master.
  EXPECT_NO_FUTURE_PROTOBUFS(
      StatusUpdateAcknowledgementMessage(), _ , master.get());

  EXPECT_CALL(exec, registered(_, _, _, _));

  EXPECT_CALL(exec, launchTask(_, _))
    .WillOnce(SendStatusUpdateFromTask(TASK_RUNNING));

  EXPECT_CALL(exec, shutdown(_))
    .Times(AtMost(1));

  driver.start();

  AWAIT_READY(statusUpdate);

  // Settle the clock to ensure driver finishes processing the status
  // update and sends acknowledgement if necessary. In this test it
  // shouldn't send an acknowledgement.
  Clock::pause();
  Clock::settle();

  driver.stop();
  driver.join();

  Shutdown();
}


// Ensures that when a scheduler enables explicit acknowledgements
// on the driver, there are no implicit acknowledgements sent, and
// the call to 'acknowledgeStatusUpdate' sends the ack to the master.
TEST_F(MesosSchedulerDriverTest, ExplicitAcknowledgements)
{
  Try<PID<Master> > master = StartMaster();
  ASSERT_SOME(master);

  MockExecutor exec(DEFAULT_EXECUTOR_ID);
  TestContainerizer containerizer(&exec);
  Try<PID<Slave> > slave = StartSlave(&containerizer);
  ASSERT_SOME(slave);

  MockScheduler sched;
  MesosSchedulerDriver driver(
      &sched, DEFAULT_FRAMEWORK_INFO, master.get(), false, DEFAULT_CREDENTIAL);

  EXPECT_CALL(sched, registered(&driver, _, _));

  EXPECT_CALL(sched, resourceOffers(&driver, _))
    .WillOnce(LaunchTasks(DEFAULT_EXECUTOR_INFO, 1, 1, 16, "*"))
    .WillRepeatedly(Return()); // Ignore subsequent offers.

  Future<TaskStatus> status;
  EXPECT_CALL(sched, statusUpdate(&driver, _))
    .WillOnce(FutureArg<1>(&status));

  // Ensure no status update acknowledgements are sent from the driver
  // to the master until the explicit acknowledgement is sent.
  EXPECT_NO_FUTURE_PROTOBUFS(
      StatusUpdateAcknowledgementMessage(), _ , master.get());

  EXPECT_CALL(exec, registered(_, _, _, _));

  EXPECT_CALL(exec, launchTask(_, _))
    .WillOnce(SendStatusUpdateFromTask(TASK_RUNNING));

  EXPECT_CALL(exec, shutdown(_))
    .Times(AtMost(1));

  driver.start();

  AWAIT_READY(status);

  // Settle the clock to ensure driver finishes processing the status
  // update, we want to ensure that no implicit acknowledgement gets
  // sent.
  Clock::pause();
  Clock::settle();

  // Now send the acknowledgement.
  Future<StatusUpdateAcknowledgementMessage> acknowledgement =
    FUTURE_PROTOBUF(StatusUpdateAcknowledgementMessage(), _ , master.get());

  driver.acknowledgeStatusUpdate(status.get());

  AWAIT_READY(acknowledgement);

  driver.stop();
  driver.join();

  Shutdown();
}


// This test ensures that when explicit acknowledgements are enabled,
// acknowledgements for master-generated updates are dropped by the
// driver. We test this by creating an invalid task that uses no
// resources.
TEST_F(MesosSchedulerDriverTest, ExplicitAcknowledgementsMasterGeneratedUpdate)
{
  Try<PID<Master> > master = StartMaster();
  ASSERT_SOME(master);

  Try<PID<Slave> > slave = StartSlave();
  ASSERT_SOME(slave);

  MockScheduler sched;
  MesosSchedulerDriver driver(
      &sched, DEFAULT_FRAMEWORK_INFO, master.get(), false, DEFAULT_CREDENTIAL);

  EXPECT_CALL(sched, registered(&driver, _, _));

  Future<vector<Offer>> offers;
  EXPECT_CALL(sched, resourceOffers(&driver, _))
    .WillOnce(FutureArg<1>(&offers))
    .WillRepeatedly(Return()); // Ignore subsequent offers.

  // Ensure no status update acknowledgements are sent to the master.
  EXPECT_NO_FUTURE_PROTOBUFS(
      StatusUpdateAcknowledgementMessage(), _ , master.get());

  driver.start();

  AWAIT_READY(offers);
  EXPECT_NE(0u, offers.get().size());

  // Launch a task using no resources.
  TaskInfo task;
  task.set_name("");
  task.mutable_task_id()->set_value("1");
  task.mutable_slave_id()->MergeFrom(offers.get()[0].slave_id());
  task.mutable_executor()->MergeFrom(DEFAULT_EXECUTOR_INFO);

  vector<TaskInfo> tasks;
  tasks.push_back(task);

  Future<TaskStatus> status;
  EXPECT_CALL(sched, statusUpdate(&driver, _))
    .WillOnce(FutureArg<1>(&status));

  driver.launchTasks(offers.get()[0].id(), tasks);

  AWAIT_READY(status);
  ASSERT_EQ(TASK_ERROR, status.get().state());
  ASSERT_EQ(TaskStatus::SOURCE_MASTER, status.get().source());
  ASSERT_EQ(TaskStatus::REASON_TASK_INVALID, status.get().reason());

  // Now send the acknowledgement.
  driver.acknowledgeStatusUpdate(status.get());

  // Settle the clock to ensure driver processes the acknowledgement,
  // which should get dropped due to having come from the master.
  Clock::pause();
  Clock::settle();

  driver.stop();
  driver.join();

  Shutdown();
}


// This test ensures that the driver handles an empty slave id
// in an acknowledgement message by dropping it. The driver will
// log an error in this case (but we don't test for that). We
// generate a status with no slave id by performing reconciliation.
TEST_F(MesosSchedulerDriverTest, ExplicitAcknowledgementsUnsetSlaveID)
{
  Try<PID<Master> > master = StartMaster();
  ASSERT_SOME(master);

  MockScheduler sched;
  MesosSchedulerDriver driver(
      &sched, DEFAULT_FRAMEWORK_INFO, master.get(), false, DEFAULT_CREDENTIAL);

  Future<Nothing> registered;
  EXPECT_CALL(sched, registered(&driver, _, _))
    .WillOnce(FutureSatisfy(&registered));

  // Ensure no status update acknowledgements are sent to the master.
  EXPECT_NO_FUTURE_PROTOBUFS(
      StatusUpdateAcknowledgementMessage(), _ , master.get());

  driver.start();

  AWAIT_READY(registered);

  Future<TaskStatus> update;
  EXPECT_CALL(sched, statusUpdate(&driver, _))
    .WillOnce(FutureArg<1>(&update));

  // Peform reconciliation without using a slave id.
  vector<TaskStatus> statuses;

  TaskStatus status;
  status.mutable_task_id()->set_value("foo");
  status.set_state(TASK_RUNNING);

  statuses.push_back(status);

  driver.reconcileTasks(statuses);

  AWAIT_READY(update);
  ASSERT_EQ(TASK_LOST, update.get().state());
  ASSERT_EQ(TaskStatus::SOURCE_MASTER, update.get().source());
  ASSERT_EQ(TaskStatus::REASON_RECONCILIATION, update.get().reason());
  ASSERT_FALSE(update.get().has_slave_id());

  // Now send the acknowledgement.
  driver.acknowledgeStatusUpdate(update.get());

  // Settle the clock to ensure driver processes the acknowledgement,
  // which should get dropped due to the missing slave id.
  Clock::pause();
  Clock::settle();

  driver.stop();
  driver.join();

  Shutdown();
}

} // namespace tests {
} // namespace internal {
} // namespace mesos {
