#include <sched-transform.hpp>
#include <sstream>
#include <stdexcept>

namespace PAMAURY_SCHEDULER_NS
{

/**
 * schedule_chain_transformation
 */
schedule_chain_transformation::schedule_chain_transformation()
{
}

schedule_chain_transformation::~schedule_chain_transformation()
{
}

/**
 * dummy_schedule_chain_transformation
 */
dummy_schedule_chain_transformation::dummy_schedule_chain_transformation()
{
}

dummy_schedule_chain_transformation::~dummy_schedule_chain_transformation()
{
}

void dummy_schedule_chain_transformation::transform(schedule_chain&) const
{
}

/**
 * chain_expander
 */
chain_expander::chain_expander()
{
}

chain_expander::~chain_expander()
{
}

void chain_expander::transform(schedule_chain&) const
{
    throw std::runtime_error("chain_expander_schedule_chain_transformation::transform is unimplemented");
}

const std::vector< const chain_schedule_unit * >& chain_expander::expand_list() const
{
    return m_expand_list;
}

std::vector< const chain_schedule_unit * >& chain_expander::expand_list()
{
    return m_expand_list;
}

/**
 * schedule_dag_tranformation
 */
schedule_dag_tranformation::schedule_dag_tranformation()
{
}

schedule_dag_tranformation::~schedule_dag_tranformation()
{
}

/**
 * unique_reg_ids
 */

unique_reg_ids::unique_reg_ids()
{
}

unique_reg_ids::~unique_reg_ids()
{
}

const schedule_chain_transformation *unique_reg_ids::transform(schedule_dag& dag) const
{
    std::vector< schedule_dep > to_remove;
    std::vector< schedule_dep > to_add;
    unsigned reg_idx = 1;
    
    for(size_t u = 0; u < dag.get_units().size(); u++)
    {
        const schedule_unit *unit = dag.get_units()[u];
        std::map< unsigned, unsigned > reg_map;
        
        for(size_t i = 0; i < dag.get_succs(unit).size(); i++)
        {
            schedule_dep dep = dag.get_succs(unit)[i];
            to_remove.push_back(dep);

            if(reg_map.find(dep.reg()) == reg_map.end())
                reg_map[dep.reg()] = reg_idx++;
            
            dep.set_reg(reg_map[dep.reg()]);
            to_add.push_back(dep);
        }
    }

    dag.remove_dependencies(to_remove);
    dag.add_dependencies(to_add);

    return new dummy_schedule_chain_transformation();
}


}

