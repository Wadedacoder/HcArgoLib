/*
 * Copyright 2017 Rice University
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "hclib.hpp"
#include <iostream>

void hclib::start_tracing()
{
  std::cout << "Starting tracing" << std::endl;
	// tracing_enabled = true;
	// reset_worker_AC_counter(numWorkers); // See Lecture #13, Slides #16
	// /* Each worker’s AC value set to (workerID * UINT_MAX/numWorkers) */
	// reset_worker_SC_counters(numWorkers);
}

void hclib::stop_tracing()
{
  std::cout << "Stopping tracing" << std::endl;
	// if(replay_enabled == false)
	// {
	// 	list_aggregation(numWorkers); // See Lecture #13, Slides #35-36
	// 	list_sorting(numWorkers); // See Lecture #13, Slides #37
	// 	create_array_to_store_stolen_task(numWorkers); // See Lecture #13, Slides #39-40
	// 	replay_enabled = true;
	// }
}

int hclib::current_worker() {
    return hclib_current_worker();
}

int hclib::num_workers() {
    return hclib_num_workers();
}

void hclib::init(int argc, char** argv) {
  hclib_init(argc, argv);
}

void hclib::finalize() {
  hclib_finalize();
}
