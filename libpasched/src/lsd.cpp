#include "tools.hpp"
#include "lsd.hpp"
#include <fstream>
#include <stdexcept>
#include <sstream>

namespace PAMAURY_SCHEDULER_NS
{

std::string lsd_schedule_unit::to_string() const
{
    return m_name;
}

unsigned lsd_schedule_unit::internal_register_pressure() const
{
    return 0;
}

const lsd_schedule_unit *lsd_schedule_unit::dup() const
{
    return new lsd_schedule_unit(*this);
}

const lsd_schedule_unit *lsd_schedule_unit::deep_dup() const
{
    return dup();
}

void dump_schedule_dag_to_lsd_file(const schedule_dag& dag, const char *filename)
{
    std::ofstream fout(filename);

    if(!fout)
        throw std::runtime_error("cannot open file '" + std::string(filename) + "' for writing");
    for(size_t u = 0; u < dag.get_units().size(); u++)
    {
        const schedule_unit *unit = dag.get_units()[u];
        fout << "Unit " << (void *)unit << " Name ";
        size_t pos = 0;
        std::string name = unit->to_string();
        while((pos = name.find('\n', pos)) != std::string::npos)
        {
            name.replace(pos, 1, "\\\n");
            pos += 2;
        }
        fout << name << "\n";
        for(size_t i = 0; i < dag.get_succs(unit).size(); i++)
        {
            const schedule_dep& dep = dag.get_succs(unit)[i];
            fout << "To " << (void *)dep.to() << " Latency 1 Kind ";
            if(dep.kind() == schedule_dep::data_dep)
                fout << "data Reg " << dep.reg() << "\n";
            else if(dep.kind() == schedule_dep::order_dep)
                fout << "order\n";
        }
    }
}

void build_schedule_dag_from_lsd_file(const char *filename, schedule_dag& dag)
{
    std::ifstream fin(filename);

    if(!fin)
        throw std::runtime_error("cannot open file '" + std::string(filename) + "' for reading");

    std::vector< lsd_schedule_unit * > units;
    std::map< std::string, int > name_map;
    int current_unit = -1;

    std::string line;
    while(std::getline(fin, line))
    {
        line = trim(line);
        if(line.size() == 0)
            continue;
        
        if(line.substr(0, 5) == "Unit ")
        {
            line = trim(line.substr(5));
            size_t pos = line.find(" ");
            if(pos == std::string::npos)
                throw std::runtime_error("illformed lsd file: Unit line type with no name ('" + line + "')");
            std::string name = line.substr(0, pos);
            if(name_map.find(name) == name_map.end())
            {
                name_map[name] = units.size();
                units.push_back(new lsd_schedule_unit(name));
                dag.add_unit(units[units.size() - 1]);
            }
            current_unit = name_map[name];

            line = trim(line.substr(pos));
            if(line.substr(0, 5) != "Name ")
                throw std::runtime_error("illformed lsd file: Unit line type with no name ('" + line + "')");
            line = line.substr(5); // No trim !

            while(line.size() > 0 && line[line.size() - 1] == '\\')
            {
                units[current_unit]->name() += line.substr(0, line.size() - 1) + "\n";
                if(!std::getline(fin, line))
                    throw std::runtime_error("illformed lsd file: Unit line type with name continuation but no line after that ('" + line + "')");
            }
            units[current_unit]->name() += line;
        }
        else if(line.substr(0, 3) == "To ")
        {
            if(current_unit == -1)
                throw std::runtime_error("illformed lsd file: To line type with no current unit ('" + line + "')");
            
            line = trim(line.substr(3));
            
            size_t pos = line.find(" ");
            if(pos == std::string::npos)
                throw std::runtime_error("illformed lsd file: incomplete To line type ('" + line + "')");
            std::string to = line.substr(0, pos);
            line = trim(line.substr(pos));

            if(name_map.find(to) == name_map.end())
            {
                name_map[to] = units.size();
                units.push_back(new lsd_schedule_unit(to));
                dag.add_unit(units[units.size() - 1]);
            }

            schedule_dep d;
            d.set_from(units[current_unit]);
            d.set_to(units[name_map[to]]);

            if(line.substr(0, 8) != "Latency ")
                throw std::runtime_error("illformed lsd file: To line type with no latency ('" + line + "')");
            line = trim(line.substr(8));
            pos = line.find(" ");
            if(pos == std::string::npos)
                throw std::runtime_error("illformed lsd file: To line type with no kind ('" + line + "')");
            
            std::istringstream iss(line.substr(0, pos));
            int latency;
            if(!(iss >> latency) || !iss.eof())
                throw std::runtime_error("illformed lsd file: To line type with invalid latency ('" + line + "')");
            /* ignore latency */
            
            line = trim(line.substr(pos));

            if(line.substr(0, 5) != "Kind ")
                throw std::runtime_error("illformed lsd file: To line type with no kind ('" + line + "')");

            line = trim(line.substr(5));

            pos = line.find(" ");
            if(pos == std::string::npos)
                pos = line.size();
            std::string kind = line.substr(0, pos);
            line = trim(line.substr(pos));

            if(kind == "order")
            {
                if(line.size() != 0)
                    throw std::runtime_error("illformed lsd file: To line type with extra data at the end ('" + line + "')");
                d.set_kind(schedule_dep::order_dep);
            }
            else if(kind == "data")
            {
                if(line.substr(0, 4) != "Reg ")
                    throw std::runtime_error("illformed lsd file: To line type with no data reg ('" + line + "')");
                line = trim(line.substr(4));
                if(line.find(" ") != std::string::npos)
                    throw std::runtime_error("illformed lsd file: To line type with extra data at the end ('" + line + "')");
                std::istringstream iss(line);
                int reg;
                if(!(iss >> reg) || !iss.eof())
                    throw std::runtime_error("illformed lsd file: To line type with invalid reg ('" + line + "')");
                d.set_reg(reg);
                d.set_kind(schedule_dep::data_dep);
            }
            else
                throw std::runtime_error("illformed lsd file: To line type with bad kind ('" + line + "')");

            dag.add_dependency(d);
        }
        else
            throw std::runtime_error("illformed lsd file: unknown line type ('" + line + "')");
    }
}



}
