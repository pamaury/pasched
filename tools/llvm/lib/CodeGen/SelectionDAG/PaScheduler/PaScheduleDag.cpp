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
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/Statistic.h"
#include <climits>
#include <cstdio>
#include <map>
#include <pasched.hpp>

using namespace llvm;

/**
 * PaScheduleDAG
 */
class LLVMScheduleUnit;

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

    std::set< pasched::schedule_dep::reg_t > ComputeClobberRegs(SUnit *SU);
    bool CheckPhysRegAndUpdate(
        const pasched::schedule_dag& dag,
        std::set< pasched::schedule_dep::reg_t >& phys_regs,
        const std::map< SUnit *, const LLVMScheduleUnit * >& name_map);
    EVT GetPhysicalRegisterVT(SDNode *N, unsigned Reg) const;
    SUnit *CloneInstruction(
        SUnit *su,
        const std::set< const pasched::schedule_unit * >& succs_to_fix,
        const std::map< SUnit *, const LLVMScheduleUnit * >& su_name_map);
    void BuildPaSchedGraph(
        pasched::schedule_dag& dag,
        std::map< SUnit *, const LLVMScheduleUnit * >& map,
        std::set< pasched::schedule_dep::reg_t >& phys_deps);
    
    bool ForceUnitLatencies() const { return true; }

    std::set< SUnit * > m_units_to_ignore;
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

std::set< pasched::schedule_dep::reg_t > PaScheduleDAG::ComputeClobberRegs(SUnit *SU)
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
        {
            set.insert(*Reg);
            for(const unsigned *Alias = TRI->getAliasSet(*Reg); *Alias; Alias++)
                set.insert(*Alias);
        }
    }

    return set;
}

EVT PaScheduleDAG::GetPhysicalRegisterVT(SDNode *N, unsigned Reg) const
{
    const TargetInstrDesc& TID = TII->get(N->getMachineOpcode());
    assert(TID.ImplicitDefs && "Physical reg def must be in implicit def list!");
    unsigned NumRes = TID.getNumDefs();
    for(const unsigned *ImpDef = TID.getImplicitDefs(); *ImpDef; ImpDef++)
    {
        if(Reg == *ImpDef)
            break;
        NumRes++;
    }
    return N->getValueType(NumRes);
}

SUnit *PaScheduleDAG::CloneInstruction(
    SUnit *su,
    const std::set< const pasched::schedule_unit * >& succs_to_fix,
    const std::map< SUnit *, const LLVMScheduleUnit * >& su_name_map)
{
    if(su->getNode()->getFlaggedNode())
        return NULL;
    /* Clone the offending node but before, check it's okay */
    SDNode *n = su->getNode();
    if(n == NULL)
        return NULL;
    bool try_unfold = false;
    for(unsigned i = 0; i < n->getNumValues(); i++)
    {
        EVT vt = n->getValueType(i);
        if(vt == MVT::Flag)
            return NULL;
        else if(vt == MVT::Other)
            try_unfold = true;
    }
    for(unsigned i = 0; i < n->getNumOperands(); i++)
    {
        const SDValue& op = n->getOperand(i);
        EVT vt = op.getNode()->getValueType(op.getResNo());
        if(vt == MVT::Flag)
            return NULL;
    }
    /* try unfolding */
    if(try_unfold)
    {
        SmallVector< SDNode*, 2 > new_nodes;
        if(!TII->unfoldMemoryOperand(*DAG, n, new_nodes))
            return NULL;
        assert(new_nodes.size() == 2 && "Expected a load folding node!");

        n = new_nodes[1];
        SDNode *load_node = new_nodes[0];
        unsigned num_vals = n->getNumValues();
        unsigned old_num_vals = su->getNode()->getNumValues();
        for(unsigned i = 0; i < num_vals; i++)
            DAG->ReplaceAllUsesOfValueWith(SDValue(su->getNode(), i), SDValue(n, i));
        
        DAG->ReplaceAllUsesOfValueWith(
            SDValue(su->getNode(), old_num_vals - 1),
            SDValue(load_node, 1));

        SUnit *new_su = NewSUnit(n);
        assert(n->getNodeId() == -1 && "Node already inserted!");
        n->setNodeId(new_su->NodeNum);

        const TargetInstrDesc& TID = TII->get(n->getMachineOpcode());
        for(unsigned i = 0; i < TID.getNumOperands(); i++)
        {
            if(TID.getOperandConstraint(i, TOI::TIED_TO) != -1)
            {
                new_su->isTwoAddress = true;
                break;
            }
        }
        if(TID.isCommutable())
            new_su->isCommutable = true;

        /* LoadNode may already exist. This can happen when there is another
         * load from the same location and producing the same type of value
         * but it has different alignment or volatileness. */
        bool is_new_load = true;
        SUnit *load_su;
        if(load_node->getNodeId() != -1)
        {
            load_su = &SUnits[load_node->getNodeId()];
            is_new_load = false;
        }
        else
        {
            load_su = NewSUnit(load_node);
            load_node->setNodeId(load_su->NodeNum);
        }

        SDep chain_pred;
        SmallVector<SDep, 4> chain_succs;
        SmallVector<SDep, 4> load_preds;
        SmallVector<SDep, 4> node_preds;
        SmallVector<SDep, 4> node_succs;
        for(SUnit::pred_iterator it = su->Preds.begin(); it != su->Preds.end(); ++it)
        {
            if(it->isCtrl())
                chain_pred = *it;
            else if(it->getSUnit()->getNode() && it->getSUnit()->getNode()->isOperandOf(load_node))
                load_preds.push_back(*it);
            else
                node_preds.push_back(*it);
        }
        for(SUnit::succ_iterator it = su->Succs.begin(); it != su->Succs.end(); ++it)
        {
            if(it->isCtrl())
                chain_succs.push_back(*it);
            else
                node_succs.push_back(*it);
        }

        if(chain_pred.getSUnit())
        {
            su->removePred(chain_pred);
            if(is_new_load)
                load_su->addPred(chain_pred);
        }
        for(unsigned i = 0; i < load_preds.size(); i++)
        {
            const SDep& pred = load_preds[i];
            su->removePred(pred);
            if(is_new_load)
                load_su->addPred(pred);
        }
        for(unsigned i = 0; i < node_preds.size(); i++)
        {
            const SDep& pred = node_preds[i];
            su->removePred(pred);
            new_su->addPred(pred);
        }
        for(unsigned i = 0; i < node_succs.size(); i++)
        {
            SDep d = node_succs[i];
            SUnit *succ_dep = d.getSUnit();
            d.setSUnit(su);
            succ_dep->removePred(d);
            d.setSUnit(new_su);
            succ_dep->addPred(d);
        }
        for(unsigned i = 0; i < chain_succs.size(); i++)
        {
            SDep d = chain_succs[i];
            SUnit *succ_dep = d.getSUnit();
            d.setSUnit(su);
            succ_dep->removePred(d);
            if(is_new_load)
            {
                d.setSUnit(load_su);
                succ_dep->addPred(d);
            }
        } 
        if(is_new_load)
            new_su->addPred(SDep(load_su, SDep::Order, load_su->Latency));

        /* old instruction is now ignored */
        m_units_to_ignore.insert(su);
        return new_su;
    }
    /* clone it */
    SUnit *new_su = Clone(su);
    /* it has the same predecessors */
    for(SUnit::pred_iterator it = su->Preds.begin(); it != su->Preds.end(); ++it)
    {
        if(!it->isArtificial())
            new_su->addPred(*it);
    }
    /* loop through all succs of A in LLVM dag */
    std::vector< std::pair< SUnit *, SDep > > to_delete;
    for(SUnit::succ_iterator it = su->Succs.begin(); it != su->Succs.end(); ++it)
    {
        /* if the unit is not tricky, don't do this */
        assert(su_name_map.find(it->getSUnit()) != su_name_map.end());
        if(succs_to_fix.find(su_name_map.find(it->getSUnit())->second) == succs_to_fix.end())
            continue;
        SUnit *succ_su = it->getSUnit();
        /* add a new dep from the result of the copy */
        SDep dep = *it;
        dep.setSUnit(new_su);
        succ_su->addPred(dep);
        /* add this dep to delete list */
        dep.setSUnit(su);
        to_delete.push_back(std::make_pair(succ_su, dep));
    }
    /* delete deps */
    for(size_t i = 0; i < to_delete.size(); i++)
        to_delete[i].first->removePred(to_delete[i].second);

    return new_su;
}

bool PaScheduleDAG::CheckPhysRegAndUpdate(
        const pasched::schedule_dag& dag,
        std::set< pasched::schedule_dep::reg_t >& phys_regs,
        const std::map< SUnit *, const LLVMScheduleUnit * >& su_name_map)
{
    /* loop throught each register */
    std::set< pasched::schedule_dep::reg_t >::iterator rit;
    for(rit = phys_regs.begin(); rit != phys_regs.end(); ++rit)
    {
        pasched::schedule_dep::reg_t reg = *rit;
        /* make a list of creators */
        std::vector< const pasched::schedule_unit * > creators;
        /* list of phys regs succ dep on R for each creator */
        std::vector< std::vector< const pasched::schedule_unit * > > creators_phys_succs;
        for(size_t i = 0; i < dag.get_units().size(); i++)
        {
            const pasched::schedule_unit *unit = dag.get_units()[i];
            std::set< pasched::schedule_dep::reg_t > set = dag.get_reg_phys_create(unit);
            if(set.find(reg) != set.end())
            {
                creators.push_back(unit);
                std::vector< const pasched::schedule_unit * > list;
                for(size_t j = 0; j < dag.get_succs(unit).size(); j++)
                {
                    const pasched::schedule_dep& dep = dag.get_succs(unit)[j];
                    if(dep.is_phys() && dep.reg() == reg)
                        list.push_back(dep.to());
                }
                creators_phys_succs.push_back(list);
            }
        }
        /* build a path map */
        std::vector< std::vector< bool > > path;
        std::map< const pasched::schedule_unit *, size_t > name_map;
        dag.build_path_map(path, name_map);
        /* consider each pair of creators and build a list of conclits */
        std::vector< std::pair< size_t, size_t > > conflicts;
        for(size_t i = 0; i < creators.size(); i++)
            for(size_t j = i + 1; j < creators.size(); j++)
            {
                /* a pair (U,V) is in conflict w.r.t to a phys R if there
                 * is a successor S(U) of U using R and a successor S(V)
                 * of V using R such as there is path from V to S(U) and
                 * one from S(U) to S(V)
                 * OR
                 * if there is a path from U to V and then from V to a
                 * a successor S(U) of U */
                 
                bool partial_i_j = false;
                bool partial_j_i = false;
                for(size_t k = 0; k < creators_phys_succs[j].size(); k++)
                {
                    const pasched::schedule_unit *succ = creators_phys_succs[j][k];
                    if(succ != creators[i] && path[name_map[creators[i]]][name_map[succ]])
                        partial_i_j = true;
                }
                for(size_t k = 0; k < creators_phys_succs[i].size(); k++)
                {
                    const pasched::schedule_unit *succ = creators_phys_succs[i][k];
                    if(succ != creators[j] && path[name_map[creators[j]]][name_map[succ]])
                        partial_j_i = true;
                }
                
                if(partial_i_j && partial_j_i)
                    conflicts.push_back(std::make_pair(i, j));
            }
        /* if no conflicts, we are done (for this register !) */
        if(conflicts.size() == 0)
            continue;
        /* only consider first conflict */
        const pasched::schedule_unit *a = creators[conflicts[0].first];
        const pasched::schedule_unit *b = creators[conflicts[0].second];
        /* we want to simulate a schedule so we need to virtually schedule either A
         * or B. It might be that only one order is possible if there is path between
         * A and B or between B and A, check that and make sure A is schedulable first */
        if(path[name_map[b]][name_map[a]])
            /* path between B and A: swap them */
            std::swap(a, b);
        /* virtually schedule all successors that depend on the phys R and that
         * do not require B to be scheduled */
        std::set< const pasched::schedule_unit * > tricky_units;
        for(size_t i = 0; i < dag.get_succs(a).size(); i++)
        {
            const pasched::schedule_dep& dep = dag.get_succs(a)[i];
            if(!dep.is_phys() || dep.reg() != reg)
                continue;
            if(!path[name_map[b]][name_map[dep.to()]])
                continue;
            tricky_units.insert(dep.to());
        }
        /* normally, at this point, there must be at least one tricky unit */
        assert(tricky_units.size() != 0 && "no tricky units but unschedulable DAG ?");
        /* compute some register classes */
        SUnit *su_a = static_cast< const LLVMScheduleUnit * >(a)->GetSU();
        EVT vt = GetPhysicalRegisterVT(su_a->getNode(), reg);
        const TargetRegisterClass *rc = TRI->getMinimalPhysRegClass(reg, vt);
        const TargetRegisterClass *dest_rc = TRI->getCrossCopyRegClass(rc);
        /* LLVM code says:
         * If cross copy register class is null, then it must be possible copy
         * the value directly.
         *
         * FIXME: cross copy class implementation is the same as LLVM and code suggest it
         * is buggy (call NewSUnit with NULL param) so we must do as LLVM and duplicate the node
         * :( */
        if(dest_rc)
        {
            SUnit *new_def = CloneInstruction(su_a, tricky_units, su_name_map);
            if(new_def)
                return true;
        }
        assert(false && "cannot clone unfold memory operand or clone unit, cross class copy is not implemented");
        return false;
    }

    return false;
}

void PaScheduleDAG::BuildPaSchedGraph(
    pasched::schedule_dag& dag,
    std::map< SUnit *, const LLVMScheduleUnit * >& map,
    std::set< pasched::schedule_dep::reg_t >& phys_deps)
{
    dag.clear();
    map.clear();
    phys_deps.clear();

    /* build name map */
    for(size_t u = 0; u < SUnits.size(); u++)
    {
        if(m_units_to_ignore.find(&SUnits[u]) != m_units_to_ignore.end())
            continue;
        
        LLVMScheduleUnit *unit = new LLVMScheduleUnit(
            &SUnits[u],
            getGraphNodeLabel(&SUnits[u]),
            ComputeClobberRegs(&SUnits[u]));
        dag.add_unit(unit);
        map[&SUnits[u]] = unit;
    }
    /* build the schedule graph */
    for(size_t u = 0; u < SUnits.size(); u++)
    {
        if(m_units_to_ignore.find(&SUnits[u]) != m_units_to_ignore.end())
            continue;
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
                {
                    dep.set_kind(pasched::schedule_dep::phys_dep);
                    phys_deps.insert(sdep.getReg());
                }
                dep.set_reg(sdep.getReg());
            }
            else if(sdep.getKind() == SDep::Order)
                dep.set_kind(pasched::schedule_dep::order_dep);
            else
                assert(false && "Unexpected dependency kind");

            dag.add_dependency(dep);
        }
    }
    /* Add clobbered */
    for(size_t u = 0; u < SUnits.size(); u++)
    {
        SUnit& unit = SUnits[u];
        if(m_units_to_ignore.find(&unit) != m_units_to_ignore.end())
            continue;
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
            /* don't add it if does not clobber a register actually in a phys dep */
            if(phys_deps.find(*it) == phys_deps.end())
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
}

void PaScheduleDAG::Schedule()
{
    // Build the scheduling graph.
    BuildSchedGraph(NULL);
    
    //dbgs() << "********** PaScheduleDAG **********\n";

    std::map< SUnit *, const LLVMScheduleUnit * > map; 

    /* allocate schedule units build a map of them */
    pasched::generic_schedule_dag dag;

    assert(EntrySU.Succs.size() == 0 && "not handled yet");
    assert(ExitSU.Preds.size() == 0 && "not handled yet");
    /* iterate as long as the current phys reg layout prevents a valid scheduling
     * and solve them with duplication or cross class copies */
    m_units_to_ignore.clear();
    while(true)
    {
        std::set< pasched::schedule_dep::reg_t > phys_deps; /* list of phys reg in deps */
        BuildPaSchedGraph(dag, map, phys_deps);
        /* check if it schedulable and adds instructions if not */
        if(CheckPhysRegAndUpdate(dag, phys_deps, map))
        {
            for(size_t i = 0; i < dag.get_units().size(); i++)
                delete dag.get_units()[i];
        }
        else
            break;
    }

    /* build the transformation pipeline */
    pasched::transformation_pipeline pipeline;
    pasched::transformation_pipeline snd_stage_pipe;
    pasched::transformation_loop loop(&snd_stage_pipe);
    dag_accumulator after_unique_acc(false);
    pipeline.add_stage(new pasched::unique_reg_ids);
    pipeline.add_stage(&after_unique_acc);
    pipeline.add_stage(new pasched::handle_physical_regs(true));
    //pipeline.add_stage(&loop);
    
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
    #elif 0
    pasched::exp_scheduler sched(&fallback_sched, 10000, false);
    #elif 1
    pasched::simple_rp_scheduler sched;
    #else
    pasched::rand_scheduler sched;
    #endif
    
    pasched::generic_schedule_chain chain;
    pasched::basic_status status;
    /* let's heat the cpu a bit */
    pipeline.transform(dag, sched, chain, status);
    /* Check the schedule */
    if(!chain.check_against_dag(after_unique_acc.get_dag()))
    {
        debug_view_scheduled_dag(after_unique_acc.get_dag(), chain);
        assert(false);
    }
    /* fill output sequence with schedule */
    for(size_t i = 0; i < chain.get_unit_count(); i++)
    {
        const LLVMScheduleUnitBase *base = static_cast< const LLVMScheduleUnitBase * >(chain.get_unit_at(i));
        if(base->is_llvm_unit())
        {
            SUnit *su = static_cast< const LLVMScheduleUnit * >(base)->GetSU();
            Sequence.push_back(su);
        }
    }

    /* Check schedule size */
    size_t dead_nodes = 0;
    for(size_t i = 0; i < SUnits.size(); i++)
    {
        if(m_units_to_ignore.find(&SUnits[i]) != m_units_to_ignore.end())
            dead_nodes++;
    }
    assert(Sequence.size() + dead_nodes == SUnits.size() && "Invalid output schedule sequence size");

    /* output hard dag if any */
    if(fallback_accum.get_dag().get_units().size() != 0)
    {
        srand(time(NULL));
        std::string str;
        raw_string_ostream buffer(str);
        buffer << "dag_%" << rand() << rand() << ".lsd";
        buffer.flush();
        dump_schedule_dag_to_lsd_file(after_unique_acc.get_dag(), str.c_str());

        // FIXME: ugly output, should use llvm system
        llvm::outs() << "Output hard DAG to " << str << "\n";
    }
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
}

static RegisterScheduler
  tdPaDAGScheduler("pa-sched", "Pamaury scheduler",
                     createPaDAGScheduler);
