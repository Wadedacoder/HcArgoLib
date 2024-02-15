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
#include <stdio.h>

const int NO_REQ = -1;
const task_t* NO_RESP = 1; // Non-null pointer

pthread_key_t selfKey;
pthread_once_t selfKeyInitialized = PTHREAD_ONCE_INIT;

hclib_worker_state* workers;
// bool* worker_status; // 1: excess task
// task_t* transfer_cells
bool dbg_mode;
int * worker_id;
int nb_workers;
int not_done;
static double user_specified_timer = 0;
static double benchmark_start_time_stats = 0;

void dbg_printf() {
   // print all worker status
    for(int i=0; i<nb_workers; i++) {
         printf("Worker %d: status=%d | size=%d | req=%d \n", i, workers[i].status, dequeSize(workers[i].deque), workers[i].request_cell);
    }
}

double mysecond() {
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec + ((double) tv.tv_usec / 1000000);
}

void update_worker_status(int wid) {
    bool b = dequeSize(workers[wid].deque) > 0;
    if(workers[wid].status != b) {
        workers[wid].status = b;
    }
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
        workers[i].status = false;
        workers[i].transfer_cell = NO_RESP;
        workers[i].request_cell = NO_REQ;
    }
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
    dbg_mode = (getenv("HCLIB_DEBUG") != NULL) ? atoi(getenv("HCLIB_DEBUG")) : 0;
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
    dequePush(ws->deque, task);
    update_worker_status(wid);
    ws->total_push++;
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

   while(finish->counter > 0 && not_done) {
       if (dequeEmpty(workers[wid].deque)){
           // try to acquire
            acquire(wid);
        }
        else{
            task_t* task = dequePop(workers[wid].deque);
            update_worker_status(wid);
            communicate(wid);
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

void communicate(int wid){
    int j = workers[wid].request_cell;
    if(j == NO_REQ) return;
    if(dequeSize(workers[wid].deque) <= 2) {
        workers[j].transfer_cell = NULL;
    } else {
        if(dbg_mode){ 
            printf("Worker %d is trying to get robbed\n", wid);
            dbg_printf();
        }
        workers[j].transfer_cell = dequeSteal(workers[wid].deque);
        // workers[j].transfer_cell = dequePop(workers[wid].deque);
        update_worker_status(wid);
        // update_worker_status(j);
    }
    workers[wid].request_cell = NO_REQ;
}

void acquire(int wid) {
    int count = 0;
    while(not_done){
        if(count <= 0 && dbg_mode){ 
            printf("Worker %d is trying to steal\n", wid);
            dbg_printf();
        }
        workers[wid].transfer_cell = NO_RESP;
        // printf("Worker %d is trying to steal\n", wid);
        // check if all workers are idle
        bool all_idle = true;
        for(int i=0; i<nb_workers; i++) {
            if(i != wid && workers[i].status) {
                count = 1;
                all_idle = false;
                break;
            }
        }
        if(all_idle) {
            count = 1;
            break;
        }

        int k = rand() % nb_workers;
        if(k == wid) k = (k+1) % nb_workers;
        if(workers[k].status && hc_cas(&workers[k].request_cell, NO_REQ, wid) && not_done) {
            while(workers[wid].transfer_cell == NO_RESP && not_done) {
                // communicate
                communicate(wid);
            }
            if(workers[wid].transfer_cell && not_done) {
                if(dbg_mode){ 
                    printf("Worker %d has stolen a task from %d\n", wid, k);
                    dbg_printf();
                }
                dequePush(workers[wid].deque, workers[wid].transfer_cell);
                update_worker_status(wid);
                workers[wid].request_cell = NO_REQ;
                workers[wid].total_steals++;
                return; 
            }
        }
        count = 1;
        // printf("Worker %d failed to steal\n", wid);
    }
    
}

void* worker_routine(void * args) {
    int wid = *((int *) args);
   set_current_worker(wid);
   while(not_done) {
        // task_t* task = dequePop(workers[wid].deque);
        if (dequeEmpty(workers[wid].deque)){
           // try to acquire
            acquire(wid);
        }
        else{
            task_t* task = dequePop(workers[wid].deque);
            update_worker_status(wid);
            communicate(wid);
            execute_task(task);
        }
    }
    return NULL;
}

