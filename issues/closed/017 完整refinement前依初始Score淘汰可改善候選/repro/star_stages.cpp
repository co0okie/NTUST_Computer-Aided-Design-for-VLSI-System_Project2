#define main original_main
#include "../../../../src/main.cpp"
#undef main
int main(int argc,char**argv){
 auto lines=cbi::read_logical_lines(argv[1]);auto parsed=cbi::parse_problem(std::get<std::vector<cbi::LogicalLine>>(lines));auto p=std::get<cbi::Problem>(parsed);
 auto tree=cbi::Builder(p,false,-1).build_star();
 for(int stage=0;stage<3;++stage){
 auto m=cbi::validate_tree(p,tree);std::cout<<stage<<" "<<m.maximum_arrival<<" "<<m.minimum_arrival<<" "<<m.score<<"\n";std::ofstream(std::string(argv[2])+std::to_string(stage)+".cbi")<<cbi::serialize_tree(p,tree);
 if(stage==0)tree=cbi::improve_tree(p,std::move(tree));else if(stage==1)tree=cbi::refine_tree(p,std::move(tree));
 }
}
