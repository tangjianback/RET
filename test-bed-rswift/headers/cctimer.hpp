#ifndef SPACEX_CCTIMER_HPP
#define SPACEX_CCTIMER_HPP

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "vector"
#include <cerrno>
#include <string.h>
#include <iostream>

namespace SpaceX{
namespace CCtimer{
  /// Return the TSC
  static inline size_t rdtsc() {
    uint64_t rax;
    uint64_t rdx;
    asm volatile("rdtsc" : "=a"(rax), "=d"(rdx));
    return static_cast<size_t>((rdx << 32) | rax);
  }

  static void nano_sleep(size_t ns, double freq_ghz) {
    size_t start = rdtsc();
    size_t end = start;
    size_t upp = static_cast<size_t>(freq_ghz * ns);
    while (end - start < upp) end = rdtsc();
  }


  /// using freq and tsc ,caculate the freq_ghz
  static double measure_rdtsc_freq() {
    struct timespec start, end;
    clock_gettime(CLOCK_REALTIME, &start);
    uint64_t rdtsc_start = rdtsc();

    // Do not change this loop! The hardcoded value below depends on this loop
    // and prevents it from being optimized out.
    uint64_t sum = 5;
    for (uint64_t i = 0; i < 500000; i++) {
      sum += i + (sum + i) * (i % sum);
    }
    if(sum == 13580802877818827968ull)
    {
      throw "Error in RDTSC freq measurement";
    }

    clock_gettime(CLOCK_REALTIME, &end);
    uint64_t clock_ns =
        static_cast<uint64_t>(end.tv_sec - start.tv_sec) * 1000000000 +
        static_cast<uint64_t>(end.tv_nsec - start.tv_nsec);
    uint64_t rdtsc_cycles = rdtsc() - rdtsc_start;

    double _freq_ghz = rdtsc_cycles * 1.0 / clock_ns;
    // if(_freq_ghz >= 0.5 && _freq_ghz <= 5.0)
    // {
    //   throw "Invalid RDTSC frequency";
    // }
    return _freq_ghz;
  }
  /* the code below uesd for convert cycles to time or reverse */
  static double to_sec(size_t cycles, double freq_ghz) {
    return (cycles / (freq_ghz * 1000000000));
  }

  static double to_msec(size_t cycles, double freq_ghz) {
    return (cycles / (freq_ghz * 1000000));
  }

  static double to_usec(size_t cycles, double freq_ghz) {
    return (cycles / (freq_ghz * 1000));
  }

  static double to_nsec(size_t cycles, double freq_ghz) {
    return (cycles / freq_ghz);
  }
  static size_t s_to_cycles(double s,double freq_ghz){
    return static_cast<size_t>(s * 1000 * 1000 *1000* freq_ghz);
  }
  static size_t ms_to_cycles(double ms, double freq_ghz) {
    return static_cast<size_t>(ms * 1000 * 1000 * freq_ghz);
  }

  static size_t us_to_cycles(double us, double freq_ghz) {
    return static_cast<size_t>(us * 1000 * freq_ghz);
  }

  static size_t ns_to_cycles(double ns, double freq_ghz) {
    return static_cast<size_t>(ns * freq_ghz);
  }

  /*code below uesd for caculate time elasp */

  /// Return nanoseconds elapsed since timestamp \p t0
  static double ns_since(const struct timespec &t0) {
    struct timespec t1;
    clock_gettime(CLOCK_REALTIME, &t1);
    return (t1.tv_sec - t0.tv_sec) * 1000000000.0 + (t1.tv_nsec - t0.tv_nsec);
  }
}//endnamepsace CCtimer
}//endnamespace SpaceX

#endif

