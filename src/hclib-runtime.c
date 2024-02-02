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

#include <stdio.h>

#include <abt.h>
#include "abt_pools_sched.h"
#include "hclib-internal.h"

pthread_key_t selfKey;
pthread_once_t selfKeyInitialized = PTHREAD_ONCE_INIT;

hclib_worker_state* workers;
int * worker_id;
int nb_workers;
int not_done;
static double user_specified_timer = 0;
static double benchmark_start_time_stats = 0;


typedef struct ULT_obj
{
    ABT_thread* thread;
    struct ULT_obj* link;
}ULT_obj;

typedef struct finish_obj
{
    struct ULT_obj* async_list;
    struct finish_obj* link;
}finish_obj;


// Argobots data
ABT_pool *pools;
ABT_xstream *xstreams;
ABT_sched *scheds;

struct finish_obj** finish_objs;
int* worker_ids;
int allocated_workers = 0;

int get_worker_index_from_id(int worker_id)
{
    for(int i = 0; i < allocated_workers; i++)
    {
        if(worker_ids[i] == worker_id)
            return i;
    }
    worker_ids[allocated_workers] = worker_id;
    printf("ALLOCATED NEW WORKER %d at index %d\n", worker_id, allocated_workers);
    allocated_workers++;
    return allocated_workers-1;
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

void setup() {

    int i,j;
    /* Allocate memory. */
    xstreams =
        (ABT_xstream *)malloc(sizeof(ABT_xstream) * nb_workers);
    pools = (ABT_pool *)malloc(sizeof(ABT_pool) * nb_workers);
    scheds = (ABT_sched *)malloc(sizeof(ABT_sched) * nb_workers);

    /* Initialize Argobots. */
    ABT_init(0, NULL);

    /* Create pools. */
    create_pools(nb_workers, pools);
    create_scheds(nb_workers, pools, scheds);

    /* Set up a primary execution stream. */
    ABT_xstream_self(&xstreams[0]);
    ABT_xstream_set_main_sched(xstreams[0], scheds[0]);

    /* Create secondary execution streams. */
    for (i = 1; i < nb_workers; i++) {
        ABT_xstream_create(scheds[i], &xstreams[i]);
    }

    worker_ids = (int*)malloc(nb_workers * sizeof(int));
    finish_objs = (struct finish_obj**)malloc(nb_workers * sizeof(struct finish_obj*));

    // TODO: Most of the below code will be removed after Argobots is fully integrated.

    // Build queues
    // not_done = 1;
    // pthread_once(&selfKeyInitialized, initializeKey);
    // workers = (hclib_worker_state*) malloc(sizeof(hclib_worker_state) * nb_workers);
    // for(int i=0; i<nb_workers; i++) {
    //   workers[i].deque = malloc(sizeof(deque_t));
    //   void * val = NULL;
    //   dequeInit(workers[i].deque, val);
    //   workers[i].current_finish = NULL;
    //   workers[i].id = i;
    // }
    // // Start workers
    // for(int i=1;i<nb_workers;i++) {
    //     pthread_attr_t attr;
    //     pthread_attr_init(&attr);
    //     pthread_create(&workers[i].tid, &attr, &worker_routine, &workers[i].id);
    // }
    // set_current_worker(0);

    // allocate root finish
    start_finish();
}

// void setup() {
//     // Build queues
//     not_done = 1;
//     pthread_once(&selfKeyInitialized, initializeKey);
//     workers = (hclib_worker_state*) malloc(sizeof(hclib_worker_state) * nb_workers);
//     for(int i=0; i<nb_workers; i++) {
//       workers[i].deque = malloc(sizeof(deque_t));
//       void * val = NULL;
//       dequeInit(workers[i].deque, val);
//       workers[i].current_finish = NULL;
//       workers[i].id = i;
//     }
//     // Start workers
//     for(int i=1;i<nb_workers;i++) {
//         pthread_attr_t attr;
//         pthread_attr_init(&attr);
//         pthread_create(&workers[i].tid, &attr, &worker_routine, &workers[i].id);
//     }
//     set_current_worker(0);
//     // allocate root finish
//     start_finish();
// }

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

void hclib_async(generic_frame_ptr fct_ptr, void * arg) {
    // task_t * task = malloc(sizeof(*task));
    // *task = (task_t){
    //     ._fp = fct_ptr,
    //     .args = arg,
    // };
    total_asyncs++;
    int worker_id;
    ABT_xstream_self_rank(&worker_id);
    // printf("WORKER ID: %d\n", worker_id);
    worker_id = get_worker_index_from_id(worker_id);
    ABT_thread new_ult;
    ABT_thread_create(pools[worker_id], fct_ptr, arg, ABT_THREAD_ATTR_NULL, &new_ult);

    // ULT_obj* new_async = (ULT_obj*)malloc(sizeof(ULT_obj));
    // new_async -> thread = &new_ult;
    // new_async -> link = finish_objs[worker_id] -> async_list;
    // finish_objs[worker_id] -> async_list = new_async; 

    ABT_thread_free(&new_ult);
    // Instead of spawn, the task should be sent to the argobots worker
    // spawn(task);
}

// void hclib_async(generic_frame_ptr fct_ptr, void * arg) {
//     task_t * task = malloc(sizeof(*task));
//     *task = (task_t){
//         ._fp = fct_ptr,
//         .args = arg,
//     };
//     spawn(task);
// }


void start_finish() {

    int worker_id;
    ABT_xstream_self_rank(&worker_id);
    printf("WORKER ID: %d\n", worker_id);
    worker_id = get_worker_index_from_id(worker_id);

    // struct ULT_obj* thread_list = NULL;
    struct finish_obj* new_finish = (struct finish_obj*)malloc(sizeof(struct finish_obj));
    new_finish -> async_list = NULL;
    new_finish -> link = finish_objs[worker_id];
    finish_objs[worker_id] = new_finish;

    // TODO: REMOVE
    // int wid = hclib_current_worker();
    // hclib_worker_state* ws = &workers[wid];
    // finish_t * finish = (finish_t*) malloc(sizeof(finish_t));
    // finish->parent = ws->current_finish;
    // check_in_finish(finish->parent);
    // ws->current_finish = finish;
    // finish->counter = 0;
}

// void start_finish() {
//     int wid = hclib_current_worker();
//     hclib_worker_state* ws = &workers[wid];
//     finish_t * finish = (finish_t*) malloc(sizeof(finish_t));
//     finish->parent = ws->current_finish;
//     check_in_finish(finish->parent);
//     ws->current_finish = finish;
//     finish->counter = 0;
// }

void end_finish(){

    int worker_id;
    ABT_xstream_self_rank(&worker_id);
    worker_id = get_worker_index_from_id(worker_id);

    struct ULT_obj* thread_list = finish_objs[worker_id] -> async_list;
    struct ULT_obj* p = thread_list;

    while(p != NULL)
    {
        struct ULT_obj* q = p -> link;
        ABT_thread_free(p -> thread);
        p -> link = NULL;
        free(p);
        p = q;
    }

    struct finish_obj* curr_finish = finish_objs[worker_id];
    finish_objs[worker_id] = finish_objs[worker_id] -> link;
    curr_finish -> link = NULL;
    // delete curr_finish;
    free(curr_finish);

    // TODO: REMOVE
    // int wid = hclib_current_worker();
    // hclib_worker_state* ws = &workers[wid];
    // finish_t* current_finish = ws->current_finish;
    // if (current_finish->counter > 0) {
    //     slave_worker_finishHelper_routine(current_finish);
    // }
    // assert(current_finish->counter == 0);
    // check_out_finish(current_finish->parent); // NULL check in check_out_finish
    // ws->current_finish = current_finish->parent;
    // free(current_finish);
}

void hclib_finalize() {

    int i;

    /* Join secondary execution streams. */
    for (i = 1; i < nb_workers; i++) {
        ABT_xstream_join(xstreams[i]);
        ABT_xstream_free(&xstreams[i]);
    }

    // end_finish();

    /* Finalize Argobots. */
    ABT_finalize();

    /* Free allocated memory. */
    free(xstreams);
    free(pools);
    free(scheds);


    free(finish_objs);
    free(worker_ids);

    // TODO: REMOVE
    // not_done = 0;
    // // int i;
    // int tpush=workers[0].total_push, tsteals=workers[0].total_steals;
    // for(i=1;i< nb_workers; i++) {
    //     pthread_join(workers[i].tid, NULL);
	// tpush+=workers[i].total_push;
	// tsteals+=workers[i].total_steals;
    // }
    double duration = (mysecond() - benchmark_start_time_stats) * 1000;
    printf("============================ Tabulate Statistics ============================\n");
    printf("time.kernel\ttotalAsync\ttotalSteals\n");
    printf("%.3f\t%d\t%d\n",user_specified_timer, total_asyncs, total_steals);
    printf("=============================================================================\n");
    printf("===== Total Time in %.f msec =====\n", duration);
    printf("===== Test PASSED in 0.0 msec =====\n");
}

// void hclib_finalize() {
//     end_finish();
//     not_done = 0;
//     int i;
//     int tpush=workers[0].total_push, tsteals=workers[0].total_steals;
//     for(i=1;i< nb_workers; i++) {
//         pthread_join(workers[i].tid, NULL);
// 	tpush+=workers[i].total_push;
// 	tsteals+=workers[i].total_steals;
//     }
//     double duration = (mysecond() - benchmark_start_time_stats) * 1000;
//     printf("============================ Tabulate Statistics ============================\n");
//     printf("time.kernel\ttotalAsync\ttotalSteals\n");
//     printf("%.3f\t%d\t%d\n",user_specified_timer,tpush,tsteals);
//     printf("=============================================================================\n");
//     printf("===== Total Time in %.f msec =====\n", duration);
//     printf("===== Test PASSED in 0.0 msec =====\n");
// }

void hclib_kernel(generic_frame_ptr fct_ptr, void * arg) {
    double start = mysecond();
    fct_ptr(arg);
    user_specified_timer = (mysecond() - start)*1000;
}

void hclib_finish(generic_frame_ptr fct_ptr, void * arg) {
    // start_finish();
    fct_ptr(arg);
    // end_finish();
}
