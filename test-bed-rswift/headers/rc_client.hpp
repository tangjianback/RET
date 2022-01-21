#pragma once
#include<array>
#include <atomic>
#include "rdmacc.hpp"
namespace window_parameter{
    // variables for all flows
    constexpr uint32_t round_bitmap_len = 64;
    constexpr uint32_t flow_num = 1;
    constexpr uint32_t flow_iters = 100000;
    constexpr double init_window = 14.0;
    constexpr double base_rtt = 5.0;
    constexpr double fix_target_value = 20.0;

    constexpr bool fix_target = true;
    constexpr bool fix_window = false;

    double freq_ghz_ = 0; //cpu freq
    uint64_t last_wr_complete_time;
    uint64_t last_wr_rtt_duration;

    struct cc_contro_struct
    {
        // variables 
        std::array<std::tuple<bool, uint64_t>, window_parameter::round_bitmap_len> cc_cache_;//slot avil and begin time
        std::atomic_uint infight_segment_num_; // inflight num wr
        SpaceX::RdmaCC::rdmacc* cc_for_current_conn_ = nullptr; //cc instance
        int send_wr_counter = 0;
        int cqe_wr_counter = 0;
        // const should be assigned for every flow,the value below describe the default value
        int  iters = flow_iters;
    };
    // cc_contro_structs
    std::array<struct cc_contro_struct,flow_num> cc_context;
}
