#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
// #include <abt.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef void (*generic_frame_ptr)(void*);

struct finish_t;

typedef struct task_t {
    void *args;
    generic_frame_ptr _fp;
    struct finish_t* current_finish;

    int tid;
} task_t;

typedef struct trace_node
{
	int tid; // task ID
	int wC; // Worker who created the task
	int wE; // Worker who executed the task
	int SC; // Steal counter
    task_t* task;

    struct trace_node* link;
} trace_node;

/**
 * @brief Spawn a new task asynchronously.
 * @param[in] fct_ptr           The function to execute
 * @param[in] arg               Argument to the async
 */
void hclib_async(generic_frame_ptr fct_ptr, void * arg);
void hclib_finish(generic_frame_ptr fct_ptr, void * arg);
void hclib_kernel(generic_frame_ptr fct_ptr, void * arg);
int hclib_current_worker();
void start_finish();
void end_finish();
int hclib_num_workers();
void hclib_init(int32_t argc, char **argv);
void hclib_finalize();

bool hclib_tracing_enabled();
bool hclib_replay_enabled();
void hclib_set_tracing_enabled(bool enable_tracing);
void hclib_set_replay_enabled(bool enable_replay);

void reset_all_worker_AC_counter();
void reset_worker_AC_counter(int numWorkers);
void reset_all_worker_SC_counter();
void reset_worker_SC_counter(int numWorkers);

void create_array_to_store_stolen_task();
void trace_list_aggregation(trace_node** trace_list, int numWorkers);
void trace_list_aggregation_all();
void trace_list_sorting(trace_node** trace_list, int numWorkers);
void trace_list_sorting_all();

void reset_for_replay();

trace_node** test_set_default_trace_lists();
void test_print_trace_list(trace_node** trace_list, int numWorkers);

#ifdef __cplusplus
}
#endif
