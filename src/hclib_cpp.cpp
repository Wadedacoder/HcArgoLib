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

#define DEBUGGING true
#define debugout if(DEBUGGING) std::cout

void hclib::start_tracing()
{
	debugout << "Starting tracing" << std::endl;
	hclib_set_tracing_enabled(true);
	reset_all_worker_AC_counter();
	reset_all_worker_SC_counter();
}

void hclib::stop_tracing()
{
	if(!hclib_replay_enabled())
	{
		trace_list_aggregation_all();
		trace_list_sorting_all();
		create_array_to_store_stolen_task();
		hclib_set_replay_enabled(true);
		debugout << "Replay enabled" << std::endl;
	}
}

void hclib::test_trace_aggregation_sort()
{
	int num_workers = 3;
	printf("*** TESTING LIST AGGREGATION AND SORTING ***\n\n");
	trace_node** default_trace_list = test_set_default_trace_lists();
	printf("INITIAL LISTS:\n");
	test_print_trace_list(default_trace_list, num_workers);

	trace_list_aggregation(default_trace_list, num_workers);
	printf("\nAGGREGATED LISTS:\n");
	test_print_trace_list(default_trace_list, num_workers);

	trace_list_sorting(default_trace_list, num_workers);
	printf("\nSORTED LISTS:\n");
	test_print_trace_list(default_trace_list, num_workers);
	printf("\n");
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
