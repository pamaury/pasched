#include "scheduler.hpp"
#include <glpk.h>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>
#include <iostream>

namespace PAMAURY_SCHEDULER_NS
{

namespace
{
    void compute_max_path_to(const schedule_dag& dag,
            const schedule_unit *unit,
            std::map< const schedule_unit *, size_t >& len,
            bool follow_succ)
    {
        if(len.find(unit) != len.end())
            return;
        size_t max = 0;
        if(follow_succ)
        {
            for(size_t i = 0; i < dag.get_succs(unit).size(); i++)
            {
                const schedule_unit *to = dag.get_succs(unit)[i].to();
                compute_max_path_to(dag, to, len, follow_succ);
                max = std::max(max, len[to] + 1);
            }
        }
        else
        {
            for(size_t i = 0; i < dag.get_preds(unit).size(); i++)
            {
                const schedule_unit *from = dag.get_preds(unit)[i].from();
                compute_max_path_to(dag, from, len, follow_succ);
                max = std::max(max, len[from] + 1);
            }
        }

        len[unit] = max;
    }
}

mris_ilp_scheduler::mris_ilp_scheduler(const schedule_dag& sd)
    :scheduler(sd)
{
}

mris_ilp_scheduler::~mris_ilp_scheduler()
{
}

void mris_ilp_scheduler::schedule(schedule_chain& sc)
{
    char name[32];
    // column of z
    int z_col;
    // columns of the f(u)
    std::vector<int> f_u_col;
    // columns of the w(u,v)
    std::vector< std::vector< int > > w_u_v_col;
    // columns of the d(u,t)
    std::vector< std::vector< int > > d_u_t_col;
    // colunms of the e(u,t,i)
    std::vector< std::vector< std::vector< int > > > e_u_t_i_col;
    // columns of the e(u,t)
    std::vector< std::vector< int > > e_u_t_col;
    // columns of the s(u,t)
    std::vector< std::vector< int > > s_u_t_col;
    // columns of the s(t)
    std::vector< int > s_t_col;
    // unit map
    std::map< const schedule_unit *, unsigned > unit_map;
    
    size_t n = m_graph.get_units().size();

    for(unsigned u = 1; u <= n; u++)
        unit_map[m_graph.get_units()[u - 1]] = u;

    f_u_col.resize(n + 1);
    d_u_t_col.resize(n + 1);
    s_u_t_col.resize(n + 1);
    e_u_t_col.resize(n + 1);
    w_u_v_col.resize(n + 1);
    e_u_t_i_col.resize(n + 1);
    for(size_t i = 1; i <= n; i++)
    {
        w_u_v_col[i].resize(n + 1);
        d_u_t_col[i].resize(n + 1);
        s_u_t_col[i].resize(n + 1);
        e_u_t_col[i].resize(n + 1);
        e_u_t_i_col[i].resize(n + 1);
    }
    s_t_col.resize(n + 1);
    
    // create the problem
    glp_prob *p = glp_create_prob();
    glp_set_prob_name(p, "mris");
    // set objective name
    glp_set_obj_name(p, "min_reg_pres");
    // set objective direction
    glp_set_obj_dir(p, GLP_MIN);
    // create the only objective variable
    z_col = glp_add_cols(p, 1);
    glp_set_col_name(p, z_col, "z");
    glp_set_col_bnds(p, z_col, GLP_FR, 0, 0);
    glp_set_col_kind(p, z_col, GLP_IV); // intger variable
    glp_set_obj_coef(p, z_col, 1.0);
    // create f(u) variables
    for(unsigned u = 1; u <= n; u++)
    {
        sprintf(name, "f(%u)", u);
        
        f_u_col[u] = glp_add_cols(p, 1);
        glp_set_col_name(p, f_u_col[u], name);
        glp_set_col_kind(p, f_u_col[u], GLP_IV); // intger variable
        // add constraints (1) 1 <= f(u) <= n
        glp_set_col_bnds(p, f_u_col[u], GLP_DB, 1, n); // 1 <= f(u) <= n
    }
    // create w(u,v) variables
    for(unsigned u = 1; u <= n; u++)
        for(unsigned v = 1; v <= n; v++)
        {
            w_u_v_col[u][v] = glp_add_cols(p, 1);
            sprintf(name, "w(%u,%u)", u, v);

            glp_set_col_name(p, w_u_v_col[u][v], name);
            glp_set_col_bnds(p, w_u_v_col[u][v], GLP_FR, 0, 0);// free variable
            glp_set_col_kind(p, w_u_v_col[u][v], GLP_BV); // binary variable
        }
    // create d(u,t) variables
    for(unsigned u = 1; u <= n; u++)
        for(unsigned t = 1; t <= n; t++)
        {
            d_u_t_col[u][t] = glp_add_cols(p, 1);
            sprintf(name, "d(%u,%u)", u, t);

            glp_set_col_name(p, d_u_t_col[u][t], name);
            glp_set_col_bnds(p, d_u_t_col[u][t], GLP_FR, 0, 0);// free variable
            glp_set_col_kind(p, d_u_t_col[u][t], GLP_BV); // binary variable
        }
    // create e(u,t,i) variables
    for(unsigned u = 1; u <= n; u++)
        for(unsigned t = 1; t <= n; t++)
        {
            e_u_t_i_col[u][t].push_back(0);
            unsigned i = 1; /* i holds the real index */
            for(unsigned __i = 1; __i <= m_graph.get_succs(m_graph.get_units()[u - 1]).size(); __i++, i++)
            {
                const schedule_dep& dep = m_graph.get_succs(m_graph.get_units()[u - 1])[__i - 1];
                if(dep.kind() == schedule_dep::order_dep && false)
                {
                    i--;
                    continue; // ignore order with no register
                }
                e_u_t_i_col[u][t].push_back(glp_add_cols(p, 1));
                sprintf(name, "e(%u,%u,%u)", u, t, i);

                glp_set_col_name(p, e_u_t_i_col[u][t][i], name);
                glp_set_col_bnds(p, e_u_t_i_col[u][t][i], GLP_FR, 0, 0);// free variable
                glp_set_col_kind(p, e_u_t_i_col[u][t][i], GLP_BV); // binary variable
            }
        }
    // create e(u,t) variables
    for(unsigned u = 1; u <= n; u++)
        for(unsigned t = 1; t <= n; t++)
        {
            e_u_t_col[u][t] = glp_add_cols(p, 1);
            sprintf(name, "e(%u,%u)", u, t);

            glp_set_col_name(p, e_u_t_col[u][t], name);
            glp_set_col_bnds(p, e_u_t_col[u][t], GLP_FR, 0, 0);// free variable
            glp_set_col_kind(p, e_u_t_col[u][t], GLP_BV); // binary variable
        }
    // create s(u,t) variables
    for(unsigned u = 1; u <= n; u++)
        for(unsigned t = 1; t <= n; t++)
        {
            s_u_t_col[u][t] = glp_add_cols(p, 1);
            sprintf(name, "s(%u,%u)", u, t);

            glp_set_col_name(p, s_u_t_col[u][t], name);
            glp_set_col_bnds(p, s_u_t_col[u][t], GLP_FR, 0, 0);// free variable
            glp_set_col_kind(p, s_u_t_col[u][t], GLP_BV); // binary variable
        }
    // create the s(t) variables
    for(unsigned t = 1; t <= n; t++)
    {
        s_t_col[t] = glp_add_cols(p, 1);
        sprintf(name, "s(%u)", t);

        glp_set_col_name(p, s_t_col[t], name);
        glp_set_col_bnds(p, s_t_col[t], GLP_FR, 0, 0);// free variable
        glp_set_col_kind(p, s_t_col[t], GLP_BV); // binary variable
    }
    // add scheduling constraints: f(u)<f(v) or f(v)<f(u)
    for(unsigned u = 1; u <= n; u++)
        for(unsigned v = 1; v <= n; v++)
        {
            if(u >= v)
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
    // for each (u,v) arc, fix the w(u,v) variable to 1 or 0
    for(size_t i = 0; i < m_graph.get_deps().size(); i++)
    {
        const schedule_dep& dep = m_graph.get_deps()[i];
        unsigned u = unit_map[dep.from()];
        unsigned v = unit_map[dep.to()];

        /* we crerated only the w(u,v) var for u<v to avoid redundancy */
        if(u > v)
            glp_set_col_bnds(p, w_u_v_col[v][u], GLP_FX, 0.0, 0.0);
        else
            glp_set_col_bnds(p, w_u_v_col[u][v], GLP_FX, 1.0, 1.0);
            
    }
    // add live ranges constraints
    for(unsigned u = 1; u <= n; u++)
        for(unsigned t = 1; t <= n; t++)
        {
            // t - f(u) >= -n*(1 - d(u,t))
            // <=> f(u) + n*d(u,t) <= n+t
            {
                int row = glp_add_rows(p, 1);

                sprintf(name, "(3a)(%u,%u)", u, t);
                glp_set_row_name(p, row, name);
                glp_set_row_bnds(p, row, GLP_UP, 0, n + t);
                int idx[3];
                idx[1] = f_u_col[u];
                idx[2] = d_u_t_col[u][t];
                double val[3];
                val[1] = 1.0;
                val[2] = n;
                glp_set_mat_row(p, row, 2, idx, val);
            }
            // t - f(u) < n*d(u,t)
            // <=> f(u) + n*d(u,t) >= t + 1
            {
                int row = glp_add_rows(p, 1);

                sprintf(name, "(3b)(%u,%u)", u, t);
                glp_set_row_name(p, row, name);
                glp_set_row_bnds(p, row, GLP_LO, 1 + t, 0);
                int idx[3];
                idx[1] = f_u_col[u];
                idx[2] = d_u_t_col[u][t];
                double val[3];
                val[1] = 1.0;
                val[2] = n;
                glp_set_mat_row(p, row, 2, idx, val);
            }

            unsigned k = 0;
            unsigned i = 1;
            for(unsigned __i = 1; __i <= m_graph.get_succs(m_graph.get_units()[u - 1]).size(); __i++, i++)
            {
                const schedule_dep& dep = m_graph.get_succs(m_graph.get_units()[u - 1])[__i - 1];
                if(dep.kind() == schedule_dep::order_dep && false)
                {
                    i--;
                    continue; // ignore dependencies with no register
                }
                k++;
                unsigned vi = unit_map[dep.to()];
                // t - f(vi) >= -n*e(u,t,i)
                // <=> f(vi) - n*e(u,t,i) <= t
                {
                    int row = glp_add_rows(p, 1);

                    sprintf(name, "(4a)(%u,%u,%u)", u, t, i);
                    glp_set_row_name(p, row, name);
                    glp_set_row_bnds(p, row, GLP_UP, 0, t);
                    int idx[3];
                    idx[1] = f_u_col[vi];
                    idx[2] = e_u_t_i_col[u][t][i];
                    double val[3];
                    val[1] = 1.0;
                    val[2] = -(double)n;
                    glp_set_mat_row(p, row, 2, idx, val);
                }
                // t - f(vi) < n*(1 - e(u,t,i))
                // <=> f(vi) - n*e(u,t,i) >= t-n+1
                {
                    int row = glp_add_rows(p, 1);

                    sprintf(name, "(4b)(%u,%u,%u)", u, t, i);
                    glp_set_row_name(p, row, name);
                    glp_set_row_bnds(p, row, GLP_LO, 1+t-(double)n, 0);
                    int idx[3];
                    idx[1] = f_u_col[vi];
                    idx[2] = e_u_t_i_col[u][t][i];
                    double val[3];
                    val[1] = 1.0;
                    val[2] = -(double)n;
                    glp_set_mat_row(p, row, 2, idx, val);
                }
            }
            // e(u,t,1) + ... + e(u,t,k) >= e(u,t)
            // <=> e(u,t,1) + ... + e(u,t,k) - e(u,t) >= 0
            {
                int row = glp_add_rows(p, 1);

                sprintf(name, "(5a)(%u,%u)", u, t);
                glp_set_row_name(p, row, name);
                glp_set_row_bnds(p, row, GLP_LO, 0, 0);

                int *idx = new int[2 + k];
                double *val = new double[2 + k];
                idx[1] = e_u_t_col[u][t];
                val[1] = -1.0;
                for(unsigned i = 1; i <= k; i++)
                {
                    idx[1 + i] = e_u_t_i_col[u][t][i];
                    val[1 + i] = 1.0;
                }
                glp_set_mat_row(p, row, 1 + k, idx, val);

                delete[] idx;
                delete[] val;
            }
            // e(u,t,1) + ... + e(u,t,k) <= k*e(u,t)
            // <=> e(u,t,1) + ... + e(u,t,k) - k*e(u,t) <= 0
            {
                int row = glp_add_rows(p, 1);

                sprintf(name, "(5b)(%u,%u)", u, t);
                glp_set_row_name(p, row, name);
                glp_set_row_bnds(p, row, GLP_UP, 0, 0);

                int *idx = new int[2 + k];
                double *val = new double[2 + k];
                idx[1] = e_u_t_col[u][t];
                val[1] = -(double)k;
                for(unsigned i = 1; i <= k; i++)
                {
                    idx[1 + i] = e_u_t_i_col[u][t][i];
                    val[1 + i] = 1.0;
                }
                glp_set_mat_row(p, row, 1 + k, idx, val);

                delete[] idx;
                delete[] val;
            }
            // d(u,t) + e(u,t) >= 2*s(u,t)
            // <=> d(u,t) + e(u,t) - 2*s(u,t) >= 0
            {
                int row = glp_add_rows(p, 1);

                sprintf(name, "(6a)(%u,%u)", u, t);
                glp_set_row_name(p, row, name);
                glp_set_row_bnds(p, row, GLP_LO, 0, 0);
                int idx[4];
                idx[1] = d_u_t_col[u][t];
                idx[2] = e_u_t_col[u][t];
                idx[3] = s_u_t_col[u][t];
                double val[4];
                val[1] = 1.0;
                val[2] = 1.0;
                val[3] = -2.0;
                glp_set_mat_row(p, row, 3, idx, val);
            }
            // d(u,t) + e(u,t) <= s(u,t) + 1
            // <=> d(u,t) + e(u,t) - s(u,t) <= 1
            {
                int row = glp_add_rows(p, 1);

                sprintf(name, "(6b)(%u,%u)", u, t);
                glp_set_row_name(p, row, name);
                glp_set_row_bnds(p, row, GLP_UP, 0, 1);
                int idx[4];
                idx[1] = d_u_t_col[u][t];
                idx[2] = e_u_t_col[u][t];
                idx[3] = s_u_t_col[u][t];
                double val[4];
                val[1] = 1.0;
                val[2] = 1.0;
                val[3] = -1.0;
                glp_set_mat_row(p, row, 3, idx, val);
            }
        }
    // register pressure synthesis
    for(unsigned t = 1; t <= n; t++)
    {
        // z >= s(1,t) + ... + s(n,t)
        // <=> s(1,t) + ... + s(n,t) - z <= 0
        int row = glp_add_rows(p, 1);
        
        sprintf(name, "(7)(%u)", t);
        glp_set_row_name(p, row, name);
        glp_set_row_bnds(p, row, GLP_UP, 0, 0);
        int *idx = new int[2 + n];
        double *val = new double[2 + n];
        idx[1] = z_col;
        val[1] = -1.0;
        for(unsigned u = 1; u <= n; u++)
        {
            idx[1 + u] = s_u_t_col[u][t];
            val[1 + u] = 1.0;
        }
        glp_set_mat_row(p, row, 1 + n, idx, val);
        delete[] idx;
        delete[] val;
    }

    /* post-generation optimization */
    std::map< const schedule_unit *, size_t > max_path_to_leaf;
    std::map< const schedule_unit *, size_t > max_path_to_root;
    for(size_t u = 0; u < m_graph.get_roots().size(); u++)
        compute_max_path_to(m_graph, m_graph.get_roots()[u], max_path_to_leaf, true);
    for(size_t u = 0; u < m_graph.get_leaves().size(); u++)
        compute_max_path_to(m_graph, m_graph.get_leaves()[u], max_path_to_root, false);

    for(unsigned u = 1; u <= n; u++)
    {
        // reduce f(u) range
        const schedule_unit *unit = m_graph.get_units()[u - 1];
        unsigned min = 1 + max_path_to_root[unit];
        unsigned max = n - max_path_to_leaf[unit];
        //std::cout << "Unit " << unit->to_string() << ": reduce range to [" << min << "," << max << "]\n";
        glp_set_col_bnds(p, f_u_col[u], GLP_DB, min, max); // min <= f(u) <= max
        // fix d(u,t) to 0 in range [1;min[
        for(unsigned t = 1; t < min; t++)
            glp_set_col_bnds(p, d_u_t_col[u][t], GLP_FX, 0, 0);// d(u,t)=0
        // fix d(u,t) to 1 in range [max;n]
        for(unsigned t = max; t <= n; t++)
            glp_set_col_bnds(p, d_u_t_col[u][t], GLP_FX, 1, 1);// d(u,t)=1

        unsigned i = 1;
        for(unsigned __i = 1; __i <= m_graph.get_succs(m_graph.get_units()[u - 1]).size(); __i++, i++)
        {
            const schedule_dep& dep = m_graph.get_succs(m_graph.get_units()[u - 1])[__i - 1];
            if(dep.kind() == schedule_dep::order_dep && false)
            {
                i--;
                continue; // ignore dependencies with no register
            }
            const schedule_unit *vi = dep.to();
            unsigned min_vi = 1 + max_path_to_root[vi];
            unsigned max_vi = n - max_path_to_leaf[vi];
            // fix e(u,t,i) to 1 in range [1;min_vi[
            for(unsigned t = 1; t < min_vi; t++)
                glp_set_col_bnds(p, e_u_t_i_col[u][t][i], GLP_FX, 1, 1);// e(u,t,i)=1
            // fix e(u,t,i) to 0 in range [max_vi;n]
            for(unsigned t = max_vi; t <= n; t++)
                glp_set_col_bnds(p, e_u_t_i_col[u][t][i], GLP_FX, 0, 0);// e(u,t,i)=0
        }
    }

    glp_write_lp(p, 0, "test.lp");

    glp_iocp cp;
    glp_init_iocp(&cp);
    cp.presolve = GLP_ON;

    int sts = glp_intopt(p, &cp);
    if(sts != 0)
        throw std::runtime_error("ILP solver error !");
    switch(glp_mip_status(p))
    {
        case GLP_UNDEF: throw std::runtime_error("ILP solution is undefined !");
        case GLP_NOFEAS: throw std::runtime_error("ILP has no solution !");
        default: break;
    }

    /* retrieve solution */
    std::vector< const schedule_unit * > placement;

    placement.resize(n + 1);
    for(unsigned u = 1; u <= n; u++)
    {
        unsigned idx = glp_mip_col_val(p, f_u_col[u]);
        if(placement[idx] != 0)
            throw std::runtime_error("ILP solver went mad !");
        placement[idx] = m_graph.get_units()[u - 1];
    }

    for(unsigned u = 1; u <= n; u++)
        sc.emit_unit(placement[u]);
    
    glp_delete_prob(p);
}

}
