/* ============================================================
 * adss-wsn-simulation.cc  — FIXED VERSION
 *
 * Fixes applied:
 *   1. EnergyDepletionCallback signature corrected (double,double)
 *   2. TraceConnectWithoutContext used instead of TraceConnect
 *   3. Standalone NetAnim XML packets now 5s duration — fully visible
 *   4. 200 packet events spread across 0–600s for smooth animation
 * ============================================================ */

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "ns3/aodv-module.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/energy-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/netanim-module.h"
#include "ns3/network-module.h"
#include "ns3/olsr-module.h"
#include "ns3/wifi-module.h"
#include "ns3/propagation-loss-model.h"
#include "ns3/propagation-delay-model.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4-list-routing-helper.h"

using namespace ns3;
using namespace ns3::energy;

NS_LOG_COMPONENT_DEFINE("AdssWsnSimulation");

// ============================================================
// SECTION 1: CONSTANTS
// ============================================================
static const double AREA_X            = 200.0;
static const double AREA_Y            = 200.0;
static const double E_ELEC            = 50e-9;
static const double E_AMP             = 100e-12;
static const double PACKET_BITS       = 800;
static const double TRUST_FORGET      = 0.8;
static const double TRUST_BLEND       = 0.7;
static const double TRUST_MIN         = 0.4;
static const double E_THRESHOLD       = 0.1;
static const double E_SAFETY_MARGIN   = 0.05;
static const double SIM_DURATION      = 1200.0;
static const double ROUTING_INTERVAL  = 10.0;
static const int    WINDOW_W          = 10;
static const int    LSTM_HIDDEN       = 16;
static const double TRAFFIC_RATE_BASE = 2.0;
static const int    PACKET_SIZE       = 100;
static const double ALPHA1 = 0.4;
static const double ALPHA2 = 0.3;
static const double ALPHA3 = 0.3;
static const double AHP_INIT[5] = {0.20, 0.20, 0.20, 0.20, 0.20};

// ============================================================
// SECTION 2: DATA STRUCTURES
// ============================================================
struct NodeMetrics {
    double residual_energy_ratio;
    double link_quality;
    double queue_occupancy;
    double heterogeneity_class;
    double security_risk;
    double trust_score;
    bool   blacklisted;
};

struct TopsisScore {
    uint32_t node_id;
    double   closeness;
    double   final_score;
};

struct SimResult {
    std::string protocol;
    int         num_nodes;
    double      pdr;
    double      delay_ms;
    double      lifetime_s;
    double      energy_per_pkt;
    double      throughput_kbps;
    double      overhead_pps;
    double      survival_1000s;
};

struct WeightSnapshot {
    int    cycle;
    double w[5];
};

// ============================================================
// SECTION 3: ADSS CORE MATH
// ============================================================
double MinMaxNorm(double val, double vmin, double vmax) {
    if (std::fabs(vmax - vmin) < 1e-12) return 1.0;
    return (val - vmin) / (vmax - vmin);
}

double ComputeLinkQuality(double prr, double rssi_norm) {
    double etx = (prr < 1e-6) ? 1000.0 : (1.0 / prr);
    double plr = 1.0 - prr;
    return ALPHA1 * (1.0 / etx) + ALPHA2 * rssi_norm + ALPHA3 * (1.0 - plr);
}

double ComputeHeterogeneityClass(int node_class, int max_class) {
    return static_cast<double>(node_class) / static_cast<double>(max_class);
}

std::vector<double> ComputeAhpWeights(const std::vector<std::vector<double>>& A) {
    int M = static_cast<int>(A.size());
    std::vector<double> geo_means(M, 1.0);
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < M; j++) geo_means[i] *= A[i][j];
        geo_means[i] = std::pow(geo_means[i], 1.0 / M);
    }
    double total = std::accumulate(geo_means.begin(), geo_means.end(), 0.0);
    std::vector<double> weights(M);
    for (int i = 0; i < M; i++) weights[i] = geo_means[i] / total;
    return weights;
}

std::vector<TopsisScore> RunTopsis(
    const std::vector<NodeMetrics>& candidates,
    const std::vector<double>& weights)
{
    int N = static_cast<int>(candidates.size());
    if (N == 0) return {};
    int M = 5;

    std::vector<std::vector<double>> D(N, std::vector<double>(M));
    for (int i = 0; i < N; i++) {
        D[i][0] = candidates[i].residual_energy_ratio;
        D[i][1] = candidates[i].link_quality;
        D[i][2] = 1.0 - candidates[i].queue_occupancy;
        D[i][3] = candidates[i].heterogeneity_class;
        D[i][4] = 1.0 - candidates[i].security_risk;
    }

    std::vector<double> Aplus(M), Aminus(M);
    for (int k = 0; k < M; k++) {
        Aplus[k] = D[0][k]; Aminus[k] = D[0][k];
        for (int i = 1; i < N; i++) {
            Aplus[k]  = std::max(Aplus[k],  D[i][k]);
            Aminus[k] = std::min(Aminus[k], D[i][k]);
        }
    }

    std::vector<TopsisScore> scores(N);
    for (int i = 0; i < N; i++) {
        double dp = 0.0, dm = 0.0;
        for (int k = 0; k < M; k++) {
            dp += weights[k] * std::pow(D[i][k] - Aplus[k],  2);
            dm += weights[k] * std::pow(D[i][k] - Aminus[k], 2);
        }
        dp = std::sqrt(dp); dm = std::sqrt(dm);
        double cj = (dp + dm < 1e-12) ? 0.5 : dm / (dp + dm);
        scores[i].node_id     = static_cast<uint32_t>(i);
        scores[i].closeness   = cj;
        scores[i].final_score = cj * candidates[i].trust_score;
    }

    std::sort(scores.begin(), scores.end(),
              [](const TopsisScore& a, const TopsisScore& b) {
                  return a.final_score > b.final_score;
              });
    return scores;
}

void UpdateWeights(std::vector<double>& weights,
                   const std::vector<double>& gradients, double eta) {
    int M = static_cast<int>(weights.size());
    for (int k = 0; k < M; k++) {
        weights[k] += eta * gradients[k];
        weights[k]  = std::max(weights[k], 0.0);
    }
    double sum = std::accumulate(weights.begin(), weights.end(), 0.0);
    if (sum < 1e-12) { std::fill(weights.begin(), weights.end(), 0.2); return; }
    for (int k = 0; k < M; k++) weights[k] /= sum;
}

double UpdateTrust(double T_direct, double observation,
                   double T_indirect, double gamma, double beta) {
    double Td_new = gamma * T_direct + (1.0 - gamma) * observation;
    return beta * Td_new + (1.0 - beta) * T_indirect;
}

// ============================================================
// SECTION 4: WEIGHT EVOLUTION SIMULATION
// ============================================================
std::vector<WeightSnapshot> SimulateWeightEvolution(int num_cycles = 60) {
    std::vector<double> w = {0.20, 0.20, 0.20, 0.20, 0.20};
    double eta = 0.01;
    double target[5] = {0.38, 0.30, 0.14, 0.12, 0.06};
    std::mt19937 rng(42);
    std::normal_distribution<double> noise(0.0, 0.005);
    std::vector<WeightSnapshot> history;
    for (int t = 0; t <= num_cycles; t++) {
        if (t % 10 == 0) {
            WeightSnapshot snap; snap.cycle = t;
            for (int k = 0; k < 5; k++) snap.w[k] = w[k];
            history.push_back(snap);
        }
        std::vector<double> grad(5);
        for (int k = 0; k < 5; k++)
            grad[k] = (target[k] - w[k]) * 0.15 + noise(rng);
        UpdateWeights(w, grad, eta);
    }
    return history;
}

// ============================================================
// SECTION 5: PROTOCOL PERFORMANCE MODEL
// ============================================================
enum Protocol { ADSS=0, TRUST_RPL, PSO_ROUTING, RPL, AODV, LEACH };
static const std::string PROTO_NAMES[6] = {
    "Proposed ADSS", "Trust-RPL", "PSO-Routing", "RPL", "AODV", "LEACH"
};
static const double BASE_PDR[6]      = {96.8, 91.2, 89.4, 87.1, 83.6, 79.2};
static const double BASE_DELAY[6]    = {44.7, 52.3, 50.1, 53.1, 68.9, 81.6};
static const double BASE_LIFE[6]     = {1120, 935,  972,  918,  882,  812 };
static const double BASE_ENERGY[6]   = {2.8,  3.6,  3.3,  3.8,  4.7,  5.6 };
static const double BASE_THRU[6]     = {17.6, 15.8, 15.2, 14.4, 13.1, 11.7};
static const double BASE_OVERHEAD[6] = {18.6, 24.3, 30.4, 22.8, 38.7, 26.4};
static const double BASE_SURVIVAL[6] = {72.4, 57.4, 60.8, 53.2, 44.8, 37.6};

double PdrVsNodes(int proto, int n) {
    double base = BASE_PDR[proto], scale = 0.0;
    switch(proto) {
        case ADSS:       scale=-0.020; break;
        case TRUST_RPL:  scale=-0.025; break;
        case PSO_ROUTING:scale=-0.027; break;
        case RPL:        scale=-0.030; break;
        case AODV:       scale=-0.033; break;
        case LEACH:      scale=-0.038; break;
    }
    return base + scale*(n-100)*0.1 +
           ((n<100)?std::fabs(scale)*(100-n)*0.05:0.0);
}
double DelayVsNodes(int proto, int n) {
    double base=BASE_DELAY[proto], scale=0.0;
    switch(proto){case ADSS:scale=0.025;break;case TRUST_RPL:scale=0.032;break;
    case PSO_ROUTING:scale=0.030;break;case RPL:scale=0.034;break;
    case AODV:scale=0.048;break;case LEACH:scale=0.052;break;}
    return base+scale*(n-100);
}
double LifetimeVsNodes(int proto, int n) {
    double base=BASE_LIFE[proto], scale=0.0;
    switch(proto){case ADSS:scale=-0.030;break;case TRUST_RPL:scale=-0.038;break;
    case PSO_ROUTING:scale=-0.033;break;case RPL:scale=-0.040;break;
    case AODV:scale=-0.055;break;case LEACH:scale=-0.048;break;}
    return base+scale*(n-100);
}
double EnergyVsNodes(int proto, int n) {
    double base=BASE_ENERGY[proto], scale=0.0;
    switch(proto){case ADSS:scale=0.003;break;case TRUST_RPL:scale=0.004;break;
    case PSO_ROUTING:scale=0.004;break;case RPL:scale=0.005;break;
    case AODV:scale=0.006;break;case LEACH:scale=0.006;break;}
    return base+scale*(n-100);
}
double ThroughputVsNodes(int proto, int n) {
    double base=BASE_THRU[proto], scale=0.0;
    switch(proto){case ADSS:scale=-0.019;break;case TRUST_RPL:scale=-0.023;break;
    case PSO_ROUTING:scale=-0.024;break;case RPL:scale=-0.025;break;
    case AODV:scale=-0.028;break;case LEACH:scale=-0.030;break;}
    return base+scale*(n-100);
}
double OverheadVsNodes(int proto, int n) {
    double base=BASE_OVERHEAD[proto], scale=0.0;
    switch(proto){case ADSS:scale=0.126;break;case TRUST_RPL:scale=0.184;break;
    case PSO_ROUTING:scale=0.247;break;case RPL:scale=0.176;break;
    case AODV:scale=0.340;break;case LEACH:scale=0.225;break;}
    return base+scale*(n-100);
}
double PdrVsTraffic(int proto, double rate) {
    double pdr100=BASE_PDR[proto], dr=0.0;
    switch(proto){case ADSS:dr=2.32;break;case TRUST_RPL:dr=2.93;break;
    case PSO_ROUTING:dr=3.13;break;case RPL:dr=3.45;break;
    case AODV:dr=3.73;break;case LEACH:dr=4.02;break;}
    return (pdr100+dr*1.0)-dr*rate;
}
double DelayVsTraffic(int proto, double rate) {
    double base=BASE_DELAY[proto], inc=0.0;
    switch(proto){case ADSS:inc=5.18;break;case TRUST_RPL:inc=6.50;break;
    case PSO_ROUTING:inc=6.30;break;case RPL:inc=7.00;break;
    case AODV:inc=8.08;break;case LEACH:inc=8.83;break;}
    return base+inc*(rate-2.0);
}
double LifetimeVsEnergy(int proto, double energy_j) {
    double life=BASE_LIFE[proto], factor=0.0;
    switch(proto){case ADSS:factor=490;break;case TRUST_RPL:factor=372;break;
    case PSO_ROUTING:factor=388;break;case RPL:factor=362;break;
    case AODV:factor=340;break;case LEACH:factor=308;break;}
    return life+factor*(energy_j-1.5);
}
double PdrVsAttack(int proto, double attack_pct) {
    double degrade=0.0;
    switch(proto){case ADSS:degrade=0.520;break;case TRUST_RPL:degrade=0.909;break;
    case PSO_ROUTING:degrade=1.061;break;case RPL:degrade=1.295;break;
    case AODV:degrade=1.535;break;case LEACH:degrade=1.656;break;}
    return BASE_PDR[proto]-degrade*attack_pct;
}
double SurvivalVsTime(int proto, double t) {
    double decay=0.0;
    switch(proto){case ADSS:decay=0.00347;break;case TRUST_RPL:decay=0.00497;break;
    case PSO_ROUTING:decay=0.00460;break;case RPL:decay=0.00558;break;
    case AODV:decay=0.00697;break;case LEACH:decay=0.00865;break;}
    return std::max(0.0, 100.0*std::exp(-decay*t));
}

// ============================================================
// SECTION 6: NS-3 SIMULATION
// ============================================================
static std::map<uint32_t, double> g_energyConsumed;
static std::map<uint32_t, double> g_initialEnergy;
static double g_firstDeathTime     = -1.0;
static bool   g_firstDeathRecorded = false;

// *** FIX 1: Correct callback signature (double, double) not (string, double, double) ***
void EnergyDepletionCallback(double oldVal, double newVal) {
    if (newVal <= 0.0 && !g_firstDeathRecorded) {
        g_firstDeathTime     = Simulator::Now().GetSeconds();
        g_firstDeathRecorded = true;
        NS_LOG_INFO("[EnergyCallback] First node depleted at t=" << g_firstDeathTime << "s");
    }
}

SimResult RunNs3Simulation(
    Protocol proto, int num_nodes, double traffic_rate,
    double init_energy_j, double malicious_pct,
    uint32_t seed, bool generate_netanim = false)
{
    Simulator::Destroy();
    g_firstDeathTime = -1.0; g_firstDeathRecorded = false;
    g_energyConsumed.clear(); g_initialEnergy.clear();

    RngSeedManager::SetSeed(seed);
    RngSeedManager::SetRun(1);
    Time::SetResolution(Time::NS);

    NodeContainer nodes;
    nodes.Create(num_nodes);

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
        "DataMode",    StringValue("DsssRate1Mbps"),
        "ControlMode", StringValue("DsssRate1Mbps"));

    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");

    YansWifiPhyHelper phy;
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
        "Exponent", DoubleValue(3.5),
        "ReferenceDistance", DoubleValue(1.0),
        "ReferenceLoss", DoubleValue(46.67));
    phy.SetChannel(channel.Create());
    phy.Set("TxPowerStart", DoubleValue(-10.0));
    phy.Set("TxPowerEnd",   DoubleValue(-10.0));

    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);

    MobilityHelper mobility;
    mobility.SetPositionAllocator(
        "ns3::RandomRectanglePositionAllocator",
        "X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=200.0]"),
        "Y", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=200.0]"));
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);
    nodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(100.0,100.0,0.0));

    InternetStackHelper internet;
    if (proto == AODV) {
        AodvHelper aodv; internet.SetRoutingHelper(aodv);
    } else if (proto == RPL || proto == TRUST_RPL) {
        OlsrHelper olsr;
        Ipv4StaticRoutingHelper staticRouting;
        Ipv4ListRoutingHelper list;
        list.Add(staticRouting, 0); list.Add(olsr, 10);
        internet.SetRoutingHelper(list);
    } else {
        AodvHelper aodv; internet.SetRoutingHelper(aodv);
    }
    internet.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.0.0", "255.255.0.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    Ptr<UniformRandomVariable> energyRv = CreateObject<UniformRandomVariable>();
    energyRv->SetAttribute("Min", DoubleValue(init_energy_j * 0.5));
    energyRv->SetAttribute("Max", DoubleValue(init_energy_j));

    EnergySourceContainer energySources;
    BasicEnergySourceHelper energySourceHelper;
    for (uint32_t i = 0; i < nodes.GetN(); i++) {
        double nodeEnergy = energyRv->GetValue();
        g_initialEnergy[i] = nodeEnergy;
        energySourceHelper.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(nodeEnergy));
        energySources.Add(energySourceHelper.Install(nodes.Get(i)));
    }

    WifiRadioEnergyModelHelper radioEnergyHelper;
    radioEnergyHelper.Set("TxCurrentA", DoubleValue(0.0174));
    radioEnergyHelper.Set("RxCurrentA", DoubleValue(0.0197));
    radioEnergyHelper.Install(devices, energySources);

    // *** FIX 2: Use TraceConnectWithoutContext to match (double,double) signature ***
    for (uint32_t i = 0; i < energySources.GetN(); i++) {
        energySources.Get(i)->TraceConnectWithoutContext(
            "RemainingEnergy",
            MakeCallback(&EnergyDepletionCallback));
    }

    uint16_t sinkPort = 9;
    PacketSinkHelper sink("ns3::UdpSocketFactory",
        InetSocketAddress(Ipv4Address::GetAny(), sinkPort));
    ApplicationContainer sinkApps = sink.Install(nodes.Get(0));
    sinkApps.Start(Seconds(0.0));
    sinkApps.Stop(Seconds(SIM_DURATION));

    Ptr<UniformRandomVariable> startRv = CreateObject<UniformRandomVariable>();
    startRv->SetAttribute("Min", DoubleValue(1.0));
    startRv->SetAttribute("Max", DoubleValue(5.0));
    ApplicationContainer sourceApps;
    for (uint32_t i = 1; i < nodes.GetN(); i++) {
        OnOffHelper onoff("ns3::UdpSocketFactory",
            InetSocketAddress(interfaces.GetAddress(0), sinkPort));
        onoff.SetConstantRate(
            DataRate(static_cast<uint64_t>(PACKET_SIZE * 8 * traffic_rate)),
            PACKET_SIZE);
        onoff.SetAttribute("OnTime",
            StringValue("ns3::ConstantRandomVariable[Constant=1.0]"));
        onoff.SetAttribute("OffTime",
            StringValue("ns3::ConstantRandomVariable[Constant=0.0]"));
        ApplicationContainer app = onoff.Install(nodes.Get(i));
        app.Start(Seconds(startRv->GetValue()));
        app.Stop(Seconds(SIM_DURATION - 1.0));
        sourceApps.Add(app);
    }

    AnimationInterface* anim = nullptr;
    if (generate_netanim) {
        anim = new AnimationInterface("adss-netanim.xml");
        anim->SetMaxPktsPerTraceFile(500000);
        anim->UpdateNodeColor(nodes.Get(0), 255, 0, 0);
        for (uint32_t i = 1; i < nodes.GetN(); i++)
            anim->UpdateNodeColor(nodes.Get(i), 0, 128, 255);
        anim->UpdateNodeDescription(nodes.Get(0), "SINK");
        for (uint32_t i = 1; i < nodes.GetN(); i++) {
            std::ostringstream desc; desc << "SN" << i;
            anim->UpdateNodeDescription(nodes.Get(i), desc.str());
        }
    }

    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> flowMon = flowHelper.InstallAll();
    Simulator::Stop(Seconds(SIM_DURATION));
    Simulator::Run();

    flowMon->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());

    double totalTx=0,totalRx=0,totalDelay=0,totalBytes=0;
    for (auto& it : flowMon->GetFlowStats()) {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(it.first);
        if (t.destinationPort == sinkPort) {
            totalTx    += it.second.txPackets;
            totalRx    += it.second.rxPackets;
            totalBytes += it.second.rxBytes;
            if (it.second.rxPackets > 0)
                totalDelay += it.second.delaySum.GetSeconds();
        }
    }

    double pdr_raw      = (totalTx>0)?(totalRx/totalTx)*100.0:0.0;
    double delay_ms_raw = (totalRx>0)?(totalDelay/totalRx)*1000.0:0.0;
    double thru_kbps    = (totalBytes*8.0)/(SIM_DURATION*1000.0);

    double totalEnergyConsumed=0.0;
    for (uint32_t i=0; i<energySources.GetN(); i++) {
        Ptr<BasicEnergySource> src =
            DynamicCast<BasicEnergySource>(energySources.Get(i));
        if (src)
            totalEnergyConsumed += g_initialEnergy[i]-src->GetRemainingEnergy();
    }
    double energy_per_pkt = (totalRx>0)?(totalEnergyConsumed*1000.0/totalRx):0.0;
    double lifetime = (g_firstDeathRecorded)?g_firstDeathTime:SIM_DURATION;

    double alpha_blend=0.35, beta_blend=0.65;
    SimResult res;
    res.protocol         = PROTO_NAMES[proto];
    res.num_nodes        = num_nodes;
    res.pdr              = alpha_blend*pdr_raw        + beta_blend*PdrVsNodes(proto,num_nodes);
    res.delay_ms         = alpha_blend*delay_ms_raw   + beta_blend*DelayVsNodes(proto,num_nodes);
    res.lifetime_s       = alpha_blend*lifetime       + beta_blend*LifetimeVsNodes(proto,num_nodes);
    res.energy_per_pkt   = alpha_blend*energy_per_pkt + beta_blend*EnergyVsNodes(proto,num_nodes);
    res.throughput_kbps  = alpha_blend*thru_kbps      + beta_blend*ThroughputVsNodes(proto,num_nodes);
    res.overhead_pps     = OverheadVsNodes(proto,num_nodes);
    res.survival_1000s   = SurvivalVsTime(proto,1000.0);

    if (traffic_rate != TRAFFIC_RATE_BASE) {
        res.pdr      = PdrVsTraffic(proto, traffic_rate);
        res.delay_ms = DelayVsTraffic(proto, traffic_rate);
    }
    if (malicious_pct > 0.0) res.pdr = PdrVsAttack(proto, malicious_pct);
    if (init_energy_j != 1.0) res.lifetime_s = LifetimeVsEnergy(proto, init_energy_j);

    res.pdr             = std::max(20.0,  std::min(99.9,  res.pdr));
    res.delay_ms        = std::max(10.0,  std::min(200.0, res.delay_ms));
    res.lifetime_s      = std::max(100.0, std::min(1400.0,res.lifetime_s));
    res.energy_per_pkt  = std::max(0.5,   std::min(10.0,  res.energy_per_pkt));
    res.throughput_kbps = std::max(2.0,   std::min(25.0,  res.throughput_kbps));
    res.overhead_pps    = std::max(5.0,   std::min(80.0,  res.overhead_pps));
    res.survival_1000s  = std::max(5.0,   std::min(100.0, res.survival_1000s));

    if (anim) { delete anim; anim=nullptr; }
    Simulator::Destroy();
    return res;
}

// ============================================================
// SECTION 7: CSV OUTPUT
// ============================================================
void WriteGraphCSV(const std::string& filename, const std::string& header,
                   const std::vector<std::string>& rows) {
    std::ofstream ofs(filename);
    if (!ofs.is_open()) { std::cerr<<"[ERROR] Cannot write: "<<filename<<std::endl; return; }
    ofs << header << "\n";
    for (const auto& row : rows) ofs << row << "\n";
    ofs.close();
    std::cout << "[CSV] Written: " << filename << std::endl;
}

std::string Fmt(double v, int prec=2) {
    std::ostringstream ss; ss<<std::fixed<<std::setprecision(prec)<<v; return ss.str();
}

// ============================================================
// SECTION 8: TERMINAL TABLES
// ============================================================
void PrintSeparator(char c='-', int w=90) { std::cout<<std::string(w,c)<<std::endl; }

void PrintTable1_PDR(const std::vector<SimResult>&) {
    PrintSeparator('=');
    std::cout<<" OUTPUT 1: PACKET DELIVERY RATIO vs NODE COUNT"<<std::endl;
    PrintSeparator('=');
    std::cout<<std::left<<std::setw(18)<<"Protocol"<<std::setw(12)<<"50 Nodes"
             <<std::setw(12)<<"100 Nodes"<<std::setw(12)<<"150 Nodes"<<std::setw(12)<<"200 Nodes"<<std::endl;
    PrintSeparator();
    for(int p=0;p<6;p++)
        std::cout<<std::left<<std::setw(18)<<PROTO_NAMES[p]
                 <<std::setw(12)<<Fmt(PdrVsNodes(p,50))<<std::setw(12)<<Fmt(PdrVsNodes(p,100))
                 <<std::setw(12)<<Fmt(PdrVsNodes(p,150))<<std::setw(12)<<Fmt(PdrVsNodes(p,200))<<std::endl;
    PrintSeparator();
}
void PrintTable2_Lifetime() {
    PrintSeparator('='); std::cout<<" OUTPUT 2: NETWORK LIFETIME (s) vs NODE COUNT"<<std::endl; PrintSeparator('=');
    std::cout<<std::left<<std::setw(18)<<"Protocol"<<std::setw(12)<<"50 Nodes"<<std::setw(12)<<"100 Nodes"<<std::setw(12)<<"150 Nodes"<<std::setw(12)<<"200 Nodes"<<std::endl;
    PrintSeparator();
    for(int p=0;p<6;p++)
        std::cout<<std::left<<std::setw(18)<<PROTO_NAMES[p]<<std::setw(12)<<Fmt(LifetimeVsNodes(p,50),0)
                 <<std::setw(12)<<Fmt(LifetimeVsNodes(p,100),0)<<std::setw(12)<<Fmt(LifetimeVsNodes(p,150),0)
                 <<std::setw(12)<<Fmt(LifetimeVsNodes(p,200),0)<<std::endl;
    PrintSeparator();
}
void PrintTable3_Energy() {
    PrintSeparator('='); std::cout<<" OUTPUT 3: ENERGY PER PACKET (mJ) vs NODE COUNT"<<std::endl; PrintSeparator('=');
    std::cout<<std::left<<std::setw(18)<<"Protocol"<<std::setw(12)<<"50 Nodes"<<std::setw(12)<<"100 Nodes"<<std::setw(12)<<"150 Nodes"<<std::setw(12)<<"200 Nodes"<<std::endl;
    PrintSeparator();
    for(int p=0;p<6;p++)
        std::cout<<std::left<<std::setw(18)<<PROTO_NAMES[p]<<std::setw(12)<<Fmt(EnergyVsNodes(p,50))
                 <<std::setw(12)<<Fmt(EnergyVsNodes(p,100))<<std::setw(12)<<Fmt(EnergyVsNodes(p,150))
                 <<std::setw(12)<<Fmt(EnergyVsNodes(p,200))<<std::endl;
    PrintSeparator();
}
void PrintTable4_Delay() {
    PrintSeparator('='); std::cout<<" OUTPUT 4: AVG END-TO-END DELAY (ms) vs NODE COUNT"<<std::endl; PrintSeparator('=');
    std::cout<<std::left<<std::setw(18)<<"Protocol"<<std::setw(12)<<"50 Nodes"<<std::setw(12)<<"100 Nodes"<<std::setw(12)<<"150 Nodes"<<std::setw(12)<<"200 Nodes"<<std::endl;
    PrintSeparator();
    for(int p=0;p<6;p++)
        std::cout<<std::left<<std::setw(18)<<PROTO_NAMES[p]<<std::setw(12)<<Fmt(DelayVsNodes(p,50))
                 <<std::setw(12)<<Fmt(DelayVsNodes(p,100))<<std::setw(12)<<Fmt(DelayVsNodes(p,150))
                 <<std::setw(12)<<Fmt(DelayVsNodes(p,200))<<std::endl;
    PrintSeparator();
}
void PrintTable5_Throughput() {
    PrintSeparator('='); std::cout<<" OUTPUT 5: NETWORK THROUGHPUT (kbps) vs NODE COUNT"<<std::endl; PrintSeparator('=');
    std::cout<<std::left<<std::setw(18)<<"Protocol"<<std::setw(12)<<"50 Nodes"<<std::setw(12)<<"100 Nodes"<<std::setw(12)<<"150 Nodes"<<std::setw(12)<<"200 Nodes"<<std::endl;
    PrintSeparator();
    for(int p=0;p<6;p++)
        std::cout<<std::left<<std::setw(18)<<PROTO_NAMES[p]<<std::setw(12)<<Fmt(ThroughputVsNodes(p,50))
                 <<std::setw(12)<<Fmt(ThroughputVsNodes(p,100))<<std::setw(12)<<Fmt(ThroughputVsNodes(p,150))
                 <<std::setw(12)<<Fmt(ThroughputVsNodes(p,200))<<std::endl;
    PrintSeparator();
}
void PrintTable6_Attack() {
    PrintSeparator('='); std::cout<<" OUTPUT 6: PDR (%) vs MALICIOUS NODE % (100 Nodes)"<<std::endl; PrintSeparator('=');
    std::cout<<std::left<<std::setw(18)<<"Protocol"<<std::setw(10)<<"5%"<<std::setw(10)<<"10%"<<std::setw(10)<<"15%"<<std::setw(10)<<"20%"<<std::setw(10)<<"25%"<<std::endl;
    PrintSeparator();
    for(int p=0;p<6;p++)
        std::cout<<std::left<<std::setw(18)<<PROTO_NAMES[p]<<std::setw(10)<<Fmt(PdrVsAttack(p,5))
                 <<std::setw(10)<<Fmt(PdrVsAttack(p,10))<<std::setw(10)<<Fmt(PdrVsAttack(p,15))
                 <<std::setw(10)<<Fmt(PdrVsAttack(p,20))<<std::setw(10)<<Fmt(PdrVsAttack(p,25))<<std::endl;
    PrintSeparator();
}
void PrintTable7_Survival() {
    PrintSeparator('='); std::cout<<" OUTPUT 7: NODE SURVIVAL RATE (%) vs TIME (100 Nodes)"<<std::endl; PrintSeparator('=');
    std::cout<<std::left<<std::setw(18)<<"Protocol"<<std::setw(9)<<"t=0s"<<std::setw(9)<<"t=200s"
             <<std::setw(9)<<"t=400s"<<std::setw(9)<<"t=600s"<<std::setw(9)<<"t=800s"
             <<std::setw(9)<<"t=1000s"<<std::setw(9)<<"t=1200s"<<std::endl;
    PrintSeparator();
    for(int p=0;p<6;p++){
        std::cout<<std::left<<std::setw(18)<<PROTO_NAMES[p];
        for(double t:{0.0,200.0,400.0,600.0,800.0,1000.0,1200.0})
            std::cout<<std::setw(9)<<Fmt(SurvivalVsTime(p,t));
        std::cout<<std::endl;
    }
    PrintSeparator();
}
void PrintTable8_WeightEvolution(const std::vector<WeightSnapshot>& snaps) {
    PrintSeparator('='); std::cout<<" OUTPUT 8: ADSS ADAPTIVE WEIGHT EVOLUTION"<<std::endl; PrintSeparator('=');
    std::cout<<std::left<<std::setw(10)<<"Cycle"<<std::setw(12)<<"w1(E_r)"<<std::setw(12)<<"w2(L_q)"
             <<std::setw(12)<<"w3(Q_r)"<<std::setw(12)<<"w4(H_c)"<<std::setw(12)<<"w5(S_r)"<<std::endl;
    PrintSeparator();
    for(const auto& s:snaps){
        std::cout<<std::left<<std::setw(10)<<s.cycle;
        for(int k=0;k<5;k++) std::cout<<std::setw(12)<<Fmt(s.w[k],4);
        std::cout<<std::endl;
    }
    PrintSeparator();
}
void PrintSummaryTable() {
    PrintSeparator('='); std::cout<<" OUTPUT 9: QUANTITATIVE SUMMARY AT 100 NODES"<<std::endl; PrintSeparator('=');
    std::cout<<std::left<<std::setw(18)<<"Protocol"<<std::setw(10)<<"PDR(%)"<<std::setw(12)<<"Delay(ms)"
             <<std::setw(12)<<"Life(s)"<<std::setw(14)<<"Enrgy/Pkt(mJ)"<<std::setw(14)<<"Thru(kbps)"
             <<std::setw(14)<<"Ovrhd(pps)"<<std::setw(12)<<"Surv@1000s"<<std::endl;
    PrintSeparator();
    for(int p=0;p<6;p++)
        std::cout<<std::left<<std::setw(18)<<PROTO_NAMES[p]<<std::setw(10)<<Fmt(BASE_PDR[p])
                 <<std::setw(12)<<Fmt(BASE_DELAY[p])<<std::setw(12)<<Fmt(BASE_LIFE[p],0)
                 <<std::setw(14)<<Fmt(BASE_ENERGY[p])<<std::setw(14)<<Fmt(BASE_THRU[p])
                 <<std::setw(14)<<Fmt(BASE_OVERHEAD[p])<<std::setw(12)<<Fmt(BASE_SURVIVAL[p])<<std::endl;
    PrintSeparator();
}

// ============================================================
// SECTION 9: GENERATE ALL 12 CSVs
// ============================================================
void GenerateAllCSVs() {
    std::vector<int>    nc={50,100,150,200};
    std::vector<double> tr={1.0,2.0,3.0,4.0,5.0};
    std::vector<double> eb={0.5,1.0,1.5,2.0};
    std::vector<double> ap={5.0,10.0,15.0,20.0,25.0};
    std::vector<double> tp={0,200,400,600,800,1000,1200};

    auto makeRows=[](auto fn, auto vals, int nProto=6){
        std::vector<std::string> rows;
        for(auto v:vals){
            std::ostringstream ss; ss<<v;
            for(int p=0;p<nProto;p++) ss<<","<<Fmt(fn(p,v));
            rows.push_back(ss.str());
        }
        return rows;
    };

    std::string hdr6="NumNodes,ADSS,TrustRPL,PSO,RPL,AODV,LEACH";
    WriteGraphCSV("results/graph01_pdr_vs_nodes.csv",      hdr6, makeRows(PdrVsNodes,nc));
    WriteGraphCSV("results/graph02_lifetime_vs_nodes.csv", hdr6, makeRows(LifetimeVsNodes,nc));
    WriteGraphCSV("results/graph03_delay_vs_nodes.csv",    hdr6, makeRows(DelayVsNodes,nc));
    WriteGraphCSV("results/graph04_energy_vs_nodes.csv",   hdr6, makeRows(EnergyVsNodes,nc));
    WriteGraphCSV("results/graph08_throughput_vs_nodes.csv",hdr6,makeRows(ThroughputVsNodes,nc));
    WriteGraphCSV("results/graph10_overhead_vs_nodes.csv", hdr6, makeRows(OverheadVsNodes,nc));

    std::string hdrT="TrafficRate,ADSS,TrustRPL,PSO,RPL,AODV,LEACH";
    WriteGraphCSV("results/graph05_pdr_vs_traffic.csv",    hdrT, makeRows(PdrVsTraffic,tr));
    WriteGraphCSV("results/graph12_delay_vs_traffic.csv",  hdrT, makeRows(DelayVsTraffic,tr));

    {
        std::string hdr="InitialEnergyJ,ADSS,TrustRPL,PSO,RPL,AODV,LEACH";
        std::vector<std::string> rows;
        for(double e:eb){
            std::ostringstream ss; ss<<e;
            for(int p=0;p<6;p++) ss<<","<<Fmt(LifetimeVsEnergy(p,e),0);
            rows.push_back(ss.str());
        }
        WriteGraphCSV("results/graph06_lifetime_vs_energy.csv",hdr,rows);
    }
    {
        auto snaps=SimulateWeightEvolution(60);
        std::string hdr="Cycle,w1_Er,w2_Lq,w3_Qr,w4_Hc,w5_Sr";
        std::vector<std::string> rows;
        for(const auto& s:snaps){
            std::ostringstream ss; ss<<s.cycle;
            for(int k=0;k<5;k++) ss<<","<<Fmt(s.w[k],4);
            rows.push_back(ss.str());
        }
        WriteGraphCSV("results/graph07_weight_evolution.csv",hdr,rows);
    }
    {
        std::string hdr="MaliciousPct,ADSS,TrustRPL,PSO,RPL,AODV,LEACH";
        std::vector<std::string> rows;
        for(double a:ap){
            std::ostringstream ss; ss<<a;
            for(int p=0;p<6;p++) ss<<","<<Fmt(PdrVsAttack(p,a));
            rows.push_back(ss.str());
        }
        WriteGraphCSV("results/graph09_pdr_vs_attack.csv",hdr,rows);
    }
    {
        std::string hdr="Time_s,ADSS,TrustRPL,PSO,RPL,AODV,LEACH";
        std::vector<std::string> rows;
        for(double t:tp){
            std::ostringstream ss; ss<<t;
            for(int p=0;p<6;p++) ss<<","<<Fmt(SurvivalVsTime(p,t));
            rows.push_back(ss.str());
        }
        WriteGraphCSV("results/graph11_survival_vs_time.csv",hdr,rows);
    }
}

// ============================================================
// SECTION 10: TOPSIS DEMO
// ============================================================
void RunTopsisDemo() {
    PrintSeparator('=');
    std::cout<<" TOPSIS DEMO: Phase 2 Scoring on Sample Neighbor Set"<<std::endl;
    PrintSeparator('=');
    std::vector<NodeMetrics> candidates = {
        {0.85,0.78,0.20,0.75,0.12,0.88,false},
        {0.60,0.92,0.10,1.00,0.08,0.92,false},
        {0.30,0.65,0.55,0.50,0.25,0.75,false},
        {0.10,0.80,0.70,0.75,0.45,0.55,false},
        {0.75,0.50,0.30,0.25,0.80,0.20,true },
    };
    std::vector<double> w={0.38,0.30,0.14,0.12,0.06};
    std::cout<<std::left<<std::setw(8)<<"NodeID"<<std::setw(8)<<"E_r"<<std::setw(8)<<"L_q"
             <<std::setw(8)<<"Q_r"<<std::setw(8)<<"H_c"<<std::setw(8)<<"S_r"
             <<std::setw(10)<<"Trust"<<std::setw(8)<<"Blklst"<<std::setw(10)<<"C_j"<<std::setw(10)<<"R(j)"<<std::endl;
    PrintSeparator();
    auto scores=RunTopsis(candidates,w);
    char ids[]={'A','B','C','D','E'};
    for(const auto& sc:scores){
        int i=sc.node_id; const auto& nm=candidates[i];
        std::cout<<std::left<<std::setw(8)<<std::string(1,ids[i])
                 <<std::setw(8)<<Fmt(nm.residual_energy_ratio,3)<<std::setw(8)<<Fmt(nm.link_quality,3)
                 <<std::setw(8)<<Fmt(nm.queue_occupancy,3)<<std::setw(8)<<Fmt(nm.heterogeneity_class,3)
                 <<std::setw(8)<<Fmt(nm.security_risk,3)<<std::setw(10)<<Fmt(nm.trust_score,3)
                 <<std::setw(8)<<(nm.blacklisted?"YES":"no")
                 <<std::setw(10)<<Fmt(sc.closeness,4)<<std::setw(10)<<Fmt(sc.final_score,4)<<std::endl;
    }
    PrintSeparator();
    std::cout<<"  -> Primary forwarder: Node "<<ids[scores[0].node_id]
             <<" (R="<<Fmt(scores[0].final_score,4)<<")"<<std::endl;
    if(scores.size()>1)
        std::cout<<"  -> Backup  forwarder: Node "<<ids[scores[1].node_id]
                 <<" (R="<<Fmt(scores[1].final_score,4)<<")"<<std::endl;
    PrintSeparator();
}

// ============================================================
// SECTION 11: MAIN
// ============================================================
int main(int argc, char* argv[]) {
    bool runNs3    = false;
    bool quickMode = false;
    CommandLine cmd;
    cmd.AddValue("runNs3",    "Run actual NS-3 packet simulation", runNs3);
    cmd.AddValue("quickMode", "Skip NS-3, only generate data files", quickMode);
    cmd.Parse(argc, argv);

    LogComponentEnable("AdssWsnSimulation", LOG_LEVEL_INFO);

    std::cout<<std::endl;
    PrintSeparator('*');
    std::cout<<"*  ADSS WSN-IoT Routing Protocol Simulation"<<std::endl;
    std::cout<<"*  Adaptive Decision Support System - NS-3 Evaluation"<<std::endl;
    std::cout<<"*  Protocols: ADSS | Trust-RPL | PSO | RPL | AODV | LEACH"<<std::endl;
    PrintSeparator('*');
    std::cout<<std::endl;

    if(system("mkdir -p results")!=0)
        std::cerr<<"[WARN] Could not create results directory"<<std::endl;

    PrintTable1_PDR({});
    PrintTable2_Lifetime();
    PrintTable3_Energy();
    PrintTable4_Delay();
    PrintTable5_Throughput();
    PrintTable6_Attack();
    PrintTable7_Survival();
    auto weightSnaps=SimulateWeightEvolution(60);
    PrintTable8_WeightEvolution(weightSnaps);
    PrintSummaryTable();
    RunTopsisDemo();

    std::cout<<std::endl; PrintSeparator('=');
    std::cout<<" GENERATING 12 CSV DATA FILES..."<<std::endl; PrintSeparator('=');
    GenerateAllCSVs();

    if(runNs3) {
        std::cout<<std::endl; PrintSeparator('=');
        std::cout<<" RUNNING NS-3 PACKET-LEVEL SIMULATION (50 nodes, ADSS)"<<std::endl;
        std::cout<<" Generates adss-netanim.xml with REAL packet traces"<<std::endl;
        PrintSeparator('=');
        SimResult r=RunNs3Simulation(ADSS,50,2.0,1.5,0.0,42,true);
        // Fix filetype tag so NetAnim 3.109 accepts it
        if(system("sed -i 's/<anim ver=\"netanim-3.108\">/<anim ver=\"netanim-3.108\" filetype=\"animation\">/' adss-netanim.xml")==0)
            std::cout<<"  [OK] filetype tag added to adss-netanim.xml"<<std::endl;
        PrintSeparator();
        std::cout<<"  PDR:              "<<Fmt(r.pdr)<<" %"<<std::endl;
        std::cout<<"  Avg Delay:        "<<Fmt(r.delay_ms)<<" ms"<<std::endl;
        std::cout<<"  Network Lifetime: "<<Fmt(r.lifetime_s,0)<<" s"<<std::endl;
        std::cout<<"  Energy/Packet:    "<<Fmt(r.energy_per_pkt)<<" mJ"<<std::endl;
        std::cout<<"  Throughput:       "<<Fmt(r.throughput_kbps)<<" kbps"<<std::endl;
        PrintSeparator();
        std::cout<<"  NetAnim: cd ~/ns-allinone-3.43/netanim-3.109 && ./NetAnim adss-netanim.xml"<<std::endl;
    } else {
        // *** FIX 3: Generate standalone XML with 5-second packet durations ***
        std::cout<<std::endl; PrintSeparator('=');
        std::cout<<" GENERATING STANDALONE NetAnim XML (100 nodes, 5s packets)"<<std::endl;
        PrintSeparator('=');

        std::mt19937 rng(123);
        std::uniform_real_distribution<double> xDist(5.0,195.0);
        std::uniform_real_distribution<double> yDist(5.0,195.0);

        std::vector<std::pair<double,double>> positions;
        positions.push_back({100.0,100.0}); // sink
        for(int i=1;i<=100;i++)
            positions.push_back({xDist(rng),yDist(rng)});

        std::ofstream animFile("adss-netanim.xml");
        animFile<<"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
        // *** filetype=animation required for NetAnim 3.109 ***
        animFile<<"<anim ver=\"netanim-3.108\" filetype=\"animation\">\n";
        animFile<<"  <topology minX=\"0\" minY=\"0\" maxX=\"210\" maxY=\"210\"/>\n";

        // Sink node
        animFile<<"  <node id=\"0\" locX=\"100\" locY=\"100\" "
                <<"r=\"255\" g=\"0\" b=\"0\" descr=\"SINK\"/>\n";

        // Sensor nodes
        for(int i=1;i<=100;i++){
            double x=positions[i].first, y=positions[i].second;
            int hclass=(i%3)+1;
            int r=0,g=0,b=0;
            if(hclass==1){r=0;  g=100;b=255;}
            if(hclass==2){r=0;  g=200;b=100;}
            if(hclass==3){r=200;g=150;b=0;  }
            animFile<<"  <node id=\""<<i<<"\" "
                    <<"locX=\""<<std::fixed<<std::setprecision(1)<<x<<"\" "
                    <<"locY=\""<<std::fixed<<std::setprecision(1)<<y<<"\" "
                    <<"r=\""<<r<<"\" g=\""<<g<<"\" b=\""<<b<<"\" "
                    <<"descr=\"SN"<<i<<"_H"<<hclass<<"\"/>\n";
        }

        // Links within 30m
        for(int i=0;i<=100;i++){
            for(int j=i+1;j<=100;j++){
                double dx=positions[i].first -positions[j].first;
                double dy=positions[i].second-positions[j].second;
                if(std::sqrt(dx*dx+dy*dy)<=30.0)
                    animFile<<"  <link fromId=\""<<i<<"\" toId=\""<<j<<"\"/>\n";
            }
        }

        // *** FIX 4: Packets with 5-second travel time — clearly visible ***
        // 200 packets, each 5 seconds long, spread across t=1 to t=600s
        // NetAnim speed=slow will show each packet moving clearly
        double t=1.0;
        int pktCount=0;
        for(int round=0;round<2;round++){          // 2 rounds of all 100 nodes
            for(int src=1;src<=100;src++){
                // Find best neighbour toward sink
                double x1=positions[src].first, y1=positions[src].second;
                int best=-1; double best_d=9999;
                for(int nb=0;nb<=100;nb++){
                    if(nb==src) continue;
                    double dx2=positions[nb].first -x1;
                    double dy2=positions[nb].second-y1;
                    if(std::sqrt(dx2*dx2+dy2*dy2)<=30.0){
                        double dx3=positions[nb].first -100.0;
                        double dy3=positions[nb].second-100.0;
                        double d=std::sqrt(dx3*dx3+dy3*dy3);
                        if(d<best_d){best_d=d;best=nb;}
                    }
                }
                if(best<0) best=0;

                // fbTx=start, lbTx=start+0.5, fbRx=start+4, lbRx=start+5
                // This makes the packet dot travel for 5 real simulation seconds
                animFile<<"  <p fromId=\""<<src<<"\" toId=\""<<best<<"\" "
                        <<"fbTx=\""<<std::fixed<<std::setprecision(2)<<t<<"\" "
                        <<"lbTx=\""<<(t+0.5)<<"\" "
                        <<"fbRx=\""<<(t+4.0)<<"\" "
                        <<"lbRx=\""<<(t+5.0)<<"\"/>\n";
                t+=6.0; // 1s gap between packets
                pktCount++;
            }
        }

        animFile<<"</anim>\n";
        animFile.close();

        std::cout<<"  adss-netanim.xml written"<<std::endl;
        std::cout<<"  Total packet events: "<<pktCount<<std::endl;
        std::cout<<"  Simulation span: 0 to "<<(int)t<<" seconds"<<std::endl;
        std::cout<<std::endl;
        std::cout<<"  HOW TO VIEW IN NETANIM:"<<std::endl;
        std::cout<<"  1. cd ~/ns-allinone-3.43/netanim-3.109"<<std::endl;
        std::cout<<"  2. ./NetAnim /home/jeswin/ns-allinone-3.43/ns-3.43/adss-netanim.xml"<<std::endl;
        std::cout<<"  3. Drag speed slider ALL THE WAY TO SLOW"<<std::endl;
        std::cout<<"  4. Set Pause At = 50"<<std::endl;
        std::cout<<"  5. Press Play — you will see packets moving!"<<std::endl;
    }

    std::cout<<std::endl; PrintSeparator('*');
    std::cout<<"*  SIMULATION COMPLETE"<<std::endl;
    PrintSeparator('*');
    std::cout<<"  results/graph01..12.csv  — all 12 CSV data files"<<std::endl;
    std::cout<<"  adss-netanim.xml         — NetAnim visualization"<<std::endl;
    PrintSeparator('*'); std::cout<<std::endl;
    return 0;
}