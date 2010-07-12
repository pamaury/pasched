#include <sched-transform.hpp>
#include <sstream>
#include <stdexcept>

namespace PAMAURY_SCHEDULER_NS
{


/**
 * schedule_dag_tranformation
 */
transformation::transformation()
{
}

transformation::~transformation()
{
}

/**
 * glued_transformation_scheduler
 */

glued_transformation_scheduler::glued_transformation_scheduler(const transformation *transform, const scheduler *sched)
    :m_transform(transform), m_scheduler(sched)
{
}

glued_transformation_scheduler::~glued_transformation_scheduler()
{
}

void glued_transformation_scheduler::schedule(schedule_dag& d, schedule_chain& c) const
{
    m_transform->transform(d, *m_scheduler, c);
}

/**
 * packed_transformation
 */
packed_transformation::packed_transformation(const transformation *first, const transformation *second)
    :m_first(first), m_second(second)
{
}

packed_transformation::~packed_transformation()
{
}

void packed_transformation::transform(schedule_dag& d, const scheduler& s, schedule_chain& c) const
{
    glued_transformation_scheduler internal_sched(m_second, &s);
    m_first->transform(d, internal_sched, c);
}

/**
 * transformation_pipeline
 */

transformation_pipeline::transformation_pipeline()
{
}

transformation_pipeline::~transformation_pipeline()
{
    for(size_t i = 0; i < m_packers.size(); i++)
        delete m_packers[i];
}

void transformation_pipeline::add_stage(const transformation *transform)
{
    m_pipeline.push_back(transform);
    m_packers.push_back(new packed_transformation(m_packers.back(), transform));
}

void transformation_pipeline::transform(schedule_dag& d, const scheduler& s, schedule_chain& c) const
{
    if(m_packers.size() == 0)
        throw std::runtime_error("transformation_pipeline::transform called with empty pipeline");
    m_packers.back()->transform(d, s, c);
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

void unique_reg_ids::transform(schedule_dag& dag, const scheduler& s, schedule_chain& c) const
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

    s.schedule(dag, c);
}


}

