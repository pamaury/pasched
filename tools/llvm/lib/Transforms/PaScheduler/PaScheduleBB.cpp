#define DEBUG_TYPE "pa-sched"
#include "llvm/Pass.h"
#include "llvm/Function.h"
#include "llvm/BasicBlock.h"
#include "llvm/Instruction.h"
#include "llvm/Instructions.h"
#include "llvm/Operator.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AliasSetTracker.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/raw_os_ostream.h"
#include <climits>
#include <map>
#include <sstream>
#include <pasched.hpp>

using namespace llvm;

namespace
{

class LLVMScheduleUnit : public pasched::schedule_unit
{
    public:
    /* instruction may be null for dummy instructions */
    LLVMScheduleUnit(Instruction *I, const std::string& label)
        :m_inst(I), m_label(label) {}
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

    Instruction *get_instruction() const
    {
        return m_inst;
    }

    protected:
    Instruction *m_inst;
    std::string m_label;
};

class dag_accumulator : public pasched::transformation
{
    public:
    dag_accumulator() {}
    virtual ~dag_accumulator() {}

    virtual void transform(pasched::schedule_dag& dag, const pasched::scheduler& s, pasched::schedule_chain& c,
        pasched::transformation_status& status) const
    {
        status.begin_transformation();
        status.set_modified_graph(false);
        status.set_junction(false);
        status.set_deadlock(false);
        
        pasched::schedule_dag *d = dag.deep_dup();
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
};
    
struct PaScheduler : public BasicBlockPass
{
    static char ID;
    PaScheduler()
        :BasicBlockPass(&ID) {}

    virtual void getAnalysisUsage(AnalysisUsage& AU) const
    {
        AU.addRequired< AliasAnalysis >();
    }

    virtual bool runOnBasicBlock(BasicBlock& BB)
    {
        AliasAnalysis *AA = &getAnalysis< AliasAnalysis >();
        AliasSetTracker *AST = new AliasSetTracker(*AA);
        std::map< Instruction *, const LLVMScheduleUnit * > map;
        std::map< Instruction *, size_t > map_pos;
        std::map< Value *, pasched::schedule_dep::reg_t > reg_map;
        LLVMScheduleUnit *dummy_in = new LLVMScheduleUnit(0, "Dummy IN");
        LLVMScheduleUnit *dummy_out = new LLVMScheduleUnit(0, "Dummy OUT");
        pasched::generic_schedule_dag dag;

        #define ADD_REG(reg_name, value) \
            if(reg_map.find(value) == reg_map.end()) \
                reg_map[value] = pasched::schedule_dep::generate_unique_reg_id(); \
            pasched::schedule_dep::reg_t reg_name = reg_map[value];

        dag.add_unit(dummy_in);
        dag.add_unit(dummy_out);

        #if 0
        Instruction *last_mem_inst = 0;
        Instruction *last_throw = 0;
        #endif
        //dbgs() << "BasicBlock: " << BB.getName() << "\n";
        size_t pos = 0;
        for(BasicBlock::iterator DI = BB.begin(); DI != BB.end(); pos++)
        {
            Instruction *inst = DI++;
            if(isa<PHINode>(inst))
                continue;
            std::ostringstream oss;
            raw_os_ostream os(oss);
            os.SetUnbuffered();
            os << *inst;
            map[inst] = new LLVMScheduleUnit(inst, oss.str());
            map_pos[inst] = pos;
            dag.add_unit(map[inst]);
            /* add deps to schedule before dummies */
            dag.add_dependency(
                pasched::schedule_dep(
                    dummy_in,
                    map[inst],
                    pasched::schedule_dep::order_dep));
            dag.add_dependency(
                pasched::schedule_dep(
                    map[inst],
                    dummy_out,
                    pasched::schedule_dep::order_dep));

            User *user = dyn_cast< User >(inst);
            if(user)
            {
                for(unsigned i = 0; i < user->getNumOperands(); i++)
                {
                    Instruction *to = dyn_cast< Instruction >(user->getOperand(i));
                    /* reference to a mapped instruction inside the BB -> OK */
                    if(to && map.find(to) != map.end())
                    {
                        ADD_REG(reg, user->getOperand(i))
                        
                        dag.add_dependency(
                            pasched::schedule_dep(
                                map[to],
                                map[inst],
                                pasched::schedule_dep::data_dep,
                                reg));
                    }
                    /* reference to an instruction inside or outside the BB -> OK */
                    else if(isa<Instruction>(user->getOperand(i)))
                    {
                        assert(user->getOperand(i)->getType()->isSized() && "Reference to unsized typed instruction");

                        ADD_REG(reg, user->getOperand(i))
                        
                        dag.add_dependency(
                            pasched::schedule_dep(
                                dummy_in,
                                map[inst],
                                pasched::schedule_dep::data_dep,
                                reg));
                    }
                    /* reference to an operator -> ignore */
                    else if(isa<Operator>(user->getOperand(i)))
                    {
                        //dbgs() << "operator: " << *user->getOperand(i) << "\n";
                    }
                    /* other: ignore */
                    else if(user->getOperand(i)->getType()->isSized())
                    {
                        //dbgs() << "outside ref: " << *user->getOperand(i) << "\n";
                    }
                }
            }
            /* terminator */
            TerminatorInst *terminator = dyn_cast< TerminatorInst >(inst);
            if(terminator)
            {
                /* add dependencies to all other instructions */
                for(size_t u = 0; u < dag.get_units().size(); u++)
                {
                    const pasched::schedule_unit *unit = dag.get_units()[u];
                    if(unit == dummy_in || unit == dummy_out || unit == map[inst])
                        continue;
                    dag.add_dependency(
                        pasched::schedule_dep(
                            unit,
                            map[inst],
                            pasched::schedule_dep::order_dep));
                }
            }
            #if 0
            /* check for memory operations */
            if(inst->mayReadFromMemory() || inst->mayWriteToMemory())
            {
                /* add link to last memory operation if any */
                if(last_mem_inst)
                    dag.add_dependency(
                        pasched::schedule_dep(
                            map[last_mem_inst],
                            map[inst],
                            pasched::schedule_dep::order_dep));
                last_mem_inst = inst;
            }
            /* check for side effects */
            if(inst->mayHaveSideEffects())
            {
                /* schedule after last throw */
                if(last_throw)
                    dag.add_dependency(
                        pasched::schedule_dep(
                            map[last_throw],
                            map[inst],
                            pasched::schedule_dep::order_dep));
            }
            /* check for throw */
            if(inst->mayThrow())
            {
                /* enforce order for instructions before */
                BasicBlock::iterator temp_it = BB.begin();
                while(temp_it != DI)
                {
                    Instruction *temp_inst = temp_it++;
                    if(map.find(temp_inst) != map.end())
                        dag.add_dependency(
                            pasched::schedule_dep(
                                map[temp_inst],
                                map[inst],
                                pasched::schedule_dep::order_dep));
                }
                last_throw = inst;
            }
            #endif
            /*
            dbgs() << "  " << inst << ":  " << *inst;
            User *user = dyn_cast< User >(inst);
            if(user)
            {
                dbgs() << "{uses " << user->getNumOperands() << " operands:";
                for(unsigned i = 0; i < user->getNumOperands(); i++)
                    dbgs() << " " << user->getOperand(i);
                dbgs() << "}";
            }
            dbgs() << "\n";
            */
        }

        for(BasicBlock::iterator DI = BB.begin(); DI != BB.end();)
        {
            Instruction *inst = DI++;
            if(isa<PHINode>(inst))
                continue;
            AST->add(inst);
        }

        for(AliasSetTracker::iterator it = AST->begin(); it != AST->end(); ++it)
        {
            AliasSet& AS = *it;
            if(AS.isForwardingAliasSet())
                continue;
            //dbgs() << "AliasSet:\n";
            std::vector< Instruction * > list;
            for(AliasSet::iterator it2 = AS.begin(); it2 != AS.end(); ++it2)
            {
                //dbgs() << "  -- " << *it2.getPointer();
                for(BasicBlock::iterator DI = BB.begin(); DI != BB.end();)
                {
                    Instruction *inst = DI++;
                    if(isa<PHINode>(inst))
                        continue;
                    User *user = dyn_cast< User >(inst);
                    if(!user)
                        continue;
                    for(unsigned i = 0; i < user->getNumOperands(); i++)
                        /* avoid putting the same instruction twice ! */
                        if(user->getOperand(i) == it2.getPointer() &&
                                !pasched::container_contains(list, inst))
                            list.push_back(inst);
                }
            }
            /*
            dbgs() << "  Instructions:\n";
            for(size_t i = 0; i < list.size(); i++)
                dbgs() << *list[i] << "\n";
            */
            Instruction *last = 0;
            while(list.size())
            {
                size_t min_pos = 0;
                for(size_t i = 0; i < list.size(); i++)
                    if(map_pos[list[i]] < map_pos[list[min_pos]])
                        min_pos = i;
                if(last)
                    dag.add_dependency(
                        pasched::schedule_dep(
                            map[last],
                            map[list[min_pos]],
                            pasched::schedule_dep::order_dep));
                last = list[min_pos];
                pasched::unordered_vector_remove(min_pos, list);
            }
        }

        /* Before scheduling, remove redundant data deps
         * There could be some in the case of such sequence:
         * %0 = ....
         * %1 = mult int32 %0, %0
         */
        dag.remove_redundant_data_deps();

        pasched::transformation_pipeline pipeline;
        pasched::transformation_pipeline snd_stage_pipe;
        pasched::transformation_loop loop(&snd_stage_pipe);
        dag_accumulator accum;
        pipeline.add_stage(new pasched::unique_reg_ids);
        pipeline.add_stage(&loop);
        pipeline.add_stage(&accum);
        snd_stage_pipe.add_stage(new pasched::strip_dataless_units);
        snd_stage_pipe.add_stage(new pasched::strip_useless_order_deps);
        snd_stage_pipe.add_stage(new pasched::split_def_use_dom_use_deps);
        snd_stage_pipe.add_stage(new pasched::smart_fuse_two_units(false, true));
        snd_stage_pipe.add_stage(new pasched::simplify_order_cuts);
        //snd_stage_pipe.add_stage(new pasched::break_symmetrical_branch_merge);
        snd_stage_pipe.add_stage(new pasched::collapse_chains);
        snd_stage_pipe.add_stage(new pasched::split_merge_branch_units);

        #if 0
        pasched::basic_list_scheduler basic_sched;
        pasched::mris_ilp_scheduler sched(&basic_sched, 250);
        #else
        pasched::basic_list_scheduler sched;
        #endif
        pasched::generic_schedule_chain chain;
        pasched::basic_status status;
        /* rememember dag for later check */
        pasched::schedule_dag *dag_copy = dag.dup();
        std::ostringstream oss;
        dump_schedule_dag_to_lsd_stream(dag, oss);
        dbgs() << "**** Schedule DAG ****\n";
        dbgs() << oss.str();
        //debug_view_dag(dag);
        pipeline.transform(dag, sched, chain, status);
        //debug_view_dag(*dag_copy);
        //debug_view_dag(accum.get_dag());
        //debug_view_chain(chain);
        //dump_schedule_dag_to_lsd_file(*dag_copy, "dag.lsd");
        /* check chain against dag */
        bool ok = chain.check_against_dag(*dag_copy);
        delete dag_copy;
        assert(ok && "Invalid schedule (DAG failure)");
        /* other sanity checks */
        assert(chain.get_unit_at(0) == dummy_in && "Invalid schedule (IN failure)");
        assert(chain.get_unit_at(chain.get_unit_count() - 1) == dummy_out && "Invalid schedule (OUT failure)");
        assert(chain.get_unit_at(chain.get_unit_count() - 2) == map[BB.getTerminator()]  && "Invalid schedule (terminator failure)");
        /* reorder in the BB */
        for(size_t i = 1; i < (chain.get_unit_count() - 2); i++)
        {
            const LLVMScheduleUnit *unit = static_cast< const LLVMScheduleUnit *>(chain.get_unit_at(i));
            unit->get_instruction()->removeFromParent();
        }
        for(size_t i = 1; i < (chain.get_unit_count() - 2); i++)
        {
            const LLVMScheduleUnit *unit = static_cast< const LLVMScheduleUnit *>(chain.get_unit_at(i));
            unit->get_instruction()->insertBefore(BB.getTerminator());
        }

        /*
        dbgs() << BB;
        
        for(size_t i = 0; i < chain.get_unit_count(); i++)
            dbgs() << chain.get_unit_at(i)->to_string() << "\n";
        */
        
        AST->clear();
        
        return true;
    }
};

char PaScheduler::ID = 0;
static RegisterPass<PaScheduler>
X("pa-sched", "Basic Block Instruction Scheduler");

}
