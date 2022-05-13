/* ========================================================================== */
/* === KLU_compute_path ===================================================== */
/* ========================================================================== */

/*
 * Computes the factorization path given change vector
 * Any new refactorisation can be computed by iterating over entries in factorization path
 * instead of all columns (see klu_partial.c)
 */
#include "klu_internal.h"
#include <string.h>

void saveLU(double *Lx, int *Li, int *Lp, double *Ux, int *Ui, int *Up, double *Fx, int *Fi, int *Fp, int lnz, int unz,
            int n, int nzoff)
{
    static int counter = 0;
    char strL[32];
    char strU[32];
    char strF[32];
    char counterstring[32];
    sprintf(counterstring, "%d", counter);
    strcpy(strL, "KLU_L");
    strcpy(strU, "KLU_U");
    strcpy(strF, "KLU_F");
    strcat(strL, counterstring);
    strcat(strU, counterstring);
    strcat(strF, counterstring);
    strcat(strL, ".csc");
    strcat(strU, ".csc");
    strcat(strF, ".csc");
    counter++;
    FILE *l, *u, *g;
    g = fopen(strF, "w");
    l = fopen(strL, "w");
    u = fopen(strU, "w");
    int i;
    for ( i = 0 ; i < nzoff ; i++)
    {
        printf("%d, ", Fi[i]);
    }
    for ( i = 0 ; i < n + 1 ; i++)
    {
        printf("%d, ", Fp[i]);
    }

    for (i = 0; i < nzoff - 1; i++)
    {
        fprintf(g, "%lf, ", Fx[i]);
    }
    fprintf(g, "%lf\n", Fx[nzoff-1]);
    for (i = 0; i < nzoff-1; i++)
    {
        fprintf(g, "%d, ", Fi[i]);
    }
    fprintf(g, "%d\n", Fi[nzoff-1]);
    for (i = 0; i < n ; i++)
    {
        fprintf(g, "%d, ", Fp[i]);
    }
    fprintf(g, "%d\n", Fp[n]);

    for (i = 0; i < lnz-1; i++)
    {
        fprintf(l, "%lf, ", Lx[i]);
    }
    fprintf(l, "%lf\n", Lx[lnz-1]);
    for (i = 0; i < lnz-1; i++)
    {
        fprintf(l, "%d, ", Li[i]);
    }
    fprintf(l, "%d\n", Li[lnz-1]);
    for (i = 0; i < n; i++)
    {
        fprintf(l, "%d, ", Lp[i]);
    }
    fprintf(l, "%d\n", Lp[n]);

    for (i = 0; i < unz-1; i++)
    {
        fprintf(u, "%lf, ", Ux[i]);
    }
    fprintf(u, "%lf\n", Ux[unz-1]);
    for (i = 0; i < unz-1; i++)
    {
        fprintf(u, "%d, ", Ui[i]);
    }
    fprintf(u, "%d\n", Ui[unz-1]);
    for (i = 0; i < n; i++)
    {
        fprintf(u, "%d, ", Up[i]);
    }
    fprintf(u, "%d\n", Up[n]);

    fclose(l);
    fclose(u);
    fclose(g);
}

/*
 * Main function. Expects a factorized matrix and "changeVector", which contains columns of A that change
 * e.g. changeVector = {3, 5}, if columns 3 and 5 contain varying entries. Needs to be permuted, since
 * L*U = P*A*Q
 */
int KLU_compute_path(KLU_symbolic *Symbolic, KLU_numeric *Numeric, KLU_common *Common, Int *changeVector, Int changeLen)
{
    /* Declarations */
    /* LU data */
    int n = Symbolic->n;
    int lnz = Numeric->lnz;
    int unz = Numeric->unz;
    int nzoff = Numeric->nzoff;
    int nb = Symbolic->nblocks;
    int *Lp, *Li, *Up, *Ui, *Fi, *Fp;
    double *Lx, *Ux, *Fx;
    int *P, *Q, *R;
    double *Rs;
    // int* Pinv = Numeric->Pinv;

    int RET;

    /* indices and temporary variables */
    int i, k, j, ent;
    int pivot;
    int col, nextcol;
    int u_closest, l_closest;

    // blocks
    Int k2, k1, nk;
    // TODO: save sizeof(...) statically and not call for each alloc
    Int *Qi = calloc(n, sizeof(Int));
    Int *changeVector_permuted = calloc(changeLen, sizeof(Int));

    if (Numeric->path)
    {
        KLU_free(Numeric->path, n, sizeof(int), Common);
    }
    if (Numeric->bpath)
    {
        KLU_free(Numeric->bpath, Numeric->nblocks, sizeof(int), Common);
    }

    Numeric->path = KLU_malloc(n, sizeof(int), Common);
    Numeric->bpath = KLU_malloc(nb, sizeof(int), Common);

    for (i = 0; i < n; i++)
    {
        Numeric->path[i] = 0;
    }
    for (i = 0; i < nb; i++)
    {
        Numeric->bpath[i] = 0;
    }

    // TODO: solve smarter, no more klu_extracts.
    Lp = calloc(n + 1, sizeof(int));
    Up = calloc(n + 1, sizeof(int));
    Fp = calloc(n + 1, sizeof(int));
    Lx = calloc(lnz, sizeof(double));
    Ux = calloc(unz, sizeof(double));
    Fx = calloc(nzoff, sizeof(double));
    Li = calloc(lnz, sizeof(int));
    Ui = calloc(unz, sizeof(int));
    Fi = calloc(nzoff, sizeof(int));
    P = calloc(n, sizeof(int));
    Q = calloc(n, sizeof(int));
    Rs = calloc(n, sizeof(double));
    R = calloc(nb + 1, sizeof(int));

    // first, get LU decomposition
    // sloppy implementation, as there might be a smarter way to do this
    RET = klu_extract(Numeric, Symbolic, Lp, Li, Lx, Up, Ui, Ux, Fp, Fi, Fx, P, Q, Rs, R, Common);
    //saveLU(Lx, Li, Lp, Ux, Ui, Up, Fx, Fi, Fp, lnz, unz, n, nzoff);
    if (RET != (TRUE))
    {
        return (FALSE);
    }

    // second, invert permutation vector
    // Q gives "oldcol", we need "newcol"
    for (i = 0; i < n; i++)
    {
        Qi[Q[i]] = i;
    }

    // third, apply permutation on changeVector
    for (i = 0; i < changeLen; i++)
    {
        changeVector_permuted[i] = Qi[changeVector[i]];
    }

    for(i=0 ; i < changeLen ; i++)
    {
        printf("%d, ", changeVector_permuted[i]);
    }

    // fourth, sort permuted vector
    // in "full partial refactorisation", only find minimum value
    // sloppy selectionsort implementation for first design
    // for (i = 0 ; i < changeLen ; i++)
    // {
    //     k = i;
    //     pivot = changeVector_permuted [i];
    //     for (j = i + 1 ; j < changeLen ; j++)
    //     {
    //         if (changeVector_permuted [j] < pivot)
    //         {
    //             pivot = changeVector_permuted [j];
    //             k = j;
    //         }
    //     }
    //     if (k != i)
    //     {
    //         // found smaller
    //         // switch positions
    //         int tmp = changeVector_permuted [i];
    //         changeVector_permuted [i] = pivot;
    //         changeVector_permuted [k] = tmp;
    //     }
    // }

    // step three and four can / should be done externally

    if (Common->btf == FALSE)
    {
        Numeric->bpath[0] = 1;
        /* no blocks */
        for (i = 0; i < changeLen; i++)
        {
            pivot = changeVector_permuted[i];
            if (Numeric->path[pivot] == 1)
            {
                continue;
            }
            Numeric->path[pivot] = 1;
            while (pivot < n)
            {

                u_closest = n + 1;
                l_closest = n + 1;

                // find closest off-diagonal entry in L
                col = Lp[pivot];
                nextcol = Lp[pivot + 1];

                if (nextcol - col == 1)
                {
                    // only one entry in column => diagonal entry
                    l_closest = n + 1;
                }
                else
                {
                    // indices are not sorted!!!! TODO. Use klu_sort maybe?

                    // find closest off-diagonal entry in L, "look down" in pivot-th column
                    for (ent = col + 1; ent < nextcol; ent++)
                    {
                        if (l_closest > Li[ent])
                        {
                            l_closest = Li[ent];
                        }
                    }
                }
                if (l_closest - pivot == 1)
                {
                    // l_closest = pivot + 1, there can't be a closer off-diagonal row entry. save computation time.
                    goto minimum;
                }

                // find closest off-diagonal entry in U
                /* u saved in csc:
                   Ux = [x, x, x, x, x] entries saved column-wise
                   Ui = [0, 2, 1, 4, 3] row position of those entries in columns
                   Up = [0, 1, 2, 3, 4, 5] column pointers
                   to find closest off-diagonal of row k:
                   find all Ui[j] == pivot
                   find Up[k] <= j < Up[k+1], k > pivot
                   ALTERNATIVELY
                   Transpose U
                   do the same as for L
                 */
                for (j = 0; j < unz; j++)
                {
                    if (Ui[j] == pivot)
                    {
                        // check if Ui[j] is right to pivot
                        for (k = 0; k < n + 1; k++)
                        {
                            if (Up[k] <= j && j < Up[k + 1])
                            {
                                if (k > pivot)
                                {
                                    // found closest off-diagonal
                                    u_closest = k;
                                    // don't hate me
                                    goto minimum;
                                    // alternatively: j=unz+1
                                }
                                break;
                            }
                        }
                    }
                }
            minimum:
                pivot = MIN(l_closest, u_closest);
                // check if pivot is either already in path or n+1 (no more off-diag values)
                if (Numeric->path[pivot] == 1 || pivot == n + 1)
                {
                    break;
                }
                Numeric->path[pivot] = 1;
            }
        }
    }
    else
    {
        // fifth, compute factorization path
        for (i = 0; i < changeLen; i++)
        {
            // get next changing column
            pivot = changeVector_permuted[i];

            // check if it was already computed
            if (Numeric->path[pivot] == 1)
            {
                // already computed pivot
                // do nothing, go to next pivot
                continue;
            }

            // set first value of singleton path
            Numeric->path[pivot] = 1;

            // find block of pivot
            for (k = 0; k < nb; k++)
            {
                k1 = R[k];
                k2 = R[k + 1];
                if (k1 <= pivot && pivot < k2)
                {
                    nk = k2 - k1;

                    /* set varying block */
                    Numeric->bpath[k] = 1;
                    break;
                }
            }

            if (nk == 1)
            {
                // 1x1-block, its pivot already in path
                continue;
            }

            // propagate until end
            // in blocks, pivot < n_block[k]
            while (pivot < k2)
            {
                u_closest = k2;
                l_closest = k2;

                // find closest off-diagonal entry in L
                col = Lp[pivot];
                nextcol = Lp[pivot + 1];

                if (nextcol - col == 1)
                {
                    // only one entry in column => diagonal entry
                    l_closest = k2;
                }
                else
                {
                    // indices are not sorted!!!! TODO. Use klu_sort maybe?

                    // find closest off-diagonal entry in L, "look down" in pivot-th column
                    for (ent = col + 1; ent < nextcol; ent++)
                    {
                        if (l_closest > Li[ent])
                        {
                            l_closest = Li[ent];
                        }
                    }
                }
                if (l_closest - pivot == 1)
                {
                    // l_closest = pivot + 1, there can't be a closer off-diagonal row entry. save computation time.
                    goto minimum_btf;
                }

                // find closest off-diagonal entry in U
                /* u saved in csc:
                   Ux = [x, x, x, x, x] entries saved column-wise
                   Ui = [0, 2, 1, 4, 3] row position of those entries in columns
                   Up = [0, 1, 2, 3, 4, 5] column pointers
                   to find closest off-diagonal of row k:
                   find all Ui[j] == pivot
                   find Up[k] <= j < Up[k+1], k > pivot
                   ALTERNATIVELY
                   Transpose U
                   do the same as for L
                 */
                for (j = 0; j < unz; j++)
                {
                    if (Ui[j] == pivot)
                    {
                        // check if Ui[j] is right to pivot
                        for (k = 0; k < n + 1; k++)
                        {
                            if (Up[k] <= j && j < Up[k + 1])
                            {
                                if (k > pivot)
                                {
                                    // found closest off-diagonal
                                    u_closest = k;
                                    // don't hate me
                                    goto minimum_btf;
                                    // alternatively: j=unz+1
                                }
                                break;
                            }
                        }
                    }
                }
            minimum_btf:
                pivot = MIN(l_closest, u_closest);
                // check if pivot is either already in path or n+1 (no more off-diag values)
                if (Numeric->path[pivot] == 1 || pivot == k2) // n+1)
                {
                    break;
                }
                Numeric->path[pivot] = 1;
            }
        }
    }
    printf("Number of blocks: %d\n", nb);
    printf("Path: ");
    for (int i = 0; i < n - 1; i++)
    {
        printf("%d, ", Numeric->path[i]);
    }
    printf("%d\n", Numeric->path[n-1]);
    free(Lp);
    free(Li);
    free(Lx);
    free(Up);
    free(Ui);
    free(Ux);
    free(Fi);
    free(Fp);
    free(Fx);
    free(P);
    free(Q);
    free(Qi);
    free(R);
    free(Rs);
    free(changeVector_permuted);
    return (TRUE);
}

/*
 * Secondary function. Expects a factorized matrix and "changeVector", which contains columns of A that change
 * e.g. changeVector = {3, 5}, if columns 3 and 5 contain varying entries. Needs to be permuted, since
 * L*U = P*A*Q
 */
int KLU_compute_path2(KLU_symbolic *Symbolic, KLU_numeric *Numeric, KLU_common *Common, Int *changeVector,
                      Int changeLen)
{
    /* Declarations */
    /* LU data */
    int n = Symbolic->n;
    int lnz = Numeric->lnz;
    int unz = Numeric->unz;
    int nzoff = Numeric->nzoff;
    int nb = Symbolic->nblocks;
    int *Lp, *Li, *Up, *Ui, *Fi, *Fp;
    double *Lx, *Ux, *Fx;
    int *P, *Q, *R;
    double *Rs;

    int RET;

    /* indices and temporary variables */
    int i, k, j, ent;
    int pivot;
    int col, nextcol;
    int u_closest, l_closest;
    int flag = 1;

    // blocks
    Int k2, k1, nk;

    // TODO: save sizeof(...) statically and not call for each alloc
    Int *Qi = calloc(n, sizeof(Int));
    Int *changeVector_permuted = calloc(changeLen, sizeof(Int));

    if (Numeric->path)
    {
        KLU_free(Numeric->path, n, sizeof(int), Common);
    }
    if (Numeric->bpath)
    {
        KLU_free(Numeric->bpath, Numeric->nblocks, sizeof(int), Common);
    }

    Numeric->path = KLU_malloc(n, sizeof(int), Common);
    Numeric->bpath = KLU_malloc(nb, sizeof(int), Common);
    int *workpath = (int *)calloc(n, sizeof(int));

    for (i = 0; i < n; i++)
    {
        Numeric->path[i] = 0;
        workpath[i] = 0;
    }
    for (i = 0; i < nb; i++)
    {
        Numeric->bpath[i] = 0;
    }

    // TODO: solve smarter, no more klu_extracts.
    Lp = calloc(n + 1, sizeof(int));
    Up = calloc(n + 1, sizeof(int));
    Fp = calloc(n + 1, sizeof(int));
    Lx = calloc(lnz, sizeof(double));
    Ux = calloc(unz, sizeof(double));
    Fx = calloc(nzoff, sizeof(double));
    Li = calloc(lnz, sizeof(int));
    Ui = calloc(unz, sizeof(int));
    Fi = calloc(nzoff, sizeof(int));
    P = calloc(n, sizeof(int));
    Q = calloc(n, sizeof(int));
    Rs = calloc(n, sizeof(double));
    R = calloc(nb + 1, sizeof(int));

    // first, get LU decomposition
    // sloppy implementation, as there might be a smarter way to do this
    RET = klu_extract(Numeric, Symbolic, Lp, Li, Lx, Up, Ui, Ux, Fp, Fi, Fx, P, Q, Rs, R, Common);
    //saveLU(Lx, Li, Lp, Ux, Ui, Up, Fx, Fi, Fp, lnz, unz, n, nzoff);
    if (RET != (TRUE))
    {
        return (FALSE);
    }

    printf("Change-Vec: ");
    for(i=0 ; i < changeLen ; i++)
    {
        printf("%d, ", changeVector[i]);
    }
    printf("\n");


    // second, invert permutation vector
    // Q gives "oldcol", we need "newcol"
    for (i = 0; i < n; i++)
    {
        Qi[Q[i]] = i;
    }
    Int *Pi = calloc(n, sizeof(Int));
    for (i = 0; i < n; i++)
    {
        Pi[P[i]] = i;
    }

    printf("Pi: ");
    for (i = 0; i < n-1 ; i++)
    {
        printf("%d, ", Pi[i]);
    }
    printf("%d\n", Pi[n-1]);
    free(Pi);
    printf("Qi: ");
    for (i = 0; i < n-1 ; i++)
    {
        printf("%d, ", Qi[i]);
    }
    printf("%d\n", Qi[n-1]);


    printf("P: ");
    for (i = 0; i < n-1 ; i++)
    {
        printf("%d, ", P[i]);
    }
    printf("%d\n", P[n-1]);
    printf("Q: ");
    for (i = 0; i < n-1 ; i++)
    {
        printf("%d, ", Q[i]);
    }
    printf("%d\n", Q[n-1]);



    // third, apply permutation on changeVector
    for (i = 0; i < changeLen; i++)
    {
        changeVector_permuted[i] = Qi[changeVector[i]];
    }
    printf("Permuted Vec: ");
    for(i=0 ; i < changeLen ; i++)
    {
        printf("%d, ", changeVector_permuted[i]);
    }
    printf("\n");

    // step three can / should be done externally

    // fourth, compute factorization path
    if (Common->btf == FALSE)
    {
        Numeric->bpath[0] = 1;
        /* no blocks */
        for (i = 0; i < changeLen; i++)
        {
            pivot = changeVector_permuted[i];
            if (Numeric->path[pivot] == 1)
            {
                // already computed pivot?
                continue;
            }
            Numeric->path[pivot] = 1;
            while (pivot < n && flag)
            {
                flag = 0;
                nextcol = Up[pivot + 1];

                // scan Ui for values in row "pivot"
                for (j = nextcol; j < unz; j++)
                {
                    if (Ui[j] == pivot)
                    {
                        // column j is affected
                        // find column in which j is
                        for (k = 0; k < n; k++)
                        {
                            if (j >= Up[k] && j < Up[k + 1])
                            {
                                col = k;
                                break;
                            }
                        }
                        Numeric->path[col] = 1;
                        workpath[col] = 1;
                    }
                }
                for (j = pivot + 1; j < n; j++)
                {
                    if (workpath[j] == 1)
                    {
                        workpath[j] = 0;
                        pivot = j;
                        flag = 1;
                        break;
                    }
                }
            }
        }
    }
    else
    {
        for (i = 0; i < changeLen; i++)
        {
            flag = 1;
            // get next changing column
            pivot = changeVector_permuted[i];

            // check if it was already computed
            if (Numeric->path[pivot] == 1)
            {
                // already computed pivot
                // do nothing, go to next pivot
                continue;
            }

            // set first value of singleton path
            Numeric->path[pivot] = 1;

            // find block of pivot
            for (k = 0; k < nb; k++)
            {
                k1 = R[k];
                k2 = R[k + 1];
                if (k1 <= pivot && pivot < k2)
                {
                    nk = k2 - k1;

                    /* set varying block */
                    Numeric->bpath[k] = 1;
                    break;
                }
            }

            if (nk == 1)
            {
                // 1x1-block, its pivot already in path
                continue;
            }

            /* propagate until end
             * in blocks, pivot < n_block[k]
             *
             * k2 is the first column of the NEXT block
             *
             * if pivot == k2 - 1, it is the last column of the
             * current block. then, "nextcol" would be k2, which is already
             * in the next block. Thus, only do loop if pivot < k2 - 1
             */
            while (pivot < k2 - 1 && flag == 1)
            {
                flag = 0;
                nextcol = Up[pivot + 1];
                // find closest off-diagonal entry in U
                /* u saved in csc:
                   Ux = [x, x, x, x, x] entries saved column-wise
                   Ui = [0, 2, 1, 4, 3] row position of those entries in columns
                   Up = [0, 1, 2, 3, 4, 5] column pointers
                   to find closest off-diagonal of row k:
                   find all Ui[j] == pivot
                   find Up[k] <= j < Up[k+1], k > pivot
                   ALTERNATIVELY
                   Transpose U
                   do the same as for L
                 */

                /* This loop finds all nnz in row "pivot",
                 * meaning their respective columns depend on pivot column
                 */
                for (j = nextcol; j < unz; j++)
                {
                    if (Ui[j] == pivot)
                    {
                        // find column in which j is
                        for (k = k1; k < k2; k++)
                        {
                            if (j >= Up[k] && j < Up[k + 1])
                            {
                                col = k;
                                Numeric->path[col] = 1;
                                workpath[col] = 1;
                                break;
                            }
                        }
                    }
                }
                for (j = pivot + 1; j < k2; j++)
                {
                    if (workpath[j] == 1)
                    {
                        workpath[j] = 0;
                        pivot = j;
                        flag = 1;
                        break;
                    }
                }
            }
        }
    }

    // for ( i = 0 ; i < n ; i ++)
    // {
    //     Numeric->path[i] = 1;
    // }
    // for ( i = 0 ; i < nb ; i++)
    // {
    //     Numeric->bpath[i] = 1;
    // }

    // static int counter = 0;
    // if (counter==1)
    // {
    //   for( i = 0 ; i < 6; i ++)
    //   {
    //     Numeric->path[i] = 1;
    //     Numeric->bpath[i] = 1;
    //   }
    // }
    // counter++;

    printf("Block path: ");
    for (int i = 0; i < nb - 1; i++)
    {
        printf("%d, ", Numeric->bpath[i]);
    }
    printf("%d\n", Numeric->bpath[nb-1]);

    printf("Path: ");
    for (int i = 0; i < n - 1; i++)
    {
        printf("%d, ", Numeric->path[i]);
    }
    printf("%d\n", Numeric->path[n-1]);
    free(workpath);
    free(Lp);
    free(Li);
    free(Lx);
    free(Up);
    free(Ui);
    free(Ux);
    free(Fi);
    free(Fp);
    free(Fx);
    free(P);
    free(Q);
    free(Qi);
    free(R);
    free(Rs);
    free(changeVector_permuted);
    return (TRUE);
}
