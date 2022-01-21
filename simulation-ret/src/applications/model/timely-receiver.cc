/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright 2007 University of Washington
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
 */

#include "ns3/log.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv6-address.h"
#include "ns3/address-utils.h"
#include "ns3/nstime.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/socket.h"
#include "ns3/udp-socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/seq-ts-header.h"

#include "timely-receiver.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TimelyReceiverApplication");
NS_OBJECT_ENSURE_REGISTERED (TimelyReceiver);

TypeId
TimelyReceiver::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TimelyReceiver")
    .SetParent<Application> ()
    .AddConstructor<TimelyReceiver> ()
    .AddAttribute ("Port", "Port on which we listen for incoming packets.",
                   UintegerValue (9),
                   MakeUintegerAccessor (&TimelyReceiver::m_port),
                   MakeUintegerChecker<uint16_t> ())
	.AddAttribute ("PriorityGroup", "The priority group of this flow",
				   UintegerValue (0),
				   MakeUintegerAccessor (&TimelyReceiver::m_pg),
				   MakeUintegerChecker<uint16_t> ())
	.AddAttribute("flow_id","flow_id",
					UintegerValue(1000),
					MakeUintegerAccessor(&TimelyReceiver::flow_id),
					MakeUintegerChecker<uint32_t>())
	  .AddAttribute("max_packet_seq", "max_packet_seq",
		  UintegerValue(1000),
		  MakeUintegerAccessor(&TimelyReceiver::max_packet_seq),
		  MakeUintegerChecker<uint32_t>())
	  .AddAttribute("start_time", "start_time",
		  UintegerValue(1000),
		  MakeUintegerAccessor(&TimelyReceiver::start_time),
		  MakeUintegerChecker<uint64_t>())
	.AddAttribute ("ChunkSize", 
				   "The chunk size can be sent before getting an ack",
				   UintegerValue (1000),
				   MakeUintegerAccessor (&TimelyReceiver::m_chunk),
				   MakeUintegerChecker<uint32_t> ())
  ;
  return tid;
}

 //bool  TimelyReceiver::congestion_point = false;
 //uint64_t TimelyReceiver::target_rtt = 10000;
 //uint64_t TimelyReceiver::lqqmax_rtt = 50000;
 //uint64_t TimelyReceiver::lqqaddfactor = 1 * 842;
 //uint64_t TimelyReceiver::lqqdecreasefactor = 5 * 842;
 //uint32_t TimelyReceiver::decrease_interval_packet_num = 10;
 //uint32_t TimelyReceiver::rate_calculate_window = 20;
 //uint32_t TimelyReceiver::sequelize_time_per_packet = 842;


TimelyReceiver::TimelyReceiver ()
{
	m_received = 0;
	m_sent = 0;
	count = 0;

	NS_LOG_FUNCTION_NOARGS ();
}

TimelyReceiver::~TimelyReceiver()
{
  NS_LOG_FUNCTION_NOARGS ();
  m_socket = 0;
  m_socket6 = 0;
}

void
TimelyReceiver::DoDispose (void)
{
  NS_LOG_FUNCTION_NOARGS ();
  Application::DoDispose ();
}

void 
TimelyReceiver::StartApplication (void)
{
  NS_LOG_FUNCTION_NOARGS ();

  if (m_socket == 0)
    {
      TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
      m_socket = Socket::CreateSocket (GetNode (), tid);
      InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (), m_port);
      m_socket->Bind (local);
      if (addressUtils::IsMulticast (m_local))
        {
          Ptr<UdpSocket> udpSocket = DynamicCast<UdpSocket> (m_socket);
          if (udpSocket)
            {
              // equivalent to setsockopt (MCAST_JOIN_GROUP)
              udpSocket->MulticastJoinGroup (0, m_local);
            }
          else
            {
              NS_FATAL_ERROR ("Error: Failed to join multicast group");
            }
        }
    }

  if (m_socket6 == 0)
    {
      TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
      m_socket6 = Socket::CreateSocket (GetNode (), tid);
      Inet6SocketAddress local6 = Inet6SocketAddress (Ipv6Address::GetAny (), m_port);
      m_socket6->Bind (local6);
      if (addressUtils::IsMulticast (local6))
        {
          Ptr<UdpSocket> udpSocket = DynamicCast<UdpSocket> (m_socket6);
          if (udpSocket)
            {
              // equivalent to setsockopt (MCAST_JOIN_GROUP)
              udpSocket->MulticastJoinGroup (0, local6);
            }
          else
            {
              NS_FATAL_ERROR ("Error: Failed to join multicast group");
            }
        }
    }

  m_socket->SetRecvCallback (MakeCallback (&TimelyReceiver::HandleRead, this));
  m_socket6->SetRecvCallback (MakeCallback (&TimelyReceiver::HandleRead, this));
  //Event_decrease = Simulator::Schedule(NanoSeconds(decrease_interval_packet_num * sequelize_time_per_packet), &TimelyReceiver::target_decrease, this);
 
}

void 
TimelyReceiver::target_decrease()
{
	return;
}









void 
TimelyReceiver::StopApplication ()
{
  NS_LOG_FUNCTION_NOARGS ();

  if (m_socket != 0) 
    {
      m_socket->Close ();
      m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
    }
  if (m_socket6 != 0) 
    {
      m_socket6->Close ();
      m_socket6->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
    }
}

void 
TimelyReceiver::HandleRead(Ptr<Socket> socket)
{
	
	this->GetNode();


	Ptr<Packet> packet;
	Address from;
	while ((packet = socket->RecvFrom(from)))
	{

		if (flow_id == this->GetNode()->last_flow_id)
		{
			this->GetNode()->last_flow_id_remian_counter++;
		}
		else
		{
			this->GetNode()->last_flow_id = flow_id;
			this->GetNode()->last_flow_id_remian_counter = 1;
		}

		m_received++;
		SeqTsHeader receivedSeqTs;
		packet->RemoveHeader(receivedSeqTs);
		if (receivedSeqTs.GetAckNeeded() == 1)
		{
			double timeNow = Simulator::Now().GetSeconds();
			double rcvdTs = receivedSeqTs.GetTs().GetSeconds();
			double oneWayDelay = timeNow - rcvdTs;
			int sz = packet->GetSize();

			
			/*if (receivedSeqTs.GetSeq() == max_packet_seq - 1)
			{
				if(max_packet_seq < 100)
					std::cout << "sid: " << flow_id << " size: " << max_packet_seq << " fct: " << Simulator::Now().GetInteger() - start_time << std::endl;
				else 
					std::cout << "bid: " << flow_id << " size: " << max_packet_seq << " fct: " << Simulator::Now().GetInteger() - start_time << std::endl;
			}*/
			
			
			
			//calculate target
			bool ecn_marked = (bool)receivedSeqTs.GetTargetAsUint64();
			// if mark
			if (ecn_marked)
			{
				this->GetNode()->con_mark += 1;
				// can increase
				if (this->GetNode()->con_mark <= 5)
				{
					this->GetNode()->IncreaseEvent_f();
				}
				// can not increase
				else
				{
					this->GetNode()->con_mark = 6;
				}
			}
			else
				this->GetNode()->con_mark = 0;
			
			count++;
			SeqTsHeader seqTs;
			seqTs.SetSeq(m_sent);
			seqTs.SetPG(7);


			seqTs.SetTsAsUint64(receivedSeqTs.GetTsAsUint64());
			seqTs.SetTargetAsUint64(this->GetNode()->lqq_target);
			
					
			/*if (current_rate_percen < 0.9) {
				seqTs.SetTsAsUint64(receivedSeqTs.GetTsAsUint64());
				seqTs.SetTargetAsUint64(this->GetNode()->lqq_max_target +1);
			}

			else {
				seqTs.SetTsAsUint64(receivedSeqTs.GetTsAsUint64());
				seqTs.SetTargetAsUint64(this->GetNode()->lqq_target);
			}*/
			
			
			Ptr<Packet> p = Create<Packet>(0);
			p->AddHeader(seqTs);
			NS_LOG_LOGIC("Echoing packet");
			socket->SendTo(p, 0, from);
			m_sent++;

		}
	}
}

} // Namespace ns3
