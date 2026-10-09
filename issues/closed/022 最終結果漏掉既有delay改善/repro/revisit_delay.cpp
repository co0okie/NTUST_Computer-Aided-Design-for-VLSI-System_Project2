#define main original_cbi_main
#include "src/main.cpp"
#undef main
int main(int argc,char**argv){
 if(argc!=4)return 2;auto lines=cbi::read_logical_lines(argv[1]);if(cbi::is_error(lines))return 3;
 auto parsed=cbi::parse_problem(std::get<std::vector<cbi::LogicalLine>>(lines));if(cbi::is_error(parsed))return 4;
 auto const&p=std::get<cbi::Problem>(parsed);
 try{auto original=cbi::solve(p);std::size_t trials=2048,insertions=16;auto trial=cbi::delay_tree(p,original,&trials,&insertions);std::cerr<<"trials="<<2048-trials<<" insertions="<<16-insertions<<'\n';auto a=cbi::validate_tree(p,original),b=cbi::validate_tree(p,trial);
 std::ofstream(argv[2])<<cbi::serialize_tree(p,original);std::ofstream(argv[3])<<cbi::serialize_tree(p,trial);std::cout<<a.score<<" "<<b.score<<"\n";
 }catch(std::runtime_error const&e){std::cerr<<e.what()<<'\n';return 5;}
}
