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

#ifndef __LINUX_ROUTING_QUEUEING_FQ_CODEL_HPP__
#define __LINUX_ROUTING_QUEUEING_FQ_CODEL_HPP__

#include <string>

#include <stout/try.hpp>

#include "linux/routing/queueing/handle.hpp"

namespace routing {
namespace queueing {
namespace fq_codel {

// The handle of the fq_codel queueing discipline is fixed.
extern const Handle HANDLE;


// The default number of flows for the fq_codel queueing discipline.
extern const int DEFAULT_FLOWS;


// Returns true if there exists an fq_codel queueing discipline on the
// egress side of the link.
Try<bool> exists(const std::string& link);


// Creates a new fq_codel queueing discipline on the egress side of
// the link. Returns false if a queueing discipline already exists.
Try<bool> create(const std::string& link);


// Removes the fq_codel queueing discipline from the link. Return
// false if the fq_codel queueing discipline is not found.
Try<bool> remove(const std::string& link);

} // namespace fq_codel {
} // namespace queueing {
} // namespace routing {

#endif // __LINUX_ROUTING_QUEUEING_FQ_CODEL_HPP__
