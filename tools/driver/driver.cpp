#include <iostream>
#include <pasched.hpp>
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <iomanip>
#include <vector>
#include <queue>
#include <cassert>
#include <set>
#include <list>
#include <sstream>

/**
 * Shortcuts for often used types
 */
typedef pasched::schedule_dag sched_dag_t;
typedef const pasched::schedule_unit *sched_unit_ptr_t;
typedef pasched::schedule_dep sched_dep_t;
typedef std::set< sched_unit_ptr_t > sched_unit_set_t;
typedef std::vector< sched_unit_ptr_t > sched_unit_vec_t;
typedef std::vector< sched_dep_t > sched_dep_vec_t;
typedef sched_dep_t::reg_t sched_reg_t;
typedef std::set< sched_reg_t > sched_reg_set_t;

/**
 * I/O functions
 */
void ddl_read(const char *filename, pasched::schedule_dag& dag)
{
    pasched::ddl_program p;
    pasched::load_ddl_program_from_ddl_file(filename, p);
    pasched::build_schedule_dag_from_ddl_program(p, dag);
}

void lsd_read(const char *filename, pasched::schedule_dag& dag)
{
    pasched::build_schedule_dag_from_lsd_file(filename, dag);
}

void dot_write(pasched::schedule_dag& dag, const char *filename)
{
    pasched::dump_schedule_dag_to_dot_file(dag, filename);
}

void dotsvg_write(pasched::schedule_dag& dag, const char *filename)
{
    char *name = tmpnam(NULL);
    pasched::dump_schedule_dag_to_dot_file(dag, name);
    system((std::string("dot -Tsvg -o ") + filename + " " + name).c_str());
    remove(name);
}

void null_write(pasched::schedule_dag& dag, const char *filename)
{
    (void) dag;
    (void) filename;
}

/**
 * Printing/Debug functions
 */
std::ostream& operator<<(std::ostream& os, sched_unit_ptr_t u)
{
    return os << u->to_string();
}

template<typename T>
std::ostream& operator<<(std::ostream& os, const std::set< T >& s)
{
    typename std::set< T >::const_iterator it = s.begin();
    os << "{";

    while(it != s.end())
    {
        os << *it;
        ++it;
        if(it == s.end())
            break;
        os << ", ";
    }
    
    return os << "}";
}

template<typename T>
std::ostream& operator<<(std::ostream& os, const std::vector< T >& v)
{
    os << "[";

    for(size_t i = 0; i < v.size(); i++)
    {
        os << v[i];
        if((i + 1) != v.size())
            os << ", ";
    }
    
    return os << "]";
}

/**
 * Delete order dependencies that are redundant because they are already
 * enforced by a combinaison of data&order dependencies.
 * They do not change the scheduling but simplify the graph.
 */
void strip_useless_order_deps(pasched::schedule_dag& dag)
{
    /* Shortcuts */
    const sched_unit_vec_t& units = dag.get_units();
    size_t n = units.size();
    /* Map a unit pointer to a number */
    std::map< sched_unit_ptr_t, size_t > name_map;
    /* path[u][v] is true if there is a path from u to v */
    std::vector< std::vector< bool > > path;
    sched_dep_vec_t to_remove;

    /* As a first step, remove duplicate order, that is when
     * there are several order deps between same pair of units */
    for(size_t u = 0; u < n; u++)
    {
        sched_unit_ptr_t unit = units[u];
        const sched_dep_vec_t& succs = dag.get_succs(unit);
        for(size_t i = 0; i < succs.size(); i++)
            for(size_t j = i + 1; j < succs.size(); j++)
                if(succs[i].kind() == sched_dep_t::order_dep &&
                        succs[j].kind() == sched_dep_t::order_dep &&
                        succs[i].to() == succs[j].to())
                {
                    to_remove.push_back(succs[j]);
                    break;
                }
    }

    dag.remove_dependencies(to_remove);
    to_remove.clear();

    path.resize(n);
    for(size_t u = 0; u < n; u++)
    {
        name_map[units[u]] = u;
        path[u].resize(n);
    }

    /* Fill path */
    for(size_t u = 0; u < n; u++)
    {
        sched_unit_set_t reach = dag.get_reachable(units[u],
            sched_dag_t::rf_follow_succs | sched_dag_t::rf_include_unit);
        sched_unit_set_t::iterator it = reach.begin();
        
        for(; it != reach.end(); ++it)
            path[u][name_map[*it]] = true;
    }

    

    /* To remove a dependency, we go through each node U
     * If for such a node, there are two dependencies A->U and B->U
     * such that there is a path from A to B (A--->B), and A->U is an
     * order dep, then A->U is useless */
    for(size_t u = 0; u < n; u++)
    {
        sched_unit_ptr_t unit = units[u];
        const sched_dep_vec_t& preds = dag.get_preds(unit);

        /* Loop through each pair of dep (A->U,B->U) */
        for(size_t i = 0; i < preds.size(); i++)
        {
            size_t i_from = name_map[preds[i].from()];
            /* Mind the order !
             * We don't want to treat each pair twice because we would
             * end up removing each edge twice. Furthermore we should
             * be careful in such a situation:
             * A -> U where are two order dep between A and U
             * then we want to remove only one of them */
            for(size_t j = i + 1; j < preds.size(); j++)
            {
                size_t j_from = name_map[preds[j].from()];

                assert(path[i_from][u] && path[j_from][u]);

                /* Try both order */
                if(path[i_from][j_from] && preds[i].kind() == sched_dep_t::order_dep)
                    to_remove.push_back(preds[i]);
                else if(path[j_from][i_from] && preds[j].kind() == sched_dep_t::order_dep)
                    to_remove.push_back(preds[j]);
            }
        }
    }

    for(size_t i = 0; i < to_remove.size(); i++)
        dag.remove_dependency(to_remove[i]);
}

/**
 * If the graph can be splitted into two subgraph G and H such that
 * the only depdencies between them are:
 * 1) order dep
 * 2) go from G to H (G->H)
 * Then it means that we can schedule G as a whole and then H as a whole
 * because they do not interfere. So we can cut all these dependencies
 * because they make the graph appear as more complicated than it really
 * is */
void simplify_order_cuts(pasched::schedule_dag& dag)
{
    const sched_unit_vec_t& units = dag.get_units();
    size_t n = units.size();
    std::map< sched_unit_ptr_t, size_t > name_map;

    for(size_t u = 0; u < n; u++)
        name_map[units[u]] = u;

    /* Repeat until no change because a cut can lead to other cuts
     * which were unavailable before */
    while(true)
    {
        bool graph_changed = false;
        /* Keep a map of proceeded nodes, to avoiding proceeding a node twice */
        std::vector < bool > proceeded;
        proceeded.resize(n);

        for(size_t u = 0; u < n; u++)
        {
            if(proceeded[u])
                continue;
            std::set< size_t > component;
            std::queue< size_t > to_process;
            sched_dep_vec_t to_remove;

            /* For a node U, we compute the largest component C which
             * contains U and which is stable by these operations:
             * 1) If A is in C and A->B is a data dep, B is in C
             * 2) If A is in C and B->A is a data dep, B is in C
             * 2) If A is in C and B->A is an order dep, B is in C
             * then we go through all the order edges that go out of C
             * and cut them because we are sure that the only edges going
             * out of C are order dep and that there are not edges going
             * in C */

            /* start with U */
            component.insert(u);
            to_process.push(u);

            while(!to_process.empty())
            {
                /* For a node A */
                size_t node = to_process.front();
                to_process.pop();

                /* Add all nodes B such that there is a dep B->A */
                for(size_t i = 0; i < dag.get_preds(units[node]).size(); i++)
                {
                    const sched_dep_t& d = dag.get_preds(units[node])[i];
                    size_t i_idx = name_map[d.from()];
                    if(component.find(i_idx) != component.end())
                        continue;
                    component.insert(i_idx);
                    to_process.push(i_idx);
                }

                /* Add all nodes B such that there is a data dep A->B */
                for(size_t i = 0; i < dag.get_succs(units[node]).size(); i++)
                {
                    const sched_dep_t& d = dag.get_succs(units[node])[i];
                    if(d.kind() == sched_dep_t::order_dep)
                        continue;
                    size_t i_idx = name_map[d.to()];
                    if(component.find(i_idx) != component.end())
                        continue;
                    component.insert(i_idx);
                    to_process.push(i_idx);
                }
            }

            std::set< size_t >::iterator it = component.begin();
            std::set< size_t >::iterator it_end = component.end();

            /* Delete all order edges going out of the component */
            for(; it != it_end; ++it)
            {
                for(size_t i = 0; i < dag.get_succs(units[*it]).size(); i++)
                {
                    const sched_dep_t& d = dag.get_succs(units[*it])[i];
                    if(d.kind() != sched_dep_t::order_dep)
                        continue;
                    size_t i_idx = name_map[d.to()];
                    if(component.find(i_idx) != component.end())
                        continue;
                    to_remove.push_back(d);
                }
            }

            if(to_remove.size() == 0)
                continue;
            graph_changed = true;
            for(size_t i = 0; i < to_remove.size(); i++)
                dag.remove_dependency(to_remove[i]);
        }

        if(!graph_changed)
            break;
    }
}

/**
 * If the graph consist of two subgraphs G and H with one articulation node x
 * such that each node in G has a directed path to x and each node y in H
 * has a directed path from x, then we can safely schedule G then H because
 * any schedule node in H reauires x to be schedule which thus require that
 * G be scheduled a whole. So we split x and the graph into two subgraphs
 */
void split_merge_branch_units_conservative(pasched::schedule_dag& dag)
{
    sched_unit_vec_t to_split;

    for(size_t u = 0; u < dag.get_units().size(); u++)
    {
        if(dag.get_preds(dag.get_units()[u]).size() == 0 ||
                dag.get_succs(dag.get_units()[u]).size() == 0)
            continue;
        
        sched_unit_set_t pr = dag.get_reachable(dag.get_units()[u], sched_dag_t::rf_follow_preds);
        sched_unit_set_t sr = dag.get_reachable(dag.get_units()[u], sched_dag_t::rf_follow_succs);

        if((sr.size() + pr.size() + 1) != dag.get_units().size())
            continue;

        sched_unit_set_t::iterator it = pr.begin();

        for(; it != pr.end(); ++it)
        {
            for(size_t i = 0; i < dag.get_succs(*it).size(); i++)
                if(sr.find(dag.get_succs(*it)[i].to()) != sr.end())
                    goto Lskip;
        }

        it = sr.begin();
        for(; it != sr.end(); ++it)
        {
            for(size_t i = 0; i < dag.get_preds(*it).size(); i++)
                if(pr.find(dag.get_preds(*it)[i].from()) != pr.end())
                    goto Lskip;
        }

        //std::cout << "found split node: " << dag.get_units()[u]->to_string() << "\n";
        //std::cout << "pr: " << pr << "\n";
        //std::cout << "sr: " << sr << "\n";

        to_split.push_back(dag.get_units()[u]);

        Lskip:
        continue;
    }

    for(size_t u = 0; u < to_split.size(); u++)
    {
        sched_unit_ptr_t unit = to_split[u];
        // make a copy of the unit
        sched_unit_ptr_t cpy = unit->dup();
        // add the unit
        dag.add_unit(cpy);
        // copy succs dependencies to the unit copy
        for(size_t i = 0; i < dag.get_succs(unit).size(); i++)
        {
            sched_dep_t d = dag.get_succs(unit)[i];
            d.set_from(cpy);
            dag.add_dependency(d);
        }
        // remove those dependencies from the original unit
        while(dag.get_succs(unit).size() != 0)
            dag.remove_dependency(dag.get_succs(unit)[0]);
    }
}

/**
 * If there is a node U such that all children for the same dependency
 * are U(1) ... U(k) satisfy the property that all U(i) are reachable
 * from, say, U(1), then we can cut all (U,U(i)) dep for i>=1
 * and add (U(1),U(i)) instead
 */
void split_def_use_dom_use_deps(pasched::schedule_dag& dag)
{
    /* First compute path map, it will not changed during the algorithm
     * even though some edges are changed */
     
    /* Shortcuts */
    const sched_unit_vec_t& units = dag.get_units();
    size_t n = units.size();
    /* Map a unit pointer to a number */
    std::map< sched_unit_ptr_t, size_t > name_map;
    /* path[u][v] is true if there is a path from u to v */
    std::vector< std::vector< bool > > path;

    path.resize(n);
    for(size_t u = 0; u < n; u++)
    {
        name_map[units[u]] = u;
        path[u].resize(n);
    }

    /* Fill path */
    for(size_t u = 0; u < n; u++)
    {
        sched_unit_set_t reach = dag.get_reachable(units[u],
            sched_dag_t::rf_follow_succs | sched_dag_t::rf_include_unit);
        sched_unit_set_t::iterator it = reach.begin();
        for(; it != reach.end(); ++it)
            path[u][name_map[*it]] = true;
        /*
        std::cout << "Reachable from " << units[u]->to_string() << "\n";
        std::cout << "  " << reach << "\n";
        */
    }
    
    /* Each iteration might modify the graph */
    while(true)
    {
        /* For each unit U */
        for(size_t u = 0; u < dag.get_units().size(); u++)
        {
            sched_unit_ptr_t unit = dag.get_units()[u];
            const sched_dep_vec_t& succs = dag.get_succs(unit);
            /* Skip useless units for speed reason */
            if(succs.size() <= 1)
                continue;
            /* Compute the set of registers on successors dep */
            std::map< unsigned, sched_dep_vec_t > reg_succs;

            for(size_t i = 0; i < succs.size(); i++)
                if(succs[i].kind() == sched_dep_t::data_dep)
                    reg_succs[succs[i].reg()].push_back(succs[i]);

            if(0)
            {
                std::cout << "Unit: " << unit->to_string() << "\n";
                std::map< unsigned, sched_dep_vec_t >::iterator reg_succs_it = reg_succs.begin();
                for(; reg_succs_it != reg_succs.end(); ++reg_succs_it)
                {
                    std::cout << "  Register " << reg_succs_it->first << "\n";
                    for(size_t i = 0; i < reg_succs_it->second.size(); i++)
                        std::cout << "    To " << reg_succs_it->second[i].to()->to_string() << "\n";
                }
            }
            
            /* Try each register R */
            std::map< unsigned, sched_dep_vec_t >::iterator reg_succs_it = reg_succs.begin();
            for(; reg_succs_it != reg_succs.end(); ++reg_succs_it)
            {
                sched_dep_vec_t reg_use = reg_succs_it->second;

                /* See of one successor S of U dominate all use
                 * NOTE: S can be any successor of U
                 * NOTE: there can be several dominators, we list tham all */
                sched_unit_vec_t dominators;
                
                for(size_t dom_idx = 0; dom_idx < succs.size(); dom_idx++)
                {
                    for(size_t i = 0; i < reg_use.size(); i++)
                        if(!path[name_map[succs[dom_idx].to()]][name_map[reg_use[i].to()]])
                            goto Lskip;
                    dominators.push_back(succs[dom_idx].to());
                    Lskip:
                    continue;
                }
                
                for(size_t dom_idx = 0; dom_idx < dominators.size(); dom_idx++)
                {
                    /* There is dominator D */
                    sched_unit_ptr_t dom = dominators[dom_idx];

                    bool dominator_is_in_reg_use = false;
                    bool non_dominator_is_in_reg_use = false;
                    for(size_t i = 0; i < reg_use.size(); i++)
                        if(dom == reg_use[i].to())
                            dominator_is_in_reg_use = true;
                        else
                            non_dominator_is_in_reg_use = true;
                    /* There must a node in reg use which is not the dominator */
                    if(!non_dominator_is_in_reg_use)
                        continue; /* next dominator */

                    /*
                    std::cout << "Dominator: " << dom->to_string() << "\n";
                    for(size_t i = 0; i < reg_use.size(); i++)
                        std::cout << "  -> " << reg_use[i].to()->to_string() << "\n";
                    */ 

                    /* For each dependency (U,V) on register R, remove (U,V) */
                    dag.remove_dependencies(reg_use);
                    /* For each old dependency (U,V) on register R, add (D,V)
                     * NOTE: beware if the dominator is one of the considered child !
                     *       Otherwise, we'll create a self-loop */
                    for(size_t i = 0; i < reg_use.size(); i++)
                        if(dom != reg_use[i].to())
                            reg_use[i].set_from(dom);

                    /* Add (U,D) on R
                     * Except if dominator_is_in_reg_use because the previous
                     * didn't modify the link so the (U,D) edge is already in the list */
                    if(!dominator_is_in_reg_use)
                        reg_use.push_back(sched_dep_t(unit, dom,
                            sched_dep_t::data_dep, reg_succs_it->first));

                    dag.add_dependencies(reg_use);
                    
                    goto Lgraph_changed;
                }
            }
        }

        /* Graph did not change */
        break;

        Lgraph_changed:
        continue;
    }
}

/**
 * Run the alternative MRIS ILP scheduler on the graph and adds the order links
 * in order to force a chain order.
 */
void mris_ilp_schedule(pasched::schedule_dag& dag)
{
    pasched::mris_ilp_scheduler mris;
    pasched::generic_schedule_chain sc;

    mris.schedule(dag, sc);

    //std::cout << "-==== Schedule ====-\n";
    /*
    for(size_t i = 0; i < sc.get_units().size(); i++)
        std::cout << "(" << std::setw(2) << i << ") " << sc.get_units()[i]->to_string() << "\n";
    */
    /* add order edges to force chain order in the original graph */
    for(size_t i = 0; (i + 1) < sc.get_units().size(); i++)
    {
        sched_dep_t d;
        d.set_from(sc.get_units()[i]);
        d.set_to(sc.get_units()[i + 1]);
        d.set_kind(sched_dep_t::order_dep);
        dag.add_dependency(d);
    }
}

void unique_reg_ids(pasched::schedule_dag& dag)
{
    const pasched::unique_reg_ids uri;
    const pasched::schedule_chain_transformation *sct= uri.transform(dag);
    delete sct;
}

/**
 * Helper function for fusing
 */
void fuse_unit_to_successor(pasched::schedule_dag& dag, sched_unit_ptr_t unit)
{
    if(dag.get_reachable(unit, sched_dag_t::rf_follow_succs | sched_dag_t::rf_immediate).size() != 1)
        throw std::runtime_error("fuse_unit_to_successor called with invalid parameters");
        
    dag.fuse_units(unit, dag.get_succs(unit)[0].to());
}

void fuse_unit_to_predecessor(pasched::schedule_dag& dag, sched_unit_ptr_t unit)
{
    if(dag.get_reachable(unit, sched_dag_t::rf_follow_preds | sched_dag_t::rf_immediate).size() != 1)
        throw std::runtime_error("fuse_unit_to_predecessor called with invalid parameters");

    dag.fuse_units(dag.get_preds(unit)[0].from(), unit);
}

void smart_fuse_two_units(pasched::schedule_dag& dag)
{
    while(true)
    {
        for(size_t u = 0; u < dag.get_units().size(); u++)
        {
            sched_unit_ptr_t unit = dag.get_units()[u];

            sched_reg_set_t vc = dag.get_reg_create(unit);
            sched_reg_set_t vu = dag.get_reg_use(unit);
            sched_reg_set_t vd = dag.get_reg_destroy(unit);
            sched_unit_set_t ipreds = dag.get_reachable(unit, sched_dag_t::rf_follow_preds | sched_dag_t::rf_immediate);
            sched_unit_set_t isuccs = dag.get_reachable(unit, sched_dag_t::rf_follow_succs | sched_dag_t::rf_immediate);

            /*
            std::cout << "Unit: " << unit << "\n";
            std::cout << "  VC=" << vc << "\n";
            std::cout << "  VU=" << vu << "\n";
            std::cout << "  VD=" << vd << "\n";
            */
            
            /* Case 1
             * - unit has one predecessor only
             * - unit destroys more variable than it creates ones
             * - IRP of unit is lower than the number of destroyed variables
             * Then
             * - fuse unit to predecessor */
            if(ipreds.size() == 1 && vd.size() >= vc.size() &&
                    unit->internal_register_pressure() <= vd.size())
            {
                fuse_unit_to_predecessor(dag, unit);
                goto Lgraph_changed;
            }
            /* Case 2
             * - unit has one successor only
             * - unit creates more variable than it uses ones
             * - IRP of unit is lower than the number of created variables
             * Then
             * - fuse unit to successor */
            else if(isuccs.size() == 1 && vc.size() >= vu.size() &&
                    unit->internal_register_pressure() <= vc.size())
            {
                fuse_unit_to_successor(dag, unit);
                goto Lgraph_changed;
            }
        }
        /* no change, stop */
        break;
        Lgraph_changed:
        continue;
    }
}

/**
 * 
 */
void strip_nrinro_costless_units(pasched::schedule_dag& dag)
{
    /* We repeat everything from the beginning at each step because
     * each modification can add order dependencies and delete a node,
     * so it's easier this way */
    while(true)
    {
        /* Find such a node */
        size_t u;
        sched_unit_ptr_t unit = 0;
        for(u = 0; u < dag.get_units().size(); u++)
        {
            unit = dag.get_units()[u];
            /* - costless */
            if(unit->internal_register_pressure() != 0)
                goto Lskip;
            /* - NRI */
            for(size_t i = 0; i < dag.get_preds(unit).size(); i++)
                if(dag.get_preds(unit)[i].kind() != sched_dep_t::order_dep)
                    goto Lskip;

            /* - NRO */
            for(size_t i = 0; i < dag.get_succs(unit).size(); i++)
                if(dag.get_succs(unit)[i].kind() != sched_dep_t::order_dep)
                    goto Lskip;
            break;
            Lskip:
            continue;
        }

        /* Stop if not found */
        if(u == dag.get_units().size())
            break;
        /* We will remove the unit but add some dependencies to keep correctness
         * We add the dependencies at the end because it can invalidate
         * the lists we are going through */
        sched_dep_vec_t to_add;
        
        for(size_t i = 0; i < dag.get_preds(unit).size(); i++)
            for(size_t j = 0; j < dag.get_succs(unit).size(); j++)
            {
                sched_dep_t d;
                d.set_from(dag.get_preds(unit)[i].from());
                d.set_to(dag.get_succs(unit)[j].to());
                d.set_kind(sched_dep_t::order_dep);

                to_add.push_back(d);
            }

        /* Add the dependencies */
        dag.add_dependencies(to_add);

        /* Remove the unit */
        dag.remove_unit(unit);
    }
}

void strip_redundant_data_deps(pasched::schedule_dag& dag)
{
    sched_dep_vec_t to_remove;
    
    for(size_t u = 0; u < dag.get_units().size(); u++)
    {
        sched_unit_ptr_t unit = dag.get_units()[u];
        for(size_t i = 0; i < dag.get_succs(unit).size(); i++)
        {
            const sched_dep_t& dep = dag.get_succs(unit)[i];
            if(dep.kind() != sched_dep_t::data_dep)
                continue;
            for(size_t j = i + 1; j < dag.get_succs(unit).size(); j++)
            {
                const sched_dep_t& dep2 = dag.get_succs(unit)[j];
                if(dep2.kind() == sched_dep_t::data_dep &&
                        dep2.to() == dep.to() &&
                        dep2.reg() == dep2.reg())
                    to_remove.push_back(dep2);
            }
        }
    }

    dag.remove_dependencies(to_remove);
}

void break_symmetrical_branch_merge(pasched::schedule_dag& dag)
{
    sched_dep_vec_t to_add;
    
    for(size_t u = 0; u < dag.get_units().size(); u++)
    {
        sched_unit_ptr_t unit = dag.get_units()[u];
        sched_unit_set_t preds_order_succs;
        sched_unit_set_t preds_preds;
        unsigned irp = 0;
        sched_unit_set_t preds = dag.get_reachable(unit, sched_dag_t::rf_follow_preds_data | sched_dag_t::rf_immediate);

        if(preds.size() < 2)
            continue;
        if(dag.get_reachable(unit, sched_dag_t::rf_follow_preds_order | sched_dag_t::rf_immediate).size() != 0)
            continue;

        for(sched_unit_set_t::iterator it = preds.begin(); it != preds.end(); ++it)
        {
            sched_unit_ptr_t the_pred = *it;
            /* - must be single data successor */
            if(dag.get_reachable(the_pred, sched_dag_t::rf_follow_succs_data | sched_dag_t::rf_immediate).size() != 1)
                goto Lnext;
            /* - predecessors's predecessors */
            sched_unit_set_t this_preds_order_succs = dag.get_reachable(the_pred, sched_dag_t::rf_follow_succs_order | sched_dag_t::rf_immediate);
            sched_unit_set_t this_preds_preds = dag.get_reachable(the_pred, sched_dag_t::rf_follow_preds | sched_dag_t::rf_immediate);
            
            if(it == preds.begin())
            {
                preds_preds = this_preds_preds;
                preds_order_succs = this_preds_order_succs;
                irp = the_pred->internal_register_pressure();
            }
            else
            {
                if(preds_preds != this_preds_preds)
                    goto Lnext;
                if(preds_order_succs != this_preds_order_succs)
                    goto Lnext;
                if(irp != the_pred->internal_register_pressure())
                    goto Lnext;
            }
        }

        for(sched_unit_set_t::iterator it = preds.begin(); it != preds.end(); ++it)
        {
            sched_unit_set_t::iterator it2 = it;
            ++it2;
            if(it2 == preds.end())
                break;

            sched_dep_t d;
            d.set_from(*it);
            d.set_to(*it2);
            d.set_kind(sched_dep_t::order_dep);
            to_add.push_back(d);
        }

        Lnext:
        continue;
    }

    dag.add_dependencies(to_add);
}

void reg_analysis_info(pasched::schedule_dag& dag)
{
    std::cout << "--== Reg analysis ==--\n";
    for(size_t u = 0; u < dag.get_units().size(); u++)
    {
        sched_unit_ptr_t unit = dag.get_units()[u];
        std::cout << "  Unit: " << unit << "\n";
        sched_reg_set_t vc = dag.get_reg_create(unit);
        sched_reg_set_t vu = dag.get_reg_use(unit);
        sched_reg_set_t vd = dag.get_reg_destroy(unit);
        sched_reg_set_t vde = dag.get_reg_destroy_exact(unit);
        sched_reg_set_t vdde = dag.get_reg_dont_destroy_exact(unit);

        std::cout << "    VC  =" << vc << "\n";
        std::cout << "    VU  =" << vu << "\n";
        std::cout << "    VD  =" << vd << "\n";
        std::cout << "    VDE =" << vde << "\n";
        std::cout << "    VDDE=" << vdde << "\n";
    }
}

/**
 * Helper function that split dag with more than one connected component
 */
void branch_dag_list(std::list< pasched::generic_schedule_dag >& list)
{
    std::list< pasched::generic_schedule_dag >::iterator it = list.begin();

    //std::cout << "before: " << list.size() << " DAGs in the list\n";

    while(it != list.end())
    {
        /* Empty DAG ? */
        if(it->get_units().size() == 0)
        {
            std::list< pasched::generic_schedule_dag >::iterator it2 = it;
            ++it;
            list.erase(it2);
            continue;
        }
        
        sched_unit_set_t cc;
        std::queue< sched_unit_ptr_t > q;
        q.push(it->get_units()[0]);

        while(!q.empty())
        {
            sched_unit_ptr_t u = q.front();
            q.pop();
            if(cc.find(u) != cc.end())
                continue;
            cc.insert(u);

            for(size_t i = 0; i < it->get_preds(u).size(); i++)
                q.push(it->get_preds(u)[i].from());
            for(size_t i = 0; i < it->get_succs(u).size(); i++)
                q.push(it->get_succs(u)[i].to());
        }

        /* connected graph ? */
        if(cc.size() == it->get_units().size())
        {
            ++it;
            continue;
        }

        /* extract this connected component */
        list.push_front(pasched::generic_schedule_dag());

        sched_unit_set_t::iterator set_it = cc.begin();

        for(; set_it != cc.end(); ++set_it)
            list.front().add_unit(*set_it);

        set_it = cc.begin();
        for(; set_it != cc.end(); ++set_it)
        {
            for(size_t i = 0; i < it->get_preds(*set_it).size(); i++)
            {
                const sched_dep_t& d = it->get_preds(*set_it)[i];
                if(cc.find(d.from()) != cc.end())
                    list.front().add_dependency(d);
            }
        }

        /* remove it from dag */
        set_it = cc.begin();
        for(; set_it != cc.end(); ++set_it)
            it->remove_unit(*set_it);

        /* restart loop with the same dag */
    }

    //std::cout << "after: " << list.size() << " DAGs in the list\n";
}

/**
 * Helper function that merge a set of DAG in one DAG as side by side,
 * unrelated subgraphs
 */
void merge_dag_list(std::list< pasched::generic_schedule_dag >& list, pasched::schedule_dag& dag)
{
    std::list< pasched::generic_schedule_dag >::iterator it = list.begin();

    for(;it != list.end(); ++it)
    {
        for(size_t u = 0; u < it->get_units().size(); u++)
            dag.add_unit(it->get_units()[u]);
        for(size_t u = 0; u < it->get_units().size(); u++)
        {
            sched_unit_ptr_t unit = it->get_units()[u];
            for(size_t i = 0; i < it->get_preds(unit).size(); i++)
                dag.add_dependency(it->get_preds(unit)[i]);
        }
    }
}

/**
 * Few tables for passes registration and blabla
 */
typedef void (*read_cb_t)(const char *filename, pasched::schedule_dag& dag);
typedef void (*write_cb_t)(pasched::schedule_dag& dag, const char *filename);
typedef void (*pass_cb_t)(pasched::schedule_dag& dag);

struct format_t
{
    const char *name;
    const char **ext;
    const char *desc;
    read_cb_t read;
    write_cb_t write;
};

struct pass_t
{
    const char *name;
    const char *desc;
    pass_cb_t apply;
};

const char *ddl_ext[] = {"ddl", 0};
const char *dot_ext[] = {"dot", 0};
const char *dotsvg_ext[] = {"svg", 0};
const char *null_ext[] = {0};

format_t formats[] =
{
    {"ddl", ddl_ext, "Data Dependency Language file", &ddl_read, 0},
    {"lsd", ddl_ext, "LLVM Schedule DAG file", &lsd_read, 0},
    {"dot", dot_ext, "Graphviz file", 0, &dot_write},
    {"dotsvg", dotsvg_ext, "Graphviz file rendered to SVG", 0, &dotsvg_write},
    {"null", null_ext, "Drop output to the void", 0, &null_write},
    {0}
};

pass_t passes[] =
{
    {"unique-reg-ids", "Number data dependencies to have unique register IDs", &unique_reg_ids},
    {"strip-nrinro-costless-units", "Strip no-register-in no-register-out costless units", &strip_nrinro_costless_units},
    {"strip-useless-order-deps", "Strip order dependencies already enforced by order depedencies", &strip_useless_order_deps},
    {"simplify-order-cuts", "Simplify the graph by finding one way cuts made of order dependencies", &simplify_order_cuts},
    {"mris-ilp-schedule", "Schedule it with the MRIS ILP scheduler", &mris_ilp_schedule},
    {"split-def-use-dom-use-deps", "Split edges from a def to a use which dominates all other uses", &split_def_use_dom_use_deps},
    {"strip-redundant-data-deps", "", &strip_redundant_data_deps},
    {"smart-fuse-two-units", "", &smart_fuse_two_units},
    {"break-symmetrical-branch-merge", "", &break_symmetrical_branch_merge},
    {"reg-analysis-info", "", &reg_analysis_info},
    {0}
};

/**
 * Helper function to display help
 */
void display_usage()
{
    std::cout << "usage: driver <fmt> <input> <fmt> <output> [<pass 1> <pass 2> ...]\n";
    std::cout << "Formats:\n";
    size_t max_size = 0;
    for(int i = 0; formats[i].name != 0; i++)
        max_size = std::max(max_size, strlen(formats[i].name));
    
    for(int i = 0; formats[i].name != 0; i++)
    {
        std::cout << " -" << std::setw(max_size) << std::left << formats[i].name << "\t" << formats[i].desc;
        if(formats[i].read == 0)
            std::cout << "(Output only)";
        if(formats[i].write == 0)
            std::cout << "(Input only)";
        std::cout << "\n";
    }
    std::cout << "Passes:\n";

    max_size = 0;
    for(int i = 0; passes[i].name != 0; i++)
        max_size = std::max(max_size, strlen(passes[i].name));
    for(int i = 0; passes[i].name != 0; i++)
    {
        std::cout << " -" << std::setw(max_size) << std::left << passes[i].name << "\t" << passes[i].desc << "\n";
    }
}

int __main(int argc, char **argv)
{
    if(argc < 5)
    {
        display_usage();
        return 1;
    }

    int from = -1;
    int to = -1;

    for(int i = 0; formats[i].name != 0; i++)
    {
        if(strcmp(formats[i].name, argv[1] + 1) == 0)
            from = i;
        if(strcmp(formats[i].name, argv[3] + 1) == 0)
            to = i;
    }

    if(from == -1 || argv[1][0] != '-')
    {
        std::cout << "Unknown input format '" << argv[1] << "'\n";
        return 1;
    }

    if(formats[from].read == 0)
    {
        std::cout << "Format '" << formats[from].name << "' cannot be used as input\n";
        return 1;
    }

    if(to == -1 || argv[3][0] != '-')
    {
        std::cout << "Unknown output format '" << argv[3] << "'\n";
        return 1;
    }

    if(formats[to].write == 0)
    {
        std::cout << "Format '" << formats[to].name << "' cannot be used as output\n";
        return 1;
    }

    std::list< pasched::generic_schedule_dag > dag_list;
    pasched::generic_schedule_dag dag;
    formats[from].read(argv[2], dag);
    if(!dag.is_consistent())
    {
        std::cout << "Internal error, schedule DAG is not consistent !\n";
        return 1;
    }
    dag_list.push_front(dag);
    branch_dag_list(dag_list);

    for(int i = 5; i < argc; i++)
    {
        int j = 0;
        while(passes[j].name && strcmp(passes[j].name, argv[i] + 1) != 0)
            j++;

        if(passes[j].name == 0 || argv[i][0] != '-')
        {
            std::cout << "Unknown pass '" << argv[i] << "'\n";
            return 1;
        }

        std::list< pasched::generic_schedule_dag >::iterator it = dag_list.begin();
        std::list< pasched::generic_schedule_dag >::iterator it_end = dag_list.end();
        
        for(; it != it_end; ++it)
        {
            passes[j].apply(*it);
            if(!it->is_consistent())
            {
                std::cout << "Internal error, schedule DAG is not consistent !\n";
                return 1;
            }
        }
        
        branch_dag_list(dag_list);
    }

    dag.clear();
    merge_dag_list(dag_list, dag);
    
    formats[to].write(dag, argv[4]);
    
    return 0;
}

int main(int argc, char **argv)
{
    try
    {
        return __main(argc, argv);
    }
    catch(std::exception& e)
    {
        std::cout << "exception: " << e.what() << "\n";
        return 1;
    }
    catch(...)
    {
        std::cout << "exception: unknown !\n";
        return 1;
    }
}
