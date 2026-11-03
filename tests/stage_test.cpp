#define private public
#include "../Source/Engine.h"
#undef private
#include <cstdio>
#include <fstream>
using namespace hl;
int main(){ Engine e; e.setRate(48000); e.P.chaosTurb=0; e.P.noiseOn=false;
  std::ifstream f("/tmp/parity/st.txt"); Genome g; for(auto& h:g.hidden) f>>h; Layer L; L.key=KClick; L.kind=Kind::Click; L.role=Role::Transient;
  f>>L.seed>>L.pos>>L.spread>>L.grain>>L.dens>>L.count>>L.pitch>>L.pjit>>L.rev>>L.pan>>L.width>>L.gain>>L.start>>L.decay>>L.curve>>L.bright>>L.reso>>L.f0;
  const int N = (int)std::ceil(e.P.maxLen*48000); e.trk.on=false; Rng r(L.seed); Engine::Mods m;
  std::vector<float> A(N), B(N); e.fillGenerator(A,B,L,r,g,m); std::vector<float> a(A.begin(),A.begin()+4000); e.applyLPG(A,B,L,r,g,m);
  auto rd=[](const char* p){ std::ifstream f(p,std::ios::binary); std::vector<float> v(4000); f.read((char*)v.data(),16000); return v; };
  auto ja=rd("/tmp/parity/stA.f32"), jb=rd("/tmp/parity/stB.f32");
  double d1=0,d2=0; int f2=-1; for(int i=0;i<4000;i++){ d1=std::max(d1,(double)std::abs(ja[i]-a[i])); double d=std::abs(jb[i]-A[i]); if(d>d2) d2=d; if(d>1e-7&&f2<0) f2=i; }
  printf("gen diff %.2e   lpg diff %.2e first %d\n", d1, d2, f2);
  for(int i=f2-2;i<f2+6 && f2>=0;i++) printf("%d js %.9g c %.9g\n", i, jb[i], A[i]);
}
