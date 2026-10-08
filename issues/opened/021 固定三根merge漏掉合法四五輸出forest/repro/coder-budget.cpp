#include "tree.hpp"
#include <cassert>
#include <iostream>
int main() {
 std::vector<cbi::Problem> cases{
 {{2,2},{4,4},{{"T0",4,5,1}},100,{2,2},{{1,1},{1,3},{2,1},{0,2},{3,1},{3,3},{2,3},{4,2}}},
 {{2,2},{4,4},{{"T0",5,8,1}},100,{2,2},{{1,1},{1,3},{2,1},{0,1},{0,3},{3,1},{3,3},{2,3},{4,1},{4,3}}},
 {{3,3},{5,5},{{"T0",4,7,1}},100,{2,2},{{1,1},{1,3},{1,4},{2,1},{3,1},{4,1},{4,2},{4,3},{2,4},{3,3},{3,4},{4,4}}}
 };
 for(std::size_t i=0;i<cases.size();++i) {
  auto const& p=cases[i];std::size_t trials=65536;
  assert(!cbi::small_forest(p,trials,true));
  trials=65536;auto tree=cbi::small_forest(p,trials,true,true);
  assert(tree && tree->size()<=24 && cbi::validate_tree(p,*tree).score<=(i==2?203:102));
  std::cout<<i<<" trials "<<65536-trials<<'\n';
  for(std::size_t limit : {0U,1U,100U}) {
   trials=limit;tree=cbi::small_forest(p,trials,true,true);
   assert(trials<=limit);if(tree)cbi::validate_tree(p,*tree);
  }
 }
 auto p=cases[0];p.buffer_library[0].fanout=1000000000000000000LL;
 std::size_t trials=65536;auto tree=cbi::small_forest(p,trials,true,true);
 assert(tree && cbi::validate_tree(p,*tree).score<=102);
 p=cases[0];p.buffer_library={{"T0",2,100,1},{"T1",4,5,5}};
 trials=65536;tree=cbi::small_forest(p,trials,true,true);
 assert(tree && cbi::validate_tree(p,*tree).score<=110);
 p=cases[0];p.buffer_library[0].length=4;
 trials=65536;assert(!cbi::small_forest(p,trials,true,true));
 p=cases[0];trials=65536;assert(cbi::small_forest(p,trials,true,true));
 p.sinks.resize(13);trials=65536;assert(!cbi::small_forest(p,trials,true,true) && trials==65536);
 p=cases[0];p.buffer_library.resize(65,p.buffer_library[0]);
 assert(!cbi::small_forest(p,trials,true,true) && trials==65536);
}
