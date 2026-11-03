#include "../Source/Engine.h"
#include <cstdio>
#include <fstream>
using namespace hl;
int main(){ Engine e; const char* ex[] = {"Struck plate","Noise burst","Glass drone","Wood ticks"};
  for(int i=0;i<4;i++){ e.loadBuiltin(i, ex[i], true); std::ifstream f("/tmp/parity/s"+std::to_string(i)+".f32", std::ios::binary); f.seekg(0,std::ios::end); size_t n=f.tellg()/4; f.seekg(0); std::vector<float> J(n); f.read((char*)J.data(), n*4);
    double md=0; int first=-1; for(size_t k=0;k<std::min(n,e.slots[i].data.size());k++){ double d=std::abs(J[k]-e.slots[i].data[k]); if(d>md) md=d; if(d>1e-6&&first<0) first=k; }
    printf("%s js %zu c %zu maxdiff %.2e first %d\n", ex[i], n, e.slots[i].data.size(), md, first); } }
