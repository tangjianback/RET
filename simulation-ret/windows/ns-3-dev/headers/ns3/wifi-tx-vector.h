/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2010 CTTC
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
 * Author: Nicola Baldo <nbaldo@cttc.es>
 *       : Ghada Badawy <gbadawy@gmail.com>
 */

#ifndef WIFI_TX_VECTOR_H
#define WIFI_TX_VECTOR_H

#include <ns3/wifi-mode.h>
#include <ostream>

namespace ns3 {


/**
 * This class mimics the TXVECTOR which is to be
 * passed to the PHY in order to define the parameters which are to be
 * used for a transmission. See IEEE 802.11-2007 15.2.6 "Transmit PLCP",
 * and also 15.4.4.2 "PMD_SAP peer-to-peer service primitive
 * parameters".
 *
 * \note the above reference is valid for the DSSS PHY only (clause
 * 15). TXVECTOR is defined also for the other PHYs, however they
 * don't include the TXPWRLVL explicitly in the TXVECTOR. This is
 * somewhat strange, since all PHYs actually have a
 * PMD_TXPWRLVL.request primitive. We decide to include the power
 * level in WifiTxVector for all PHYs, since it serves better our
 * purposes, and furthermore it seems close to the way real devices
 * work (e.g., madwifi).
 */
class WifiTxVector
{
public:
  WifiTxVector ();
  WifiTxVector (WifiMode m, uint8_t l, uint8_t r, bool sg, uint8_t ns, uint8_t ne, bool Stbc);
  /**
   *  \returns the txvector payload mode
   */
  WifiMode GetMode (void) const;
 /**
  * Sets the selected payload transmission mode
  */
  void SetMode (WifiMode mode);
  /**
   *  \returns the transmission power level
   */
  uint8_t GetTxPowerLevel (void) const;
  /**
   * Sets the selected transmission power level
   */
  void SetTxPowerLevel (uint8_t powerlevel);
  /**
   *  \returns the number of retries
   */
  uint8_t GetRetries (void) const;
   /**
   * Sets the number of retries
   */
  void SetRetries (uint8_t retries);
  /**
   *  \returns if ShortGuardInterval is used or not
   */
  bool IsShortGuardInterval (void) const;
   /**
   * Sets if short gurad interval is being used
   */
  void SetShortGuardInterval (bool guardinterval);
 /**
   *  \returns the number of Nss
   */
  uint8_t GetNss (void) const;
   /**
   * Sets the number of Nss refer to IEEE802.11n Table 20-28 for explanation and range
   */
  void SetNss (uint8_t nss);
  /**
   *  \returns the number of Ness  
   */
  uint8_t GetNess (void) const;
   /**
   * Sets the Ness number refer to IEEE802.11n Table 20-6 for explanation
   */
  void SetNess (uint8_t ness);
  /**
   *  \returns if STBC is used or not
   */
  bool IsStbc (void) const;
  /**
   * Sets if STBC is being used
   */
  void SetStbc (bool stbcsatuts);

  
private:

  WifiMode m_mode;         /**< The DATARATE parameter in Table 15-4. 
                           It is the value that will be passed
                           to PMD_RATE.request */ 
  uint8_t  m_txPowerLevel;  /**< The TXPWR_LEVEL parameter in Table 15-4. 
                           It is the value that will be passed
                           to PMD_TXPWRLVL.request */ 
  uint8_t  m_retries;      /**< The DATA_RETRIES/RTS_RETRIES parameter
                           for Click radiotap information */
  bool     m_shortGuardInterval; //true if short GI is going to be used
  uint8_t  m_nss; //number of streams
  uint8_t  m_ness; //number of stream in beamforming
  bool     m_stbc; //STBC used or not

};

std::ostream & operator << (std::ostream & os,const WifiTxVector &v); 

} // namespace ns3

#endif // WIFI_TX_VECTOR_H
