#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#ifndef SPACEX_CC_HPP
#define SPACEX_CC_HPP

namespace SpaceX{
namespace RdmaCC{
    class rdmacc
    {
    public:
        /* debug */
        bool cc_fix_window;
        bool cc_fix_target;
        double cc_fix_target_value;
        std::string cc_info_str;
        /* config  */
        double cc_fs_min_cwnd;
        double cc_fs_max_cwnd;
        double cc_fs_range;
        double cc_min_window_forall;
        double cc_max_window_forall;
        double cc_increat_ai_forfabric;
        double cc_decreat_b_forfabric;
        double cc_decreat_b_max_forfabric;
        double cc_decreat_gradient_b_forfabric;
        double cc_base_rtt_simple;
        
        /* const */
        double cc_fs_alfa;
        double cc_fs_beta;
        double cc_fs_base_target;
        double cc_freq_ghz;

        /* variable */
        double cc_window_pre_forfabric;
        double cc_window_now_forfabric;
        double cc_delay_now_forfabric;
        double cc_delay_pre_forfabric;
        double cc_target_delay_forfabric;
        double cc_vague_rtt;
        double cc_vague_diff;
        unsigned long  cc_last_time_decrease_forfabric_tsc;
        unsigned long cc_receive_ack_counter;               
        
        
        /* set parameters for current sender */
        rdmacc(double init_window,double base_rtt,double freq_ghz,bool fix_window,bool fix_target, double fix_target_value);
        
        /* receive ack from the receiver */
        void receive_ack(double sample_rtt);
        
        /* updateWindow using sample_rtt*/
        void update_window_forfabric(double sample_rtt);
        
        /* caculate the target rtt and return it */
        double get_target_delay_forfabric();

        /* return the window size for debug */
        double get_window();

        /* return the vague rtt */
        double get_mean_rtt();

        /* return change status for debug */
        std::string cc_info();

        /* two small funtions */
        double Min(double a, double b){return a < b ? a : b;};
        double Max(double a, double b){return a > b ? a : b;};

    };
}// endnamesapce RdmaCC
}// endnamespace SpaceX
#endif