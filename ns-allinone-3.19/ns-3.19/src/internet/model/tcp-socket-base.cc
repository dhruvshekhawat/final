/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2007 Georgia Tech Research Corporation
 * Copyright (c) 2010 Adrian Sai-wah Tam
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
 * Author: Adrian Sai-wah Tam <adrian.sw.tam@gmail.com>
 */


#define NS_LOG_APPEND_CONTEXT \
  if (m_node) { std::clog << Simulator::Now ().GetSeconds () << " [node " << m_node->GetId () << "] "; }
#include<iostream>
#include "ns3/abort.h"
#include "ns3/node.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/log.h"
#include "ns3/ipv4.h"
#include "ns3/ipv6.h"
#include "ns3/ipv4-interface-address.h"
#include "ns3/ipv4-route.h"
#include "ns3/ipv6-route.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv6-routing-protocol.h"
#include "ns3/simulation-singleton.h"
#include "ns3/simulator.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/trace-source-accessor.h"
#include "tcp-socket-base.h"
#include "tcp-l4-protocol.h"
#include "ipv4-end-point.h"
#include "ipv6-end-point.h"
#include "ipv6-l3-protocol.h"
#include "tcp-header.h"
#include "rtt-estimator.h"
#include "state-machine-var.h"

#include <algorithm>
using namespace std;
NS_LOG_COMPONENT_DEFINE ("TcpSocketBase");
int l;
int c1;

bool m_datasock=false;

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (TcpSocketBase)
  ;

bool                     m_bufferfull=false;      //check if Tx buffer is full
bool                     m_newack=true;          //check if the ack recieved is valid and new
uint32_t                 sz_1=0;              // number of bytes sent by senddatapacket()
bool                     nagle=false;              //non-zero till sendpendingdata can send data. When 1, send pending data sends data coming out loop

Address f_add;
Address t_add;

SequenceNumber32 saved_seq;

TypeId
TcpSocketBase::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TcpSocketBase")
    .SetParent<TcpSocket> ()
//    .AddAttribute ("TcpState", "State in TCP state machine",
//                   TypeId::ATTR_GET,
//                   EnumValue (CLOSED),
//                   MakeEnumAccessor (&TcpSocketBase::m_state),
//                   MakeEnumChecker (CLOSED, "Closed"))
    .AddAttribute ("MaxSegLifetime",
                   "Maximum segment lifetime in seconds, use for TIME_WAIT state transition to CLOSED state",
                   DoubleValue (120), /* RFC793 says MSL=2 minutes*/
                   MakeDoubleAccessor (&TcpSocketBase::m_msl),
                   MakeDoubleChecker<double> (0))
    .AddAttribute ("MaxWindowSize", "Max size of advertised window",
                   UintegerValue (65535),
                   MakeUintegerAccessor (&TcpSocketBase::m_maxWinSize),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("IcmpCallback", "Callback invoked whenever an icmp error is received on this socket.",
                   CallbackValue (),
                   MakeCallbackAccessor (&TcpSocketBase::m_icmpCallback),
                   MakeCallbackChecker ())
    .AddAttribute ("IcmpCallback6", "Callback invoked whenever an icmpv6 error is received on this socket.",
                   CallbackValue (),
                   MakeCallbackAccessor (&TcpSocketBase::m_icmpCallback6),
                   MakeCallbackChecker ())                   
    .AddTraceSource ("RTO",
                     "Retransmission timeout",
                     MakeTraceSourceAccessor (&TcpSocketBase::m_rto))
    .AddTraceSource ("RTT",
                     "Last RTT sample",
                     MakeTraceSourceAccessor (&TcpSocketBase::m_lastRtt))
    .AddTraceSource ("NextTxSequence",
                     "Next sequence number to send (SND.NXT)",
                     MakeTraceSourceAccessor (&TcpSocketBase::m_nextTxSequence))
    .AddTraceSource ("HighestSequence",
                     "Highest sequence number ever sent in socket's life time",
                     MakeTraceSourceAccessor (&TcpSocketBase::m_highTxMark))
    .AddTraceSource ("State",
                     "TCP state",
                     MakeTraceSourceAccessor (&TcpSocketBase::m_state))
    .AddTraceSource ("RWND",
                     "Remote side's flow control window",
                     MakeTraceSourceAccessor (&TcpSocketBase::m_rWnd))
  ;
  return tid;
}



//added by dhruv
const char* StateNames[] = {"CLOSED","LISTEN","SYN_SENT","SYN_RCVD","ESTABLISHED","CLOSE_WAIT","LAST_ACK","FIN_WAIT_1","FIN_WAIT_2", "CLOSING", "TIME_WAIT","RECEIVED_ACK","RECEIVED_NEW_ACK","DATA_RECEIVED","CHECK","PERSIST_STATE","BUFFER_READ","SEND","SCHEDULE_ACK","CHECK_REC","UPDATE_RETX",
"IDLE_S","IDLE_R","LAST_STATE"};

const char* EventNames[]={"APP_LISTEN", "APP_CONNECT","APP_SEND","APP_CLOSE","TIMEOUT","ACK_RX","SYN_RX","SYN_ACK_RX","FIN_RX","FIN_ACK_RX", "FIN_ACKED","RST_RX","BAD_FLAGS","RWIND_0","FORCED_TRANS","CHECK_NAGLE","SEND_DATA","NOTIFY_APP", "CHECK_RX_BUFFER",
"ACK_CHECK","SET_RTO","RX_BUFFER_FULL","RX_BUFFER_GAP","DELAY_ACK_PERMIT","DATA_RECV","LAST_DATA_FIN","SEND_SYN_ACK","LAST_EVENT"};


const char* ActionNames[]={"NO_ACT","ACK_TX","ACK_TX_1","RST_TX","SYN_TX","SYN_ACK_TX","SYN_ACK_TX1","FIN_TX","FIN_ACK_TX","GOT_ACK","TX_DATA","PEER_CLOSE","APP_CLOSED","CANCEL_TM","SERV_NOTIFY","SS_CWND1","SS_CWND2","RWIND_0_ACTION",  "BUFFER_READ_ACTIONS",  "NAGLE_ACTION","SEND_DATA_ACTION","RX_BUFFER_ACTIONS","NOTIFY_APP_ACTION","DATA_RECEIVED_ACTION","ACK_CHECK_ACTION","SET_RTO_ACTION","FORK_DATA_SOCKET","SET_DELAYED_ACK", "TX_DATA_ARQ",
"LAST_ACTION"};



TcpSocketBase::TcpSocketBase (void)
  : m_dupAckCount (0),
    m_delAckCount (0),
    m_endPoint (0),
    m_endPoint6 (0),
    m_node (0),
    m_tcp (0),
    m_rtt (0),
    m_nextTxSequence (0),
    // Change this for non-zero initial sequence number
    m_highTxMark (0),
    m_rxBuffer (0),
    m_txBuffer (0),
    m_state (CLOSED),
    m_errno (ERROR_NOTERROR),
    m_closeNotified (false),
    m_closeOnEmpty (false),
    m_shutdownSend (false),
    m_shutdownRecv (false),
    m_connected (false),
    m_segmentSize (0),
    // For attribute initialization consistency (quiet valgrind)
    m_rWnd (0)
{


  NS_LOG_FUNCTION (this);
}

TcpSocketBase::TcpSocketBase (const TcpSocketBase& sock)
  : TcpSocket (sock),
    //copy object::m_tid and socket::callbacks
    m_dupAckCount (sock.m_dupAckCount),
    m_delAckCount (0),
    m_delAckMaxCount (sock.m_delAckMaxCount),
    m_noDelay (sock.m_noDelay),
    m_cnRetries (sock.m_cnRetries),
    m_delAckTimeout (sock.m_delAckTimeout),
    m_persistTimeout (sock.m_persistTimeout),
    m_cnTimeout (sock.m_cnTimeout),
    m_endPoint (0),
    m_endPoint6 (0),
    m_node (sock.m_node),
    m_tcp (sock.m_tcp),
    m_rtt (0),
    m_nextTxSequence (sock.m_nextTxSequence),
    m_highTxMark (sock.m_highTxMark),
    m_rxBuffer (sock.m_rxBuffer),
    m_txBuffer (sock.m_txBuffer),
    m_state (sock.m_state),
    m_errno (sock.m_errno),
    m_closeNotified (sock.m_closeNotified),
    m_closeOnEmpty (sock.m_closeOnEmpty),
    m_shutdownSend (sock.m_shutdownSend),
    m_shutdownRecv (sock.m_shutdownRecv),
    m_connected (sock.m_connected),
    m_msl (sock.m_msl),
    m_segmentSize (sock.m_segmentSize),
    m_maxWinSize (sock.m_maxWinSize),
    m_rWnd (sock.m_rWnd)
{

  NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC ("Invoked the copy constructor");
  // Copy the rtt estimator if it is set
  if (sock.m_rtt)
    {
      m_rtt = sock.m_rtt->Copy ();
    }
  // Reset all callbacks to null
  Callback<void, Ptr< Socket > > vPS = MakeNullCallback<void, Ptr<Socket> > ();
  Callback<void, Ptr<Socket>, const Address &> vPSA = MakeNullCallback<void, Ptr<Socket>, const Address &> ();
  Callback<void, Ptr<Socket>, uint32_t> vPSUI = MakeNullCallback<void, Ptr<Socket>, uint32_t> ();
  SetConnectCallback (vPS, vPS);
  SetDataSentCallback (vPSUI);
  SetSendCallback (vPSUI);
  SetRecvCallback (vPS);
}

TcpSocketBase::~TcpSocketBase (void)
{
  NS_LOG_FUNCTION (this);
  m_node = 0;
  if (m_endPoint != 0)
    {
      NS_ASSERT (m_tcp != 0);
      /*
       * Upon Bind, an Ipv4Endpoint is allocated and set to m_endPoint, and
       * DestroyCallback is set to TcpSocketBase::Destroy. If we called
       * m_tcp->DeAllocate, it wil destroy its Ipv4EndpointDemux::DeAllocate,
       * which in turn destroys my m_endPoint, and in turn invokes
       * TcpSocketBase::Destroy to nullify m_node, m_endPoint, and m_tcp.
       */
      NS_ASSERT (m_endPoint != 0);
      m_tcp->DeAllocate (m_endPoint);
      NS_ASSERT (m_endPoint == 0);
    }
  if (m_endPoint6 != 0)
    {
      NS_ASSERT (m_tcp != 0);
      NS_ASSERT (m_endPoint6 != 0);
      m_tcp->DeAllocate (m_endPoint6);
      NS_ASSERT (m_endPoint6 == 0);
    }
  m_tcp = 0;
  CancelAllTimers ();
}

/* Associate a node with this TCP socket */
void
TcpSocketBase::SetNode (Ptr<Node> node)
{
  m_node = node;
}

/* Associate the L4 protocol (e.g. mux/demux) with this socket */
void
TcpSocketBase::SetTcp (Ptr<TcpL4Protocol> tcp)
{
  m_tcp = tcp;
}

/* Set an RTT estimator with this socket */
void
TcpSocketBase::SetRtt (Ptr<RttEstimator> rtt)
{
  m_rtt = rtt;
}

/* Inherit from Socket class: Returns error code */
enum Socket::SocketErrno
TcpSocketBase::GetErrno (void) const
{
  return m_errno;
}

/* Inherit from Socket class: Returns socket type, NS3_SOCK_STREAM */
enum Socket::SocketType
TcpSocketBase::GetSocketType (void) const
{
  return NS3_SOCK_STREAM;
}

/* Inherit from Socket class: Returns associated node */
Ptr<Node>
TcpSocketBase::GetNode (void) const
{
  NS_LOG_FUNCTION_NOARGS ();
  return m_node;
}

//added by dhruv
Actions_t TcpSocketBase::OnEvent (Events_t e)
{
  NS_LOG_FUNCTION (this << e);
  States_t saveState = m_state;
  NS_LOG_LOGIC ("Processing Event: " << EventNames[e]<<endl);

ExtendedFiniteStateMachine tcp;
State_Action stateAction = tcp.Table_Lookup(m_state,e);
//state changed
  m_state = stateAction.state;
  NS_LOG_LOGIC (StateNames[saveState]<< "=====>" <<StateNames[m_state]<<endl);

  NS_LOG_LOGIC ("State Transition Action: " << ActionNames[stateAction.action]<<endl);

//checks if the transitioning state is CLOSED
  bool needCloseNotify = (stateAction.state == CLOSED && m_state != CLOSED 
    && e != TIMEOUT);  

  //extra event logic is here for RX events
  //e = SYN_ACK_RX
  if (saveState == SYN_SENT && m_state == ESTABLISHED)
    // this means the application side has completed its portion of 
    // the handshaking
    {
      Simulator::ScheduleNow(&TcpSocketBase::ConnectionSucceeded, this);
      m_connected = true;
      NS_LOG_LOGIC ("TcpSocketBase " << this << " Connected!");
    }
  if (saveState < CLOSING && (m_state == CLOSING || m_state == TIME_WAIT) )
    {
      NS_LOG_LOGIC ("TcpSocketBase peer closing, send EOF to application");
      if (!m_shutdownRecv)
        {
          NotifyDataRecv ();
        }
    }

//Basically telling the App to close the socket
  if (needCloseNotify && !m_closeNotified)
    {
      NS_LOG_LOGIC ("TcpSocketBase " << this << " transition to CLOSED from " 
               << m_state << " event " << e << " closeNot " << m_closeNotified
               << " action " << stateAction.action);
      NotifyNormalClose();
      m_closeNotified = true;
      NS_LOG_LOGIC ("TcpSocketBase " << this << " calling Closed from PE"
              << " origState " << saveState
              << " event " << e);
      NS_LOG_LOGIC ("TcpSocketBase " << this << " transition to CLOSED from "
          << m_state << " event " << e
          << " set CloseNotif ");
    }

  if (m_state == CLOSED && saveState != CLOSED && m_endPoint != 0)
    {
  DeallocateEndPoint ();
    }
    
  return stateAction.action;
}

bool TcpSocketBase::OnAction (Actions_t a)
{ // These actions do not require a packet or any TCP Headers
  NS_LOG_FUNCTION (this << a);
  switch (a)
  {
    case NO_ACT:
      break;

    case SET_RTO_ACTION:{
    	
    NS_LOG_LOGIC("Action description:1. Cacel RTO unless the ACK for SYN_RCVD state"<<endl<<"Schedule Retx timer to start and expire"<<endl);
  if (m_state != SYN_RCVD)
    { 
      NS_LOG_LOGIC (this << " Cancelled ReTxTimeout event which was set to expire at " <<
                    (Simulator::Now () + Simulator::GetDelayLeft (m_retxEvent)).GetSeconds ());
      m_retxEvent.Cancel ();

      m_rto = m_rtt->RetransmitTimeout ();
      NS_LOG_LOGIC (this << " Schedule ReTxTimeout at time " <<
                    Simulator::Now ().GetSeconds () << " to expire at time " <<
                    (Simulator::Now () + m_rto.Get ()).GetSeconds ());
      m_retxEvent = Simulator::Schedule (m_rto, &TcpSocketBase::ReTxTimeout, this);
    }

}
break;
    
    case ACK_TX:
    NS_LOG_LOGIC("Action Description: Send an empty ACK packet"<<endl);
      SendEmptyPacket (TcpHeader::ACK);
      break;
    case ACK_TX_1:
      NS_ASSERT (false);
      break;
    case RST_TX:
    NS_LOG_LOGIC("Action Description: Send an empty RST packet"<<endl);
    SendRST();
      break;
      case SYN_ACK_TX1: //packetprocess
      NS_LOG_LOGIC ("TcpSocketBase " << this <<" Action SYN_ACK_TX");
      SendEmptyPacket (TcpHeader::SYN | TcpHeader::ACK);
      break;


case NAGLE_ACTION:
{

NS_LOG_LOGIC("Action Description:Checks the available window."<<endl<<"1.If window is less than segmentsize and bytes in buffer then STOP"<<endl<<"1. If buffer has unacked data and data is less than one segment invoke Nagle algorithm"<<endl);

uint32_t w=AvailableWindow();
cout<<"w= "<<w<<", segmentsize="<<m_segmentSize<<", Bytes in Txbuffer"<<m_txBuffer.SizeFromSequence(m_nextTxSequence)<<endl;
 // Stop sending if we need to wait for a larger Tx window (prevent silly window syndrome)
      if (w < m_segmentSize && m_txBuffer.SizeFromSequence (m_nextTxSequence) > w)
        {
         nagle=true;  // No more
        }

      // Nagle's algorithm (RFC896): Hold off sending if there is unacked data
      // in the buffer and the amount of data to send is less than one segment
      if (!m_noDelay && UnAckDataCount () > 0
          && m_txBuffer.SizeFromSequence (m_nextTxSequence) < m_segmentSize)
        {
          NS_LOG_LOGIC ("Invoking Nagle's algorithm. Wait to send.");
          nagle=true;
        }
     }
break;

    case FIN_TX:
          NS_LOG_LOGIC("Action Description: Sent an empty FIN packet"<<endl);
      SendEmptyPacket (TcpHeader::FIN);
      break;
    case FIN_ACK_TX:
    NS_LOG_LOGIC("Action Description: Sent an empty FIN+ACK packet"<<endl);
      SendEmptyPacket (TcpHeader::FIN | TcpHeader::ACK);
      break;
    case GOT_ACK:
      NS_ASSERT (false); // This should be processed in OnAction_PacketInfo
      break;


case BUFFER_READ_ACTIONS:
{

    NS_LOG_LOGIC("Action Description: 1. Notify the app that some data has been sent."<<endl<<"2.Advance the next seq num to the new ack received"<<endl<<"3. If no data to be sent cancel ReTxtimeout"<<endl);

  if (GetTxAvailable () > 0)
    {
//Notify that some data has been sent
 NotifySend (GetTxAvailable ());
    }
  if (saved_seq > m_nextTxSequence)
    {
      m_nextTxSequence = saved_seq; // advance next tx to be sent
    }
  if (m_txBuffer.Size () == 0 && m_state != FIN_WAIT_1 && m_state != CLOSING)
    { // No retransmit timer if no data to retransmit
      NS_LOG_LOGIC (this << " Cancelled ReTxTimeout event which was set to expire at " <<
                    (Simulator::Now () + Simulator::GetDelayLeft (m_retxEvent)).GetSeconds ());
      m_retxEvent.Cancel ();
    }
}
break;

    case TX_DATA:
      NS_LOG_LOGIC ("Action description:Try to send pending data in Tx buffer");
      SendPendingData (m_connected);
      break;

      case TX_DATA_ARQ:
      {

  cout<<"In TX_DATA_ARQ"<<endl;
  
  if (m_txBuffer.Size () == 0)
    {
      return false;                           // Nothing to send

    }
  if (m_endPoint == 0 && m_endPoint6 == 0)
    {
      NS_LOG_INFO ("TcpSocketBase::SendPendingData: No endpoint; m_shutdownSend=" << m_shutdownSend);
      return false; // Is this the right way to handle this condition?
    }
  uint32_t nPacketsSent = 0;
  m_rWnd= 537; // set specifically for ARQ protocol
  uint32_t w = AvailableWindow (); // Get available window size
      NS_LOG_LOGIC ("TcpSocketBase " << this << " SendPendingData" <<
                    " rxwin " << m_rWnd <<"w"<<w<<
                    " segsize " << m_segmentSize <<
                    " nextTxSeq " << m_nextTxSequence <<
                    " highestRxAck " << m_txBuffer.HeadSequence () <<
                    " pd->Size " << m_txBuffer.Size () <<
                    " pd->SFS " << m_txBuffer.SizeFromSequence (m_nextTxSequence));
     
Actions_t action1 = OnEvent (CHECK_NAGLE);
  OnAction (action1);

Actions_t action2 = OnEvent (SEND_DATA);
  OnAction (action2);

      nPacketsSent++;                             // Count sent this loop
      m_nextTxSequence += sz_1;                     // Advance next tx sequence

  NS_LOG_LOGIC ("*******SENDPENDINGDATA sent " << nPacketsSent << " packets****************************************");
  
  nagle=false;
  

      }
      break;
    case PEER_CLOSE:
      NS_ASSERT (false); // This should be processed in OnAction_PacketInfo
      NS_LOG_LOGIC ("TcpSocketBase " << this <<" Action PEER_CLOSE");
      break;
    case APP_CLOSED:
      NS_LOG_LOGIC ("TcpSocketBase " << this <<" Action APP_CLOSED");
      break;
    case CANCEL_TM:
      NS_LOG_LOGIC ("TcpSocketBase " << this <<" Action CANCEL_TM");
      break;
    
    case SERV_NOTIFY:
      NS_ASSERT (false); // This should be processed in OnAction_PacketInfo
      break;

   case SYN_TX:
    {
    NS_LOG_LOGIC("Action Description: Send an empty SYN packet"<<endl);
       SendEmptyPacket (TcpHeader::SYN);
  }
 break;
case SEND_DATA_ACTION:
{
   NS_LOG_LOGIC("Action description:1.Send no more than the window, SDP() returns no. of bytes sent"<<endl<<"2. Advance the next seq number to be sent"<<endl);
uint32_t w=AvailableWindow();
uint32_t s = std::min (w, m_segmentSize);
 sz_1 = SendDataPacket (m_nextTxSequence, s, true); //changed m_connected
 
 
 }
break;

case RWIND_0_ACTION:
{
    NS_LOG_LOGIC (this << "Enter zerowindow persist state");
      NS_LOG_LOGIC (this << "Cancelled ReTxTimeout event which was set to expire at " <<
                    (Simulator::Now () + Simulator::GetDelayLeft (m_retxEvent)).GetSeconds ());
      m_retxEvent.Cancel ();
      NS_LOG_LOGIC ("Schedule persist timeout at time " <<
                    Simulator::Now ().GetSeconds () << " to expire at time " <<
                    (Simulator::Now () + m_persistTimeout).GetSeconds ());
      m_persistEvent = Simulator::Schedule (m_persistTimeout, &TcpSocketBase::PersistTimeout, this);
      NS_ASSERT (m_persistTimeout == Simulator::GetDelayLeft (m_persistEvent));
}
break;


case LAST_ACTION:
      NS_LOG_LOGIC ("TcpSocketBase " << this <<" Action LAST_ACTION");
      break;
   
    default:
    break;
  }
  return true;
}

bool TcpSocketBase::OnAction_PacketInfo (Actions_t a, Ptr<Packet> p,
                                     const TcpHeader& tcpHeader,
                                     const Address& fromAddress,
                                     const Address& toAddress)
{
switch (a)
{
    case RST_TX:
      {
        NS_LOG_LOGIC ("Action description: Send empty RST packet and return (No Action)"<<endl);
        SendRST();
        return NO_ACT;
      }

case ACK_CHECK_ACTION:{
// Received ACK. Compare the ACK number against highest unacked seqno

      NS_LOG_LOGIC ("Action description:1. Ignore if no ACK flag"<<endl<<"2. Ignore if old ACK"<<endl<<"3. Check if Dup ACK"<<endl);
     
  if (0 == (tcpHeader.GetFlags () & TcpHeader::ACK))
    { 
     m_newack=false;
    }
  else if (tcpHeader.GetAckNumber () < m_txBuffer.HeadSequence ())
    { // Case 1: Old ACK, ignored.
      m_newack=false;
      NS_LOG_LOGIC ("Ignored ack of " << tcpHeader.GetAckNumber ());
    }
  else if (tcpHeader.GetAckNumber () == m_txBuffer.HeadSequence ())
    { // Case 2: Potentially a duplicated ACK
      if (tcpHeader.GetAckNumber () < m_nextTxSequence && p->GetSize() == 0)
        {m_newack=false;
          NS_LOG_LOGIC ("Dupack of " << tcpHeader.GetAckNumber ());
          DupAck (tcpHeader, ++m_dupAckCount);
          m_state = IDLE_S;
        }
      // otherwise, the ACK is precisely equal to the nextTxSequence
      NS_ASSERT (tcpHeader.GetAckNumber () <= m_nextTxSequence);
    }
  }
break;

case DATA_RECEIVED_ACTION:
{
NS_LOG_LOGIC("Action description: Call the function to take care Event: DATA_RECV"<<endl);
      ReceivedData (p, tcpHeader);
}
break;


case NOTIFY_APP_ACTION:
{

	//cout<<"I am  notifying"<<endl;
	
NS_LOG_LOGIC("Action description: Called because data was received. 1. Notify app that some data has been received"<<endl<<"2. If FIN was received last, then check if all data has been received and Peer Close"<<endl);

if (!m_shutdownRecv)
        {
          NotifyDataRecv ();
        }
      // Handle exceptions
      if (m_closeNotified)
        {
          NS_LOG_WARN ("Why TCP " << this << " got data after close notification?");

        }
      if (m_rxBuffer.Finished () && (tcpHeader.GetFlags () & TcpHeader::FIN) == 0)
        {

  Actions_t actio = OnEvent (LAST_DATA_FIN);
  OnAction (actio);//Do Peer close

        }

}
break;

case RX_BUFFER_ACTIONS:
{
NS_LOG_LOGIC("Action 1: Checks if buffer is full or not"<<endl<<"Action 2. Send ACK for all received data"<<endl<<"Check something and schedule delayed ACK?"<<endl);

SequenceNumber32 expectedSeq = m_rxBuffer.NextRxSequence ();
if (!m_rxBuffer.Add (p, tcpHeader))
    { // Insert failed: No data or RX buffer full
      m_bufferfull=true;
      
  Actions_t action2 = OnEvent (RX_BUFFER_FULL);
  OnAction (action2);
    break;
    }

  // Now send a new ACK packet acknowledging all received and delivered data
  if (m_rxBuffer.Size () > m_rxBuffer.Available () || m_rxBuffer.NextRxSequence () > expectedSeq + p->GetSize ())
    { // A gap exists in the buffer, or we filled a gap: Always ACK
Actions_t action3 = OnEvent (RX_BUFFER_GAP);
  OnAction (action3);


  }
  else
    { // In-sequence packet: ACK if delayed ack count allows
    	Actions_t actionsome = OnEvent (DELAY_ACK_PERMIT);
  //OnAction_PacketInfo (action3, p, tcpHeader, fromAddress, toAddress);
    	OnAction_PacketInfo (actionsome, p, tcpHeader, fromAddress, toAddress);
        }
 
 
}
break;

case SET_DELAYED_ACK:
{
  if (++m_delAckCount >= m_delAckMaxCount)
        {
          
          m_delAckEvent.Cancel ();
          m_delAckCount = 0;
          SendEmptyPacket (TcpHeader::ACK);

        }//m_delackevent- Delayed ACK timeout event
      else if (m_delAckEvent.IsExpired ())
        {
          
          Actions_t action1 = OnEvent (FORCED_TRANS);
          OnAction (action1);
          m_delAckEvent = Simulator::Schedule (m_delAckTimeout,
                                               &TcpSocketBase::DelAckTimeout, this);
          NS_LOG_LOGIC (this << " scheduled delayed ACK at " << (Simulator::Now () + Simulator::GetDelayLeft (m_delAckEvent)).GetSeconds ());


        }


}
break;


case SS_CWND_1:
{

NS_LOG_LOGIC("Action Description: Update cwnd and ssthresh");
//c1= getchar();

      saved_seq=tcpHeader.GetAckNumber();
      NewAck (saved_seq);
      

}
break;
case SS_CWND_2:
{

NS_LOG_LOGIC("Task: Making TCP flexible ");
//c1= getchar();
      saved_seq=tcpHeader.GetAckNumber();


}
      break;
 
   case SYN_ACK_TX:
{ 
     NS_LOG_LOGIC ("Action description: Fork a socket to handle data transmission"<<endl);
       
   
  // Clone the socket, simulate fork
  //After TcpSocketBase cloned, allocate a new end point to handle the incoming connection and send a SYN+ACK to complete the handshake.
  Ptr<TcpSocketBase> newSock = Fork ();
  NS_LOG_LOGIC ("Cloned a TcpSocketBase " << newSock);
  Simulator::ScheduleNow (&TcpSocketBase::CompleteFork, newSock,p, tcpHeader, fromAddress, toAddress);
  //SendEmptyPacket (TcpHeader::SYN | TcpHeader::ACK);

}
       break;
 
 case FORK_DATA_SOCKET:
 {
  
  Ptr<TcpSocketBase> newSock = Fork ();
  NS_LOG_LOGIC ("Cloned a TcpSocketBase " << newSock);
  Simulator::ScheduleNow (&TcpSocketBase::CompleteFork, newSock,p, tcpHeader, fromAddress, toAddress);
  m_connected=true;
  m_datasock=true;

              m_rxBuffer.SetNextRxSequence (tcpHeader.GetSequenceNumber () + SequenceNumber32 (1));//change expected seq num+1
      m_highTxMark = ++m_nextTxSequence;// highest seq number ever sent
      m_txBuffer.SetHeadSequence (m_nextTxSequence);// only to be called at start
      m_delAckCount = m_delAckMaxCount;

 }
 break;   

case ACK_TX_1:
{
 NS_LOG_LOGIC("Action 1. Cancel ReTx timeout and Increment Rx expected seq by 1"<<endl<<"Action 2. Send empty ACK packet and start filling the TX buffer"<<endl);
m_connected = true;
      m_retxEvent.Cancel ();// cancel timeout
      m_rxBuffer.SetNextRxSequence (tcpHeader.GetSequenceNumber () + SequenceNumber32 (1));//change expected seq num+1
      m_highTxMark = ++m_nextTxSequence;// highest seq number ever sent
      m_txBuffer.SetHeadSequence (m_nextTxSequence);// only to be called at start
      SendEmptyPacket (TcpHeader::ACK);
      Actions_t action1 = OnEvent (FORCED_TRANS);
  OnAction (action1);

      Simulator::ScheduleNow (&TcpSocketBase::ConnectionSucceeded, this);

      m_delAckCount = m_delAckMaxCount;
}
      break;
 

   case GOT_ACK:
{
     ReceivedAck (p, tcpHeader);

}
      break;
    
     
    case PEER_CLOSE:
    {
      NS_LOG_LOGIC("Got Peer Close");
DoPeerClose();
        }
      break;
    
    case SERV_NOTIFY:
      NS_LOG_LOGIC ("TcpSocketBase " << this <<" Action SERV_NOTIFY");
      NS_LOG_LOGIC ("TcpSocketBase " << this << " Connected!");
Notify_sender (p, tcpHeader, fromAddress, toAddress);

      break;
    default:
      return OnAction (a);

  }
  return true;
}






/* Inherit from Socket class: Bind socket to an end-point in TcpL4Protocol */
int
TcpSocketBase::Bind (void)
{
  NS_LOG_FUNCTION (this);
  m_endPoint = m_tcp->Allocate ();
  if (0 == m_endPoint)
    {
      m_errno = ERROR_ADDRNOTAVAIL;
      return -1;
    }
  m_tcp->m_sockets.push_back (this);// CHECK: different i.e list of sockets
  return SetupCallback ();
}

int
TcpSocketBase::Bind6 (void)
{
  NS_LOG_FUNCTION (this);
  m_endPoint6 = m_tcp->Allocate6 ();
  if (0 == m_endPoint6)
    {
      m_errno = ERROR_ADDRNOTAVAIL;
      return -1;
    }
  m_tcp->m_sockets.push_back (this);
  return SetupCallback ();
}

/* Inherit from Socket class: Bind socket (with specific address) to an end-point in TcpL4Protocol */
int
TcpSocketBase::Bind (const Address &address)
{

cout<<" DEBUG: Inside bind now, assign an MAC address to the socket"<<endl;
  NS_LOG_FUNCTION (this << address);
  if (InetSocketAddress::IsMatchingType (address))
    {
      InetSocketAddress transport = InetSocketAddress::ConvertFrom (address);
      Ipv4Address ipv4 = transport.GetIpv4 ();
      uint16_t port = transport.GetPort ();
      if (ipv4 == Ipv4Address::GetAny () && port == 0)
        {
          m_endPoint = m_tcp->Allocate ();
        }
      else if (ipv4 == Ipv4Address::GetAny () && port != 0)
        {
          m_endPoint = m_tcp->Allocate (port);
        }
      else if (ipv4 != Ipv4Address::GetAny () && port == 0)
        {
          m_endPoint = m_tcp->Allocate (ipv4);
        }
      else if (ipv4 != Ipv4Address::GetAny () && port != 0)
        {
          m_endPoint = m_tcp->Allocate (ipv4, port);
        }
      if (0 == m_endPoint)
        {
          m_errno = port ? ERROR_ADDRINUSE : ERROR_ADDRNOTAVAIL;
          return -1;
        }
    }
  else if (Inet6SocketAddress::IsMatchingType (address))
    {
      Inet6SocketAddress transport = Inet6SocketAddress::ConvertFrom (address);
      Ipv6Address ipv6 = transport.GetIpv6 ();
      uint16_t port = transport.GetPort ();
      if (ipv6 == Ipv6Address::GetAny () && port == 0)
        {
          m_endPoint6 = m_tcp->Allocate6 ();
        }
      else if (ipv6 == Ipv6Address::GetAny () && port != 0)
        {
          m_endPoint6 = m_tcp->Allocate6 (port);
        }
      else if (ipv6 != Ipv6Address::GetAny () && port == 0)
        {
          m_endPoint6 = m_tcp->Allocate6 (ipv6);
        }
      else if (ipv6 != Ipv6Address::GetAny () && port != 0)
        {
          m_endPoint6 = m_tcp->Allocate6 (ipv6, port);
        }
      if (0 == m_endPoint6)
        {
          m_errno = port ? ERROR_ADDRINUSE : ERROR_ADDRNOTAVAIL;
          return -1;
        }
    }
  else
    {
      m_errno = ERROR_INVAL;
      return -1;
    }
  m_tcp->m_sockets.push_back (this);
  NS_LOG_LOGIC ("TcpSocketBase " << this << " got an endpoint: " << m_endPoint);

  return SetupCallback ();
//CHECK: This has a separate function 
}

/* Inherit from Socket class: Initiate connection to a remote address:port */
int
TcpSocketBase::Connect (const Address & address)
{

cout<<"Connecting to MAC address of receiver"<<endl;
  NS_LOG_FUNCTION (this << address);

  // If haven't do so, Bind() this socket first
  if (InetSocketAddress::IsMatchingType (address) && m_endPoint6 == 0)
    {
      if (m_endPoint == 0)
        {
          if (Bind () == -1)
            {
              NS_ASSERT (m_endPoint == 0);
              return -1; // Bind() failed
            }
          NS_ASSERT (m_endPoint != 0);
        }
      InetSocketAddress transport = InetSocketAddress::ConvertFrom (address);
      m_endPoint->SetPeer (transport.GetIpv4 (), transport.GetPort ());
      m_endPoint6 = 0;

      // Get the appropriate local address and port number from the routing protocol and set up endpoint
      if (SetupEndpoint () != 0)
        { // Route to destination does not exist
          return -1;
        }
    }
  else if (Inet6SocketAddress::IsMatchingType (address)  && m_endPoint == 0)
    {
      // If we are operating on a v4-mapped address, translate the address to
      // a v4 address and re-call this function
      Inet6SocketAddress transport = Inet6SocketAddress::ConvertFrom (address);
      Ipv6Address v6Addr = transport.GetIpv6 ();
      if (v6Addr.IsIpv4MappedAddress () == true)
        {
          Ipv4Address v4Addr = v6Addr.GetIpv4MappedAddress ();
          return Connect (InetSocketAddress (v4Addr, transport.GetPort ()));
        }

      if (m_endPoint6 == 0)
        {
          if (Bind6 () == -1)
            {
              NS_ASSERT (m_endPoint6 == 0);
              return -1; // Bind() failed
            }
          NS_ASSERT (m_endPoint6 != 0);
        }
      m_endPoint6->SetPeer (v6Addr, transport.GetPort ());
      m_endPoint = 0;

      // Get the appropriate local address and port number from the routing protocol and set up endpoint
      if (SetupEndpoint6 () != 0)
        { // Route to destination does not exist
          return -1;
        }
    }
  else
    {
      m_errno = ERROR_INVAL;
      return -1;
    }

  // Re-initialize parameters in case this socket is being reused after CLOSE
  m_rtt->Reset ();
  m_cnCount = m_cnRetries;

  // DoConnect() will do state-checking and send a SYN packet

cout<<"DEBUG: Calling doconnect method from connect"<<endl;
  return DoConnect ();
}

/* Inherit from Socket cla on the endpoint for an incoming connection */
int
TcpSocketBase::Listen (void)
{
  NS_LOG_FUNCTION (this);

  // Linux quits EINVAL if we're not in CLOSED state, so match what they do
  if (m_state != CLOSED)
    {
      m_errno = ERROR_INVAL;
      return -1;
    }
 Actions_t action = OnEvent (APP_LISTEN);
  OnAction (action);


  return 0;

}

/* Inherit from Socket class: Kill this socket and signal the peer (if any) */
int
TcpSocketBase::Close (void)
{
  NS_LOG_FUNCTION (this);
  /// \internal
  /// First we check to see if there is any unread rx data.
  /// \bugid{426} claims we should send reset in this case.
  if (m_rxBuffer.Size () != 0)
    {
      NS_LOG_INFO ("Socket " << this << " << unread rx data during close.  Sending reset");
      SendRST ();
      return 0;
    }
 
  if (m_txBuffer.SizeFromSequence (m_nextTxSequence) > 0)
    { // App close with pending data must wait until all data transmitted
      if (m_closeOnEmpty == false)
        {
          m_closeOnEmpty = true;
         // NS_LOG_INFO ("Socket " << this << " deferring close, state " << TcpStateName[m_state]);
        }
      return 0;
    }
  return DoClose ();
}

/* Inherit from Socket class: Signal a termination of send */
int
TcpSocketBase::ShutdownSend (void)
{
  NS_LOG_FUNCTION (this);
  
  //this prevents data from being added to the buffer
  m_shutdownSend = true;
  m_closeOnEmpty = true;
  //if buffer is already empty, send a fin now
  //otherwise fin will go when buffer empties.
  if (m_txBuffer.Size () == 0)
    {
      //changed if (m_state == ESTABLISHED || m_state == CLOSE_WAIT)
    	if(m_state ==CLOSE_WAIT)
        {
          NS_LOG_INFO("Emtpy tx buffer, send fin");
          SendEmptyPacket (TcpHeader::FIN);  

          if (m_state == ESTABLISHED)
            { // On active close: I am the first one to send FIN
              NS_LOG_INFO ("ESTABLISHED -> FIN_WAIT_1");
              m_state = FIN_WAIT_1;
            }
          else
            { // On passive close: Peer sent me FIN already
              NS_LOG_INFO ("CLOSE_WAIT -> LAST_ACK");
              m_state = LAST_ACK;
            }  
        }
    }
 
  return 0;
}

/* Inherit from Socket class: Signal a termination of receive */
int
TcpSocketBase::ShutdownRecv (void)
{
  NS_LOG_FUNCTION (this);
  m_shutdownRecv = true;
  return 0;
}

/* Inherit from Socket class: Send a packet. Parameter flags is not used.
    Packet has no TCP header. Invoked by upper-layer application */
int
TcpSocketBase::Send (Ptr<Packet> p, uint32_t flags)
{
  NS_LOG_FUNCTION (this << p);
  NS_ABORT_MSG_IF (flags, "use of flags is not supported in TcpSocketBase::Send()");
  if (m_state == ESTABLISHED || m_state == IDLE_S || m_state == SYN_SENT || m_state == CLOSE_WAIT)
    {
      // Store the packet into Tx buffer
cout<<"DEBUG: Storing data in transmission buffer"<<endl;
      if (!m_txBuffer.Add (p))
        { // TxBuffer overflow, send failed

cout<<"inside mtxbuffer : BUFFER FULL"<<endl;
          m_errno = ERROR_MSGSIZE;
          return -1;
        }
      if (m_shutdownSend)
        {


          m_errno = ERROR_SHUTDOWN;
          return -1;
        }
      // Submit the data to lower layers
     // NS_LOG_LOGIC ("txBufSize=" << m_txBuffer.Size () << " state " << TcpStateName[m_state]);


      if (m_state == ESTABLISHED || m_state == CLOSE_WAIT  || m_state == IDLE_S || m_datasock) //chenged m_state = IDLE_S
        { 
         
         
// send pending data
                Actions_t action1 = OnEvent (FORCED_TRANS);
                OnAction (action1);

        }
      return p->GetSize ();
    }
  else
    { // Connection not established yet
      m_errno = ERROR_NOTCONN;
      return -1; // Send failure
    }
}

/* Inherit from Socket class: In TcpSocketBase, it is same as Send() call */
int
TcpSocketBase::SendTo (Ptr<Packet> p, uint32_t flags, const Address &address)
{
  return Send (p, flags); // SendTo() and Send() are the same
}

/* Inherit from Socket class: Return data to upper-layer application. Parameter flags
   is not used. Data is returned as a packet of size no larger than maxSize */
Ptr<Packet>
TcpSocketBase::Recv (uint32_t maxSize, uint32_t flags)
{
  NS_LOG_FUNCTION (this);
  NS_ABORT_MSG_IF (flags, "use of flags is not supported in TcpSocketBase::Recv()");
  if (m_rxBuffer.Size () == 0 && m_state == CLOSE_WAIT)
    {
      return Create<Packet> (); // Send EOF on connection close
    }
  Ptr<Packet> outPacket = m_rxBuffer.Extract (maxSize);
  if (outPacket != 0 && outPacket->GetSize () != 0)
    {
      SocketAddressTag tag;
      if (m_endPoint != 0)
        {
          tag.SetAddress (InetSocketAddress (m_endPoint->GetPeerAddress (), m_endPoint->GetPeerPort ()));
        }
      else if (m_endPoint6 != 0)
        {
          tag.SetAddress (Inet6SocketAddress (m_endPoint6->GetPeerAddress (), m_endPoint6->GetPeerPort ()));
        }
      outPacket->AddPacketTag (tag);
    }
  return outPacket;
}

/* Inherit from Socket class: Recv and return the remote's address */
Ptr<Packet>
TcpSocketBase::RecvFrom (uint32_t maxSize, uint32_t flags, Address &fromAddress)
{
  NS_LOG_FUNCTION (this << maxSize << flags);
  Ptr<Packet> packet = Recv (maxSize, flags);
  // Null packet means no data to read, and an empty packet indicates EOF
  if (packet != 0 && packet->GetSize () != 0)
    {
      if (m_endPoint != 0)
        {
          fromAddress = InetSocketAddress (m_endPoint->GetPeerAddress (), m_endPoint->GetPeerPort ());
        }
      else if (m_endPoint6 != 0)
        {
          fromAddress = Inet6SocketAddress (m_endPoint6->GetPeerAddress (), m_endPoint6->GetPeerPort ());
        }
      else
        {
          fromAddress = InetSocketAddress (Ipv4Address::GetZero (), 0);
        }
    }
  return packet;
}

/* Inherit from Socket class: Get the max number of bytes an app can send */
uint32_t
TcpSocketBase::GetTxAvailable (void) const
{
  NS_LOG_FUNCTION (this);
  return m_txBuffer.Available ();
}

/* Inherit from Socket class: Get the max number of bytes an app can read */
uint32_t
TcpSocketBase::GetRxAvailable (void) const
{
  NS_LOG_FUNCTION (this);
  return m_rxBuffer.Available ();
}

/* Inherit from Socket class: Return local address:port */
int
TcpSocketBase::GetSockName (Address &address) const
{
  NS_LOG_FUNCTION (this);
  if (m_endPoint != 0)
    {
      address = InetSocketAddress (m_endPoint->GetLocalAddress (), m_endPoint->GetLocalPort ());
    }
  else if (m_endPoint6 != 0)
    {
      address = Inet6SocketAddress (m_endPoint6->GetLocalAddress (), m_endPoint6->GetLocalPort ());
    }
  else
    { // It is possible to call this method on a socket without a name
      // in which case, behavior is unspecified
      // Should this return an InetSocketAddress or an Inet6SocketAddress?
      address = InetSocketAddress (Ipv4Address::GetZero (), 0);
    }
  return 0;
}

/* Inherit from Socket class: Bind this socket to the specified NetDevice */
void
TcpSocketBase::BindToNetDevice (Ptr<NetDevice> netdevice)
{
  NS_LOG_FUNCTION (netdevice);
  Socket::BindToNetDevice (netdevice); // Includes sanity check
  if (m_endPoint == 0 && m_endPoint6 == 0)
    {
      if (Bind () == -1)
        {
          NS_ASSERT ((m_endPoint == 0 && m_endPoint6 == 0));
          return;
        }
      NS_ASSERT ((m_endPoint != 0 && m_endPoint6 != 0));
    }

  if (m_endPoint != 0)
    {
      m_endPoint->BindToNetDevice (netdevice);
    }
  // No BindToNetDevice() for Ipv6EndPoint
  return;
}

/* Clean up after Bind. Set up callback functions in the end-point. */
int
TcpSocketBase::SetupCallback (void)
{
  NS_LOG_FUNCTION (this);

  if (m_endPoint == 0 && m_endPoint6 == 0)
    {
      return -1;
    }
  if (m_endPoint != 0)
    {
      m_endPoint->SetRxCallback (MakeCallback (&TcpSocketBase::ForwardUp, Ptr<TcpSocketBase> (this)));
      m_endPoint->SetIcmpCallback (MakeCallback (&TcpSocketBase::ForwardIcmp, Ptr<TcpSocketBase> (this)));
      m_endPoint->SetDestroyCallback (MakeCallback (&TcpSocketBase::Destroy, Ptr<TcpSocketBase> (this)));
    }
  if (m_endPoint6 != 0)
    {
      m_endPoint6->SetRxCallback (MakeCallback (&TcpSocketBase::ForwardUp6, Ptr<TcpSocketBase> (this)));
      m_endPoint6->SetIcmpCallback (MakeCallback (&TcpSocketBase::ForwardIcmp6, Ptr<TcpSocketBase> (this)));
      m_endPoint6->SetDestroyCallback (MakeCallback (&TcpSocketBase::Destroy6, Ptr<TcpSocketBase> (this)));
    }

  return 0;
}//CHECK NOT IN IMPL

/* Perform the real connection tasks: Send SYN if allowed, RST if invalid */
int
TcpSocketBase::DoConnect (void)
{
  NS_LOG_FUNCTION (this);

  cout<<"Inside doconnect(): A new connection is allowed only if this socket does not have a connection"<<endl;
  if (m_state == CLOSED || m_state == LISTEN || m_state == SYN_SENT || m_state == LAST_ACK || m_state == CLOSE_WAIT)
    { // send a SYN packet and change state into SYN_SENT
     // SendEmptyPacket (TcpHeader::SYN);
     // NS_LOG_INFO (TcpStateName[m_state] << " -> SYN_SENT");
     // m_state = SYN_SENT;
//added by dhruv
Actions_t action = OnEvent (APP_CONNECT);
  bool success = OnAction (action);
  if (success) 
    {
      return 0;
    }
  return -1;

    }
  else if (m_state != TIME_WAIT)
    { // In states SYN_RCVD, ESTABLISHED, FIN_WAIT_1, FIN_WAIT_2, and CLOSING, an connection
      // exists. We send RST, tear down everything, and close this socket.
      SendRST ();
      CloseAndNotify ();
    }
  return 0;
}

/* Do the action to close the socket. Usually send a packet with appropriate
    flags depended on the current m_state. */
int
TcpSocketBase::DoClose (void)
{
  NS_LOG_FUNCTION (this);
  switch (m_state)
    {
    case SYN_RCVD:
    case ESTABLISHED:
      // send FIN to close the peer
      SendEmptyPacket (TcpHeader::FIN);
      NS_LOG_INFO ("ESTABLISHED -> FIN_WAIT_1");
      m_state = FIN_WAIT_1;
      break;
    case CLOSE_WAIT:
      // send FIN+ACK to close the peer
      SendEmptyPacket (TcpHeader::FIN | TcpHeader::ACK);
      NS_LOG_INFO ("CLOSE_WAIT -> LAST_ACK");
      m_state = LAST_ACK;
      break;
    case SYN_SENT:
    case CLOSING:
      // Send RST if application closes in SYN_SENT and CLOSING
      SendRST ();
      CloseAndNotify ();
      break;
    case LISTEN:
    case LAST_ACK:
      // In these three states, move to CLOSED and tear down the end point
      CloseAndNotify ();
      break;
    case CLOSED:
    case FIN_WAIT_1:
    case FIN_WAIT_2:
    case TIME_WAIT:
    default: /* mute compiler */
      // Do nothing in these four states
      break;
    }
  return 0;
}

/* Peacefully close the socket by notifying the upper layer and deallocate end point */
void
TcpSocketBase::CloseAndNotify (void)
{
  NS_LOG_FUNCTION (this);

  if (!m_closeNotified)
    {
      NotifyNormalClose ();
    }
  if (m_state != TIME_WAIT)
    {
      DeallocateEndPoint ();
    }
  m_closeNotified = true;
 // NS_LOG_INFO (TcpStateName[m_state] << " -> CLOSED");
  CancelAllTimers ();
  m_state = CLOSED;
}


/* Tell if a sequence number range is out side the range that my rx buffer can
    accpet */
bool
TcpSocketBase::OutOfRange (SequenceNumber32 head, SequenceNumber32 tail) const
{
  if (m_state == LISTEN || m_state == SYN_SENT || m_state == SYN_RCVD)
    { // Rx buffer in these states are not initialized.
      return false;
    }
  if (m_state == LAST_ACK || m_state == CLOSING || m_state == CLOSE_WAIT)
    { // In LAST_ACK and CLOSING states, it only wait for an ACK and the
      // sequence number must equals to m_rxBuffer.NextRxSequence ()
      return (m_rxBuffer.NextRxSequence () != head);
    }

  // In all other cases, check if the sequence number is in range
  return (tail < m_rxBuffer.NextRxSequence () || m_rxBuffer.MaxRxSequence () <= head);
}

/* Function called by the L3 protocol when it received a packet to pass on to
    the TCP. This function is registered as the "RxCallback" function in
    SetupCallback(), which invoked by Bind(), and CompleteFork() */
void
TcpSocketBase::ForwardUp (Ptr<Packet> packet, Ipv4Header header, uint16_t port,
                          Ptr<Ipv4Interface> incomingInterface)
{
  DoForwardUp (packet, header, port, incomingInterface);
}

void
TcpSocketBase::ForwardUp6 (Ptr<Packet> packet, Ipv6Header header, uint16_t port, Ptr<Ipv6Interface> incomingInterface)
{
  DoForwardUp (packet, header, port);
}

void
TcpSocketBase::ForwardIcmp (Ipv4Address icmpSource, uint8_t icmpTtl,
                            uint8_t icmpType, uint8_t icmpCode,
                            uint32_t icmpInfo)
{
  NS_LOG_FUNCTION (this << icmpSource << (uint32_t)icmpTtl << (uint32_t)icmpType <<
                   (uint32_t)icmpCode << icmpInfo);
  if (!m_icmpCallback.IsNull ())
    {
      m_icmpCallback (icmpSource, icmpTtl, icmpType, icmpCode, icmpInfo);
    }
}

void
TcpSocketBase::ForwardIcmp6 (Ipv6Address icmpSource, uint8_t icmpTtl,
                            uint8_t icmpType, uint8_t icmpCode,
                            uint32_t icmpInfo)
{
  NS_LOG_FUNCTION (this << icmpSource << (uint32_t)icmpTtl << (uint32_t)icmpType <<
                   (uint32_t)icmpCode << icmpInfo);
  if (!m_icmpCallback6.IsNull ())
    {
      m_icmpCallback6 (icmpSource, icmpTtl, icmpType, icmpCode, icmpInfo);
    }
}

/* The real function to handle the incoming packet from lower layers. This is
    wrapped by ForwardUp() so that this function can be overloaded by daughter
    classes. */
void
TcpSocketBase::DoForwardUp (Ptr<Packet> packet, Ipv4Header header, uint16_t port,
                            Ptr<Ipv4Interface> incomingInterface)
{
  NS_LOG_LOGIC ("Socket " << this << " forward up from PeerAdd:PeerPort  " <<
                m_endPoint->GetPeerAddress () <<
                ":" << m_endPoint->GetPeerPort () <<
                " to LocalAdd:LocalPort" << m_endPoint->GetLocalAddress () <<
                ":" << m_endPoint->GetLocalPort ());

NS_LOG_LOGIC("Action 1. Peel header and check flags, options and Estimate RTT"<<endl<<"Action 2. Update Rx window"<<endl<<"Action 3. If rwind !=0 cancel the persist timer"<<endl<<"Action 4.Check the set flags in header and set event accordingly and process action"<<endl);

  Address fromAddress = InetSocketAddress (header.GetSource (), port);
  Address toAddress = InetSocketAddress (header.GetDestination (), m_endPoint->GetLocalPort ());

  // Peel off TCP header and do validity checking
  TcpHeader tcpHeader;
  packet->RemoveHeader (tcpHeader);
  if (tcpHeader.GetFlags () & TcpHeader::ACK)
    {
      EstimateRtt (tcpHeader);
    }
  ReadOptions (tcpHeader);


  // Update Rx window size, i.e. the flow control window
  if (m_rWnd.Get () == 0 && tcpHeader.GetWindowSize () != 0)
    { // persist probes end
      NS_LOG_LOGIC (this << " Leaving zerowindow persist state");
      m_persistEvent.Cancel ();
    }
  m_rWnd = tcpHeader.GetWindowSize ();


  // Discard fully out of range data packets
  if (packet->GetSize ()
      && OutOfRange (tcpHeader.GetSequenceNumber (), tcpHeader.GetSequenceNumber () + packet->GetSize ()))
    {
      // Acknowledgement should be sent for all unacceptable packets (RFC793, p.69)
      if ((m_state == ESTABLISHED || m_state == IDLE_S || m_state == IDLE_R)&& !(tcpHeader.GetFlags () & TcpHeader::RST))
        {
          SendEmptyPacket (TcpHeader::ACK);

        }
      return;
    }
Events_t event = SimulationSingleton<ExtendedFiniteStateMachine>::Get ()->FlagsEvent (tcpHeader.GetFlags () );
  // Given an ACK_RX event and FIN_WAIT_1, CLOSING, or LAST_ACK state, 
  // we have to check the sequence numbers to determine if the 
  // ACK is for the FIN
cout<<"Flags"<<unsigned(tcpHeader.GetFlags())<<endl;
//c1=getchar();
 if ((m_state == FIN_WAIT_1 || m_state == CLOSING 
                             || m_state == LAST_ACK) && event == ACK_RX)
    {
      if (tcpHeader.GetSequenceNumber () == m_rxBuffer.NextRxSequence ())
        {
          // This ACK is for the fin, change event to 
          // recognize this
          event = FIN_ACKED;
        }
    }

  Actions_t action = OnEvent (event); //updates the state
  NS_LOG_DEBUG("Socket " << this << 
               " processing pkt action, " << action <<
               " current state " << m_state);
  OnAction_PacketInfo (action, packet, tcpHeader, fromAddress, toAddress);



  /*// TCP state machine code in different process functions
  // C.f.: tcp_rcv_state_process() in tcp_input.c in Linux kernel
  switch (m_state)
    {
    case ESTABLISHED:
      ProcessEstablished (packet, tcpHeader);
      break;
    case LISTEN:
      ProcessListen (packet, tcpHeader, fromAddress, toAddress);
      break;
    case TIME_WAIT:
      // Do nothing
      break;
    case CLOSED:
      // Send RST if the incoming packet is not a RST
      if ((tcpHeader.GetFlags () & ~(TcpHeader::PSH | TcpHeader::URG)) != TcpHeader::RST)
        { // Since m_endPoint is not configured yet, we cannot use SendRST here
          TcpHeader h;
          h.SetFlags (TcpHeader::RST);
          h.SetSequenceNumber (m_nextTxSequence);
          h.SetAckNumber (m_rxBuffer.NextRxSequence ());
          h.SetSourcePort (tcpHeader.GetDestinationPort ());
          h.SetDestinationPort (tcpHeader.GetSourcePort ());
          h.SetWindowSize (AdvertisedWindowSize ());
          AddOptions (h);
          m_tcp->SendPacket (Create<Packet> (), h, header.GetDestination (), header.GetSource (), m_boundnetdevice);
        }
      break;
    case SYN_SENT:
      ProcessSynSent (packet, tcpHeader);
      break;
    case SYN_RCVD:
      Notify_sender (packet, tcpHeader, fromAddress, toAddress);
      break;
    case FIN_WAIT_1:
    case FIN_WAIT_2:
    case CLOSE_WAIT:
      ProcessWait (packet, tcpHeader);
      break;
    case CLOSING:
      ProcessClosing (packet, tcpHeader);
      break;
    case LAST_ACK:
      ProcessLastAck (packet, tcpHeader);
      break;
    default: // mute compiler
      break;}*/
    
}

void
TcpSocketBase::DoForwardUp (Ptr<Packet> packet, Ipv6Header header, uint16_t port)
{
  NS_LOG_LOGIC ("Socket " << this << " forward up " <<
                m_endPoint6->GetPeerAddress () <<
                ":" << m_endPoint6->GetPeerPort () <<
                " to " << m_endPoint6->GetLocalAddress () <<
                ":" << m_endPoint6->GetLocalPort ());
  Address fromAddress = Inet6SocketAddress (header.GetSourceAddress (), port);
  Address toAddress = Inet6SocketAddress (header.GetDestinationAddress (), m_endPoint6->GetLocalPort ());
f_add=fromAddress;
t_add=toAddress;

  // Peel off TCP header and do validity checking
  TcpHeader tcpHeader;
  packet->RemoveHeader (tcpHeader);
  if (tcpHeader.GetFlags () & TcpHeader::ACK)
    {
      EstimateRtt (tcpHeader);
    }
  ReadOptions (tcpHeader);

  // Update Rx window size, i.e. the flow control window
  if (m_rWnd.Get () == 0 && tcpHeader.GetWindowSize () != 0)
    { // persist probes end
      NS_LOG_LOGIC (this << " Leaving zerowindow persist state");
      m_persistEvent.Cancel ();
    }
  m_rWnd = tcpHeader.GetWindowSize ();

  // Discard fully out of range packets
  if (packet->GetSize ()
      && OutOfRange (tcpHeader.GetSequenceNumber (), tcpHeader.GetSequenceNumber () + packet->GetSize ()))
    {
     /* NS_LOG_LOGIC ("At state " << TcpStateName[m_state] <<
                    " received packet of seq [" << tcpHeader.GetSequenceNumber () <<
                    ":" << tcpHeader.GetSequenceNumber () + packet->GetSize () <<
                    ") out of range [" << m_rxBuffer.NextRxSequence () << ":" <<
                    m_rxBuffer.MaxRxSequence () << ")");*/
      // Acknowledgement should be sent for all unacceptable packets (RFC793, p.69)
      if ((m_state == ESTABLISHED || m_state == IDLE_S || m_state == IDLE_R)&& !(tcpHeader.GetFlags () & TcpHeader::RST))
        {
          SendEmptyPacket (TcpHeader::ACK);
        }
      return;
    }

Events_t event = SimulationSingleton<ExtendedFiniteStateMachine>::Get ()->FlagsEvent (tcpHeader.GetFlags () );
  // Given an ACK_RX event and FIN_WAIT_1, CLOSING, or LAST_ACK state, 
  // we have to check the sequence numbers to determine if the 
  // ACK is for the FIN


 if ((m_state == FIN_WAIT_1 || m_state == CLOSING 
                             || m_state == LAST_ACK) && event == ACK_RX)
    {
      if (tcpHeader.GetSequenceNumber () == m_rxBuffer.NextRxSequence ())
        {
          // This ACK is for the fin, change event to 
          // recognize this
          event = FIN_ACKED;
        }
    }

cout<<"mike check "<<StateNames[m_state] <<endl;
  Actions_t action = OnEvent (event); //updates the state
  NS_LOG_DEBUG("Socket " << this << 
               " processing pkt action, " << action <<
               " current state " << m_state);
  OnAction_PacketInfo (action, packet, tcpHeader, fromAddress, toAddress);

/*  // TCP state machine code in different process functions
  // C.f.: tcp_rcv_state_process() in tcp_input.c in Linux kernel
  switch (m_state)
    {
    case ESTABLISHED:
      ProcessEstablished (packet, tcpHeader);
      break;
    case LISTEN:
      ProcessListen (packet, tcpHeader, fromAddress, toAddress);
      break;
    case TIME_WAIT:
      // Do nothing
      break;
    case CLOSED:
      // Send RST if the incoming packet is not a RST
      if ((tcpHeader.GetFlags () & ~(TcpHeader::PSH | TcpHeader::URG)) != TcpHeader::RST)
        { // Since m_endPoint is not configured yet, we cannot use SendRST here
          TcpHeader h;
          h.SetFlags (TcpHeader::RST);
          h.SetSequenceNumber (m_nextTxSequence);
          h.SetAckNumber (m_rxBuffer.NextRxSequence ());
          h.SetSourcePort (tcpHeader.GetDestinationPort ());
          h.SetDestinationPort (tcpHeader.GetSourcePort ());
          h.SetWindowSize (AdvertisedWindowSize ());
          AddOptions (h);
          m_tcp->SendPacket (Create<Packet> (), h, header.GetDestinationAddress (), header.GetSourceAddress (), m_boundnetdevice);
        }
      break;
    case SYN_SENT:
      ProcessSynSent (packet, tcpHeader);
      break;
    case SYN_RCVD:
      Notify_sender (packet, tcpHeader, fromAddress, toAddress);
      break;
    case FIN_WAIT_1:
    case FIN_WAIT_2:
    case CLOSE_WAIT:
      ProcessWait (packet, tcpHeader);
      break;
    case CLOSING:
      ProcessClosing (packet, tcpHeader);
      break;
    case LAST_ACK:
      ProcessLastAck (packet, tcpHeader);
      break;
    default: // mute compiler
      break;
    }*/
}

/* Received a packet upon ESTABLISHED state. This function is mimicking the
    role of tcp_rcv_established() in tcp_input.c in Linux kernel. */
void
TcpSocketBase::ProcessEstablished (Ptr<Packet> packet, const TcpHeader& tcpHeader)
{
  NS_LOG_FUNCTION (this << tcpHeader);

  // Extract the flags. PSH and URG are not honoured.
  uint8_t tcpflags = tcpHeader.GetFlags () & ~(TcpHeader::PSH | TcpHeader::URG);

  // Different flags are different events
  if (tcpflags == TcpHeader::ACK)
    {
      ReceivedAck (packet, tcpHeader);
    }
  else if (tcpflags == TcpHeader::SYN)
    { // Received SYN, old NS-3 behaviour is to set state to SYN_RCVD and
      // respond with a SYN+ACK. But it is not a legal state transition as of
      // RFC793. Thus this is ignored.
    }
  else if (tcpflags == (TcpHeader::SYN | TcpHeader::ACK))
    { // No action for received SYN+ACK, it is probably a duplicated packet
    }
  else if (tcpflags == TcpHeader::FIN || tcpflags == (TcpHeader::FIN | TcpHeader::ACK))
    { // Received FIN or FIN+ACK, bring down this socket nicely
      PeerClose (packet, tcpHeader);
    }
  else if (tcpflags == 0)
    { // No flags means there is only data
      ReceivedData (packet, tcpHeader);
      if (m_rxBuffer.Finished ())
        {
          PeerClose (packet, tcpHeader);
        }
    }
  else
    { // Received RST or the TCP flags is invalid, in either case, terminate this socket
      if (tcpflags != TcpHeader::RST)
        { // this must be an invalid flag, send reset
          NS_LOG_LOGIC ("Illegal flag " << tcpflags << " received. Reset packet is sent.");
          SendRST ();
        }
      CloseAndNotify ();
    }
}

/* Process the newly received ACK */
void
TcpSocketBase::ReceivedAck (Ptr<Packet> packet, const TcpHeader& tcpHeader)
{
  NS_LOG_FUNCTION (this << tcpHeader);


 Actions_t action = OnEvent (ACK_CHECK);
  OnAction_PacketInfo (action,packet,tcpHeader,f_add,t_add);

if(m_newack){

// code for sender
  if (tcpHeader.GetAckNumber () > m_txBuffer.HeadSequence ())
    { // Case 3: New ACK, reset m_dupAckCount and update m_txBuffer
        NS_LOG_LOGIC ("New ack of " << tcpHeader.GetAckNumber ());
        m_state=RECEIVED_NEW_ACK;
        cout<<"RTT estimated, window updates, options read"<<endl;
        Actions_t action = OnEvent (FORCED_TRANS);
        OnAction_PacketInfo (action,packet,tcpHeader,f_add,t_add);
        if(action==1){
        	cout<<"You just had an ACK being sent"<<endl;
	        	//c1= getchar();
        }
        cout<<"DEBUG: NewAck of each variant called to handle tx differently"<<endl;
        m_dupAckCount = 0;
      saved_seq=tcpHeader.GetAckNumber();

      TcpSocketBase::NewAck (saved_seq); 
    }

}
m_newack=true;

  // If there is any data piggybacked, store it into m_rxBuffer
  if (packet->GetSize () > 0)
    {
cout<<"THE ACK HAD DATA PIGGYBACKED"<<endl;

 Actions_t action = OnEvent (DATA_RECV);
  OnAction_PacketInfo (action,packet,tcpHeader,f_add,t_add);
    }
    
}

/* Received a packet upon LISTEN state. */
void
TcpSocketBase::ProcessListen (Ptr<Packet> packet, const TcpHeader& tcpHeader,
                              const Address& fromAddress, const Address& toAddress)
{
  NS_LOG_FUNCTION (this << tcpHeader);

  // Extract the flags. PSH and URG are not honoured.
  uint8_t tcpflags = tcpHeader.GetFlags () & ~(TcpHeader::PSH | TcpHeader::URG);

  // Fork a socket if received a SYN. Do nothing otherwise.
  // C.f.: the LISTEN part in tcp_v4_do_rcv() in tcp_ipv4.c in Linux kernel
  if (tcpflags != TcpHeader::SYN)
    {
      return;
    }

  // Clone the socket, simulate fork
//After TcpSocketBase cloned, allocate a new end point to handle the incoming connection and send a SYN+ACK to complete the handshake.
  Ptr<TcpSocketBase> newSock = Fork ();
  NS_LOG_LOGIC ("Cloned a TcpSocketBase " << newSock);
  Simulator::ScheduleNow (&TcpSocketBase::CompleteFork, newSock,
                          packet, tcpHeader, fromAddress, toAddress);
}

/* Received a packet upon SYN_SENT */
void
TcpSocketBase::ProcessSynSent (Ptr<Packet> packet, const TcpHeader& tcpHeader)
{
  NS_LOG_FUNCTION (this << tcpHeader);

  // Extract the flags. PSH and URG are not honoured.
  uint8_t tcpflags = tcpHeader.GetFlags () & ~(TcpHeader::PSH | TcpHeader::URG);

  if (tcpflags == 0)
    { // Bare data, accept it and move to ESTABLISHED state. This is not a normal behaviour. Remove this?
      NS_LOG_INFO ("SYN_SENT -> ESTABLISHED");
      m_state = ESTABLISHED;
      m_connected = true;
      m_retxEvent.Cancel ();
      m_delAckCount = m_delAckMaxCount;
      ReceivedData (packet, tcpHeader);
      Simulator::ScheduleNow (&TcpSocketBase::ConnectionSucceeded, this);
    }
  else if (tcpflags == TcpHeader::ACK)
    { // Ignore ACK in SYN_SENT
    }
  else if (tcpflags == TcpHeader::SYN)
    { // Received SYN, move to SYN_RCVD state and respond with SYN+ACK
      NS_LOG_INFO ("SYN_SENT -> SYN_RCVD");
      m_state = SYN_RCVD;
      m_cnCount = m_cnRetries;
      m_rxBuffer.SetNextRxSequence (tcpHeader.GetSequenceNumber () + SequenceNumber32 (1));
      SendEmptyPacket (TcpHeader::SYN | TcpHeader::ACK);
    }
  else if (tcpflags == (TcpHeader::SYN | TcpHeader::ACK)
           && m_nextTxSequence + SequenceNumber32 (1) == tcpHeader.GetAckNumber ())
    { // Handshake completed
      NS_LOG_INFO ("SYN_SENT -> ESTABLISHED");
      m_state = ESTABLISHED;
     /* m_connected = true;
      m_retxEvent.Cancel ();
      m_rxBuffer.SetNextRxSequence (tcpHeader.GetSequenceNumber () + SequenceNumber32 (1));
      m_highTxMark = ++m_nextTxSequence;
      m_txBuffer.SetHeadSequence (m_nextTxSequence);
      SendEmptyPacket (TcpHeader::ACK);
      SendPendingData (m_connected);
      Simulator::ScheduleNow (&TcpSocketBase::ConnectionSucceeded, this);
      // Always respond to first data packet to speed up the connection.
      // Remove to get the behaviour of old NS-3 code.
      m_delAckCount = m_delAckMaxCount;*/
    }
  else
    { // Other in-sequence input
      if (tcpflags != TcpHeader::RST)
        { // When (1) rx of FIN+ACK; (2) rx of FIN; (3) rx of bad flags
          NS_LOG_LOGIC ("Illegal flag " << std::hex << static_cast<uint32_t> (tcpflags) << std::dec << " received. Reset packet is sent.");
          SendRST ();
        }
      CloseAndNotify ();
    }
}

/* Received a packet upon SYN_RCVD */
void
TcpSocketBase::Notify_sender (Ptr<Packet> packet, const TcpHeader& tcpHeader,
                               const Address& fromAddress, const Address& toAddress)
{
  NS_LOG_FUNCTION (this << tcpHeader);

  // Extract the flags. PSH and URG are not honoured.
  uint8_t tcpflags = tcpHeader.GetFlags () & ~(TcpHeader::PSH | TcpHeader::URG);

  if (tcpflags == 0
      || (tcpflags == TcpHeader::ACK
          && m_nextTxSequence + SequenceNumber32 (1) == tcpHeader.GetAckNumber ()))
    { // If it is bare data, accept it and move to ESTABLISHED state. This is
      // possibly due to ACK lost in 3WHS. If in-sequence ACK is received, the
      // handshake is completed nicely.
     // NS_LOG_INFO ("SYN_RCVD -> ESTABLISHED");
//      m_state = ESTABLISHED;
      m_connected = true;
      m_retxEvent.Cancel ();
      m_highTxMark = ++m_nextTxSequence;
      m_txBuffer.SetHeadSequence (m_nextTxSequence);
      if (m_endPoint)
        {
          m_endPoint->SetPeer (InetSocketAddress::ConvertFrom (fromAddress).GetIpv4 (),
                               InetSocketAddress::ConvertFrom (fromAddress).GetPort ());
        }
      else if (m_endPoint6)
        {
          m_endPoint6->SetPeer (Inet6SocketAddress::ConvertFrom (fromAddress).GetIpv6 (),
                                Inet6SocketAddress::ConvertFrom (fromAddress).GetPort ());
        }
      // Always respond to first data packet to speed up the connection.
      // Remove to get the behaviour of old NS-3 code.
      m_delAckCount = m_delAckMaxCount;
      ReceivedAck (packet, tcpHeader);
      NotifyNewConnectionCreated (this, fromAddress);
      // As this connection is established, the socket is available to send data now
      if (GetTxAvailable () > 0)
        {
          NotifySend (GetTxAvailable ());
        }
    }
  else if (tcpflags == TcpHeader::SYN)
    { // Probably the peer lost my SYN+ACK
      m_rxBuffer.SetNextRxSequence (tcpHeader.GetSequenceNumber () + SequenceNumber32 (1));
      SendEmptyPacket (TcpHeader::SYN | TcpHeader::ACK);
    }
  else if (tcpflags == (TcpHeader::FIN | TcpHeader::ACK))
    {
      if (tcpHeader.GetSequenceNumber () == m_rxBuffer.NextRxSequence ())
        { // In-sequence FIN before connection complete. Set up connection and close.
          m_connected = true;
          m_retxEvent.Cancel ();
          m_highTxMark = ++m_nextTxSequence;
          m_txBuffer.SetHeadSequence (m_nextTxSequence);
          if (m_endPoint)
            {
              m_endPoint->SetPeer (InetSocketAddress::ConvertFrom (fromAddress).GetIpv4 (),
                                   InetSocketAddress::ConvertFrom (fromAddress).GetPort ());
            }
          else if (m_endPoint6)
            {
              m_endPoint6->SetPeer (Inet6SocketAddress::ConvertFrom (fromAddress).GetIpv6 (),
                                    Inet6SocketAddress::ConvertFrom (fromAddress).GetPort ());
            }
          PeerClose (packet, tcpHeader);
        }
    }
  else
    { // Other in-sequence input
      if (tcpflags != TcpHeader::RST)
        { // When (1) rx of SYN+ACK; (2) rx of FIN; (3) rx of bad flags
          NS_LOG_LOGIC ("Illegal flag " << tcpflags << " received. Reset packet is sent.");
          if (m_endPoint)
            {
              m_endPoint->SetPeer (InetSocketAddress::ConvertFrom (fromAddress).GetIpv4 (),
                                   InetSocketAddress::ConvertFrom (fromAddress).GetPort ());
            }
          else if (m_endPoint6)
            {
              m_endPoint6->SetPeer (Inet6SocketAddress::ConvertFrom (fromAddress).GetIpv6 (),
                                    Inet6SocketAddress::ConvertFrom (fromAddress).GetPort ());
            }
          SendRST ();
        }
      CloseAndNotify ();
    }
}

/* Received a packet upon CLOSE_WAIT, FIN_WAIT_1, or FIN_WAIT_2 states */
void
TcpSocketBase::ProcessWait (Ptr<Packet> packet, const TcpHeader& tcpHeader)
{
  NS_LOG_FUNCTION (this << tcpHeader);

  // Extract the flags. PSH and URG are not honoured.
  uint8_t tcpflags = tcpHeader.GetFlags () & ~(TcpHeader::PSH | TcpHeader::URG);

  if (packet->GetSize () > 0 && tcpflags != TcpHeader::ACK)
    { // Bare data, accept it
      ReceivedData (packet, tcpHeader);
    }
  else if (tcpflags == TcpHeader::ACK)
    { // Process the ACK, and if in FIN_WAIT_1, conditionally move to FIN_WAIT_2
      ReceivedAck (packet, tcpHeader);
      if (m_state == FIN_WAIT_1 && m_txBuffer.Size () == 0
          && tcpHeader.GetAckNumber () == m_highTxMark + SequenceNumber32 (1))
        { // This ACK corresponds to the FIN sent
          NS_LOG_INFO ("FIN_WAIT_1 -> FIN_WAIT_2");
          m_state = FIN_WAIT_2;
        }
    }
  else if (tcpflags == TcpHeader::FIN || tcpflags == (TcpHeader::FIN | TcpHeader::ACK))
    { // Got FIN, respond with ACK and move to next state
      if (tcpflags & TcpHeader::ACK)
        { // Process the ACK first
          ReceivedAck (packet, tcpHeader);
        }
      m_rxBuffer.SetFinSequence (tcpHeader.GetSequenceNumber ());
    }
  else if (tcpflags == TcpHeader::SYN || tcpflags == (TcpHeader::SYN | TcpHeader::ACK))
    { // Duplicated SYN or SYN+ACK, possibly due to spurious retransmission
      return;
    }
  else
    { // This is a RST or bad flags
      if (tcpflags != TcpHeader::RST)
        {
          NS_LOG_LOGIC ("Illegal flag " << tcpflags << " received. Reset packet is sent.");
          SendRST ();
        }
      CloseAndNotify ();
      return;
    }

  // Check if the close responder sent an in-sequence FIN, if so, respond ACK
  if ((m_state == FIN_WAIT_1 || m_state == FIN_WAIT_2) && m_rxBuffer.Finished ())
    {
      if (m_state == FIN_WAIT_1)
        {
          NS_LOG_INFO ("FIN_WAIT_1 -> CLOSING");
          m_state = CLOSING;
          if (m_txBuffer.Size () == 0
              && tcpHeader.GetAckNumber () == m_highTxMark + SequenceNumber32 (1))
            { // This ACK corresponds to the FIN sent
              TimeWait ();
            }
        }
      else if (m_state == FIN_WAIT_2)
        {
          TimeWait ();
        }
      SendEmptyPacket (TcpHeader::ACK);
      if (!m_shutdownRecv)
        {
          NotifyDataRecv ();
        }
    }
}

/* Received a packet upon CLOSING */
void
TcpSocketBase::ProcessClosing (Ptr<Packet> packet, const TcpHeader& tcpHeader)
{
  NS_LOG_FUNCTION (this << tcpHeader);

  // Extract the flags. PSH and URG are not honoured.
  uint8_t tcpflags = tcpHeader.GetFlags () & ~(TcpHeader::PSH | TcpHeader::URG);

  if (tcpflags == TcpHeader::ACK)
    {
      if (tcpHeader.GetSequenceNumber () == m_rxBuffer.NextRxSequence ())
        { // This ACK corresponds to the FIN sent
          TimeWait ();
        }
    }
  else
    { // CLOSING state means simultaneous close, i.e. no one is sending data to
      // anyone. If anything other than ACK is received, respond with a reset.
      if (tcpflags == TcpHeader::FIN || tcpflags == (TcpHeader::FIN | TcpHeader::ACK))
        { // FIN from the peer as well. We can close immediately.
          SendEmptyPacket (TcpHeader::ACK);
        }
      else if (tcpflags != TcpHeader::RST)
        { // Receive of SYN or SYN+ACK or bad flags or pure data
          NS_LOG_LOGIC ("Illegal flag " << tcpflags << " received. Reset packet is sent.");
          SendRST ();
        }
      CloseAndNotify ();
    }
}

/* Received a packet upon LAST_ACK */
void
TcpSocketBase::ProcessLastAck (Ptr<Packet> packet, const TcpHeader& tcpHeader)
{
  NS_LOG_FUNCTION (this << tcpHeader);

  // Extract the flags. PSH and URG are not honoured.
  uint8_t tcpflags = tcpHeader.GetFlags () & ~(TcpHeader::PSH | TcpHeader::URG);

  if (tcpflags == 0)
    {
      ReceivedData (packet, tcpHeader);
    }
  else if (tcpflags == TcpHeader::ACK)
    {
      if (tcpHeader.GetSequenceNumber () == m_rxBuffer.NextRxSequence ())
        { // This ACK corresponds to the FIN sent. This socket closed peacefully.
          CloseAndNotify ();
        }
    }
  else if (tcpflags == TcpHeader::FIN)
    { // Received FIN again, the peer probably lost the FIN+ACK
      SendEmptyPacket (TcpHeader::FIN | TcpHeader::ACK);
    }
  else if (tcpflags == (TcpHeader::FIN | TcpHeader::ACK) || tcpflags == TcpHeader::RST)
    {
      CloseAndNotify ();
    }
  else
    { // Received a SYN or SYN+ACK or bad flags
      NS_LOG_LOGIC ("Illegal flag " << tcpflags << " received. Reset packet is sent.");
      SendRST ();
      CloseAndNotify ();
    }
}

/* Peer sent me a FIN. Remember its sequence in rx buffer. */
void
TcpSocketBase::PeerClose (Ptr<Packet> p, const TcpHeader& tcpHeader)
{
  NS_LOG_FUNCTION (this << tcpHeader);

  // Ignore all out of range packets
  if (tcpHeader.GetSequenceNumber () < m_rxBuffer.NextRxSequence ()
      || tcpHeader.GetSequenceNumber () > m_rxBuffer.MaxRxSequence ())
    {
      return;
    }
  // For any case, remember the FIN position in rx buffer first
  m_rxBuffer.SetFinSequence (tcpHeader.GetSequenceNumber () + SequenceNumber32 (p->GetSize ()));
  NS_LOG_LOGIC ("Accepted FIN at seq " << tcpHeader.GetSequenceNumber () + SequenceNumber32 (p->GetSize ()));
  // If there is any piggybacked data, process it
  if (p->GetSize ())
    {
      ReceivedData (p, tcpHeader);
    }
  // Return if FIN is out of sequence, otherwise move to CLOSE_WAIT state by DoPeerClose
  if (!m_rxBuffer.Finished ())
    {
      return;
    }

  // Simultaneous close: Application invoked Close() when we are processing this FIN packet
  if (m_state == FIN_WAIT_1)
    {
      NS_LOG_INFO ("FIN_WAIT_1 -> CLOSING");
      m_state = CLOSING;
      return;
    }

  DoPeerClose (); // Change state, respond with ACK
}

/* Received a in-sequence FIN. Close down this socket. */
void
TcpSocketBase::DoPeerClose (void)
{
  NS_ASSERT (m_state == ESTABLISHED || m_state == SYN_RCVD || m_state == IDLE_R);

  // Move the state to CLOSE_WAIT
 // NS_LOG_INFO (TcpStateName[m_state] << " -> CLOSE_WAIT");
//  m_state = CLOSE_WAIT;

  if (!m_closeNotified)
    {
      // The normal behaviour for an application is that, when the peer sent a in-sequence
      // FIN, the app should prepare to close. The app has two choices at this point: either
      // respond with ShutdownSend() call to declare that it has nothing more to send and
      // the socket can be closed immediately; or remember the peer's close request, wait
      // until all its existing data are pushed into the TCP socket, then call Close()
      // explicitly.
      NS_LOG_LOGIC ("TCP " << this << " calling NotifyNormalClose");
      NotifyNormalClose ();
      m_closeNotified = true;
    }
  if (m_shutdownSend)
    { // The application declares that it would not sent any more, close this socket
      Close ();
    }
  else
    { // Need to ack, the application will close later
      SendEmptyPacket (TcpHeader::ACK);
    }
  if (m_state == LAST_ACK)
    {
      NS_LOG_LOGIC ("TcpSocketBase " << this << " scheduling LATO1");
      m_lastAckEvent = Simulator::Schedule (m_rtt->RetransmitTimeout (),
                                            &TcpSocketBase::LastAckTimeout, this);
    }
}

/* Kill this socket. This is a callback function configured to m_endpoint in
   SetupCallback(), invoked when the endpoint is destroyed. */
void
TcpSocketBase::Destroy (void)
{
  NS_LOG_FUNCTION (this);
  m_endPoint = 0;
  if (m_tcp != 0)
    {
      std::vector<Ptr<TcpSocketBase> >::iterator it
        = std::find (m_tcp->m_sockets.begin (), m_tcp->m_sockets.end (), this);
      if (it != m_tcp->m_sockets.end ())
        {
          m_tcp->m_sockets.erase (it);
        }
    }
  NS_LOG_LOGIC (this << " Cancelled ReTxTimeout event which was set to expire at " <<
                (Simulator::Now () + Simulator::GetDelayLeft (m_retxEvent)).GetSeconds ());
  CancelAllTimers ();
}

/* Kill this socket. This is a callback function configured to m_endpoint in
   SetupCallback(), invoked when the endpoint is destroyed. */
void
TcpSocketBase::Destroy6 (void)
{
  NS_LOG_FUNCTION (this);
  m_endPoint6 = 0;
  if (m_tcp != 0)
    {
      std::vector<Ptr<TcpSocketBase> >::iterator it
        = std::find (m_tcp->m_sockets.begin (), m_tcp->m_sockets.end (), this);
      if (it != m_tcp->m_sockets.end ())
        {
          m_tcp->m_sockets.erase (it);
        }
    }
  NS_LOG_LOGIC (this << " Cancelled ReTxTimeout event which was set to expire at " <<
                (Simulator::Now () + Simulator::GetDelayLeft (m_retxEvent)).GetSeconds ());
  CancelAllTimers ();
}

/* Send an empty packet with specified TCP flags */
void
TcpSocketBase::SendEmptyPacket (uint8_t flags)
{
  NS_LOG_FUNCTION (this << (uint32_t)flags);
  NS_LOG_LOGIC ("Action 1. Prepare packet by setting seq, ack no., options, flags etc. "<<endl<<"Action 2. Set src, dest port"<<endl<<"Action 3. Set window size using rwind"<<endl<<"Action 4. Schedule RETx timer and finally send the packet"<<endl);

  Ptr<Packet> p = Create<Packet> ();
  TcpHeader header;
  SequenceNumber32 s = m_nextTxSequence;

  /*
   * Add tags for each socket option.
   * Note that currently the socket adds both IPv4 tag and IPv6 tag
   * if both options are set. Once the packet got to layer three, only
   * the corresponding tags will be read.
   */
  if (IsManualIpTos ())
    {
      SocketIpTosTag ipTosTag;
      ipTosTag.SetTos (GetIpTos ());
      p->AddPacketTag (ipTosTag);
    }

  if (IsManualIpv6Tclass ())
    {
      SocketIpv6TclassTag ipTclassTag;
      ipTclassTag.SetTclass (GetIpv6Tclass ());
      p->AddPacketTag (ipTclassTag);
    }

  if (IsManualIpTtl ())
    {
      SocketIpTtlTag ipTtlTag;
      ipTtlTag.SetTtl (GetIpTtl ());
      p->AddPacketTag (ipTtlTag);
    }

  if (IsManualIpv6HopLimit ())
    {
      SocketIpv6HopLimitTag ipHopLimitTag;
      ipHopLimitTag.SetHopLimit (GetIpv6HopLimit ());
      p->AddPacketTag (ipHopLimitTag);
    }

  if (m_endPoint == 0 && m_endPoint6 == 0)
    {
      NS_LOG_WARN ("Failed to send empty packet due to null endpoint");
      return;
    }
  if (flags & TcpHeader::FIN)
    {
      flags |= TcpHeader::ACK;
    }
  else if (m_state == FIN_WAIT_1 || m_state == LAST_ACK || m_state == CLOSING)
    {
      ++s;
    }

  header.SetFlags (flags);
  header.SetSequenceNumber (s);
  header.SetAckNumber (m_rxBuffer.NextRxSequence ());
  if (m_endPoint != 0)
    {
      header.SetSourcePort (m_endPoint->GetLocalPort ());
      header.SetDestinationPort (m_endPoint->GetPeerPort ());
    }
  else
    {
      header.SetSourcePort (m_endPoint6->GetLocalPort ());
      header.SetDestinationPort (m_endPoint6->GetPeerPort ());
    }

if(m_state==CHECK_REC){
  header.SetWindowSize (AdvertisedWindowSize ()); 
Actions_t action1 = OnEvent (FORCED_TRANS);
          OnAction (action1);
}

else{
  header.SetWindowSize (AdvertisedWindowSize ()); 
}
AddOptions (header);
  m_rto = m_rtt->RetransmitTimeout ();
  bool hasSyn = flags & TcpHeader::SYN;
  bool hasFin = flags & TcpHeader::FIN;
  bool isAck = flags == TcpHeader::ACK;
  if (hasSyn)
    {
      if (m_cnCount == 0)
        { // No more connection retries, give up
          NS_LOG_LOGIC ("Connection failed.");
          CloseAndNotify ();
          return;
        }
      else
        { // Exponential backoff of connection time out
          int backoffCount = 0x1 << (m_cnRetries - m_cnCount);
          m_rto = m_cnTimeout * backoffCount;
          m_cnCount--;
        }
    }
  if (m_endPoint != 0)
    {
      m_tcp->SendPacket (p, header, m_endPoint->GetLocalAddress (),
                         m_endPoint->GetPeerAddress (), m_boundnetdevice);
    }
  else
    {
      m_tcp->SendPacket (p, header, m_endPoint6->GetLocalAddress (),
                         m_endPoint6->GetPeerAddress (), m_boundnetdevice);
    }
  if (flags & TcpHeader::ACK)
    { // If sending an ACK, cancel the delay ACK as well
      m_delAckEvent.Cancel ();
      m_delAckCount = 0;
    }
  if (m_retxEvent.IsExpired () && (hasSyn || hasFin) && !isAck )
    { // Retransmit SYN / SYN+ACK / FIN / FIN+ACK to guard against lost
      NS_LOG_LOGIC ("Schedule retransmission timeout at time "
                    << Simulator::Now ().GetSeconds () << " to expire at time "
                    << (Simulator::Now () + m_rto.Get ()).GetSeconds ());
      m_retxEvent = Simulator::Schedule (m_rto, &TcpSocketBase::SendEmptyPacket, this, flags);

    }
}

/* This function closes the endpoint completely. Called upon RST_TX action. */
void
TcpSocketBase::SendRST (void)
{
  NS_LOG_FUNCTION (this);
  SendEmptyPacket (TcpHeader::RST);
  NotifyErrorClose ();
  DeallocateEndPoint ();
}

/* Deallocate the end point and cancel all the timers */
void
TcpSocketBase::DeallocateEndPoint (void)
{
  if (m_endPoint != 0)
    {
      m_endPoint->SetDestroyCallback (MakeNullCallback<void> ());
      m_tcp->DeAllocate (m_endPoint);
      m_endPoint = 0;
      std::vector<Ptr<TcpSocketBase> >::iterator it
        = std::find (m_tcp->m_sockets.begin (), m_tcp->m_sockets.end (), this);
      if (it != m_tcp->m_sockets.end ())
        {
          m_tcp->m_sockets.erase (it);
        }
      CancelAllTimers ();
    }
  if (m_endPoint6 != 0)
    {
      m_endPoint6->SetDestroyCallback (MakeNullCallback<void> ());
      m_tcp->DeAllocate (m_endPoint6);
      m_endPoint6 = 0;
      std::vector<Ptr<TcpSocketBase> >::iterator it
        = std::find (m_tcp->m_sockets.begin (), m_tcp->m_sockets.end (), this);
      if (it != m_tcp->m_sockets.end ())
        {
          m_tcp->m_sockets.erase (it);
        }
      CancelAllTimers ();
    }
}

/* Configure the endpoint to a local address. Called by Connect() if Bind() didn't specify one. */
int
TcpSocketBase::SetupEndpoint ()
{
  NS_LOG_FUNCTION (this);
  Ptr<Ipv4> ipv4 = m_node->GetObject<Ipv4> ();
  NS_ASSERT (ipv4 != 0);
  if (ipv4->GetRoutingProtocol () == 0)
    {
      NS_FATAL_ERROR ("No Ipv4RoutingProtocol in the node");
    }
  // Create a dummy packet, then ask the routing function for the best output
  // interface's address
  Ipv4Header header;
  header.SetDestination (m_endPoint->GetPeerAddress ());
  Socket::SocketErrno errno_;
  Ptr<Ipv4Route> route;
  Ptr<NetDevice> oif = m_boundnetdevice;
  route = ipv4->GetRoutingProtocol ()->RouteOutput (Ptr<Packet> (), header, oif, errno_);
  if (route == 0)
    {
      NS_LOG_LOGIC ("Route to " << m_endPoint->GetPeerAddress () << " does not exist");
      NS_LOG_ERROR (errno_);
      m_errno = errno_;
      return -1;
    }
  NS_LOG_LOGIC ("Route exists");
  m_endPoint->SetLocalAddress (route->GetSource ());
  return 0;
}

int
TcpSocketBase::SetupEndpoint6 ()
{
  NS_LOG_FUNCTION (this);
  Ptr<Ipv6L3Protocol> ipv6 = m_node->GetObject<Ipv6L3Protocol> ();
  NS_ASSERT (ipv6 != 0);
  if (ipv6->GetRoutingProtocol () == 0)
    {
      NS_FATAL_ERROR ("No Ipv6RoutingProtocol in the node");
    }
  // Create a dummy packet, then ask the routing function for the best output
  // interface's address
  Ipv6Header header;
  header.SetDestinationAddress (m_endPoint6->GetPeerAddress ());
  Socket::SocketErrno errno_;
  Ptr<Ipv6Route> route;
  Ptr<NetDevice> oif = m_boundnetdevice;
  route = ipv6->GetRoutingProtocol ()->RouteOutput (Ptr<Packet> (), header, oif, errno_);
  if (route == 0)
    {
      NS_LOG_LOGIC ("Route to " << m_endPoint6->GetPeerAddress () << " does not exist");
      NS_LOG_ERROR (errno_);
      m_errno = errno_;
      return -1;
    }
  NS_LOG_LOGIC ("Route exists");
  m_endPoint6->SetLocalAddress (route->GetSource ());
  return 0;
}

/* This function is called only if a SYN received in LISTEN state. After
   TcpSocketBase cloned, allocate a new end point to handle the incoming
   connection and send a SYN+ACK to complete the handshake. */
void
TcpSocketBase::CompleteFork (Ptr<Packet> p, const TcpHeader& h,
                             const Address& fromAddress, const Address& toAddress)
{

NS_LOG_LOGIC("Action 1. Update count of connection retries"<<endl<<"Action 1. Advance the next Rx Sequnce no. by 1"<<endl);

  // Get port and address from peer (connecting host)
  if (InetSocketAddress::IsMatchingType (toAddress))
    {
      m_endPoint = m_tcp->Allocate (InetSocketAddress::ConvertFrom (toAddress).GetIpv4 (),
                                    InetSocketAddress::ConvertFrom (toAddress).GetPort (),
                                    InetSocketAddress::ConvertFrom (fromAddress).GetIpv4 (),
                                    InetSocketAddress::ConvertFrom (fromAddress).GetPort ());
      m_endPoint6 = 0;
    }
  else if (Inet6SocketAddress::IsMatchingType (toAddress))
    {
      m_endPoint6 = m_tcp->Allocate6 (Inet6SocketAddress::ConvertFrom (toAddress).GetIpv6 (),
                                      Inet6SocketAddress::ConvertFrom (toAddress).GetPort (),
                                      Inet6SocketAddress::ConvertFrom (fromAddress).GetIpv6 (),
                                      Inet6SocketAddress::ConvertFrom (fromAddress).GetPort ());
      m_endPoint = 0;
    }
  m_tcp->m_sockets.push_back (this);

  m_cnCount = m_cnRetries;
  SetupCallback ();
  // Set the sequence number and send SYN+ACK

  m_rxBuffer.SetNextRxSequence (h.GetSequenceNumber () + SequenceNumber32 (1)); //adding 1 to seq number
  
  Actions_t actionrockstar = OnEvent (SEND_SYN_ACK);
          OnAction (actionrockstar);


}

void
TcpSocketBase::ConnectionSucceeded ()
{ // Wrapper to protected function NotifyConnectionSucceeded() so that it can
  // be called as a scheduled event
  NotifyConnectionSucceeded ();
  // The if-block below was moved from ProcessSynSent() to here because we need
  // to invoke the NotifySend() only after NotifyConnectionSucceeded() to
  // reflect the behaviour in the real world.
  if (GetTxAvailable () > 0)
    {
      NotifySend (GetTxAvailable ());
    }
}

/* Extract at most maxSize bytes from the TxBuffer at sequence seq, add the
    TCP header, and send to TcpL4Protocol 
    TCP header, and send to TcpL4Protocol */
uint32_t
TcpSocketBase::SendDataPacket (SequenceNumber32 seq, uint32_t maxSize, bool withAck)
{

  NS_LOG_FUNCTION (this << seq << maxSize << withAck);

NS_LOG_LOGIC("Action 1. Copy the data (seq,seq+maxsize) from Tx buffer"<<endl<<"Action 2. Prepare a full packet."<<endl<<"Action 3. Add the src, dest port and set the window size using r_wind"<<endl<<"Act 4. Schedule ReTx timeout and set the highest seq no. ever sent");

  Ptr<Packet> p = m_txBuffer.CopyFromSequence (maxSize, seq);
  uint32_t sz = p->GetSize (); // Size of packet
  uint8_t flags = withAck ? TcpHeader::ACK : 0;
  uint32_t remainingData = m_txBuffer.SizeFromSequence (seq + SequenceNumber32 (sz));

  /*
   * Add tags for each socket option.
   * Note that currently the socket adds both IPv4 tag and IPv6 tag
   * if both options are set. Once the packet got to layer three, only
   * the corresponding tags will be read.
   */
  if (IsManualIpTos ())
    {
      SocketIpTosTag ipTosTag;
      ipTosTag.SetTos (GetIpTos ());
      p->AddPacketTag (ipTosTag);
    }

  if (IsManualIpv6Tclass ())
    {
      SocketIpv6TclassTag ipTclassTag;
      ipTclassTag.SetTclass (GetIpv6Tclass ());
      p->AddPacketTag (ipTclassTag);
    }

  if (IsManualIpTtl ())
    {
      SocketIpTtlTag ipTtlTag;
      ipTtlTag.SetTtl (GetIpTtl ());
      p->AddPacketTag (ipTtlTag);
    }

  if (IsManualIpv6HopLimit ())
    {
      SocketIpv6HopLimitTag ipHopLimitTag;
      ipHopLimitTag.SetHopLimit (GetIpv6HopLimit ());
      p->AddPacketTag (ipHopLimitTag);
    }

  if (m_closeOnEmpty && (remainingData == 0))
    {
      flags |= TcpHeader::FIN;
      if (m_state == ESTABLISHED)
        { // On active close: I am the first one to send FIN
          NS_LOG_INFO ("ESTABLISHED -> FIN_WAIT_1");
          m_state = FIN_WAIT_1;
        }
      else if (m_state == CLOSE_WAIT)
        { // On passive close: Peer sent me FIN already
          NS_LOG_INFO ("CLOSE_WAIT -> LAST_ACK");
          m_state = LAST_ACK;
        }
    }
  TcpHeader header;
  header.SetFlags (flags);
  header.SetSequenceNumber (seq);
  header.SetAckNumber (m_rxBuffer.NextRxSequence ());
  if (m_endPoint)
    {
      header.SetSourcePort (m_endPoint->GetLocalPort ());
      header.SetDestinationPort (m_endPoint->GetPeerPort ());
    }
  else
    {
      header.SetSourcePort (m_endPoint6->GetLocalPort ());
      header.SetDestinationPort (m_endPoint6->GetPeerPort ());
    }
  header.SetWindowSize (AdvertisedWindowSize ()); 
  AddOptions (header);
  if (m_retxEvent.IsExpired () )
    { // Schedule retransmit
      m_rto = m_rtt->RetransmitTimeout ();
      NS_LOG_LOGIC (this << " SendDataPacket Schedule ReTxTimeout at time " <<
                    Simulator::Now ().GetSeconds () << " to expire at time " <<
                    (Simulator::Now () + m_rto.Get ()).GetSeconds () );
      m_retxEvent = Simulator::Schedule (m_rto, &TcpSocketBase::ReTxTimeout, this);
    }
  NS_LOG_LOGIC ("Send packet via TcpL4Protocol with flags 0x" << std::hex << static_cast<uint32_t> (flags) << std::dec);
  if (m_endPoint)
    {
      m_tcp->SendPacket (p, header, m_endPoint->GetLocalAddress (),
                         m_endPoint->GetPeerAddress (), m_boundnetdevice);
    }
  else
    {
      m_tcp->SendPacket (p, header, m_endPoint6->GetLocalAddress (),
                         m_endPoint6->GetPeerAddress (), m_boundnetdevice);
    }
  m_rtt->SentSeq (seq, sz);       // notify the RTT
  // Notify the application of the data being sent unless this is a retransmit
  if (seq == m_nextTxSequence)
    {
      Simulator::ScheduleNow (&TcpSocketBase::NotifyDataSent, this, sz);
    }
  // Update highTxMark
  m_highTxMark = std::max (seq + sz, m_highTxMark.Get ());
  return sz;
}
/* Send as much pending data as possible according to the Tx window. Note that
 *  this function did not implement the PSH flag
 */
bool
TcpSocketBase::SendPendingData (bool withAck)
{

  NS_LOG_LOGIC ("Action 1. Checks the available window size"<<endl<<"Action 2. Sends the data as long as available window allows, (through while loop)");
  NS_LOG_FUNCTION (this << withAck);
  if (m_txBuffer.Size () == 0)
    {
      return false;                           // Nothing to send

    }
  if (m_endPoint == 0 && m_endPoint6 == 0)
    {
      NS_LOG_INFO ("TcpSocketBase::SendPendingData: No endpoint; m_shutdownSend=" << m_shutdownSend);
      return false; // Is this the right way to handle this condition?
    }
  uint32_t nPacketsSent = 0;
  while (m_txBuffer.SizeFromSequence (m_nextTxSequence))
    {

cout<<"DEBUG: LOOPING IN WHILE LOOP : "<<endl;
uint32_t w = AvailableWindow (); // Get available window size
      NS_LOG_LOGIC ("TcpSocketBase " << this << " SendPendingData" <<
                    " rxwin " << m_rWnd <<"w"<<w<<
                    " segsize " << m_segmentSize <<
                    " nextTxSeq " << m_nextTxSequence <<
                    " highestRxAck " << m_txBuffer.HeadSequence () <<
                    " pd->Size " << m_txBuffer.Size () <<
                    " pd->SFS " << m_txBuffer.SizeFromSequence (m_nextTxSequence));
     
Actions_t action1 = OnEvent (CHECK_NAGLE);
  OnAction (action1);

if(nagle){
cout<<"Stopped sending. Has to wait for higher Tx window OR amount of data to be sent is < 1 segment"<<endl;
break;

}


Actions_t action2 = OnEvent (SEND_DATA);
  OnAction (action2);

      nPacketsSent++;                             // Count sent this loop
      m_nextTxSequence += sz_1;                     // Advance next tx sequence
//DEBUG: sz here is the number of bytes sent
    }

  NS_LOG_LOGIC ("*******SENDPENDINGDATA sent " << nPacketsSent << " packets****************************************");
  
  nagle=false;
m_state=IDLE_S;
//just in case. Not needed actually
//Actions_t action8 = OnEvent (FORCED_TRANS);
 // OnAction (action8);
  return (nPacketsSent > 0);
}
 
uint32_t
TcpSocketBase::UnAckDataCount ()
{
  NS_LOG_FUNCTION (this);
  return m_nextTxSequence.Get () - m_txBuffer.HeadSequence ();

//DEBUG: Next seq num to be sent - first byte's sequence number
}

uint32_t
TcpSocketBase::BytesInFlight ()
{
  NS_LOG_FUNCTION (this);
  return m_highTxMark.Get () - m_txBuffer.HeadSequence ();
}

uint32_t
TcpSocketBase::Window ()
{

cout<<"DEBUG: Inside TcpSocketBase window method"<<endl;
  NS_LOG_FUNCTION (this);
  return m_rWnd;
}

uint32_t
TcpSocketBase::AvailableWindow ()
{
  NS_LOG_FUNCTION_NOARGS ();
  uint32_t unack = UnAckDataCount (); // Number of outstanding bytes
  uint32_t win = Window (); // Number of bytes allowed to be outstanding
  NS_LOG_LOGIC ("UnAckCount=" << unack << ", Win=" << win);
  return (win < unack) ? 0 : (win - unack);
}

uint16_t
TcpSocketBase::AdvertisedWindowSize ()
{
  return std::min (m_rxBuffer.MaxBufferSize () - m_rxBuffer.Size (), (uint32_t)m_maxWinSize);
}

// Receipt of new packet, put into Rx buffer
void
TcpSocketBase::ReceivedData (Ptr<Packet> p, const TcpHeader& tcpHeader)
{
NS_LOG_LOGIC("Action 1: Puts the data in the Rx buffer"<<endl<<"Action 2: Notify the app of data reception"<<endl);

  NS_LOG_FUNCTION (this << tcpHeader);
  NS_LOG_LOGIC ("seq " << tcpHeader.GetSequenceNumber () <<
                " ack " << tcpHeader.GetAckNumber () <<
                " pkt size " << p->GetSize () );

  // Put into Rx buffer
  SequenceNumber32 expectedSeq = m_rxBuffer.NextRxSequence ();


 Actions_t action = OnEvent (CHECK_RX_BUFFER);
  OnAction_PacketInfo (action,p,tcpHeader,f_add,t_add);

if(m_bufferfull){
m_bufferfull=false;
return;
}
 
// Notify app to receive if necessary
  if (expectedSeq < m_rxBuffer.NextRxSequence ())
    { // NextRxSeq advanced, we have something to send to the app
  Actions_t action = OnEvent (NOTIFY_APP);
  OnAction_PacketInfo (action,p,tcpHeader,f_add,t_add);

    }
/*
  NS_LOG_FUNCTION (this << tcpHeader);
  NS_LOG_LOGIC ("seq " << tcpHeader.GetSequenceNumber () <<
                " ack " << tcpHeader.GetAckNumber () <<
                " pkt size " << p->GetSize () );

  // Put into Rx buffer
  SequenceNumber32 expectedSeq = m_rxBuffer.NextRxSequence ();
  if (!m_rxBuffer.Add (p, tcpHeader))
    { // Insert failed: No data or RX buffer full
      SendEmptyPacket (TcpHeader::ACK);
cout<<"first++"<<endl;
      return;

    }
  // Now send a new ACK packet acknowledging all received and delivered data
  if (m_rxBuffer.Size () > m_rxBuffer.Available () || m_rxBuffer.NextRxSequence () > expectedSeq + p->GetSize ())
    { // A gap exists in the buffer, or we filled a gap: Always ACK
      SendEmptyPacket (TcpHeader::ACK);
cout<<"second++"<<endl;
    }
  else
    { // In-sequence packet: ACK if delayed ack count allows
      if (++m_delAckCount >= m_delAckMaxCount)
        {
          m_delAckEvent.Cancel ();
          m_delAckCount = 0;
          SendEmptyPacket (TcpHeader::ACK);
cout<<"third++"<<endl;
        }
      else if (m_delAckEvent.IsExpired ())
        {
          m_delAckEvent = Simulator::Schedule (m_delAckTimeout,
                                               &TcpSocketBase::DelAckTimeout, this);
          NS_LOG_LOGIC (this << " scheduled delayed ACK at " << (Simulator::Now () + Simulator::GetDelayLeft (m_delAckEvent)).GetSeconds ());
cout<<"fourth++"<<endl;
        }
    }
  // Notify app to receive if necessary
  if (expectedSeq < m_rxBuffer.NextRxSequence ())
    { // NextRxSeq advanced, we have something to send to the app
      if (!m_shutdownRecv)
        {
          NotifyDataRecv ();
cout<<"fifth++"<<endl;
        }
      // Handle exceptions
      if (m_closeNotified)
        {
          NS_LOG_WARN ("Why TCP " << this << " got data after close notification?");
cout<<"sixth++"<<endl;
        }
      // If we received FIN before and now completed all "holes" in rx buffer,
      // invoke peer close procedure
      if (m_rxBuffer.Finished () && (tcpHeader.GetFlags () & TcpHeader::FIN) == 0)
        {
cout<<"seventh++"<<endl;
          DoPeerClose ();
        }
    }
  */
}

/* Called by ForwardUp() to estimate RTT */
void
TcpSocketBase::EstimateRtt (const TcpHeader& tcpHeader)
{
  // Use m_rtt for the estimation. Note, RTT of duplicated acknowledgement
  // (which should be ignored) is handled by m_rtt. Once timestamp option
  // is implemented, this function would be more elaborated.
  Time nextRtt =  m_rtt->AckSeq (tcpHeader.GetAckNumber () );

  //nextRtt will be zero for dup acks.  Don't want to update lastRtt in that case
  //but still needed to do list clearing that is done in AckSeq. 
  if(nextRtt != 0)
  {
    m_lastRtt = nextRtt;
    NS_LOG_FUNCTION(this << m_lastRtt);
  }
  
}

// Called by the ReceivedAck() when new ACK received and by Notify_sender()
// when the three-way handshake completed. This cancels retransmission timer
// and advances Tx window
void
TcpSocketBase::NewAck (SequenceNumber32 const& ack)
{
  NS_LOG_FUNCTION (this << ack);

NS_LOG_LOGIC("Action 1: Set RTO"<<endl<<"Action 2: Check persist state"<<endl<<"Action 3:Discard data up to but not including this sequence number"<<endl<<"Action 4: Notify app of data ,Sendpending data"<<endl);
Actions_t action1 =OnEvent (SET_RTO);
OnAction (action1);



if (m_rWnd.Get () == 0 && m_persistEvent.IsExpired ())
    { // Zero window: Enter persist state to send 1 byte to probe

 Actions_t action2 = OnEvent (RWIND_0);
  OnAction (action2);  
m_state = IDLE_S;
return;  
    }
  // Note the highest ACK and tell app to send more
  NS_LOG_LOGIC ("TCP " << this << " NewAck " << ack <<
                " numberAck " << (ack - m_txBuffer.HeadSequence ())); // Number bytes ack'ed

  m_txBuffer.DiscardUpTo (ack);

Actions_t action3 = OnEvent (FORCED_TRANS);
  OnAction (action3);

Actions_t action = OnEvent (FORCED_TRANS);
  OnAction (action);
}

// Retransmit timeout
void
TcpSocketBase::ReTxTimeout ()
{
  NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC (this << " ReTxTimeout Expired at time " << Simulator::Now ().GetSeconds ());
  // If erroneous timeout in closed/timed-wait state, just return
  if (m_state == CLOSED || m_state == TIME_WAIT)
    {
      return;
    }
  // If all data are received (non-closing socket and nothing to send), just return
  if (m_state <= ESTABLISHED && m_txBuffer.HeadSequence () >= m_highTxMark)
    {
      return;
    }

  Retransmit ();
}

void
TcpSocketBase::DelAckTimeout (void)
{
  m_delAckCount = 0;
  SendEmptyPacket (TcpHeader::ACK);
}

void
TcpSocketBase::LastAckTimeout (void)
{
  NS_LOG_FUNCTION (this);

  m_lastAckEvent.Cancel ();
  if (m_state == LAST_ACK)
    {
      CloseAndNotify ();
    }
  if (!m_closeNotified)
    {
      m_closeNotified = true;
    }
}

// Send 1-byte data to probe for the window size at the receiver when
// the local knowledge tells that the receiver has zero window size
// C.f.: RFC793 p.42, RFC1112 sec.4.2.2.17
void
TcpSocketBase::PersistTimeout ()
{
  NS_LOG_LOGIC ("PersistTimeout expired at " << Simulator::Now ().GetSeconds ());
  m_persistTimeout = std::min (Seconds (60), Time (2 * m_persistTimeout)); // max persist timeout = 60s
  Ptr<Packet> p = m_txBuffer.CopyFromSequence (1, m_nextTxSequence);
  TcpHeader tcpHeader;
  tcpHeader.SetSequenceNumber (m_nextTxSequence);
  tcpHeader.SetAckNumber (m_rxBuffer.NextRxSequence ());
  tcpHeader.SetWindowSize (AdvertisedWindowSize ()); 
  if (m_endPoint != 0)
    {
      tcpHeader.SetSourcePort (m_endPoint->GetLocalPort ());
      tcpHeader.SetDestinationPort (m_endPoint->GetPeerPort ());
    }
  else
    {
      tcpHeader.SetSourcePort (m_endPoint6->GetLocalPort ());
      tcpHeader.SetDestinationPort (m_endPoint6->GetPeerPort ());
    }
  AddOptions (tcpHeader);

  if (m_endPoint != 0)
    {
      m_tcp->SendPacket (p, tcpHeader, m_endPoint->GetLocalAddress (),
                         m_endPoint->GetPeerAddress (), m_boundnetdevice);
    }
  else
    {
      m_tcp->SendPacket (p, tcpHeader, m_endPoint6->GetLocalAddress (),
                         m_endPoint6->GetPeerAddress (), m_boundnetdevice);
    }
  NS_LOG_LOGIC ("Schedule persist timeout at time "
                << Simulator::Now ().GetSeconds () << " to expire at time "
                << (Simulator::Now () + m_persistTimeout).GetSeconds ());
  m_persistEvent = Simulator::Schedule (m_persistTimeout, &TcpSocketBase::PersistTimeout, this);
}

void
TcpSocketBase::Retransmit ()
{

cout<<"iske andar aaya hu"<<endl;
  m_nextTxSequence = m_txBuffer.HeadSequence (); // Start from highest Ack
  m_rtt->IncreaseMultiplier (); // Double the timeout value for next retx timer
  m_dupAckCount = 0;
  DoRetransmit (); // Retransmit the packet
}

void
TcpSocketBase::DoRetransmit ()
{
  NS_LOG_FUNCTION (this);
  // Retransmit SYN packet
  if (m_state == SYN_SENT)
    {
      if (m_cnCount > 0)
        {
          SendEmptyPacket (TcpHeader::SYN);
        }
      else
        {
          NotifyConnectionFailed ();
        }
      return;
    }
  // Retransmit non-data packet: Only if in FIN_WAIT_1 or CLOSING state
  if (m_txBuffer.Size () == 0)
    {
      if (m_state == FIN_WAIT_1 || m_state == CLOSING)
        { // Must have lost FIN, re-send
          SendEmptyPacket (TcpHeader::FIN);
        }
      return;
    }
  // Retransmit a data packet: Call SendDataPacket
  NS_LOG_LOGIC ("TcpSocketBase " << this << " retxing seq " << m_txBuffer.HeadSequence ());
  uint32_t sz = SendDataPacket (m_txBuffer.HeadSequence (), m_segmentSize, true);
  // In case of RTO, advance m_nextTxSequence
  m_nextTxSequence = std::max (m_nextTxSequence.Get (), m_txBuffer.HeadSequence () + sz);

}

void
TcpSocketBase::CancelAllTimers ()
{
  m_retxEvent.Cancel ();
  m_persistEvent.Cancel ();
  m_delAckEvent.Cancel ();
  m_lastAckEvent.Cancel ();
  m_timewaitEvent.Cancel ();
}

/* Move TCP to Time_Wait state and schedule a transition to Closed state */
void
TcpSocketBase::TimeWait ()
{
  //NS_LOG_INFO (TcpStateName[m_state] << " -> TIME_WAIT");
  m_state = TIME_WAIT;
  CancelAllTimers ();
  // Move from TIME_WAIT to CLOSED after 2*MSL. Max segment lifetime is 2 min
  // according to RFC793, p.28
  m_timewaitEvent = Simulator::Schedule (Seconds (2 * m_msl),
                                         &TcpSocketBase::CloseAndNotify, this);
}

/* Below are the attribute get/set functions */

void
TcpSocketBase::SetSndBufSize (uint32_t size)
{
  m_txBuffer.SetMaxBufferSize (size);
}

uint32_t
TcpSocketBase::GetSndBufSize (void) const
{
  return m_txBuffer.MaxBufferSize ();
}

void
TcpSocketBase::SetRcvBufSize (uint32_t size)
{
  m_rxBuffer.SetMaxBufferSize (size);
}

uint32_t
TcpSocketBase::GetRcvBufSize (void) const
{
  return m_rxBuffer.MaxBufferSize ();
}

void
TcpSocketBase::SetSegSize (uint32_t size)
{
  m_segmentSize = size;
  NS_ABORT_MSG_UNLESS (m_state == CLOSED, "Cannot change segment size dynamically.");
}

uint32_t
TcpSocketBase::GetSegSize (void) const
{
  return m_segmentSize;
}

void
TcpSocketBase::SetConnTimeout (Time timeout)
{
  m_cnTimeout = timeout;
}

Time
TcpSocketBase::GetConnTimeout (void) const
{
  return m_cnTimeout;
}

void
TcpSocketBase::SetConnCount (uint32_t count)
{
  m_cnRetries = count;
}

uint32_t
TcpSocketBase::GetConnCount (void) const
{
  return m_cnRetries;
}

void
TcpSocketBase::SetDelAckTimeout (Time timeout)
{
  m_delAckTimeout = timeout;
}

Time
TcpSocketBase::GetDelAckTimeout (void) const
{
  return m_delAckTimeout;
}

void
TcpSocketBase::SetDelAckMaxCount (uint32_t count)
{
  m_delAckMaxCount = count;
}

uint32_t
TcpSocketBase::GetDelAckMaxCount (void) const
{
  return m_delAckMaxCount;
}

void
TcpSocketBase::SetTcpNoDelay (bool noDelay)
{
  m_noDelay = noDelay;
}

bool
TcpSocketBase::GetTcpNoDelay (void) const
{
  return m_noDelay;
}

void
TcpSocketBase::SetPersistTimeout (Time timeout)
{
  m_persistTimeout = timeout;
}

Time
TcpSocketBase::GetPersistTimeout (void) const
{
  return m_persistTimeout;
}

bool
TcpSocketBase::SetAllowBroadcast (bool allowBroadcast)
{
  // Broadcast is not implemented. Return true only if allowBroadcast==false
  return (!allowBroadcast);
}

bool
TcpSocketBase::GetAllowBroadcast (void) const
{
  return false;
}

/* Placeholder function for future extension that reads more from the TCP header */
void
TcpSocketBase::ReadOptions (const TcpHeader&)
{
}

/* Placeholder function for future extension that changes the TCP header */
void
TcpSocketBase::AddOptions (TcpHeader&)
{
}

} // namespace ns3
  
