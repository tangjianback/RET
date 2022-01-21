#ifndef TIMELY_SENDER_H
#define TIMELY_SENDER_H

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/ipv4-address.h"
#include <vector>
#include <queue>



namespace ns3 {

	class Socket;
	class Packet;

	/**
	* \ingroup timelysenderreceiver
	* \class Timely sender
	* \brief UDP sender that implements TIMELY protocol.
	*
	*/


	class TimelySender : public Application
	{
	public:
		static TypeId
			GetTypeId(void);

		TimelySender();

		virtual ~TimelySender();

		/**
		* \brief set the remote address and port
		* \param ip remote IP address
		* \param port remote port
		*/
		void SetRemote(Ipv4Address ip, uint16_t port);
		void SetRemote(Ipv6Address ip, uint16_t port);
		void SetRemote(Address ip, uint16_t port);
		void SetPG(uint16_t pg);

		static uint32_t trace_node;
		static uint32_t fct_num;

	protected:
		virtual void DoDispose(void);

	private:

		virtual void StartApplication(void);
		virtual void StopApplication(void);

		void Init();
		//void ScheduleTransmit(Time dt);
		void Send(void);
		void SendBurst();
		//void SendPaced();
		void SendPacket(int no);
		void Receive(Ptr<Socket> socket);
		void UpdateSendRate();
		double GenerateRTTSample(Time ts);
		double GetBurstDuration(double rate);
		double GetBurstDuration(int packets, double rate);
		void UpdateWindow_forfabric();
		void UpdateWIndow_forfabric_dcqcn();
		double real_window_for_send();
		int64_t Get_targetdelay_forfabric();
		int64_t Mix(double a, double b);
		int64_t Max(double a, double b);
		void result_handle();
		void result_handle_dcqcn();
		void showthroughput();

		// Basic networking parameters. 
		Ptr<Socket> m_socket;
		Address m_peerAddress;
		uint16_t m_peerPort;
		EventId m_sendEvent;
		EventId throughput_debug;

		uint16_t m_pg;

		// General paremetrs.
		uint64_t m_allowed; // max packets to send
		uint32_t m_pktSize; // packets size. 
		uint32_t m_burstSize; // we send these many packets in a burst - this is the smallest unit of rate control.

							  // Timely algorithm parameters.
		double m_C;		// link speed in bits per second.
		double m_initRate; // initial sending rate.
		uint32_t m_delta;	// additive increase step in bits per second. 
		double m_t_high;	// t_high in seconds
		double m_t_low;	// t_low in seconds
		double m_min_rtt;	// min rtt in seconds.
		double m_beta;		// beta;
		double m_alpha;		// alpha;
		double m_maxRateMultiple; // the rate cannot exceed this value times m_C. This simulates the fact that the host cannot send faster than link speed, minus header.
		double m_minRateMultiple; // the rate cannot go below this value times m_C. This is mostly a safeguard.

								  // Timely variables.
		double m_rate; // current sending rate.
		double m_prev_rtt; // previous RTT sample.
		double m_new_rtt; // new RTT sample.
		double m_rtt_diff; // new RTT diff
		double m_N; // 5 if we are in HAI mode, 1 otherwise.

					// Bookkeeping
		double m_sleep;
		int m_burst_in_packets;
		uint32_t m_sent;
		uint32_t m_received;
		double m_sdel; // serialization delay.
		double m_maxRate;
		double m_minRate;




		/*siwift ³ÌÐòÊ¹ÓÃµÄ±äÁ¿*/

		double tj_window_pre_forfabric;
		double tj_window_now_forfabric;
		double Mypriority;
		uint64_t tj_inflight_packet;

		int64_t tj_delay_now_forfabric;
		int64_t tj_delay_pre_forfabric;

		int64_t tj_target_delay_forfabric;

		int64_t tj_vague_rtt;
		int64_t tj_vague_var;
		int64_t tj_vague_diff;

		int64_t tj_first_pacing = 1;

		int64_t tj_last_time_decrease_forfabric;

		double tj_fs_min_cwnd;
		double tj_fs_max_cwnd;
		int64_t tj_fs_range;

		int64_t tj_fs_base_target;
		int64_t tj_fs_very_base_rtt;

		double tj_fs_alfa;
		double tj_fs_beta;




		double tj_increat_ai_forfabric;
		double tj_decreat_b_forfabric;
		double tj_decreat_gradient;

		double tj_decreat_b_max_forfabric;
		double tj_min_window_forall;
		double tj_max_window_forall;

		uint32_t DestNo;
		uint32_t Print_alre;
		uint32_t flow_id;
		bool stop_reach;
		uint64_t start_time;


		//dcqcn(rttbased)
		int64_t rtt_max;
		int64_t rtt_min;
		double window_ai_dcqcn;
		double window_di_dcqcn;
		int64_t last_update;

	};

} // namespace ns3

#endif /* TIMELy_SENDER_H */


