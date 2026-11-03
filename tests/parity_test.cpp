#include "../Source/Engine.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <map>
static std::map<std::string,double> loadP(const std::string& fn){ std::ifstream f(fn); std::string t((std::istreambuf_iterator<char>(f)),{}); std::map<std::string,double> m; size_t i=0;
  while((i=t.find('"',i))!=std::string::npos){ size_t j=t.find('"',i+1); std::string k=t.substr(i+1,j-i-1); size_t c=j+1; if(c<t.size()&&t[c]==':'){ char* e; double v=strtod(t.c_str()+c+1,&e); if(e!=t.c_str()+c+1) m[k]=v; } i=j+1; } return m; }
using namespace hl;
static Circuit rc(std::istream& s){ Circuit c; s>>c.a>>c.sp; for(double& v:c.s) s>>v; for(double& v:c.m) s>>v; for(double& v:c.d) s>>v; return c; }
static std::string strOf(const std::string& fn, const std::string& k){ std::ifstream f(fn); std::string t((std::istreambuf_iterator<char>(f)),{}); auto i=t.find("\""+k+"\":"); if(i==std::string::npos) return ""; i+=k.size()+3; if(t[i]=='"'){ auto j=t.find('"',i+1); return t.substr(i+1,j-i-1);} auto j=t.find_first_of(",}",i); return t.substr(i,j-i); }
int main(int argc, char** argv){ std::string pre = argc>1? argv[1] : ""; int cnt = argc>2? atoi(argv[2]) : 5;
  for(int cs=0; cs<cnt; cs++){
    Engine e; e.setRate(48000);
    const char* ex[] = {"Struck plate","Noise burst","Glass drone","Wood ticks"};
    for(int i=0;i<4;i++) e.loadBuiltin(i, ex[i], true);
    std::string pj = "/tmp/parity/"+(pre.empty()?std::string("P"):pre=="g"?std::string("p"):pre=="X"?std::string("Z"):pre)+std::to_string(cs)+".json";
    { std::ifstream ff(pj); std::string l1,l2; std::getline(ff,l1); std::getline(ff,l2); std::ofstream("/tmp/parity/_g.json")<<l2; }
    auto G=[&](const char* k){ return strOf("/tmp/parity/_g.json",k); }; auto Ps=[&](const char* k){ return strOf(pj,k); };
    e.P.noiseOn = G("noiseOn")=="true"; e.P.clickOn = G("clickOn")=="true"; e.P.blipOn = G("blipOn")=="true";
    { std::string t=G("noiseType"); e.P.noiseType = t=="white"?NoiseType::White:t=="brown"?NoiseType::Brown:t=="crackle"?NoiseType::Crackle:t=="velvet"?NoiseType::Velvet:NoiseType::Pink; }
    { std::string t=G("noiseRole"); e.P.noiseRole = t=="body"?Role::Body:t=="tail"?Role::Tail:t=="transient"?Role::Transient:Role::Texture; }
    { std::string t=Ps("distType"); e.P.distType = t=="sat"?DistType::Sat:t=="shape"?DistType::Shape:t=="fold"?DistType::Fold:t=="phase"?DistType::Phase:DistType::Off; }
    { std::string t=Ps("resType"); e.P.resType = t=="modal"?ResType::Modal:t=="comb"?ResType::Comb:ResType::Off; }
    { std::string t=Ps("resStruct"); e.P.resStruct = t=="harmonic"?ResStruct::Harmonic:t=="bell"?ResStruct::Bell:t=="plate"?ResStruct::Plate:t=="random"?ResStruct::Random:ResStruct::Bar; }
    if(pre.empty()){ auto only=[&](std::vector<int> k){ for(int i=0;i<4;i++) if(std::find(k.begin(),k.end(),i)==k.end()) e.slots[i].clear(); }; if(cs<2) only({}); if(cs==2) only({0}); }
    { auto m=loadP(pj); auto& P=e.P;
      #define S(k) if(m.count(#k)) P.k=m[#k];
      S(instability)S(mutation)S(glide)S(A)S(D)S(S)S(R)S(pitchAmt)S(pitchTime)S(pitchCurve)S(lpgResp)S(reso)S(bright)S(maxLen)S(threshold)S(noiseLevel)S(interact)S(body)S(bodyTime)S(rot)S(rotOnset)S(chaosTurb)S(chaosCouple)
      S(distDrive)S(distMix)S(distTone)S(satAsym)S(satKnee)S(shCurve)S(shH2)S(shH3)S(shH5)S(shSteps)S(fdFolds)S(fdBias)S(fdShape)S(pdDepth)S(pdSmooth)S(pdFb)S(resMix)S(resPitch)S(resRand)S(resAmt)S(resDamp)S(resVoices)S(resSpread)S(scatter)S(shA)S(shD)S(shS)S(shR)S(shFollow) }
    std::ifstream f("/tmp/parity/"+(pre.empty()?std::string("G"):pre)+std::to_string(cs)+".txt"); Genome g; uint32_t seed; f>>seed; g.seed=seed;
    for(auto& h:g.hidden) f>>h;
    for(int k=0;k<4;k++){ auto& s=g.glide[k]; f>>s.val>>s.vals[0]>>s.vals[1]>>s.vals[2]; int n; f>>n; s.routes.resize(n); for(auto& r:s.routes) f>>r.dest>>r.depth>>r.comp; }
    for(auto& c:g.chaos.h) c=rc(f); for(auto& c:g.chaos.sl) c=rc(f);
    for(auto& r:g.chaos.route) f>>r.a>>r.b>>r.w>>r.sa>>r.sb; f>>g.chaos.k;
    int nl; f>>nl; g.layers.resize(nl);
    for(auto& L:g.layers){ int role; f>>L.key>>role>>L.seed>>L.pos>>L.spread>>L.grain>>L.dens>>L.count>>L.pitch>>L.pjit>>L.rev>>L.pan>>L.width>>L.gain>>L.start>>L.decay>>L.curve>>L.bright>>L.reso>>L.f0; L.role=(Role)role; L.kind=kindOf(L.key); }
    int no; f>>no; g.order.resize(no); for(auto& o:g.order) f>>o;
    int nk; f>>nk; g.links.resize(nk); for(auto& l:g.links){ int t; f>>l.from>>l.to>>t>>l.dest>>l.amount; l.type = t? LinkType::Clock:LinkType::Follow; }
    f>>g.resJit; for(auto& r:g.rot) f>>r;
    auto o = e.render(g);
    std::string on = "/tmp/parity/"+(pre.empty()?std::string("O"):pre=="g"?std::string("o"):pre=="X"?std::string("Y"):pre)+std::to_string(cs)+".f32"; std::ifstream bf(on, std::ios::binary); std::vector<float> js((std::istreambuf_iterator<char>(bf)), {}); 
    std::vector<float> J(js.size()/sizeof(float)/1); // reinterpret
    bf.close(); std::ifstream bf2(on, std::ios::binary); bf2.seekg(0,std::ios::end); size_t bytes=bf2.tellg(); bf2.seekg(0); J.resize(bytes/4); bf2.read((char*)J.data(), bytes);
    int nj = (int)J.size()/2; double maxd=0, pk=0; int nn = std::min(nj, o->end);
    for(int i=0;i<nn;i++){ maxd=std::max(maxd,(double)std::abs(J[i]-o->L[i])); maxd=std::max(maxd,(double)std::abs(J[nj+i]-o->R[i])); pk=std::max(pk,(double)std::abs(J[i])); }
    int first=-1; for(int i=0;i<nn;i++) if(std::abs(J[i]-o->L[i])>1e-5){first=i;break;}
    printf("first diff at %d  ",first); for(int i=first;i<first+4 && first>=0;i++) printf("[%d js %.7g c %.7g ratio %.6f] ", i, J[i], o->L[i], J[i]/o->L[i]); printf("\n");
    printf("case %d: js len %d  c++ len %d  max diff %.2e (peak %.3f)  thalf %.0f\n", cs, nj, o->end, maxd, pk, o->thalf);
  }
}
