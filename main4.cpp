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
#include <limits>

#include "alias.h"

#define AIGSIZE Abc_NtkNodeNum(Abc_FrameReadNtk(pAbc))
#define GIASIZE Gia_ManAndNum(Abc_FrameReadGia(pAbc))

using namespace std;

void prepare_frontiers(vector<string> const &moves, vector<Abc_Ntk_t *> &frontiers, int depth) {
  Abc_Frame_t * pAbc = Abc_FrameGetGlobalFrame();
  Abc_Ntk_t * pNtk = Abc_NtkDup(Abc_FrameReadNtk(pAbc));
  if(depth == 0) {
    frontiers.push_back(pNtk);
    return;
  }
  for(string m: moves) {
    Cmd_CommandExecute(pAbc, m.c_str());
    prepare_frontiers(moves, frontiers, depth - 1);
    Abc_FrameReplaceCurrentNetwork(pAbc, Abc_NtkDup(pNtk));
  }
  Abc_NtkDelete(pNtk);
}

int main(int argc, char **argv) {
  // argparse
  argparse::ArgumentParser ap("opt");
  ap.add_argument("input");
  ap.add_argument("-o", "--output");
  //ap.add_argument("-s", "--stages").default_value(20).scan<'i', int>(); // number of moves to decide
  ap.add_argument("-d", "--depth").default_value(1).scan<'i', int>(); // length of each sample (excluding the target move itself)
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
  //int nStages = ap.get<int>("-s");
  int nDepth = ap.get<int>("-d");
  int nNoImprovement = ap.get<int>("-m");
  int fVerbose = ap.get<bool>("-v");

  // abc init
  Abc_Start();
  Abc_Frame_t * pAbc = Abc_FrameGetGlobalFrame();
  
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
    //"rs -l",
    //"rs -N 2 -l",
    "rs -K 6 -l",
    "rs -K 6 -N 2 -l",
    "rs -K 8 -l",
    "rs -K 8 -N 2 -l",
    "rs -K 10 -l",
    "rs -K 10 -N 2 -l",
    "rs -K 12 -l",
    "rs -K 12 -N 2 -l",
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
  if(fVerbose) {
    cout << "init: " << AIGSIZE << endl;
  }

  // optimize
  auto start = chrono::steady_clock::now();
  // prepare frontiers
  vector<Abc_Ntk_t *> frontiers;
  prepare_frontiers(moves, frontiers, nDepth);
  int nFrontiers = frontiers.size();
  // iterate
  Abc_Ntk_t * pBest = Abc_NtkDup(Abc_FrameReadNtk(pAbc));
  int n = AIGSIZE;
  int itr = 0;
  int itr_end = itr + nNoImprovement;
  for(; itr < itr_end; itr++) {
    vector<vector<float>> scores(nMoves);
    vector<vector<Abc_Ntk_t *>> next_frontiers(nMoves);
    int i = 0;
    if(fVerbose) {
      cout << frontiers.size() << endl;
    }
    for(Abc_Ntk_t * pNtk: frontiers) {
      for(string m: moves) {
        Abc_FrameReplaceCurrentNetwork(pAbc, Abc_NtkDup(pNtk));
        Cmd_CommandExecute(pAbc, m.c_str());
        if(n > AIGSIZE) {
          Abc_NtkDelete(pBest);
          pBest = Abc_NtkDup(Abc_FrameReadNtk(pAbc));
          n = AIGSIZE;
          itr_end = itr + nNoImprovement + 1;
        }
        int bin = i / nFrontiers;
        scores[bin].push_back(-(float)AIGSIZE); // smaller AIG gets higher score
        next_frontiers[bin].push_back(Abc_NtkDup(Abc_FrameReadNtk(pAbc)));
        i++;
      }
    }
    if(fVerbose) {
      for(int i = 0; i < nMoves; i++) {
        cout << "move " << i << ": ";
        for(int w = 0; w < nFrontiers; w++) {
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
        for(int w = 0; w < nFrontiers; w++) {
          score += scores[i][w];
        }
        score /= nFrontiers;
        if(best_score < score) {
          best_score = score;
          selected_move = i;
        }
      }
    }
    if(fVerbose) {
      cout << "selecting " << moves[selected_move] << endl;
    }
    frontiers.swap(next_frontiers[selected_move]);
    for(auto & fs: next_frontiers) {
      for(auto & p: fs) {
        Abc_NtkDelete(p);
      }
    }
  }
  
  // end
  auto end = chrono::steady_clock::now();
  std::chrono::duration<double> elapsed_seconds = end - start;
  if(fVerbose) {
    cout << "elapsed_seconds " << elapsed_seconds.count() << endl;
    cout << "size " << n << endl;
  }
  for(auto & p: frontiers) {
    Abc_NtkDelete(p);
  }

  // write
  if(auto poutput = ap.present("-o")) {
    Abc_FrameReplaceCurrentNetwork(pAbc, pBest);
    string str = *poutput;
    char * cstr = new char[10 + str.length() + 1];
    strcpy(cstr, "write ");
    strcat(cstr, str.c_str());
    Cmd_CommandExecute(pAbc, cstr);
    delete [] cstr;
  } else {
    Abc_NtkDelete(pBest);
  }

  // finish
  Abc_Stop();

  std::cout << "#itration: " << itr << endl;
  
  return 0;
}
