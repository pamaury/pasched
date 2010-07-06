#ifndef __PAMAURY_DLL_HPP__
#define __PAMAURY_DLL_HPP__

#include "config.hpp"
#include "sched-dag.hpp"
#include "sched-chain.hpp"

namespace PAMAURY_SCHEDULER_NS
{

/* Data Dependency Language schedule unit */
class ddl_schedule_unit : public schedule_unit
{
    public:
    ddl_schedule_unit() {}
    virtual ~ddl_schedule_unit() {}

    virtual std::string to_string() const;

    inline std::vector< std::string >& out() { return m_out; }
    inline const std::vector< std::string >& out() const { return m_out; }
    inline std::vector< std::string >& in() { return m_in; }
    inline const std::vector< std::string >& in() const { return m_in; }

    virtual const ddl_schedule_unit *dup() const;

    virtual unsigned internal_register_pressure() const;

    protected:
    std::vector< std::string > m_out;
    std::vector< std::string > m_in;
};

struct ddl_program
{
    std::vector< const ddl_schedule_unit * > instrs;
};

void load_ddl_program_from_ddl_file(const char *file, ddl_program& p);
void build_schedule_dag_from_ddl_program(const ddl_program& p, schedule_dag& dag);
void dump_ddl_program_analysis_to_tex_file(ddl_program& p, const char *filename);
void build_ddl_program_from_schedule_units(const std::vector<const schedule_unit *>& v, ddl_program& p);

}

#endif // __PAMAURY_DLL_HPP__
