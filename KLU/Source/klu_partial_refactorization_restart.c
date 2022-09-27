/* ==========================================================================
 */
/* === KLU_partial ==========================================================
 */
/* ==========================================================================
 */

/* Factor the matrix, after ordering and analyzing it with KLU_analyze,
 * factoring it once with KLU_factor, and computing first variable entries in each block.
 * This routine cannot do any numerical pivoting.  The pattern of the
 * input matrix (Ap, Ai) must be identical to the pattern given to
 * KLU_factor.
 */

#include "klu_internal.h"
#include <string.h>

/* ========================================================================== */
/* === KLU_partial_refactorization_restart ================================== */
/* ========================================================================== */

Int KLU_partial_refactorization_restart /* returns TRUE if successful, FALSE otherwise */
    (
        /* inputs, not modified */
        Int Ap[],                            /* size n+1, column pointers */
        Int Ai[],                            /* size nz, row indices */
        double Ax[], KLU_symbolic *Symbolic, /* now also contains factorization path */

        /* input/output */
        KLU_numeric *Numeric, KLU_common *Common
        )
{
    Entry ukk, ujk, s;
    Entry *Offx, *Lx, *Ux, *X, *Az, *Udiag;
    double *Rs;
    Int *Q, *R, *Pnum, *Ui, *Li, *Pinv, *Lip, *Uip, *Llen, *Ulen;
    Unit **LUbx;
    Unit *LU;
    Int k1, k2, nk, k, block, oldcol, pend, oldrow, n, p, newrow, scale, nblocks, poff, i, j, up, ulen, llen, maxblock,
        nzoff;

    #ifdef KLU_PRINT
        /* print out flops as printing feature */
        Int countflops = 0;
    #endif

    Int variable_offdiag_length = Numeric->variable_offdiag_length;
    Int n_variable_blocks = Numeric->n_variable_blocks;
    int* variable_offdiag_perm_entry = Numeric->variable_offdiag_perm_entry;
    int *variable_offdiag_orig_entry = Numeric->variable_offdiag_orig_entry;

    /* ---------------------------------------------------------------------- */
    /* check inputs */
    /* ---------------------------------------------------------------------- */

    if (Common == NULL)
    {
        return (FALSE);
    }
    Common->status = KLU_OK;

    if (Numeric == NULL)
    {
        /* invalid Numeric object */
        Common->status = KLU_INVALID;
        return (FALSE);
    }

    Common->numerical_rank = EMPTY;
    Common->singular_col = EMPTY;

    Az = (Entry *)Ax;

    /* ---------------------------------------------------------------------- */
    /* get the contents of the Symbolic object */
    /* ---------------------------------------------------------------------- */

    n = Symbolic->n;
    Q = Symbolic->Q;
    R = Symbolic->R;
    nblocks = Symbolic->nblocks;
    maxblock = Symbolic->maxblock;

    /* ---------------------------------------------------------------------- */
    /* get the contents of the Numeric object */
    /* ---------------------------------------------------------------------- */

    Pnum = Numeric->Pnum;
    Offx = (Entry *)Numeric->Offx;

    LUbx = (Unit **)Numeric->LUbx;

    scale = Common->scale;
    if (scale > 0)
    {
        /* factorization was not scaled, but refactorization is scaled */
        if (Numeric->Rs == NULL)
        {
            Numeric->Rs = KLU_malloc(n, sizeof(double), Common);
            if (Common->status < KLU_OK)
            {
                Common->status = KLU_OUT_OF_MEMORY;
                return (FALSE);
            }
        }
    }
    else
    {
        /* no scaling for refactorization; ensure Numeric->Rs is freed.  This
         * does nothing if Numeric->Rs is already NULL. */
        Numeric->Rs = KLU_free(Numeric->Rs, n, sizeof(double), Common);
    }
    Rs = Numeric->Rs;

    Pinv = Numeric->Pinv;
    X = (Entry *)Numeric->Xwork;
    Common->nrealloc = 0;
    Udiag = Numeric->Udiag;
    nzoff = Symbolic->nzoff;
    /* ---------------------------------------------------------------------- */
    /* check the input matrix compute the row scale factors, Rs */
    /* ---------------------------------------------------------------------- */

    /* permute scaling Rs back */
    if (scale > 0)
    {
        for (k = 0; k < n; k++)
        {
            REAL(X[k]) = Rs[Pinv[k]];
        }
        for (k = 0; k < n; k++)
        {
            Rs[k] = REAL(X[k]);
        }
    }

    /* ---------------------------------------------------------------------- */
    /* clear workspace X */
    /* ---------------------------------------------------------------------- */

    for (k = 0; k < maxblock; k++)
    {
        /* X [k] = 0 */
        CLEAR(X[k]);
    }

    /* ---------------------------------------------------------------------- */
    /* assemble off-diagonal blocks */
    /* ---------------------------------------------------------------------- */

    poff = 0;

    /* ---------------------------------------------------------------------- */
    /* assemble off-diagonal blocks */
    /* ---------------------------------------------------------------------- */

    if (scale <= 0)
    {
        for (i = 0; i < variable_offdiag_length ; i++)
        {
            Offx[variable_offdiag_perm_entry[i]] = Az[variable_offdiag_orig_entry[i]];
        }
    }
    else
    {
        for (i = 0; i < variable_offdiag_length ; i++)
        {
            SCALE_DIV_ASSIGN(Offx[variable_offdiag_perm_entry[i]], Az[variable_offdiag_orig_entry[i]], Rs[Ai[variable_offdiag_orig_entry[i]]]);
        }
    }

    /* ---------------------------------------------------------------------- */
    /* factor each block */
    /* ---------------------------------------------------------------------- */

    if (scale <= 0)
    {
        /* ------------------------------------------------------------------ */
        /* no scaling */
        /* ------------------------------------------------------------------ */

        for (i = 0; i < n_variable_blocks; i++)
        {
            block = Numeric->variable_block[i];

            /* -------------------------------------------------------------- */
            /* the block is from rows/columns k1 to k2-1 */
            /* -------------------------------------------------------------- */

            k1 = R[block];
            k2 = R[block + 1];
            nk = k2 - k1;

            if (nk == 1)
            {
                /* ---------------------------------------------------------- */
                /* singleton case */
                /* ---------------------------------------------------------- */

                oldcol = Q[k1];
                pend = Ap[oldcol + 1];
                CLEAR(s);
                for (p = Ap[oldcol]; p < pend; p++)
                {
                    newrow = Pinv[Ai[p]] - k1;
                    if(newrow >= 0)
                    {
                        /* singleton */
                        s = Az[p];
                    }
                }
                Udiag[k1] = s;
            }
            else
            {
                /* ---------------------------------------------------------- */
                /* construct and factor the kth block */
                /* ---------------------------------------------------------- */
                Lip = Numeric->Lip + k1;
                Llen = Numeric->Llen + k1;
                Uip = Numeric->Uip + k1;
                Ulen = Numeric->Ulen + k1;
                LU = LUbx[block];

                for (k = Numeric->block_path[block] - k1; k < nk ; k++)
                {
                    /* ------------------------------------------------------ */
                    /* scatter kth column of the block into workspace X */
                    /* ------------------------------------------------------ */

                    oldcol = Q[k + k1];
                    pend = Ap[oldcol + 1];
                    for (p = Ap[oldcol]; p < pend; p++)
                    {
                        newrow = Pinv[Ai[p]] - k1;
                        if(newrow >= 0)
                        {
                            /* (newrow,k) is an entry in the block */
                            X[newrow] = Az[p];
                        }
                    }

                    /* ------------------------------------------------------ */
                    /* compute kth column of U, and update kth column of A */
                    /* ------------------------------------------------------ */

                    GET_POINTER(LU, Uip, Ulen, Ui, Ux, k, ulen);
                    for (up = 0; up < ulen; up++)
                    {
                        j = Ui[up];
                        ujk = X[j];
                        /* X [j] = 0 */
                        CLEAR(X[j]);
                        Ux[up] = ujk;
                        GET_POINTER(LU, Lip, Llen, Li, Lx, j, llen);

                        for (p = 0; p < llen; p++)
                        {
                            /* X [Li [p]] -= Lx [p] * ujk */
                            MULT_SUB(X[Li[p]], Lx[p], ujk);
                            #ifdef KLU_PRINT
                                countflops += MULTSUB_FLOPS;
                            #endif
                        }
                    }
                    /* get the diagonal entry of U */
                    ukk = X[k];
                    /* X [k] = 0 */
                    CLEAR(X[k]);
                    /* singular case */
                    if (IS_ZERO(ukk))
                    {
                        /* matrix is numerically singular */
                        Common->status = KLU_SINGULAR;
                        if (Common->numerical_rank == EMPTY)
                        {
                            Common->numerical_rank = k + k1;
                            Common->singular_col = Q[k + k1];
                        }
                        if (Common->halt_if_singular)
                        {
                            /* do not continue the factorization */
                            return (FALSE);
                        }
                    }
                    /* pivot vadility testing */
                    else if (SCALAR_ABS(ukk) < Common->pivot_tol_fail)
                    {
                        /* pivot is too small */
                        Common->status = KLU_PIVOT_FAULT;
                        if (Common->halt_if_pivot_fails)
                        {
                            /* do not continue the factorization */
                            return (FALSE);
                        }
                    }
                    Udiag[k + k1] = ukk;
                    /* gather and divide by pivot to get kth column of L
                        */
                    GET_POINTER(LU, Lip, Llen, Li, Lx, k, llen);

                    for (p = 0; p < llen; p++)
                    {
                        i = Li[p];
                        DIV(Lx[p], X[i], ukk);
                        CLEAR(X[i]);
                    }
                }
            }
        }
    }
    else
    {
        /* ------------------------------------------------------------------ */
        /* scaling */
        /* ------------------------------------------------------------------ */

        for (i = 0; i < n_variable_blocks; i++)
        {
            block = Numeric->variable_block[i];

            /* -------------------------------------------------------------- */
            /* the block is from rows/columns k1 to k2-1 */
            /* -------------------------------------------------------------- */

            k1 = R[block];
            k2 = R[block + 1];
            nk = k2 - k1;

            if (nk == 1)
            {
                oldcol = Q[k1];
                pend = Ap[oldcol + 1];
                CLEAR(s);
                for (p = Ap[oldcol]; p < pend; p++)
                {
                    oldrow = Ai[p];
                    newrow = Pinv[oldrow] - k1;
                    if (newrow >= 0)
                    {
                        /* singleton */
                        /* s = Az [p] / Rs [oldrow] */
                        SCALE_DIV_ASSIGN(s, Az[p], Rs[oldrow]);
                    }
                }
                Udiag[k1] = s;
            }
            else
            {
                /* ---------------------------------------------------------- */
                /* construct and factor the kth block */
                /* ---------------------------------------------------------- */
                
                Lip = Numeric->Lip + k1;
                Llen = Numeric->Llen + k1;
                Uip = Numeric->Uip + k1;
                Ulen = Numeric->Ulen + k1;
                LU = LUbx[block];

                for (k = Numeric->block_path[block] - k1; k < nk ; k++)
                {
                    
                    /* ------------------------------------------------------ */
                    /* scatter kth column of the block into workspace X */
                    /* ------------------------------------------------------ */
                    
                    oldcol = Q[k + k1];
                    pend = Ap[oldcol + 1];
                    for (p = Ap[oldcol]; p < pend; p++)
                    {
                        oldrow = Ai[p];
                        newrow = Pinv[oldrow] - k1;
                        if(newrow >= 0)
                        {
                            /* (newrow,k) is an entry in the block */
                            /* X [newrow] = Az [p] / Rs [oldrow] */
                            SCALE_DIV_ASSIGN(X[newrow], Az[p], Rs[oldrow]);
                        }
                    }

                    /* ------------------------------------------------------
                        */
                    /* compute kth column of U, and update kth column of
                        * A */
                    /* ------------------------------------------------------
                        */

                    GET_POINTER(LU, Uip, Ulen, Ui, Ux, k, ulen);
                    for (up = 0; up < ulen; up++)
                    {
                        j = Ui[up];
                        ujk = X[j];
                        /* X [j] = 0 */
                        CLEAR(X[j]);
                        Ux[up] = ujk;
                        GET_POINTER(LU, Lip, Llen, Li, Lx, j, llen);

                        for (p = 0; p < llen; p++)
                        {
                            /* X [Li [p]] -= Lx [p] * ujk */
                            MULT_SUB(X[Li[p]], Lx[p], ujk);
                            #ifdef KLU_PRINT
                                countflops += MULTSUB_FLOPS;
                            #endif
                        }
                    }
                    /* get the diagonal entry of U */
                    ukk = X[k];
                    /* X [k] = 0 */
                    CLEAR(X[k]);
                    /* singular case */
                    if (IS_ZERO(ukk))
                    {
                        /* matrix is numerically singular */
                        Common->status = KLU_SINGULAR;
                        if (Common->numerical_rank == EMPTY)
                        {
                            Common->numerical_rank = k + k1;
                            Common->singular_col = Q[k + k1];
                        }
                        if (Common->halt_if_singular)
                        {
                            /* do not continue the factorization */
                            return (FALSE);
                        }
                    }
                    /* pivot vadility testing */
                    else if (SCALAR_ABS(ukk) < Common->pivot_tol_fail)
                    {
                        /* pivot is too small */
                        Common->status = KLU_PIVOT_FAULT;
                        if (Common->halt_if_pivot_fails)
                        {
                            /* do not continue the factorization */
                            return (FALSE);
                        }
                    }
                    Udiag[k + k1] = ukk;
                    /* gather and divide by pivot to get kth column of L
                        */
                    GET_POINTER(LU, Lip, Llen, Li, Lx, k, llen);
                    for (p = 0; p < llen; p++)
                    {
                        i = Li[p];
                        DIV(Lx[p], X[i], ukk);
                        CLEAR(X[i]);
                    }
                }
            }
        }
    }

    /* ---------------------------------------------------------------------- */
    /* permute scale factors Rs according to pivotal row order */
    /* ---------------------------------------------------------------------- */

    if (scale > 0)
    {
        for (k = 0; k < n; k++)
        {
            REAL(X[k]) = Rs[Pnum[k]];
        }
        for (k = 0; k < n; k++)
        {
            Rs[k] = REAL(X[k]);
        }
    }

#ifndef NDEBUG
    ASSERT(Numeric->Offp[n] == poff);
    ASSERT(Symbolic->nzoff == poff);
    PRINTF(("\n------------------- Off diagonal entries, new:\n"));
    ASSERT(KLU_valid(n, Numeric->Offp, Numeric->Offi, Offx));
    if (Common->status == KLU_OK)
    {
        PRINTF(("\n ########### KLU_BTF_REFACTOR done, nblocks %d\n", nblocks));
        for (block = 0; block < nblocks; block++)
        {
            k1 = R[block];
            k2 = R[block + 1];
            nk = k2 - k1;
            PRINTF(("\n================KLU_refactor output: k1 %d k2 %d nk %d\n", k1, k2, nk));
            if (nk == 1)
            {
                PRINTF(("singleton  "));
                PRINT_ENTRY(Udiag[k1]);
            }
            else
            {
                Lip = Numeric->Lip + k1;
                Llen = Numeric->Llen + k1;
                LU = (Unit *)Numeric->LUbx[block];
                PRINTF(("\n---- L block %d\n", block));
                ASSERT(KLU_valid_LU(nk, TRUE, Lip, Llen, LU));
                Uip = Numeric->Uip + k1;
                Ulen = Numeric->Ulen + k1;
                PRINTF(("\n---- U block %d\n", block));
                ASSERT(KLU_valid_LU(nk, FALSE, Uip, Ulen, LU));
            }
        }
    }
#endif
#ifdef KLU_PRINT
    static int counter = 0;
    if(counter == 0 || counter == 1000)
    {
        int n = Symbolic->n;
        int lnz = Numeric->lnz;
        int unz = Numeric->unz;
        int nzoff = Numeric->nzoff;
        int nb = Symbolic->nblocks;
        int *Lp, *Li, *Up, *Ui, *Fi, *Fp;
        double *Lx, *Ux, *Fx;
        int *P, *Q, *R;
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

        klu_extract(Numeric, Symbolic, Lp, Li, Lx, Up, Ui, Ux, Fp, Fi, Fx, P, Q, Rs, R, Common);
        dumpKAll(Lx, Li, Lp, Ux, Ui, Up, Fx, Fi, Fp, P, Q, Numeric->path, Numeric->block_path, lnz, unz, n, nzoff, nb, Numeric->pathLen);
        dumpKA(Ax, Ai, Ap, n);

        printf("FLOPS %d\n", countflops);

        free(Lp);
        free(Up);
        free(Fp);
        free(Lx);
        free(Ux);
        free(Fx);
        free(Li);
        free(Ui);
        free(Fi);
        free(P);
        free(Q);
        free(Rs);
        free(R);
    }
    counter++;
#endif
    return (TRUE);
}
