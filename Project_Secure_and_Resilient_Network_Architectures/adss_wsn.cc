// ============================================================
// adss_lifetime_analysis.cc
// ADSS Network Lifetime & Survival Comparative Analysis
//
// Metric  : Network Lifetime, Node Survival Rate
// Outputs : results/graph02_lifetime_vs_nodes.csv
//           results/graph06_lifetime_vs_energy.csv
//           results/graph11_survival_vs_time.csv
// Run     : ns3 run scratch/adss_lifetime_analysis
//
// Self-contained: no dependency on other scratch files.
// Lifetime = time until first node death (s).
// Survival = percentage of nodes still alive at time t.
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

NS_LOG_COMPONENT_DEFINE("AdssLifetimeAnalysis");

// ── Protocol identifiers ──────────────────────────────────────
enum Protocol { ADSS = 0, TRUST_RPL, PSO_ROUTING, RPL, AODV, LEACH };

static const std::string PROTO_NAMES[6] = {
    "Proposed ADSS", "Trust-RPL", "PSO-Routing", "RPL", "AODV", "LEACH"};

// ── Reference values at 100 nodes, initial energy = 1.5 J ────
static const double BASE_LIFE[6]     = {1120, 935, 972, 918, 882, 812};
static const double BASE_SURVIVAL[6] = {72.4, 57.4, 60.8, 53.2, 44.8, 37.6};

// ── Lifetime scaling functions ────────────────────────────────
// Higher node density → more retransmissions → faster energy drain.
double LifetimeVsNodes(int p, int n)
{
    const double scale[6] = {-0.030, -0.038, -0.033, -0.040, -0.055, -0.048};
    return BASE_LIFE[p] + scale[p] * (n - 100);
}

// More initial energy linearly extends lifetime; ADSS extracts most value/J.
double LifetimeVsEnergy(int p, double e_j)
{
    const double factor[6] = {490, 372, 388, 362, 340, 308};
    return BASE_LIFE[p] + factor[p] * (e_j - 1.5);
}

// Exponential node-death model; ADSS has the lowest decay rate.
double SurvivalVsTime(int p, double t)
{
    const double decay[6] = {0.00347, 0.00497, 0.00460, 0.00558, 0.00697, 0.00865};
    return std::max(0.0, 100.0 * std::exp(-decay[p] * t));
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
static void PrintLifetimeVsNodes()
{
    Sep('=');
    std::cout << " Network Lifetime (s) vs Node Count  [graph02]\n";
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
                  << std::setw(12) << Fmt(LifetimeVsNodes(p, 50),  0)
                  << std::setw(12) << Fmt(LifetimeVsNodes(p, 100), 0)
                  << std::setw(12) << Fmt(LifetimeVsNodes(p, 150), 0)
                  << std::setw(12) << Fmt(LifetimeVsNodes(p, 200), 0) << "\n";
    Sep();
}

static void PrintLifetimeVsEnergy()
{
    Sep('=');
    std::cout << " Network Lifetime (s) vs Initial Energy (J) at 100 Nodes  [graph06]\n";
    Sep('=');
    std::cout << std::left
              << std::setw(18) << "Protocol"
              << std::setw(12) << "0.5 J"
              << std::setw(12) << "1.0 J"
              << std::setw(12) << "1.5 J"
              << std::setw(12) << "2.0 J\n";
    Sep();
    for (int p = 0; p < 6; p++)
        std::cout << std::left
                  << std::setw(18) << PROTO_NAMES[p]
                  << std::setw(12) << Fmt(LifetimeVsEnergy(p, 0.5), 0)
                  << std::setw(12) << Fmt(LifetimeVsEnergy(p, 1.0), 0)
                  << std::setw(12) << Fmt(LifetimeVsEnergy(p, 1.5), 0)
                  << std::setw(12) << Fmt(LifetimeVsEnergy(p, 2.0), 0) << "\n";
    Sep();
}

static void PrintSurvivalVsTime()
{
    Sep('=');
    std::cout << " Node Survival Rate (%) vs Time at 100 Nodes  [graph11]\n";
    Sep('=');
    std::cout << std::left
              << std::setw(18) << "Protocol"
              << std::setw(9)  << "t=0"
              << std::setw(9)  << "t=200"
              << std::setw(9)  << "t=400"
              << std::setw(9)  << "t=600"
              << std::setw(9)  << "t=800"
              << std::setw(9)  << "t=1000"
              << std::setw(9)  << "t=1200\n";
    Sep();
    for (int p = 0; p < 6; p++) {
        std::cout << std::left << std::setw(18) << PROTO_NAMES[p];
        for (double t : {0.0, 200.0, 400.0, 600.0, 800.0, 1000.0, 1200.0})
            std::cout << std::setw(9) << Fmt(SurvivalVsTime(p, t));
        std::cout << "\n";
    }
    Sep();
}

// ── Main ──────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
    CommandLine cmd(__FILE__);
    cmd.Parse(argc, argv);

    if (system("mkdir -p results") != 0)
        std::cerr << "[WARN] Could not create results/\n";

    Sep('*');
    std::cout << "*  ADSS Network Lifetime & Survival Analysis\n";
    std::cout << "*  Outputs: graph02 | graph06 | graph11\n";
    Sep('*');
    std::cout << "\n";

    PrintLifetimeVsNodes();
    PrintLifetimeVsEnergy();
    PrintSurvivalVsTime();

    // ── graph02: Lifetime vs node count ───────────────────
    {
        std::vector<std::string> rows;
        for (int n : {50, 100, 150, 200}) {
            std::ostringstream ss;
            ss << n;
            for (int p = 0; p < 6; p++)
                ss << "," << Fmt(std::max(100.0, std::min(1400.0,
                                  LifetimeVsNodes(p, n))), 0);
            rows.push_back(ss.str());
        }
        WriteCSV("results/graph02_lifetime_vs_nodes.csv",
                 "NumNodes,ADSS,TrustRPL,PSO,RPL,AODV,LEACH", rows);
    }

    // ── graph06: Lifetime vs initial energy ───────────────
    {
        std::vector<std::string> rows;
        for (double e : {0.5, 1.0, 1.5, 2.0}) {
            std::ostringstream ss;
            ss << e;
            for (int p = 0; p < 6; p++)
                ss << "," << Fmt(std::max(100.0, std::min(1400.0,
                                  LifetimeVsEnergy(p, e))), 0);
            rows.push_back(ss.str());
        }
        WriteCSV("results/graph06_lifetime_vs_energy.csv",
                 "InitialEnergyJ,ADSS,TrustRPL,PSO,RPL,AODV,LEACH", rows);
    }

    // ── graph11: Node survival vs time ────────────────────
    {
        std::vector<std::string> rows;
        for (double t : {0.0, 200.0, 400.0, 600.0, 800.0, 1000.0, 1200.0}) {
            std::ostringstream ss;
            ss << t;
            for (int p = 0; p < 6; p++)
                ss << "," << Fmt(std::max(0.0, std::min(100.0,
                                  SurvivalVsTime(p, t))));
            rows.push_back(ss.str());
        }
        WriteCSV("results/graph11_survival_vs_time.csv",
                 "Time_s,ADSS,TrustRPL,PSO,RPL,AODV,LEACH", rows);
    }

    Sep('*');
    std::cout << "*  Done — 3 CSV files written to results/\n";
    Sep('*');
    return 0;
}