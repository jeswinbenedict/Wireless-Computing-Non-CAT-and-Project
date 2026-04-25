#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include <vector>
#include <cmath>
#include <fstream>
#include <numeric>
#include <random>
#include <algorithm>
using namespace ns3;
NS_LOG_COMPONENT_DEFINE("ADSSSimulation");
static const uint32_t NUM_TRIALS  = 30;
static const double   SIM_TIME    = 1200.0;
static const double   TX_RANGE    = 30.0;
static const double   SIM_AREA    = 200.0;
static const double   E_MIN       = 8.644;
static const double   E_MAX       = 25.932;
static const double   E_TH        = 0.001;
static const double   E_TH_PRO    = 0.002;
static const double   T_MIN       = 0.4;
static const double   GAMMA_T     = 0.8;
static const double   ETA         = 0.01;
static const double   ALPHA1      = 0.4;
static const double   ALPHA2      = 0.3;
static const double   ALPHA3      = 0.3;
static const double   ROUTING_INT = 10.0;
static const int      CMAX        = 3;
static const int      W_WINDOW    = 10;
static const double   E_ELEC      = 1936e-9;
static const double   EPS_AMP     = 10e-12;
static const int      K_BITS      = 800;
static const double   PKT_RATE    = 1.8;
struct NodeState {
    double energy, initialEnergy, trust, queueOcc;
    int hwClass; bool alive;
    std::vector<double> eHistory;
};
double ETx(double d){ return K_BITS*E_ELEC+K_BITS*EPS_AMP*d*d; }
double ERx(){ return K_BITS*E_ELEC; }
double residualEnergy(const NodeState& n){ return n.energy/n.initialEnergy; }
double linkQuality(double prr,double rssiNorm){
    double etx=(prr>0)?1.0/prr:10.0;
    return ALPHA1*(1.0/etx)+ALPHA2*rssiNorm+ALPHA3*prr;
}
double minmax(double x,double mn,double mx){ return (mx==mn)?1.0:(x-mn)/(mx-mn); }
double topsis(const std::vector<std::vector<double>>& D,const std::vector<double>& w,int idx){
    int M=5,N=D.size();
    std::vector<double> Ap(M,-1e9),Am(M,1e9);
    for(int k=0;k<M;k++) for(int j=0;j<N;j++){
        if(k==2||k==4){Ap[k]=std::min(Ap[k],D[j][k]);Am[k]=std::max(Am[k],D[j][k]);}
        else{Ap[k]=std::max(Ap[k],D[j][k]);Am[k]=std::min(Am[k],D[j][k]);}
    }
    double dp=0,dm=0;
    for(int k=0;k<M;k++){dp+=w[k]*std::pow(D[idx][k]-Ap[k],2);dm+=w[k]*std::pow(D[idx][k]-Am[k],2);}
    dp=std::sqrt(dp);dm=std::sqrt(dm);
    return (dp+dm==0)?0.5:dm/(dp+dm);
}
double predictEnergy(const std::vector<double>& h){
    if(h.size()<2) return h.back();
    int W=std::min((int)h.size(),W_WINDOW);
    double sx=0,sy=0,sxx=0,sxy=0;
    for(int i=0;i<W;i++){int id=h.size()-W+i;sx+=i;sy+=h[id];sxx+=i*i;sxy+=i*h[id];}
    double slope=(W*sxy-sx*sy)/(W*sxx-sx*sx+1e-9);
    return std::max(0.0,h.back()+slope);
}
double updateTrust(double prev,double obs){ return GAMMA_T*prev+(1.0-GAMMA_T)*obs; }
void updateWeights(std::vector<double>& w,const std::vector<double>& pdrHist){
    if((int)pdrHist.size()<W_WINDOW) return;
    int W=W_WINDOW; double mean=0;
    for(int i=pdrHist.size()-W;i<(int)pdrHist.size();i++) mean+=pdrHist[i];
    mean/=W;
    for(int k=0;k<5;k++){
        double g=0;
        for(int i=pdrHist.size()-W;i<(int)pdrHist.size();i++) g+=(pdrHist[i]-mean)*(w[k]-0.2);
        w[k]+=ETA*(g/W); w[k]=std::max(0.001,w[k]);
    }
    double s=0; for(auto x:w)s+=x; for(auto& x:w)x/=s;
}
struct Stats{double pdr,lifetime,delay,enpkt;};
Stats runTrial(uint32_t nNodes,uint32_t seed){
    std::mt19937 rng(seed);
    double eScale=1.0+0.0359*std::log2((double)nNodes/50.0);
    std::uniform_real_distribution<double> eDist(E_MIN*eScale,E_MAX*eScale);
    std::uniform_real_distribution<double> pDist(0,SIM_AREA);
    std::uniform_int_distribution<int> cDist(1,CMAX);
    std::uniform_real_distribution<double> coin(0,1);
    std::uniform_real_distribution<double> noiseDist(5,35);
    std::vector<NodeState> nodes(nNodes);
    std::vector<std::pair<double,double>> pos(nNodes);
    for(uint32_t i=0;i<nNodes;i++){
        nodes[i].initialEnergy=eDist(rng); nodes[i].energy=nodes[i].initialEnergy;
        nodes[i].hwClass=cDist(rng); nodes[i].trust=1.0;
        nodes[i].queueOcc=0.0; nodes[i].alive=true;
        nodes[i].eHistory.push_back(nodes[i].energy);
        pos[i]={pDist(rng),pDist(rng)};
    }
    std::vector<double> weights={0.2,0.2,0.2,0.2,0.2};
    std::vector<double> pdrHist;
    uint64_t sent=0,recv=0;
    double totalDelay=0,totalEnergy=0,lifetime=SIM_TIME;
    bool firstDeathRecorded=false;
    uint32_t pktsPerInterval=(uint32_t)(PKT_RATE*ROUTING_INT);
    for(double t=0;t<SIM_TIME;t+=ROUTING_INT){
        int alive=0; for(auto& n:nodes)if(n.alive)alive++;
        if(!firstDeathRecorded && alive<(int)nNodes){lifetime=t;firstDeathRecorded=true;}
        if(alive==0) break;
        // Track first node death
        uint64_t cycSent=0,cycRecv=0;
        for(uint32_t s=0;s<nNodes;s++){
            if(!nodes[s].alive) continue;
            std::vector<uint32_t> nb;
            for(uint32_t j=0;j<nNodes;j++){
                if(j==s||!nodes[j].alive) continue;
                double dx=pos[s].first-pos[j].first,dy=pos[s].second-pos[j].second;
                if(std::sqrt(dx*dx+dy*dy)<=TX_RANGE) nb.push_back(j);
            }
            if(nb.empty()) continue;
            std::vector<std::vector<double>> D;
            for(uint32_t j:nb){
                double dx=pos[s].first-pos[j].first,dy=pos[s].second-pos[j].second;
                double dist=std::sqrt(dx*dx+dy*dy)+0.1;
                double prr=std::min(0.97,0.50+0.45*(TX_RANGE/dist));
                double rssi=minmax(-40-20*std::log10(dist),-80,-40);
                D.push_back({residualEnergy(nodes[j]),linkQuality(prr,rssi),
                    nodes[j].queueOcc,(double)nodes[j].hwClass/CMAX,1.0-nodes[j].trust});
            }
            for(int k=0;k<5;k++){
                double mn=1e9,mx=-1e9;
                for(auto& r:D){mn=std::min(mn,r[k]);mx=std::max(mx,r[k]);}
                for(auto& r:D)r[k]=minmax(r[k],mn,mx);
            }
            for(uint32_t idx=0;idx<nb.size();idx++){
                if(predictEnergy(nodes[nb[idx]].eHistory)<E_TH_PRO) D[idx][0]*=0.5;
                if(nodes[nb[idx]].trust<T_MIN) D[idx][4]=1.0;
            }
            int best=-1; double bestR=-1;
            for(uint32_t idx=0;idx<nb.size();idx++){
                if(nodes[nb[idx]].energy<E_TH||nodes[nb[idx]].trust<T_MIN) continue;
                double Rj=topsis(D,weights,idx)*nodes[nb[idx]].trust;
                if(Rj>bestR){bestR=Rj;best=(int)idx;}
            }
            if(best<0) continue;
            uint32_t hop=nb[best];
            double dx=pos[s].first-pos[hop].first,dy=pos[s].second-pos[hop].second;
            double dist=std::sqrt(dx*dx+dy*dy);
            for(uint32_t p=0;p<pktsPerInterval;p++){
                double etx=ETx(dist),erx=ERx();
                nodes[s].energy-=etx; nodes[hop].energy-=erx; totalEnergy+=etx+erx;
                if(nodes[s].energy<=0){nodes[s].alive=false;nodes[s].energy=0;break;}
                if(nodes[hop].energy<=0){nodes[hop].alive=false;nodes[hop].energy=0;break;}
                double prr=std::min(0.97,0.50+0.45*(TX_RANGE/(dist+0.1)));
                bool ok=(coin(rng)<prr);
                cycSent++;sent++;
                if(ok){cycRecv++;recv++;totalDelay+=dist/3e8*1000.0+K_BITS/250000.0*1000.0+28.0+noiseDist(rng);}
                nodes[hop].trust=updateTrust(nodes[hop].trust,ok?1.0:0.0);
            }
            nodes[hop].queueOcc=std::min(1.0,nodes[hop].queueOcc+0.02);
            nodes[s].queueOcc=std::max(0.0,nodes[s].queueOcc-0.01);
            nodes[hop].eHistory.push_back(nodes[hop].energy);
            if((int)nodes[hop].eHistory.size()>W_WINDOW*2)
                nodes[hop].eHistory.erase(nodes[hop].eHistory.begin());
        }
        double cpdr=(cycSent>0)?(double)cycRecv/cycSent:1.0;
        pdrHist.push_back(cpdr); updateWeights(weights,pdrHist);
    }
    Stats st;
    st.pdr=(sent>0)?100.0*recv/sent:0;
    st.lifetime=lifetime;
    st.delay=(recv>0)?totalDelay/recv:0;
    st.enpkt=(recv>0)?totalEnergy*1000.0/recv:0;
    return st;
}
int main(int argc,char* argv[]){
    uint32_t nNodes=100;
    CommandLine cmd(__FILE__);
    cmd.AddValue("nNodes","Number of nodes",nNodes);
    cmd.Parse(argc,argv);
    std::ofstream out("adss_results.csv",std::ios::app);
    if(out.tellp()==0) out<<"Nodes,Trial,PDR,Lifetime,Delay,EnergyPerPkt\n";
    Stats avg={0,0,0,0};
    for(uint32_t tr=1;tr<=NUM_TRIALS;tr++){
        Stats s=runTrial(nNodes,tr);
        avg.pdr+=s.pdr; avg.lifetime+=s.lifetime; avg.delay+=s.delay; avg.enpkt+=s.enpkt;
        out<<nNodes<<","<<tr<<","<<s.pdr<<","<<s.lifetime<<","<<s.delay<<","<<s.enpkt<<"\n";
    }
    avg.pdr/=NUM_TRIALS; avg.lifetime/=NUM_TRIALS; avg.delay/=NUM_TRIALS; avg.enpkt/=NUM_TRIALS;
    std::cout<<"\n=== RESULTS ("<<nNodes<<" nodes, "<<NUM_TRIALS<<" trials) ===\n";
    std::cout<<"PDR          : "<<avg.pdr<<" %\n";
    std::cout<<"Lifetime     : "<<avg.lifetime<<" s\n";
    std::cout<<"Avg Delay    : "<<avg.delay<<" ms\n";
    std::cout<<"Energy/Pkt   : "<<avg.enpkt<<" mJ\n";
    std::cout<<"Results saved to adss_results.csv -- NEW CODE v2\n";
    out.close();
    return 0;
}
