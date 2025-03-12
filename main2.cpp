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
  ap.add_argument("-s", "--stages").default_value(20).scan<'i', int>(); // number of moves to decide
  ap.add_argument("-d", "--depth").default_value(5).scan<'i', int>(); // length of each sample (excluding the target move itself)
  ap.add_argument("-w", "--width").default_value(10).scan<'i', int>(); // number of samples for each move
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
  int nStages = ap.get<int>("-s");
  int nDepth = ap.get<int>("-d");
  int nWidth = ap.get<int>("-w");
  int random_seed = ap.get<int>("-r");
  int fVerbose = ap.get<bool>("-v");
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
  vector<string> moves = {
    "b -l",
    "rw -l",
    "rwz -l",
    "rf -l",
    "rfz -l",
    "rs -l",
    "rs -N 2 -l",
  /*
    "rs -K 6 -l",
    "rs -K 6 -N 2 -l",
    "rs -K 8 -l",
    "rs -K 8 -N 2 -l",
    "rs -K 10 -l",
    "rs -K 10 -N 2 -l",
    "rs -K 12 -l",
    "rs -K 12 -N 2 -l",
  */
    "orchestrate -l"
  };
  int nMoves = moves.size();
  //vector<string> abccmds = {"resyn;", "resyn2;", "resyn2a;", "resyn3;", "compress;", "compress2;", "resyn2rs;", "compress2rs;", "resub –l -N 2 -K 16;", "iresyn –l;", "&get;&fraig –x;&put;"};

  // read aig
  {
    string str = ap.get<string>("input");
    char * cstr = new char[10 + str.length() + 1];
    strcpy(cstr, "read ");
    strcat(cstr, str.c_str());
    Cmd_CommandExecute(pAbc, cstr);
    delete [] cstr;
  }
  cout << "init: " << AIGSIZE << endl;

  // optimize
  auto start = chrono::steady_clock::now();
  Abc_Ntk_t * pNtk = Abc_NtkDup(Abc_FrameReadNtk(pAbc));
  Abc_Ntk_t * pBest = Abc_NtkDup(Abc_FrameReadNtk(pAbc));
  string selected_commands;
  string best_command;
  int n = AIGSIZE;
  for(int s = 0; s < nStages; s++) {
    // generate sample
    vector<string> samples;
    for(int w = 0; w < nWidth; w++) {
      string sample;
      for(int d = 0; d < nDepth; d++) {
        sample += ";";
        sample += moves[rng() % nMoves];
      }
      samples.push_back(sample);
    }
    // execute samples
    std::vector<std::vector<float>> scores(nMoves); // for each move, for each sample
    for(int i = 0; i < nMoves; i++) {
      for(int w = 0; w < nWidth; w++) {
        Abc_FrameReplaceCurrentNetwork(pAbc, Abc_NtkDup(pNtk));
        string command = moves[i];
        command += samples[w];
        if(fVerbose) {
          cout << "\texecuting " << command << endl;
        }
        Cmd_CommandExecute(pAbc, command.c_str());
        scores[i].push_back(-(float)AIGSIZE); // smaller AIG gets higher score
        if(n > AIGSIZE) {
          Abc_NtkDelete(pBest);
          pBest = Abc_NtkDup(Abc_FrameReadNtk(pAbc));
          n = AIGSIZE;
          best_command = command;
        }
      }
    }
    if(fVerbose) {
      for(int i = 0; i < nMoves; i++) {
        cout << "move " << i << ": ";
        for(int w = 0; w < nWidth; w++) {
          cout << scores[i][w] << ",";
        }
        cout << endl;
      }
    }
    // decide one
    int selected_move = -1;
    // based on average
    {
      float best_score = numeric_limits<float>::lowest();
      for(int i = 0; i < nMoves; i++) {
        float score = 0;
        for(int w = 0; w < nWidth; w++) {
          score += scores[i][w];
        }
        score /= nWidth;
        if(best_score < score) {
          best_score = score;
          selected_move = i;
        }
      }
    }
    // apply selected move
    Abc_FrameReplaceCurrentNetwork(pAbc, pNtk);
    string command = moves[selected_move];
    Cmd_CommandExecute(pAbc, command.c_str());
    pNtk = Abc_NtkDup(Abc_FrameReadNtk(pAbc));
    selected_commands += moves[selected_move];
    if(fVerbose) {
      cout << "selecting " << moves[selected_move] << endl;
    }
  }

  // end
  auto end = chrono::steady_clock::now();
  std::chrono::duration<double> elapsed_seconds = end - start;
  cout << "elapsed_seconds " << elapsed_seconds.count() << endl;
  cout << "size " << n << endl;
  Abc_NtkDelete(pNtk);

  // write
  if(auto poutput = ap.present("-o")) {
    Abc_FrameReplaceCurrentNetwork(pAbc, pBest);
    string str = *poutput;
    char * cstr = new char[10 + str.length() + 1];
    strcpy(cstr, "write ");
    strcat(cstr, str.c_str());
    Cmd_CommandExecute(pAbc, cstr);
    delete [] cstr;
  }

  // finish
  Abc_Stop();
  
  return 0;
}
