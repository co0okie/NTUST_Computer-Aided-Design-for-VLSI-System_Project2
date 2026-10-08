#include "tree.hpp"
#include <cassert>
#include <iostream>
int main() {
 cbi::Problem p{{3,78},{40,40},{{"T0",4,84,1}},9,{12,28},{{34,20},{24,13},{38,19},{20,1},{9,35},{37,29},{1,12},{15,26},{34,18},{26,35}}};
 auto original=cbi::initial_tree(p,false);
 original.push_back({{9,5},0,{7,4}});
 original.push_back({{23,31},0,{2,13,5}});
 original.push_back({{28,29},0,{10,6}});
 original.push_back({{29,27},0,{9,3,1,8}});
 original.push_back({{15,20},0,{11,12,14}});
 original[0].children={15};assert(cbi::validate_tree(p,original).score==50);
 for(std::size_t cap : {0U,1U,100U}) {
  std::size_t trials=cap,insertions=16;
  auto result=cbi::final_delay(p,original,&trials,&insertions);
  assert(trials<=cap && insertions<=16 && cbi::validate_tree(p,result).score<=50);
  if(cap==0)assert(!cbi::tree_less(result,original) && !cbi::tree_less(original,result) && insertions==16);
 }
 std::size_t trials=5000,insertions=100;
 auto result=cbi::final_delay(p,original,&trials,&insertions);
 assert(cbi::validate_tree(p,result).score<=34 && 5000-trials<=2048 && 100-insertions<=16);
 std::cout<<"Score "<<cbi::validate_tree(p,result).score<<", trials "<<5000-trials<<", insertions "<<100-insertions<<'\n';
 trials=2048;insertions=0;result=cbi::final_delay(p,original,&trials,&insertions);
 assert(trials==2048 && insertions==0 && cbi::validate_tree(p,result).score==50);
 p.buffer_library[0].cost=100;trials=2048;insertions=16;
 result=cbi::final_delay(p,original,&trials,&insertions);
 assert(insertions==16 && cbi::validate_tree(p,result).score==cbi::validate_tree(p,original).score);
 p.dimensions={1000,1000};p.source={0,0};p.source_limits={1,1000};
 p.buffer_library={{"T0",2,1000,1}};p.sinks.clear();
 for(int x=0;x<26;++x)for(int y=0;y<10;++y)p.sinks.push_back({10+10*x,10+10*y});
 original=cbi::Builder(p,false).build();trials=2048;insertions=16;
 result=cbi::final_delay(p,original,&trials,&insertions);
 assert(trials==2048 && insertions==16 && !cbi::tree_less(result,original) && !cbi::tree_less(original,result));
 cbi::validate_tree(p,result);
}
