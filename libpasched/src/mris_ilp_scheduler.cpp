#include "scheduler.hpp"
#include <glpk.h>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>
#include <set>
#include <queue>
#include <iostream>

//#define DEBUG_ILP_CREATION

//#define USE_FU
#define USE_WUV_TRANSITIVITY
//#define POST_GEN_OPT

#if !defined(USE_FU) && !defined(USE_WUV_TRANSITIVITY)
#error You must choose to use f(u) variable or w(u,v) transitivity
#endif

namespace PAMAURY_SCHEDULER_NS
{

mris_ilp_scheduler::mris_ilp_scheduler()
{
}

mris_ilp_scheduler::~mris_ilp_scheduler()
{
}

struct reg_info_t
{
    reg_info_t(unsigned id) : id(id) {}
    // register ID (for debug)
    unsigned id;
    // list of killers
    std::vector< unsigned > killers;
};

struct instr_regs_info_t
{
    // map for register IDs to reg_info indices
    std::map< unsigned, size_t > reg_map;
    // info for each register created
    std::vector< reg_info_t > reg_info;
};

void mris_ilp_scheduler::schedule(const schedule_dag& dag, schedule_chain& sc) const
{
    char name[32];
    // column of z
    int z_col;
    #ifdef USE_FU
    // columns of the f(u)
    std::vector< int> f_u_col;
    #endif
    // columns of the w(u,v)
    std::vector< std::vector< int > > w_u_v_col;
    // columns of the vnd(u(ri),v)
    std::vector< std::vector< std::vector< int > > > vnd_u_i_v_col;
    // columns of the va(u(ri),v)
    std::vector< std::vector< std::vector< int > > > va_u_i_v_col;
    // columns of the rp(u)
    std::vector< int> rp_u_col;
    // shortcut
    const std::vector< const schedule_unit * > units = dag.get_units();
    // unit map
    std::map< const schedule_unit *, unsigned > unit_map;
    // for each instruction, store a list of created registers and killers
    std::vector< instr_regs_info_t > reg_created;

    
    size_t n = dag.get_units().size();
    /* build unit map */
    for(unsigned u = 0; u < n; u++)
        unit_map[dag.get_units()[u]] = u;
    
    /* compute register informations */
    reg_created.resize(n);
    // for each instruction U
    for(unsigned u = 0; u < n; u++)
    {
        const schedule_unit *unit = units[u];
        instr_regs_info_t& rc = reg_created[u];

        // for each successor S
        for(size_t i = 0; i < dag.get_succs(unit).size(); i++)
        {
            const schedule_dep& dep = dag.get_succs(unit)[i];
            // skip if not a data dep
            if(dep.kind() != schedule_dep::data_dep)
                continue;
            // check if register R is already mapped and map it if not
            if(rc.reg_map.find(dep.reg()) == rc.reg_map.end())
            {
                // map it
                rc.reg_map[dep.reg()] = rc.reg_info.size();
                rc.reg_info.push_back(reg_info_t(dep.reg()));
            }
            // add S to the list of killers of U(R)
            rc.reg_info[rc.reg_map[dep.reg()]].killers.push_back(unit_map[dep.to()]);
        }

        #ifdef DEBUG_ILP_CREATION
        std::cout << "unit " << u << ": " << unit->to_string() << "\n";
        for(size_t i = 0; i < rc.reg_info.size(); i++)
        {
            std::cout << "  create reg " << rc.reg_info[i].id << "\n";
            for(size_t j = 0; j < rc.reg_info[i].killers.size(); j++)
                std::cout << "    destroyed by unit " <<
                    rc.reg_info[i].killers[j] << ": "<<
                    units[rc.reg_info[i].killers[j]]->to_string() << "\n";
        }
        #endif
    }

    // create variables tables
    w_u_v_col.resize(n);
    vnd_u_i_v_col.resize(n);
    va_u_i_v_col.resize(n);
    rp_u_col.resize(n);
    #ifdef USE_FU
    f_u_col.resize(n);
    #endif
    for(unsigned u = 0; u < n; u++)
    {
        w_u_v_col[u].resize(n);
        vnd_u_i_v_col[u].resize(reg_created[u].reg_info.size());
        va_u_i_v_col[u].resize(reg_created[u].reg_info.size());
        
        for(unsigned i = 0; i < reg_created[u].reg_info.size(); i++)
        {
            vnd_u_i_v_col[u][i].resize(n);
            va_u_i_v_col[u][i].resize(n);
        }
    }
    
    // create the problem
    glp_prob *p = glp_create_prob();
    glp_set_prob_name(p, "mris_alt");
    // set objective name
    glp_set_obj_name(p, "min_reg_pres");
    // set objective direction
    glp_set_obj_dir(p, GLP_MIN);
    // create the only objective variable
    z_col = glp_add_cols(p, 1);
    glp_set_col_name(p, z_col, "z");
    glp_set_col_bnds(p, z_col, GLP_FR, 0, 0); // free variable
    glp_set_col_kind(p, z_col, GLP_IV); // integer variable
    glp_set_obj_coef(p, z_col, 1.0); // objective is z
    // create rp(u) variables
    // and f(u) if neeeded
    for(unsigned u = 0; u < n; u++)
    {
        #ifdef USE_FU
        sprintf(name, "f(%u)", u);
        
        f_u_col[u] = glp_add_cols(p, 1);
        glp_set_col_name(p, f_u_col[u], name);
        glp_set_col_kind(p, f_u_col[u], GLP_IV); // intger variable
        // add constraints (1) 1 <= f(u) <= n
        glp_set_col_bnds(p, f_u_col[u], GLP_DB, 1, n); // 1 <= f(u) <= n
        #endif
        // rp(u)
        sprintf(name, "rp(%u)", u);
        
        rp_u_col[u] = glp_add_cols(p, 1);
        glp_set_col_name(p, rp_u_col[u], name);
        glp_set_col_kind(p, rp_u_col[u], GLP_IV); // integer variable
        glp_set_col_bnds(p, rp_u_col[u], GLP_LO, 0, 0); // 0 <= rp(u)
    }
    // create w(u,v) variables
    for(unsigned u = 0; u < n; u++)
        for(unsigned v = 0; v < n; v++)
        {
            // w(u,v)
            w_u_v_col[u][v] = glp_add_cols(p, 1);
            sprintf(name, "w(%u,%u)", u, v);

            glp_set_col_name(p, w_u_v_col[u][v], name);
            glp_set_col_bnds(p, w_u_v_col[u][v], GLP_FR, 0, 0);// free variable
            glp_set_col_kind(p, w_u_v_col[u][v], GLP_BV); // binary variable

            if(u == v)
                glp_set_col_bnds(p, w_u_v_col[u][v], GLP_FX, 0, 0);// w(u,u) = 0
        }
    
    // create va(u(ri),v), vnd(u(ri),v) variables
    for(unsigned u = 0; u < n; u++)
        for(unsigned i = 0; i < reg_created[u].reg_info.size(); i++)
            for(unsigned v = 0; v < n; v++)
            {
                // va(u(ri)v)
                va_u_i_v_col[u][i][v] = glp_add_cols(p, 1);
                sprintf(name, "va(%u(r%u),%u)", u, reg_created[u].reg_info[i].id, v);

                glp_set_col_name(p, va_u_i_v_col[u][i][v], name);
                glp_set_col_bnds(p, va_u_i_v_col[u][i][v], GLP_FR, 0, 0);// free variable
                glp_set_col_kind(p, va_u_i_v_col[u][i][v], GLP_BV); // binary variable

                // vnd(u(ri),v)
                vnd_u_i_v_col[u][i][v] = glp_add_cols(p, 1);
                sprintf(name, "vnd(%u(r%u),%u)", u, reg_created[u].reg_info[i].id, v);

                glp_set_col_name(p, vnd_u_i_v_col[u][i][v], name);
                glp_set_col_bnds(p, vnd_u_i_v_col[u][i][v], GLP_FR, 0, 0);// free variable
                glp_set_col_kind(p, vnd_u_i_v_col[u][i][v], GLP_BV); // binary variable
            }
    // add scheduling constraints: w(u,v)=not w(v,u)
    // not necessary but gives a speedup
    for(unsigned u = 0; u < n; u++)
        for(unsigned v = 0; v < n; v++)
        {
            // skip u=v because neither is true
            if(u == v)
                continue;
            // w(u,v) + w(v,u) = 1
            {
                int row = glp_add_rows(p, 1);

                sprintf(name, "(1a)(%u,%u)", u, v);
                glp_set_row_name(p, row, name);
                glp_set_row_bnds(p, row, GLP_FX, 1, 1);
                int idx[3];
                idx[1] = w_u_v_col[u][v];
                idx[2] = w_u_v_col[v][u];
                double val[3];
                val[1] = 1.0;
                val[2] = 1.0;
                glp_set_mat_row(p, row, 2, idx, val);
            }
        }
    #ifdef USE_WUV_TRANSITIVITY
    // add scheduling constraints: w(u,x) /\ w(x,v) => w(u,v)
    for(unsigned u = 0; u < n; u++)
        for(unsigned x = 0; x < n; x++)
            for(unsigned v = 0; v < n; v++)
            {
                if(u==v || u==x || v==x)
                    continue;
                // w(u,x) + w(x,v) <= 1 + w(u,v)
                // <=> w(u,x) + w(x,v) - w(u,v) <= 1
                int row = glp_add_rows(p, 1);

                sprintf(name, "(2)(%u,%u,%u)", u, x, v);
                glp_set_row_name(p, row, name);
                glp_set_row_bnds(p, row, GLP_UP, 0, 1);
                int idx[4];
                idx[1] = w_u_v_col[u][x];
                idx[2] = w_u_v_col[x][v];
                idx[3] = w_u_v_col[u][v];
                double val[4];
                val[1] = 1.0;
                val[2] = 1.0;
                val[3] = -1.0;
                glp_set_mat_row(p, row, 3, idx, val);
            }
    #endif
    #ifdef USE_FU
    // add scheduling constraints: w(u,v)=(f(u) < f(v))
    for(unsigned u = 0; u < n; u++)
        for(unsigned v = 0; v < n; v++)
        {
            if(u == v)
                continue;
            // f(u) - f(v) > -n*w(u,v)
            // <=> f(u) - f(v) + n*w(u,v) >= 1
            {
                int row = glp_add_rows(p, 1);

                sprintf(name, "(2a)(%u,%u)", u, v);
                glp_set_row_name(p, row, name);
                glp_set_row_bnds(p, row, GLP_LO, 1, 0);
                int idx[4];
                idx[1] = f_u_col[u];
                idx[2] = f_u_col[v];
                idx[3] = w_u_v_col[u][v];
                double val[4];
                val[1] = 1.0;
                val[2] = -1.0;
                val[3] = (double)n;
                glp_set_mat_row(p, row, 3, idx, val);
            }

            // f(v) - f(u) > -n*(1 - w(u,v))
            // <=> f(v) - f(u) - n*w(u,v) >= 1-n
            {
                int row = glp_add_rows(p, 1);

                sprintf(name, "(2b)(%u,%u)", u, v);
                glp_set_row_name(p, row, name);
                glp_set_row_bnds(p, row, GLP_LO, 1.0-n, 0);
                int idx[4];
                idx[1] = f_u_col[v];
                idx[2] = f_u_col[u];
                idx[3] = w_u_v_col[u][v];
                double val[4];
                val[1] = 1.0;
                val[2] = -1.0;
                val[3] = -(double)n;
                glp_set_mat_row(p, row, 3, idx, val);
            }
        }
    #endif
    // for each (u,v) arc, fix the w(u,v) variable to 1 or 0
    for(size_t i = 0; i < dag.get_deps().size(); i++)
    {
        const schedule_dep& dep = dag.get_deps()[i];
        unsigned u = unit_map[dep.from()];
        unsigned v = unit_map[dep.to()];

        glp_set_col_bnds(p, w_u_v_col[u][v], GLP_FX, 1.0, 1.0);
        glp_set_col_bnds(p, w_u_v_col[v][u], GLP_FX, 0.0, 0.0);
    }
    // add live ranges constraints
    for(unsigned u = 0; u < n; u++)
    {
        // rp(u) >= sum_over_creators(v) sum_over_reg_created(v(ri)) va(v(ri),u)
        // <=> rp(u) - sum_over_creators(v) sum_over_reg_created(v(ri)) va(v(ri),u) >= 0
        {
            int row = glp_add_rows(p, 1);

            sprintf(name, "(3)(%u)", u);
            glp_set_row_name(p, row, name);
            glp_set_row_bnds(p, row, GLP_LO, 0, 0);

            std::vector< unsigned > col_indices;
            for(unsigned v = 0; v < n; v++)
                for(unsigned i = 0; i < reg_created[v].reg_info.size(); i++)
                    col_indices.push_back(va_u_i_v_col[v][i][u]);

            unsigned k = col_indices.size();
            int *idx = new int[2 + k];
            double *val = new double[2 + k];
            idx[1] = rp_u_col[u];
            val[1] = 1.0;
            for(size_t i = 0; i < k; i++)
            {
                idx[2 + i] = col_indices[i];
                val[2 + i] = -1.0;
            }
            glp_set_mat_row(p, row, 1 + k, idx, val);

            delete[] idx;
            delete[] val;
        }

        for(unsigned i = 0; i < reg_created[u].reg_info.size(); i++)
            for(unsigned v = 0; v < n; v++)
            {
                // (1-w(v,u)) + vnd(u(ri),v) >= 2*va(u(ri),v)
                // <=> 2*va(u(ri),v) + w(v,u) - vnd(u(ri),v) <= 1
                {
                    int row = glp_add_rows(p, 1);

                    sprintf(name, "(2a)(%u(r%d),%u)", u, reg_created[u].reg_info[i].id, v);
                    glp_set_row_name(p, row, name);
                    glp_set_row_bnds(p, row, GLP_UP, 0, 1);
                    int idx[4];
                    idx[1] = va_u_i_v_col[u][i][v];
                    idx[2] = w_u_v_col[v][u];
                    idx[3] = vnd_u_i_v_col[u][i][v];
                    double val[4];
                    val[1] = 2.0;
                    val[2] = 1.0;
                    val[3] = -1.0;
                    glp_set_mat_row(p, row, 3, idx, val);
                }
                // (1-w(v,u)) + vnd(u(ri),v) <= 1 + va(u(ri),v)
                // <=> va(u(ri),v) + w(v,u(ri)) - vnd(u(ri),v) >= 0
                {
                    int row = glp_add_rows(p, 1);

                    sprintf(name, "(2b)(%u(r%d),%u)", u, reg_created[u].reg_info[i].id, v);
                    glp_set_row_name(p, row, name);
                    glp_set_row_bnds(p, row, GLP_LO, 0, 0);
                    int idx[4];
                    idx[1] = w_u_v_col[v][u];
                    idx[2] = vnd_u_i_v_col[u][i][v];
                    idx[3] = va_u_i_v_col[u][i][v];
                    double val[4];
                    val[1] = 1.0;
                    val[2] = -1.0;
                    val[3] = 1.0;
                    glp_set_mat_row(p, row, 3, idx, val);
                }
                // sum_over_killers(u(ri))(x) w(v,x) >= vnd(u(ri),v)
                // <=> sum_over_killers(u(ri))(x) w(v,x) - vnd(u(ri),v) >= 0
                {
                    int row = glp_add_rows(p, 1);

                    sprintf(name, "(4a)(%u(r%d),%u)", u, reg_created[u].reg_info[i].id, v);
                    glp_set_row_name(p, row, name);
                    glp_set_row_bnds(p, row, GLP_LO, 0, 0);

                    unsigned k = reg_created[u].reg_info[i].killers.size();
                    int *idx = new int[2 + k];
                    double *val = new double[2 + k];
                    idx[1] = vnd_u_i_v_col[u][i][v];
                    val[1] = -1.0;
                    for(unsigned j = 0; j < k; j++)
                    {
                        unsigned x = reg_created[u].reg_info[i].killers[j];
                        idx[2 + j] = w_u_v_col[v][x];
                        val[2 + j] = 1.0;
                    }
                    glp_set_mat_row(p, row, 1 + k, idx, val);

                    delete[] idx;
                    delete[] val;
                }
                // sum_over_killers(u(ri))(x) w(v,x) <= nb_killers * vnd(u(ri),v)
                // <=> sum_over_killers(u(ri))(x) w(v,x) - nb_killers * vnd(u(ri),v) <= 0
                {
                    int row = glp_add_rows(p, 1);

                    sprintf(name, "(4b)(%u(r%d),%u)", u, reg_created[u].reg_info[i].id, v);
                    glp_set_row_name(p, row, name);
                    glp_set_row_bnds(p, row, GLP_UP, 0, 0);

                    unsigned k = reg_created[u].reg_info[i].killers.size();
                    int *idx = new int[2 + k];
                    double *val = new double[2 + k];
                    idx[1] = vnd_u_i_v_col[u][i][v];
                    val[1] = -(double)k;
                    for(unsigned j = 0; j < k; j++)
                    {
                        unsigned x = reg_created[u].reg_info[i].killers[j];
                        idx[2 + j] = w_u_v_col[v][x];
                        val[2 + j] = 1.0;
                    }
                    glp_set_mat_row(p, row, 1 + k, idx, val);

                    delete[] idx;
                    delete[] val;
                }
            }
    }
    // register pressure synthesis
    for(unsigned u = 0; u < n; u++)
    {
        // z >= rp(u)
        // <=> z - rp(u) >= 0
        int row = glp_add_rows(p, 1);

        sprintf(name, "(5)(%u)", u);
        glp_set_row_name(p, row, name);
        glp_set_row_bnds(p, row, GLP_LO, 0, 0);
        int idx[3];
        idx[1] = z_col;
        idx[2] = rp_u_col[u];
        double val[3];
        val[1] = 1.0;
        val[2] = -1.0;
        glp_set_mat_row(p, row, 2, idx, val);
    }
    #ifdef POST_GEN_OPT
    // add post generation additional constraint to speedup
    {
        std::vector< std::vector< bool > > path;
        path.resize(n);
        for(unsigned u = 0; u < n; u++)
            path[u].resize(n);
        // compute path map
        for(unsigned u = 0; u < n; u++)
        {
            std::queue< unsigned > q;
            q.push(u);

            while(!q.empty())
            {
                unsigned v = q.front();
                q.pop();
                if(path[u][v])
                    continue;
                path[u][v] = true;
                for(size_t i = 0; i < dag.get_succs(units[v]).size(); i++)
                    q.push(unit_map[dag.get_succs(units[v])[i].to()]);
            }
        }
    }
    #endif

    glp_write_lp(p, 0, "test.lp");

    glp_iocp cp;
    glp_init_iocp(&cp);
    cp.presolve = GLP_ON;
    cp.gmi_cuts = GLP_ON;
    cp.mir_cuts = GLP_ON;
    cp.cov_cuts = GLP_ON;
    cp.clq_cuts = GLP_ON;
    cp.msg_lev = GLP_MSG_ALL;

    int sts = glp_intopt(p, &cp);
    if(sts != 0)
        throw std::runtime_error("ILP solver error !");
    switch(glp_mip_status(p))
    {
        case GLP_UNDEF: throw std::runtime_error("ILP solution is undefined !");
        case GLP_NOFEAS: throw std::runtime_error("ILP has no solution !");
        default: break;
    }

    //glp_print_mip(p, "test.sol");

    /* retrieve solution */
    std::vector< unsigned > placement;
    std::set< unsigned > to_be_placed;
    for(unsigned u = 0; u < n; u++)
        to_be_placed.insert(u);

    for(unsigned u = 0; u < n; u++)
    {
        /* pick an instruction not already placement */
        unsigned cur_inst = *to_be_placed.begin();
        //std::cout << "Cur instruction: " << units[cur_inst]->to_string() << "\n";
        /* for each instruction X to be placed */
        std::set< unsigned >::iterator it = to_be_placed.begin();
        for(; it != to_be_placed.end(); ++it)
        {
            /* if w(X,cur_inst)=1, then select X */
            unsigned val = glp_mip_col_val(p, w_u_v_col[*it][cur_inst]);
            if(val)
            {
                cur_inst = *it;
                //std::cout << "  Switch to: " << units[cur_inst]->to_string() << "\n";
            }
        }
        /* check cur_inst is okay (sanity check) */
        for(size_t i = 0; i < placement.size(); i++)
        {
            unsigned val = glp_mip_col_val(p, w_u_v_col[placement[i]][cur_inst]);
            if(val != 1)
                throw std::runtime_error("ILP solver went mad ! (1)");
        }
        it = to_be_placed.begin();
        for(; it != to_be_placed.end(); ++it)
        {
            unsigned val = glp_mip_col_val(p, w_u_v_col[cur_inst][*it]);
            if(cur_inst != *it && val != 1)
                throw std::runtime_error("ILP solver went mad ! (1)");
        }
        /* add instruction */
        //std::cout << "  Emit: " << units[cur_inst]->to_string() << "\n";
        placement.push_back(cur_inst);
        to_be_placed.erase(cur_inst);
    }

    /*
    for(unsigned __u = 0; __u < n; __u++)
    {
        unsigned u = placement[__u];
        std::cout << "Instr: " << units[u]->to_string() << "\n";
    }
    */

    for(unsigned u = 0; u < n; u++)
        sc.emit_unit(units[placement[u]]);
    
    glp_delete_prob(p);
}

}
