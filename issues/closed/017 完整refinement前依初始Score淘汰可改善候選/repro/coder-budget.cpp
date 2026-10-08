#include "tree.hpp"
#include <cassert>
#include <iostream>
int main() {
 cbi::Problem p{{3,32},{30,30},{{"T0",2,11,4},{"T1",2,18,13}},10,{9,9},{{9,3},{17,24},{24,6}}};
 auto star=cbi::Builder(p,false,-1).build_star();
 auto score=cbi::validate_tree(p,star).score;
 assert(score==178);
 std::size_t trials=0,insertions=16;
 auto zero=cbi::refine_tree(p,star,&trials,&insertions);
 assert(trials==0 && insertions==16);
 assert(!cbi::tree_less(zero,star) && !cbi::tree_less(star,zero));
 trials=1;
 auto one=cbi::refine_tree(p,star,&trials,&insertions);
 assert(trials==0 && cbi::validate_tree(p,one).score<=score);
 trials=8192;insertions=16;
 auto refined=cbi::improve_tree(p,star,&trials);
 refined=cbi::refine_tree(p,std::move(refined),&trials,&insertions);
 assert(cbi::validate_tree(p,refined).score<=27);
 std::cout << "Score " << cbi::validate_tree(p,refined).score << ", trials " << 8192-trials << ", insertions " << 16-insertions << '\n';
 auto unchanged=cbi::refine_tree(p,refined,&trials,&insertions);
 assert(cbi::validate_tree(p,unchanged).score<=27);
}
