#define main original_cbi_main
#include "../../../../src/main.cpp"
#undef main
int main(int argc, char** argv) {
 if (argc != 3) return 2;
 auto lines=cbi::read_logical_lines(argv[1]);
 if(cbi::is_error(lines)) return 3;
 auto parsed=cbi::parse_problem(std::get<std::vector<cbi::LogicalLine>>(lines));
 if(cbi::is_error(parsed))return 4;
 auto const& p=std::get<cbi::Problem>(parsed);
 std::optional<cbi::Tree> best;
 cbi::Integer score=std::numeric_limits<cbi::Integer>::max();
 std::string label;
 auto consider=[&](cbi::Tree candidate) {
  auto initial=cbi::validate_tree(p,candidate).score;
  candidate=cbi::improve_tree(p,std::move(candidate));
  auto m=cbi::validate_tree(p,candidate);
  if(!best || m.score<score){std::cerr<<label<<" initial="<<initial<<" improved="<<m.score<<"\n";score=m.score;best=std::move(candidate);}
 };
 auto direct=cbi::initial_tree(p,false);
 for(std::size_t i=1;i<=p.sinks.size();++i) direct[0].children.push_back(i);
 label="direct";try{consider(std::move(direct));}catch(std::runtime_error const&){}
 for(auto relay:cbi::buffer_policies(p)){
  for(auto branch:cbi::buffer_policies(p,true))for(bool parity:{true,false}){
   label="binary relay="+std::to_string(relay)+" branch="+std::to_string(branch)+" parity="+std::to_string(parity);
   try{consider(cbi::Builder(p,parity,relay,branch).build());}catch(std::runtime_error const&){}
  }
  label="star relay="+std::to_string(relay);try{consider(cbi::Builder(p,false,relay).build_star());}catch(std::runtime_error const&){}
  label="hub relay="+std::to_string(relay);try{consider(cbi::Builder(p,false,relay).build_hub());}catch(std::runtime_error const&){}
 }
 if(!best)return 5;
 std::ofstream out(argv[2]);out<<cbi::serialize_tree(p,*best);
 std::cout<<score<<'\n';
}
