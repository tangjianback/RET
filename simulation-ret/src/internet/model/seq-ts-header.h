/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
* Copyright (c) 2009 INRIA
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
* Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
*/

#ifndef SEQ_TS_HEADER_H
#define SEQ_TS_HEADER_H

#include "ns3/header.h"
#include "ns3/nstime.h"

namespace ns3 {
	/**
	* \ingroup udpclientserver
	* \class SeqTsHeader
	* \brief Packet header for Udp client/server application
	* The header is made of a 32bits sequence number followed by
	* a 64bits time stamp.
	*/
	class SeqTsHeader : public Header
	{
	public:
		SeqTsHeader();

		/**
		* \param seq the sequence number
		*/
		void SetSeq(uint32_t seq);
		/**
		* \return the sequence number
		*/
		uint32_t GetSeq(void) const;
		/**
		* \return the time stamp
		*/
		Time GetTs(void) const;
		uint64_t GetTsAsUint64() { return m_ts; };
		void SetTsAsUint64(uint64_t ts) { m_ts = ts; };

		uint64_t GetTargetAsUint64() { return target_ts; };
		void SetTargetAsUint64(uint64_t ts) { target_ts = ts; };

		void SetPG(uint16_t pg);
		uint16_t GetPG() const;

		static TypeId GetTypeId(void);
		void SetAckNeeded() { m_ackNeeded = 1; }
		uint8_t GetAckNeeded() { return m_ackNeeded; }

	private:
		virtual TypeId GetInstanceTypeId(void) const;
		virtual void Print(std::ostream &os) const;
		virtual uint32_t GetSerializedSize(void) const;
		virtual void Serialize(Buffer::Iterator start) const;
		virtual uint32_t Deserialize(Buffer::Iterator start);

		uint32_t m_seq;
		uint64_t m_ts;
		uint16_t m_pg;
		uint8_t m_ackNeeded;
		uint64_t target_ts;
	};

} // namespace ns3

#endif /* SEQ_TS_HEADER_H */
