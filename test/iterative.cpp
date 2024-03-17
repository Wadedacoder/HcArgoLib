#include<iostream>
#include<algorithm>
#include<stdio.h>
#include<string.h>
#include<cmath>
#include<sys/time.h>

#include "hclib.hpp"

/*
 * Ported from HJlib
 *
 * Author: Vivek Kumar
 *
 */

//48 * 256 * 2048
#define SIZE 25165824
// #define SIZE 100
#define ITERATIONS 5

double* myNew, *myVal;
int n;

long get_usecs () {
  struct timeval t;
  gettimeofday(&t,NULL);
  return t.tv_sec*1000000+t.tv_usec;
}
 
int ceilDiv(int d) {
  int m = SIZE / d;
  if (m * d == SIZE) {
    return m;
  } else {
    return (m + 1);
  }
}

void recurse(uint64_t low, uint64_t high) {
  if((high - low) > 512) {
    uint64_t mid = (high+low)/2;
    // /* An async task */ recurse(low, mid);
    hclib::finish([&]() {
    hclib::async([&]( ){recurse(low, mid);});  
    recurse(mid, high);
    });
;
  } else {
    // std::cout << "low: " << low << " high: " << high << std::endl;
    for(uint64_t j=low; j<high; j++) {
      myNew[j] = (myVal[j - 1] + myVal[j + 1]) / 2.0;
    //   std::cout << "myNew[" << j << "]: " << myNew[j] << std::endl;
    }
  }
}

void runParallel() {
    hclib::start_tracing();
    hclib::finish([&]() {
    recurse(1, SIZE+1);
    });
    hclib::stop_tracing();
    double* temp = myNew;
    myNew = myVal;
    myVal = temp;
  for(int i=1; i<ITERATIONS; i++) {
    // hclib::start_tracing();
    hclib::reset_replay();
    hclib::finish([&]() {
    recurse(1, SIZE+1);
    });
    // hclib::stop_tracing();
    double* temp = myNew;
    myNew = myVal;
    myVal = temp;
    
  }
}

int main (int argc, char ** argv) {
    hclib::init(argc, argv);
    // TODO: REMOVE the below test line
    // hclib::test_trace_aggregation_sort();

    myNew = new double[(SIZE + 2)];
    myVal = new double[(SIZE + 2)];
    memset(myNew, 0, sizeof(double) * (SIZE + 2));
    memset(myVal, 0, sizeof(double) * (SIZE + 2));
    // for(int i=0; i<SIZE; i++) {
    //   myVal[i] = i/5.0;
    // }
    myVal[513] = 10000.0;
    std::cout << "Starting iterative.." << std::endl;
    hclib::kernel([&]() {
        runParallel();
        });
    std::cout << "Done!" << std::endl;
    for(int i=510; i<520; i++) {
      std::cout << myVal[i] << " ";
    }
    hclib::finalize();
}