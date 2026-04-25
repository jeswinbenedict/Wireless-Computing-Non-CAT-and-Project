#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/applications-module.h"
#include "ns3/netanim-module.h"
#include "ns3/olsr-helper.h"

using namespace ns3;

int main(int argc, char *argv[]) {
    uint32_t nNodes = 50;
    CommandLine cmd;
    cmd.AddValue("nNodes", "Number of nodes", nNodes);
    cmd.Parse(argc, argv);

    NodeContainer nodes;
    nodes.Create(nNodes);

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
        "DataMode",    StringValue("DsssRate11Mbps"),
        "ControlMode", StringValue("DsssRate11Mbps"));

    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");

    YansWifiPhyHelper phy;
    YansWifiChannelHelper ch = YansWifiChannelHelper::Default();
    phy.SetChannel(ch.Create());

    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);

    MobilityHelper mobility;
    mobility.SetPositionAllocator("ns3::GridPositionAllocator",
        "MinX", DoubleValue(0.0), "MinY", DoubleValue(0.0),
        "DeltaX", DoubleValue(20.0), "DeltaY", DoubleValue(20.0),
        "GridWidth", UintegerValue(10),
        "LayoutType", StringValue("RowFirst"));
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    OlsrHelper olsr;
    InternetStackHelper internet;
    internet.SetRoutingHelper(olsr);
    internet.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    UdpEchoServerHelper echoServer(9);
    ApplicationContainer serverApp = echoServer.Install(nodes.Get(0));
    serverApp.Start(Seconds(1.0));
    serverApp.Stop(Seconds(40.0));

    for (uint32_t i = 1; i < nNodes; i++) {
        UdpEchoClientHelper echoClient(interfaces.GetAddress(0), 9);
        echoClient.SetAttribute("MaxPackets", UintegerValue(200));
        echoClient.SetAttribute("Interval",   TimeValue(Seconds(0.3)));
        echoClient.SetAttribute("PacketSize",  UintegerValue(512));
        ApplicationContainer clientApp = echoClient.Install(nodes.Get(i));
        clientApp.Start(Seconds(10.0 + i * 0.2));
        clientApp.Stop(Seconds(40.0));
    }

    AnimationInterface anim("/home/jeswin/ns-allinone-3.43/ns-3.43/netanim_demo.xml");
    anim.SetMaxPktsPerTraceFile(500000000);
    anim.EnablePacketMetadata(true);

    anim.UpdateNodeColor(nodes.Get(0), 255, 0, 0);
    anim.UpdateNodeSize(nodes.Get(0)->GetId(), 4, 4);
    anim.UpdateNodeDescription(nodes.Get(0), "SINK");

    for (uint32_t i = 1; i < nNodes; i++) {
        if (i % 3 == 0)      anim.UpdateNodeColor(nodes.Get(i), 30,  144, 255);
        else if (i % 3 == 1) anim.UpdateNodeColor(nodes.Get(i), 50,  205,  50);
        else                  anim.UpdateNodeColor(nodes.Get(i), 255, 165,   0);
        anim.UpdateNodeSize(nodes.Get(i)->GetId(), 2, 2);
        anim.UpdateNodeDescription(nodes.Get(i), "SN" + std::to_string(i));
    }

    Simulator::Stop(Seconds(40.0));
    Simulator::Run();
    Simulator::Destroy();
    return 0;
}
