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

int* AC_for_worker;
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
        AC_for_worker[workerID] = workerID * INT_MAX/numWorkers;
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
    if(DEBUGGING){
            // make the SC_for_worker array
        SC_for_worker[0] = 1;
        SC_for_worker[1] = 2;
        SC_for_worker[2] = 3;
    }
    for(int workerID = 0; workerID < nb_workers; workerID++)
    {
        stolen_tasks_array[workerID] = NULL;
        if(SC_for_worker[workerID] > 0){
            stolen_tasks_array[workerID] = (task_t**)malloc(SC_for_worker[workerID] * sizeof(task_t*));
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
    // trace_list = new_trace_list;
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
    test_print_trace_list(trace_list_for_worker, nb_workers);
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
    // change th replay list to the original list
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
        AC_for_worker[workerID] = 0;
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
    AC_for_worker = (int*)malloc(nb_workers * sizeof(int));
    SC_for_worker = (int*)malloc(nb_workers * sizeof(int));
    trace_list_for_worker = (trace_node**)malloc(nb_workers * sizeof(trace_node*));
    for(int worker_id = 0; worker_id < nb_workers; worker_id++)
    {
        trace_list_for_worker[worker_id] = NULL;
    }
    trace_list_for_worker  = test_set_default_trace_lists();

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

void hclib_init(int argc, char **argv) {
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
    if(replay_enabled && (trace_list_for_replay[wid] != NULL && trace_list_for_replay[wid] -> tid == AC_for_worker[wid]))
    {
        // put the task in the stolen_tasks_array of the WE worker
        debugout("Task %d is replayed by worker %d\n", trace_list_for_replay[wid] -> tid, wid);
        int WE = trace_list_for_replay[wid] -> wE;
        int SC = trace_list_for_replay[wid] -> SC;
        debugout("WE: %d, SC: %d\n", WE, SC);
        stolen_tasks_array[WE][SC] = task;
        int tid = trace_list_for_replay[wid] -> tid;
        trace_list_for_replay[wid] = trace_list_for_replay[wid] -> link;
        debugout("Task %d is stolen by worker %d\n", tid, WE);
    }
    else{
        dequePush(ws->deque, task);
    }
    ws->total_push++;
    AC_for_worker[wid]++;
}

void hclib_async(generic_frame_ptr fct_ptr, void * arg) {
    task_t * task = malloc(sizeof(*task));
    *task = (task_t){
        ._fp = fct_ptr,
        .args = arg,
    };
    spawn(task);
}

void slave_worker_finishHelper_routine(finish_t* finish) {
    int wid = hclib_current_worker();
    while(finish->counter > 0) {
       task_t* task = dequePop(workers[wid].deque);
       if (!task) {
            debugout("Worker %d is trying to steal\n", wid);
           // try to steal
           if(replay_enabled){
               if(index_for_stolen_tasks_array[wid] < SC_for_worker[wid]){ // check if the stolen_tasks_array of the WE worker is not full
                   // get the task from the stolen_tasks_array of the WE worker
                   task = stolen_tasks_array[wid][index_for_stolen_tasks_array[wid]];
                   if(task)
                   {
                       index_for_stolen_tasks_array[wid]++;
                          workers[wid].total_steals++;
                          break;
                       debugout("Task is replayed by worker %d\n", wid);
                   }
               }
           }
           else{
                int i = 1;
                while (i < nb_workers && finish->counter > 0) {
                    task = dequeSteal(workers[(wid+i)%(nb_workers)].deque);
	                if(task) {
                        workers[wid].total_steals++;
                        break;
                    }
	                i++;
                }
	        }
        }
        if(task) {
            // printf("Worker %d is executing task\n", task->_fp);
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
                       index_for_stolen_tasks_array[wid]++;
                          workers[wid].total_steals++;
                          SC_for_worker[wid]++;
                       debugout("Task is replayed by worker %d\n", wid);
                   }
               }
           }
           else{
                int i = 1;
                while (i < nb_workers) {
                    task = dequeSteal(workers[(wid+i)%(nb_workers)].deque);
	                if(task) {
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

trace_node** test_set_default_trace_lists()
{
    int num_lists = nb_workers;
    trace_node** default_trace_lists = (trace_node**)malloc(num_lists * sizeof(trace_node*));
    for(int i = 0; i < num_lists; i++)
    {
        default_trace_lists[i] = NULL;
    }
    
    trace_node* p00 = (trace_node*)malloc(sizeof(trace_node));
    trace_node* p10 = (trace_node*)malloc(sizeof(trace_node));
    trace_node* p11 = (trace_node*)malloc(sizeof(trace_node));
    trace_node* p20 = (trace_node*)malloc(sizeof(trace_node));
    trace_node* p21 = (trace_node*)malloc(sizeof(trace_node));
    trace_node* p22 = (trace_node*)malloc(sizeof(trace_node));

    p00 -> tid = 102;
    p00 -> wC = 1;
    p00 -> wE = 0;
    p00 -> SC = 0;
    p00 -> link = NULL;

    p10 -> tid = 10;
    p10 -> wC = 0;
    p10 -> wE = 1;
    p10 -> SC = 0;
    p10 -> link = p11;
    p11 -> tid = 109;
    p11 -> wC = 2;
    p11 -> wE = 1;
    p11 -> SC = 1;
    p11 -> link = NULL;

    p20 -> tid = 101;
    p20 -> wC = 1;
    p20 -> wE = 2;
    p20 -> SC = 0;
    p20 -> link = p21;
    p21 -> tid = 20;
    p21 -> wC = 0;
    p21 -> wE = 2;
    p21 -> SC = 1;
    p21 -> link = p22;
    p22 -> tid = 103;
    p22 -> wC = 1;
    p22 -> wE = 2;
    p22 -> SC = 2;
    p22 -> link = NULL;


    default_trace_lists[0] = p00;
    default_trace_lists[1] = p10;
    default_trace_lists[2] = p20;


    return default_trace_lists;
}

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