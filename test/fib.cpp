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

int fib_serial(int n) {
    if (n <= 2) return 1;
    return fib_serial(n-1) + fib_serial(n-2);
}

int fib(int n)
{
    if (n <= threshold) {
        return fib_serial(n);
    }
    else {
	int x, y;
	hclib::finish([&]() {
  	    hclib::async([&](){x = fib(n-1);});
  	    y = fib(n-2);
	});
	return x + y;
    }
}

int main (int argc, char ** argv) {
    hclib::init(argc, argv);
    int n = 15, res;
    if(argc > 1) n = atoi(argv[1]);
    if(argc > 2) threshold = atoi(argv[2]);

    printf("Starting Fib(%d)..\n",n);
    hclib::kernel([&]() {
      res = fib(n);
    });
    printf("Fib(%d) = %d\n",n,res);
    hclib::finalize();
    return 0;
}

