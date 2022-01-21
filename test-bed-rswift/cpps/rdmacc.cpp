#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include "../headers/rdmacc.hpp"
#include "../headers/cctimer.hpp"

// do not open it when you are using rc network
#undef Debug

namespace SpaceX{
namespace RdmaCC{

/* init all variables ,const ,and config parameters using init_window and base_rtt */
rdmacc::rdmacc(double init_window,double base_rtt,double freq_ghz,bool fix_window, bool fix_target, double fix_target_value){

#ifdef Debug
    std::cout<<"debug info open ......."<<std::endl;
#endif

    /* debug */
    cc_fix_window = fix_window;
    cc_fix_target = fix_target;
    cc_fix_target_value = fix_target_value;
   /* config  */
    cc_fs_min_cwnd = 0.1;         // segment  
    cc_fs_max_cwnd = 14.0;         // segment
    cc_fs_range = 25.0;              // us 
    cc_min_window_forall = 0.05;    // segment
    cc_max_window_forall = 20.0;     // segment
    cc_increat_ai_forfabric = 0.5;        
    cc_decreat_b_forfabric = 0.5;
    cc_decreat_b_max_forfabric = 0.4;
    cc_decreat_gradient_b_forfabric = 0.8;
    cc_base_rtt_simple = base_rtt;       // the rtt for one hop distance

    /* const */
    cc_fs_base_target = base_rtt;
    cc_fs_alfa = (double)cc_fs_range / ((1 / sqrt(cc_fs_min_cwnd)) - (1 / sqrt(cc_fs_max_cwnd)));;
    cc_fs_beta = -cc_fs_alfa / sqrt(cc_fs_max_cwnd);
    cc_freq_ghz = freq_ghz;

    /* variable */
    cc_window_pre_forfabric = init_window;
    cc_window_now_forfabric = init_window;
    cc_delay_now_forfabric = 0;
    cc_delay_pre_forfabric = 0;
    cc_target_delay_forfabric = 0;
    cc_vague_rtt = 0;
    cc_vague_diff = 0;
    cc_receive_ack_counter = 0;

    // timestamp_fordecrease
    cc_last_time_decrease_forfabric_tsc = SpaceX::CCtimer::rdtsc();
}

 /* using sample update_window and orther stats */
 void rdmacc::receive_ack(double sample_rtt)
 {
    if(sample_rtt < cc_base_rtt_simple)
        sample_rtt = cc_base_rtt_simple;
        
    cc_receive_ack_counter += 1;
    // update delay,vague_rtt,vague_diff for fabric
    if (unlikely(cc_delay_pre_forfabric == 0 && cc_vague_rtt == 0 && cc_vague_diff == 0))
    {
        cc_delay_pre_forfabric = sample_rtt;
        cc_vague_rtt = sample_rtt;
        cc_vague_diff = 0;
    }
    else
    {
        cc_delay_pre_forfabric = cc_delay_now_forfabric;
        cc_vague_rtt = 0.8*cc_vague_rtt + 0.2*sample_rtt;
        cc_vague_diff = 0.2*cc_vague_diff + 0.8* (sample_rtt - cc_delay_pre_forfabric);
    }
    cc_delay_now_forfabric = sample_rtt;

    // update window funtion if the window is not fixed
    if(!cc_fix_window)
        update_window_forfabric(sample_rtt);
    
    // result handle
    if (cc_window_now_forfabric < cc_min_window_forall)
        cc_window_now_forfabric = cc_min_window_forall;

    else if (cc_window_now_forfabric > cc_max_window_forall)
        cc_window_now_forfabric = cc_max_window_forall;
    //mark if it is decreasing
    if (cc_window_now_forfabric < cc_window_pre_forfabric)
    {
        cc_last_time_decrease_forfabric_tsc = SpaceX::CCtimer::rdtsc();
    }

#ifdef Debug
    cc_info_str += std::to_string(cc_window_now_forfabric);
#endif

 }

 /* updateWindow using sample_rtt*/
void rdmacc::update_window_forfabric(double sample_rtt)
{
#ifdef Debug
    cc_info_str = "window: "+ std::to_string(cc_window_now_forfabric)+" rtt_diff: "+ std::to_string(cc_vague_diff)+" rtt_sample: "+ std::to_string(sample_rtt)+" target_rtt: "+std::to_string(get_target_delay_forfabric())+" chang_window_to: ";
#endif

    //get the target for current window
    cc_window_pre_forfabric = cc_window_now_forfabric;
    cc_target_delay_forfabric = get_target_delay_forfabric();

    /*tangjian's fix code!!!.if the rtt_diff is larger or smaller than cc_target_delay_forfabric.
      it means there are some new connections come into the traffic,and it shloud drop window or add window
      a little bit faster, notes that at the very beginning ,the target is not correct enough to do 
      stuff like this,so instead of using target ,we use cc_fs_range at this moment*/
    
    
    double rtt_diff_thresh;
    if(cc_receive_ack_counter <= 3){
        rtt_diff_thresh = cc_fs_range;
    }
    else{
        rtt_diff_thresh = cc_target_delay_forfabric;
    }
    rtt_diff_thresh = 30.0;
    
    if (cc_vague_diff > rtt_diff_thresh && cc_delay_now_forfabric >get_target_delay_forfabric())
    {
        struct timespec temp_time_stamp = {0, 0};
        unsigned long time_elapes_tsc = SpaceX::CCtimer::rdtsc() - cc_last_time_decrease_forfabric_tsc;
        double time_elapes_us = SpaceX::CCtimer::to_usec(time_elapes_tsc,cc_freq_ghz);
        if( time_elapes_us > cc_vague_rtt)
        {
            double diff_decrease = cc_decreat_gradient_b_forfabric *((double)(cc_vague_diff - rtt_diff_thresh) / (double)cc_vague_diff);
            if (diff_decrease < cc_decreat_b_max_forfabric)
                cc_window_now_forfabric = (1 - cc_decreat_b_max_forfabric) * cc_window_now_forfabric;
            else
                cc_window_now_forfabric = (1 - diff_decrease) * cc_window_now_forfabric;
        }
        return;
    }
    if (cc_vague_diff < -rtt_diff_thresh && cc_delay_now_forfabric <= get_target_delay_forfabric())
    {
        cc_window_now_forfabric = cc_window_now_forfabric + cc_increat_ai_forfabric;
        return;
    }
    
    

    // standard swift logic with fairness fixed code(addtive add)
    if (cc_delay_now_forfabric <= cc_target_delay_forfabric)
    {
        // the code below improve fairness
        double temp_ai_forfabric = cc_increat_ai_forfabric *((double)cc_vague_rtt / (double)(cc_base_rtt_simple + cc_vague_rtt - cc_fs_base_target));
        
        if (cc_window_now_forfabric >= 1)
            cc_window_now_forfabric += temp_ai_forfabric / cc_window_now_forfabric;
        else
            cc_window_now_forfabric += temp_ai_forfabric;
    }
    //  standard swift logic(multiple decrease)
    else 
    {
        struct timespec temp_time_stamp = {0, 0};
        unsigned long time_elapes_tsc = SpaceX::CCtimer::rdtsc() - cc_last_time_decrease_forfabric_tsc;
        double time_elapes_us = SpaceX::CCtimer::to_usec(time_elapes_tsc,cc_freq_ghz);
        
        if( time_elapes_us > cc_vague_rtt)
        {
            double normal_decrease = 1.0 - cc_decreat_b_forfabric* (((double)cc_delay_now_forfabric - (double)cc_target_delay_forfabric) / (double)cc_delay_now_forfabric);
            double thresh_decrease = 1.0 - cc_decreat_b_max_forfabric;
            cc_window_now_forfabric = (normal_decrease > thresh_decrease ? normal_decrease : thresh_decrease)* cc_window_now_forfabric;  
        }
    }
   return;
}
 /* caculate the target rtt and return it */
double rdmacc::get_target_delay_forfabric()
{
    // if fix_target
    if(cc_fix_target)
        return cc_fix_target_value;

    // the uncommon case
    if (cc_window_now_forfabric < cc_fs_min_cwnd)
		return cc_fs_range + cc_fs_base_target;
	else if(cc_window_now_forfabric > cc_fs_max_cwnd)
		return cc_fs_base_target;
    
    // common case
	double temp_target = cc_fs_base_target + Max(0, Min(cc_fs_alfa / sqrt(cc_window_now_forfabric) + cc_fs_beta, cc_fs_range));
	return temp_target;
}

std::string rdmacc::cc_info()
{
    return cc_info_str;
}

double rdmacc::get_window()
{
    return cc_window_now_forfabric;
}
double rdmacc::get_mean_rtt()
{
    return cc_vague_rtt;
}
}//namespace RdmaCC
}//namespace SpaceX