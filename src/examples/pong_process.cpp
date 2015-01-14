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

#include <process/process.hpp>

using namespace process;

using namespace process::http;

using std::string;

class PongProcess : public Process<PongProcess>
{
public:
  PongProcess() {}
  virtual ~PongProcess() {}

  virtual void initialize()
  {
    install("ping", &PongProcess::ping);
  }

private:
  void ping(const UPID& from, const string& body)
  {
    static const string message("hi");
    send(from, "pong", message.c_str(), message.size());
  }
};


int main(int argc, char** argv)
{
  PongProcess process;
  PID<PongProcess> pid = spawn(&process);
  wait(pid);
  return 0;
}
