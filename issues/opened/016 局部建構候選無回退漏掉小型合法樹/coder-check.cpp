#include "tree.hpp"
#include <cassert>
#include <algorithm>
int main() {
    cbi::Problem p{{2,4},{5,5},{{"T0",2,5,1}},100,{2,1},{{2,4},{3,4},{1,2},{1,3}}};
    std::sort(p.sinks.begin(),p.sinks.end(),[](auto a,auto b){return std::tie(a.x,a.y)<std::tie(b.x,b.y);});
    unsigned permutations=0;
    do {
        std::size_t budget=65536;
        auto t=cbi::small_forest(p,budget);
        assert(t && t->size()<=16 && budget<65536);
        assert(cbi::validate_tree(p,*t).score<=302);
        ++permutations;
    } while(std::next_permutation(p.sinks.begin(),p.sinks.end(),[](auto a,auto b){return std::tie(a.x,a.y)<std::tie(b.x,b.y);}));
    assert(permutations==24);
    std::size_t zero=0;
    assert(!cbi::small_forest(p,zero) && zero==0);
    std::size_t one=1;
    assert(!cbi::small_forest(p,one) && one==0);
    auto bad=p;bad.buffer_library[0].length=1;
    std::size_t budget=65536;
    assert(!cbi::small_forest(bad,budget));
    auto large=p;large.sinks.resize(9);
    budget=65536;assert(!cbi::small_forest(large,budget) && budget==65536);
    large=p;large.buffer_library.resize(65);
    assert(!cbi::small_forest(large,budget) && budget==65536);
    budget=65536;assert(cbi::small_forest(p,budget));
    // Boundary pins plus a translated interior preserve valid forest geometry.
    p.dimensions={50,50};p.source={22,21};
    for(auto& s:p.sinks) {s.x+=20;s.y+=20;}
    budget=65536;auto translated=cbi::small_forest(p,budget);
    assert(translated);cbi::validate_tree(p,*translated);
}
