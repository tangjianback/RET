/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
* Copyright (c) 2007,2008,2009 INRIA, UDCAST
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation;
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
* Author: Amine Ismail <amine.ismail@sophia.inria.fr>
*                      <amine.ismail@udcast.com>
*/
#include "ns3/log.h"
#include "ns3/ipv4-address.h"
#include "ns3/nstime.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/random-variable.h"
#include "ns3/qbb-net-device.h"
#include "ns3/ipv4-end-point.h"
#include "timely-sender.h"
#include "ns3/seq-ts-header.h"
#include <stdlib.h>
#include <stdio.h>

namespace ns3 {

	NS_LOG_COMPONENT_DEFINE("TimelySender");
	NS_OBJECT_ENSURE_REGISTERED(TimelySender);

	TypeId
		TimelySender::GetTypeId(void)
	{
		static TypeId tid = TypeId("ns3::TimelySender")
			.SetParent<Application>()

			.AddConstructor<TimelySender>()

			.AddAttribute("MaxPackets",
				"The maximum number of packets the application will send",
				UintegerValue(100),
				MakeUintegerAccessor(&TimelySender::m_allowed),
				MakeUintegerChecker<uint32_t>())

			.AddAttribute("RemoteAddress",
				"The destination Address of the outbound packets",
				AddressValue(),
				MakeAddressAccessor(&TimelySender::m_peerAddress),
				MakeAddressChecker())

			.AddAttribute("RemotePort", "The destination port of the outbound packets",
				UintegerValue(100),
				MakeUintegerAccessor(&TimelySender::m_peerPort),
				MakeUintegerChecker<uint16_t>())

			.AddAttribute("PriorityGroup", "The priority group of this flow",
				UintegerValue(0),
				MakeUintegerAccessor(&TimelySender::m_pg),
				MakeUintegerChecker<uint16_t>())

			.AddAttribute("PacketSize",
				"Size of packets generated. The minimum packet size is 14 bytes which is the size of the header carrying the sequence number and the time stamp.",
				UintegerValue(1000),
				MakeUintegerAccessor(&TimelySender::m_pktSize),
				MakeUintegerChecker<uint32_t>(14, 1500))
			.AddAttribute("flow_id",
				"flow_id",
				UintegerValue(1000),
				MakeUintegerAccessor(&TimelySender::flow_id),
				MakeUintegerChecker<uint32_t>())

			.AddAttribute("InitialRate",
				"Initial send rate in bits per second",
				DoubleValue(1.0 * 1000 * 1000 * 1000),
				MakeDoubleAccessor(&TimelySender::m_initRate),
				MakeDoubleChecker<double>())

			.AddAttribute("LinkSpeed",
				"Bottleneck link speed in bits per second",
				DoubleValue(10.0 * 1000 * 1000 * 1000),
				MakeDoubleAccessor(&TimelySender::m_C),
				MakeDoubleChecker<double>())

			.AddAttribute("Delta",
				"Additive increase in bits per second",
				UintegerValue(10 * 1000 * 1000),
				MakeUintegerAccessor(&TimelySender::m_delta),
				MakeUintegerChecker<uint32_t>())

			.AddAttribute("T_high",
				"T_high threshold in microseconds",
				DoubleValue(500.0 / (1000 * 1000)),
				MakeDoubleAccessor(&TimelySender::m_t_high),
				MakeDoubleChecker<double>())

			.AddAttribute("T_low",
				"T_low threshold in microseconds",
				DoubleValue(50.0 / (1000 * 1000)),
				MakeDoubleAccessor(&TimelySender::m_t_low),
				MakeDoubleChecker<double>())

			.AddAttribute("Min RTT",
				"MIN RTT in microseconds",
				DoubleValue(20.0 / (1000 * 1000)),
				MakeDoubleAccessor(&TimelySender::m_min_rtt),
				MakeDoubleChecker<double>())

			.AddAttribute("Beta",
				"Beta",
				DoubleValue(0.8),
				MakeDoubleAccessor(&TimelySender::m_beta),
				MakeDoubleChecker<double>())

			.AddAttribute("Alpha",
				"Alpha",
				DoubleValue(0.875),
				MakeDoubleAccessor(&TimelySender::m_alpha),
				MakeDoubleChecker<double>())


			.AddAttribute("BurstSize",
				"BurstSize",
				UintegerValue(16000),
				MakeUintegerAccessor(&TimelySender::m_burstSize),
				MakeUintegerChecker<uint32_t>())

			.AddAttribute("DestNo",
				"DestNo",
				UintegerValue(0),
				MakeUintegerAccessor(&TimelySender::DestNo),
				MakeUintegerChecker<uint32_t>())

			.AddAttribute("MinRateMultiple",
				"MinRateMultiple",
				DoubleValue(0.01),
				MakeDoubleAccessor(&TimelySender::m_minRateMultiple),
				MakeDoubleChecker<double>())

			.AddAttribute("MaxRateMultiple",
				"MaxRateMultiple",
				DoubleValue(0.96),
				MakeDoubleAccessor(&TimelySender::m_maxRateMultiple),
				MakeDoubleChecker<double>());

		return tid;
	}
	uint32_t TimelySender::trace_node = 0;
	uint32_t TimelySender::fct_num = 0;
	TimelySender::TimelySender()
	{
		NS_LOG_FUNCTION_NOARGS();
	}

	void
		TimelySender::Init()
	{
		
		m_sent = 0;
		m_socket = 0;
		m_sendEvent = EventId();

		m_rate = m_initRate;
		m_burst_in_packets = (int)(m_burstSize / m_pktSize);
		m_sleep = GetBurstDuration(m_rate);
		m_rtt_diff = 0;
		m_received = 0;
		m_sdel = GetBurstDuration(1, m_C);
		m_prev_rtt = m_sdel;
		m_maxRate = m_maxRateMultiple * m_C;
		m_minRate = m_minRateMultiple * m_C;
		m_N = 1;

		///下面是唐剑的东西
		///1)常量 
		Mypriority = 1.0;

		/* 调节的参数
		发送端的参数主要是ai ,decrease
		ai 1
		b 0.5
		b_max  0.4
		b_gradient 0.8
		*/
		tj_increat_ai_forfabric = 1.0;
		tj_decreat_b_forfabric = 0.5;
		tj_decreat_b_max_forfabric = 0.4;
		tj_decreat_gradient = 0.8;

		/*

		target函数的主要参数
		farange   50000
		fs_min 0.01
		fs_max 30.0

		*/
		tj_min_window_forall = 0.001;
		tj_max_window_forall = 40;
		tj_fs_min_cwnd = 0.01;
		tj_fs_max_cwnd = 30.0;
		tj_fs_range = 15000;
		tj_fs_very_base_rtt = 4500;
		tj_fs_base_target = 4500;
		tj_vague_var = 0;
		last_update = 0;

		tj_fs_alfa = (double)tj_fs_range / ((1 / sqrt(tj_fs_min_cwnd)) - (1 / sqrt(tj_fs_max_cwnd)));
		tj_fs_beta = -tj_fs_alfa / sqrt(tj_fs_max_cwnd);

		///2)变量
		tj_window_now_forfabric = m_initRate / Mypriority;
		tj_inflight_packet = 0;
		tj_vague_rtt = 5000;
		stop_reach = false;
		start_time = Simulator::Now().GetInteger();
		tj_last_time_decrease_forfabric = Simulator::Now().GetInteger();
		tj_delay_now_forfabric = Get_targetdelay_forfabric();

		tj_first_pacing = 1;

		////dcqcn(rttbased)
		rtt_min = 10000;
		rtt_max = 50000;
		window_di_dcqcn = 0.1;
		window_ai_dcqcn = 1.0;

		

		///tangjian
		m_node->GetId();
		uint32_t from_tj = m_node->GetId();
		uint32_t to_tj = DestNo;

		//// 计算自己所属的机架
		//uint32_t from_rack = 0;
		//uint32_t to_rack = 0;
		//if (from_tj <= 41)
		//	from_rack = 0;
		//else if (from_tj <= 65)
		//	from_rack = 1;
		//else if (from_tj <= 89)
		//	from_rack = 2;
		//else if (from_tj <= 113)
		//	from_rack = 3;
		//else if (from_tj <= 137)
		//	from_rack = 4;
		//else if (from_tj <= 161)
		//	from_rack = 5;
		//else if (from_tj <= 185)
		//	from_rack = 6;
		//else if (from_tj <= 209)
		//	from_rack = 7;
		//else if (from_tj <= 233)
		//	from_rack = 8;
		//else if (from_tj <= 257)
		//	from_rack = 9;
		//else if (from_tj <= 281)
		//	from_rack = 10;
		//else
		//	from_rack = 11;

		//if (to_tj <= 41)
		//	to_rack = 0;
		//else if (to_tj <= 65)
		//	to_rack = 1;
		//else if (to_tj <= 89)
		//	to_rack = 2;
		//else if (to_tj <= 113)
		//	to_rack = 3;
		//else if (to_tj <= 137)
		//	to_rack = 4;
		//else if (to_tj <= 161)
		//	to_rack = 5;
		//else if (to_tj <= 185)
		//	to_rack = 6;
		//else if (to_tj <= 209)
		//	to_rack = 7;
		//else if (to_tj <= 233)
		//	to_rack = 8;
		//else if (to_tj <= 257)
		//	to_rack = 9;
		//else if (to_tj <= 281)
		//	to_rack = 10;
		//else
		//	to_rack = 11;

		//if (from_rack == to_rack)
		//{
		//	tj_fs_base_target = 5000;
		//	tj_fs_very_base_rtt = 5000;
		//}
		//	
		//else {
		//	tj_fs_base_target = 10000;
		//	tj_fs_very_base_rtt = 10000;
		//}
			tj_fs_base_target = 4230;
			tj_fs_very_base_rtt = 4230;
		////std::cout << "base target:" << tj_fs_base_target << std::endl;
		//Print_alre = 0;
		
		

	}

	double
		TimelySender::GetBurstDuration(double rate)
	{
		return (double)m_burst_in_packets * m_pktSize * 8.0 / rate;
	}

	double
		TimelySender::GetBurstDuration(int packets, double rate)
	{
		return (double)packets * m_pktSize * 8.0 / rate;
	}

	TimelySender::~TimelySender()
	{
		NS_LOG_FUNCTION_NOARGS();
	}

	void
		TimelySender::SetRemote(Ipv4Address ip, uint16_t port)
	{
		m_peerAddress = Address(ip);
		m_peerPort = port;
	}

	void
		TimelySender::SetRemote(Ipv6Address ip, uint16_t port)
	{
		m_peerAddress = Address(ip);
		m_peerPort = port;
	}

	void
		TimelySender::SetRemote(Address ip, uint16_t port)
	{
		m_peerAddress = ip;
		m_peerPort = port;
	}

	void
		TimelySender::DoDispose(void)
	{
		NS_LOG_FUNCTION_NOARGS();
		Application::DoDispose();
	}

	void
		TimelySender::StartApplication(void)
	{
		//std::cout << Simulator::Now() << " " << flow_id << " start" << std::endl;
		NS_LOG_FUNCTION_NOARGS();

		Init();

		if (m_socket == 0)
		{
			TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
			m_socket = Socket::CreateSocket(GetNode(), tid);
			if (Ipv4Address::IsMatchingType(m_peerAddress) == true)
			{
				m_socket->Bind();
				m_socket->Connect(InetSocketAddress(Ipv4Address::ConvertFrom(m_peerAddress), m_peerPort));
			}
			else if (Ipv6Address::IsMatchingType(m_peerAddress) == true)
			{
				m_socket->Bind6();
				m_socket->Connect(Inet6SocketAddress(Ipv6Address::ConvertFrom(m_peerAddress), m_peerPort));
			}
		}

		m_socket->SetRecvCallback(MakeCallback(&TimelySender::Receive, this));
		m_sendEvent = Simulator::Schedule(Seconds(0.0), &TimelySender::Send, this);
		//throughput_debug = Simulator::Schedule(MicroSeconds(10), &TimelySender::showthroughput, this);
	}

	void
		TimelySender::StopApplication()
	{
		NS_LOG_FUNCTION_NOARGS();
		if (m_sendEvent.IsRunning()) {
			m_sendEvent.Cancel();
		}
		stop_reach = true;
		//Simulator::Cancel(m_sendEvent);
	}
	double TimelySender::real_window_for_send()
	{
		return tj_window_now_forfabric;
		//std::cout << ((double)tj_delay_now_forfabric / (double)(tj_fs_base_target + tj_delay_now_forfabric - tj_fs_base_target_individual)) << std::endl;
		/*if (m_received == 0)
			return tj_window_now_forfabric;
		else
			return tj_window_now_forfabric * ((double)tj_delay_now_forfabric / (double)(tj_fs_very_base_rtt + tj_delay_now_forfabric - tj_fs_base_target));
		*/
	}

	void TimelySender::Send()
	{
		/*if (m_sent == 0)
		std::cout << m_allowed << " start time:" << Simulator::Now().GetInteger() << std::endl;*/
		NS_ASSERT(m_sendEvent.IsExpired());
		if (m_sent < m_allowed )
		{
			/*if (Simulator::Now().GetSeconds() >= 2.3 && flow_id < 450)
				return;*/

			if (real_window_for_send() * Mypriority >= 1)
			{
				SendBurst();
				tj_first_pacing = 1;
			}

			else
			{
				if (tj_first_pacing == 1)
				{
					tj_first_pacing = 0;
				}
				else
				{
					SendBurst();
				}
				m_sendEvent = Simulator::Schedule(NanoSeconds(tj_vague_rtt / real_window_for_send() * Mypriority), &TimelySender::Send, this);
				
			}
		}


	}
	void TimelySender::showthroughput()
	{
		if (m_received < m_allowed) {
			std::cout << flow_id << "throughput " << tj_window_now_forfabric * 1052 * 8 / tj_vague_rtt << std::endl;
			throughput_debug = Simulator::Schedule(MicroSeconds(1), &TimelySender::showthroughput, this);
		}
				
	}

	void
		TimelySender::SendBurst()
	{
		NS_LOG_FUNCTION_NOARGS();
		int should_send;
		should_send = real_window_for_send() * Mypriority - tj_inflight_packet;
		double should_send_temp = real_window_for_send() * Mypriority - tj_inflight_packet;



		if (real_window_for_send() * Mypriority < 1 && should_send_temp >0)
			should_send = 1;

		if (should_send <= 0)
			should_send = 0;

		for (int i = 0; i < should_send && m_sent < m_allowed; i++)
		{
			SendPacket(i);
		}
		tj_inflight_packet += should_send;
	}

	void
		TimelySender::SendPacket(int no)
	{

		SeqTsHeader seqTs;

		seqTs.SetSeq(m_sent);

		seqTs.SetPG(m_pg);

		
		seqTs.SetTargetAsUint64(520);

		seqTs.SetAckNeeded();
		Ptr<Packet> p = Create<Packet>(m_pktSize);

		p->AddHeader(seqTs);
		if (m_socket->Send(p) >= 0)
		{
			m_sent++;
		}
		else
		{
			NS_LOG_INFO("Error while sending");
			exit(-1);
		}
	}

	void
		TimelySender::SetPG(uint16_t pg)
	{
		m_pg = pg;
		return;
	}

	void
		TimelySender::Receive(Ptr<Socket> socket)
	{
		// 如果到达了发送数据包的时间点
		if (stop_reach) {
			if (m_sendEvent.IsRunning()) {
				m_sendEvent.Cancel();
			}
			return;
		}

		Ptr<Packet> packet;
		Address from;
		while ((packet = socket->RecvFrom(from)))
		{
			
			
			m_received++;

			int x = packet->GetSize();
			SeqTsHeader seqTs;
			packet->RemoveHeader(seqTs);

			// get the delay for this round
			if(m_received == 1)
				tj_delay_pre_forfabric = Simulator::Now().GetInteger() - seqTs.GetTs().GetInteger() - 210;
			else
				tj_delay_pre_forfabric = tj_delay_now_forfabric;
			
			
			tj_delay_now_forfabric = Simulator::Now().GetInteger() - seqTs.GetTs().GetInteger()- 210;
			
			if (tj_delay_now_forfabric < 4000)
			{
				std::cout << "delay get wrong :" << tj_delay_now_forfabric<< std::endl;
			}
			/*if (int(UniformVariable(0, 1).GetValue() * 1000 >= 999))
			{
				std::cout << "rtt " << tj_delay_now_forfabric << std::endl;
			}*/
			//std::cout << flow_id << "  " << tj_window_now_forfabric << std::endl;
			
			if (m_received == m_allowed && m_allowed < 200)
			{
				fct_num++;
				//std::cout << Simulator::Now().GetNanoSeconds() << " flows complete " << ++fct_num << " flowid " << flow_id <<" flowsize "<<m_allowed << " FCT "<<(Simulator::Now().GetInteger() - start_time) / 1000<< std::endl;
			}
						
			// calculate tj_vague_rtt
			if (m_received == 1)
				tj_vague_rtt = Simulator::Now().GetInteger() - seqTs.GetTs().GetInteger();
			else
				tj_vague_rtt = 0.8 * tj_vague_rtt + 0.2* (Simulator::Now().GetInteger() - seqTs.GetTs().GetInteger());

			//update rtt_diff
			if (m_received == 1)
				tj_vague_diff = 0;
			else {
				tj_vague_diff = 0.8 * (tj_delay_now_forfabric - tj_delay_pre_forfabric)+ 0.2*(tj_vague_diff);
			}

			//update vague_rtt
			if (m_received == 1)
				tj_vague_rtt = tj_delay_now_forfabric;
			else {
				tj_vague_rtt = 0.8 * tj_delay_now_forfabric + 0.2*(tj_vague_rtt);
			}

			
			//tj_target_delay_forfabric = Get_targetdelay_forfabric() + 5000;
			tj_target_delay_forfabric = tj_fs_very_base_rtt + seqTs.GetTargetAsUint64();

			// print the node you want to trace
			if (flow_id <= 20)
			{
				//std::cout << Simulator::Now().GetSeconds() << " " << flow_id << " " << real_window_for_send() << " " << tj_delay_now_forfabric << " " <<tj_vague_rtt<<" "<< tj_target_delay_forfabric <<" "<<tj_vague_diff<< std::endl;
				//std::cout << Simulator::Now().GetSeconds() << " " << m_node->GetId() << " " << DestNo << " " << flow_id << " " << real_window_for_send() << " " << tj_delay_now_forfabric << " " << tj_vague_rtt << " " << tj_target_delay_forfabric << " " << tj_vague_diff << std::endl ;
			}

			UpdateWindow_forfabric();
			//UpdateWIndow_forfabric_dcqcn();

			result_handle();
			//if (m_allowed < 100)
			//{
			//	fct_num += 1;
			//	if (fct_num == 3999)
			//		std::cout << "small flow fct" << std::endl;
			//	//std::cout << Simulator::Now().GetSeconds() << " " << flow_id << " " << real_window_for_send() << " " << tj_delay_now_forfabric << " " <<tj_vague_rtt<<" "<< tj_target_delay_forfabric <<" "<<tj_vague_diff<< std::endl;
			//	//std::cout << " " << real_window_for_send() << std::endl;
			//}
			//result_handle_dcqcn();

			tj_inflight_packet = m_sent - seqTs.GetSeq() - 1;


			/*if (seqTs.GetSeq()+1 == m_allowed)
			std::cout << m_allowed << "  over time:" << Simulator::Now().GetInteger() << std::endl;
			*/
			// if window < 1.0 and this is not the first time to pace
			// we should not call send otherwise we should do
			if (!(real_window_for_send() * Mypriority < 1.0 && tj_first_pacing == 0))
				Send();
		}
	}



	void TimelySender::result_handle()
	{
		//window bound
		if (tj_window_now_forfabric < tj_min_window_forall)
			tj_window_now_forfabric = tj_min_window_forall;

		else if (tj_window_now_forfabric > tj_max_window_forall)
			tj_window_now_forfabric = tj_max_window_forall;

		//mark if it is decreasing
		if (tj_window_now_forfabric < tj_window_pre_forfabric)
			tj_last_time_decrease_forfabric = Simulator::Now().GetInteger();

		
	}

	void TimelySender::UpdateWIndow_forfabric_dcqcn()
	{
		if (Simulator::Now().GetInteger() - last_update < tj_vague_rtt)
			return;
		last_update = Simulator::Now().GetInteger();
		if (tj_delay_now_forfabric < 4500)
			tj_window_now_forfabric = tj_window_now_forfabric + 1;
		else
			tj_window_now_forfabric = tj_window_now_forfabric * (1.0 - (double)(tj_delay_now_forfabric - 4230)/ 4230);
	}



	void TimelySender::UpdateWindow_forfabric()
	{
		//if (m_received >= 2)
		//{
		//	uint64_t rtt_diff_thresh;
		//	rtt_diff_thresh = tj_target_delay_forfabric;

		//	if (tj_vague_diff > (int64_t)rtt_diff_thresh && tj_delay_now_forfabric > tj_target_delay_forfabric)
		//	{
		//		//if(flow_id <= 200)
		//		//std::cout << "enter fengkuang tiaozheng decrease.." << std::endl;
		//		double diff_decrease = tj_decreat_gradient*((double)(tj_vague_diff - rtt_diff_thresh) / (double)tj_vague_diff);
		//		if (diff_decrease < tj_decreat_b_max_forfabric)
		//			tj_window_now_forfabric = (1 - tj_decreat_b_max_forfabric) * tj_window_now_forfabric;
		//		else
		//			tj_window_now_forfabric = (1 - diff_decrease)* tj_window_now_forfabric;
		//		return;
		//	}
		//	if (tj_vague_diff < -(int64_t)rtt_diff_thresh && tj_delay_now_forfabric <= tj_target_delay_forfabric)
		//	{
		//		//std::cout << "enter fengkuang tiaozheng add.." << std::endl;
		//		tj_window_now_forfabric += tj_increat_ai_forfabric;
		//		return;
		//	}
		//}
	


		tj_window_pre_forfabric = tj_window_now_forfabric;
		

		// if current delay is smaller than the target ,we should add
		if (tj_delay_now_forfabric <= tj_target_delay_forfabric)
		{
			double temp_ai_forfabric = tj_increat_ai_forfabric;
			if (tj_window_now_forfabric >= 1)
				tj_window_now_forfabric += temp_ai_forfabric / tj_window_now_forfabric;
			else
				tj_window_now_forfabric += temp_ai_forfabric;
			return;
		}

		//otherwise we should decrease(but only if the pause time is greater than one rtt)
		if (Simulator::Now().GetInteger() - tj_last_time_decrease_forfabric >= tj_vague_rtt)
		{
			
			double normal_decrease = 1.0 - tj_decreat_b_forfabric* ( ((double)tj_delay_now_forfabric - (double)tj_target_delay_forfabric) / (double)tj_delay_now_forfabric);
			double thresh_decrease = 1.0 - tj_decreat_b_max_forfabric;
			//if (flow_id <= 200)
			//std::cout << " normal decrease " << normal_decrease << "  " << thresh_decrease << std::endl;
			tj_window_now_forfabric = (normal_decrease > thresh_decrease ? normal_decrease : thresh_decrease)* tj_window_now_forfabric;
		}

	}

	int64_t TimelySender::Get_targetdelay_forfabric()
	{
		if (tj_window_now_forfabric < tj_fs_min_cwnd)
			return tj_fs_range + tj_fs_base_target;
		else if (tj_window_now_forfabric > tj_fs_max_cwnd)
			return tj_fs_base_target;
		int64_t temp_target = tj_fs_base_target + (int64_t)Max(0, Mix(tj_fs_alfa / sqrt(tj_window_now_forfabric) + tj_fs_beta, tj_fs_range));

		return temp_target;

	}
	int64_t TimelySender::Mix(double a, double b)
	{
		return a < b ? a : b;
	}
	int64_t TimelySender::Max(double a, double b)
	{
		return a > b ? a : b;
	}

} // Namespace ns3