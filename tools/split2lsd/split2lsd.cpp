#include <fstream>
#include <string>
#include <iostream>
#include <sstream>

int main(int argc, char **argv)
{
    if(argc != 3)
    {
        std::cout << "Usage: split2lsd <file> <out prefix>\n";
        return 0;
    }

    std::ifstream fin(argv[1]);
    if(!fin)
    {
        std::cout << "Cannot open '" << argv[1] << "' for reading\n";
        return 1;
    }

    std::string line;
    int counter = 0;

    if(!std::getline(fin, line))
        return 0;
    while(true)
    {
        if(line.size() == 0 || line[0] != '*')
        {
            std::cout << "error\n";
            return 1;
        }

        std::ostringstream oss;
        oss << argv[2] << counter++ << ".lsd";
        std::ofstream fout(oss.str().c_str());
        if(!fout)
        {
            std::cout << "Cannot open '" << oss.str() << "' for writing\n";
            return 1;
        }
        while(std::getline(fin, line))
        {
            if(line.size() > 0 && line[0] == '*')
                goto Lnext;
            fout << line << "\n";
        }

        break;
        Lnext:
        continue;
    }

    return 0;
}
