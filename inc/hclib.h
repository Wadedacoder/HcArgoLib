#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
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
} task_t;

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
void hclib_init(int argc, char **argv);
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
void trace_list_aggregation();
void trace_list_sorting();

#ifdef __cplusplus
}
#endif
