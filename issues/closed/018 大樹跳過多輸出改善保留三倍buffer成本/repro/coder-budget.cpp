#include "tree.hpp"
#include <cassert>
#include <iostream>
int main() {
 cbi::Problem p{{1,200},{64,64},{{"T0",4,200,1}},100,{32,0},{}};
 for(int x=1;x<64;x+=2)for(int y=1;y<64;y+=2)p.sinks.push_back({x,y});
 auto original=cbi::Builder(p,false).build();
 auto before=cbi::validate_tree(p,original);assert(before.score==1023);
 std::size_t work=0;
 auto zero=cbi::contract_large_tree(p,original,&work);
 assert(work==0 && !cbi::tree_less(zero,original) && !cbi::tree_less(original,zero));
 work=1;auto one=cbi::contract_large_tree(p,original,&work);
 assert(work==0 && cbi::validate_tree(p,one).score<=before.score);
 for(std::size_t cap : {10U,100U,1000U}) {
  work=cap;auto partial=cbi::contract_large_tree(p,original,&work);
  auto m=cbi::validate_tree(p,partial);
  assert(work==0 && m.score<=before.score);
  assert(m.minimum_arrival==before.minimum_arrival && m.maximum_arrival==before.maximum_arrival);
 }
 work=1048576;auto result=cbi::contract_large_tree(p,original,&work);
 auto after=cbi::validate_tree(p,result);
 assert(after.score==341 && after.minimum_arrival==before.minimum_arrival && after.maximum_arrival==before.maximum_arrival);
 std::cout<<"Score "<<after.score<<", work "<<1048576-work<<'\n';
 p.buffer_library={{"T0",2,200,1},{"T1",4,200,10}};
 original=cbi::Builder(p,false).build();before=cbi::validate_tree(p,original);
 result=cbi::contract_large_tree(p,original);after=cbi::validate_tree(p,result);
 assert(after.score<=before.score);
 p.buffer_library={{"T0",2,200,1}};
 original=cbi::Builder(p,false).build();before=cbi::validate_tree(p,original);
 result=cbi::contract_large_tree(p,original);after=cbi::validate_tree(p,result);
 assert(after.score==before.score);
 p.buffer_library.resize(65,p.buffer_library[0]);work=1048576;
 result=cbi::contract_large_tree(p,original,&work);
 assert(work==1048576 && !cbi::tree_less(result,original) && !cbi::tree_less(original,result));
}
