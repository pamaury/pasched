#include "sched-dag.hpp"
#include "tools.hpp"
#include <fstream>
#include <stdexcept>
#include <sstream>

namespace PAMAURY_SCHEDULER_NS
{

namespace
{
    std::string escape_label(const std::string& s)
    {
        bool multilines = s.find('\n') != std::string::npos;
        std::ostringstream oss;

        if(multilines)
            oss << "{";

        for(size_t i = 0; i < s.size(); i++)
        {
            switch(s[i])
            {
                case '\n': oss << " | "; break;
                case ' ': oss << "\\ "; break;
                case '<': oss << "\\<"; break;
                case '>': oss << "\\>"; break;
                case '[': oss << "\\["; break;
                case ']': oss << "\\]"; break;
                case '|': oss << "\\|"; break;
                case '{': oss << "\\{"; break;
                case '}': oss << "\\}"; break;
                case '"': oss << "\\\""; break;
                default: oss << s[i]; break;
            }
        }

        if(multilines)
            oss << "}";
        return oss.str();
    }

    std::string type_from_label(const std::string& s)
    {
        if(s.find('\n') == std::string::npos)
            return "box";
        else
            return "Mrecord";
    }

    void emit_node_color(std::ofstream& fout, const std::string& tab,
        const std::vector< dag_printer_opt >& opts, const schedule_unit *unit)
    {
        for(size_t i = 0; i < opts.size(); i++)
        {
            if(opts[i].type != dag_printer_opt::po_color_node)
                continue;
            if(opts[i].color_node.unit != unit)
                continue;
            fout << tab << "color = \"" << opts[i].color_node.color << "\"\n";
        }
    }

    void emit_dep_color_and_style(std::ofstream& fout, const std::string& tab,
        const std::vector< dag_printer_opt >& opts, const schedule_dep& dep)
    {
        bool has_color = false;
        for(size_t i = 0; i < opts.size(); i++)
        {
            if(opts[i].type != dag_printer_opt::po_color_dep)
                continue;
            if(opts[i].color_dep.dep != dep)
                continue;
            fout << tab << tab << "color = \"" << opts[i].color_dep.color << "\"\n";
            has_color = true;
            break;
        }

        if(dep.kind() == schedule_dep::order_dep)
        {
            if(!has_color)
                fout << tab << tab << "color = blue\n";
            fout << tab << tab << "style = dashed\n";
        }
        else if(dep.kind() == schedule_dep::phys_dep)
        {
            if(!has_color)
                fout << tab << tab << "color = red\n";
            fout << tab << tab << "arrowhead = odiamond\n";
        }
    }
}

void dump_schedule_dag_to_dot_file(const schedule_dag& dag, const char *filename,
    const std::vector< dag_printer_opt >& opts)
{
    std::string tab = "    ";
    std::ofstream fout(filename, std::ios::out | std::ios::trunc);
    if(!fout)
        throw std::runtime_error("cannot open file '" + std::string(filename) + "' for writing");

    fout << "digraph G {\n";

    /* enumerate nodes */
    std::map< const schedule_unit *, std::string > name_map;
    for(size_t i = 0; i < dag.get_units().size(); i++)
    {
        const schedule_unit *unit = dag.get_units()[i];
        std::ostringstream oss;

        oss << "node" << i;
        
        name_map[unit] = oss.str();

        std::string label = unit->to_string();
        if(label.find("<-") != std::string::npos)
            label.replace(label.find("<-"), 2, "&larr;");
        
        fout << tab << oss.str() << " [\n";
        fout << tab << tab << "label = \"" << escape_label(label) << "\"\n";
        fout << tab << tab << "shape = " << type_from_label(label) << "\n";
        fout << tab << tab << "style = rounded\n";
        emit_node_color(fout, tab, opts, unit);
        fout << tab << "];\n";
    }

    /* enumerate edges */
    for(size_t i = 0; i < dag.get_deps().size(); i++)
    {
        const schedule_dep& dep = dag.get_deps()[i];

        std::ostringstream oss;

        switch(dep.kind())
        {
            case schedule_dep::virt_dep:
                oss << "r" << dep.reg();
                break;
            case schedule_dep::phys_dep:
                oss << "p" << dep.reg();
                break;
            case schedule_dep::order_dep: oss << "order"; break;
            default: oss << "?"; break;
        }

        fout << tab << name_map[dep.from()] << " -> " << name_map[dep.to()] << "[\n";
        fout << tab << tab << "label = \"" << oss.str() << "\"\n";
        emit_dep_color_and_style(fout, tab, opts, dep);
        
        fout << tab << "];\n";
    }

    fout << "}\n";
    
    fout.close();
}

}
