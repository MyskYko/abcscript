#include <iostream>
#include <iomanip>

#include <base/abc/abc.h>
#include <aig/aig/aig.h>
#include <opt/dar/dar.h>
#include <aig/gia/gia.h>
#include <aig/gia/giaAig.h>
#include <base/main/main.h>
#include <base/main/mainInt.h>
#include <map/mio/mio.h>

#include <argparse/argparse.hpp>

#include <chrono>
#include <string>
#include <random>
#include <cmath>

#include "alias.h"

#define AIGSIZE Abc_NtkNodeNum(Abc_FrameReadNtk(pAbc))
#define GIASIZE Gia_ManAndNum(Abc_FrameReadGia(pAbc))

#define N 59
// largest N where pbinom(0, N, 0.05) becomes less than p-value=0.05: pbinom(0, 59, 0.05)=0.04849
// using null hypothesis that increasing max_no_imp is meaningful at a probability geq 5%
// in the worst case (5%),  probability of getting 0 meaningful runs among N is pbinom(0, N, 0.05)
// when that happens, we are observing something with pbinom(0, N, 0.05) probability
// we reject the null hypothesis if we observe something with a probability less than p-value
// so, with N=59, we can reject the null hypothesis when 0 runs among N improved with extra iteration

// N=299 if geq 1% probablity with p-value=0.05


#define T 1.645
// sample can deviate from mean by T * (std_dev / sqrt(nSamples)) with p-value probability
// number above is assuming number of samples is large enough, looking one-side, and using p-value=0.05
// if we already have the result less than that, I think we can stop
// 1.960 for p-value=0.025
// 2.326 for p-value=0.01

using namespace std;

class Sampler {
  unsigned int m_z;
  unsigned int m_w;
  bool fVerbose;
  Gia_Man_t *pGia;
  Gia_Man_t *pBest;
  int itr;
  int last_imp;
  int n;

  int runid;

  unsigned rng() {
    m_z = 36969 * (m_z & 65535) + (m_z >> 16);
    m_w = 18000 * (m_w & 65535) + (m_w >> 16);
    return (m_z << 16) + m_w;
  }

  void reset() {
    m_z = 3716960521u;
    m_w = 2174103536u;
    rng(); // abc does this
  }

  string DeepSynOne(int i, int &fCom) {
    int fUseTwo = 0;
    unsigned Rand = rng();
    //cout << Rand << endl;
    int fDch = Rand & 1;
    //int fCom = (Rand >> 1) & 3;
    fCom = (Rand >> 1) & 1;
    int fFx  = (Rand >> 2) & 1;
    int KLut = fUseTwo ? 2 + (i % 5) : 3 + (i % 4);
    string Command;
    Command += "&dch";
    Command += fDch ? " -f" : "";
    Command += "; ";
    Command += "&if -a -K ";
    Command += to_string(KLut);
    Command += "; ";
    Command += "&mfs -e -W 20 -L 20; ";
    Command += fFx ? "&fx; &st" : "";
    return Command;
  }
  
public:
  Sampler(Gia_Man_t *pGia, int random_seed) :
    fVerbose(true),
    pGia(Gia_ManDup(pGia)),
    pBest(Gia_ManDup(pGia)),
    itr(-1),
    last_imp(-1) {
    n = Gia_ManAndNum(pGia);
    reset();
    for(int s = 0; s < 10 + random_seed; s++) {
      rng();
    }
    runid = random_seed;
  }
  ~Sampler() {
    Gia_ManStop(pGia);
    Gia_ManStop(pBest);
  }

  bool Run(int max_no_imp) {
    Abc_Frame_t * pAbc = Abc_FrameGetGlobalFrame();
    Abc_FrameUpdateGia(pAbc, pGia);
    bool fImproved = false;
    while(itr - last_imp <= max_no_imp) {
      itr++;
      // ifmfs
      int fCom;
      string Command = DeepSynOne(itr, fCom);
      if(fVerbose) {
        //cout << Command << endl;
      }
      Cmd_CommandExecute(pAbc, Command.c_str());
      if(fCom) {
        if(fVerbose) {
          //cout << "c2rs" << endl;
        }
        Cmd_CommandExecute(pAbc, "&put; compress2rs; &get");
      } else {
        if(fVerbose) {
          //cout << "&dc2" << endl;
        }
        Cmd_CommandExecute(pAbc, "&dc2");
      }
      if(fVerbose) {
        cout << setw(2) << runid << ": itr = " << setw(5) << itr << ", size = " << setw(5) << GIASIZE << endl;
      }
      if(n > GIASIZE) {
        n = GIASIZE;
        last_imp = itr;
        Gia_ManStop(pBest);
        pBest = Gia_ManDup(Abc_FrameReadGia(pAbc));
        fImproved = true;
      }
    }
    pGia = Gia_ManDup(Abc_FrameReadGia(pAbc));
    return fImproved;
  }

  int getn() {
    return n;
  }

  Gia_Man_t *getbest() {
    return Gia_ManDup(pBest);
  }
};


int main(int argc, char **argv) {
  // argparse
  argparse::ArgumentParser ap("opt");
  ap.add_argument("input");
  ap.add_argument("-o", "--output");
  ap.add_argument("-m", "--max_noimp").default_value(100).scan<'i', int>();
  ap.add_argument("-r", "--random_seed").default_value(0).scan<'i', int>();
  ap.add_argument("-v", "--verbose").default_value(false).implicit_value(true);
  try {
    ap.parse_args(argc, argv);
  }
  catch (const runtime_error& err) {
    cerr << err.what() << endl;
    cerr << ap;
    return 1;
  }

  // parameters
  // int max_noimp = ap.get<int>("-m");
  // int random_seed = ap.get<int>("-r");
  //std::mt19937 rng(random_seed);
  int fVerbose = ap.get<bool>("-v");

  // abc init
  Abc_Start();
  Abc_Frame_t * pAbc = Abc_FrameGetGlobalFrame();
  
  // setup alias
  for(char * cmd: alias) {
    Cmd_CommandExecute(pAbc, cmd);
  }

  // read aig
  {
    string str = ap.get<string>("input");
    char * cstr = new char[6 + str.length() + 1];
    strcpy(cstr, "&read ");
    strcat(cstr, str.c_str());
    Cmd_CommandExecute(pAbc, cstr);
    delete [] cstr;
  }
  if(fVerbose) {
    cout << "init: " << GIASIZE << endl;
  }

  // optimize
  auto start = chrono::steady_clock::now();
  Gia_Man_t * pGia = Gia_ManDup(Abc_FrameReadGia(pAbc));
  Sampler **vSamplers = new Sampler*[N];
  for(int i = 0; i < N; i++) {
    vSamplers[i] = new Sampler(pGia, i);
  }

  int max_no_imp = -1;
  int fImproved = 1;
  while(fImproved) {
    max_no_imp++;
    fImproved = 0;
    for(int i = 0; i < N; i++) {
      fImproved += vSamplers[i]->Run(max_no_imp);
    }
    cout << "*** improved " << fImproved << " / " << N << " with max_no_imp = " << max_no_imp << " ***" << endl;
  }

  cout << "max no imp = " << max_no_imp << endl;

  Gia_Man_t *pBest = Gia_ManDup(pGia);

  int minn = 10000000;
  for(int i = 0; i < N; i++) {
    if(minn > vSamplers[i]->getn()) {
      minn = vSamplers[i]->getn();
      Gia_ManStop(pBest);
      pBest = vSamplers[i]->getbest();
    }
  }
  cout << "minn = " << minn << endl;

  // next step is increase sample size, while we don't need to maintain previous ones any more, we only need ns
  std::vector<int> ns;
  for(int i = 0; i < N; i++) {
    ns.push_back(vSamplers[i]->getn());
  }
  
  for(int i = 0; i < N; i++) {
    delete vSamplers[i];
  }
  delete[] vSamplers;

  // let's prepare
  int sumn = 0;
  for(int n: ns) {
    sumn += n;
  }
  int M = N;

  // loop until current min is less than left of confidence interval
  while(true) {
    float meann = (float)sumn / M;
    float varin = 0;
    for(int n: ns) {
      float d = (float)n - meann;
      varin = varin + d * d;
    }
    varin = varin / (M - 1); // Bessel's correction
    float stddevn = sqrt(varin);
    float left = meann - T * stddevn / sqrt(M);
    cout << "M = " << M << ", sum = " << sumn << ", mean = " << meann << ", vari = " << varin << ", stddev = " << stddevn << ", left = " << left << ", min = " << minn << endl;
    if((float)minn < left) {
      break;
    }
    Sampler s(pGia, M);
    s.Run(max_no_imp);
    int n = s.getn();
    cout << "new result " << n << endl;
    ns.push_back(n);
    sumn += n;
    if(minn > n) {
      minn = n;
      Gia_ManStop(pBest);
      pBest = s.getbest();
    }
    M++;
  }

  auto end = chrono::steady_clock::now();
  chrono::duration<double> elapsed_seconds = end - start;
  if(fVerbose) {
    cout << "elapsed_seconds " << elapsed_seconds.count() << endl;
    cout << "size " << minn << endl;
  }
  cout << "#max_no_imp: " << max_no_imp << endl;
  cout << "#sample: " << M << endl;

  // write
  if(auto poutput = ap.present("-o")) {
    Abc_FrameUpdateGia(pAbc, Gia_ManDup(pBest));
    string str = *poutput;
    char * cstr = new char[7 + str.length() + 1];
    strcpy(cstr, "&write ");
    strcat(cstr, str.c_str());
    Cmd_CommandExecute(pAbc, cstr);
    delete [] cstr;
  }

  Gia_ManStop(pGia);
  Gia_ManStop(pBest);
  
  // finish
  Abc_Stop();
  
  return 0;
}
