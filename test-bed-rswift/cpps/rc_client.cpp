#include<iostream>
#include<string>
#include<errno.h>
#include<thread>
#include<vector>
#include <unistd.h>
#include<signal.h>
#include<fstream>
#include "../headers/pingpong.hpp"
#include "../headers/rc_pingpong.hpp"
#include "../headers/cmdline.h"
#include "../headers/rc_client.hpp"
#include "../headers/rdmacc.hpp"


struct pingpong_context *ctx;

bool begin_to_send = false;
int process_id;

int unpack_wrid_to_flow_seq(uint64_t wrid,uint8_t &flow_id, uint64_t &seq_number)
{
  flow_id = wrid >> 56;
  seq_number = wrid & 0xffffffffffffff;
  return 0;
}


void cc_send_segments(uint8_t flow_id)
{
	// set the segments_send_num_limit for current flow
	uint64_t segments_send_num_limit = window_parameter::cc_context[flow_id].iters;
	uint64_t seg_wr_seq = 0;
	// cycle to send data 
	while(!begin_to_send);
	while(true)
	{
		if(seg_wr_seq == segments_send_num_limit)
		{
			return;
		}
		int ret = 0;
		double updated_window = window_parameter::cc_context[flow_id].cc_for_current_conn_->get_window();
		// if we could send more segs
		if(likely(updated_window > window_parameter::cc_context[flow_id].infight_segment_num_))
		{
			// however the window is smaller than one(it means there is no seg in the network for current flow)
			if(updated_window <1 )
			{
				// calculate the block_no for the this segment send operation
				uint16_t block_no = seg_wr_seq % window_parameter::round_bitmap_len;
				// if the the slot is empty(already acked),so we can post one send and record it 
				if(likely(std::get<0>(window_parameter::cc_context[flow_id].cc_cache_[block_no]) == 1))
				{
					// sleep some round of rtt
					double sleep_us = window_parameter::cc_context[flow_id].cc_for_current_conn_->get_mean_rtt()/updated_window;
					SpaceX::CCtimer::nano_sleep((size_t)(sleep_us*1000),window_parameter::freq_ghz_);			
					// send one segment
					if(pp_post_send(ctx,seg_wr_seq,1,flow_id) != 1){
						std::cout<<"can not post ...."<<std::endl;
						continue;
					}
					//add seg_wr_seg
					seg_wr_seq ++;
					window_parameter::cc_context[flow_id].send_wr_counter++;
				}
			}

			// otherwise send segments that allows at once
			else
			{
				// calculate the wrs that we can send
				uint16_t allow_to_send = updated_window - window_parameter::cc_context[flow_id].infight_segment_num_;
				allow_to_send = allow_to_send < segments_send_num_limit - seg_wr_seq? allow_to_send:segments_send_num_limit - seg_wr_seq;
				
				// calculate the block_no for the first segment send operation
				uint16_t block_no = seg_wr_seq % window_parameter::round_bitmap_len;
				
				// calcalate the length that we can send by one bb_post and she the slots busy
				uint16_t allow_to_send_one_post = 0;
				
				for(uint16_t i = 0; i < allow_to_send; i++)
				{
					if(std::get<0>(window_parameter::cc_context[flow_id].cc_cache_.at((i + block_no)%window_parameter::round_bitmap_len)) == true)
					{
						allow_to_send_one_post ++;
						continue;
					}
					break;
				}
				
				// if we can not send any segment;
				if (allow_to_send_one_post == 0)
					continue;
				// send segments
				int real_send = pp_post_send(ctx,seg_wr_seq,allow_to_send_one_post,flow_id);
				if(real_send != allow_to_send_one_post)
				{
					std::cout<<"send does not match required "<<std::endl;
				}
				// add seg_wr_seg
				seg_wr_seq +=real_send;
				window_parameter::cc_context[flow_id].send_wr_counter += real_send;
			}
       }
	   else{
		   
	   }
    }
}
void cc_send_cqe()
{
	std::vector<double> rtt_vec;
	std::vector<unsigned int> inflight_vec;
	std::vector<uint64_t> cqe_time_vec;
	std::vector<std::string> log_vec;

	// calculate the all seg cqe nums cc_send_cqe should get
	uint64_t segments_send_num_limit = 0;
	for( auto &i : window_parameter::cc_context)
	{
		segments_send_num_limit += i.iters;
	}
	
	constexpr uint16_t max_cqe_num_one_poll = 6;
	struct ibv_exp_wc wc[max_cqe_num_one_poll];

	uint64_t begin = SpaceX::CCtimer::rdtsc();
	while(true)
	{

		// if get all send cqe then log and return
		if(segments_send_num_limit == 0)
		{
			if (process_id == 1)
			{
				std::ofstream outfile1;
				outfile1.open("datas/"+std::to_string(process_id)+".txt");
				//uint64_t begin = SpaceX::CCtimer::rdtsc();
				for(auto &i : log_vec)
				{
					outfile1 << i << std::endl;
				}
				outfile1.close();
			}
			return;
		}
		memset(wc,0,sizeof(wc));
		int ne, i;
		do {
			ne = ibv_exp_poll_cq(ctx->cq, max_cqe_num_one_poll, wc, sizeof(wc[0]));
			if (ne < 0) {
				fprintf(stderr, "poll CQ failed %d\n", ne);
				return;
			}
		} while (ne < 1);
		for (i = 0; i < ne; ++i) {

			if (wc[i].status != IBV_WC_SUCCESS) {
				fprintf(stderr, "Failed status %s (%d) for wr_id %d\n",
					ibv_wc_status_str(wc[i].status),
					wc[i].status, (int) wc[i].wr_id);
				return;
			}
			else{
				uint64_t seq_number;
				uint8_t flow_id; 
				unpack_wrid_to_flow_seq(wc[i].wr_id,flow_id,seq_number);
				//std::cout<< "ack for flowid: " << (int)flow_id<<  " seq_num : "<<seq_number<<std::endl;
				window_parameter::cc_context[flow_id].cqe_wr_counter +=1;
				
				uint16_t index = seq_number  % window_parameter::round_bitmap_len;

				// wait for the slot setting to be busy
				while(std::get<0>(window_parameter::cc_context[flow_id].cc_cache_.at(index)));

				// notify the send thread to send wr
				window_parameter::cc_context[flow_id].infight_segment_num_ --;
				
				// if(flow_id == 0)
				// 	inflight_vec.push_back(window_parameter::cc_context[flow_id].infight_segment_num_);
				
				// calculate the rtt and update cc window
				uint64_t rtt_ns = 0;

				unsigned long cqe_time = get_cqe_time(ctx->context,wc[i]);
				rtt_ns = cqe_time - std::get<1>(window_parameter::cc_context[flow_id].cc_cache_[index]);
				
				
				if(flow_id == 0 && process_id == 1)
				  	log_vec.push_back(std::to_string(cqe_time)+" "+std::to_string(rtt_ns));
				
				
				window_parameter::cc_context[flow_id].cc_for_current_conn_->receive_ack((double)rtt_ns/1000);
				// mark the slot is available
				std::get<0>(window_parameter::cc_context[flow_id].cc_cache_.at(index)) = true;		
			}	
		}
		segments_send_num_limit -= ne;
	}
}

void bug_info()
{
	std::ofstream outfile1;
	// std::ofstream outfile2;
	// std::ofstream outfile3;
	// std::ofstream outfile4;
   	outfile1.open("40_flow_rtt.txt");
	// outfile2.open("4flow_window_2.txt");
	// outfile3.open("4flow_window_3.txt");
	// outfile4.open("4flow_window_4.txt");

	uint64_t begin = SpaceX::CCtimer::rdtsc();
	while(begin_to_send)
	{
		SpaceX::CCtimer::nano_sleep(100000,window_parameter::freq_ghz_);
		//std::cout<<SpaceX::CCtimer::to_msec(SpaceX::CCtimer::rdtsc() - begin,window_parameter::freq_ghz_) << " "<< window_parameter::cc_context[0].cc_for_current_conn_->get_mean_rtt()<<std::endl;
		outfile1 << SpaceX::CCtimer::to_msec(SpaceX::CCtimer::rdtsc() - begin,window_parameter::freq_ghz_) << " "<< window_parameter::cc_context[0].cc_for_current_conn_->get_mean_rtt()<<std::endl;
		// outfile2 << SpaceX::CCtimer::to_msec(SpaceX::CCtimer::rdtsc() - begin,window_parameter::freq_ghz_) << " "<< window_parameter::cc_context[1].cc_for_current_conn_->get_window()<<std::endl;
		// outfile3 << SpaceX::CCtimer::to_msec(SpaceX::CCtimer::rdtsc() - begin,window_parameter::freq_ghz_) << " "<< window_parameter::cc_context[2].cc_for_current_conn_->get_window()<<std::endl;
		// outfile4 << SpaceX::CCtimer::to_msec(SpaceX::CCtimer::rdtsc() - begin,window_parameter::freq_ghz_) << " "<< window_parameter::cc_context[3].cc_for_current_conn_->get_window()<<std::endl;
	}
	outfile1.close();
	// outfile2.close();
	// outfile3.close();
	// outfile4.close();
}

void sighandler(int signum)
{
	begin_to_send = true;
}


int main(int argc, char *argv[])
{
	
	signal(SIGRTMAX, sighandler);
	cmdline::parser opt;
	opt.add<std::string>("server", 'S', "server name", false, "11.0.0.6");
	opt.add<bool>("time", 't', "use timestamp", false, true);
    opt.add<int>("size", 's', "byte size", false, 8, cmdline::range(8, 1073741824));
    opt.add<int>("iter", 'i', "iterations", false, 10, cmdline::range(1, 1000000));
	opt.add<int>("port", 'p', "port for connect", true, 10, cmdline::range(1, 1000000));
	opt.add<int>("pid",'z',"process id ",true,10,cmdline::range(0, 1000000));
    opt.add<int>("interface", 'I', "rdma interface number", false, 1, cmdline::range(1,2));
    opt.add<int>("gidindex", 'g', "gid index", false, 3, cmdline::range(1, 3));
    opt.add<std::string>("device", 'd', "device name", false, "mlx5_0");
	
    opt.parse_check(argc, argv);

	struct ibv_device      **dev_list;
	struct ibv_device	*ib_dev;
	
	struct pingpong_dest     my_dest;
	struct pingpong_dest    *rem_dest;
	struct timeval           start, end;
	char                    *ib_devname = NULL;
	char                    *servername = NULL;
	int                      port = 18515;
	int                      ib_port = 1;
	unsigned long long       size = 4096 * 4;
	enum ibv_mtu		 mtu = IBV_MTU_1024;
	int                      rx_depth = 500;
	
	int                      use_event = 0;
	int                      routs;
	int                      rcnt, scnt;
	int                      num_cq_events = 0;
	int                      sl = 0;
	int			 gidx = -1;
	char			 gid[INET6_ADDRSTRLEN];
	int                      inlr_recv = 0;         //inline receive 在小数据模式中，将受到数据直接放在cqe中
	int			 check_nop = 0;
	int			 err;

	srand48(getpid() * time(NULL));
	contig_addr = NULL;

	/* set the startup parameter here */
	ib_devname = strdupa((opt.get<std::string>("device")).c_str());
	servername = strdupa((opt.get<std::string>("server")).c_str());
	
	if(strlen(servername) == 0)
		servername = NULL;
	gidx = opt.get<int>("gidindex");
	//iters = opt.get<int>("iter");;
	use_ts = opt.get<bool>("time");
	port = opt.get<int>("port");
	process_id = opt.get<int>("pid");

	page_size = sysconf(_SC_PAGESIZE);

	dev_list = ibv_get_device_list(NULL);
	if (!dev_list) {
		perror("Failed to get IB devices list");
		return 1;
	}
	
	

	if (!ib_devname) {
		ib_dev = *dev_list;
		if (!ib_dev) {
			fprintf(stderr, "No IB devices found\n");
			return 1;
		}
	} else {
		int i;
		for (i = 0; dev_list[i]; ++i)
			if (!strcmp(ibv_get_device_name(dev_list[i]), ib_devname))
				break;
		ib_dev = dev_list[i];
		if (!ib_dev) {
			fprintf(stderr, "IB device %s not found\n", ib_devname);
			return 1;
		}
	}
	
	/// 设置所有的内存参数设置，内存开辟，并且qp设置状态为init
	ctx = pp_init_ctx(ib_dev, size, rx_depth, ib_port, use_event, inlr_recv);
	if (!ctx)
		return 1;
	

	// routs = pp_post_recv(ctx, ctx->rx_depth);
	// if (routs < ctx->rx_depth) {
	// 	fprintf(stderr, "Couldn't post receive (%d)\n", routs);
	// 	return 1;
	// }

	if (use_event)
		if (ibv_req_notify_cq(ctx->cq, 0)) {
			fprintf(stderr, "Couldn't request CQ notification\n");
			return 1;
		}


	if (pp_get_port_info(ctx->context, ib_port, &ctx->portinfo)) {
		fprintf(stderr, "Couldn't get port info\n");
		return 1;
	}

	my_dest.lid = ctx->portinfo.lid;
	if (ctx->portinfo.link_layer != IBV_LINK_LAYER_ETHERNET &&
							!my_dest.lid) {
		fprintf(stderr, "Couldn't get local LID\n");
		return 1;
	}

	if (gidx >= 0) {
		if (ibv_query_gid(ctx->context, ib_port, gidx, &my_dest.gid)) {
			fprintf(stderr, "can't read sgid of index %d\n", gidx);
			return 1;
		}
	} else
		memset(&my_dest.gid, 0, sizeof my_dest.gid);

	my_dest.qpn = ctx->qp->qp_num;
	my_dest.psn = lrand48() & 0xffffff;
	inet_ntop(AF_INET6, &my_dest.gid, gid, sizeof gid);
	// printf("  local address:  LID 0x%04x, QPN 0x%06x, PSN 0x%06x, GID %s\n",
	//        my_dest.lid, my_dest.qpn, my_dest.psn, gid);


	/// 如果是客户端就拿到对方信息就好
	if (servername)
		rem_dest = pp_client_exch_dest(servername, port, &my_dest);
	/// 如果是服务器拿到对方的消息之后还要设置自己的qp状态到最新的状态
	else
		rem_dest = pp_server_exch_dest(ctx, ib_port, mtu, port, sl,
								&my_dest, gidx);

	if (!rem_dest)
		return 1;

	inet_ntop(AF_INET6, &rem_dest->gid, gid, sizeof gid);
	// printf("  remote address: LID 0x%04x, QPN 0x%06x, PSN 0x%06x, GID %s\n",
	//        rem_dest->lid, rem_dest->qpn, rem_dest->psn, gid);

	if (servername)
		if (pp_connect_ctx(ctx, ib_port, my_dest.psn, mtu, sl, rem_dest,
					gidx))
			return 1;

	ctx->pending = PINGPONG_RECV_WRID;

	
	/// 是否支持时间戳
	if(use_ts){
		struct ibv_exp_device_attr attr;
		memset(&attr, 0, sizeof(attr));
		attr.comp_mask |= IBV_EXP_DEVICE_ATTR_WITH_TIMESTAMP_MASK;
		int ret = ibv_exp_query_device(ctx->context, &attr);
		if(ret){
			printf("query get an error\n");
			return 1;
		}
		if (attr.comp_mask & IBV_EXP_DEVICE_ATTR_WITH_TIMESTAMP_MASK) {
		if (!attr.timestamp_mask) {
				/* Time stamping is supported with mask attr.timestamp_mask */
				printf("timestamp is not sopported\n");
				return 1;
			}
		}
	}


	// 初始化发送的参数
	for(int i = 0; i < window_parameter::cc_context.size(); i++)
	{
		// 将cc_cache 中的ack位置和时间戳初始化
		window_parameter::cc_context.at(i).cc_cache_.fill(std::make_tuple(true, 0));
		// 其他上下文环境的初始化
		window_parameter::cc_context.at(i).infight_segment_num_ = 0;
		// 流的初始化 init_window,base rtt,fixed_window 都可以每个流定制(否则使用默认值)
		window_parameter::cc_context.at(i).cc_for_current_conn_ = new SpaceX::RdmaCC::rdmacc(window_parameter::init_window,window_parameter::base_rtt,window_parameter::freq_ghz_,window_parameter::fix_window,window_parameter::fix_target,window_parameter::fix_target_value);
	}
	
	// 初始化所有流共享的变量
	window_parameter::freq_ghz_ = SpaceX::CCtimer::measure_rdtsc_freq();
	window_parameter::last_wr_complete_time = 0;
	window_parameter::last_wr_rtt_duration = 0;

	

	// 对每一个流开启发送函数
	std::vector<std::thread *> thread_vec;
	for(int i = 0; i < window_parameter::cc_context.size(); i++)
	{
		thread_vec.push_back(new std::thread(cc_send_segments,i));
	}

	// 开启cqe接收函数 并且开始放数据
	std::thread cqe_thread(cc_send_cqe);

	
	//begin_to_send = true;
	//std::cout<<"process ready to get signal..."<<std::endl;
	while(!begin_to_send);
	// 开启bug_info 线程

	// std::thread * bug_info_thread_pointer = nullptr;
	// if(process_id == 0)
	// {
	// 	bug_info_thread_pointer = new std::thread(bug_info);
	// }
		


	// 开始发送的时间标记
	if (gettimeofday(&start, NULL)) {
		perror("gettimeofday");
		return 1;
	}

	// 主线程等多所有子线程完成
	for(auto& t : thread_vec)
	{
		t->join();
		delete t;
	}
	cqe_thread.join();

	begin_to_send  = false;

	// if(bug_info_thread_pointer != nullptr)
	// {
	// 	bug_info_thread_pointer->join();
	// 	delete bug_info_thread_pointer;
	// }
		

	if (gettimeofday(&end, NULL)) {
		perror("gettimeofday");
		return 1;
	}
	// 释放所有的cc示例
	for(auto &i: window_parameter::cc_context)
	{
		delete i.cc_for_current_conn_;
	}
	
	float usec = (end.tv_sec - start.tv_sec) * 1000000 +
		(end.tv_usec - start.tv_usec);
	long long bytes = (long long) size * window_parameter::flow_num * window_parameter::flow_iters;

	//printf("%lld bytes in %.2f seconds = %.2f Mbit/sec\n",
	//		bytes, usec / 1000000., bytes * 8. / usec);

	//std::cout<<"client complete"<<std::endl;

	ibv_ack_cq_events(ctx->cq, num_cq_events);

	if (pp_close_ctx(ctx))
		return 1;

	ibv_free_device_list(dev_list);
	free(rem_dest);

	return 0;
}
