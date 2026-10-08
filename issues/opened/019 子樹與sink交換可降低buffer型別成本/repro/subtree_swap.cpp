#define main original_cbi_main
#include "src/main.cpp"
#undef main
int main(int argc,char**argv){
 if(argc!=4)return 2;
 auto lines=cbi::read_logical_lines(argv[1]);if(cbi::is_error(lines))return 3;
 auto parsed=cbi::parse_problem(std::get<std::vector<cbi::LogicalLine>>(lines));if(cbi::is_error(parsed))return 4;
 auto const&p=std::get<cbi::Problem>(parsed);
 try{
 auto original=cbi::solve(p),best=original;auto baseline=cbi::validate_tree(p,original).score,score=baseline;
 std::vector<std::size_t> parent(original.size());for(std::size_t i=0;i<original.size();++i)for(auto ch:original[i].children)parent[ch]=i;
 auto ancestor=[&](std::size_t a,std::size_t b){while(b){if(a==b)return true;b=parent[b];}return false;};
 for(std::size_t a=1;a<original.size();++a)for(std::size_t b=a+1;b<original.size();++b){
 if((a<=p.sinks.size()&&b<=p.sinks.size())||parent[a]==parent[b]||ancestor(a,b)||ancestor(b,a))continue;
 auto trial=original;auto&x=trial[parent[a]].children;auto&y=trial[parent[b]].children;*std::find(x.begin(),x.end(),a)=b;*std::find(y.begin(),y.end(),b)=a;
 try{cbi::select_buffer_types(p,trial);auto m=cbi::validate_tree(p,trial);if(m.score<score){score=m.score;best=trial;std::cerr<<"swap "<<a<<" "<<b<<" parents "<<parent[a]<<" "<<parent[b]<<" Score "<<score<<"\n";}}catch(std::runtime_error const&){}
 }
 std::ofstream(argv[2])<<cbi::serialize_tree(p,original);std::ofstream(argv[3])<<cbi::serialize_tree(p,best);std::cout<<baseline<<" "<<score<<"\n";
 }catch(std::runtime_error const&e){std::cerr<<e.what()<<"\n";return 5;}
}
