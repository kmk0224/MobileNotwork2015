#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/bridge-helper.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/netanim-module.h"

#include "ns3/ap-wifi-mac.h"

using namespace ns3;

static void changePosition(Ptr<Node> node)
{
	Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
	Vector coor = mobility->GetPosition();
	coor.x += 2.0;
	mobility->SetPosition(coor);
	
	Simulator::Schedule(Seconds(1.0), &changePosition, node);
}

void setStaticRoute(Ptr<Node> node, const char* dest, 
					const char* next, uint32_t interface)
{
	Ipv4StaticRoutingHelper staticRouting;
	Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
	Ptr<Ipv4StaticRouting> a = staticRouting.GetStaticRouting(ipv4);
	a->AddHostRouteTo(Ipv4Address(dest), Ipv4Address(next), interface);
}

int main (int argc, char *argv[])
{
	double simTime = 70.0;		// 50.0
	uint32_t port = 6000;

    /* =================  Node creation ======================== */
    // all wired nodes(0:server 1:AP0 2:AP1)
    NodeContainer wiredNodes; 
    NodeContainer server; // mobile station will communicate with it
    server.Create(1);
    wiredNodes.Add(server); 

    // all APs (0:AP0 1:AP1)
    NodeContainer apNodes;
    apNodes.Create(3);
    wiredNodes.Add(apNodes);

    // all STAs (0:STA0)
    NodeContainer staNodes;
    staNodes.Create(1);

    // all wifi nodes (0:AP0 1:AP1 2:STA0)
    NodeContainer wifiNodes;
    wifiNodes.Add(apNodes);
    wifiNodes.Add(staNodes);

    /* ===============  Wired network configuration  ================== */
    // server and two APs are in the same network
    CsmaHelper csma;
    NetDeviceContainer csmaDev = csma.Install(wiredNodes); 

    /* ===============  Wireless network configuration ================ */
    // physical layer
    YansWifiPhyHelper phy = YansWifiPhyHelper::Default();
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    phy.SetChannel(channel.Create());

    // mac layer
    WifiHelper wifi = WifiHelper::Default();
    wifi.SetRemoteStationManager("ns3::ArfWifiManager");

    NqosWifiMacHelper mac = NqosWifiMacHelper::Default();

    // AP
    Ssid ssid = Ssid("test");
    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));

    NetDeviceContainer apDev = wifi.Install(phy, mac, apNodes);

    // mobilet station
    mac.SetType("ns3::StaWifiMac", 
    		  "Ssid", SsidValue(ssid),
	    	  "ActiveProbing", BooleanValue(true));

    NetDeviceContainer staDev = wifi.Install(phy, mac, staNodes);

    /* ===============  Bridge wifi and csma in Aps  ================ */
	// APs have two network devices, one for csma/cd and one for wifi
	BridgeHelper bridge;
	NetDeviceContainer bridgeDev;

	// bridge the wired and wireless network devices
	// bridgeDev0 on AP0 - apDev0 (AP0 wifi) <-> csmaDev1 (AP0 csma)
	bridgeDev = bridge.Install(wiredNodes.Get(1), 
				NetDeviceContainer(apDev.Get(0), csmaDev.Get(1)));
				
	// bridgeDev1 on AP1 - apDev1 (AP1 wifi) <-> csmaDev2 (AP1 csma)
	bridgeDev.Add(bridge.Install(wiredNodes.Get(2), 
				NetDeviceContainer(apDev.Get(1), csmaDev.Get(2))));
				
	// bridgeDev1 on AP2 - apDev2 (AP2 wifi) <-> csmaDev3 (AP2 csma)
	bridgeDev.Add(bridge.Install(wiredNodes.Get(3), 
				NetDeviceContainer(apDev.Get(2), csmaDev.Get(3))));

    /* ===============  Network layer configuration ================ */
    InternetStackHelper internet;
    internet.Install(wiredNodes);
    internet.Install(staNodes);

    // IP address set
    // all nodes are in the same network
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    // server's device has 10.1.1.1
    Ipv4InterfaceContainer serverIf = address.Assign(csmaDev.Get(0));
	// AP0 has 10.1.1.2 and AP1 has 10.1.1.3 and AP2 has 10.1.1.4
	Ipv4InterfaceContainer apIf = address.Assign(bridgeDev);
	// mobile staion has 10.1.1.5
	Ipv4InterfaceContainer staIf = address.Assign(staDev);

    /* =================  Position and mobility  ================== */
    MobilityHelper mobility;
    // Initial positions of all nodes
    Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();
    pos->Add(Vector(0, 0, 0));  // server's position
    pos->Add(Vector(50, 20, 0)); // AP0's position
    pos->Add(Vector(100, 50, 0)); // AP1's position	
    pos->Add(Vector(200, 20, 0)); // AP2's position
    pos->Add(Vector(70, 35, 0)); // STA0's position

    mobility.SetPositionAllocator(pos);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(server);
    mobility.Install(apNodes);
    mobility.Install(staNodes);

    // mobile station moves from AP1 to AP0 in the speed of 2m/s
    Simulator::Schedule(Seconds(1.0), &changePosition, staNodes.Get(0));

    /* =================  Traffic generation ======================== */
    // mobile staion sends data to the server
    OnOffHelper onOff("ns3::UdpSocketFactory", 
				Address(InetSocketAddress(serverIf.GetAddress(0), port)));
    onOff.SetAttribute("DataRate", StringValue("100Mbps"));
  
	// install the application on the mobile station
    ApplicationContainer apps = onOff.Install(staNodes.Get (0));

	// server consumes the packets after packet receiption
    PacketSinkHelper sink("ns3::UdpSocketFactory", 
				InetSocketAddress(Ipv4Address::GetAny(), port));
    apps.Add(sink.Install(server.Get(0)));

    apps.Start (Seconds (1.0));
    apps.Stop (Seconds (simTime)); 
  
    AnimationInterface anim("project.xml");
  
    csma.EnablePcap("project-server", csmaDev.Get(0), true);
    phy.EnablePcap("project-sta", staDev, true);

    Simulator::Stop(Seconds(simTime));
    Simulator::Run ();
    Simulator::Destroy ();
    
	return 0;
}

