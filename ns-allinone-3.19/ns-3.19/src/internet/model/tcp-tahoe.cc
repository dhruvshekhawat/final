/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
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
#include<stdio.h>
#include "tcp-tahoe.h"
#include "ns3/log.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/simulator.h"
#include "ns3/abort.h"
#include "ns3/node.h"
#include <string>
#include <math.h>
#include <stdlib.h>

//#include "libxml2/libxml/xmlmemory.h"
//#include "libxml2/libxml/parser.h"
#include <stdlib.h>
#include <wchar.h>
#include <fstream>

using namespace std;
NS_LOG_COMPONENT_DEFINE ("TcpTahoe");
char beta[6][50];
int c;
char alpha[6][50];
char K[2][50];
int i_beta,i_alpha, i_k;
ofstream myfile;

static void parseDoc(const char *docname)
{
    xmlDocPtr doc;
    xmlNodePtr cur;
    xmlChar *key;
    i_beta = 0;
    i_alpha = 0;
    i_k=0;
    doc = xmlParseFile(docname);

    if (doc == NULL) {
        fprintf(stderr, "Document not parsed successfully. \n");
        return;
    }

    cur = xmlDocGetRootElement(doc);

    if (cur == NULL) {
        fprintf(stderr, "empty document\n");
        xmlFreeDoc(doc);
        return;
    }

    for (cur = cur->children; cur != NULL; cur = cur->next) {
        if (cur->type != XML_ELEMENT_NODE)
            continue;
        key = xmlNodeGetContent(cur);
       if(i_beta<6){
        strcpy(beta[i_beta],(char*)key);

        xmlFree(key);
        i_beta++;}
   else{
          if(i_alpha<6){
         strcpy(alpha[i_alpha],(char*)key);
        xmlFree(key);
        i_alpha++; }
else{
 strcpy(K[i_k],(char*)key);


        xmlFree(key);
        i_k++; }
}
}    
cout<<"beta1: "<<beta[0]<<", beta2: "<<beta[1]<<", beta3: "<<beta[2]<<", beta4: "<<beta[3]<<", beta5: "<<beta[4]<<", beta6: "<<beta[5]<<endl;
cout<<"alpha1: "<<alpha[0]<<", alpha2: "<<alpha[1]<<", alpha3: "<<alpha[2]<<", alpha4: "<<alpha[3]<<", alpha5: "<<alpha[4]<<", alpha6: "<<alpha[5]<<endl;
cout<<"K_ss"<<K[0]<<"K_ca"<<K[1]<<endl;

    xmlFreeDoc(doc);
return;
}

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (TcpTahoe)
  ;

TypeId
TcpTahoe::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TcpTahoe")
    .SetParent<TcpSocketBase> ()
    .AddConstructor<TcpTahoe> ()
    .AddAttribute ("ReTxThreshold", "Threshold for fast retransmit",
                    UintegerValue (3),
                    MakeUintegerAccessor (&TcpTahoe::m_retxThresh),
                    MakeUintegerChecker<uint32_t> ())
    .AddTraceSource ("CongestionWindow",
                     "The TCP connection's congestion window",
                     MakeTraceSourceAccessor (&TcpTahoe::m_cWnd))
  ;
  return tid;
}

TcpTahoe::TcpTahoe (void) : m_initialCWnd (1), m_retxThresh (3)
{

  NS_LOG_FUNCTION (this);
}

TcpTahoe::TcpTahoe (const TcpTahoe& sock)
  : TcpSocketBase (sock),
    m_cWnd (sock.m_cWnd),
    m_ssThresh (sock.m_ssThresh),
    m_initialCWnd (sock.m_initialCWnd),
    m_retxThresh (sock.m_retxThresh)

{

  NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC ("Invoked the copy constructor");

}

TcpTahoe::~TcpTahoe (void)
{
}

/* We initialize m_cWnd from this function, after attributes initialized */
int
TcpTahoe::Listen (void)
{
  NS_LOG_FUNCTION (this);
  InitializeCwnd ();
  return TcpSocketBase::Listen ();
}

/* We initialize m_cWnd from this function, after attributes initialized */
int
TcpTahoe::Connect (const Address & address)
{
  NS_LOG_FUNCTION (this << address);
  InitializeCwnd ();
  return TcpSocketBase::Connect (address);

}

/* Limit the size of in-flight data by cwnd and receiver's rxwin */
uint32_t
TcpTahoe::Window (void)
{

cout<<"DEBUG: Inside TCPtahoe window method"<<endl;
  NS_LOG_FUNCTION (this);
 return std::min (m_rWnd.Get (), m_cWnd.Get ());
}

Ptr<TcpSocketBase>
TcpTahoe::Fork (void)
{
  return CopyObject<TcpTahoe> (this);
}

/* New ACK (up to seqnum seq) received. Increase cwnd and call TcpSocketBase::NewAck() */
void
TcpTahoe::NewAck (SequenceNumber32 const& seq)
{
myfile.open("cwndvals.txt",std::ios_base::app);
cout<<"In NewAck. Do you want to change the coefficients?"<<endl;
//c= getchar();

const char *docname="/home/dhruvie/cwnd_progression.xml";
parseDoc (docname);
//will parse the document each time
  NS_LOG_FUNCTION (this << seq);
  NS_LOG_LOGIC ("TcpTahoe receieved ACK for seq " << seq <<
                " cwnd " << m_cWnd <<
                " ssthresh " << m_ssThresh);

int x=m_cWnd/m_segmentSize;

  if (m_cWnd < m_ssThresh)
    { // Slow start mode, add one segSize to cWnd. Default m_ssThresh is 65535. (RFC2001, sec.1)


      x = atoi(beta[0])*pow(x, -3) + atoi(beta[1])*pow(x, -2) + atoi(beta[2])*pow(x, -1) +atoi(K[0]) + atoi(alpha[0])*pow(x, 1) + atoi(alpha[1])*pow(x, 2) + atoi(alpha[2])* pow(x, 3);



      m_cWnd=x*m_segmentSize;
myfile<<"In SlowStart, updated to cwnd " << m_cWnd << " ssthresh " << m_ssThresh<<"\n";
      NS_LOG_INFO ("In SlowStart, updated to cwnd " << m_cWnd << " ssthresh " << m_ssThresh);

    }
  else
    { // Congestion avoidance mode, increase by (segSize*segSize)/cwnd. (RFC2581, sec.3.1)
      // To increase cwnd for one segSize per RTT, it should be (ackBytes*segSize)/cwnd
      /*double adder = static_cast<double> (m_segmentSize * m_segmentSize) / m_cWnd.Get ();
      adder = std::max (1.0, adder);
      m_cWnd += static_cast<uint32_t> (adder);*/

     x = atoi(beta[3])*pow(x, -3) + atoi(beta[4])*pow(x, -2) + atoi(beta[5])*pow(x, -1) +atoi(K[1]) + atoi(alpha[3])*pow(x, 1) + atoi(alpha[4])*pow(x, 2) + atoi(alpha[5])* pow(x, 3);
      m_cWnd=x*m_segmentSize;
myfile<<"In CongAvoid, updated to cwnd " << m_cWnd << " ssthresh " << m_ssThresh<<"\n";
      NS_LOG_INFO ("In CongAvoid, updated to cwnd " << m_cWnd << " ssthresh " << m_ssThresh);
    }
          // Complete newAck processing
myfile.close();
}



/* Cut down ssthresh upon triple dupack */
void
TcpTahoe::DupAck (const TcpHeader& t, uint32_t count)
{
  NS_LOG_FUNCTION (this << "t " << count);
  if (count == m_retxThresh)
    { 
      NS_LOG_INFO ("Triple Dup Ack: old ssthresh " << m_ssThresh << " cwnd " << m_cWnd);
      m_ssThresh = std::max (static_cast<unsigned> (m_cWnd / 2), m_segmentSize * 2);  // Half ssthresh
      m_cWnd = m_segmentSize; // Run slow start again
      m_nextTxSequence = m_txBuffer.HeadSequence (); // Restart from highest Ack
      NS_LOG_INFO ("Triple Dup Ack: new ssthresh " << m_ssThresh << " cwnd " << m_cWnd);
      NS_LOG_LOGIC ("Triple Dup Ack: retransmit missing segment at " << Simulator::Now ().GetSeconds ());
      DoRetransmit ();
    }

}

/* Retransmit timeout */
void TcpTahoe::Retransmit (void)
{

cout<<"TAHOE ke retx ke andar aaya "<<endl;
  NS_LOG_FUNCTION (this);

  NS_LOG_LOGIC (this << " ReTxTimeout Expired at time " << Simulator::Now ().GetSeconds ());
  // If erroneous timeout in closed/timed-wait state, just return
  if (m_state == CLOSED || m_state == TIME_WAIT) return;
  // If all data are received (non-closing socket and nothing to send), just return
  if (m_state <= IDLE_S && m_txBuffer.HeadSequence () >= m_highTxMark) return;
  m_ssThresh = std::max (static_cast<unsigned> (m_cWnd / 2), m_segmentSize * 2);  // Half ssthresh
  m_cWnd = m_segmentSize;                   // Set cwnd to 1 segSize (RFC2001, sec.2)
  m_nextTxSequence = m_txBuffer.HeadSequence (); // Restart from highest Ack
  m_rtt->IncreaseMultiplier ();             // Double the next RTO
  DoRetransmit ();                          // Retransmit the packet
}

void
TcpTahoe::SetSegSize (uint32_t size)
{
  NS_ABORT_MSG_UNLESS (m_state == CLOSED, "TcpTahoe::SetSegSize() cannot change segment size after connection started.");
  m_segmentSize = size;
}

void
TcpTahoe::SetSSThresh (uint32_t threshold)
{
  m_ssThresh = threshold;
}

uint32_t
TcpTahoe::GetSSThresh (void) const
{
  return m_ssThresh;
}

void
TcpTahoe::SetInitialCwnd (uint32_t cwnd)
{
  NS_ABORT_MSG_UNLESS (m_state == CLOSED, "TcpTahoe::SetInitialCwnd() cannot change initial cwnd after connection started.");
  m_initialCWnd = cwnd;
}

uint32_t
TcpTahoe::GetInitialCwnd (void) const
{
  return m_initialCWnd;
}

void 
TcpTahoe::InitializeCwnd (void)
{
  /*
   * Initialize congestion window, default to 1 MSS (RFC2001, sec.1) and must
   * not be larger than 2 MSS (RFC2581, sec.3.1). Both m_initiaCWnd and
   * m_segmentSize are set by the attribute system in ns3::TcpSocket.
   */
  m_cWnd = m_initialCWnd * m_segmentSize;
}

} // namespace ns3
  
