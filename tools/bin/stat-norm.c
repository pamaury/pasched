#include <stdio.h>

int main()
{
    while(!feof(stdin))
    {
        char timer[1024];
        float val;
        if(scanf("%s %f", timer, &val) == 2)
            printf("%s %f\n", timer, val);
    }
    return 0;
}
