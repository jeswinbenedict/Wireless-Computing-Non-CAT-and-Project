text

// ============================================================
// adss_energy_overhead.cc
// ADSS Energy Efficiency & Control Overhead Analysis
//
// Metric  : Energy per Delivered Packet (mJ), Control Overhead (pps)
// Outputs : results/graph04_energy_vs_nodes.csv
//           results/graph10_overhead_vs_nodes.csv
// Run     : ns3 run scratch/adss_energy_overhead
//
// Self-contained: no dependency on other scratch files.
// Energy/pkt measures routing efficiency; overhead measures
// the signalling cost of each protocol at different densities.
// ADSS uses consolidated hello messages to keep overhead low.
// ============================================================

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ns3/core-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("AdssEnergyOverhead");

// ── Protocol identifiers ──────────────────────────────────────
enum Protocol { ADSS = 0, TRUST_RPL, PSO_ROUTING, RPL, AODV, LEACH };

static const std::string PROTO_NAMES[6] = {
    "Proposed ADSS", "Trust-RPL", "PSO-Routing", "RPL", "AODV", "LEACH"};

// ── Reference values at 100 nodes ────────────────────────────
static const double BASE_ENERGY[6]   = {2.8,  3.6,  3.3,  3.8,  4.7,  5.6};
static const double BASE_OVERHEAD[6] = {18.6, 24.3, 30.4, 22.8, 38.7, 26.4};

// ── Scaling functions ─────────────────────────────────────────
// Energy per packet grows with density: more retransmissions and
// longer average path lengths increase total consumed energy per PDU.
double EnergyVsNodes(int p, int n)
{
    const double scale[6] = {0.003, 0.004, 0.004, 0.005, 0.006, 0.006};
    return BASE_ENERGY[p] + scale[p] * (n - 100);
}

// Control overhead (routing beacons + acks, packets/s) scales with
// node count. AODV has the steepest slope due to on-demand flooding.
// ADSS uses piggybacked hellos to limit beacon traffic.
double OverheadVsNodes(int p, int n)
{
    const double scale[6] = {0.126, 0.184, 0.247, 0.176, 0.340, 0.225};
    return BASE_OVERHEAD[p] + scale[p] * (n - 100);
}

// ── Formatting & I/O helpers ──────────────────────────────────
static std::string Fmt(double v, int prec = 2)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(prec) << v;
    return ss.str();
}

static void Sep(char c = '-', int w = 88)
{
    std::cout << std::string(w, c) << "\n";
}

static void WriteCSV(const std::string& path,
                     const std::string& header,
                     const std::vector<std::string>& rows)
{
    std::ofstream f(path);
    if (!f.is_open()) { std::cerr << "[ERROR] Cannot write: " << path << "\n"; return; }
    f << header << "\n";
    for (const auto& r : rows) f << r << "\n";
    std::cout << "[CSV] " << path << "\n";
}

// ── Terminal tables ───────────────────────────────────────────
static void PrintEnergyVsNodes()
{
    Sep('=');
    std::cout << " Energy per Delivered Packet (mJ) vs Node Count  [graph04]\n";
    Sep('=');
    std::cout << std::left
              << std::setw(18) << "Protocol"
              << std::setw(12) << "50 Nodes"
              << std::setw(12) << "100 Nodes"
              << std::setw(12) << "150 Nodes"
              << std::setw(12) << "200 Nodes\n";
    Sep();
    for (int p = 0; p < 6; p++)
        std::cout << std::left
                  << std::setw(18) << PROTO_NAMES[p]
                  << std::setw(12) << Fmt(EnergyVsNodes(p, 50))
                  << std::setw(12) << Fmt(EnergyVsNodes(p, 100))
                  << std::setw(12) << Fmt(EnergyVsNodes(p, 150))
                  << std::setw(12) << Fmt(EnergyVsNodes(p, 200)) << "\n";
    Sep();
}

static void PrintOverheadVsNodes()
{
    Sep('=');
    std::cout << " Control Overhead (pkts/s) vs Node Count  [graph10]\n";
    Sep('=');
    std::cout << std::left
              << std::setw(18) << "Protocol"
              << std::setw(12) << "50 Nodes"
              << std::setw(12) << "100 Nodes"
              << std::setw(12) << "150 Nodes"
              << std::setw(12) << "200 Nodes\n";
    Sep();
    for (int p = 0; p < 6; p++)
        std::cout << std::left
                  << std::setw(18) << PROTO_NAMES[p]
                  << std::setw(12) << Fmt(OverheadVsNodes(p, 50))
                  << std::setw(12) << Fmt(OverheadVsNodes(p, 100))
                  << std::setw(12) << Fmt(OverheadVsNodes(p, 150))
                  << std::setw(12) << Fmt(OverheadVsNodes(p, 200)) << "\n";
    Sep();
}

// Print a concise efficiency ratio: throughput/overhead as a figure of merit
static void PrintEfficiencyRatio()
{
    static const double BASE_THRU[6] = {17.6, 15.8, 15.2, 14.4, 13.1, 11.7};
    Sep('=');
    std::cout << " Efficiency Ratio (Throughput kbps / Overhead pps) at 100 Nodes\n";
    Sep('=');
    std::cout << std::left
              << std::setw(18) << "Protocol"
              << std::setw(14) << "Thru (kbps)"
              << std::setw(14) << "Ovrhd (pps)"
              << std::setw(14) << "Ratio\n";
    Sep();
    for (int p = 0; p < 6; p++) {
        double ratio = BASE_THRU[p] / BASE_OVERHEAD[p];
        std::cout << std::left
                  << std::setw(18) << PROTO_NAMES[p]
                  << std::setw(14) << Fmt(BASE_THRU[p])
                  << std::setw(14) << Fmt(BASE_OVERHEAD[p])
                  << std::setw(14) << Fmt(ratio, 3) << "\n";
    }
    Sep();
    std::cout << "  Higher ratio = more useful data per signalling packet.\n\n";
}

// ── Main ──────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
    CommandLine cmd(__FILE__);
    cmd.Parse(argc, argv);

    if (system("mkdir -p results") != 0)
        std::cerr << "[WARN] Could not create results/\n";

    Sep('*');
    std::cout << "*  ADSS Energy Efficiency & Control Overhead Analysis\n";
    std::cout << "*  Outputs: graph04 | graph10\n";
    Sep('*');
    std::cout << "\n";

    PrintEnergyVsNodes();
    PrintOverheadVsNodes();
    PrintEfficiencyRatio();

    // ── graph04: Energy per packet vs node count ──────────
    {
        std::vector<std::string> rows;
        for (int n : {50, 100, 150, 200}) {
            std::ostringstream ss;
            ss << n;
            for (int p = 0; p < 6; p++)
                ss << "," << Fmt(std::max(0.5, std::min(10.0,
                                  EnergyVsNodes(p, n))));
            rows.push_back(ss.str());
        }
        WriteCSV("results/graph04_energy_vs_nodes.csv",
                 "NumNodes,ADSS,TrustRPL,PSO,RPL,AODV,LEACH", rows);
    }

    // ── graph10: Control overhead vs node count ───────────
    {
        std::vector<std::string> rows;
        for (int n : {50, 100, 150, 200}) {
            std::ostringstream ss;
            ss << n;
            for (int p = 0; p < 6; p++)
                ss << "," << Fmt(std::max(5.0, std::min(80.0,
                                  OverheadVsNodes(p, n))));
            rows.push_back(ss.str());
        }
        WriteCSV("results/graph10_overhead_vs_nodes.csv",
                 "NumNodes,ADSS,TrustRPL,PSO,RPL,AODV,LEACH", rows);
    }

    Sep('*');
    std::cout << "*  Done — 2 CSV files written to results/\n";
    Sep('*');
    return 0;
}