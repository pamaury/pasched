#define DEBUG_TYPE "pa-sched"
#include "llvm/Pass.h"
#include "llvm/Function.h"
#include "llvm/BasicBlock.h"
#include "llvm/Instruction.h"
#include "llvm/Instructions.h"
#include "llvm/Operator.h"
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

    Instruction *GetInstruction() const
    {
        return m_inst;
    }

    protected:
    Instruction *m_inst;
    std::string m_label;
};
    
struct PaScheduler : public BasicBlockPass
{
    static char ID;
    PaScheduler()
        :BasicBlockPass(&ID) {}

    virtual bool runOnBasicBlock(BasicBlock& BB)
    {
        std::map< Instruction *, const LLVMScheduleUnit * > map;
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

        Instruction *last_mem_inst = 0;
        Instruction *last_throw = 0;
        //dbgs() << "BasicBlock: " << BB.getName() << "\n";
        for(BasicBlock::iterator DI = BB.begin(); DI != BB.end();)
        {
            Instruction *inst = DI++;
            if(isa<PHINode>(inst))
                continue;
            std::ostringstream oss;
            raw_os_ostream os(oss);
            os.SetUnbuffered();
            os << *inst;
            map[inst] = new LLVMScheduleUnit(inst, oss.str());
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

        debug_view_dag(dag);
        
        return true;
    }
};

char PaScheduler::ID = 0;
static RegisterPass<PaScheduler>
X("pa-sched", "Basic Block Instruction Scheduler");

}
