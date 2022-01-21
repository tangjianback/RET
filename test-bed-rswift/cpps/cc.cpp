#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <cerrno>
#include <fstream>
#include <iostream>
#include <string.h>
#include <iomanip>
#include <thread>
#include <signal.h>
#include "vector"
#include "../headers/cctimer.hpp"
#include "../headers/rdmacc.hpp"
#include "../headers/cmdline.h"


// suing line rate is 100Gbps and the base_rtt_simple is 3us
void test(double mean_rtt,size_t random_add_rtt)
{
  std::ifstream infile; 
  infile.open("rtt_sample"); 
  int s = 1000;
  std::vector<double> rtt_vec;
  double temp_value;
  while(s)
  {
    infile >> temp_value;
    rtt_vec.push_back(temp_value);
    s--;
  }
  // for(auto &i:rtt_vec)
  // {
  //   std::cout<<i<<std::endl;
  // }
  infile.close();
  //return;



  double freq_ghz = SpaceX::CCtimer::measure_rdtsc_freq();
  // init the window with 6.0  and the base rtt is 2.0us
  SpaceX::RdmaCC::rdmacc mycc(12.0,10.0,freq_ghz,0,0,0);

  // std::vector<double> sample_us;
  // for (size_t i = 0; i < 2000; i++) {
  //   double rtt_sample = mean_rtt + (double)(static_cast<size_t>(rand()) % random_add_rtt)/10;
  //   // if(i > 200 && i < 400)
  //   // {
  //   //   rtt_sample = mean_rtt + 30 + (double)(static_cast<size_t>(rand()) % random_add_rtt)/10;
  //   // }
  //   sample_us.push_back(rtt_sample);
  // }
  std::vector<std::string> cc_info_vec;
  for (double rtt_us : rtt_vec) {

    //uint64_t begin = SpaceX::CCtimer::rdtsc();
    if(rtt_us < 10.0)
      rtt_us = 10.0;
    mycc.receive_ack(rtt_us);
    //std::cout<<SpaceX::CCtimer::to_nsec(SpaceX::CCtimer::rdtsc() - begin,freq_ghz)<<std::endl;;
    
    cc_info_vec.push_back(mycc.cc_info());
    SpaceX::CCtimer::nano_sleep(8000, freq_ghz);  // Update every five microsecond
  }
  for( auto i:cc_info_vec)
  {
    std::cout<<i<<std::endl;
  }
  printf("mean %lf us, random %lf us, tput %.2f pkt\n", mean_rtt,(double)random_add_rtt/10, mycc.get_window());

}
uint64_t pack_flow_seq_to_wrid(uint8_t flow_id, uint64_t seq_number)
{
    if (seq_number > 0xffffffffffffff)
    {
      std::cout<<"warning:  the seq_number is larger than 0xffffffffffffff..."<<std::endl;
      seq_number = seq_number % 64;
    }
    if(flow_id > 0xff)
    {
      std::cout<<"the flow id is not valid,and 0 will be returned"<<std::endl;
      return 0;
    }
    return (((uint64_t)flow_id)<<56) + seq_number;
}

int unpack_wrid_to_flow_seq(uint64_t wrid,uint8_t &flow_id, uint64_t &seq_number)
{
  flow_id = wrid >> 56;
  seq_number = wrid & 0xffffffffffffff;
  return 0;
}

void cc_send_segments(int i)
{
  std::cout<<i<<std::endl;
}

void sighandler(int signum)
{
   std::cout<<"get signal "<<signum<<std::endl;
   exit(0);
}

int main(int argc, char *argv[]){
  // set the varience to 0-1 and the base_rtt to 3.0
  cmdline::parser opt;
  opt.add<int>("port", 'p', "port for connect", true, 10, cmdline::range(1, 1000000));
  opt.parse_check(argc, argv);
  // size_t random_add_rtt = 1.0;
  // size_t base_rtt = 5.0;
  // for (size_t iter = 0; iter < 1; iter++) {
  //   test(base_rtt, random_add_rtt*10);
  // }
  signal(SIGRTMAX, sighandler);
  std::cout<<"port info "<< opt.get<int>("port")<<std::endl;
   while(1) 
   {
      std::string s;
      //std::cin>>s;
      //if(s == "send")
      {
        //int ret = raise(SIGRTMAX);
        // if(ret)
        // {
        //   std::cout<<"raise error"<<std::endl;
        // }
      }   
   }

  return 0;
}