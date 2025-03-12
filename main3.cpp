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
#include <limits>

#include "alias.h"

#define AIGSIZE Abc_NtkNodeNum(Abc_FrameReadNtk(pAbc))
#define GIASIZE Gia_ManAndNum(Abc_FrameReadGia(pAbc))

using namespace std;

int main(int argc, char **argv) {
  // argparse
  argparse::ArgumentParser ap("opt");
  ap.add_argument("input");
  ap.add_argument("-o", "--output");
  ap.add_argument("-c", "--command").default_value(string{"compress2rs"});
  ap.add_argument("-m", "--no_improvement").default_value(5).scan<'i', int>();
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
  string command = ap.get<string>("-c");
  int nNoImprovement = ap.get<int>("-m");
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
    char * cstr = new char[10 + str.length() + 1];
    strcpy(cstr, "read ");
    strcat(cstr, str.c_str());
    Cmd_CommandExecute(pAbc, cstr);
    delete [] cstr;
  }
  if(fVerbose) {
    cout << "init: " << AIGSIZE << endl;
  }

  // optimize
  auto start = chrono::steady_clock::now();
  int n = AIGSIZE;
  int itr = 0;
  int itr_end = itr + nNoImprovement + 1;
  for(; itr < itr_end; itr++) {
    Cmd_CommandExecute(pAbc, command.c_str());
    if(fVerbose) {
      cout << itr << ": " << AIGSIZE << endl;
    }
    if(n > AIGSIZE) {
      n = AIGSIZE;
      itr_end = itr + nNoImprovement + 2;
    }
  }
  
  // end
  auto end = chrono::steady_clock::now();
  std::chrono::duration<double> elapsed_seconds = end - start;
  if(fVerbose) {
    cout << "elapsed_seconds " << elapsed_seconds.count() << endl;
    cout << "size " << n << endl;
  }
  
  // write
  if(auto poutput = ap.present("-o")) {
    string str = *poutput;
    char * cstr = new char[10 + str.length() + 1];
    strcpy(cstr, "write ");
    strcat(cstr, str.c_str());
    Cmd_CommandExecute(pAbc, cstr);
    delete [] cstr;
  }

  // finish
  Abc_Stop();

  std::cout << "#itration: " << itr << endl;
  
  return 0;
}
