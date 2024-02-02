/*
Heavily inspired from the example provided in the argobots source code
https://github.com/pmodels/argobots
*/

#include <abt.h>
#include <pthread.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>

typedef struct node node;
typedef struct deque_pool deque;

int total_steals = 0;
int total_asyncs = 0;

struct node{
    struct node *next;
    struct node *prev;
    ABT_thread thread;
};

struct deque_pool {
    pthread_mutex_t mutex; // mutex for the deque whenever we are popping or pushing. TODO replace with atomic operations
    node *head;
    node *tail;
    int size;
};


/*
IN this deque implementation:
Push can only happen at the head
Pop by main thread can only happen at the head
Pop by worker thread can only happen at the tail (called by stealling)
Locking is done by the mutex during pop/steal only
*/

ABT_unit create_node(ABT_pool pool, ABT_thread thread){
    node* n = (node*)malloc(sizeof(node));
    if(!n) return ABT_UNIT_NULL; // memory allocation failed
    n->thread = thread;
    return (ABT_unit)n;
}

void free_node(ABT_pool pool, ABT_unit unit){
    free(unit);
}

ABT_bool is_empty(ABT_pool pool){
    deque* d;
    ABT_pool_get_data(pool, (void**)&d); // get the deque from the pool
    if(d->head == NULL) return ABT_TRUE;
    else return ABT_FALSE;
}

ABT_thread deque_pop(ABT_pool pool, ABT_pool_context context){
    deque* d;
    ABT_pool_get_data(pool, (void**)&d); // get the deque from the pool
    node *n = NULL;
    pthread_mutex_lock(&d->mutex);
    ABT_bool steal = (ABT_bool) (context & ABT_POOL_CONTEXT_OWNER_SECONDARY); // if the caller is not the primary owner he is stealing
    if(d->size == 0){
        pthread_mutex_unlock(&d->mutex);
        return ABT_THREAD_NULL;
    }
    else if(d->head == d->tail){
        n = d->head;
        d->head = NULL;
        d->tail = NULL;
    }
    else if(steal){
        n = d->tail;
        d->tail = d->tail->next;
    }
    else{
        n = d->head;
        d->head = d->head->prev;
    }
    d->size--;
    pthread_mutex_unlock(&d->mutex);
    if(!n) return ABT_THREAD_NULL;
    return n->thread;
}

void deque_push(ABT_pool pool, ABT_unit unit, ABT_pool_context context) {
    deque* d;
    ABT_pool_get_data(pool, (void**)&d); // get the deque from the pool
    node* n = (node*)unit;
    pthread_mutex_lock(&d->mutex);
    ABT_bool pushing_cond = context & (ABT_POOL_CONTEXT_OP_THREAD_CREATE |
                                       ABT_POOL_CONTEXT_OP_THREAD_CREATE_TO |
                                       ABT_POOL_CONTEXT_OP_THREAD_REVIVE |
                                       ABT_POOL_CONTEXT_OP_THREAD_REVIVE_TO); // check if the caller is a thread creation function took reference from example
    if(d->size == 0){
        d->head = n;
        d->tail = n;
    }
    else if(pushing_cond){
        n->prev = d->head;
        d->head->next = n;
        d->head = n;
    } else {
        n->next = d->tail;
        d->tail->prev = n;
        d->tail = n;
    }
    d->size++;
    pthread_mutex_unlock(&d->mutex);
    return;
}


int deque_init(ABT_pool pool, ABT_pool_config config) {
    deque* d = (deque*)calloc(1, sizeof(deque));
    if(!d) return ABT_ERR_MEM; // memory allocation failed
    d->size = 0;

    // initialize mutex
    int ret = pthread_mutex_init(&d->mutex, NULL);
    if(ret != 0){ 
        free(d);
        return ABT_ERR_SYS; // mutex initialization failed
    }

    // Link the pool handle to the deque
    ABT_pool_set_data(pool, (void*)d);

    // TODO : Pool-set error handling

    return ABT_SUCCESS;
    
}

void deque_free(ABT_pool pool){
    deque* d;
    ABT_pool_get_data(pool, (void**)&d); // get the deque from the pool
    pthread_mutex_destroy(&d->mutex);
    free(d);
}



void create_pools(int num_xstreams, ABT_pool* pools)
{
    ABT_pool_user_def pool_def;
    ABT_pool_user_def_create(create_node,
                        free_node,
                        is_empty,
                        deque_pop,
                        deque_push,
                        &pool_def);
    ABT_pool_user_def_set_init(pool_def, deque_init);
    ABT_pool_user_def_set_free(pool_def, deque_free);

    // Configure the pool
    ABT_pool_config config;
    ABT_pool_config_create(&config);
     const int automatic = 1;
    ABT_pool_config_set(config, ABT_pool_config_automatic.key,
                        ABT_pool_config_automatic.type, &automatic);

    int i;
    for(i = 0; i < num_xstreams; i++){
        ABT_pool_create(pool_def, config, &pools[i]);
    }
    ABT_pool_user_def_free(&pool_def);
    ABT_pool_config_free(&config);

}

/*
Scheduler implementation
*/
typedef struct {
    unsigned int freq;
} sched_data;


int sched_init(ABT_sched sched, ABT_sched_config config)
{
    sched_data *data = (sched_data *)malloc(sizeof(sched_data));

    ABT_sched_config_read(config, 1, &data->freq);
    ABT_sched_set_data(sched, (void *)data);

    return ABT_SUCCESS;
}

void sched_run(ABT_sched sched)
{
    unsigned int count = 0;
    sched_data *data;
    int n_pools, target = 1;
    ABT_pool *pools;
    ABT_bool finish;
    time_t seed = time(NULL);
   
    
    ABT_sched_get_data(sched, (void **)&data);
    ABT_sched_get_num_pools(sched, &n_pools);
    pools = (ABT_pool *)malloc(n_pools * sizeof(ABT_pool));
    ABT_sched_get_pools(sched, n_pools, 0, pools);

    
    while (1) {
        ABT_thread thread;
        ABT_pool_pop_thread(pools[0], &thread);
        if (thread != ABT_THREAD_NULL) {
            ABT_self_schedule(thread, ABT_POOL_NULL);
        } else if (n_pools > 1) {
            /* Steal a thread from the target pool */
            if(n_pools > 2) target = rand_r(&seed) % (n_pools - 1) + 1;
            ABT_pool_pop_thread(pools[target], &thread);
            if (thread != ABT_THREAD_NULL) {
                ABT_self_schedule(thread, pools[target]);
            }
        }
        if (++count >= data->freq) {
            count = 0;
            ABT_sched_has_to_stop(sched, &finish);
            if (finish == ABT_TRUE)
                break;
            ABT_xstream_check_events(sched);
        }
    }

    free(pools);
}


int sched_free(ABT_sched sched)
{
    sched_data *data;
    ABT_sched_get_data(sched, (void **)&data); // get the data from the scheduler
    free(data);
    return ABT_SUCCESS;
}

void create_scheds(int num, ABT_pool *pools, ABT_sched *scheds)
{
    ABT_sched_config config;
    ABT_pool *my_pools;

    ABT_sched_config_var cv_freq = { .index = 0,
                                     .type = ABT_SCHED_CONFIG_INT };

    ABT_sched_def sched_def = { .type = ABT_SCHED_TYPE_ULT,
                                .init = sched_init,
                                .run = sched_run,
                                .free = sched_free,
                                .get_migr_pool = NULL };

    ABT_sched_config_create(&config, 
                            cv_freq,
                            4,
                            ABT_sched_config_var_end);

    my_pools = (ABT_pool *)malloc(num * sizeof(ABT_pool));
    for (int i = 0; i < num; i++) {
        for (int k = 0; k < num; k++) {
            my_pools[k] = pools[(i + k) % num];
        }

        ABT_sched_create(&sched_def, num, my_pools, config, &scheds[i]);
    }
    free(my_pools);

    ABT_sched_config_free(&config);
}

