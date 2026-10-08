#include "tree.hpp"
#include <cassert>
#include <iostream>
int main() {
 cbi::Problem p{{4,16},{30,30},{{"T0",2,17,7},{"T1",2,66,13}},15,{4,17},{{3,1},{21,10},{29,18},{16,19},{14,20},{20,29},{13,12},{29,15}}};
 auto original=cbi::initial_tree(p,false);
 original.push_back({{3,11},1,{1,7}});
 original.push_back({{22,15},0,{2,8}});
 original.push_back({{10,13},1,{9,10}});
 original.push_back({{14,19},0,{5,4}});
 original.push_back({{20,19},1,{3,6}});
 original.push_back({{20,18},0,{12,13}});
 original.push_back({{10,15},0,{11,14}});
 original[0].children={15};
 assert(cbi::validate_tree(p,original).score==112);
 std::size_t budget=0;
 auto zero=cbi::swap_subtrees(p,original,&budget);
 assert(budget==0 && !cbi::tree_less(zero,original) && !cbi::tree_less(original,zero));
 budget=1;auto one=cbi::swap_subtrees(p,original,&budget);
 assert(budget==0 && cbi::validate_tree(p,one).score<=112);
 budget=2048;auto improved=cbi::swap_subtrees(p,original,&budget);
 auto m=cbi::validate_tree(p,improved);
 assert(m.score<=106);
 std::cout<<"Score "<<m.score<<", trials "<<2048-budget<<'\n';
 assert(cbi::validate_tree(p,cbi::swap_leaves(p,original)).score==112);
 // A source-budget rejection and cyclic exchanges cannot replace a legal tree.
 p.source_limits={1,8};budget=2048;
 improved=cbi::swap_subtrees(p,original,&budget);
 assert(cbi::validate_tree(p,improved).score<=112);
 p.dimensions={1000,1000};p.source={0,0};p.source_limits={1,1000};
 p.buffer_library={{"T0",2,1000,1}};p.sinks.clear();
 for(int x=0;x<26;++x)for(int y=0;y<10;++y)p.sinks.push_back({10+10*x,10+10*y});
 original=cbi::Builder(p,false).build();cbi::validate_tree(p,original);
 budget=2048;auto large=cbi::swap_subtrees(p,original,&budget);
 assert(budget==2048 && !cbi::tree_less(large,original) && !cbi::tree_less(original,large));
}
