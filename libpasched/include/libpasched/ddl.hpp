#ifndef __PAMAURY_DLL_HPP__
#define __PAMAURY_DLL_HPP__

#include "config.hpp"
#include "sched-dag.hpp"
#include "sched-chain.hpp"

namespace PAMAURY_SCHEDULER_NS
{

/**
 * A DDL(Data Dependency Language) file is composed of a list of
 * instruction. As the goal of such a file is only to represent the data
 * dependencies, each instruction is of the form
 * <reg list> '<-' <reg list>
 * A (possibly empty) list of register is a list of comma(',') separated
 * names which should not contained white space.
 *
 * Example:

a <-
b <-
d <- a
c <- a, b
e <- c, d
<- e

 * Such a file can only model data dependencies and will not produce
 * order dependencies.
 */

/**
 * Data Dependency Language schedule unit
 */
class ddl_schedule_unit : public schedule_unit
{
    public:
    /** schedule_unit interface */ 
    ddl_schedule_unit() {}
    virtual ~ddl_schedule_unit() {}

    virtual std::string to_string() const;

    virtual const ddl_schedule_unit *dup() const;

    virtual unsigned internal_register_pressure() const;

    /** ddl_schedule_unit specific part */
    virtual std::vector< std::string >& out();
    virtual const std::vector< std::string >& out() const;
    virtual std::vector< std::string >& in();
    virtual const std::vector< std::string >& in() const;

    protected:
    std::vector< std::string > m_out;
    std::vector< std::string > m_in;
};

/**
 * Represent a linear DDL program
 */
struct ddl_program
{
    std::vector< const ddl_schedule_unit * > instrs;
};

/**
 * Read a DDL from a file
 */
void load_ddl_program_from_ddl_file(const char *file, ddl_program& p);

/**
 * Build a schedule DAG from a DDL program by building data dependencies
 */
void build_schedule_dag_from_ddl_program(const ddl_program& p, schedule_dag& dag);

/**
 * Analyse a DDL program and output live variable analysis to a TEX file
 */
void dump_ddl_program_analysis_to_tex_file(ddl_program& p, const char *filename);

/**
 * Build a DDL program from a list of schedule units
 * 
 * NOTE: this will upcast units to ddl_schedule_unit units
 */
void build_ddl_program_from_schedule_units(const std::vector<const schedule_unit *>& v, ddl_program& p);

}

#endif // __PAMAURY_DLL_HPP__
