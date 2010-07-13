#include "tools.hpp"
#include "ddl.hpp"
#include <fstream>
#include <stdexcept>
#include <sstream>

namespace PAMAURY_SCHEDULER_NS
{

std::string ddl_schedule_unit::to_string() const
{
    std::ostringstream oss;
    for(size_t i = 0; i < m_out.size(); i++)
    {
        oss << m_out[i];
        if((i + 1) != m_out.size())
            oss << ", ";
    }
    oss << " <- ";
    for(size_t i = 0; i < m_in.size(); i++)
    {
        oss << m_in[i];
        if((i + 1) != m_in.size())
            oss << ", ";
    }

    return oss.str();
}

unsigned ddl_schedule_unit::internal_register_pressure() const
{
    return 0;
}

const ddl_schedule_unit *ddl_schedule_unit::dup() const
{
    return new ddl_schedule_unit(*this);
}

const ddl_schedule_unit *ddl_schedule_unit::deep_dup() const
{
    return dup();
}

std::vector< std::string >& ddl_schedule_unit::out()
{
    return m_out;
}

const std::vector< std::string >& ddl_schedule_unit::out() const
{
    return m_out;
}

std::vector< std::string >& ddl_schedule_unit::in()
{
    return m_in;
}

const std::vector< std::string >& ddl_schedule_unit::in() const
{
    return m_in;
}


namespace
{
    void parse_dep_list(std::string line, std::vector< std::string >& list)
    {
        while(true)
        {
            line = trim(line);
            if(line.size() == 0)
                return;
            size_t p = line.find(",");
            if(p == std::string::npos)
            {
                list.push_back(line);
                return;
            }
            list.push_back(trim(line.substr(0, p)));
            line = line.substr(p + 1);
        }
    }
}

void load_ddl_program_from_ddl_file(const char *filename, ddl_program& p)
{
    std::ifstream fin(filename);
    if(!fin)
        throw std::runtime_error("cannot open file '" + std::string(filename) + "' for reading");

    p.instrs.clear();
    /* load instructions */    
    std::string line;
    while(std::getline(fin, line))
    {
        size_t comment = line.find(";");
        if(comment != std::string::npos)
            line = line.substr(0, comment);
        line = trim(line);
        if(line.size() == 0)
            continue; // skip empty line
        
        size_t pos = line.find("<-");
        if(pos == std::string::npos)
            throw std::runtime_error("illformed ddl file '" + line + "'");

        ddl_schedule_unit *instr = new ddl_schedule_unit;
        parse_dep_list(line.substr(0, pos), instr->out());
        parse_dep_list(line.substr(pos + 2), instr->in());
        p.instrs.push_back(instr);
    }
    fin.close();
}

void build_schedule_dag_from_ddl_program(const ddl_program& p, schedule_dag& dag)
{
    /* build DDG */
    for(size_t i = 0; i < p.instrs.size(); i++)
        dag.add_unit(p.instrs[i]);

    std::map< std::string, size_t > last_def;
    std::map< std::string, size_t > last_use;
    std::map< std::string, unsigned > name_map;
    unsigned next_name = 1;

    for(size_t u = 0; u < p.instrs.size(); u++)
    {
        const ddl_schedule_unit *unit = p.instrs[u];
        for(size_t i = 0; i < unit->in().size(); i++)
        {
            std::string name = unit->in()[i];
            /* assign a number of the name if mecessary */
            if(name_map.find(name) == name_map.end())
                name_map[name] = next_name++;
            /* check for last use */
            if(last_def.find(name) == last_def.end())
                throw std::runtime_error("ddl instruction '" + unit->to_string() + "' uses the undefined name '" + name + "'");
            /* add flow dependency */
            dag.add_dependency(
                schedule_dep(
                    /* from */ p.instrs[last_def[name]],
                    /* to */ unit,
                    /* kind */ schedule_dep::data_dep,
                    /* reg */ name_map[name]));
            /* update last use */
            last_use[name] = u;
        }

        for(size_t i = 0; i < unit->out().size(); i++)
        {
            std::string name = unit->out()[i];
            /* assign a number of the name if mecessary */
            if(name_map.find(name) == name_map.end())
                name_map[name] = next_name++;
            /* update last def */
            last_def[name] = u;
        }
    }
}

struct name_info
{
    std::vector< std::pair< unsigned, unsigned > > live_ranges;
    bool has_def;
    unsigned def;
    bool has_use;
    unsigned last_use;
    bool displayed;
    std::string name;

    name_info()
        :has_def(false), has_use(false), displayed(false){}
};

void dump_ddl_program_analysis_to_tex_file(ddl_program& p, const char *filename)
{
    std::string tab = "  ";
    std::ofstream fout(filename, std::ios::out | std::ios::trunc);
    if(!fout)
        throw std::runtime_error("cannot open file '" + std::string(filename) + "' for writing");

    fout << "\\documentclass[12pt]{article}\n";
    fout << "\\usepackage[english]{babel}\n";
    fout << "\\usepackage[T1]{fontenc}\n";
    fout << "\\usepackage[utf8]{inputenc}\n";
    fout << "\\usepackage[left=2cm,right=2cm,top=3cm,bottom=3cm]{geometry}\n";
    fout << "\\usepackage{amsmath,amssymb,mathrsfs}\n";
    fout << "\\usepackage{amsthm}\n";
    fout << "\\usepackage{color}\n";
    fout << "\\usepackage{multirow}\n";
    fout << "\\usepackage{colortbl}\n";
    fout << "\n";
    fout << "\\begin{document}\n";

    std::map< std::string, unsigned > name_map;
    std::map< unsigned, std::string > rev_name_map;
    std::vector< name_info > info_map;
    unsigned name_count = 0;

    /* build name map */
    for(size_t u = 0; u < p.instrs.size(); u++)
    {
        const ddl_schedule_unit *unit = p.instrs[u];
        for(size_t i = 0; i < unit->out().size(); i++)
            if(name_map.find(unit->out()[i]) == name_map.end())
            {
                name_map[unit->out()[i]] = name_count;
                rev_name_map[name_count++] = unit->out()[i];
            }
        for(size_t i = 0; i < unit->in().size(); i++)
            if(name_map.find(unit->in()[i]) == name_map.end())
            {
                name_map[unit->in()[i]] = name_count;
                rev_name_map[name_count++] = unit->in()[i];
            }
                
    }

    info_map.resize(name_count);

    /* compute live ranges */
    for(size_t u = 0; u < p.instrs.size(); u++)
    {
        const ddl_schedule_unit *unit = p.instrs[u];
        for(size_t i = 0; i < unit->in().size(); i++)
        {
            unsigned index = name_map[unit->in()[i]];
            name_info& ni = info_map[index];

            ni.has_use = true;
            ni.last_use = u;
        }
        
        for(size_t i = 0; i < unit->out().size(); i++)
        {
            unsigned index = name_map[unit->out()[i]];
            name_info& ni = info_map[index];

            if(ni.has_def && ni.has_use)
                ni.live_ranges.push_back(std::make_pair(ni.def, ni.last_use));
            ni.has_def = true;
            ni.has_use = false;
            ni.def = u;
        }
    }
    /* close open ranges */
    for(size_t i = 0; i < info_map.size(); i++)
    {
        name_info& ni = info_map[i];
        if(ni.has_def && ni.has_use)
            ni.live_ranges.push_back(std::make_pair(ni.def, ni.last_use));
    }

    std::string col_desc;
    col_desc.append(name_count, 'c');

    unsigned max_reg_pressure = 0;
    /* output array */
    fout << "\\begin{tabular}{rcl" << col_desc << "}\n";
    fout << tab << "\\hline\n";
    for(size_t u = 0; u < p.instrs.size(); u++)
    {
        const ddl_schedule_unit *unit = p.instrs[u];

        fout << tab;
        fout << "\\multirow{2}{*}{";
        for(size_t i = 0; i < unit->out().size(); i++)
        {
            fout << unit->out()[i];
            if((i + 1) != unit->out().size())
                fout << ",";
        }

        fout << "} & \\multirow{2}{*}{$\\leftarrow$} & \\multirow{2}{*}{";

        for(size_t i = 0; i < unit->in().size(); i++)
        {
            fout << unit->in()[i];
            if((i + 1) != unit->in().size())
                fout << ",";
        }
        fout << "}";

        unsigned reg_pressure = 0;
        for(size_t i = 0; i < info_map.size(); i++)
        {
            name_info& ni = info_map[i];
            std::pair< unsigned, unsigned > range;
            bool in_range = false;
            for(size_t j = 0; j < ni.live_ranges.size(); j++)
                if(ni.live_ranges[j].first < u && u <= ni.live_ranges[j].second)
                {
                    range = ni.live_ranges[i];
                    in_range = true;
                    break;
                }

            fout << " & ";
            if(!in_range)
            {
                if(ni.live_ranges.size() > 0 && u == ni.live_ranges[0].first)
                    fout << "\\multicolumn{1}{c}{" << rev_name_map[i] << "}";
            }
            else
            {
                fout << "\\multicolumn{1}{>{\\columncolor{red}[.5\\tabcolsep]}p{0.1cm}}{}";
                reg_pressure++;
            }
        }
        if(reg_pressure > max_reg_pressure)
            max_reg_pressure = reg_pressure;
        
        fout << "\\\\\n";

        fout << tab << " & &";

        reg_pressure = 0;
        for(size_t i = 0; i < info_map.size(); i++)
        {
            name_info& ni = info_map[i];
            std::pair< unsigned, unsigned > range;
            bool in_range = false;
            for(size_t j = 0; j < ni.live_ranges.size(); j++)
                if(ni.live_ranges[j].first <= u && u < ni.live_ranges[j].second)
                {
                    range = ni.live_ranges[i];
                    in_range = true;
                    break;
                }

            fout << " & ";
            if(!in_range)
                continue;

            reg_pressure++;
            fout << "\\multicolumn{1}{>{\\columncolor{red}[.5\\tabcolsep]}p{0.1cm}}{}";
        }
        if(reg_pressure > max_reg_pressure)
            max_reg_pressure = reg_pressure;

        fout << "\\\\\n";

        fout << tab << "\\hline\n";
    }
    
    fout << "\\end{tabular}\n";

    /* Output register pressure */
    fout << tab << "\\begin{center}Register pressure: " << max_reg_pressure << "\\end{center}\n";
    
    fout << "\\end{document}\n";
    
}

void build_ddl_program_from_schedule_units(const std::vector<const schedule_unit *>& v, ddl_program& p)
{
    p.instrs.clear();

    for(size_t i = 0; i < v.size(); i++)
        p.instrs.push_back(dynamic_cast< const ddl_schedule_unit * >(v[i]));
}

}
