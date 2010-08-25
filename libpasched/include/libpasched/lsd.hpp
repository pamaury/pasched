#ifndef __PAMAURY_LSD_HPP__
#define __PAMAURY_LSD_HPP__

#include "config.hpp"
#include "sched-dag.hpp"
#include "sched-chain.hpp"

namespace PAMAURY_SCHEDULER_NS
{

/**
 * A LSD(LLVM Schedule DAG) file is a way to represent a schedule graph as
 * internally stored by LLVM. such a file has the following structure.
 * In is composed by a list of Unit Descriptions:
 * <Unit Desc 1>
 * <Unit Desc 2>
 * ...
 *
 * Each Unit Description has two parts:
 * - description of the unit itself:
 *   'Unit' <unit ID> 'Name' <unit name>
 *   If the <unit name> uses several line, a '\' must be used as the last character of the line
 * - list of successor dependecies which can be of two forms:
 *   'To' <unit ID> 'Latency' <latency> 'Kind' 'data 'Reg' <reg ID>
 *   'To' <unit ID> 'Latency' <latency> 'Kind' 'order'
 *
 * Example:

Unit Unit0 Name This is the name of Unit0
To Unit1 Latency 1 Kind data Reg 1
To Unit1 Latency 1 Kind order
To Unit2 Latency 1 Kind data Reg 2
Unit Unit1 Name This is the name of Unit1\
and it can use several lines
To Unit2 Latency 1 Kind data Reg 3
Unit Unit2 Name A\
Random\
Name

 * The latency information is currently not used at all, the unit ID
 * is also dropped and only used within the file. The unit name is used
 * for the to_string method.
 * 
 */

/**
 * LLVM Schedule DAG schedule unit
 */
class lsd_schedule_unit : public schedule_unit
{
    public:
    lsd_schedule_unit(const std::string& id) : m_id(id), m_irp(0) {}
    virtual ~lsd_schedule_unit() {}

    virtual std::string to_string() const;

    const std::string& name() const { return m_name; }
    std::string& name() { return m_name; }

    virtual const lsd_schedule_unit *dup() const;
    virtual const lsd_schedule_unit *deep_dup() const;

    virtual unsigned internal_register_pressure() const;
    virtual void set_internal_register_pressure(unsigned irp);

    protected:
    std::string m_id;
    std::string m_name;
    unsigned m_irp;
};

/**
 * Build a schedule DAG from a LSD file
 */
void build_schedule_dag_from_lsd_stream(std::istream& is, schedule_dag& dag);
void build_schedule_dag_from_lsd_file(const char *filename, schedule_dag& dag);
/**
 * Output a DAG to a LSD file
 */
void dump_schedule_dag_to_lsd_stream(const schedule_dag& dag, std::ostream& os);
void dump_schedule_dag_to_lsd_file(const schedule_dag& dag, const char *filename);

}

#endif // __PAMAURY_LSD_HPP__
