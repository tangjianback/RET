#include<iostream>
#include<string>
#include <vector>
#include<thread>
#include "../headers/pingpong.hpp"
#include "../headers/rc_pingpong.hpp"
#include "../headers/cmdline.h"
#define RECV_WR_NUM 6

struct pingpong_context *ctx;
bool send_should_stop = false;
int  iters = 1000;

void cc_recv_cqe()
{
	constexpr uint16_t max_cqe_num_one_poll = 6;
	while(true)
	{
		// poll recv cqe
		struct ibv_exp_wc wc[max_cqe_num_one_poll];
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
			/// 
			else{
				// 收到了数据的cqe位置
			}
			/// 一次性下发recv wr补齐
			pp_post_recv(ctx,ne,0);
		}
		iters -= ne;
		if(iters == 0)
			break;
		
	}
}



int main(int argc, char *argv[])
{
	cmdline::parser opt;
	opt.add<std::string>("server", 'S', "server name", false, "");
	opt.add<bool>("time", 't', "use timestamp", false, false);
    opt.add<int>("size", 's', "byte size", false, 8, cmdline::range(8, 1073741824));
    opt.add<int>("iter", 'i', "iterations", false, 10, cmdline::range(1,10000000));
	opt.add<int>("port", 'p', "port for connect", true, 10, cmdline::range(1, 1000000));
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
	unsigned long long       size = 4096 *4;
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
	iters = opt.get<int>("iter");;
	use_ts = opt.get<bool>("time");
	port = opt.get<int>("port");

	
	

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
	
	/// 下放足够多的recvwr
	uint32_t wr_id = 0;
	routs = pp_post_recv(ctx, ctx->rx_depth,wr_id++);
	if (routs < 1) {
		fprintf(stderr, "Couldn't post receive (%d)\n", routs);
		return 1;
	}

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

	if (gettimeofday(&start, NULL)) {
		perror("gettimeofday");
		return 1;
	}
	
	std::thread poll_cqe_thread(cc_recv_cqe);
	poll_cqe_thread.join();
	
	
	
	// while(true)
	// {
	// 	// poll recv cqe
	// 	struct ibv_exp_wc wc[2];
	// 	memset(wc,0,sizeof(wc));
	// 	int ne, i;
	// 	do {
	// 		ne = ibv_exp_poll_cq(ctx->cq, 2, wc, sizeof(wc[0]));
	// 		if (ne < 0) {
	// 			fprintf(stderr, "poll CQ failed %d\n", ne);
	// 			return 1;
	// 		}	
	// 	} while (ne < 1);

	// 	for (i = 0; i < ne; ++i) {
	// 		if (wc[i].status != IBV_WC_SUCCESS) {
	// 			fprintf(stderr, "Failed status %s (%d) for wr_id %d\n",
	// 				ibv_wc_status_str(wc[i].status),
	// 				wc[i].status, (int) wc[i].wr_id);
	// 			return 1;
	// 		}
	// 		/// 
	// 		else{
	// 			// 收到了数据的cqe位置
	// 			iters --;
	// 		}
	// 		/// 一次性下发recv wr补齐
	// 		pp_post_recv(ctx,ne,0);
	// 	}
	// 	if(iters == 0)
	// 		break;
	// }
	

	std::cout<<"server complete!!"<<std::endl;
	// if (gettimeofday(&end, NULL)) {
	// 	perror("gettimeofday");
	// 	return 1;
	// }

	ibv_ack_cq_events(ctx->cq, num_cq_events);

	if (pp_close_ctx(ctx))
		return 1;

	ibv_free_device_list(dev_list);
	free(rem_dest);

	return 0;
}
