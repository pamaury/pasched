#define DEBUG_TYPE "pa-sched"
#include "../ScheduleDAGSDNodes.h"
#include "llvm/CodeGen/LatencyPriorityQueue.h"
#include "llvm/CodeGen/ScheduleHazardRecognizer.h"
#include "llvm/CodeGen/SchedulerRegistry.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/PriorityQueue.h"
#include "llvm/ADT/Statistic.h"
#include <climits>
#include <map>
#include <pasched.hpp>

using namespace llvm;

/**
 * PaDummyScheduleDAG
 */

class PaDummyScheduleDAG : public ScheduleDAGSDNodes
{
private:
    /// HazardRec - The hazard recognizer to use.
    ScheduleHazardRecognizer *HazardRec;

public:
    PaDummyScheduleDAG(MachineFunction &mf,
                  ScheduleHazardRecognizer *HR)
        : ScheduleDAGSDNodes(mf), HazardRec(HR)
    {
    }

    ~PaDummyScheduleDAG()
    {
        delete HazardRec;
    }

    void Schedule();

private:

};

void PaDummyScheduleDAG::Schedule()
{
    // Build the scheduling graph.
    BuildSchedGraph(NULL);
    
    dbgs() << "********** PaDummyScheduleDAG **********\n";
    
    for(unsigned u = 0; u < SUnits.size(); u++)
    {
        SUnit& unit = SUnits[u];
        std::string name = getGraphNodeLabel(&unit);
        
        size_t pos = 0;
        while((pos = name.find('\n', pos)) != std::string::npos)
        {
            name.replace(pos, 1, "\\\n");
            pos += 2;
        }
        dbgs() << "Unit " << &unit << " Name " << name << "\n";
        for(unsigned i = 0; i < unit.Preds.size(); i++)
        {
            SDep& d = unit.Preds[i];
            dbgs() << "To " << d.getSUnit() << " Latency " << d.getLatency() << " Kind ";
            switch(d.getKind())
            {
                case SDep::Data: dbgs() << "data Reg " << (unsigned long)d.getReg() << "\n"; break;
                case SDep::Anti: dbgs() << "anti Reg " << (unsigned long)d.getReg() << "\n"; break;
                case SDep::Output: dbgs() << "out Reg " << (unsigned long)d.getReg() << "\n"; break;
                case SDep::Order: dbgs() << "order\n";
                default: break;
            }
        }
    }
}

/**
 *
 */

class PaScheduleDAG : public ScheduleDAGSDNodes
{
private:
    /// HazardRec - The hazard recognizer to use.
    ScheduleHazardRecognizer *HazardRec;

public:
    PaScheduleDAG(MachineFunction &mf,
                  ScheduleHazardRecognizer *HR)
        : ScheduleDAGSDNodes(mf), HazardRec(HR)
    {
    }

    ~PaScheduleDAG()
    {
        delete HazardRec;
    }

    void Schedule();

private:

};

class LLVMScheduleUnit : public pasched::schedule_unit
{
    public:
    LLVMScheduleUnit(SUnit *SU, const std::string& label)
        :m_SU(SU), m_label(label) {}
    ~LLVMScheduleUnit() {}

    virtual std::string to_string() const
    {
        return m_label;
    }

    virtual const LLVMScheduleUnit *dup() const
    {
        return new LLVMScheduleUnit(*this);
    }

    virtual const LLVMScheduleUnit *deep_dup() const
    {
        return new LLVMScheduleUnit(*this);
    }

    virtual unsigned internal_register_pressure() const
    {
        return 0;
    }

    SUnit *GetSU() const
    {
        return m_SU;
    }

    protected:
    SUnit *m_SU;
    std::string m_label;
};

void PaScheduleDAG::Schedule()
{
    // Build the scheduling graph.
    BuildSchedGraph(NULL);
    
    //dbgs() << "********** PaScheduleDAG **********\n";

    std::map< SUnit *, const LLVMScheduleUnit * > map; 
    
    pasched::generic_schedule_dag dag;
    for(size_t u = 0; u < SUnits.size(); u++)
    {
        LLVMScheduleUnit *unit = new LLVMScheduleUnit(&SUnits[u], getGraphNodeLabel(&SUnits[u]));
        dag.add_unit(unit);
        map[&SUnits[u]] = unit;
    }

    for(size_t u = 0; u < SUnits.size(); u++)
    {
        SUnit& unit = SUnits[u];
        for(size_t i = 0; i < unit.Succs.size(); i++)
        {
            SDep sdep = unit.Succs[i];
            pasched::schedule_dep dep;
            dep.set_from(map[&unit]);
            dep.set_to(map[sdep.getSUnit()]);
            if(sdep.getKind() == SDep::Data)
            {
                dep.set_kind(pasched::schedule_dep::data_dep);
                dep.set_reg(sdep.getReg());
            }
            else if(sdep.getKind() == SDep::Order)
                dep.set_kind(pasched::schedule_dep::order_dep);
            else
                assert(false && "Unexpected dependency kind");
            dag.add_dependency(dep);
        }
    }

    pasched::transformation_pipeline pipeline;
    pasched::transformation_pipeline snd_stage_pipe;
    pasched::transformation_loop loop(&snd_stage_pipe);
    pipeline.add_stage(new pasched::unique_reg_ids);
    pipeline.add_stage(&loop);
    
    snd_stage_pipe.add_stage(new pasched::strip_dataless_units);
    snd_stage_pipe.add_stage(new pasched::strip_useless_order_deps);
    snd_stage_pipe.add_stage(new pasched::simplify_order_cuts);
    snd_stage_pipe.add_stage(new pasched::split_def_use_dom_use_deps);
    snd_stage_pipe.add_stage(new pasched::smart_fuse_two_units(false, true));
    snd_stage_pipe.add_stage(new pasched::break_symmetrical_branch_merge);
    snd_stage_pipe.add_stage(new pasched::collapse_chains);
    snd_stage_pipe.add_stage(new pasched::split_merge_branch_units);

    #if 0
    pasched::simple_rp_scheduler basic_sched;
    pasched::mris_ilp_scheduler sched(&basic_sched, 1000, true);
    #elif 1
    pasched::simple_rp_scheduler basic_sched;
    pasched::exp_scheduler sched(&basic_sched, 0000, false);
    #else
    pasched::simple_rp_scheduler sched;
    #endif
    
    pasched::generic_schedule_chain chain;
    pasched::basic_status status;
    /* rememember dag for later check */
    pipeline.transform(dag, sched, chain, status);
    /* check chain against dag */
    for(size_t i = 0; i < chain.get_unit_count(); i++)
        Sequence.push_back(static_cast< const LLVMScheduleUnit * >(chain.get_unit_at(i))->GetSU());
}

/**
 * Misc
 */

namespace llvm
{
    ScheduleDAGSDNodes *
    createPaDAGScheduler(SelectionDAGISel *IS, CodeGenOpt::Level)
    {
        return new PaScheduleDAG(*IS->MF, IS->CreateTargetHazardRecognizer());
    }

    ScheduleDAGSDNodes *
    createPaDummyDAGScheduler(SelectionDAGISel *IS, CodeGenOpt::Level)
    {
        return new PaDummyScheduleDAG(*IS->MF, IS->CreateTargetHazardRecognizer());
    }
}

static RegisterScheduler
  tdPaDAGScheduler("pa-sched", "Pamaury scheduler",
                     createPaDAGScheduler);
static RegisterScheduler
  tdPaDummyDAGScheduler("pa-dummy-sched", "Pamaury dummy scheduler",
                     createPaDummyDAGScheduler);
