#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/aodv-module.h"
#include "ns3/paodv-module.h"
#include "ns3/tpaodv-module.h" 
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/system-wall-clock-ms.h"
#include <chrono>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("OverheadTest");

int main (int argc, char *argv[])
{
  std::string protocol = "AODV"; 
  uint32_t nNodes = 50; 
  bool malicious = false; 
  int nMalicious = 5; 
  double simulationTime = 100.0; 

  CommandLine cmd;
  cmd.AddValue ("protocol", "Protocol to use (AODV, PAODV, TPAODV)", protocol);
  cmd.AddValue ("nNodes", "Number of nodes", nNodes);
  cmd.AddValue ("malicious", "Enable Blackhole Attack", malicious);
  cmd.Parse (argc, argv);

  NodeContainer nodes;
  nodes.Create (nNodes);

  NodeContainer goodNodes;
  NodeContainer badNodes;

  if (malicious) {
      for (uint32_t i = 0; i < nNodes - nMalicious; ++i) {
          goodNodes.Add(nodes.Get(i));
      }
      for (uint32_t i = nNodes - nMalicious; i < nNodes; ++i) {
          badNodes.Add(nodes.Get(i));
      }
  } else {
      goodNodes = nodes;
  }

  WifiHelper wifi;
  wifi.SetStandard (WIFI_STANDARD_80211b);
  
  YansWifiPhyHelper wifiPhy;
  YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default ();
  wifiPhy.SetChannel (wifiChannel.Create ());

  WifiMacHelper wifiMac;
  wifiMac.SetType ("ns3::AdhocWifiMac");
  
  NetDeviceContainer devices = wifi.Install (wifiPhy, wifiMac, nodes);

  MobilityHelper mobility;

  ObjectFactory pos;
  pos.SetTypeId ("ns3::RandomRectanglePositionAllocator");
  pos.Set ("X", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=500.0]"));
  pos.Set ("Y", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=500.0]"));
  
  Ptr<PositionAllocator> taPositionAlloc = pos.Create ()->GetObject<PositionAllocator> ();

  mobility.SetPositionAllocator (taPositionAlloc);
  mobility.SetMobilityModel ("ns3::RandomWaypointMobilityModel",
                             "Speed", StringValue ("ns3::UniformRandomVariable[Min=5.0|Max=20.0]"),
                             "Pause", StringValue ("ns3::ConstantRandomVariable[Constant=2.0]"),
                             "PositionAllocator", PointerValue (taPositionAlloc));

  mobility.Install (nodes);

  InternetStackHelper stack;

  if (protocol == "PAODV")
    {
      PAodvHelper paodvGood;
      paodvGood.Set("RreqBound", UintegerValue(2)); 
      stack.SetRoutingHelper (paodvGood);
      stack.Install (goodNodes);

      if (malicious) {
          PAodvHelper paodvBad;
          paodvBad.Set("RreqBound", UintegerValue(2));
          paodvBad.Set("IsMalicious", BooleanValue(true)); 
          stack.SetRoutingHelper (paodvBad);
          stack.Install (badNodes);
      }
    }
  else if (protocol == "TPAODV")
    {
      TpaodvHelper tpaodvGood;
      tpaodvGood.Set("RreqBound", UintegerValue(2)); 
      stack.SetRoutingHelper (tpaodvGood);
      stack.Install (goodNodes);

      if (malicious) {
          TpaodvHelper tpaodvBad;
          tpaodvBad.Set("RreqBound", UintegerValue(2));
          tpaodvBad.Set("IsMalicious", BooleanValue(true)); 
          stack.SetRoutingHelper (tpaodvBad);
          stack.Install (badNodes);
      }
    }
  else 
    {
      AodvHelper aodvGood;
      stack.SetRoutingHelper (aodvGood);
      stack.Install (goodNodes);

      if (malicious) {
          AodvHelper aodvBad;
          aodvBad.Set("IsMalicious", BooleanValue(true)); 
          stack.SetRoutingHelper (aodvBad);
          stack.Install (badNodes);
      }
    }

  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = address.Assign (devices);

  uint16_t port = 9;
  
  UdpEchoServerHelper echoServer (port);
  ApplicationContainer serverApps = echoServer.Install (nodes);
  serverApps.Start (Seconds (1.0));
  serverApps.Stop (Seconds (simulationTime));

  UdpEchoClientHelper echoClient (Ipv4Address ("0.0.0.0"), port);
  echoClient.SetAttribute ("MaxPackets", UintegerValue (100.0));
  echoClient.SetAttribute ("Interval", TimeValue (Seconds (1.0)));
  echoClient.SetAttribute ("PacketSize", UintegerValue (1024));

  Ptr<UniformRandomVariable> urng = CreateObject<UniformRandomVariable> ();
  
  for (int i = 0; i < 10; i++)
    {
      uint32_t srcNode = urng->GetInteger (0, nNodes - 1);
      uint32_t dstNode = urng->GetInteger (0, nNodes - 1);
      
      while (srcNode == dstNode || (malicious && srcNode >= nNodes - nMalicious)) {
          srcNode = urng->GetInteger (0, nNodes - 1);
      }

      Address remoteAddress = InetSocketAddress (interfaces.GetAddress (dstNode), port);
      echoClient.SetAttribute ("Remote", AddressValue (remoteAddress));

      ApplicationContainer clientApps = echoClient.Install (nodes.Get (srcNode));
      clientApps.Start (Seconds (2.0 + i)); 
      clientApps.Stop (Seconds (simulationTime));
    }

  Simulator::Stop (Seconds (simulationTime));
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

  auto start = std::chrono::high_resolution_clock::now();
  Simulator::Run ();

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = end - start;
  double executionTime = elapsed.count();

  uint32_t totalHops = 0;
  uint32_t totalFlows = 0;
  uint32_t totalTxPackets = 0;
  uint32_t totalRxPackets = 0;

  monitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();

  for (auto const &flow : stats)
    {
      totalTxPackets += flow.second.txPackets;
      totalRxPackets += flow.second.rxPackets;
      if (flow.second.rxPackets > 0) {
          totalHops += flow.second.timesForwarded; 
          totalFlows++;

      }
    }

  double avgHops = (totalFlows > 0) ? (double)totalHops / totalFlows : 0.0;
  double pdr = (totalTxPackets > 0) ? (double)totalRxPackets / totalTxPackets * 100.0 : 0.0;

  uint32_t totalRreq = 0;
  uint32_t totalRrep = 0;
  uint32_t totalRerr = 0;
  uint64_t totalBrokenLinks = 0;
  uint32_t totalRreqRecv = 0;
  uint32_t totalMaliciousDrops = 0; 
 
  for (uint32_t i = 0; i < nNodes; i++)
    {
      Ptr<Ipv4> ipv4 = nodes.Get(i)->GetObject<Ipv4> ();
      Ptr<Ipv4RoutingProtocol> proto = ipv4->GetRoutingProtocol ();
      
      if (protocol == "PAODV") {
           Ptr<ns3::paodv::RoutingProtocol> p = DynamicCast<ns3::paodv::RoutingProtocol> (proto);
           if (p) {
               totalRreq += p->GetRreqSentCount ();
               totalRrep += p->GetRrepSentCount ();
               totalRerr += p->GetRerrSentCount ();
               totalBrokenLinks += p->GetBrokenLinkCount (); 
               totalRreqRecv += p->GetRreqReceivedCount();
               totalMaliciousDrops += p->GetMaliciousDropCount(); 
           }
      }
      else if (protocol == "TPAODV") {
           Ptr<ns3::tpaodv::RoutingProtocol> p = DynamicCast<ns3::tpaodv::RoutingProtocol> (proto);
           if (p) {
               totalRreq += p->GetRreqSentCount ();
               totalRrep += p->GetRrepSentCount ();
               totalRerr += p->GetRerrSentCount ();
               totalBrokenLinks += p->GetBrokenLinkCount (); 
               totalRreqRecv += p->GetRreqReceivedCount();
               totalMaliciousDrops += p->GetMaliciousDropCount();
           }
      }
      else {
           Ptr<ns3::aodv::RoutingProtocol> p = DynamicCast<ns3::aodv::RoutingProtocol> (proto);
           if (p) {
               totalRreq += p->GetRreqSentCount ();
               totalRrep += p->GetRrepSentCount ();
               totalRerr += p->GetRerrSentCount ();
               totalBrokenLinks += p->GetBrokenLinkCount ();
               totalRreqRecv += p->GetRreqReceivedCount();
               totalMaliciousDrops += p->GetMaliciousDropCount();
           }
      }
    }
  
  uint32_t totalRreqActivity = totalRreq + totalRreqRecv;

std::cout << "========= RESULTS (" << protocol << ", Malicious=" << malicious << ") =========" << std::endl;
  std::cout << "Nodes: " << nNodes << std::endl;
  std::cout << "----------------------------------------" << std::endl;
  
  std::cout << "Total RREQ Sent:      " << totalRreq << " packets" << std::endl;
  std::cout << "Total RREQ Received:  " << totalRreqRecv << " packets" << std::endl;
  std::cout << "TOTAL RREQ OVERHEAD:  " << totalRreqActivity << " packets" << std::endl;
  
  std::cout << "Total RREP Sent:      " << totalRrep << " packets" << std::endl;
  std::cout << "Total RERR Sent:      " << totalRerr << " packets" << std::endl;

  std::cout << "PDR:                  " << pdr << " %" << std::endl;
  
  std::cout << "----------------------------------------" << std::endl;
  std::cout << "TOTAL BROKEN LINKS:   " << totalBrokenLinks << " links" << std::endl;
  std::cout << "AVERAGE HOP COUNT:    " << avgHops << " hops" << std::endl;
  std::cout << "----------------------------------------" << std::endl;
  
  std::cout << "PACKET DROPPED        " << totalMaliciousDrops << " packets" << std::endl;
  std::cout << "BY ATTACK" << std::endl;
  std::cout << "----------------------------------------" << std::endl;
  std::cout << "TIME:                 " << executionTime << " s" << std::endl;
  std::cout << "========================================" << std::endl;

  Simulator::Destroy ();
  return 0;
}
