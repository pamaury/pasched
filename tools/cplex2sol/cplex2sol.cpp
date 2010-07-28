#include <fstream>
#include <string>
#include <iostream>
#include <sstream>
#include <vector>
#include <cmath>
#include <cstdlib>

std::string trim(const std::string& s)
{
    size_t f = s.find_first_not_of(" ");
    if(f == std::string::npos)
        return std::string();

    return s.substr(f, s.find_last_not_of(" ") - f + 1);
}

double round_if(double v)
{
    double eps = 1e-10;
    double r = ceil(v);
    if(fabs(r - v) < eps)
        return r;

    r = floor(v);
    if(fabs(r - v) < eps)
        return r;

    return v;
}

int main(int argc, char **argv)
{
    if(argc != 3)
    {
        std::cout << "Usage: cplex2sol <in cplex sol file> <out sol file>\n";
        return 0;
    }

    std::ifstream fin(argv[1]);
    if(!fin)
    {
        std::cout << "Cannot open '" << argv[1] << "' for reading\n";
        return 1;
    }

    std::ofstream fout(argv[2]);
    if(!fout)
    {
        std::cout << "Cannot open '" << argv[2] << "' for writing\n";
        return 1;
    }

    std::vector< double > constraint;
    std::vector< double > variable;
    double obj;

    std::string line;
    while(std::getline(fin, line))
    {
        int idx;
        double val;
        
        line = trim(line);
        
        std::string s = "<constraint ";
        if(line.size() > s.size() && line.substr(0, s.size()) == s)
        {
            line = trim(line.substr(s.size()));
            s = "name=\"";
            if(line.size() <= s.size() || line.substr(0, s.size()) != s)
                continue;
            line = line.substr(s.size());
            size_t pos = line.find('\"');
            if(pos == std::string::npos)
                continue;
            line = trim(line.substr(pos + 1));
            s = "index=\"";
            if(line.size() <= s.size() || line.substr(0, s.size()) != s)
                continue;
            line = line.substr(s.size());
            pos = line.find('\"');
            if(pos == std::string::npos)
                continue;

            std::istringstream iss(line.substr(0, pos));
            if(!(iss >> idx) || !iss.eof())
                continue;

            line = trim(line.substr(pos + 1));
            s = "slack=\"";
            if(line.size() <= s.size() || line.substr(0, s.size()) != s)
                continue;
            line = line.substr(s.size());
            pos = line.find('\"');
            if(pos == std::string::npos)
                continue;

            std::istringstream iss2(line.substr(0, pos));
            if(!(iss2 >> val))
                continue;

            constraint.push_back(val);
            //std::cout << "constraint: idx=" << idx << " val=" << val << "\n";
            continue;
        }

        s = "<variable ";
        if(line.size() > s.size() && line.substr(0, s.size()) == s)
        {
            line = trim(line.substr(s.size()));
            s = "name=\"";
            if(line.size() <= s.size() || line.substr(0, s.size()) != s)
                continue;
            line = line.substr(s.size());
            size_t pos = line.find('\"');
            if(pos == std::string::npos)
                continue;
            line = trim(line.substr(pos + 1));
            s = "index=\"";
            if(line.size() <= s.size() || line.substr(0, s.size()) != s)
                continue;
            line = line.substr(s.size());
            pos = line.find('\"');
            if(pos == std::string::npos)
                continue;

            std::istringstream iss(line.substr(0, pos));
            if(!(iss >> idx) || !iss.eof())
                continue;

            line = trim(line.substr(pos + 1));
            s = "value=\"";
            if(line.size() <= s.size() || line.substr(0, s.size()) != s)
                continue;
            line = line.substr(s.size());
            pos = line.find('\"');
            if(pos == std::string::npos)
                continue;

            std::istringstream iss2(line.substr(0, pos));
            if(!(iss2 >> val))
                continue;

            variable.push_back(val);
            //std::cout << "value: idx=" << idx << " val=" << val << "\n";
            continue;
        }

        s = "objectiveValue=\"";
        if(line.size() > s.size() && line.substr(0, s.size()) == s)
        {
            line = line.substr(s.size());
            size_t pos = line.find('\"');
            if(pos == std::string::npos)
                continue;

            std::istringstream iss(line.substr(0, pos));
            if(!(iss >> obj) || !iss.eof())
                continue;

            std::cout << "objective value: " << obj << "\n";
            continue;
        }
    }

    fout << constraint.size() << " " << variable.size() << "\n";
    fout << "5 " << obj << "\n";
    for(size_t i = 0; i < constraint.size(); i++)
        fout << round_if(constraint[i]) << "\n";
    for(size_t i = 0; i < variable.size(); i++)
        fout << round_if(variable[i]) << "\n";

    return 0;
}
