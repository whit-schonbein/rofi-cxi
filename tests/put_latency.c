// A simple one-sided put latency benchmark based on the OSU MPI one-sided put latency benchmark.
// The OSU benchmark times the time to do MPI_Put followed by MPI_Flush; this benchmark times 
// the time to do rofi_put followed by rofi_wait.
//
// The benchmark uses the rofi_msg_barrier_linear() function, which was introduced 
// for purposes of using the CXI OFI provider, but the previous rofi_barrier() call 
// should work as well. In any event, the barrier is not timed.
//

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "utils.h"
#include <rofi.h>

// MIN and MAX message sizes in bytes
#define MIN_MSG_SIZE (8)
#define MAX_MSG_SIZE (4 * 1024 * 1024)

#define NUM_ITERS (1000)
#define NUM_WARMUP_ITERS (200)

int main(void) {
    uint64_t cur_msg_size;
    uint64_t *buffer;
    uint64_t num_elements; // sizes are in bytes but the elements are uint64_t
    int numranks;
    int myrank;
    int ret;
    struct timespec t_start, t_end, t_end_put;
    uint64_t t_diff_ns;
    double t_diff_us;
    double accumulate = 0;

    rofi_init("cxi", NULL);

    numranks = rofi_get_size();
    if (numranks != 2) {
        printf("Requires exactly 2 processes (currently: %d).\n", numranks);
        return 1;
    }

    myrank = rofi_get_id();

    num_elements = MAX_MSG_SIZE/sizeof(uint64_t);
    if (0 == myrank) {
      printf("# Maximum buffer size: %d bytes (%lu uint64_t)\n", MAX_MSG_SIZE, num_elements);
      printf("# msg_size_bytes, put_latency_usecs\n");
      fflush(stdout);
    }

    ret = rofi_alloc(MAX_MSG_SIZE, 0x0, (void **)&buffer);
    if (ret) {
        printf("Error allocating message buffer");
        return ret;
    }

    // init init and target buffers
    for (int i = 0; i < num_elements; ++i) {
      if (1 == myrank) {
        buffer[i] = i+1;
      } else {
        buffer[i] = 0;
      }
    }

    rofi_msg_barrier_linear();

    // Do we want to time each put+wait and accumulate, or time the entire loop?
    // OSU one-sided latency benchmarks appear to do the former.

    for (cur_msg_size = MIN_MSG_SIZE; cur_msg_size <= MAX_MSG_SIZE; cur_msg_size = cur_msg_size * 2) {
      num_elements = cur_msg_size/sizeof(uint64_t);
    
      if (1 == myrank) {
        // warmup iterations
        for (int i = 0; i < NUM_WARMUP_ITERS; i++) {
          rofi_put(buffer, buffer, cur_msg_size, 0, 0x0);
          rofi_wait();
        }
        for (int i = 0; i < NUM_ITERS; i++) {
          clock_gettime(CLOCK_MONOTONIC, &t_start);
          rofi_put(buffer, buffer, cur_msg_size, 0, 0x0);
          rofi_wait(); 
          clock_gettime(CLOCK_MONOTONIC, &t_end);
          t_diff_us = ((BILLION * (t_end.tv_sec - t_start.tv_sec)) + (t_end.tv_nsec - t_start.tv_nsec))/1000;
          accumulate += t_diff_us;
        }
      }

      if (1 == myrank) {
        printf("%lu   %f\n", cur_msg_size, accumulate/NUM_ITERS);
        fflush(stdout);
        accumulate = 0;
      }

      rofi_msg_barrier_linear();

      // validate and reset the target buffer
      if (0 == myrank) {
        for (int i = 0; i < num_elements; ++i) {
          if (buffer[i] != i+1) {
            printf("    Rank 0: FAIL: Invalid buffer data for element %d (expected: %i actual: %lu)\n", i, i, buffer[i]);
          }
          buffer[i] = 0;
        }
      }

      rofi_msg_barrier_linear();

    }

    printf("# Rank %d: Entering final barrier\n", myrank);
    fflush(stdout);
    rofi_msg_barrier_linear();

    printf("# Rank %d: Exiting put_latency benchmark\n", myrank);
    fflush(stdout);

    rofi_release(buffer);
    rofi_finit();
    return 0;
}
