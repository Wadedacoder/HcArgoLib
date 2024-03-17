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

#include "hclib-internal.h"

#include <limits.h>
#include <stdint.h>

pthread_key_t selfKey;
pthread_once_t selfKeyInitialized = PTHREAD_ONCE_INIT;

hclib_worker_state* workers;
int * worker_id;
int nb_workers;
int not_done;
static double user_specified_timer = 0;
static double benchmark_start_time_stats = 0;

// Trace-Replay Data

trace_node** trace_list_for_worker = NULL;
trace_node** trace_list_for_replay = NULL;
task_t*** stolen_tasks_array = NULL;
int* index_for_stolen_tasks_array;

long long* AC_for_worker;
int* SC_for_worker;

static int tracing_enabled = false;
static int replay_enabled = false;

bool hclib_tracing_enabled()
{
    return tracing_enabled;
}
bool hclib_replay_enabled()
{
    return replay_enabled;
} 
void hclib_set_tracing_enabled(bool enable_tracing)
{
    tracing_enabled = enable_tracing;
}
void hclib_set_replay_enabled(bool enable_replay)
{
    replay_enabled = enable_replay;
}
void reset_worker_AC_counter(int numWorkers)
{
    for(int workerID = 0; workerID < numWorkers; workerID++)
    {
        AC_for_worker[workerID] = workerID * (10000000/numWorkers);
    }
}
void reset_all_worker_AC_counter()
{
    reset_worker_AC_counter(nb_workers);
}

void reset_worker_SC_counter(int numWorkers)
{
    for(int workerID = 0; workerID < numWorkers; workerID++)
    {
        SC_for_worker[workerID] = 0;
    }
}
void reset_all_worker_SC_counter()
{
    reset_worker_SC_counter(nb_workers);
}

void create_array_to_store_stolen_task()
{
    stolen_tasks_array = (task_t***)malloc(nb_workers * sizeof(task_t**));
    for(int workerID = 0; workerID < nb_workers; workerID++)
    {
        stolen_tasks_array[workerID] = NULL;
        if(true){
            stolen_tasks_array[workerID] = (task_t**)malloc((SC_for_worker[workerID]+1) * sizeof(task_t*));
            // set each element to NULL
            for(int i = 0; i < SC_for_worker[workerID]+1; i++)
            {
                stolen_tasks_array[workerID][i] = NULL;
            }
        }
        debugout("Stolen tasks array for worker %d of size %d is created\n", workerID, SC_for_worker[workerID]);
    }
}

void trace_list_aggregation(trace_node** trace_list, int numWorkers)
{
    trace_node** new_trace_list = (trace_node**)malloc(numWorkers*sizeof(trace_node*));
    for(int workerID = 0; workerID < numWorkers; workerID++)
    {
        new_trace_list[workerID] = NULL;
    }

    for(int workerID = 0; workerID < numWorkers; workerID++)
    {
        trace_node* p = trace_list[workerID];
        while(p != NULL)
        {
            trace_node* q = p -> link;
            p -> link = new_trace_list[p -> wC];
            new_trace_list[p -> wC] = p;
            p = q;
        }
    }
    for(int i = 0; i < numWorkers; i++)
        trace_list[i] = new_trace_list[i];
    free(new_trace_list);
}

void trace_list_aggregation_all()
{
    trace_list_aggregation(trace_list_for_worker, nb_workers);
}

void trace_list_sorting(trace_node** trace_list, int numWorkers)
{
    for(int workerID = 0; workerID < numWorkers; workerID++)
    {
        int list_len = 0;
        trace_node* p = trace_list[workerID];
        while(p != NULL)
        {
            list_len++;
            p = p -> link;
        }

        for(int i = 0; i < list_len; i++)
        {
            trace_node* p = trace_list[workerID];
            if(p == NULL)
                continue;
            trace_node* prev = NULL;
            while(p -> link != NULL)
            {
                if(p -> tid > p -> link -> tid)
                {
                    trace_node* q = p -> link;
                    if(prev != NULL)
                        prev -> link = q;
                    else
                        trace_list[workerID] = q;
                    
                    p -> link = p -> link -> link;
                    q -> link = p;
                    prev = q;
                }
                else
                {
                    prev = p;
                    p = p -> link;
                }
            }
        }
    }
}

void trace_list_sorting_all()
{
    trace_list_sorting(trace_list_for_worker, nb_workers);
}

void record_task_stolen_from_victim(int tid, int wC, int wE, int SC, task_t* task)
{
    trace_node* new_trace_node = (trace_node*)malloc(sizeof(trace_node));
    new_trace_node -> tid = tid;
    new_trace_node -> wC = wC;
    new_trace_node -> wE = wE;
    new_trace_node -> SC = SC;
    new_trace_node -> task = task;

    // Insert the new trace node to the list of the execution worker
    new_trace_node -> link = trace_list_for_worker[wE];
    trace_list_for_worker[wE] = new_trace_node;
}

double mysecond() {
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec + ((double) tv.tv_usec / 1000000);
}

// One global finish scope

static void initializeKey() {
    pthread_key_create(&selfKey, NULL);
}

void set_current_worker(int wid) {
    pthread_setspecific(selfKey, &workers[wid].id);
}

int hclib_current_worker() {
    return *((int *) pthread_getspecific(selfKey));
}

int hclib_num_workers() {
    return nb_workers;
}

//FWD declaration for pthread_create
void * worker_routine(void * args);

void reset_for_replay()
{
    // change the replay list to the original list
    for(int workerID = 0; workerID < nb_workers; workerID++)
    {
        trace_list_for_replay[workerID] = trace_list_for_worker[workerID];
        if(trace_list_for_replay[workerID] == NULL) continue;
        debugout("Replay list for worker %d. Starting task: %d\n", workerID, trace_list_for_replay[workerID] -> tid);
    }
    // MAKE index_for_stolen_tasks_array to 0
    for(int workerID = 0; workerID < nb_workers; workerID++)
    {
        index_for_stolen_tasks_array[workerID] = 0;
        reset_all_worker_AC_counter();
        // AC_for_worker[workerID] = workerID * (10000000/nb_workers);
        for(int i = 0; i < SC_for_worker[workerID]+1; i++)
        {
            stolen_tasks_array[workerID][i] = NULL;
        }
    }
}

void setup() {
    // Build queues
    not_done = 1;
    pthread_once(&selfKeyInitialized, initializeKey);
    workers = (hclib_worker_state*) malloc(sizeof(hclib_worker_state) * nb_workers);
    for(int i=0; i<nb_workers; i++) {
      workers[i].deque = malloc(sizeof(deque_t));
      void * val = NULL;
      dequeInit(workers[i].deque, val);
      workers[i].current_finish = NULL;
      workers[i].id = i;
    }

    // Set Trace data
    AC_for_worker = (long long*)malloc(nb_workers * sizeof(long long));
    SC_for_worker = (int*)malloc(nb_workers * sizeof(int));
    trace_list_for_worker = (trace_node**)malloc(nb_workers * sizeof(trace_node*));
    for(int worker_id = 0; worker_id < nb_workers; worker_id++)
    {
        trace_list_for_worker[worker_id] = NULL;
    }

    // Set Replay data
    trace_list_for_replay = (trace_node**)malloc(nb_workers * sizeof(trace_node*));
    for(int worker_id = 0; worker_id < nb_workers; worker_id++)
    {
        trace_list_for_replay[worker_id] = NULL;
    }
    index_for_stolen_tasks_array = (int*)malloc(nb_workers * sizeof(int));
    // Start workers
    for(int i=1;i<nb_workers;i++) {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_create(&workers[i].tid, &attr, &worker_routine, &workers[i].id);
    }
    set_current_worker(0);
    // allocate root finish
    start_finish();
}

void check_in_finish(finish_t * finish) {
    if(finish) hc_atomic_inc(&(finish->counter));
}

void check_out_finish(finish_t * finish) {
    if(finish) hc_atomic_dec(&(finish->counter));
}

void hclib_init(int32_t argc, char **argv) {
    printf("---------HCLIB_RUNTIME_INFO-----------\n");
    printf(">>> HCLIB_WORKERS\t= %s\n", getenv("HCLIB_WORKERS"));
    printf("----------------------------------------\n");
    nb_workers = (getenv("HCLIB_WORKERS") != NULL) ? atoi(getenv("HCLIB_WORKERS")) : 1;
    setup();
    benchmark_start_time_stats = mysecond();
}

void execute_task(task_t * task) {
    finish_t* current_finish = task->current_finish;
    int wid = hclib_current_worker();
    hclib_worker_state* ws = &workers[wid];
    ws->current_finish = current_finish;
    task->_fp((void *)task->args);
    check_out_finish(current_finish);
    free(task);
}

void spawn(task_t * task) {
    // get current worker
    int wid = hclib_current_worker();
    hclib_worker_state* ws = &workers[wid];
    check_in_finish(ws->current_finish);
    task->current_finish = ws->current_finish;
    // push on worker deq
    if(replay_enabled)
    {
        if(trace_list_for_replay[wid] != NULL && trace_list_for_replay[wid] -> tid == task->tid){
        // put the task in the stolen_tasks_array of the WE worker
            int WE = trace_list_for_replay[wid] -> wE;
            int SC = trace_list_for_replay[wid] -> SC;
            stolen_tasks_array[WE][SC] = task;
            int tid = trace_list_for_replay[wid] -> tid;
            trace_list_for_replay[wid] = trace_list_for_replay[wid] -> link;
        }
        else{
            dequePush(ws->deque, task);
        }
    }

    else{
        dequePush(ws->deque, task);
    }
    ws->total_push++;
}

void hclib_async(generic_frame_ptr fct_ptr, void * arg) {
    task_t * task = (task_t*)malloc(sizeof(task_t));
    *task = (task_t){
        ._fp = fct_ptr,
        .args = arg,
    };
    // Add task ID to the new taskafter incrementing the current worker's async counter
    int wid = hclib_current_worker();
    AC_for_worker[wid]++;
    task -> tid = AC_for_worker[wid];
    
    spawn(task);
}
//
void slave_worker_finishHelper_routine(finish_t* finish) {
    int wid = hclib_current_worker();
    while(finish->counter > 0) {
        task_t* task = dequePop(workers[wid].deque);
        if (!task) {
            // try to steal
            if(replay_enabled){
                
                if(index_for_stolen_tasks_array[wid] < SC_for_worker[wid]){ // check if the stolen_tasks_array of the WE worker is not full
                    // get the task from the stolen_tasks_array of the WE worker
                    task = stolen_tasks_array[wid][index_for_stolen_tasks_array[wid]];
                    if(task)
                    {
                        index_for_stolen_tasks_array[wid]++;
                        workers[wid].total_steals++;
                        debugout("Task %d is replayed by worker %d\n", task -> tid, wid);
                    }
                }
            }
            else{
                int i = 1;
                while (i < nb_workers && finish->counter > 0) {
                    int wC = (wid+i)%(nb_workers);
                    task = dequeSteal(workers[wC].deque);
	                if(task) {
                        if(tracing_enabled)
                        {
                            // if the task is stolen successfilly, record it in its trace list
                            record_task_stolen_from_victim(task -> tid, wC, wid, SC_for_worker[wid], task);
                            SC_for_worker[wid]++;
                        }
                        workers[wid].total_steals++;
                        break;
                    }
	                i++;
                }
	        }
        }
        if(task) {
            debugout("Worker %d is executing task %d\n", wid, task -> tid);  
            debugout("The finish counter is %d\n", task -> current_finish -> counter);
            // Change AC_for_worker[wid] to the task's ID
        execute_task(task);
        }
    }
}

void start_finish() {
    int wid = hclib_current_worker();
    hclib_worker_state* ws = &workers[wid];
    finish_t * finish = (finish_t*) malloc(sizeof(finish_t));
    finish->parent = ws->current_finish;
    check_in_finish(finish->parent);
    ws->current_finish = finish;
    finish->counter = 0;
}

void end_finish(){ 
    int wid = hclib_current_worker();
    hclib_worker_state* ws = &workers[wid];
    finish_t* current_finish = ws->current_finish;
    if (current_finish->counter > 0) {
        slave_worker_finishHelper_routine(current_finish);
    }
    assert(current_finish->counter == 0);
    check_out_finish(current_finish->parent); // NULL check in check_out_finish
    ws->current_finish = current_finish->parent;
    free(current_finish);
}

void hclib_finalize() {
    end_finish();
    not_done = 0;
    int i;
    int tpush=workers[0].total_push, tsteals=workers[0].total_steals;
    for(i=1;i< nb_workers; i++) {
        pthread_join(workers[i].tid, NULL);
	tpush+=workers[i].total_push;
	tsteals+=workers[i].total_steals;
    }

    // Free trace data
    free(AC_for_worker);
    free(SC_for_worker);
    free(trace_list_for_worker);
    if(stolen_tasks_array != NULL)
    {
        for(int workerID = 0; workerID < nb_workers; workerID++)
        {
            if(stolen_tasks_array[workerID] != NULL)
                free(stolen_tasks_array[workerID]);
        }
        free(stolen_tasks_array);
    }

    double duration = (mysecond() - benchmark_start_time_stats) * 1000;
    printf("============================ Tabulate Statistics ============================\n");
    printf("time.kernel\ttotalAsync\ttotalSteals\n");
    printf("%.3f\t%d\t%d\n",user_specified_timer,tpush,tsteals);
    printf("=============================================================================\n");
    printf("===== Total Time in %.f msec =====\n", duration);
    printf("===== Test PASSED in 0.0 msec =====\n");
}

void hclib_kernel(generic_frame_ptr fct_ptr, void * arg) {
    double start = mysecond();
    fct_ptr(arg);
    user_specified_timer = (mysecond() - start)*1000;
}

void hclib_finish(generic_frame_ptr fct_ptr, void * arg) {
    start_finish();
    fct_ptr(arg);
    end_finish();
}

void* worker_routine(void * args) {
    int wid = *((int *) args);
    set_current_worker(wid);
    while(not_done) {
        task_t* task = dequePop(workers[wid].deque);
        if (!task) {
            // try to steal
            if(replay_enabled){
                if(index_for_stolen_tasks_array[wid] < SC_for_worker[wid]) // check if the stolen_tasks_array of the WE worker is not full
                {
                    // get the task from the stolen_tasks_array of the WE worker
                    task = stolen_tasks_array[wid][index_for_stolen_tasks_array[wid]];
                    if(task)
                    {
                        hc_atomic_inc(&(index_for_stolen_tasks_array[wid]));
                            workers[wid].total_steals++;
                        debugout("Task is replayed by worker %d\n", wid);
                    }
                }
            }
            else{
                int i = 1;
                while (i < nb_workers) {
                    int wC = (wid+i)%(nb_workers);
                    task = dequeSteal(workers[wC].deque);
	                if(task) {
                        // if the task is stolen successfilly, record it in its trace list
                        record_task_stolen_from_victim(task -> tid, wC, wid, SC_for_worker[wid]++, task);

                        workers[wid].total_steals++;
                        break;
                    }
	                i++;
                }
            }
            if(task) {
                execute_task(task);
            }
	    }
    }
    return NULL;
}


// TEST METHODS: All methods beginning with 'test' are for testing purposes and are not used for execution

void test_print_trace_list(trace_node** trace_list, int numWorkers)
{
    for(int workerID=0; workerID < numWorkers; workerID++)
    {
        printf("W%d: ", workerID);
        trace_node* p = trace_list[workerID];
        while(p != NULL)
        {
            printf("[%d | %d | %d | %d] ---> \t", p -> tid, p -> wC, p -> wE, p -> SC);
            p = p -> link;
        }
        printf("X\n");
    }
}