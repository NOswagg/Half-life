#include "../Source/Engine.h"
#include <cstdio>
#include <chrono>
using namespace hl;
int main(){
  Engine e; e.setRate(48000);
  const char* ex[] = {"Struck plate","Noise burst","Glass drone","Wood ticks"};
  for(int i=0;i<4;i++) e.loadBuiltin(i, ex[i], true);
  auto t0 = std::chrono::steady_clock::now();
  bool ok = e.generate(true);
  auto t1 = std::chrono::steady_clock::now();
  auto o = e.current->out;
  double pk=0, bad=0; for(size_t i=0;i<o->L.size();i++){ pk=std::max(pk,(double)std::abs(o->L[i])); if(!std::isfinite(o->L[i])) bad++; }
  printf("fresh ok=%d len=%.2fs thalf=%.0f layers=%d peak=%.3f bad=%.0f ms=%.0f  %s\n", ok, o->len, o->thalf, o->layers, pk, bad, std::chrono::duration<double,std::milli>(t1-t0).count(), e.status.c_str());
  for(int k=0;k<5;k++){ t0=std::chrono::steady_clock::now(); e.generate(false); t1=std::chrono::steady_clock::now(); printf("mutate len=%.2f thalf=%.0f ms=%.0f %s\n", e.current->out->len, e.current->out->thalf, std::chrono::duration<double,std::milli>(t1-t0).count(), e.status.c_str()); }
  // everything on
  e.P.distType = DistType::Fold; e.P.resType = ResType::Comb; e.P.rot = 0.8; e.P.pitchAmt = 24; e.P.blipOn = true; e.P.target = Target::Wrong;
  t0=std::chrono::steady_clock::now(); e.generate(true); t1=std::chrono::steady_clock::now();
  o = e.current->out; pk=0; bad=0; for(size_t i=0;i<o->L.size();i++){ pk=std::max(pk,(double)std::abs(o->L[i])); if(!std::isfinite(o->L[i])) bad++; }
  printf("all-on len=%.2f peak=%.3f bad=%.0f ms=%.0f %s\n", o->len, pk, bad, std::chrono::duration<double,std::milli>(t1-t0).count(), e.status.c_str());
  // determinism: same genome renders identically
  Genome g = e.current->genome; auto a = e.render(g); auto b = e.render(g);
  bool same = a->L == b->L; printf("deterministic=%d\n", same);
  e.P.distType = DistType::Phase; e.P.resType=ResType::Modal; e.P.resStruct=ResStruct::Random; e.generate(false); printf("phase ok len=%.2f\n", e.current->out->len);
  e.P.distType = DistType::Shape; e.P.shSteps = 5; e.generate(false); printf("shape ok\n");
  e.P.distType = DistType::Sat; e.generate(false); printf("sat ok\n");
  for (auto nt : {NoiseType::White,NoiseType::Brown,NoiseType::Crackle,NoiseType::Velvet}) { e.P.noiseType = nt; e.generate(true); }
  printf("noise types ok\n");
  for (auto& n : Engine::libNames()) { auto d = e.makeBuiltin(n, 48000); double p=0; for(float v: d) p = std::max(p,(double)std::abs(v)); printf("%s %.2fs peak %.2f\n", n.c_str(), d.size()/48000.0, p); }
}
