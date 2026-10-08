#define main original_main
#include "../../../../src/main.cpp"
#undef main
int main(int argc,char**argv){
 auto lines=cbi::read_logical_lines(argv[1]);if(cbi::is_error(lines))return 1;
 auto problem=cbi::parse_problem(std::get<std::vector<cbi::LogicalLine>>(lines));if(cbi::is_error(problem))return 1;
 auto p=std::get<cbi::Problem>(problem);
 try{
 auto tree=cbi::solve(p);auto before=cbi::validate_tree(p,tree);
 std::ofstream(argv[2])<<cbi::serialize_tree(p,tree);
 auto candidate=cbi::improve_tree(p,tree);auto after=cbi::validate_tree(p,candidate);
 std::cout<<before.score<<" "<<after.score<<"\n";
 std::ofstream(argv[3])<<cbi::serialize_tree(p,candidate);
 }catch(std::exception const& e){return 2;}
}
