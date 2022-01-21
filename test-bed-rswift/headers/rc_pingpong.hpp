#ifndef IBV_RC_PINGPONG_H
#define IBV_RC_PINGPONG_H
#include "pingpong.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <malloc.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <time.h>
#include <error.h>
#include "cctimer.hpp"
#include "rc_client.hpp"

#define WR_SEQUNLIZE_TIME 1308
/// 两种work request id(1号表示是receive,2号表示是send)
enum {
	PINGPONG_RECV_WRID = 1,
	PINGPONG_SEND_WRID = 2,
};

/// 定义多个变量和控制参数
static int page_size;         /// 当前系统的page-size
static int use_contiguous_mr; 
static int use_odp;           /// 是否使用on-deman技术(请求调页技术)
static int use_dm;            /// 是否使用device memory
static int use_ts = 0;            /// 是否使用time stamp
static int use_upstream;
static int use_ooo;
static void *contig_addr;


/// 一个连接的上下文
struct pingpong_context {
	struct ibv_context	*context;
	struct ibv_comp_channel *channel;
	struct ibv_pd		*pd;
	struct ibv_mr		*mr;
	struct ibv_exp_dm	*dm;
	struct ibv_cq		*cq;
	struct ibv_qp		*qp;
	void			*buf;
	unsigned long long	 size;
	int			 rx_depth;
	int			 pending;
	struct ibv_port_attr	 portinfo;
	int			 inlr_recv;
};

struct pingpong_dest {
	int lid;
	int qpn;
	int psn;
	union ibv_gid gid;
};
unsigned long get_hard_ware_time(struct ibv_context * temp_context);

static int pp_connect_ctx(struct pingpong_context *ctx, int port, int my_psn,
			  enum ibv_mtu mtu, int sl,
			  struct pingpong_dest *dest, int sgid_idx)
{
	struct ibv_exp_qp_attr attr;
	memset(&attr,0,sizeof(attr));
	attr.qp_state = IBV_QPS_RTR;
	attr.path_mtu = mtu;
	attr.dest_qp_num = dest->qpn;
	attr.rq_psn = dest->psn;
	attr.max_dest_rd_atomic = 1;
	attr.min_rnr_timer = 12;
	attr.ah_attr.is_global = 0;
	attr.ah_attr.dlid = dest->lid;
	attr.ah_attr.sl = sl;
	attr.ah_attr.src_path_bits = 0;
	attr.ah_attr.port_num = port;
	/*struct ibv_exp_qp_attr attr = {
		.qp_state		= IBV_QPS_RTR,
		.path_mtu		= mtu,
		.dest_qp_num		= dest->qpn,
		.rq_psn			= dest->psn,
		.max_dest_rd_atomic	= 1,
		.min_rnr_timer		= 12,
		.ah_attr		= {
			.is_global	= 0,
			.dlid		= dest->lid,
			.sl		= sl,
			.src_path_bits	= 0,
			.port_num	= port
		}
	};*/
	enum ibv_exp_qp_attr_mask attr_mask;

	if (dest->gid.global.interface_id) {
		attr.ah_attr.is_global = 1;
		attr.ah_attr.grh.hop_limit = 1;
		attr.ah_attr.grh.dgid = dest->gid;
		attr.ah_attr.grh.sgid_index = sgid_idx;
	}
	attr_mask = (enum ibv_exp_qp_attr_mask)(IBV_QP_STATE              |
		    IBV_QP_AV                 |
		    IBV_QP_PATH_MTU           |
		    IBV_QP_DEST_QPN           |
		    IBV_QP_RQ_PSN             |
		    IBV_QP_MAX_DEST_RD_ATOMIC |
		    IBV_QP_MIN_RNR_TIMER);
	int attr_mask_int = (int)attr_mask;
	attr_mask_int |= use_ooo ? IBV_EXP_QP_OOO_RW_DATA_PLACEMENT : 0;
	attr_mask = (enum ibv_exp_qp_attr_mask)attr_mask_int;

	if (ibv_exp_modify_qp(ctx->qp, &attr, attr_mask)) {
		fprintf(stderr, "Failed to modify QP to RTR\n");
		return 1;
	}

	attr.qp_state	    = IBV_QPS_RTS;
	attr.timeout	    = 14;
	attr.retry_cnt	    = 7;
	attr.rnr_retry	    = 7;
	attr.sq_psn	    = my_psn;
	attr.max_rd_atomic  = 1;
	if (ibv_exp_modify_qp(ctx->qp, &attr,
			      IBV_QP_STATE              |
			      IBV_QP_TIMEOUT            |
			      IBV_QP_RETRY_CNT          |
			      IBV_QP_RNR_RETRY          |
			      IBV_QP_SQ_PSN             |
			      IBV_QP_MAX_QP_RD_ATOMIC)) {
		fprintf(stderr, "Failed to modify QP to RTS\n");
		return 1;
	}

	return 0;
}

static struct pingpong_dest *pp_client_exch_dest(const char *servername, int port,
						 const struct pingpong_dest *my_dest)
{
	struct addrinfo *res, *t;
	struct addrinfo hints;
	memset(&hints,0,sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	// struct addrinfo hints = {
	// 	.ai_family   = AF_UNSPEC,
	// 	.ai_socktype = SOCK_STREAM
	// };
	char *service;
	char msg[sizeof "0000:000000:000000:00000000000000000000000000000000"];
	int n;
	int sockfd = -1;
	struct pingpong_dest *rem_dest = NULL;
	char gid[33];

	if (asprintf(&service, "%d", port) < 0)
		return NULL;

	n = getaddrinfo(servername, service, &hints, &res);

	if (n < 0) {
		fprintf(stderr, "%s for %s:%d\n", gai_strerror(n), servername, port);
		free(service);
		return NULL;
	}

	for (t = res; t; t = t->ai_next) {
		sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
		if (sockfd >= 0) {
			if (!connect(sockfd, t->ai_addr, t->ai_addrlen))
				break;
			close(sockfd);
			sockfd = -1;
		}
	}

	freeaddrinfo(res);
	free(service);

	if (sockfd < 0) {
		fprintf(stderr, "Couldn't connect to %s:%d\n", servername, port);
		return NULL;
	}

	gid_to_wire_gid(&my_dest->gid, gid);
	sprintf(msg, "%04x:%06x:%06x:%s", my_dest->lid, my_dest->qpn,
							my_dest->psn, gid);
	if (write(sockfd, msg, sizeof msg) != sizeof msg) {
		fprintf(stderr, "Couldn't send local address\n");
		goto out;
	}

	if (recv(sockfd, msg, sizeof(msg), MSG_WAITALL) != sizeof(msg)) {
		perror("client read");
		fprintf(stderr, "Couldn't read remote address\n");
		goto out;
	}

	if (write(sockfd, "done", sizeof("done")) != sizeof("done")) {
		fprintf(stderr, "Couldn't send \"done\" msg\n");
		goto out;
	}

	rem_dest = (pingpong_dest *)malloc(sizeof *rem_dest);
	if (!rem_dest)
		goto out;

	sscanf(msg, "%x:%x:%x:%s", &rem_dest->lid, &rem_dest->qpn,
						&rem_dest->psn, gid);
	wire_gid_to_gid(gid, &rem_dest->gid);

out:
	close(sockfd);
	return rem_dest;
}

static struct pingpong_dest *pp_server_exch_dest(struct pingpong_context *ctx,
						 int ib_port, enum ibv_mtu mtu,
						 int port, int sl,
						 const struct pingpong_dest *my_dest,
						 int sgid_idx)
{
	struct addrinfo *res, *t;
	struct addrinfo hints = {
		.ai_flags    = AI_PASSIVE,
		.ai_family   = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM
	};
	char *service;
	char msg[sizeof "0000:000000:000000:00000000000000000000000000000000"];
	int n;
	int sockfd = -1, connfd;
	struct pingpong_dest *rem_dest = NULL;
	char gid[33];

	if (asprintf(&service, "%d", port) < 0)
		return NULL;

	n = getaddrinfo(NULL, service, &hints, &res);

	if (n < 0) {
		fprintf(stderr, "%s for port %d\n", gai_strerror(n), port);
		free(service);
		return NULL;
	}

	for (t = res; t; t = t->ai_next) {
		sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
		if (sockfd >= 0) {
			n = 1;

			setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof n);

			if (!bind(sockfd, t->ai_addr, t->ai_addrlen))
				break;
			close(sockfd);
			sockfd = -1;
		}
	}

	freeaddrinfo(res);
	free(service);

	if (sockfd < 0) {
		fprintf(stderr, "Couldn't listen to port %d\n", port);
		return NULL;
	}

	listen(sockfd, 1);
	std::cout<<"listen on "<<port<<std::endl;
	connfd = accept(sockfd, NULL, 0);
	close(sockfd);
	if (connfd < 0) {
		fprintf(stderr, "accept() failed\n");
		return NULL;
	}

	n = recv(connfd, msg, sizeof(msg), MSG_WAITALL);
	if (n != sizeof msg) {
		perror("server read");
		fprintf(stderr, "%d/%d: Couldn't read remote address\n", n, (int) sizeof msg);
		goto out;
	}

	rem_dest = (pingpong_dest*)malloc(sizeof *rem_dest);
	if (!rem_dest)
		goto out;

	sscanf(msg, "%x:%x:%x:%s", &rem_dest->lid, &rem_dest->qpn,
							&rem_dest->psn, gid);
	wire_gid_to_gid(gid, &rem_dest->gid);

	if (pp_connect_ctx(ctx, ib_port, my_dest->psn, mtu, sl, rem_dest,
								sgid_idx)) {
		fprintf(stderr, "Couldn't connect to remote QP\n");
		free(rem_dest);
		rem_dest = NULL;
		goto out;
	}


	gid_to_wire_gid(&my_dest->gid, gid);
	sprintf(msg, "%04x:%06x:%06x:%s", my_dest->lid, my_dest->qpn,
							my_dest->psn, gid);
	if (write(connfd, msg, sizeof msg) != sizeof msg) {
		fprintf(stderr, "Couldn't send local address\n");
		free(rem_dest);
		rem_dest = NULL;
		goto out;
	}

	/* expecting "done" msg */
	if (read(connfd, msg, sizeof(msg)) <= 0) {
		fprintf(stderr, "Couldn't read \"done\" msg\n");
		free(rem_dest);
		rem_dest = NULL;
		goto out;
	}

out:
	close(connfd);
	return rem_dest;
}

static struct pingpong_context *pp_init_ctx(struct ibv_device *ib_dev, unsigned long long size,
					    int rx_depth, int port,
					    int use_event, int inlr_recv)
{
	
	struct pingpong_context *ctx;
	struct ibv_exp_device_attr dattr;
	int ret;

	ctx = (pingpong_context *)calloc(1, sizeof *ctx);
	if (!ctx)
		return NULL;
	memset(&dattr, 0, sizeof(dattr));

	ctx->size     = size;
	ctx->rx_depth = rx_depth;

	if (!use_contiguous_mr) {
		ctx->buf = memalign(page_size, size);
		if (!ctx->buf) {
			fprintf(stderr, "Couldn't allocate work buf.\n");
			goto clean_ctx;
		}
	}

	ctx->context = ibv_open_device(ib_dev);
	if (!ctx->context) {
		fprintf(stderr, "Couldn't get context for %s\n",
			ibv_get_device_name(ib_dev));
		goto clean_buffer;
	}
	if (inlr_recv) {
		dattr.comp_mask |= IBV_EXP_DEVICE_ATTR_INLINE_RECV_SZ;
		ret = ibv_exp_query_device(ctx->context, &dattr);
		if (ret) {
			printf("  Couldn't query device for inline-receive capabilities.\n");
		} else if (!(dattr.comp_mask & IBV_EXP_DEVICE_ATTR_INLINE_RECV_SZ)) {
			printf("  Inline-receive not supported by driver.\n");
		} else if (dattr.inline_recv_sz < inlr_recv) {
			printf("  Max inline-receive(%d) < Requested inline-receive(%d).\n",
			       dattr.inline_recv_sz, inlr_recv);
		}
	}

	ctx->inlr_recv = inlr_recv;

	if (use_event) {
		ctx->channel = ibv_create_comp_channel(ctx->context);
		if (!ctx->channel) {
			fprintf(stderr, "Couldn't create completion channel\n");
			goto clean_device;
		}
	} else
		ctx->channel = NULL;

	ctx->pd = ibv_alloc_pd(ctx->context);
	if (!ctx->pd) {
		fprintf(stderr, "Couldn't allocate PD\n");
		goto clean_comp_channel;
	}

	if (!use_contiguous_mr && !use_odp && !use_dm) {
		ctx->mr = ibv_reg_mr(ctx->pd, ctx->buf, size,
				     IBV_ACCESS_LOCAL_WRITE);
	} else if (use_odp) {
		if (use_upstream) {
			int access_flags = IBV_ACCESS_LOCAL_WRITE;
			const uint32_t rc_caps_mask = IBV_ODP_SUPPORT_SEND |
					      IBV_ODP_SUPPORT_RECV;
			struct ibv_device_attr_ex attrx;
			memset(&attrx,0,sizeof(attrx));

			if (ibv_query_device_ex(ctx->context, NULL, &attrx)) {
				fprintf(stderr, "Couldn't query device for its features\n");
				goto clean_pd;
			}

			if (!(attrx.odp_caps.general_caps & IBV_ODP_SUPPORT) ||
			    (attrx.odp_caps.per_transport_caps.rc_odp_caps & rc_caps_mask) != rc_caps_mask) {
				fprintf(stderr, "The device isn't ODP capable or does not support RC send and receive with ODP\n");
				goto clean_pd;
			}

			access_flags |= IBV_ACCESS_ON_DEMAND;
			ctx->mr = ibv_reg_mr(ctx->pd, ctx->buf, size, access_flags);
		}
		else {
			struct ibv_exp_reg_mr_in in;
			memset(&in,0,sizeof(in));
			in.pd = ctx->pd;
			in.addr = ctx->buf;
			in.length = size;
			in.exp_access = IBV_EXP_ACCESS_LOCAL_WRITE | IBV_EXP_ACCESS_ON_DEMAND;
			in.comp_mask = 0;
			dattr.comp_mask |= IBV_EXP_DEVICE_ATTR_ODP;
			ret = ibv_exp_query_device(ctx->context, &dattr);
			if (ret) {
				printf(" Couldn't query device for on-demand\
				       paging capabilities.\n");
				goto clean_pd;
			} else if (!(dattr.comp_mask & IBV_EXP_DEVICE_ATTR_ODP)) {
				printf(" On-demand paging not supported by driver.\n");
				goto clean_pd;
			} else if (!(dattr.odp_caps.per_transport_caps.rc_odp_caps &
				   IBV_EXP_ODP_SUPPORT_SEND)) {
				printf(" Send is not supported for RC transport.\n");
				goto clean_pd;
			} else if (!(dattr.odp_caps.per_transport_caps.rc_odp_caps &
				   IBV_EXP_ODP_SUPPORT_RECV)) {
				printf(" Receive is not supported for RC transport.\n");
				goto clean_pd;
			}

			ctx->mr = ibv_exp_reg_mr(&in);
		}
	} else if (use_contiguous_mr) {
		struct ibv_exp_reg_mr_in in;
		memset(&in,0,sizeof(in));

		in.pd = ctx->pd;
		in.addr = contig_addr;
		in.length = size;
		in.exp_access = IBV_EXP_ACCESS_LOCAL_WRITE;
		if (contig_addr) {
			in.comp_mask = IBV_EXP_REG_MR_CREATE_FLAGS;
			in.create_flags = IBV_EXP_REG_MR_CREATE_CONTIG;
		} else {
			in.comp_mask = 0;
			in.exp_access |= IBV_EXP_ACCESS_ALLOCATE_MR;
		}

		ctx->mr = ibv_exp_reg_mr(&in);
	} else {
		struct ibv_exp_alloc_dm_attr dm_attr = {0};
		struct ibv_exp_reg_mr_in mr_in;
		memset(&mr_in,0,sizeof(mr_in));
		mr_in.pd = ctx->pd;
		mr_in.addr = 0;
		mr_in.length = size;
		mr_in.exp_access = IBV_EXP_ACCESS_LOCAL_WRITE;
		mr_in.create_flags = 0;
		// struct ibv_exp_reg_mr_in mr_in = { .pd = ctx->pd,
		// 				   .addr = 0,
		// 				   .length = size,
		// 				   .exp_access = IBV_EXP_ACCESS_LOCAL_WRITE,
		// 				   .create_flags = 0};

		dattr.comp_mask = IBV_EXP_DEVICE_ATTR_MAX_DM_SIZE;
		ret = ibv_exp_query_device(ctx->context, &dattr);
		if (ret) {
			fprintf(stderr, "Couldn't query device for max_dm_size\n");
			goto clean_pd;
		} else if (!(dattr.comp_mask & IBV_EXP_DEVICE_ATTR_MAX_DM_SIZE)) {
			fprintf(stderr, "Device memory not supported by driver\n");
			goto clean_pd;
		} else if (!(dattr.max_dm_size)) {
			fprintf(stderr, "Max dm size is zero\n");
			goto clean_pd;
		}

        fprintf(stdout, "max_dm_size is %ld\n", dattr.max_dm_size);

		dm_attr.length = size;
		ctx->dm = ibv_exp_alloc_dm(ctx->context, &dm_attr);
		if (!ctx->dm) {
			fprintf(stderr, "Dev mem allocation failed\n");
			goto clean_pd;
		}

		mr_in.dm = ctx->dm;
		mr_in.comp_mask = IBV_EXP_REG_MR_DM;
		ctx->mr = ibv_exp_reg_mr(&mr_in);
	}	
		
	if (!ctx->mr) {
		fprintf(stderr, "Couldn't register MR\n");
		goto clean_dm;
	}
	
	if (use_contiguous_mr)
		ctx->buf = ctx->mr->addr;

	/* FIXME memset(ctx->buf, 0, size); */
	memset(ctx->buf, 0x7b, size);
	
	if (use_dm) {
		struct ibv_exp_memcpy_dm_attr cpy_attr;

		cpy_attr.memcpy_dir = IBV_EXP_DM_CPY_TO_DEVICE;
		cpy_attr.host_addr = (void *)ctx->buf;
		cpy_attr.length = size;
		if (ibv_exp_memcpy_dm(ctx->dm, &cpy_attr)) {
			fprintf(stderr, "Copy to dev mem failed\n");
			goto clean_dm;
		}
	}

	if(use_ts){
		struct ibv_exp_cq_init_attr cq_init_attr;
		memset(&cq_init_attr,0,sizeof(cq_init_attr));
		cq_init_attr.flags = IBV_EXP_CQ_TIMESTAMP;
		cq_init_attr.comp_mask = IBV_EXP_CQ_INIT_ATTR_FLAGS;
		ctx->cq = ibv_exp_create_cq(ctx->context, rx_depth + 1, NULL, ctx->channel, 0, &cq_init_attr);
	}
	else{
		ctx->cq = ibv_create_cq(ctx->context, rx_depth + 1, NULL,ctx->channel, 0);
		if (!ctx->cq) {
			fprintf(stderr, "Couldn't create CQ\n");
			goto clean_mr;
		}
	}
	
	{
		
		struct ibv_exp_qp_init_attr attr;
		memset(&attr,0,sizeof(attr));
		attr.send_cq = ctx->cq;
		attr.recv_cq = ctx->cq;
		attr.cap.max_send_wr = rx_depth;
		attr.cap.max_recv_wr  = rx_depth;
		attr.cap.max_send_sge = 1;
		attr.cap.max_recv_sge = 1;
		attr.qp_type = IBV_QPT_RC;
		attr.pd = ctx->pd;
		attr.comp_mask = IBV_EXP_QP_INIT_ATTR_PD;
		attr.max_inl_recv = ctx->inlr_recv;

	
		if (ctx->inlr_recv)
			attr.comp_mask |= IBV_EXP_QP_INIT_ATTR_INL_RECV;
		
		ctx->qp = ibv_exp_create_qp(ctx->context, &attr);
		
		if (!ctx->qp)  {
			fprintf(stderr, "Couldn't create QP\n");
			goto clean_cq;
		}
		if (ctx->inlr_recv > attr.max_inl_recv)
			printf("  Actual inline-receive(%d) < requested inline-receive(%d)\n",
			       attr.max_inl_recv, ctx->inlr_recv);
	}

	{
		struct ibv_qp_attr attr;
		memset(&attr,0,sizeof(attr));

		attr.qp_state = IBV_QPS_INIT;
		attr.pkey_index = 0;
		attr.port_num = port;
		attr.qp_access_flags = 0;

		// struct ibv_qp_attr attr = {
		// 	.qp_state        = IBV_QPS_INIT,
		// 	.pkey_index      = 0,
		// 	.port_num        = port,
		// 	.qp_access_flags = 0
		// };

		if (ibv_modify_qp(ctx->qp, &attr,
				  IBV_QP_STATE              |
				  IBV_QP_PKEY_INDEX         |
				  IBV_QP_PORT               |
				  IBV_QP_ACCESS_FLAGS)) {
			fprintf(stderr, "Failed to modify QP to INIT\n");
			goto clean_qp;
		}
	}
	
	return ctx;


clean_qp:
	ibv_destroy_qp(ctx->qp);

clean_cq:
	ibv_destroy_cq(ctx->cq);

clean_mr:
	ibv_dereg_mr(ctx->mr);

clean_dm:
	if (ctx->dm)
		ibv_exp_free_dm(ctx->dm);

clean_pd:
	ibv_dealloc_pd(ctx->pd);

clean_comp_channel:
	if (ctx->channel)
		ibv_destroy_comp_channel(ctx->channel);

clean_device:
	ibv_close_device(ctx->context);

clean_buffer:
	if (!use_contiguous_mr)
		free(ctx->buf);

clean_ctx:
	free(ctx);


	return NULL;


}

int pp_close_ctx(struct pingpong_context *ctx)
{
	if (ibv_destroy_qp(ctx->qp)) {
		fprintf(stderr, "Couldn't destroy QP\n");
		return 1;
	}

	if (ibv_destroy_cq(ctx->cq)) {
		fprintf(stderr, "Couldn't destroy CQ\n");
		return 1;
	}

	if (ibv_dereg_mr(ctx->mr)) {
		fprintf(stderr, "Couldn't deregister MR\n");
		return 1;
	}

	if (use_dm) {
		if (ibv_exp_free_dm(ctx->dm)) {
			fprintf(stderr, "Couldn't free DM\n");
			return 1;
		}
	}

	if (ibv_dealloc_pd(ctx->pd)) {
		fprintf(stderr, "Couldn't deallocate PD\n");
		return 1;
	}

	if (ctx->channel) {
		if (ibv_destroy_comp_channel(ctx->channel)) {
			fprintf(stderr, "Couldn't destroy completion channel\n");
			return 1;
		}
	}

	if (ibv_close_device(ctx->context)) {
		fprintf(stderr, "Couldn't release context\n");
		return 1;
	}

	if (!use_contiguous_mr)
		free(ctx->buf);

	free(ctx);

	return 0;
}

#define mmin(a, b) a < b ? a : b
#define MAX_SGE_LEN 0xFFFFFFF

static int pp_post_recv(struct pingpong_context *ctx, int n,uint64_t wr_id)
{
	struct ibv_sge list;
	memset(&list,0,sizeof(list));
	list.addr = use_dm ? 0 : (uintptr_t) ctx->buf;
	list.length = mmin(ctx->size, MAX_SGE_LEN);
	list.lkey	= ctx->mr->lkey;

	struct ibv_recv_wr wr;
	memset(&wr,0,sizeof(wr));
	wr.wr_id = wr_id;
	wr.sg_list = &list;
	wr.num_sge    = 1;


	// struct ibv_sge list = {
	// 	.addr	= use_dm ? 0 : (uintptr_t) ctx->buf,
	// 	.length = mmin(ctx->size, MAX_SGE_LEN),
	// 	.lkey	= ctx->mr->lkey
	// };
	// struct ibv_recv_wr wr = {
	// 	.wr_id	    = PINGPONG_RECV_WRID,
	// 	.sg_list    = &list,
	// 	.num_sge    = 1,
	// };
	struct ibv_recv_wr *bad_wr;
	int i;

	for (i = 0; i < n; ++i)
	{
		if (ibv_post_recv(ctx->qp, &wr, &bad_wr))
			break;
	}
		

	return i;
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

#define MAX(a,b) a>b?a:b;

// return the actually send
static int pp_post_send(struct pingpong_context *ctx,uint64_t wrid,int n,int flow_id)
{
	if(n == 0)
		return 0;
		
	struct ibv_sge list;
	memset(&list,0,sizeof(list));
	list.addr = use_dm ? 0 : (uintptr_t) ctx->buf;
	list.length =  mmin(ctx->size, MAX_SGE_LEN);
	list.lkey	= ctx->mr->lkey;

	struct ibv_send_wr wr;
	memset(&wr,0,sizeof(wr));
	wr.wr_id	  = pack_flow_seq_to_wrid(flow_id,wrid ++ );
	wr.sg_list    = &list;
	wr.num_sge    = 1;
	wr.opcode     = IBV_WR_SEND;
	wr.send_flags = IBV_SEND_SIGNALED;
	
	struct ibv_send_wr *bad_wr;
	
	// this function will cost 1.x usd,so instead of using it every time,we use cpu_cycle to calculate the time 
	// for the rest post_send
	
	uint64_t time_stamp_for_all = get_hard_ware_time(ctx->context);
	for(int i=0;i<n;i++)
	{
		int ret = ibv_post_send(ctx->qp, &wr, &bad_wr);
		if(ret)
		{
			strerror(ret);
			perror("can not post send and because ...");
			return i;
		}
		/* 
			the variables below should be done as soon as the wr get into qp ,it should be done at leat
			before the cqe returns,well ,sometimes it will be a lillte bit late....
		*/
		std::get<1>(window_parameter::cc_context[flow_id].cc_cache_.at(wr.wr_id%window_parameter::round_bitmap_len)) = time_stamp_for_all + i * WR_SEQUNLIZE_TIME;
		window_parameter::cc_context[flow_id].infight_segment_num_ ++;
		std::get<0>(window_parameter::cc_context[flow_id].cc_cache_.at(wr.wr_id%window_parameter::round_bitmap_len)) = false;
		wr.wr_id = pack_flow_seq_to_wrid(flow_id, wrid++ );
	}
	return n;
}

static void usage(const char *argv0)
{
	printf("Usage:\n");
	printf("  %s            start a server and wait for connection\n", argv0);
	printf("  %s <host>     connect to server at <host>\n", argv0);
	printf("\n");
	printf("Options:\n");
	printf("  -p, --port=<port>         listen on/connect to port <port> (default 18515)\n");
	printf("  -d, --ib-dev=<dev>        use IB device <dev> (default first device found)\n");
	printf("  -i, --ib-port=<port>      use port <port> of IB device (default 1)\n");
	printf("  -s, --size=<size>         size of message to exchange (default 4096)\n");
	printf("  -m, --mtu=<size>          path MTU (default 1024)\n");
	printf("  -r, --rx-depth=<dep>      number of receives to post at a time (default 500)\n");
	printf("  -n, --iters=<iters>       number of exchanges (default 1000)\n");
	printf("  -l, --sl=<sl>             service level value\n");
	printf("  -e, --events              sleep on CQ events (default poll)\n");
	printf("  -g, --gid-idx=<gid index> local port gid index\n");
	printf("  -c, --contiguous-mr       use contiguous mr\n");
	printf("  -t, --inline-recv=<size>  size of inline-recv\n");
	printf("  -x, --using timestamp  timestamping on cqe\n");
	printf("  -a, --check-nop	    check NOP opcode\n");
	printf("  -o, --odp		    use on demand paging\n");
	printf("  -u, --upstream            use upstream API\n");
	printf("  -t, --upstream            use upstream API\n");
	printf("  -z, --contig_addr         use specifix addr for contig pages MR, must use with -c flag\n");
	printf("  -b, --ooo                 enable multipath processing\n");
	printf("  -j, --memic         	    use device memory\n");
}

int send_nop(struct pingpong_context *ctx)
{
	struct ibv_exp_send_wr *bad_wr;
	struct ibv_exp_send_wr wr;
	memset(&wr,0,sizeof(wr));
	struct ibv_exp_wc wc;
	memset(&wc,0,sizeof(wc));
	int err;
	int n;

	memset(&wr, 0, sizeof(wr));

	wr.wr_id		= PINGPONG_SEND_WRID;
	wr.num_sge		= 0;
	wr.exp_opcode		= IBV_EXP_WR_NOP;
	wr.exp_send_flags	= IBV_EXP_SEND_SIGNALED;

	err = ibv_exp_post_send(ctx->qp, &wr, &bad_wr);
	if (err) {
		fprintf(stderr, "post nop failed\n");
		return err;
	}

	do {
		n = ibv_exp_poll_cq(ctx->cq, 1, &wc, sizeof(wc));
		if (n < 0) {
			fprintf(stderr, "poll CQ failed %d\n", n);
			return -1;
		}
	} while (!n);

	if (wc.status != IBV_WC_SUCCESS) {
		fprintf(stderr, "completion with error %d\n", wc.status);
		return -1;
	}

	return 0;
}
unsigned long get_cqe_time(struct ibv_context * temp_context,struct ibv_exp_wc wc)
{
	struct ibv_exp_clock_info c_info;
	memset(&c_info,0,sizeof(c_info));
	struct  ibv_exp_values queried_values_clock_info;
	memset(&queried_values_clock_info, 0, sizeof(queried_values_clock_info));
	int ret = ibv_exp_query_values(temp_context, IBV_EXP_VALUES_CLOCK_INFO, &queried_values_clock_info);
	if (!ret && queried_values_clock_info.comp_mask & IBV_EXP_VALUES_CLOCK_INFO)
	{
		c_info = queried_values_clock_info.clock_info;
	}
	else{
		printf("con not get clock_info\n");
		return 0;
	}

	unsigned long cqe_ns;
	if(!(wc.exp_wc_flags & IBV_EXP_WC_WITH_TIMESTAMP))
	{
		printf("can not get cqe timestamp\n");
		return 0;
	}
	else{
		//printf("cqe timestamp is %ld\n",wc[i].timestamp);
		cqe_ns =  ibv_exp_cqe_ts_to_ns(&c_info,wc.timestamp);
		return cqe_ns;
	}
}

unsigned long get_hard_ware_time(struct ibv_context * temp_context)
{
	struct ibv_exp_clock_info c_info;
	memset(&c_info,0,sizeof(c_info));
	struct  ibv_exp_values queried_values_clock_info;
	memset(&queried_values_clock_info, 0, sizeof(queried_values_clock_info));
	int ret = ibv_exp_query_values(temp_context, IBV_EXP_VALUES_CLOCK_INFO, &queried_values_clock_info);
	if (!ret && queried_values_clock_info.comp_mask & IBV_EXP_VALUES_CLOCK_INFO)
	{
		c_info = queried_values_clock_info.clock_info;
	}
	else{
		printf("con not get clock_info\n");
		return 0;
	}

	unsigned long queried_time_ns;
	struct  ibv_exp_values queried_values;
	memset(&queried_values, 0, sizeof(queried_values));
	ret = ibv_exp_query_values(temp_context, IBV_EXP_VALUES_HW_CLOCK, &queried_values);
	if(!ret && queried_values.comp_mask & IBV_EXP_VALUES_HW_CLOCK)
	{
		unsigned long  queried_time_stamp = queried_values.hwclock;
		queried_time_ns = ibv_exp_cqe_ts_to_ns(&c_info,queried_time_stamp);
		return queried_time_ns;
	}
	else{
		printf("can not get hard ware time\n");
		return 0;
	}
	
}


// int main(int argc, char *argv[])
// {
// 	struct ibv_device      **dev_list;
// 	struct ibv_device	*ib_dev;
// 	struct pingpong_context *ctx;
// 	struct pingpong_dest     my_dest;
// 	struct pingpong_dest    *rem_dest;
// 	struct timeval           start, end;
// 	char                    *ib_devname = NULL;
// 	char                    *servername = NULL;
// 	int                      port = 18515;
// 	int                      ib_port = 1;
// 	unsigned long long       size = 4096;
// 	enum ibv_mtu		 mtu = IBV_MTU_1024;
// 	int                      rx_depth = 500;
// 	int                      iters = 1000;
// 	int                      use_event = 0;
// 	int                      routs;
// 	int                      rcnt, scnt;
// 	int                      num_cq_events = 0;
// 	int                      sl = 0;
// 	int			 gidx = -1;
// 	char			 gid[INET6_ADDRSTRLEN];
// 	int                      inlr_recv = 0;         //inline receive 在小数据模式中，将受到数据直接放在cqe中
// 	int			 check_nop = 0;
// 	int			 err;

// 	srand48(getpid() * time(NULL));
// 	contig_addr = NULL;

// 	while (1) {
// 		int c;

// 		static struct option long_options[] = {
// 			{ .name = "time-stamp",    .has_arg = 0, .val = 'x' },
// 			{ .name = "port",          .has_arg = 1, .val = 'p' },
// 			{ .name = "ib-dev",        .has_arg = 1, .val = 'd' },
// 			{ .name = "ib-port",       .has_arg = 1, .val = 'i' },
// 			{ .name = "size",          .has_arg = 1, .val = 's' },
// 			{ .name = "mtu",           .has_arg = 1, .val = 'm' },
// 			{ .name = "rx-depth",      .has_arg = 1, .val = 'r' },
// 			{ .name = "iters",         .has_arg = 1, .val = 'n' },
// 			{ .name = "sl",            .has_arg = 1, .val = 'l' },
// 			{ .name = "events",        .has_arg = 0, .val = 'e' },
// 			{ .name = "gid-idx",       .has_arg = 1, .val = 'g' },
// 			{ .name = "contiguous-mr", .has_arg = 0, .val = 'c' },
// 			{ .name = "inline-recv",   .has_arg = 1, .val = 't' },
// 			{ .name = "check-nop",	   .has_arg = 0, .val = 'a' },
// 			{ .name = "odp",           .has_arg = 0, .val = 'o' },
// 			{ .name = "upstream",      .has_arg = 0, .val = 'u' },
// 			{ .name = "contig_addr",   .has_arg = 1, .val = 'z' },
// 			{ .name = "ooo",           .has_arg = 0, .val = 'b' },
// 			{ .name = "memic",         .has_arg = 0, .val = 'j' },
// 			{ 0 }
// 		};

// 		c = getopt_long(argc, argv, "xp:d:i:s:m:r:n:l:ecg:t:ajouz:",
// 				long_options, NULL);
// 		if (c == -1)
// 			break;

// 		switch (c) {
// 		case 'x':
// 			use_ts = 1;
// 			break;
// 		case 'j':
// 			use_dm = 1;
// 			break;
// 		case 'p':
// 			port = strtol(optarg, NULL, 0);
// 			if (port < 0 || port > 65535) {
// 				usage(argv[0]);
// 				return 1;
// 			}
// 			break;

// 		case 'd':
// 			ib_devname = strdupa(optarg);
// 			break;

// 		case 'i':
// 			ib_port = strtol(optarg, NULL, 0);
// 			if (ib_port < 0) {
// 				usage(argv[0]);
// 				return 1;
// 			}
// 			break;

// 		case 's':
// 			size = strtoll(optarg, NULL, 0);
// 			break;

// 		case 'm':
// 			mtu = pp_mtu_to_enum(strtol(optarg, NULL, 0));
// 			if (mtu < 0) {
// 				usage(argv[0]);
// 				return 1;
// 			}
// 			break;

// 		case 'r':
// 			rx_depth = strtol(optarg, NULL, 0);
// 			break;

// 		case 'n':
// 			iters = strtol(optarg, NULL, 0);
// 			break;

// 		case 'l':
// 			sl = strtol(optarg, NULL, 0);
// 			break;

// 		case 'e':
// 			++use_event;
// 			break;

// 		case 'g':
// 			gidx = strtol(optarg, NULL, 0);
// 			break;

// 		case 'c':
// 			++use_contiguous_mr;
// 			break;

// 		case 't':
// 			inlr_recv = strtol(optarg, NULL, 0);
// 			break;

// 		case 'a':
// 			check_nop = 1;
// 			break;

// 		case 'o':
// 			use_odp = 1;
// 			break;

// 		case 'u':
// 			use_upstream = 1;
// 			break;
// 		case 'z':
// 			contig_addr = (void *)(uintptr_t)strtol(optarg, NULL, 0);
// 			break;
// 		case 'b':
// 			use_ooo = 1;
// 			break;
// 		default:
// 			usage(argv[0]);
// 			return 1;
// 		}
// 	}

// 	if (optind == argc - 1)
// 		servername = strdupa(argv[optind]);
// 	else if (optind < argc) {
// 		usage(argv[0]);
// 		return 1;
// 	}

// 	if (contig_addr && !use_contiguous_mr) {
// 		usage(argv[0]);
// 		return 1;
// 	}

// 	if (use_dm && (use_contiguous_mr || use_odp)) {
// 		fprintf(stderr, "Can't use device memory with on-demand paging or contiguous mr\n");
// 		return 1;
// 	}
// 	page_size = sysconf(_SC_PAGESIZE);

// 	dev_list = ibv_get_device_list(NULL);
// 	if (!dev_list) {
// 		perror("Failed to get IB devices list");
// 		return 1;
// 	}

// 	if (!ib_devname) {
// 		ib_dev = *dev_list;
// 		if (!ib_dev) {
// 			fprintf(stderr, "No IB devices found\n");
// 			return 1;
// 		}
// 	} else {
// 		int i;
// 		for (i = 0; dev_list[i]; ++i)
// 			if (!strcmp(ibv_get_device_name(dev_list[i]), ib_devname))
// 				break;
// 		ib_dev = dev_list[i];
// 		if (!ib_dev) {
// 			fprintf(stderr, "IB device %s not found\n", ib_devname);
// 			return 1;
// 		}
// 	}

// 	/// 设置所有的内存参数设置，内存开辟，并且qp设置状态为init
// 	ctx = pp_init_ctx(ib_dev, size, rx_depth, ib_port, use_event, inlr_recv);
// 	if (!ctx)
// 		return 1;

// 	routs = pp_post_recv(ctx, ctx->rx_depth);
// 	if (routs < ctx->rx_depth) {
// 		fprintf(stderr, "Couldn't post receive (%d)\n", routs);
// 		return 1;
// 	}

// 	if (use_event)
// 		if (ibv_req_notify_cq(ctx->cq, 0)) {
// 			fprintf(stderr, "Couldn't request CQ notification\n");
// 			return 1;
// 		}


// 	if (pp_get_port_info(ctx->context, ib_port, &ctx->portinfo)) {
// 		fprintf(stderr, "Couldn't get port info\n");
// 		return 1;
// 	}

// 	my_dest.lid = ctx->portinfo.lid;
// 	if (ctx->portinfo.link_layer != IBV_LINK_LAYER_ETHERNET &&
// 							!my_dest.lid) {
// 		fprintf(stderr, "Couldn't get local LID\n");
// 		return 1;
// 	}

// 	if (gidx >= 0) {
// 		if (ibv_query_gid(ctx->context, ib_port, gidx, &my_dest.gid)) {
// 			fprintf(stderr, "can't read sgid of index %d\n", gidx);
// 			return 1;
// 		}
// 	} else
// 		memset(&my_dest.gid, 0, sizeof my_dest.gid);

// 	my_dest.qpn = ctx->qp->qp_num;
// 	my_dest.psn = lrand48() & 0xffffff;
// 	inet_ntop(AF_INET6, &my_dest.gid, gid, sizeof gid);
// 	printf("  local address:  LID 0x%04x, QPN 0x%06x, PSN 0x%06x, GID %s\n",
// 	       my_dest.lid, my_dest.qpn, my_dest.psn, gid);


// 	/// 如果是客户端就拿到对方信息就好
// 	if (servername)
// 		rem_dest = pp_client_exch_dest(servername, port, &my_dest);
// 	/// 如果是服务器拿到对方的消息之后还要设置自己的qp状态到最新的状态
// 	else
// 		rem_dest = pp_server_exch_dest(ctx, ib_port, mtu, port, sl,
// 								&my_dest, gidx);

// 	if (!rem_dest)
// 		return 1;

// 	inet_ntop(AF_INET6, &rem_dest->gid, gid, sizeof gid);
// 	printf("  remote address: LID 0x%04x, QPN 0x%06x, PSN 0x%06x, GID %s\n",
// 	       rem_dest->lid, rem_dest->qpn, rem_dest->psn, gid);

// 	if (servername)
// 		if (pp_connect_ctx(ctx, ib_port, my_dest.psn, mtu, sl, rem_dest,
// 					gidx))
// 			return 1;

// 	ctx->pending = PINGPONG_RECV_WRID;

// 	if (servername) {
// 		if (check_nop) {
// 			err = send_nop(ctx);
// 			if (err) {
// 				fprintf(stderr, "nop operation failed\n");
// 				return err;
// 			}
// 		}

// 		if (pp_post_send(ctx)) {
// 			fprintf(stderr, "Couldn't post send\n");
// 			return 1;
// 		}
// 		ctx->pending |= PINGPONG_SEND_WRID;
// 	}

// 	if (gettimeofday(&start, NULL)) {
// 		perror("gettimeofday");
// 		return 1;
// 	}
	
// 	/// 是否支持时间戳
// 	if(use_ts){
// 		struct ibv_exp_device_attr attr;
// 		memset(&attr, 0, sizeof(attr));
// 		attr.comp_mask |= IBV_EXP_DEVICE_ATTR_WITH_TIMESTAMP_MASK;
// 		int ret = ibv_exp_query_device(ctx->context, &attr);
// 		if(ret){
// 			printf("query get an error\n");
// 			return 1;
// 		}
// 		if (attr.comp_mask & IBV_EXP_DEVICE_ATTR_WITH_TIMESTAMP_MASK) {
// 		if (!attr.timestamp_mask) {
// 				/* Time stamping is supported with mask attr.timestamp_mask */
// 				printf("timestamp is not sopported\n");
// 				return 1;
// 			}
// 		}
// 	}
	


// 	rcnt = scnt = 0;
// 	while (rcnt < iters || scnt < iters) {
// 		if (use_event) {
// 			struct ibv_cq *ev_cq;
// 			void          *ev_ctx;

// 			if (ibv_get_cq_event(ctx->channel, &ev_cq, &ev_ctx)) {
// 				fprintf(stderr, "Failed to get cq_event\n");
// 				return 1;
// 			}

// 			++num_cq_events;

// 			if (ev_cq != ctx->cq) {
// 				fprintf(stderr, "CQ event for unknown CQ %p\n", ev_cq);
// 				return 1;
// 			}

// 			if (ibv_req_notify_cq(ctx->cq, 0)) {
// 				fprintf(stderr, "Couldn't request CQ notification\n");
// 				return 1;
// 			}
// 		}

// 		{
// 			struct ibv_exp_wc wc[2];
// 			int ne, i;

// 			do {
// 				ne = ibv_exp_poll_cq(ctx->cq, 2, wc, sizeof(wc[0]));
// 				if (ne < 0) {
// 					fprintf(stderr, "poll CQ failed %d\n", ne);
// 					return 1;
// 				}
// 			} while (!use_event && ne < 1);

// 			for (i = 0; i < ne; ++i) {
				
// 				if (wc[i].status != IBV_WC_SUCCESS) {
// 					fprintf(stderr, "Failed status %s (%d) for wr_id %d\n",
// 						ibv_wc_status_str(wc[i].status),
// 						wc[i].status, (int) wc[i].wr_id);
// 					return 1;
// 				}

// 				switch ((int) wc[i].wr_id) {
// 				case PINGPONG_SEND_WRID:
// 					/// 如果是send ceq的话获取ceq的时间戳
// 					/// 如果
// 					if(use_ts){

// 						printf("hard ware time is %ld ns\n",get_hard_ware_time(ctx->context));
// 						printf("work comp time is %ld ns\n",get_cqe_time(ctx->context,wc[i]));
// 					}
// 					++scnt;
// 					break;

// 				case PINGPONG_RECV_WRID:
// 					if (--routs <= 1) {
// 						routs += pp_post_recv(ctx, ctx->rx_depth - routs);
// 						if (routs < ctx->rx_depth) {
// 							fprintf(stderr,
// 								"Couldn't post receive (%d)\n",
// 								routs);
// 							return 1;
// 						}
// 					}

// 					++rcnt;
// 					break;

// 				default:
// 					fprintf(stderr, "Completion for unknown wr_id %d\n",
// 						(int) wc[i].wr_id);
// 					return 1;
// 				}

// 				ctx->pending &= ~(int) wc[i].wr_id;
// 				if (scnt < iters && !ctx->pending) {
// 					if (pp_post_send(ctx)) {
// 						fprintf(stderr, "Couldn't post send\n");
// 						return 1;
// 					}
// 					ctx->pending = PINGPONG_RECV_WRID |
// 						       PINGPONG_SEND_WRID;
// 				}
// 			}
// 		}
// 	}

// 	if (gettimeofday(&end, NULL)) {
// 		perror("gettimeofday");
// 		return 1;
// 	}

// 	{
// 		float usec = (end.tv_sec - start.tv_sec) * 1000000 +
// 			(end.tv_usec - start.tv_usec);
// 		long long bytes = (long long) size * iters * 2;

// 		printf("%lld bytes in %.2f seconds = %.2f Mbit/sec\n",
// 		       bytes, usec / 1000000., bytes * 8. / usec);
// 		printf("%d iters in %.2f seconds = %.2f usec/iter\n",
// 		       iters, usec / 1000000., usec / iters);
// 	}

// 	ibv_ack_cq_events(ctx->cq, num_cq_events);

// 	if (pp_close_ctx(ctx))
// 		return 1;

// 	ibv_free_device_list(dev_list);
// 	free(rem_dest);

// 	return 0;
// }

#endif