#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>

#define MAX(a, b)  ((a) < (b) ? (b) : (a))

struct stat_t
{
    int nb_dags;
    int tot_nb_nodes;
    int max_nb_nodes;
    float tot_div_edges_nodes;
    float max_div_edges_nodes;
};

void clear(struct stat_t *s)
{
    s->nb_dags = 0;
    s->tot_nb_nodes = 0;
    s->max_nb_nodes = 0;
    s->tot_div_edges_nodes = 0;
    s->max_div_edges_nodes = 0;
}

void add_dag(struct stat_t *s, int nodes, int edges)
{
    //printf("add %p (%d,%d)\n", s, nodes, edges);
    s->nb_dags++;
    s->tot_nb_nodes += nodes;
    s->max_nb_nodes = MAX(s->max_nb_nodes, nodes);
    if(nodes != 0)
    {
        float div = (float)edges / (float)nodes;
        s->tot_div_edges_nodes += div;
        s->max_div_edges_nodes = MAX(s->max_div_edges_nodes, div);
    }
}

void print(struct stat_t *s)
{
    printf("  DAGs: %d\n", s->nb_dags);
    if(s->nb_dags == 0)
        return;
    printf("  Nodes(avg): %f\n", (float)s->tot_nb_nodes / (float)s->nb_dags);
    printf("  Nodes(max): %d\n", s->max_nb_nodes);
    printf("  Edges/Nodes(avg): %f\n", s->tot_div_edges_nodes / s->nb_dags);
    printf("  Edges/Nodes(max): %f\n", s->max_div_edges_nodes);
}

int main(int argc, char **argv)
{
    struct stat_t in;
    struct stat_t outs;
    int i;
    FILE *f;
    
    clear(&in);
    clear(&outs);

    for(i = 1; i < argc; i++)
    {
        f = fopen(argv[i], "r");
        if(f == NULL)
        {
            perror("Cannot open file");
            return 1;
        }

        int nodes = -1;
        int edges;
        bool first = true;
        int c = fgetc(f);
        while(true)
        {
            /* skip whitespace */
            while(c != EOF)
            {
                if(c == ' ')
                {
                    c = fgetc(f);
                    continue;
                }
                if(c == '\n' || isdigit(c))
                    break;
                printf("unexpected character '%c'\n", c);
                fclose(f);
                return 1;
            }

            if(c == EOF)
                break;

            if(c == '\n')
            {
                c = fgetc(f);
                first = true;
                continue;
            }

            /* read val */
            int val = 0;
            while(isdigit(c))
            {
                val = val * 10 + c - '0';
                c = fgetc(f);
            }

            /* analyze */
            if(nodes == -1)
                nodes = val;
            else
            {
                edges = val;
                add_dag(first ? &in : &outs, nodes, edges);
                nodes = -1;
                first = false;
            }
        }
        
        fclose(f);

        /*
        printf("in: %d\n", in.tot_nb_nodes);
        printf("outs: %d\n", outs.tot_nb_nodes);
        */
    }

    printf("Inputs:\n");
    print(&in);
    printf("Inputs:\n");
    print(&outs);

    return 0;
}
