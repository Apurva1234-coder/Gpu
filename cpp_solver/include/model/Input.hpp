#pragma once
#include "model/Model.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <regex>
#include <unordered_set>
#include <chrono>
namespace sovereign {
class InputTimeLimitExceeded : public std::runtime_error {
public:
    InputTimeLimitExceeded() : std::runtime_error("MILP wall-clock time limit reached during input parsing") {}
};
inline double parseNumber(const std::string& s){try{return std::stod(s);}catch(...){throw std::runtime_error("invalid number: "+s);}}
inline Model parseMPS(const std::string& path,
                      const std::chrono::steady_clock::time_point* deadline=nullptr){
    std::ifstream file(path); if(!file) throw std::runtime_error("cannot open input: "+path);
    struct RowInfo { std::string name; char type{}; };
    Model model;
    std::string section,line,rhsSet,rangeSet,boundsSet;
    std::vector<RowInfo> rows;
    std::unordered_map<std::string,std::size_t> rowIds, variableIds;
    std::vector<std::unordered_map<std::size_t,double>> rowCoefficients;
    std::unordered_map<std::string,double> rhs,ranges;
    bool integerBlock=false;
    auto ensureVariable=[&](const std::string& name)->std::size_t {
        auto found=variableIds.find(name);
        if(found!=variableIds.end()) return found->second;
        const std::size_t id=model.variables.size();
        variableIds.emplace(name,id);
        model.variables.push_back({id,id,name,VariableType::Continuous,0,INF,true});
        return id;
    };
    while(std::getline(file,line)){ if(deadline&&std::chrono::steady_clock::now()>=*deadline)throw InputTimeLimitExceeded(); if(line.empty()||line[0]=='*') continue; std::istringstream in(line); std::vector<std::string> t; std::string s; while(in>>s)t.push_back(s); if(t.empty())continue; std::string h=t[0]; for(char&c:h)c=static_cast<char>(std::toupper(c));
        if(h=="NAME"){model.name=t.size()>1?t[1]:"MPS";section=h;continue;}
        // Section labels are standalone records. In particular, an RHS data
        // record commonly starts with the RHS set name "RHS"; treating every
        // line whose first token is RHS as a header silently drops constraint RHS values.
        if(t.size()==1&&(h=="ROWS"||h=="COLUMNS"||h=="RHS"||h=="RANGES"||h=="BOUNDS"||h=="ENDATA")){section=h;if(h=="ENDATA")break;continue;}
        if(h=="OBJSENSE"){section=h;if(t.size()==1)continue;t.erase(t.begin());}
        if(section=="ROWS"&&t.size()>=2){rowIds[t[1]]=rows.size();rows.push_back({t[1],t[0][0]});rowCoefficients.emplace_back();}
        else if(section=="COLUMNS"){
            if(t.size()>=3&&t[1].find("MARKER")!=std::string::npos){integerBlock=t[2].find("INTORG")!=std::string::npos;continue;}
            if(t.size()<3||(t.size()-1)%2)throw std::runtime_error("invalid MPS COLUMNS record");
            const std::size_t variable=ensureVariable(t[0]);
            if(integerBlock)model.variables[variable].type=VariableType::Integer;
            for(size_t i=1;i+1<t.size();i+=2){
                auto row=rowIds.find(t[i]);
                if(row==rowIds.end())throw std::runtime_error("MPS COLUMNS references unknown row: "+t[i]);
                const double coefficient=parseNumber(t[i+1]);
                if(rows[row->second].type=='N'){if(coefficient!=0.0)model.objective[variable]+=coefficient;}
                else rowCoefficients[row->second][variable]+=coefficient;
            }
        }
        else if(section=="RHS"&&t.size()>=3){if(rhsSet.empty())rhsSet=t[0];if(t[0]!=rhsSet)continue;for(size_t i=1;i+1<t.size();i+=2)rhs[t[i]]=parseNumber(t[i+1]);}
        else if(section=="RANGES"&&t.size()>=3){if(rangeSet.empty())rangeSet=t[0];if(t[0]!=rangeSet)continue;for(size_t i=1;i+1<t.size();i+=2)ranges[t[i]]=parseNumber(t[i+1]);}
        else if(section=="OBJSENSE"){std::string sense=t[0];for(char&c:sense)c=static_cast<char>(std::toupper(c));if(sense=="MAX"||sense=="MAXIMIZE")model.sense=Sense::Maximize;else if(sense=="MIN"||sense=="MINIMIZE")model.sense=Sense::Minimize;else throw std::runtime_error("unsupported MPS objective sense: "+sense);}
        else if(section=="BOUNDS"&&t.size()>=3){if(boundsSet.empty())boundsSet=t[1];if(t[1]!=boundsSet)continue;std::string type=t[0],name=t[2];double value=t.size()>3?parseNumber(t[3]):0;size_t id=ensureVariable(name);if(type=="BV"){model.variables[id].type=VariableType::Binary;model.variables[id].lower=0;model.variables[id].upper=1;}else if(type=="LI"){model.variables[id].type=VariableType::Integer;model.variables[id].lower=value;}else if(type=="UI"){model.variables[id].type=VariableType::Integer;model.variables[id].upper=value;}else if(type=="LO")model.variables[id].lower=value;else if(type=="UP")model.variables[id].upper=value;else if(type=="FX")model.variables[id].lower=model.variables[id].upper=value;else if(type=="FR")model.variables[id].lower=-INF;else if(type=="MI")model.variables[id].lower=-INF;else if(type=="PL")model.variables[id].upper=INF;else throw std::runtime_error("unsupported MPS bound type: "+type);}
    }
    if(deadline&&std::chrono::steady_clock::now()>=*deadline)throw InputTimeLimitExceeded();
    if(model.name.empty()||rows.empty()||model.variables.empty())throw std::runtime_error("incomplete MPS input");
    for(std::size_t i=0;i<rows.size();++i){
        const auto& row=rows[i];
        if(row.type=='N')continue;
        Constraint c;c.originalId=model.constraints.size();c.name=row.name;c.relation=row.type=='E'?Relation::Equal:row.type=='L'?Relation::LessEqual:Relation::GreaterEqual;
        auto rhsIt=rhs.find(row.name);c.rhs=rhsIt==rhs.end()?0.0:rhsIt->second;c.coefficients=std::move(rowCoefficients[i]);
        auto range=ranges.find(row.name);
        if(range==ranges.end())model.constraints.push_back(std::move(c));
        else{double width=std::abs(range->second),lower,upper;if(row.type=='L'){lower=c.rhs;upper=c.rhs+width;}else if(row.type=='G'){lower=c.rhs-width;upper=c.rhs;}else if(range->second>=0){lower=c.rhs;upper=c.rhs+width;}else{lower=c.rhs+range->second;upper=c.rhs;}Constraint lo=c;lo.originalId=model.constraints.size();lo.name=row.name+"_RANGE_LO";lo.relation=Relation::GreaterEqual;lo.rhs=lower;model.constraints.push_back(std::move(lo));Constraint hi=c;hi.originalId=model.constraints.size();hi.name=row.name+"_RANGE_UP";hi.relation=Relation::LessEqual;hi.rhs=upper;model.constraints.push_back(std::move(hi));}
    }
    model.rebuildMappings();return model;
}
inline Model parseText(const std::string& path){
    std::ifstream f(path); if(!f) throw std::runtime_error("cannot open input: "+path); Model m; std::string line;
    while(std::getline(f,line)){ if(line.empty()||line[0]=='#')continue;
        if(line.find("name:")==0)m.name=line.substr(5);
        else if(line.find("objective:")==0){auto s=line.substr(10);while(!s.empty()&&std::isspace(static_cast<unsigned char>(s.front())))s.erase(s.begin());m.sense=s.find("minimize")==0?Sense::Minimize:Sense::Maximize;auto p=s.find(' ');if(p!=std::string::npos){std::string e=s.substr(p+1);std::regex term("([+-]?)([0-9.]*)([A-Za-z_]\\w*)");for(std::sregex_iterator i(e.begin(),e.end(),term),z;i!=z;++i){double c=(*i)[2].str().empty()?1:parseNumber((*i)[2]);if((*i)[1]=="-")c=-c;for(size_t n=0;n<m.variables.size();++n)if(m.variables[n].name==(*i)[3])m.objective[n]=c;}}}
        else if(line.find("var ")==0||line.find("variable ")==0){auto off=line.find(" ")+1;std::istringstream in(line.substr(off));std::string n,t;in>>n>>t;if(!n.empty()&&n.back()==':')n.pop_back();if(t==":")in>>t;VariableType type=t=="integer"?VariableType::Integer:t=="binary"?VariableType::Binary:VariableType::Continuous;size_t i=m.variables.size();m.variables.push_back({i,i,n,type,0,type==VariableType::Binary?1:INF,true});}
        else if(line.find("bound:")==0){std::smatch z;std::regex b("bound:\\s*([A-Za-z_]\\w*)\\s*\\[\\s*([^,]+),\\s*([^\\]]+)\\]");if(!std::regex_search(line,z,b))throw std::runtime_error("invalid TXT bound");for(auto&v:m.variables)if(v.name==z[1]){v.lower=(z[2]=="-inf"? -INF:parseNumber(z[2]));v.upper=(z[3]=="inf"?INF:parseNumber(z[3]));}}
        else if(line.find("constraint:")==0){auto p=line.find(':',11);auto body=line.substr(p+1);auto op=body.find("<=");Relation rel=Relation::LessEqual;size_t width=2;if(op==std::string::npos){op=body.find(">=");rel=Relation::GreaterEqual;}if(op==std::string::npos){op=body.find('=');rel=Relation::Equal;width=1;}if(op==std::string::npos)throw std::runtime_error("invalid TXT constraint");Constraint c;c.originalId=m.constraints.size();c.name=line.substr(11,p-11);c.relation=rel;c.rhs=parseNumber(body.substr(op+width));std::istringstream terms(body.substr(0,op));std::string term;while(terms>>term){if(term=="+")continue;for(size_t i=0;i<m.variables.size();++i){auto q=term.find(m.variables[i].name);if(q!=std::string::npos){std::string coef=term.substr(0,q);c.coefficients[i]=coef.empty()||coef=="+"?1:parseNumber(coef);}}}m.constraints.push_back(c);}
    } m.rebuildMappings(); return m;
}
inline Model parseJSON(const std::string& path){
    std::ifstream f(path);if(!f)throw std::runtime_error("cannot open input: "+path);std::string s((std::istreambuf_iterator<char>(f)),{});Model m;auto str=[&](const std::string&k){std::regex q("\\\""+k+"\\\"\\s*:\\s*\\\"([^\"]*)\\\"");std::smatch x;return std::regex_search(s,x,q)?x[1].str():std::string();};m.name=str("name");m.sense=str("objective_sense")=="minimize"?Sense::Minimize:Sense::Maximize;
    std::regex vr("\\{\\s*\\\"name\\\"\\s*:\\s*\\\"([^\"]+)\\\"\\s*,\\s*\\\"type\\\"\\s*:\\s*\\\"([^\"]+)\\\"([^}]*)\\}");for(std::sregex_iterator i(s.begin(),s.end(),vr),e;i!=e;++i){VariableType t=(*i)[2]=="integer"?VariableType::Integer:(*i)[2]=="binary"?VariableType::Binary:VariableType::Continuous;size_t n=m.variables.size();m.variables.push_back({n,n,(*i)[1],t,0,t==VariableType::Binary?1:INF,true});}
    auto section=[&](const std::string&k){auto p=s.find("\""+k+"\"");if(p==std::string::npos)return std::string();p=s.find('[',p);auto q=s.find(']',p);return s.substr(p,q-p+1);};
    const std::string number="[-+]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][-+]?[0-9]+)?";
    std::string obj=section("objective");std::regex osec("\\\"objective\\\"\\s*:\\s*\\{([^}]*)\\}");std::smatch om;if(std::regex_search(s,om,osec))obj=om[1].str();std::regex kv("\\\"([^\"]+)\\\"\\s*:\\s*("+number+")");for(std::sregex_iterator i(obj.begin(),obj.end(),kv),e;i!=e;++i)for(size_t j=0;j<m.variables.size();++j)if(m.variables[j].name==(*i)[1])m.objective[j]=parseNumber((*i)[2]);
    std::regex cr("\\{\\s*\\\"name\\\"\\s*:\\s*\\\"([^\"]+)\\\".*?\\\"coefficients\\\"\\s*:\\s*\\{([^}]*)\\}.*?\\\"operator\\\"\\s*:\\s*\\\"([^\"]+)\\\".*?\\\"rhs\\\"\\s*:\\s*("+number+")",std::regex::icase);for(std::sregex_iterator i(s.begin(),s.end(),cr),e;i!=e;++i){Constraint c;c.originalId=m.constraints.size();c.name=(*i)[1];c.relation=(*i)[3]=="<="?Relation::LessEqual:(*i)[3]==">="?Relation::GreaterEqual:Relation::Equal;c.rhs=parseNumber((*i)[4]);for(std::sregex_iterator j((*i)[2].first,(*i)[2].second,kv),z;j!=z;++j)for(size_t n=0;n<m.variables.size();++n)if(m.variables[n].name==(*j)[1])c.coefficients[n]=parseNumber((*j)[2]);m.constraints.push_back(c);}std::regex qsec("\\\"quadratic_terms\\\"\\s*:\\s*\\{([^}]*)\\}");std::smatch qmatch;if(std::regex_search(s,qmatch,qsec))for(std::sregex_iterator i(qmatch[1].first,qmatch[1].second,kv),e;i!=e;++i)for(size_t n=0;n<m.variables.size();++n)if(m.variables[n].name==(*i)[1])m.quadratic[n]=parseNumber((*i)[2]);std::regex bsec("\\\"bounds\\\"\\s*:\\s*\\{([^}]*)\\}");std::smatch bmatch;if(std::regex_search(s,bmatch,bsec)){std::regex bound("\\\"([^\"]+)\\\"\\s*:\\s*\\[\\s*(null|"+number+")\\s*,\\s*(null|"+number+")\\s*\\]",std::regex::icase);for(std::sregex_iterator i(bmatch[1].first,bmatch[1].second,bound),e;i!=e;++i)for(auto& v:m.variables)if(v.name==(*i)[1]){if((*i)[2]!="null")v.lower=parseNumber((*i)[2]);else v.lower=-INF;if((*i)[3]!="null")v.upper=parseNumber((*i)[3]);else v.upper=INF;}}m.rebuildMappings();return m;
}
inline Model parseInput(const std::string& path,
                        const std::chrono::steady_clock::time_point* deadline=nullptr){
    std::ifstream f(path);if(!f)throw std::runtime_error("cannot open input: "+path);
    std::string line; bool json=false, mps=false;
    while(std::getline(f,line)) {
        const auto first=line.find_first_not_of(" \t\r\n"); if(first==std::string::npos)continue;
        if(line[first]=='*'||line[first]=='#')continue;
        if(line[first]=='{')json=true;
        std::istringstream in(line.substr(first)); std::string token; in>>token;
        for(char& c:token)c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        mps=(token=="NAME"||token=="ROWS"||token=="OBJSENSE");
        break;
    }
    if(mps)return parseMPS(path,deadline);if(json)return parseJSON(path);return parseText(path);
}
}

