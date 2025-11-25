#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/aodv-module.h"
#include "ns3/paodv-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("AodvCompare");

int main(int argc, char *argv[])
{
    uint32_t nNodes = 50;       
    double simTime = 200.0;     
    std::string protocol = "PAODV";

    CommandLine cmd;
    cmd.AddValue("nNodes", "Number of nodes", nNodes);
    cmd.AddValue("simTime", "Simulation time", simTime);
    cmd.AddValue("protocol", "Routing protocol: AODV or PAODV", protocol);
    cmd.Parse(argc, argv);

    // ---------------- NODES ----------------
    NodeContainer nodes;
    nodes.Create(nNodes);

    // ---------------- WiFi ----------------
    WifiHelper wifi;
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("OfdmRate6Mbps"));

    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());

    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");

    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);

    // ---------------- Mobility (MATCHING PAPER [cite: 146-147]) ----------------
    MobilityHelper mobility;

    // Map size: 1000m x 1000m
    Ptr<RandomBoxPositionAllocator> positionAlloc =
        CreateObject<RandomBoxPositionAllocator>();
    positionAlloc->SetAttribute("X", StringValue("ns3::UniformRandomVariable[Min=0|Max=1000]"));
    positionAlloc->SetAttribute("Y", StringValue("ns3::UniformRandomVariable[Min=0|Max=1000]"));
    positionAlloc->SetAttribute("Z", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

    mobility.SetPositionAllocator(positionAlloc);

    // Random Waypoint with 10s Pause
    mobility.SetMobilityModel(
        "ns3::RandomWaypointMobilityModel",
        "Speed", StringValue("ns3::UniformRandomVariable[Min=1|Max=20]"), // [cite: 148]
        "Pause", StringValue("ns3::ConstantRandomVariable[Constant=10.0]"), // 
        "PositionAllocator", PointerValue(positionAlloc));

    mobility.Install(nodes);

    // ---------------- Routing ----------------
    InternetStackHelper stack;

    if (protocol == "AODV")
    {
        AodvHelper aodv;
        // Enable Hello for fair comparison
        aodv.Set("EnableHello", BooleanValue(true));
        aodv.Set("HelloInterval", TimeValue(Seconds(1.0)));
        aodv.Set("AllowedHelloLoss", UintegerValue(2));
        stack.SetRoutingHelper(aodv);
    }
    else if (protocol == "PAODV")
    {
        PAodvHelper paodv;
        
        // Enable Hello
        paodv.Set("EnableHello", BooleanValue(true));
        paodv.Set("HelloInterval", TimeValue(Seconds(1.0)));
        paodv.Set("AllowedHelloLoss", UintegerValue(2));

        // Paper Parameters [cite: 151]
        paodv.Set("DistanceThreshold", DoubleValue(100.0)); 
        paodv.Set("RreqBound", UintegerValue(10));

        stack.SetRoutingHelper(paodv);
    }
    else
    {
        NS_FATAL_ERROR("Unknown routing protocol!");
    }

    // --- THIS LINE WAS MISSING AND CAUSED THE CRASH ---
    stack.Install(nodes);

    // ---------------- IP ----------------
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.0.0.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);

    // ---------------- Traffic ----------------
    uint16_t port = 5000;

    // Server on last node
    UdpEchoServerHelper server(port);
    server.Install(nodes.Get(nNodes - 1)).Start(Seconds(1.0));

    // Client on node 0
    UdpEchoClientHelper client(interfaces.GetAddress(nNodes - 1), port);
    client.SetAttribute("MaxPackets", UintegerValue(10000));
    client.SetAttribute("Interval", TimeValue(Seconds(0.2)));
    client.SetAttribute("PacketSize", UintegerValue(200));
    client.Install(nodes.Get(0)).Start(Seconds(2.0));

    // ---------------- Flow Monitor ----------------
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // ---------------- RESULTS ----------------
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());

    Ipv4Address src = interfaces.GetAddress(0);
    Ipv4Address dst = interfaces.GetAddress(nNodes - 1);

    uint64_t tx = 0, rx = 0;
    double delaySum = 0;
    uint32_t totalHops = 0;
    uint32_t totalReceived = 0;

    for (auto &fs : monitor->GetFlowStats())
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(fs.first);

        if (t.sourceAddress == src && t.destinationAddress == dst)
        {
            tx += fs.second.txPackets;
            rx += fs.second.rxPackets;
            delaySum += fs.second.delaySum.GetSeconds();
            totalHops += fs.second.timesForwarded;
            totalReceived += fs.second.rxPackets;
        }
    }
    
    double avgHops = (totalReceived > 0) ? (double)totalHops / totalReceived : 0;

    std::cout << "\n======= " << protocol << " RESULTS =======\n";
    std::cout << "Source: " << src << "\n";
    std::cout << "Destination: " << dst << "\n";
    std::cout << "Sent: " << tx << "\n";
    std::cout << "Received: " << rx << "\n";
    std::cout << "PDR: " << (tx == 0 ? 0 : double(rx) / tx) << "\n";
    std::cout << "Delay: " << (rx == 0 ? 0 : delaySum / rx) << " s\n";

    // ---------------- ROUTING COUNTERS ----------------
    if (protocol == "AODV")
    {
        Ptr<aodv::RoutingProtocol> a =
            nodes.Get(0)->GetObject<aodv::RoutingProtocol>();

        if (a != nullptr)
        {
            std::cout << "RREQ Sent:      " << a->GetRreqSentCount() << "\n";
            std::cout << "RREP Sent:      " << a->GetRrepSentCount() << "\n";
            std::cout << "RERR Sent:      " << a->GetRerrSentCount() << "\n";
            std::cout << "Broken Links:   " << a->GetBrokenLinkCount() << "\n";
            std::cout << "Avg Hops:   " << avgHops << "\n";
        }
    }
    else if (protocol == "PAODV")
    {
        Ptr<paodv::RoutingProtocol> p =
            nodes.Get(0)->GetObject<paodv::RoutingProtocol>();

        if (p != nullptr)
        {
            std::cout << "RREQ Sent:      " << p->GetRreqSentCount() << "\n";
            std::cout << "RREP Sent:      " << p->GetRrepSentCount() << "\n";
            std::cout << "RERR Sent:      " << p->GetRerrSentCount() << "\n";
            std::cout << "Broken Links:   " << p->GetBrokenLinkCount() << "\n";
            std::cout << "Avg Hops:   " << avgHops << "\n";
        }
    }

    std::cout << "=================================\n\n";

    Simulator::Destroy();
    return 0;
}
