<div align="center">

# Network Intelligence Research Portfolio

### A. Jeswin Karunya Benedict &nbsp;·&nbsp; `23MIC7225`

**School of Computer Science and Engineering &nbsp;·&nbsp; VIT-AP University, Amaravati**

[![Research](https://img.shields.io/badge/Domain-Network%20Intelligence-blue?style=for-the-badge&logoColor=white)](.)
[![IoT](https://img.shields.io/badge/Focus-IoT%20%7C%20WSN%20%7C%20VANET-green?style=for-the-badge&logoColor=white)](.)
[![ML](https://img.shields.io/badge/Methods-ML%20%7C%20DL%20%7C%20RL-orange?style=for-the-badge&logoColor=white)](.)
[![LaTeX](https://img.shields.io/badge/Written%20in-LaTeX-teal?style=for-the-badge&logo=latex&logoColor=white)](.)

</div>

---

> This repository contains academic research work exploring intelligent, adaptive, and secure network architectures — spanning vehicular ad hoc networks, IoT cybersecurity, and WSN-based routing systems.

---

## Repository Structure

```
Network Intelligence Research
├── Assignment 1    —  Intelligent Routing Protocols for Smart Traffic
├── Assignment 2    —  Hybrid ML & DL Models for Robust IoT Cybersecurity
└── Research Project —  Adaptive Decision Support System for WSN-IoT Routing
```

---

## Assignment 1 — Intelligent Routing Protocols for Smart Traffic and Urban Mobility Networks

<table>
<tr><td><b>Type</b></td><td>Survey & Analytical Study</td></tr>
<tr><td><b>Domain</b></td><td>Vehicular Ad Hoc Networks (VANET) · Intelligent Transportation Systems (ITS)</td></tr>
<tr><td><b>Tools</b></td><td>LaTeX · draw.io · SUMO · OMNeT++ (referenced)</td></tr>
</table>

### Overview

Modern urban environments face an exponentially growing traffic problem — not due to a lack of roads, but a lack of connected intelligence between vehicles. This paper examines how Intelligent Transportation Systems (ITS) leverage V2X communication and intelligent routing protocols to transform isolated vehicles into cooperative network nodes.

### Key Protocol Families Analyzed

| Protocol Family | Latency | Overhead | Best Deployment Scenario |
|---|---|---|---|
| **Proactive Topology-Based** (DSDV, OLSR) | Low | High | Stable RSU backhaul corridors |
| **Reactive Topology-Based** (AODV, DSR) | High | Low–Moderate | Low-density environments (<40 vehicles/km) |
| **Geographic / Position-Based** (GPSR) | Low | Low–Moderate | Dense urban grids — current best practice |
| **Broadcast / Geocast** | Very Low | High | Safety-critical message dissemination |
| **Learning-Based Adaptive** (DRL, Federated RL) | Low–Moderate | Moderate | Near-future adaptive urban deployment |

### Key Insights

- **Road-aware geographic forwarding** outperforms map-oblivious protocols by 30–40% PDR improvement in organic urban topologies (European-style cities)
- **Reactive protocols become obsolete** beyond approximately 50 vehicles/km due to self-interfering RREQ floods
- **Deep Reinforcement Learning** routing achieves near-optimal delivery rates across all density profiles — the primary challenge being simulation-to-real-world transfer
- **Federated learning** is identified as the bridge to scalable, privacy-preserving on-road RL deployment

### Architecture Covered

- V2V, V2I, V2P, V2N communication modalities
- DSRC (IEEE 802.11p @ 5.9 GHz) vs. Cellular V2X (C-V2X / 3GPP Rel. 14+)
- RSU gateway integration with cloud traffic management backends
- SUMO + OMNeT++ (Veins/TraCI) simulation methodology for realistic mobility modeling

---

## Assignment 2 — Hybrid Machine Learning and Deep Learning Models for Robust IoT Cybersecurity

<table>
<tr><td><b>Type</b></td><td>Technical Survey & Model Analysis</td></tr>
<tr><td><b>Domain</b></td><td>IoT Security · Intrusion Detection · Threat Intelligence</td></tr>
<tr><td><b>Approach</b></td><td>Hybrid ML/DL fusion for multi-vector cyberattack defense</td></tr>
</table>

### Overview

With billions of IoT devices deployed globally across smart cities, healthcare, and industrial systems, the attack surface is unprecedented. This work investigates how hybrid combinations of classical machine learning and deep learning architectures can produce more robust, generalizable intrusion detection and anomaly detection systems for IoT environments.

### Core Focus Areas

- **Threat Landscape** — DDoS, Man-in-the-Middle, Replay Attacks, Botnets targeting IoT
- **Classical ML Approaches** — Random Forest, SVM, XGBoost for feature-engineered detection
- **Deep Learning Models** — LSTM, CNN, Autoencoders for sequential and raw-traffic analysis
- **Hybrid Architectures** — Ensemble and multi-stage fusion models for improved precision and recall
- **Federated Security** — Distributed model training across edge IoT nodes without raw data exposure

### Key Themes

- Trade-offs between detection accuracy, inference latency, and resource constraints on edge devices
- Handling class imbalance in IoT attack datasets (NSL-KDD, UNSW-NB15, Bot-IoT)
- Transfer learning and domain adaptation for cross-platform generalization

---

## Research Project — Adaptive Decision Support System Based Heterogeneous Routing Protocol for WSN-Based IoT Network

<table>
<tr><td><b>Type</b></td><td>Original Research Paper</td></tr>
<tr><td><b>Venue Target</b></td><td>Elsevier CAS Double-Column Journal Format</td></tr>
<tr><td><b>Domain</b></td><td>Wireless Sensor Networks · IoT Routing · Adaptive Systems</td></tr>
<tr><td><b>Affiliation</b></td><td>VIT-AP University, Amaravati, India</td></tr>
</table>

### Abstract

> WSN-IoT integration suffers from the continuous problem of routing as the environment poses issues such as heterogeneous devices, dynamic routing topologies, and extreme power limitations. Traditional routing techniques target single optimization criteria and hence are unable to handle situations involving multi-criteria such as power, reliability, latency, and security in the environment. This paper presents a new adaptive decision support framework handling heterogeneous routing via multicriteria optimization based on residual energy, link quality, node heterogeneity factors, load conditions, and risks of attacks.

### Research Objectives

1. **Integrated Routing Metric Framework** — Unify residual energy, link quality, node heterogeneity, traffic congestion, and security risk into a single multi-criteria decision model
2. **Lightweight MCDM Implementation** — Design an AHP/TOPSIS-based decision engine fit for resource-constrained WSN nodes with minimal compute and memory overhead
3. **Predictive ML Routing** — Deploy ML classifiers/regressors to predict link degradation, congestion, and node failure proactively — reducing reactive route repairs and control overhead
4. **Simulation Validation** — Benchmark ADSS against AODV, RPL, LEACH, and bio-inspired protocols across dense urban and sparse industrial topologies

### Proposed System: ADSS — Adaptive Decision Support System

```
+------------------------------------------------------------------+
|                         ADSS Framework                          |
|                                                                  |
|  +--------------+    +---------------+    +-----------------+   |
|  |    Metric    |    |  MCDM Engine  |    |   Predictive    |   |
|  |  Collector   |--->|  (AHP/TOPSIS) |--->|   ML Module     |   |
|  |              |    |               |    |  (Link/Energy)  |   |
|  +--------------+    +---------------+    +--------+--------+   |
|                                                    |            |
|  Inputs: Residual Energy · Link Quality ·          v            |
|  Node Heterogeneity · Traffic Load ·       +---------------+    |
|  Attack Risk Score                         |    Adaptive   |    |
|                                            | Route Decision|    |
|                                            +---------------+    |
+------------------------------------------------------------------+
```

### Simulation Results Overview

The following performance metrics were evaluated across varying node densities and traffic conditions:

| Metric | ADSS | AODV | RPL | LEACH |
|---|---|---|---|---|
| **Packet Delivery Ratio (PDR)** | Superior | Baseline | Moderate | Moderate |
| **Network Lifetime** | Extended | Moderate | Moderate | Limited |
| **Energy Efficiency** | Optimized | Average | Average | Cluster-limited |
| **Routing Overhead** | Controlled | High (reactive) | Moderate | Low |
| **Attack Resilience (PDR under attack)** | Robust | Vulnerable | Moderate | Vulnerable |

*Results generated via NS-2/NS-3 compatible simulation scripts (`.cc` source files included)*

### Project File Structure

```
Research Project
├── main.tex                      # Full paper in Elsevier CAS format
├── references.bib                # 30+ curated references (post-2023 journals)
├── source/
│   ├── adss_wsn.cc               # Core ADSS protocol implementation
│   ├── adss_pdr_comparison.cc    # PDR benchmarking script
│   ├── adss_qos_analysis.cc      # QoS evaluation
│   ├── adss_lifetime_analysis.cc
│   ├── adss_energy_overhead.cc
│   ├── graph01-graph12.tex       # PGFPlots graph sources
│   └── Fig1.drawio / fig2.drawio
├── results/
│   ├── graph00_comparative_summary_panel.png
│   ├── graph01_pdr_vs_nodes.{csv,png}
│   ├── graph02_lifetime_vs_nodes.{csv,png}
│   ├── graph03_delay_vs_nodes.{csv,png}
│   ├── graph04_energy_vs_nodes.{csv,png}
│   ├── graph05_pdr_vs_traffic.{csv,png}
│   ├── graph06_lifetime_vs_energy.{csv,png}
│   ├── graph07_weight_evolution.{csv,png}
│   ├── graph08_throughput_vs_nodes.{csv,png}
│   ├── graph09_pdr_vs_attack.{csv,png}
│   ├── graph10_overhead_vs_nodes.{csv,png}
│   ├── graph11_survival_vs_time.{csv,png}
│   └── graph12_delay_vs_traffic.{csv,png}
├── figures/
│   ├── figure1.png               # System architecture diagram
│   └── figure2.png               # Protocol flow illustration
└── PAPERS/
    ├── B1-B8                     # Base papers (journals, post-2023)
    └── R1-R26                    # Reference papers (30+ curated sources)
```

---

## Technologies and Tools

| Category | Tools |
|---|---|
| **Writing & Typesetting** | LaTeX · Overleaf · Elsevier CAS Template · BibTeX |
| **Simulation** | NS-2/NS-3 · SUMO · OMNeT++ (Veins/TraCI) |
| **Diagramming** | draw.io · PGFPlots / TikZ |
| **Languages** | C++ (simulation scripts) · LaTeX |
| **Domains** | VANET · WSN · IoT · Cybersecurity · Adaptive Routing |

---

## Research Themes

| Theme | Keywords |
|---|---|
| Intelligent Routing | VANET · Geographic Forwarding · DRL-based Routing |
| WSN-IoT Optimization | Energy Efficiency · Network Lifetime · Multi-hop Routing |
| Adaptive Decision Systems | AHP · TOPSIS · MCDM · Predictive ML |
| IoT Cybersecurity | Intrusion Detection · Hybrid ML/DL · Federated Security |
| Network Resilience | Attack-aware Routing · Fault Tolerance · QoS Guarantees |

---

## Author

<div align="center">

**A. Jeswin Karunya Benedict** &nbsp;·&nbsp; `23MIC7225`

School of Computer Science and Engineering
VIT-AP University · Amaravati, Andhra Pradesh, India

[jeswin.23mic7225@vitapstudent.ac.in](mailto:jeswin.23mic7225@vitapstudent.ac.in) &nbsp;·&nbsp; ORCID: [0009-0004-2202-9012](https://orcid.org/0009-0004-2202-9012)

**Advisor:** S. Gopikrishnan &nbsp;·&nbsp; [gopikrishnanme@gmail.com](mailto:gopikrishnanme@gmail.com)

</div>

---

<div align="center">

*© 2025 A. Jeswin Karunya Benedict · VIT-AP University · All research content is for academic purposes.*

</div>
