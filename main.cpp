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
#include <string>
#include <random>

#include "alias.h"

#define AIGSIZE Abc_NtkNodeNum(Abc_FrameReadNtk(pAbc))
#define GIASIZE Gia_ManAndNum(Abc_FrameReadGia(pAbc))

using namespace std;

std::string DeepSynOne(int i) {
  int fUseTwo = 0;
  unsigned Rand = Abc_Random(0);
  int fDch = Rand & 1;
  //int fCom = (Rand >> 1) & 3;
  //int fCom = (Rand >> 1) & 1;
  int fFx  = (Rand >> 2) & 1;
  int KLut = fUseTwo ? 2 + (i % 5) : 3 + (i % 4);
  std::string Command;
  Command += "&dch";
  Command += fDch ? " -f" : "";
  Command += "; ";
  Command += "&if -a -K ";
  Command += std::to_string(KLut);
  Command += "; ";
  Command += "&mfs -e -W 20 -L 20; ";
  Command += fFx ? "&fx; &st" : "";
  return Command;
}

int main(int argc, char **argv) {
  // argparse
  argparse::ArgumentParser ap("opt");
  ap.add_argument("input");
  ap.add_argument("-o", "--output");
  ap.add_argument("-m", "--max_noimp").default_value(10).scan<'i', int>();
  ap.add_argument("-r", "--random_seed").default_value(0).scan<'i', int>();
  try {
    ap.parse_args(argc, argv);
  }
  catch (const runtime_error& err) {
    cerr << err.what() << endl;
    cerr << ap;
    return 1;
  }

  // parameters
  int max_noimp = ap.get<int>("-m");
  int random_seed = ap.get<int>("-m");
  std::mt19937 rng(random_seed);

  // abc init
  Abc_Start();
  Abc_Frame_t * pAbc = Abc_FrameGetGlobalFrame();
  Abc_Random(1);
  for(int s = 0; s < 10 + random_seed; s++) {
    Abc_Random(0);
  }
  
  // setup alias
  for(char * cmd: alias) {
    Cmd_CommandExecute(pAbc, cmd);
  }
  //vector<string> abccmds = {"resyn;", "resyn2;", "resyn2a;", "resyn3;", "compress;", "compress2;", "resyn2rs;", "compress2rs;", "resub –l -N 2 -K 16;", "iresyn –l;", "&get;&fraig –x;&put;"};

  // read aig
  {
    string str = ap.get<string>("input");
    char * cstr = new char[6 + str.length() + 1];
    strcpy(cstr, "&read ");
    strcat(cstr, str.c_str());
    Cmd_CommandExecute(pAbc, cstr);
    delete [] cstr;
  }
  cout << "init: " << GIASIZE << endl;

  // optimize
  auto start = chrono::steady_clock::now();
  Gia_Man_t * pGia = Gia_ManDup(Abc_FrameReadGia(pAbc));
  int n = GIASIZE;
  //int n = AIGSIZE;
  int itr = 0;
  int itr_end = itr + max_noimp;
  while(itr < itr_end) {
    // ifmfs
    std::string Command = DeepSynOne(itr);
    cout << Command << endl;
    Cmd_CommandExecute(pAbc, Command.c_str());
    cout << "ifmfs: " << GIASIZE << endl;

    if(rng() & 1) {
      Cmd_CommandExecute(pAbc, "&put");
      int m = GIASIZE;
      Cmd_CommandExecute(pAbc, "compress2rs");
      cout << "c2rs: " << setw(5) << AIGSIZE << endl;
      while(m > AIGSIZE) {
        m = AIGSIZE;
        Cmd_CommandExecute(pAbc, "compress2rs");
        cout << "c2rs: " << setw(5) << AIGSIZE << endl;
      }
      Cmd_CommandExecute(pAbc, "&get");
    } else {
      int m = ABC_INT_MAX;
      while(m > GIASIZE) {
        m = GIASIZE;
        Cmd_CommandExecute(pAbc, "&dc2");
        cout << "dc2: " << setw(5) <<  GIASIZE << endl;
      }
    }
    //Cmd_CommandExecute(pAbc, "compress2rs");
    //Cmd_CommandExecute(pAbc, "orchestrate -K 12 -N 2 -l");
    //Cmd_CommandExecute(pAbc, "orchestrate -K 12 -N 2 -l; balance");
    //Cmd_CommandExecute(pAbc, "if -K 6; mfs2; fx; st; compress2rs");
    
    itr++;
    if(n > GIASIZE) {
      n = GIASIZE;
      itr_end = itr + max_noimp;
      Gia_ManStop(pGia);
      pGia = Gia_ManDup(Abc_FrameReadGia(pAbc));
    }
    cout << itr << " " << Abc_NtkNodeNum(Abc_FrameReadNtk(pAbc)) << endl;
    /*
    if(n > Abc_NtkNodeNum(Abc_FrameReadNtk(pAbc))) {
      n = Abc_NtkNodeNum(Abc_FrameReadNtk(pAbc));
      itr_end = itr + max_noimp;
      //pNtkRes = Abc_NtkDup( pNtk );
    }
    */
  }
  cout << endl;
    
  auto end = chrono::steady_clock::now();
  std::chrono::duration<double> elapsed_seconds = end - start;
  cout << "elapsed_seconds " << elapsed_seconds.count() << endl;
  cout << "iteration " << itr << endl;
  cout << "size " << n << endl;

  // write
  if(auto poutput = ap.present("-o")) {
    Abc_FrameUpdateGia(pAbc, pGia);
    string str = *poutput;
    char * cstr = new char[7 + str.length() + 1];
    strcpy(cstr, "&write ");
    strcat(cstr, str.c_str());
    Cmd_CommandExecute(pAbc, cstr);
    delete [] cstr;
  }

  // finish
  Abc_Stop();
  
  return 0;
}
