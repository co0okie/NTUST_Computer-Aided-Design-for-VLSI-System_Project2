#include "tree.hpp"
#include <cassert>
#include <iostream>
int main() {
 cbi::Problem p{{2,2},{4,4},{{"T0",3,4,1}},100,{2,2},{{1,1},{1,3},{2,1},{3,1},{3,3},{2,3}}};
 std::size_t budget=65536;
 assert(!cbi::small_forest(p,budget)); // Only two free positions: binary cannot fit.
 budget=65536;auto tree=cbi::small_forest(p,budget,true);
 assert(tree && cbi::validate_tree(p,*tree).score==102 && tree->size()<=24);
 std::cout<<"six sinks trials "<<65536-budget<<'\n';
 for(std::size_t limit : {0U,1U,100U}) {
  budget=limit;auto trial=cbi::small_forest(p,budget,true);
  assert(budget<=limit);if(trial)cbi::validate_tree(p,*trial);
 }
 auto bad=p;bad.buffer_library[0].fanout=2;
 budget=65536;assert(!cbi::small_forest(bad,budget,true));
 bad=p;bad.buffer_library[0].length=3;
 budget=65536;assert(!cbi::small_forest(bad,budget,true));
 p={{3,4},{5,5},{{"T0",3,6,1}},100,{1,1},{{3,1},{4,1},{3,2},{1,3},{1,4},{2,3},{3,3},{4,2},{2,4}}};
 budget=65536;tree=cbi::small_forest(p,budget,true);
 assert(tree && cbi::validate_tree(p,*tree).score<=203 && tree->size()<=24);
 std::cout<<"nine sinks trials "<<65536-budget<<'\n';
 bad=p;bad.sinks.resize(13);budget=65536;
 assert(!cbi::small_forest(bad,budget,true) && budget==65536);
 bad=p;bad.buffer_library.resize(65,p.buffer_library[0]);
 assert(!cbi::small_forest(bad,budget,true) && budget==65536);
 budget=65536;assert(cbi::small_forest(p,budget,true));
}
