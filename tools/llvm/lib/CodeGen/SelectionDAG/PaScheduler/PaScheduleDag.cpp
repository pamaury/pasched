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
#include <cstdio>
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
 * PaScheduleDAG
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

    std::set< pasched::schedule_dep::reg_t > compute_clobber_regs(SUnit *SU);
    bool ForceUnitLatencies() const { return true; }
};

class LLVMScheduleUnitBase : public pasched::schedule_unit
{
    public:
    LLVMScheduleUnitBase() {}
    virtual ~LLVMScheduleUnitBase() {}

    virtual bool is_llvm_unit() const = 0;
};

/* Real schedule unit */
class LLVMScheduleUnit : public LLVMScheduleUnitBase
{
    public:
    LLVMScheduleUnit(SUnit *SU, const std::string& label, std::set< pasched::schedule_dep::reg_t > clo)
        :m_SU(SU), m_label(label), m_clobber_regs(clo) {}
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
        /* FIXME: this is not quite good I think, it depends on how
         * implicit defs are handled */
        return 0;
    }

    SUnit *GetSU() const
    {
        return m_SU;
    }

    std::set< pasched::schedule_dep::reg_t > get_clobber_regs() const
    {
        return m_clobber_regs;
    }

    virtual bool is_llvm_unit() const { return true; }

    protected:
    
    SUnit *m_SU;
    std::string m_label;
    std::set< pasched::schedule_dep::reg_t > m_clobber_regs;
};

/* Fake unit to capture clobbered register */
class LLVMClobberRegCapture : public LLVMScheduleUnitBase
{
    public:
    LLVMClobberRegCapture() {}
    ~LLVMClobberRegCapture() {}

    virtual std::string to_string() const
    {
        return "Clobber";
    }

    virtual const LLVMClobberRegCapture *dup() const
    {
        return new LLVMClobberRegCapture(*this);
    }

    virtual const LLVMClobberRegCapture *deep_dup() const
    {
        return new LLVMClobberRegCapture(*this);
    }

    virtual unsigned internal_register_pressure() const
    {
        return 0;
    }

    virtual bool is_llvm_unit() const { return false; }
};

class dag_accumulator : public pasched::transformation
{
    public:
    dag_accumulator(bool do_deep_copy = true):m_deep(do_deep_copy) {}
    virtual ~dag_accumulator() {}

    virtual void transform(pasched::schedule_dag& dag, const pasched::scheduler& s, pasched::schedule_chain& c,
        pasched::transformation_status& status) const
    {
        status.begin_transformation();
        status.set_modified_graph(false);
        status.set_junction(false);
        status.set_deadlock(false);

        pasched::schedule_dag *d = m_deep ? dag.deep_dup() : dag.dup();
        /* accumulate */
        m_dag.add_units(d->get_units());
        m_dag.add_dependencies(d->get_deps());
        delete d;
        /* forward */
        s.schedule(dag, c);

        status.end_transformation();
    }

    pasched::schedule_dag& get_dag() { return m_dag; }

    protected:
    /* Little hack here. A transformation is not supposed to keep internal state but here
     * we want to accumulate DAGs scheduled so we keep a mutable var */
    mutable pasched::generic_schedule_dag m_dag;
    bool m_deep;
};

std::set< pasched::schedule_dep::reg_t > PaScheduleDAG::compute_clobber_regs(SUnit *SU)
{
    std::set< pasched::schedule_dep::reg_t > set;
    /* FIXME doesn't handle inline asm ? */
    for(SDNode *Node = SU->getNode(); Node; Node = Node->getFlaggedNode())
    {
        if(!Node->isMachineOpcode())
            continue;
        const TargetInstrDesc& TID = TII->get(Node->getMachineOpcode());
        if(!TID.ImplicitDefs)
          continue;
        for(const unsigned *Reg = TID.ImplicitDefs; *Reg; ++Reg)
            set.insert(*Reg);
    }

    return set;
}

void PaScheduleDAG::Schedule()
{
    // Build the scheduling graph.
    BuildSchedGraph(NULL);
    
    //dbgs() << "********** PaScheduleDAG **********\n";

    std::map< SUnit *, const LLVMScheduleUnit * > map; 

    /* allocate schedule units build a map of them */
    pasched::generic_schedule_dag dag;
    for(size_t u = 0; u < SUnits.size(); u++)
    {
        LLVMScheduleUnit *unit = new LLVMScheduleUnit(
            &SUnits[u],
            getGraphNodeLabel(&SUnits[u]),
            compute_clobber_regs(&SUnits[u]));
        dag.add_unit(unit);
        map[&SUnits[u]] = unit;
    }

    if(EntrySU.Succs.size() != 0)
        std::cout << "Entry: " << EntrySU.Succs.size() << "\n";
    if(ExitSU.Preds.size() != 0)
        std::cout << "Exit: " << ExitSU.Preds.size() << "\n";
    /* build the schedule graph */
    for(size_t u = 0; u < SUnits.size(); u++)
    {
        /* Explicit def/use */
        SUnit& unit = SUnits[u];
        for(size_t i = 0; i < unit.Succs.size(); i++)
        {
            SDep sdep = unit.Succs[i];
            pasched::schedule_dep dep;
            dep.set_from(map[&unit]);
            dep.set_to(map[sdep.getSUnit()]);
            if(sdep.getKind() == SDep::Data)
            {
                if(sdep.getReg() == 0)
                    dep.set_kind(pasched::schedule_dep::virt_dep);
                else
                    dep.set_kind(pasched::schedule_dep::phys_dep);
                dep.set_reg(sdep.getReg());
            }
            else if(sdep.getKind() == SDep::Order)
                dep.set_kind(pasched::schedule_dep::order_dep);
            else
                assert(false && "Unexpected dependency kind");
            dag.add_dependency(dep);
        }
        /* Clobbered registers */
        std::set< pasched::schedule_dep::reg_t > create = dag.get_reg_phys_create(map[&unit]);
        std::set< pasched::schedule_dep::reg_t > clob = map[&unit]->get_clobber_regs();
        LLVMClobberRegCapture *capture = 0;
        for(std::set< pasched::schedule_dep::reg_t >::iterator it = clob.begin(); it != clob.end(); ++it)
        {
            /* if the clobbered register is not in the create list, then route a dep to
             * clobber capture unit */
            if(create.find(*it) != create.end())
                continue;
            if(capture == 0)
            {
                capture = new LLVMClobberRegCapture;
                dag.add_unit(capture);
            }
            assert(capture != 0 && "no clobber capture node\n");
            dag.add_dependency(
                pasched::schedule_dep(
                    map[&unit],
                    capture,
                    pasched::schedule_dep::phys_dep, *it));
        }
    }

    //debug_view_dag(dag);

    /* build the transformation pipeline */
    pasched::transformation_pipeline pipeline;
    pasched::transformation_pipeline snd_stage_pipe;
    pasched::transformation_loop loop(&snd_stage_pipe);
    dag_accumulator after_unique_acc;
    pipeline.add_stage(new pasched::unique_reg_ids);
    pipeline.add_stage(&after_unique_acc);
    pipeline.add_stage(new pasched::handle_physical_regs);
    pipeline.add_stage(&loop);
    
    snd_stage_pipe.add_stage(new pasched::strip_dataless_units);
    snd_stage_pipe.add_stage(new pasched::strip_useless_order_deps);
    snd_stage_pipe.add_stage(new pasched::simplify_order_cuts);
    snd_stage_pipe.add_stage(new pasched::handle_physical_regs);
    snd_stage_pipe.add_stage(new pasched::split_def_use_dom_use_deps);
    snd_stage_pipe.add_stage(new pasched::smart_fuse_two_units(false, true));
    snd_stage_pipe.add_stage(new pasched::break_symmetrical_branch_merge);
    snd_stage_pipe.add_stage(new pasched::collapse_chains);
    snd_stage_pipe.add_stage(new pasched::split_merge_branch_units);

    /* build a basic fallback scheduler that captures also captures all "hard" graph and output them to a file */
    pasched::simple_rp_scheduler basic_sched;
    dag_accumulator fallback_accum;
    pasched::basic_status dummy_status;
    pasched::glued_transformation_scheduler fallback_sched(&fallback_accum, &basic_sched, dummy_status);
    #if 0
    pasched::mris_ilp_scheduler sched(&fallback_sched, 10000, true);
    #elif 1
    pasched::exp_scheduler sched(&fallback_sched, 10000, false);
    #else
    pasched::simple_rp_scheduler sched;
    #endif
    
    pasched::generic_schedule_chain chain;
    pasched::basic_status status;
    /* make a copy of the dag */
    pasched::schedule_dag *cpy = dag.dup();
    /* let's heat the cpu a bit */
    pipeline.transform(dag, sched, chain, status);
    /* Check the schedule */
    assert(chain.check_against_dag(after_unique_acc.get_dag()));
    /* fill output sequence with schedule */
    for(size_t i = 0; i < chain.get_unit_count(); i++)
    {
        const LLVMScheduleUnitBase *base = static_cast< const LLVMScheduleUnitBase * >(chain.get_unit_at(i));
        if(base->is_llvm_unit())
            Sequence.push_back(static_cast< const LLVMScheduleUnit * >(base)->GetSU());
    }
    /* output hard dag if any */
    if(fallback_accum.get_dag().get_units().size() != 0)
    {
        srand(time(NULL));
        std::string str;
        raw_string_ostream buffer(str);
        buffer << "dag_%" << rand() << rand() << ".lsd";
        buffer.flush();
        dump_schedule_dag_to_lsd_file(*cpy, str.c_str());

        // FIXME: ugly output, should use llvm system
        llvm::outs() << "Output hard DAG to " << str << "\n";
    }
    delete cpy;
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
