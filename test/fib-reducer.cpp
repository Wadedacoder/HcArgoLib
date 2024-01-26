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

using namespace std;

static int threshold = 10;
int result = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int fib_serial(int n) {
    if (n <= 2) return 1;
    return fib_serial(n-1) + fib_serial(n-2);
}

void fib(int n)
{
    if (n <= threshold) {
        int res = fib_serial(n);
	pthread_mutex_lock(&mutex);
	result += res;
	pthread_mutex_unlock(&mutex);
    }
    else {
        hclib::async([=]( ){fib(n-1);});
  	fib(n-2);
    }
}

int main (int argc, char ** argv) {
    hclib::init(argc, argv);
    int n = 40;
    if(argc > 1) n = atoi(argv[1]);
    if(argc > 2) threshold = atoi(argv[2]);

    printf("Starting Fib(%d)..\n",n);
    hclib::kernel([=]() {
        hclib::finish([=]() {
            fib(n);
        });
    });
    printf("Fib(%d) = %d\n",n,result);
    hclib::finalize();
    return 0;
}

