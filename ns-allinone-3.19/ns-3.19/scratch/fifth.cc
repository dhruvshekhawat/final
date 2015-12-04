/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
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
#include<iostream>
#include <fstream>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"

using namespace std;
using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("TcpExample");

// ===========================================================================
//
//         node 0                 node 1
//   +----------------+    +----------------+
//   |    ns-3 TCP    |    |    ns-3 TCP    |
//   +----------------+    +----------------+
//   |    10.1.1.1    |    |    10.1.1.2    |
//   +----------------+    +----------------+
//   | point-to-point |    | point-to-point |
//   +----------------+    +----------------+
//           |                     |
//           +---------------------+
//                5 Mbps, 5 ms
//

class MyApp : public Application
{
public:
  MyApp ();
  virtual ~MyApp ();

  void Setup (Ptr<Socket> socket, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate);

private:
  virtual void StartApplication (void);
  virtual void StopApplication (void);

  void ScheduleTx (void);
  void SendPacket (void);

  Ptr<Socket>     m_socket;
  Address         m_peer;
  uint32_t        m_packetSize;
  uint32_t        m_nPackets;
  DataRate        m_dataRate;
  EventId         m_sendEvent;
  bool            m_running;
  uint32_t        m_packetsSent;
};

MyApp::MyApp ()
  : m_socket (0),
    m_peer (),
    m_packetSize (0),
    m_nPackets (0),
    m_dataRate (0),
    m_sendEvent (),
    m_running (false),
    m_packetsSent (0)
{
}

MyApp::~MyApp ()
{
  m_socket = 0;
}

void
MyApp::Setup (Ptr<Socket> socket, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate)
{
  m_socket = socket;
  m_peer = address;
  m_packetSize = packetSize;
  m_nPackets = nPackets;
  m_dataRate = dataRate;
}

void
MyApp::StartApplication (void)
{
  m_running = true;
  m_packetsSent = 0;
  m_socket->Bind ();
  m_socket->Connect (m_peer);
  SendPacket ();
}

void
MyApp::StopApplication (void)
{
  m_running = false;

  if (m_sendEvent.IsRunning ())
    {
      Simulator::Cancel (m_sendEvent);
    }

  if (m_socket)
    {
      m_socket->Close ();
    }
}

void
MyApp::SendPacket (void)
{
  Ptr<Packet> packet = Create<Packet> (m_packetSize);
  m_socket->Send (packet);

  if (++m_packetsSent < m_nPackets)
    {
      ScheduleTx ();
    }
}

void
MyApp::ScheduleTx (void)
{
  if (m_running)
    {
      Time tNext (Seconds (m_packetSize * 8 / static_cast<double> (m_dataRate.GetBitRate ())));
      m_sendEvent = Simulator::Schedule (tNext, &MyApp::SendPacket, this);
    }
}

static void
CwndChange (Ptr<OutputStreamWrapper> stream, uint32_t oldCwnd, uint32_t newCwnd)
{
  //NS_LOG_UNCOND (Simulator::Now ().GetSeconds () << "\t" << newCwnd);

cout<<"DEBUG: cwnd was traced"<<endl;
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "oldcwnd   \t" << oldCwnd << "newcwnd  \t" << newCwnd << std::endl;
}

int
main (int argc, char *argv[])
{


LogComponentEnable("TcpL4Protocol", LOG_LEVEL_ALL);
LogComponentEnable("TcpSocketBase", LOG_LEVEL_ALL);
LogComponentEnable("TcpTahoe", LOG_LEVEL_ALL);


    

  //change these parameters for different simulations
  std::string tcp_variant = "TcpTahoe";
  std::string bandwidth = "5Mbps";
  std::string delay = "5ms";
  double error_rate = 0.000001;
  int queuesize = 10; //packets
  int simulation_time = 10; //seconds

  // Select TCP variant
  if (tcp_variant.compare("TcpTahoe") == 0)
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue (TcpTahoe::GetTypeId()));
  else if (tcp_variant.compare("TcpReno") == 0)
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue (TcpReno::GetTypeId()));
  else if (tcp_variant.compare("TcpNewReno") == 0)
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue (TcpNewReno::GetTypeId()));
  else
    {
      fprintf (stderr, "Invalid TCP version\n");
      exit (1);
    }

cout<<"DEBUG: TCP Variant Assigned"<<endl;

  //set qsize
  Config::SetDefault ("ns3::DropTailQueue::MaxPackets", UintegerValue(uint32_t(queuesize)));
 

  NodeContainer nodes;
  nodes.Create (2);

cout<<"DEBUG:Two nodes were created"<<endl;

  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute ("DataRate", StringValue (bandwidth));
  pointToPoint.SetChannelAttribute ("Delay", StringValue (delay));

  NetDeviceContainer devices;
  devices = pointToPoint.Install (nodes);

cout<<"DEBUG: Two nodes have been connected"<<endl;

 // Now, let's use the ListErrorModel and explicitly force a loss
  // of the packets with pkt-uids = 11 and 17 on node 2, device 0
  //std::list<uint32_t> sampleList;
  //sampleList.push_back (11);
  //sampleList.push_back (17);
  // This time, we'll explicitly create the error model we want
  //Ptr<ListErrorModel> pem = CreateObject<ListErrorModel> ();
  //pem->SetList (sampleList);
//  devices.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (pem));




  Ptr<RateErrorModel> em = CreateObject<RateErrorModel> ();
  em->SetAttribute ("ErrorRate", DoubleValue (error_rate));
  devices.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (em));

cout<<"The initialize method of InternetStackHelper calls TCPL4protocol"<<endl;

  InternetStackHelper stack;
  stack.Install (nodes);


cout<<"DEBUG: Stack layers have been loaded on the two nodes"<<endl;


  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.252");
  Ipv4InterfaceContainer interfaces = address.Assign (devices);

cout<<"DEBUG: IP addresses assigned to the two nodes"<<endl;


  uint16_t sinkPort = 8080;
  Address sinkAddress (InetSocketAddress (interfaces.GetAddress (1), sinkPort));
  PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), sinkPort));
  ApplicationContainer sinkApps = packetSinkHelper.Install (nodes.Get (1));

cout<<"DEBUG: Assigned port to sink(second) node"<<endl;

  sinkApps.Start (Seconds (0.));
  sinkApps.Stop (Seconds (simulation_time));

//assigned the start and end time for receiver node simulation

  Ptr<Socket> ns3TcpSocket = Socket::CreateSocket (nodes.Get (0), TcpSocketFactory::GetTypeId ());
//created socket for first node
cout<<"Calls the createsocket method of TCPL4protocol for node(0)"<<endl;

  Ptr<MyApp> app = CreateObject<MyApp> ();
  app->Setup (ns3TcpSocket, sinkAddress, 1460, 1000000, DataRate ("100Mbps"));
//created an application that will send data 

  nodes.Get (0)->AddApplication (app);
//installed the application on the first node

cout<<"DEBUG: Custom application installed on first (sender) node"<<endl;
  app->SetStartTime (Seconds (1.));
  app->SetStopTime (Seconds (simulation_time));

cout<<"DEBUG: start and stop time assigned for sender"<< endl;
//assigned time for starting and ending data transmission

  
  AsciiTraceHelper asciiTraceHelper;
  Ptr<OutputStreamWrapper> stream = asciiTraceHelper.CreateFileStream ("tcp-example.cwnd");
  ns3TcpSocket->TraceConnectWithoutContext ("CongestionWindow", MakeBoundCallback (&CwndChange, stream));
//this will trace the cwnd throughout the simulation and output it in a file



  //detailed trace of queue enq/deq packet tx/rx
  AsciiTraceHelper ascii;
  pointToPoint.EnableAsciiAll (ascii.CreateFileStream ("tcp-example.tr"));
  pointToPoint.EnablePcapAll ("tcp-example");

  Simulator::Stop (Seconds (simulation_time));
cout<<"DEBUG: Now the simulation will run and Socket for node 1 will be created: DEFAULT"<<endl;
  Simulator::Run ();
cout<<"DEBUG: after"<<endl;
  Simulator::Destroy ();

  return 0;
}

