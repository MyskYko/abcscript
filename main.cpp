#include <iostream>

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

#include "alias.h"

using namespace std;

string abcopt = "&dc2";
int tseed;
bool fCspf;
Gia_Man_t * opt1(Gia_Man_t * pOld) {
  Abc_Frame_t * pAbc = Abc_FrameGetGlobalFrame();
  Gia_Man_t * pGia = Gia_ManDup(pOld);
  int n;
  do {
    n = Gia_ManAndNum(pGia);
    Gia_Man_t * pNew = Gia_ManTransduction(pGia, 6, !fCspf, tseed++, 0, 0, 0, 0);
    Gia_ManStop(pGia);
    pAbc->pGia = pNew;    
    Cmd_CommandExecute(pAbc, abcopt.c_str());
    pGia = pAbc->pGia;
    pAbc->pGia = NULL;
    cout << "\t\t\topt1 " << Gia_ManAndNum(pGia) << endl;
  } while(n > Gia_ManAndNum(pGia));
  return pGia;
}

string abchop = "&put; if; mfs2; st; &get;";
int nhops;
bool resethop;
Gia_Man_t * opt2(Gia_Man_t * pOld) {
  Abc_Frame_t * pAbc = Abc_FrameGetGlobalFrame();
  Gia_Man_t * pGia = Gia_ManDup(pOld);
  int n = Gia_ManAndNum(pGia);
  Gia_Man_t * pBest = Gia_ManDup(pGia);
  for(int i = 0; i <= nhops; i++) {
    Gia_Man_t * pNew = opt1(pGia);
    cout << "\t\topt2 " << Gia_ManAndNum(pNew) << endl;
    Gia_ManStop(pGia);
    if(n > Gia_ManAndNum(pNew)) {
      n = Gia_ManAndNum(pNew);
      Gia_ManStop(pBest);
      pBest = Gia_ManDup(pNew);
      if(resethop) {
        i = 0;
      }
    }
    pAbc->pGia = pNew;
    Cmd_CommandExecute(pAbc, abchop.c_str());
    pGia = pAbc->pGia;
    pAbc->pGia = NULL;
  }
  Gia_ManStop(pGia);
  return pBest;
}

int nrestarts;
int seedbase;
Gia_Man_t * opt3(Gia_Man_t * pOld) {
  int n = Gia_ManAndNum(pOld);
  Gia_Man_t * pBest = Gia_ManDup(pOld);
  for(int i = 0; i <= nrestarts; i++) {
    tseed = 1234 * (i + seedbase);
    Gia_Man_t * pNew = opt2(pOld);
    cout << "\topt3 " << Gia_ManAndNum(pNew) << endl;
    if(n > Gia_ManAndNum(pNew)) {
      n = Gia_ManAndNum(pNew);
      Gia_ManStop(pBest);
      pBest = pNew;
    } else {
      Gia_ManStop(pNew);
    }
  }
  return pBest;
}

int main(int argc, char **argv) {
  argparse::ArgumentParser ap("opt");
  ap.add_argument("input");
  ap.add_argument("output");
  ap.add_argument("-n", "--num_restarts").default_value(10).scan<'i', int>();
  ap.add_argument("-m", "--num_hops").default_value(10).scan<'i', int>();
  ap.add_argument("-r", "--reset_hop").default_value(true).implicit_value(false);
  ap.add_argument("-b", "--seed_base").default_value(0).scan<'i', int>();
  ap.add_argument("-c", "--cspf").default_value(false).implicit_value(true);
  ap.add_argument("-s", "--multi_starts").default_value(true).implicit_value(false);
  ap.add_argument("-t", "--use_original").default_value(true).implicit_value(false);
  ap.add_argument("-v", "--verbose").default_value(false).implicit_value(true);
  try {
    ap.parse_args(argc, argv);
  }
  catch (const runtime_error& err) {
    cerr << err.what() << endl;
    cerr << ap;
    return 1;
  }

  // set argument
  nhops = ap.get<int>("--num_hops");
  nrestarts = ap.get<int>("--num_restarts");
  resethop = ap.get<bool>("--reset_hop");
  seedbase = ap.get<int>("--seed_base");
  fCspf = ap.get<bool>("--cspf");
  //vector<string> abccmds = {"resyn;", "resyn2;", "resyn2a;", "resyn3;", "compress;", "compress2;", "resyn2rs;", "compress2rs;", "resub –l -N 2 -K 16;", "iresyn –l;", "&get;&fraig –x;&put;"};

  // abc init
  auto start = chrono::steady_clock::now();
  Abc_Start();
  Abc_Frame_t * pAbc = Abc_FrameGetGlobalFrame();
  
  // setup alias
  for(char * cmd: alias) {
    Cmd_CommandExecute(pAbc, cmd);
  }

  // read aig
  Gia_Man_t * pGia;
  {
    string str = ap.get<string>("input");
    char * cstr = new char[str.length() + 1];
    strcpy(cstr, str.c_str());
    pGia = Gia_AigerRead(cstr, 0, 0, 1);
    delete [] cstr;
  }

  // setup start points
  vector<Gia_Man_t *> start_points;
  if(ap.get<bool>("--use_original")) {
    start_points.push_back(Gia_ManDup(pGia));
  }
  if(ap.get<bool>("--multi_starts")) {
    {
      pAbc->pGia = Gia_ManDup(pGia);
      Cmd_CommandExecute(pAbc, "&put; collapse; strash; &get");
      start_points.push_back(Gia_ManDup(pAbc->pGia));
      Gia_ManStop(pAbc->pGia);
      pAbc->pGia = NULL;
    }
    {
      Gia_Man_t * pNew = Gia_ManTtopt(pGia, Gia_ManCiNum(pGia), Gia_ManCoNum(pGia), 100);
      start_points.push_back(pNew);
    }
    {
      pAbc->pGia = Gia_ManDup(pGia);
      Cmd_CommandExecute(pAbc, "&put; collapse; sop; fx; strash; &get");
      start_points.push_back(Gia_ManDup(pAbc->pGia));
      Gia_ManStop(pAbc->pGia);
      pAbc->pGia = NULL;
    }
    //Cmd_CommandExecute(pAbc, "&put; collapse; dsd; sop; fx; strash; &get");
  }

  // optimize
  for(auto p: start_points) {
    cout << "init " << Gia_ManAndNum(p) << endl;
    Gia_Man_t * pNew = opt3(p);
    cout << "end " << Gia_ManAndNum(pNew) << endl;
    if(Gia_ManAndNum(pGia) > Gia_ManAndNum(pNew)) {
      Gia_ManStop(pGia);
      pGia = pNew;
    } else {
      Gia_ManStop(pNew);
    }
    Gia_ManStop(p);
  }
  auto end = chrono::steady_clock::now();
  std::chrono::duration<double> elapsed_seconds = end - start;
  cout << "elapsed_seconds " << elapsed_seconds.count() << endl;
  cout << "best " << Gia_ManAndNum(pGia) << endl;

  // write
  {
    string str = ap.get<string>("output");
    char * cstr = new char[str.length() + 1];
    strcpy(cstr, str.c_str());
    Gia_AigerWrite(pGia, cstr, 0, 0, 0);
    delete [] cstr;
  }

  // finish
  Gia_ManStop(pGia);
  Abc_Stop();
  
  return 0;
}
