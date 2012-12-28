
#include "sqlite3-vs10_1.c"
#include "sqlite3-vs10_2.c"

/*
** Disable a term in the WHERE clause.  Except, do not disable the term
** if it controls a LEFT OUTER JOIN and it did not originate in the ON
** or USING clause of that join.
**
** Consider the term t2.z='ok' in the following queries:
**
**   (1)  SELECT * FROM t1 LEFT JOIN t2 ON t1.a=t2.x WHERE t2.z='ok'
**   (2)  SELECT * FROM t1 LEFT JOIN t2 ON t1.a=t2.x AND t2.z='ok'
**   (3)  SELECT * FROM t1, t2 WHERE t1.a=t2.x AND t2.z='ok'
**
** The t2.z='ok' is disabled in the in (2) because it originates
** in the ON clause.  The term is disabled in (3) because it is not part
** of a LEFT OUTER JOIN.  In (1), the term is not disabled.
**
** IMPLEMENTATION-OF: R-24597-58655 No tests are done for terms that are
** completely satisfied by indices.
**
** Disabling a term causes that term to not be tested in the inner loop
** of the join.  Disabling is an optimization.  When terms are satisfied
** by indices, we disable them to prevent redundant tests in the inner
** loop.  We would get the correct results if nothing were ever disabled,
** but joins might run a little slower.  The trick is to disable as much
** as we can without disabling too much.  If we disabled in (1), we'd get
** the wrong answer.  See ticket #813.
*/
static void disableTerm(WhereLevel *pLevel, WhereTerm *pTerm){
  if( pTerm
      && (pTerm->wtFlags & TERM_CODED)==0
      && (pLevel->iLeftJoin==0 || ExprHasProperty(pTerm->pExpr, EP_FromJoin))
  ){
    pTerm->wtFlags |= TERM_CODED;
    if( pTerm->iParent>=0 ){
      WhereTerm *pOther = &pTerm->pWC->a[pTerm->iParent];
      if( (--pOther->nChild)==0 ){
        disableTerm(pLevel, pOther);
      }
    }
  }
}

/*
** Code an OP_Affinity opcode to apply the column affinity string zAff
** to the n registers starting at base. 
**
** As an optimization, SQLITE_AFF_NONE entries (which are no-ops) at the
** beginning and end of zAff are ignored.  If all entries in zAff are
** SQLITE_AFF_NONE, then no code gets generated.
**
** This routine makes its own copy of zAff so that the caller is free
** to modify zAff after this routine returns.
*/
static void codeApplyAffinity(Parse *pParse, int base, int n, char *zAff){
  Vdbe *v = pParse->pVdbe;
  if( zAff==0 ){
    assert( pParse->db->mallocFailed );
    return;
  }
  assert( v!=0 );

  /* Adjust base and n to skip over SQLITE_AFF_NONE entries at the beginning
  ** and end of the affinity string.
  */
  while( n>0 && zAff[0]==SQLITE_AFF_NONE ){
    n--;
    base++;
    zAff++;
  }
  while( n>1 && zAff[n-1]==SQLITE_AFF_NONE ){
    n--;
  }

  /* Code the OP_Affinity opcode if there is anything left to do. */
  if( n>0 ){
    sqlite3VdbeAddOp2(v, OP_Affinity, base, n);
    sqlite3VdbeChangeP4(v, -1, zAff, n);
    sqlite3ExprCacheAffinityChange(pParse, base, n);
  }
}


/*
** Generate code for a single equality term of the WHERE clause.  An equality
** term can be either X=expr or X IN (...).   pTerm is the term to be 
** coded.
**
** The current value for the constraint is left in register iReg.
**
** For a constraint of the form X=expr, the expression is evaluated and its
** result is left on the stack.  For constraints of the form X IN (...)
** this routine sets up a loop that will iterate over all values of X.
*/
static int codeEqualityTerm(
  Parse *pParse,      /* The parsing context */
  WhereTerm *pTerm,   /* The term of the WHERE clause to be coded */
  WhereLevel *pLevel, /* When level of the FROM clause we are working on */
  int iTarget         /* Attempt to leave results in this register */
){
  Expr *pX = pTerm->pExpr;
  Vdbe *v = pParse->pVdbe;
  int iReg;                  /* Register holding results */

  assert( iTarget>0 );
  if( pX->op==TK_EQ ){
    iReg = sqlite3ExprCodeTarget(pParse, pX->pRight, iTarget);
  }else if( pX->op==TK_ISNULL ){
    iReg = iTarget;
    sqlite3VdbeAddOp2(v, OP_Null, 0, iReg);
#ifndef SQLITE_OMIT_SUBQUERY
  }else{
    int eType;
    int iTab;
    struct InLoop *pIn;

    assert( pX->op==TK_IN );
    iReg = iTarget;
    eType = sqlite3FindInIndex(pParse, pX, 0);
    iTab = pX->iTable;
    sqlite3VdbeAddOp2(v, OP_Rewind, iTab, 0);
    assert( pLevel->plan.wsFlags & WHERE_IN_ABLE );
    if( pLevel->u.in.nIn==0 ){
      pLevel->addrNxt = sqlite3VdbeMakeLabel(v);
    }
    pLevel->u.in.nIn++;
    pLevel->u.in.aInLoop =
       sqlite3DbReallocOrFree(pParse->db, pLevel->u.in.aInLoop,
                              sizeof(pLevel->u.in.aInLoop[0])*pLevel->u.in.nIn);
    pIn = pLevel->u.in.aInLoop;
    if( pIn ){
      pIn += pLevel->u.in.nIn - 1;
      pIn->iCur = iTab;
      if( eType==IN_INDEX_ROWID ){
        pIn->addrInTop = sqlite3VdbeAddOp2(v, OP_Rowid, iTab, iReg);
      }else{
        pIn->addrInTop = sqlite3VdbeAddOp3(v, OP_Column, iTab, 0, iReg);
      }
      sqlite3VdbeAddOp1(v, OP_IsNull, iReg);
    }else{
      pLevel->u.in.nIn = 0;
    }
#endif
  }
  disableTerm(pLevel, pTerm);
  return iReg;
}

/*
** Generate code that will evaluate all == and IN constraints for an
** index.
**
** For example, consider table t1(a,b,c,d,e,f) with index i1(a,b,c).
** Suppose the WHERE clause is this:  a==5 AND b IN (1,2,3) AND c>5 AND c<10
** The index has as many as three equality constraints, but in this
** example, the third "c" value is an inequality.  So only two 
** constraints are coded.  This routine will generate code to evaluate
** a==5 and b IN (1,2,3).  The current values for a and b will be stored
** in consecutive registers and the index of the first register is returned.
**
** In the example above nEq==2.  But this subroutine works for any value
** of nEq including 0.  If nEq==0, this routine is nearly a no-op.
** The only thing it does is allocate the pLevel->iMem memory cell and
** compute the affinity string.
**
** This routine always allocates at least one memory cell and returns
** the index of that memory cell. The code that
** calls this routine will use that memory cell to store the termination
** key value of the loop.  If one or more IN operators appear, then
** this routine allocates an additional nEq memory cells for internal
** use.
**
** Before returning, *pzAff is set to point to a buffer containing a
** copy of the column affinity string of the index allocated using
** sqlite3DbMalloc(). Except, entries in the copy of the string associated
** with equality constraints that use NONE affinity are set to
** SQLITE_AFF_NONE. This is to deal with SQL such as the following:
**
**   CREATE TABLE t1(a TEXT PRIMARY KEY, b);
**   SELECT ... FROM t1 AS t2, t1 WHERE t1.a = t2.b;
**
** In the example above, the index on t1(a) has TEXT affinity. But since
** the right hand side of the equality constraint (t2.b) has NONE affinity,
** no conversion should be attempted before using a t2.b value as part of
** a key to search the index. Hence the first byte in the returned affinity
** string in this example would be set to SQLITE_AFF_NONE.
*/
static int codeAllEqualityTerms(
  Parse *pParse,        /* Parsing context */
  WhereLevel *pLevel,   /* Which nested loop of the FROM we are coding */
  WhereClause *pWC,     /* The WHERE clause */
  Bitmask notReady,     /* Which parts of FROM have not yet been coded */
  int nExtraReg,        /* Number of extra registers to allocate */
  char **pzAff          /* OUT: Set to point to affinity string */
){
  int nEq = pLevel->plan.nEq;   /* The number of == or IN constraints to code */
  Vdbe *v = pParse->pVdbe;      /* The vm under construction */
  Index *pIdx;                  /* The index being used for this loop */
  int iCur = pLevel->iTabCur;   /* The cursor of the table */
  WhereTerm *pTerm;             /* A single constraint term */
  int j;                        /* Loop counter */
  int regBase;                  /* Base register */
  int nReg;                     /* Number of registers to allocate */
  char *zAff;                   /* Affinity string to return */

  /* This module is only called on query plans that use an index. */
  assert( pLevel->plan.wsFlags & WHERE_INDEXED );
  pIdx = pLevel->plan.u.pIdx;

  /* Figure out how many memory cells we will need then allocate them.
  */
  regBase = pParse->nMem + 1;
  nReg = pLevel->plan.nEq + nExtraReg;
  pParse->nMem += nReg;

  zAff = sqlite3DbStrDup(pParse->db, sqlite3IndexAffinityStr(v, pIdx));
  if( !zAff ){
    pParse->db->mallocFailed = 1;
  }

  /* Evaluate the equality constraints
  */
  assert( pIdx->nColumn>=nEq );
  for(j=0; j<nEq; j++){
    int r1;
    int k = pIdx->aiColumn[j];
    pTerm = findTerm(pWC, iCur, k, notReady, pLevel->plan.wsFlags, pIdx);
    if( NEVER(pTerm==0) ) break;
    /* The following true for indices with redundant columns. 
    ** Ex: CREATE INDEX i1 ON t1(a,b,a); SELECT * FROM t1 WHERE a=0 AND b=0; */
    testcase( (pTerm->wtFlags & TERM_CODED)!=0 );
    testcase( pTerm->wtFlags & TERM_VIRTUAL ); /* EV: R-30575-11662 */
    r1 = codeEqualityTerm(pParse, pTerm, pLevel, regBase+j);
    if( r1!=regBase+j ){
      if( nReg==1 ){
        sqlite3ReleaseTempReg(pParse, regBase);
        regBase = r1;
      }else{
        sqlite3VdbeAddOp2(v, OP_SCopy, r1, regBase+j);
      }
    }
    testcase( pTerm->eOperator & WO_ISNULL );
    testcase( pTerm->eOperator & WO_IN );
    if( (pTerm->eOperator & (WO_ISNULL|WO_IN))==0 ){
      Expr *pRight = pTerm->pExpr->pRight;
      sqlite3ExprCodeIsNullJump(v, pRight, regBase+j, pLevel->addrBrk);
      if( zAff ){
        if( sqlite3CompareAffinity(pRight, zAff[j])==SQLITE_AFF_NONE ){
          zAff[j] = SQLITE_AFF_NONE;
        }
        if( sqlite3ExprNeedsNoAffinityChange(pRight, zAff[j]) ){
          zAff[j] = SQLITE_AFF_NONE;
        }
      }
    }
  }
  *pzAff = zAff;
  return regBase;
}

#ifndef SQLITE_OMIT_EXPLAIN
/*
** This routine is a helper for explainIndexRange() below
**
** pStr holds the text of an expression that we are building up one term
** at a time.  This routine adds a new term to the end of the expression.
** Terms are separated by AND so add the "AND" text for second and subsequent
** terms only.
*/
static void explainAppendTerm(
  StrAccum *pStr,             /* The text expression being built */
  int iTerm,                  /* Index of this term.  First is zero */
  const char *zColumn,        /* Name of the column */
  const char *zOp             /* Name of the operator */
){
  if( iTerm ) sqlite3StrAccumAppend(pStr, " AND ", 5);
  sqlite3StrAccumAppend(pStr, zColumn, -1);
  sqlite3StrAccumAppend(pStr, zOp, 1);
  sqlite3StrAccumAppend(pStr, "?", 1);
}

/*
** Argument pLevel describes a strategy for scanning table pTab. This 
** function returns a pointer to a string buffer containing a description
** of the subset of table rows scanned by the strategy in the form of an
** SQL expression. Or, if all rows are scanned, NULL is returned.
**
** For example, if the query:
**
**   SELECT * FROM t1 WHERE a=1 AND b>2;
**
** is run and there is an index on (a, b), then this function returns a
** string similar to:
**
**   "a=? AND b>?"
**
** The returned pointer points to memory obtained from sqlite3DbMalloc().
** It is the responsibility of the caller to free the buffer when it is
** no longer required.
*/
static char *explainIndexRange(sqlite3 *db, WhereLevel *pLevel, Table *pTab){
  WherePlan *pPlan = &pLevel->plan;
  Index *pIndex = pPlan->u.pIdx;
  int nEq = pPlan->nEq;
  int i, j;
  Column *aCol = pTab->aCol;
  int *aiColumn = pIndex->aiColumn;
  StrAccum txt;

  if( nEq==0 && (pPlan->wsFlags & (WHERE_BTM_LIMIT|WHERE_TOP_LIMIT))==0 ){
    return 0;
  }
  sqlite3StrAccumInit(&txt, 0, 0, SQLITE_MAX_LENGTH);
  txt.db = db;
  sqlite3StrAccumAppend(&txt, " (", 2);
  for(i=0; i<nEq; i++){
    explainAppendTerm(&txt, i, aCol[aiColumn[i]].zName, "=");
  }

  j = i;
  if( pPlan->wsFlags&WHERE_BTM_LIMIT ){
    explainAppendTerm(&txt, i++, aCol[aiColumn[j]].zName, ">");
  }
  if( pPlan->wsFlags&WHERE_TOP_LIMIT ){
    explainAppendTerm(&txt, i, aCol[aiColumn[j]].zName, "<");
  }
  sqlite3StrAccumAppend(&txt, ")", 1);
  return sqlite3StrAccumFinish(&txt);
}

/*
** This function is a no-op unless currently processing an EXPLAIN QUERY PLAN
** command. If the query being compiled is an EXPLAIN QUERY PLAN, a single
** record is added to the output to describe the table scan strategy in 
** pLevel.
*/
static void explainOneScan(
  Parse *pParse,                  /* Parse context */
  SrcList *pTabList,              /* Table list this loop refers to */
  WhereLevel *pLevel,             /* Scan to write OP_Explain opcode for */
  int iLevel,                     /* Value for "level" column of output */
  int iFrom,                      /* Value for "from" column of output */
  u16 wctrlFlags                  /* Flags passed to sqlite3WhereBegin() */
){
  if( pParse->explain==2 ){
    u32 flags = pLevel->plan.wsFlags;
    struct SrcList_item *pItem = &pTabList->a[pLevel->iFrom];
    Vdbe *v = pParse->pVdbe;      /* VM being constructed */
    sqlite3 *db = pParse->db;     /* Database handle */
    char *zMsg;                   /* Text to add to EQP output */
    sqlite3_int64 nRow;           /* Expected number of rows visited by scan */
    int iId = pParse->iSelectId;  /* Select id (left-most output column) */
    int isSearch;                 /* True for a SEARCH. False for SCAN. */

    if( (flags&WHERE_MULTI_OR) || (wctrlFlags&WHERE_ONETABLE_ONLY) ) return;

    isSearch = (pLevel->plan.nEq>0)
             || (flags&(WHERE_BTM_LIMIT|WHERE_TOP_LIMIT))!=0
             || (wctrlFlags&(WHERE_ORDERBY_MIN|WHERE_ORDERBY_MAX));

    zMsg = sqlite3MPrintf(db, "%s", isSearch?"SEARCH":"SCAN");
    if( pItem->pSelect ){
      zMsg = sqlite3MAppendf(db, zMsg, "%s SUBQUERY %d", zMsg,pItem->iSelectId);
    }else{
      zMsg = sqlite3MAppendf(db, zMsg, "%s TABLE %s", zMsg, pItem->zName);
    }

    if( pItem->zAlias ){
      zMsg = sqlite3MAppendf(db, zMsg, "%s AS %s", zMsg, pItem->zAlias);
    }
    if( (flags & WHERE_INDEXED)!=0 ){
      char *zWhere = explainIndexRange(db, pLevel, pItem->pTab);
      zMsg = sqlite3MAppendf(db, zMsg, "%s USING %s%sINDEX%s%s%s", zMsg, 
          ((flags & WHERE_TEMP_INDEX)?"AUTOMATIC ":""),
          ((flags & WHERE_IDX_ONLY)?"COVERING ":""),
          ((flags & WHERE_TEMP_INDEX)?"":" "),
          ((flags & WHERE_TEMP_INDEX)?"": pLevel->plan.u.pIdx->zName),
          zWhere
      );
      sqlite3DbFree(db, zWhere);
    }else if( flags & (WHERE_ROWID_EQ|WHERE_ROWID_RANGE) ){
      zMsg = sqlite3MAppendf(db, zMsg, "%s USING INTEGER PRIMARY KEY", zMsg);

      if( flags&WHERE_ROWID_EQ ){
        zMsg = sqlite3MAppendf(db, zMsg, "%s (rowid=?)", zMsg);
      }else if( (flags&WHERE_BOTH_LIMIT)==WHERE_BOTH_LIMIT ){
        zMsg = sqlite3MAppendf(db, zMsg, "%s (rowid>? AND rowid<?)", zMsg);
      }else if( flags&WHERE_BTM_LIMIT ){
        zMsg = sqlite3MAppendf(db, zMsg, "%s (rowid>?)", zMsg);
      }else if( flags&WHERE_TOP_LIMIT ){
        zMsg = sqlite3MAppendf(db, zMsg, "%s (rowid<?)", zMsg);
      }
    }
#ifndef SQLITE_OMIT_VIRTUALTABLE
    else if( (flags & WHERE_VIRTUALTABLE)!=0 ){
      sqlite3_index_info *pVtabIdx = pLevel->plan.u.pVtabIdx;
      zMsg = sqlite3MAppendf(db, zMsg, "%s VIRTUAL TABLE INDEX %d:%s", zMsg,
                  pVtabIdx->idxNum, pVtabIdx->idxStr);
    }
#endif
    if( wctrlFlags&(WHERE_ORDERBY_MIN|WHERE_ORDERBY_MAX) ){
      testcase( wctrlFlags & WHERE_ORDERBY_MIN );
      nRow = 1;
    }else{
      nRow = (sqlite3_int64)pLevel->plan.nRow;
    }
    zMsg = sqlite3MAppendf(db, zMsg, "%s (~%lld rows)", zMsg, nRow);
    sqlite3VdbeAddOp4(v, OP_Explain, iId, iLevel, iFrom, zMsg, P4_DYNAMIC);
  }
}
#else
# define explainOneScan(u,v,w,x,y,z)
#endif /* SQLITE_OMIT_EXPLAIN */


/*
** Generate code for the start of the iLevel-th loop in the WHERE clause
** implementation described by pWInfo.
*/
static Bitmask codeOneLoopStart(
  WhereInfo *pWInfo,   /* Complete information about the WHERE clause */
  int iLevel,          /* Which level of pWInfo->a[] should be coded */
  u16 wctrlFlags,      /* One of the WHERE_* flags defined in sqliteInt.h */
  Bitmask notReady,    /* Which tables are currently available */
  Expr *pWhere         /* Complete WHERE clause */
){
  int j, k;            /* Loop counters */
  int iCur;            /* The VDBE cursor for the table */
  int addrNxt;         /* Where to jump to continue with the next IN case */
  int omitTable;       /* True if we use the index only */
  int bRev;            /* True if we need to scan in reverse order */
  WhereLevel *pLevel;  /* The where level to be coded */
  WhereClause *pWC;    /* Decomposition of the entire WHERE clause */
  WhereTerm *pTerm;               /* A WHERE clause term */
  Parse *pParse;                  /* Parsing context */
  Vdbe *v;                        /* The prepared stmt under constructions */
  struct SrcList_item *pTabItem;  /* FROM clause term being coded */
  int addrBrk;                    /* Jump here to break out of the loop */
  int addrCont;                   /* Jump here to continue with next cycle */
  int iRowidReg = 0;        /* Rowid is stored in this register, if not zero */
  int iReleaseReg = 0;      /* Temp register to free before returning */

  pParse = pWInfo->pParse;
  v = pParse->pVdbe;
  pWC = pWInfo->pWC;
  pLevel = &pWInfo->a[iLevel];
  pTabItem = &pWInfo->pTabList->a[pLevel->iFrom];
  iCur = pTabItem->iCursor;
  bRev = (pLevel->plan.wsFlags & WHERE_REVERSE)!=0;
  omitTable = (pLevel->plan.wsFlags & WHERE_IDX_ONLY)!=0 
           && (wctrlFlags & WHERE_FORCE_TABLE)==0;

  /* Create labels for the "break" and "continue" instructions
  ** for the current loop.  Jump to addrBrk to break out of a loop.
  ** Jump to cont to go immediately to the next iteration of the
  ** loop.
  **
  ** When there is an IN operator, we also have a "addrNxt" label that
  ** means to continue with the next IN value combination.  When
  ** there are no IN operators in the constraints, the "addrNxt" label
  ** is the same as "addrBrk".
  */
  addrBrk = pLevel->addrBrk = pLevel->addrNxt = sqlite3VdbeMakeLabel(v);
  addrCont = pLevel->addrCont = sqlite3VdbeMakeLabel(v);

  /* If this is the right table of a LEFT OUTER JOIN, allocate and
  ** initialize a memory cell that records if this table matches any
  ** row of the left table of the join.
  */
  if( pLevel->iFrom>0 && (pTabItem[0].jointype & JT_LEFT)!=0 ){
    pLevel->iLeftJoin = ++pParse->nMem;
    sqlite3VdbeAddOp2(v, OP_Integer, 0, pLevel->iLeftJoin);
    VdbeComment((v, "init LEFT JOIN no-match flag"));
  }

#ifndef SQLITE_OMIT_VIRTUALTABLE
  if(  (pLevel->plan.wsFlags & WHERE_VIRTUALTABLE)!=0 ){
    /* Case 0:  The table is a virtual-table.  Use the VFilter and VNext
    **          to access the data.
    */
    int iReg;   /* P3 Value for OP_VFilter */
    sqlite3_index_info *pVtabIdx = pLevel->plan.u.pVtabIdx;
    int nConstraint = pVtabIdx->nConstraint;
    struct sqlite3_index_constraint_usage *aUsage =
                                                pVtabIdx->aConstraintUsage;
    const struct sqlite3_index_constraint *aConstraint =
                                                pVtabIdx->aConstraint;

    sqlite3ExprCachePush(pParse);
    iReg = sqlite3GetTempRange(pParse, nConstraint+2);
    for(j=1; j<=nConstraint; j++){
      for(k=0; k<nConstraint; k++){
        if( aUsage[k].argvIndex==j ){
          int iTerm = aConstraint[k].iTermOffset;
          sqlite3ExprCode(pParse, pWC->a[iTerm].pExpr->pRight, iReg+j+1);
          break;
        }
      }
      if( k==nConstraint ) break;
    }
    sqlite3VdbeAddOp2(v, OP_Integer, pVtabIdx->idxNum, iReg);
    sqlite3VdbeAddOp2(v, OP_Integer, j-1, iReg+1);
    sqlite3VdbeAddOp4(v, OP_VFilter, iCur, addrBrk, iReg, pVtabIdx->idxStr,
                      pVtabIdx->needToFreeIdxStr ? P4_MPRINTF : P4_STATIC);
    pVtabIdx->needToFreeIdxStr = 0;
    for(j=0; j<nConstraint; j++){
      if( aUsage[j].omit ){
        int iTerm = aConstraint[j].iTermOffset;
        disableTerm(pLevel, &pWC->a[iTerm]);
      }
    }
    pLevel->op = OP_VNext;
    pLevel->p1 = iCur;
    pLevel->p2 = sqlite3VdbeCurrentAddr(v);
    sqlite3ReleaseTempRange(pParse, iReg, nConstraint+2);
    sqlite3ExprCachePop(pParse, 1);
  }else
#endif /* SQLITE_OMIT_VIRTUALTABLE */

  if( pLevel->plan.wsFlags & WHERE_ROWID_EQ ){
    /* Case 1:  We can directly reference a single row using an
    **          equality comparison against the ROWID field.  Or
    **          we reference multiple rows using a "rowid IN (...)"
    **          construct.
    */
    iReleaseReg = sqlite3GetTempReg(pParse);
    pTerm = findTerm(pWC, iCur, -1, notReady, WO_EQ|WO_IN, 0);
    assert( pTerm!=0 );
    assert( pTerm->pExpr!=0 );
    assert( pTerm->leftCursor==iCur );
    assert( omitTable==0 );
    testcase( pTerm->wtFlags & TERM_VIRTUAL ); /* EV: R-30575-11662 */
    iRowidReg = codeEqualityTerm(pParse, pTerm, pLevel, iReleaseReg);
    addrNxt = pLevel->addrNxt;
    sqlite3VdbeAddOp2(v, OP_MustBeInt, iRowidReg, addrNxt);
    sqlite3VdbeAddOp3(v, OP_NotExists, iCur, addrNxt, iRowidReg);
    sqlite3ExprCacheStore(pParse, iCur, -1, iRowidReg);
    VdbeComment((v, "pk"));
    pLevel->op = OP_Noop;
  }else if( pLevel->plan.wsFlags & WHERE_ROWID_RANGE ){
    /* Case 2:  We have an inequality comparison against the ROWID field.
    */
    int testOp = OP_Noop;
    int start;
    int memEndValue = 0;
    WhereTerm *pStart, *pEnd;

    assert( omitTable==0 );
    pStart = findTerm(pWC, iCur, -1, notReady, WO_GT|WO_GE, 0);
    pEnd = findTerm(pWC, iCur, -1, notReady, WO_LT|WO_LE, 0);
    if( bRev ){
      pTerm = pStart;
      pStart = pEnd;
      pEnd = pTerm;
    }
    if( pStart ){
      Expr *pX;             /* The expression that defines the start bound */
      int r1, rTemp;        /* Registers for holding the start boundary */

      /* The following constant maps TK_xx codes into corresponding 
      ** seek opcodes.  It depends on a particular ordering of TK_xx
      */
      const u8 aMoveOp[] = {
           /* TK_GT */  OP_SeekGt,
           /* TK_LE */  OP_SeekLe,
           /* TK_LT */  OP_SeekLt,
           /* TK_GE */  OP_SeekGe
      };
      assert( TK_LE==TK_GT+1 );      /* Make sure the ordering.. */
      assert( TK_LT==TK_GT+2 );      /*  ... of the TK_xx values... */
      assert( TK_GE==TK_GT+3 );      /*  ... is correcct. */

      testcase( pStart->wtFlags & TERM_VIRTUAL ); /* EV: R-30575-11662 */
      pX = pStart->pExpr;
      assert( pX!=0 );
      assert( pStart->leftCursor==iCur );
      r1 = sqlite3ExprCodeTemp(pParse, pX->pRight, &rTemp);
      sqlite3VdbeAddOp3(v, aMoveOp[pX->op-TK_GT], iCur, addrBrk, r1);
      VdbeComment((v, "pk"));
      sqlite3ExprCacheAffinityChange(pParse, r1, 1);
      sqlite3ReleaseTempReg(pParse, rTemp);
      disableTerm(pLevel, pStart);
    }else{
      sqlite3VdbeAddOp2(v, bRev ? OP_Last : OP_Rewind, iCur, addrBrk);
    }
    if( pEnd ){
      Expr *pX;
      pX = pEnd->pExpr;
      assert( pX!=0 );
      assert( pEnd->leftCursor==iCur );
      testcase( pEnd->wtFlags & TERM_VIRTUAL ); /* EV: R-30575-11662 */
      memEndValue = ++pParse->nMem;
      sqlite3ExprCode(pParse, pX->pRight, memEndValue);
      if( pX->op==TK_LT || pX->op==TK_GT ){
        testOp = bRev ? OP_Le : OP_Ge;
      }else{
        testOp = bRev ? OP_Lt : OP_Gt;
      }
      disableTerm(pLevel, pEnd);
    }
    start = sqlite3VdbeCurrentAddr(v);
    pLevel->op = bRev ? OP_Prev : OP_Next;
    pLevel->p1 = iCur;
    pLevel->p2 = start;
    if( pStart==0 && pEnd==0 ){
      pLevel->p5 = SQLITE_STMTSTATUS_FULLSCAN_STEP;
    }else{
      assert( pLevel->p5==0 );
    }
    if( testOp!=OP_Noop ){
      iRowidReg = iReleaseReg = sqlite3GetTempReg(pParse);
      sqlite3VdbeAddOp2(v, OP_Rowid, iCur, iRowidReg);
      sqlite3ExprCacheStore(pParse, iCur, -1, iRowidReg);
      sqlite3VdbeAddOp3(v, testOp, memEndValue, addrBrk, iRowidReg);
      sqlite3VdbeChangeP5(v, SQLITE_AFF_NUMERIC | SQLITE_JUMPIFNULL);
    }
  }else if( pLevel->plan.wsFlags & (WHERE_COLUMN_RANGE|WHERE_COLUMN_EQ) ){
    /* Case 3: A scan using an index.
    **
    **         The WHERE clause may contain zero or more equality 
    **         terms ("==" or "IN" operators) that refer to the N
    **         left-most columns of the index. It may also contain
    **         inequality constraints (>, <, >= or <=) on the indexed
    **         column that immediately follows the N equalities. Only 
    **         the right-most column can be an inequality - the rest must
    **         use the "==" and "IN" operators. For example, if the 
    **         index is on (x,y,z), then the following clauses are all 
    **         optimized:
    **
    **            x=5
    **            x=5 AND y=10
    **            x=5 AND y<10
    **            x=5 AND y>5 AND y<10
    **            x=5 AND y=5 AND z<=10
    **
    **         The z<10 term of the following cannot be used, only
    **         the x=5 term:
    **
    **            x=5 AND z<10
    **
    **         N may be zero if there are inequality constraints.
    **         If there are no inequality constraints, then N is at
    **         least one.
    **
    **         This case is also used when there are no WHERE clause
    **         constraints but an index is selected anyway, in order
    **         to force the output order to conform to an ORDER BY.
    */  
    static const u8 aStartOp[] = {
      0,
      0,
      OP_Rewind,           /* 2: (!start_constraints && startEq &&  !bRev) */
      OP_Last,             /* 3: (!start_constraints && startEq &&   bRev) */
      OP_SeekGt,           /* 4: (start_constraints  && !startEq && !bRev) */
      OP_SeekLt,           /* 5: (start_constraints  && !startEq &&  bRev) */
      OP_SeekGe,           /* 6: (start_constraints  &&  startEq && !bRev) */
      OP_SeekLe            /* 7: (start_constraints  &&  startEq &&  bRev) */
    };
    static const u8 aEndOp[] = {
      OP_Noop,             /* 0: (!end_constraints) */
      OP_IdxGE,            /* 1: (end_constraints && !bRev) */
      OP_IdxLT             /* 2: (end_constraints && bRev) */
    };
    int nEq = pLevel->plan.nEq;  /* Number of == or IN terms */
    int isMinQuery = 0;          /* If this is an optimized SELECT min(x).. */
    int regBase;                 /* Base register holding constraint values */
    int r1;                      /* Temp register */
    WhereTerm *pRangeStart = 0;  /* Inequality constraint at range start */
    WhereTerm *pRangeEnd = 0;    /* Inequality constraint at range end */
    int startEq;                 /* True if range start uses ==, >= or <= */
    int endEq;                   /* True if range end uses ==, >= or <= */
    int start_constraints;       /* Start of range is constrained */
    int nConstraint;             /* Number of constraint terms */
    Index *pIdx;                 /* The index we will be using */
    int iIdxCur;                 /* The VDBE cursor for the index */
    int nExtraReg = 0;           /* Number of extra registers needed */
    int op;                      /* Instruction opcode */
    char *zStartAff;             /* Affinity for start of range constraint */
    char *zEndAff;               /* Affinity for end of range constraint */

    pIdx = pLevel->plan.u.pIdx;
    iIdxCur = pLevel->iIdxCur;
    k = pIdx->aiColumn[nEq];     /* Column for inequality constraints */

    /* If this loop satisfies a sort order (pOrderBy) request that 
    ** was passed to this function to implement a "SELECT min(x) ..." 
    ** query, then the caller will only allow the loop to run for
    ** a single iteration. This means that the first row returned
    ** should not have a NULL value stored in 'x'. If column 'x' is
    ** the first one after the nEq equality constraints in the index,
    ** this requires some special handling.
    */
    if( (wctrlFlags&WHERE_ORDERBY_MIN)!=0
     && (pLevel->plan.wsFlags&WHERE_ORDERBY)
     && (pIdx->nColumn>nEq)
    ){
      /* assert( pOrderBy->nExpr==1 ); */
      /* assert( pOrderBy->a[0].pExpr->iColumn==pIdx->aiColumn[nEq] ); */
      isMinQuery = 1;
      nExtraReg = 1;
    }

    /* Find any inequality constraint terms for the start and end 
    ** of the range. 
    */
    if( pLevel->plan.wsFlags & WHERE_TOP_LIMIT ){
      pRangeEnd = findTerm(pWC, iCur, k, notReady, (WO_LT|WO_LE), pIdx);
      nExtraReg = 1;
    }
    if( pLevel->plan.wsFlags & WHERE_BTM_LIMIT ){
      pRangeStart = findTerm(pWC, iCur, k, notReady, (WO_GT|WO_GE), pIdx);
      nExtraReg = 1;
    }

    /* Generate code to evaluate all constraint terms using == or IN
    ** and store the values of those terms in an array of registers
    ** starting at regBase.
    */
    regBase = codeAllEqualityTerms(
        pParse, pLevel, pWC, notReady, nExtraReg, &zStartAff
    );
    zEndAff = sqlite3DbStrDup(pParse->db, zStartAff);
    addrNxt = pLevel->addrNxt;

    /* If we are doing a reverse order scan on an ascending index, or
    ** a forward order scan on a descending index, interchange the 
    ** start and end terms (pRangeStart and pRangeEnd).
    */
    if( nEq<pIdx->nColumn && bRev==(pIdx->aSortOrder[nEq]==SQLITE_SO_ASC) ){
      SWAP(WhereTerm *, pRangeEnd, pRangeStart);
    }

    testcase( pRangeStart && pRangeStart->eOperator & WO_LE );
    testcase( pRangeStart && pRangeStart->eOperator & WO_GE );
    testcase( pRangeEnd && pRangeEnd->eOperator & WO_LE );
    testcase( pRangeEnd && pRangeEnd->eOperator & WO_GE );
    startEq = !pRangeStart || pRangeStart->eOperator & (WO_LE|WO_GE);
    endEq =   !pRangeEnd || pRangeEnd->eOperator & (WO_LE|WO_GE);
    start_constraints = pRangeStart || nEq>0;

    /* Seek the index cursor to the start of the range. */
    nConstraint = nEq;
    if( pRangeStart ){
      Expr *pRight = pRangeStart->pExpr->pRight;
      sqlite3ExprCode(pParse, pRight, regBase+nEq);
      if( (pRangeStart->wtFlags & TERM_VNULL)==0 ){
        sqlite3ExprCodeIsNullJump(v, pRight, regBase+nEq, addrNxt);
      }
      if( zStartAff ){
        if( sqlite3CompareAffinity(pRight, zStartAff[nEq])==SQLITE_AFF_NONE){
          /* Since the comparison is to be performed with no conversions
          ** applied to the operands, set the affinity to apply to pRight to 
          ** SQLITE_AFF_NONE.  */
          zStartAff[nEq] = SQLITE_AFF_NONE;
        }
        if( sqlite3ExprNeedsNoAffinityChange(pRight, zStartAff[nEq]) ){
          zStartAff[nEq] = SQLITE_AFF_NONE;
        }
      }  
      nConstraint++;
      testcase( pRangeStart->wtFlags & TERM_VIRTUAL ); /* EV: R-30575-11662 */
    }else if( isMinQuery ){
      sqlite3VdbeAddOp2(v, OP_Null, 0, regBase+nEq);
      nConstraint++;
      startEq = 0;
      start_constraints = 1;
    }
    codeApplyAffinity(pParse, regBase, nConstraint, zStartAff);
    op = aStartOp[(start_constraints<<2) + (startEq<<1) + bRev];
    assert( op!=0 );
    testcase( op==OP_Rewind );
    testcase( op==OP_Last );
    testcase( op==OP_SeekGt );
    testcase( op==OP_SeekGe );
    testcase( op==OP_SeekLe );
    testcase( op==OP_SeekLt );
    sqlite3VdbeAddOp4Int(v, op, iIdxCur, addrNxt, regBase, nConstraint);

    /* Load the value for the inequality constraint at the end of the
    ** range (if any).
    */
    nConstraint = nEq;
    if( pRangeEnd ){
      Expr *pRight = pRangeEnd->pExpr->pRight;
      sqlite3ExprCacheRemove(pParse, regBase+nEq, 1);
      sqlite3ExprCode(pParse, pRight, regBase+nEq);
      if( (pRangeEnd->wtFlags & TERM_VNULL)==0 ){
        sqlite3ExprCodeIsNullJump(v, pRight, regBase+nEq, addrNxt);
      }
      if( zEndAff ){
        if( sqlite3CompareAffinity(pRight, zEndAff[nEq])==SQLITE_AFF_NONE){
          /* Since the comparison is to be performed with no conversions
          ** applied to the operands, set the affinity to apply to pRight to 
          ** SQLITE_AFF_NONE.  */
          zEndAff[nEq] = SQLITE_AFF_NONE;
        }
        if( sqlite3ExprNeedsNoAffinityChange(pRight, zEndAff[nEq]) ){
          zEndAff[nEq] = SQLITE_AFF_NONE;
        }
      }  
      codeApplyAffinity(pParse, regBase, nEq+1, zEndAff);
      nConstraint++;
      testcase( pRangeEnd->wtFlags & TERM_VIRTUAL ); /* EV: R-30575-11662 */
    }
    sqlite3DbFree(pParse->db, zStartAff);
    sqlite3DbFree(pParse->db, zEndAff);

    /* Top of the loop body */
    pLevel->p2 = sqlite3VdbeCurrentAddr(v);

    /* Check if the index cursor is past the end of the range. */
    op = aEndOp[(pRangeEnd || nEq) * (1 + bRev)];
    testcase( op==OP_Noop );
    testcase( op==OP_IdxGE );
    testcase( op==OP_IdxLT );
    if( op!=OP_Noop ){
      sqlite3VdbeAddOp4Int(v, op, iIdxCur, addrNxt, regBase, nConstraint);
      sqlite3VdbeChangeP5(v, endEq!=bRev ?1:0);
    }

    /* If there are inequality constraints, check that the value
    ** of the table column that the inequality contrains is not NULL.
    ** If it is, jump to the next iteration of the loop.
    */
    r1 = sqlite3GetTempReg(pParse);
    testcase( pLevel->plan.wsFlags & WHERE_BTM_LIMIT );
    testcase( pLevel->plan.wsFlags & WHERE_TOP_LIMIT );
    if( (pLevel->plan.wsFlags & (WHERE_BTM_LIMIT|WHERE_TOP_LIMIT))!=0 ){
      sqlite3VdbeAddOp3(v, OP_Column, iIdxCur, nEq, r1);
      sqlite3VdbeAddOp2(v, OP_IsNull, r1, addrCont);
    }
    sqlite3ReleaseTempReg(pParse, r1);

    /* Seek the table cursor, if required */
    disableTerm(pLevel, pRangeStart);
    disableTerm(pLevel, pRangeEnd);
    if( !omitTable ){
      iRowidReg = iReleaseReg = sqlite3GetTempReg(pParse);
      sqlite3VdbeAddOp2(v, OP_IdxRowid, iIdxCur, iRowidReg);
      sqlite3ExprCacheStore(pParse, iCur, -1, iRowidReg);
      sqlite3VdbeAddOp2(v, OP_Seek, iCur, iRowidReg);  /* Deferred seek */
    }

    /* Record the instruction used to terminate the loop. Disable 
    ** WHERE clause terms made redundant by the index range scan.
    */
    if( pLevel->plan.wsFlags & WHERE_UNIQUE ){
      pLevel->op = OP_Noop;
    }else if( bRev ){
      pLevel->op = OP_Prev;
    }else{
      pLevel->op = OP_Next;
    }
    pLevel->p1 = iIdxCur;
  }else

#ifndef SQLITE_OMIT_OR_OPTIMIZATION
  if( pLevel->plan.wsFlags & WHERE_MULTI_OR ){
    /* Case 4:  Two or more separately indexed terms connected by OR
    **
    ** Example:
    **
    **   CREATE TABLE t1(a,b,c,d);
    **   CREATE INDEX i1 ON t1(a);
    **   CREATE INDEX i2 ON t1(b);
    **   CREATE INDEX i3 ON t1(c);
    **
    **   SELECT * FROM t1 WHERE a=5 OR b=7 OR (c=11 AND d=13)
    **
    ** In the example, there are three indexed terms connected by OR.
    ** The top of the loop looks like this:
    **
    **          Null       1                # Zero the rowset in reg 1
    **
    ** Then, for each indexed term, the following. The arguments to
    ** RowSetTest are such that the rowid of the current row is inserted
    ** into the RowSet. If it is already present, control skips the
    ** Gosub opcode and jumps straight to the code generated by WhereEnd().
    **
    **        sqlite3WhereBegin(<term>)
    **          RowSetTest                  # Insert rowid into rowset
    **          Gosub      2 A
    **        sqlite3WhereEnd()
    **
    ** Following the above, code to terminate the loop. Label A, the target
    ** of the Gosub above, jumps to the instruction right after the Goto.
    **
    **          Null       1                # Zero the rowset in reg 1
    **          Goto       B                # The loop is finished.
    **
    **       A: <loop body>                 # Return data, whatever.
    **
    **          Return     2                # Jump back to the Gosub
    **
    **       B: <after the loop>
    **
    */
    WhereClause *pOrWc;    /* The OR-clause broken out into subterms */
    SrcList *pOrTab;       /* Shortened table list or OR-clause generation */

    int regReturn = ++pParse->nMem;           /* Register used with OP_Gosub */
    int regRowset = 0;                        /* Register for RowSet object */
    int regRowid = 0;                         /* Register holding rowid */
    int iLoopBody = sqlite3VdbeMakeLabel(v);  /* Start of loop body */
    int iRetInit;                             /* Address of regReturn init */
    int untestedTerms = 0;             /* Some terms not completely tested */
    int ii;                            /* Loop counter */
    Expr *pAndExpr = 0;                /* An ".. AND (...)" expression */
   
    pTerm = pLevel->plan.u.pTerm;
    assert( pTerm!=0 );
    assert( pTerm->eOperator==WO_OR );
    assert( (pTerm->wtFlags & TERM_ORINFO)!=0 );
    pOrWc = &pTerm->u.pOrInfo->wc;
    pLevel->op = OP_Return;
    pLevel->p1 = regReturn;

    /* Set up a new SrcList ni pOrTab containing the table being scanned
    ** by this loop in the a[0] slot and all notReady tables in a[1..] slots.
    ** This becomes the SrcList in the recursive call to sqlite3WhereBegin().
    */
    if( pWInfo->nLevel>1 ){
      int nNotReady;                 /* The number of notReady tables */
      struct SrcList_item *origSrc;     /* Original list of tables */
      nNotReady = pWInfo->nLevel - iLevel - 1;
      pOrTab = sqlite3StackAllocRaw(pParse->db,
                            sizeof(*pOrTab)+ nNotReady*sizeof(pOrTab->a[0]));
      if( pOrTab==0 ) return notReady;
      pOrTab->nAlloc = (i16)(nNotReady + 1);
      pOrTab->nSrc = pOrTab->nAlloc;
      memcpy(pOrTab->a, pTabItem, sizeof(*pTabItem));
      origSrc = pWInfo->pTabList->a;
      for(k=1; k<=nNotReady; k++){
        memcpy(&pOrTab->a[k], &origSrc[pLevel[k].iFrom], sizeof(pOrTab->a[k]));
      }
    }else{
      pOrTab = pWInfo->pTabList;
    }

    /* Initialize the rowset register to contain NULL. An SQL NULL is 
    ** equivalent to an empty rowset.
    **
    ** Also initialize regReturn to contain the address of the instruction 
    ** immediately following the OP_Return at the bottom of the loop. This
    ** is required in a few obscure LEFT JOIN cases where control jumps
    ** over the top of the loop into the body of it. In this case the 
    ** correct response for the end-of-loop code (the OP_Return) is to 
    ** fall through to the next instruction, just as an OP_Next does if
    ** called on an uninitialized cursor.
    */
    if( (wctrlFlags & WHERE_DUPLICATES_OK)==0 ){
      regRowset = ++pParse->nMem;
      regRowid = ++pParse->nMem;
      sqlite3VdbeAddOp2(v, OP_Null, 0, regRowset);
    }
    iRetInit = sqlite3VdbeAddOp2(v, OP_Integer, 0, regReturn);

    /* If the original WHERE clause is z of the form:  (x1 OR x2 OR ...) AND y
    ** Then for every term xN, evaluate as the subexpression: xN AND z
    ** That way, terms in y that are factored into the disjunction will
    ** be picked up by the recursive calls to sqlite3WhereBegin() below.
    */
    if( pWC->nTerm>1 ){
      pAndExpr = sqlite3ExprAlloc(pParse->db, TK_AND, 0, 0);
      pAndExpr->pRight = pWhere;
    }

    for(ii=0; ii<pOrWc->nTerm; ii++){
      WhereTerm *pOrTerm = &pOrWc->a[ii];
      if( pOrTerm->leftCursor==iCur || pOrTerm->eOperator==WO_AND ){
        WhereInfo *pSubWInfo;          /* Info for single OR-term scan */
        Expr *pOrExpr = pOrTerm->pExpr;
        if( pAndExpr ){
          pAndExpr->pLeft = pOrExpr;
          pOrExpr = pAndExpr;
        }
        /* Loop through table entries that match term pOrTerm. */
        pSubWInfo = sqlite3WhereBegin(pParse, pOrTab, pOrExpr, 0, 0,
                        WHERE_OMIT_OPEN_CLOSE | WHERE_AND_ONLY |
                        WHERE_FORCE_TABLE | WHERE_ONETABLE_ONLY);
        if( pSubWInfo ){
          explainOneScan(
              pParse, pOrTab, &pSubWInfo->a[0], iLevel, pLevel->iFrom, 0
          );
          if( (wctrlFlags & WHERE_DUPLICATES_OK)==0 ){
            int iSet = ((ii==pOrWc->nTerm-1)?-1:ii);
            int r;
            r = sqlite3ExprCodeGetColumn(pParse, pTabItem->pTab, -1, iCur, 
                                         regRowid);
            sqlite3VdbeAddOp4Int(v, OP_RowSetTest, regRowset,
                                 sqlite3VdbeCurrentAddr(v)+2, r, iSet);
          }
          sqlite3VdbeAddOp2(v, OP_Gosub, regReturn, iLoopBody);

          /* The pSubWInfo->untestedTerms flag means that this OR term
          ** contained one or more AND term from a notReady table.  The
          ** terms from the notReady table could not be tested and will
          ** need to be tested later.
          */
          if( pSubWInfo->untestedTerms ) untestedTerms = 1;

          /* Finish the loop through table entries that match term pOrTerm. */
          sqlite3WhereEnd(pSubWInfo);
        }
      }
    }
    sqlite3DbFree(pParse->db, pAndExpr);
    sqlite3VdbeChangeP1(v, iRetInit, sqlite3VdbeCurrentAddr(v));
    sqlite3VdbeAddOp2(v, OP_Goto, 0, pLevel->addrBrk);
    sqlite3VdbeResolveLabel(v, iLoopBody);

    if( pWInfo->nLevel>1 ) sqlite3StackFree(pParse->db, pOrTab);
    if( !untestedTerms ) disableTerm(pLevel, pTerm);
  }else
#endif /* SQLITE_OMIT_OR_OPTIMIZATION */

  {
    /* Case 5:  There is no usable index.  We must do a complete
    **          scan of the entire table.
    */
    static const u8 aStep[] = { OP_Next, OP_Prev };
    static const u8 aStart[] = { OP_Rewind, OP_Last };
    assert( bRev==0 || bRev==1 );
    assert( omitTable==0 );
    pLevel->op = aStep[bRev];
    pLevel->p1 = iCur;
    pLevel->p2 = 1 + sqlite3VdbeAddOp2(v, aStart[bRev], iCur, addrBrk);
    pLevel->p5 = SQLITE_STMTSTATUS_FULLSCAN_STEP;
  }
  notReady &= ~getMask(pWC->pMaskSet, iCur);

  /* Insert code to test every subexpression that can be completely
  ** computed using the current set of tables.
  **
  ** IMPLEMENTATION-OF: R-49525-50935 Terms that cannot be satisfied through
  ** the use of indices become tests that are evaluated against each row of
  ** the relevant input tables.
  */
  for(pTerm=pWC->a, j=pWC->nTerm; j>0; j--, pTerm++){
    Expr *pE;
    testcase( pTerm->wtFlags & TERM_VIRTUAL ); /* IMP: R-30575-11662 */
    testcase( pTerm->wtFlags & TERM_CODED );
    if( pTerm->wtFlags & (TERM_VIRTUAL|TERM_CODED) ) continue;
    if( (pTerm->prereqAll & notReady)!=0 ){
      testcase( pWInfo->untestedTerms==0
               && (pWInfo->wctrlFlags & WHERE_ONETABLE_ONLY)!=0 );
      pWInfo->untestedTerms = 1;
      continue;
    }
    pE = pTerm->pExpr;
    assert( pE!=0 );
    if( pLevel->iLeftJoin && !ExprHasProperty(pE, EP_FromJoin) ){
      continue;
    }
    sqlite3ExprIfFalse(pParse, pE, addrCont, SQLITE_JUMPIFNULL);
    pTerm->wtFlags |= TERM_CODED;
  }

  /* For a LEFT OUTER JOIN, generate code that will record the fact that
  ** at least one row of the right table has matched the left table.  
  */
  if( pLevel->iLeftJoin ){
    pLevel->addrFirst = sqlite3VdbeCurrentAddr(v);
    sqlite3VdbeAddOp2(v, OP_Integer, 1, pLevel->iLeftJoin);
    VdbeComment((v, "record LEFT JOIN hit"));
    sqlite3ExprCacheClear(pParse);
    for(pTerm=pWC->a, j=0; j<pWC->nTerm; j++, pTerm++){
      testcase( pTerm->wtFlags & TERM_VIRTUAL );  /* IMP: R-30575-11662 */
      testcase( pTerm->wtFlags & TERM_CODED );
      if( pTerm->wtFlags & (TERM_VIRTUAL|TERM_CODED) ) continue;
      if( (pTerm->prereqAll & notReady)!=0 ){
        assert( pWInfo->untestedTerms );
        continue;
      }
      assert( pTerm->pExpr );
      sqlite3ExprIfFalse(pParse, pTerm->pExpr, addrCont, SQLITE_JUMPIFNULL);
      pTerm->wtFlags |= TERM_CODED;
    }
  }
  sqlite3ReleaseTempReg(pParse, iReleaseReg);

  return notReady;
}

#if defined(SQLITE_TEST)
/*
** The following variable holds a text description of query plan generated
** by the most recent call to sqlite3WhereBegin().  Each call to WhereBegin
** overwrites the previous.  This information is used for testing and
** analysis only.
*/
SQLITE_API char sqlite3_query_plan[BMS*2*40];  /* Text of the join */
static int nQPlan = 0;              /* Next free slow in _query_plan[] */

#endif /* SQLITE_TEST */


/*
** Free a WhereInfo structure
*/
static void whereInfoFree(sqlite3 *db, WhereInfo *pWInfo){
  if( ALWAYS(pWInfo) ){
    int i;
    for(i=0; i<pWInfo->nLevel; i++){
      sqlite3_index_info *pInfo = pWInfo->a[i].pIdxInfo;
      if( pInfo ){
        /* assert( pInfo->needToFreeIdxStr==0 || db->mallocFailed ); */
        if( pInfo->needToFreeIdxStr ){
          sqlite3_free(pInfo->idxStr);
        }
        sqlite3DbFree(db, pInfo);
      }
      if( pWInfo->a[i].plan.wsFlags & WHERE_TEMP_INDEX ){
        Index *pIdx = pWInfo->a[i].plan.u.pIdx;
        if( pIdx ){
          sqlite3DbFree(db, pIdx->zColAff);
          sqlite3DbFree(db, pIdx);
        }
      }
    }
    whereClauseClear(pWInfo->pWC);
    sqlite3DbFree(db, pWInfo);
  }
}


/*
** Generate the beginning of the loop used for WHERE clause processing.
** The return value is a pointer to an opaque structure that contains
** information needed to terminate the loop.  Later, the calling routine
** should invoke sqlite3WhereEnd() with the return value of this function
** in order to complete the WHERE clause processing.
**
** If an error occurs, this routine returns NULL.
**
** The basic idea is to do a nested loop, one loop for each table in
** the FROM clause of a select.  (INSERT and UPDATE statements are the
** same as a SELECT with only a single table in the FROM clause.)  For
** example, if the SQL is this:
**
**       SELECT * FROM t1, t2, t3 WHERE ...;
**
** Then the code generated is conceptually like the following:
**
**      foreach row1 in t1 do       \    Code generated
**        foreach row2 in t2 do      |-- by sqlite3WhereBegin()
**          foreach row3 in t3 do   /
**            ...
**          end                     \    Code generated
**        end                        |-- by sqlite3WhereEnd()
**      end                         /
**
** Note that the loops might not be nested in the order in which they
** appear in the FROM clause if a different order is better able to make
** use of indices.  Note also that when the IN operator appears in
** the WHERE clause, it might result in additional nested loops for
** scanning through all values on the right-hand side of the IN.
**
** There are Btree cursors associated with each table.  t1 uses cursor
** number pTabList->a[0].iCursor.  t2 uses the cursor pTabList->a[1].iCursor.
** And so forth.  This routine generates code to open those VDBE cursors
** and sqlite3WhereEnd() generates the code to close them.
**
** The code that sqlite3WhereBegin() generates leaves the cursors named
** in pTabList pointing at their appropriate entries.  The [...] code
** can use OP_Column and OP_Rowid opcodes on these cursors to extract
** data from the various tables of the loop.
**
** If the WHERE clause is empty, the foreach loops must each scan their
** entire tables.  Thus a three-way join is an O(N^3) operation.  But if
** the tables have indices and there are terms in the WHERE clause that
** refer to those indices, a complete table scan can be avoided and the
** code will run much faster.  Most of the work of this routine is checking
** to see if there are indices that can be used to speed up the loop.
**
** Terms of the WHERE clause are also used to limit which rows actually
** make it to the "..." in the middle of the loop.  After each "foreach",
** terms of the WHERE clause that use only terms in that loop and outer
** loops are evaluated and if false a jump is made around all subsequent
** inner loops (or around the "..." if the test occurs within the inner-
** most loop)
**
** OUTER JOINS
**
** An outer join of tables t1 and t2 is conceptally coded as follows:
**
**    foreach row1 in t1 do
**      flag = 0
**      foreach row2 in t2 do
**        start:
**          ...
**          flag = 1
**      end
**      if flag==0 then
**        move the row2 cursor to a null row
**        goto start
**      fi
**    end
**
** ORDER BY CLAUSE PROCESSING
**
** *ppOrderBy is a pointer to the ORDER BY clause of a SELECT statement,
** if there is one.  If there is no ORDER BY clause or if this routine
** is called from an UPDATE or DELETE statement, then ppOrderBy is NULL.
**
** If an index can be used so that the natural output order of the table
** scan is correct for the ORDER BY clause, then that index is used and
** *ppOrderBy is set to NULL.  This is an optimization that prevents an
** unnecessary sort of the result set if an index appropriate for the
** ORDER BY clause already exists.
**
** If the where clause loops cannot be arranged to provide the correct
** output order, then the *ppOrderBy is unchanged.
*/
SQLITE_PRIVATE WhereInfo *sqlite3WhereBegin(
  Parse *pParse,        /* The parser context */
  SrcList *pTabList,    /* A list of all tables to be scanned */
  Expr *pWhere,         /* The WHERE clause */
  ExprList **ppOrderBy, /* An ORDER BY clause, or NULL */
  ExprList *pDistinct,  /* The select-list for DISTINCT queries - or NULL */
  u16 wctrlFlags        /* One of the WHERE_* flags defined in sqliteInt.h */
){
  int i;                     /* Loop counter */
  int nByteWInfo;            /* Num. bytes allocated for WhereInfo struct */
  int nTabList;              /* Number of elements in pTabList */
  WhereInfo *pWInfo;         /* Will become the return value of this function */
  Vdbe *v = pParse->pVdbe;   /* The virtual database engine */
  Bitmask notReady;          /* Cursors that are not yet positioned */
  WhereMaskSet *pMaskSet;    /* The expression mask set */
  WhereClause *pWC;               /* Decomposition of the WHERE clause */
  struct SrcList_item *pTabItem;  /* A single entry from pTabList */
  WhereLevel *pLevel;             /* A single level in the pWInfo list */
  int iFrom;                      /* First unused FROM clause element */
  int andFlags;              /* AND-ed combination of all pWC->a[].wtFlags */
  sqlite3 *db;               /* Database connection */

  /* The number of tables in the FROM clause is limited by the number of
  ** bits in a Bitmask 
  */
  testcase( pTabList->nSrc==BMS );
  if( pTabList->nSrc>BMS ){
    sqlite3ErrorMsg(pParse, "at most %d tables in a join", BMS);
    return 0;
  }

  /* This function normally generates a nested loop for all tables in 
  ** pTabList.  But if the WHERE_ONETABLE_ONLY flag is set, then we should
  ** only generate code for the first table in pTabList and assume that
  ** any cursors associated with subsequent tables are uninitialized.
  */
  nTabList = (wctrlFlags & WHERE_ONETABLE_ONLY) ? 1 : pTabList->nSrc;

  /* Allocate and initialize the WhereInfo structure that will become the
  ** return value. A single allocation is used to store the WhereInfo
  ** struct, the contents of WhereInfo.a[], the WhereClause structure
  ** and the WhereMaskSet structure. Since WhereClause contains an 8-byte
  ** field (type Bitmask) it must be aligned on an 8-byte boundary on
  ** some architectures. Hence the ROUND8() below.
  */
  db = pParse->db;
  nByteWInfo = ROUND8(sizeof(WhereInfo)+(nTabList-1)*sizeof(WhereLevel));
  pWInfo = sqlite3DbMallocZero(db, 
      nByteWInfo + 
      sizeof(WhereClause) +
      sizeof(WhereMaskSet)
  );
  if( db->mallocFailed ){
    sqlite3DbFree(db, pWInfo);
    pWInfo = 0;
    goto whereBeginError;
  }
  pWInfo->nLevel = nTabList;
  pWInfo->pParse = pParse;
  pWInfo->pTabList = pTabList;
  pWInfo->iBreak = sqlite3VdbeMakeLabel(v);
  pWInfo->pWC = pWC = (WhereClause *)&((u8 *)pWInfo)[nByteWInfo];
  pWInfo->wctrlFlags = wctrlFlags;
  pWInfo->savedNQueryLoop = pParse->nQueryLoop;
  pMaskSet = (WhereMaskSet*)&pWC[1];

  /* Disable the DISTINCT optimization if SQLITE_DistinctOpt is set via
  ** sqlite3_test_ctrl(SQLITE_TESTCTRL_OPTIMIZATIONS,...) */
  if( db->flags & SQLITE_DistinctOpt ) pDistinct = 0;

  /* Split the WHERE clause into separate subexpressions where each
  ** subexpression is separated by an AND operator.
  */
  initMaskSet(pMaskSet);
  whereClauseInit(pWC, pParse, pMaskSet, wctrlFlags);
  sqlite3ExprCodeConstants(pParse, pWhere);
  whereSplit(pWC, pWhere, TK_AND);   /* IMP: R-15842-53296 */
    
  /* Special case: a WHERE clause that is constant.  Evaluate the
  ** expression and either jump over all of the code or fall thru.
  */
  if( pWhere && (nTabList==0 || sqlite3ExprIsConstantNotJoin(pWhere)) ){
    sqlite3ExprIfFalse(pParse, pWhere, pWInfo->iBreak, SQLITE_JUMPIFNULL);
    pWhere = 0;
  }

  /* Assign a bit from the bitmask to every term in the FROM clause.
  **
  ** When assigning bitmask values to FROM clause cursors, it must be
  ** the case that if X is the bitmask for the N-th FROM clause term then
  ** the bitmask for all FROM clause terms to the left of the N-th term
  ** is (X-1).   An expression from the ON clause of a LEFT JOIN can use
  ** its Expr.iRightJoinTable value to find the bitmask of the right table
  ** of the join.  Subtracting one from the right table bitmask gives a
  ** bitmask for all tables to the left of the join.  Knowing the bitmask
  ** for all tables to the left of a left join is important.  Ticket #3015.
  **
  ** Configure the WhereClause.vmask variable so that bits that correspond
  ** to virtual table cursors are set. This is used to selectively disable 
  ** the OR-to-IN transformation in exprAnalyzeOrTerm(). It is not helpful 
  ** with virtual tables.
  **
  ** Note that bitmasks are created for all pTabList->nSrc tables in
  ** pTabList, not just the first nTabList tables.  nTabList is normally
  ** equal to pTabList->nSrc but might be shortened to 1 if the
  ** WHERE_ONETABLE_ONLY flag is set.
  */
  assert( pWC->vmask==0 && pMaskSet->n==0 );
  for(i=0; i<pTabList->nSrc; i++){
    createMask(pMaskSet, pTabList->a[i].iCursor);
#ifndef SQLITE_OMIT_VIRTUALTABLE
    if( ALWAYS(pTabList->a[i].pTab) && IsVirtual(pTabList->a[i].pTab) ){
      pWC->vmask |= ((Bitmask)1 << i);
    }
#endif
  }
#ifndef NDEBUG
  {
    Bitmask toTheLeft = 0;
    for(i=0; i<pTabList->nSrc; i++){
      Bitmask m = getMask(pMaskSet, pTabList->a[i].iCursor);
      assert( (m-1)==toTheLeft );
      toTheLeft |= m;
    }
  }
#endif

  /* Analyze all of the subexpressions.  Note that exprAnalyze() might
  ** add new virtual terms onto the end of the WHERE clause.  We do not
  ** want to analyze these virtual terms, so start analyzing at the end
  ** and work forward so that the added virtual terms are never processed.
  */
  exprAnalyzeAll(pTabList, pWC);
  if( db->mallocFailed ){
    goto whereBeginError;
  }

  /* Check if the DISTINCT qualifier, if there is one, is redundant. 
  ** If it is, then set pDistinct to NULL and WhereInfo.eDistinct to
  ** WHERE_DISTINCT_UNIQUE to tell the caller to ignore the DISTINCT.
  */
  if( pDistinct && isDistinctRedundant(pParse, pTabList, pWC, pDistinct) ){
    pDistinct = 0;
    pWInfo->eDistinct = WHERE_DISTINCT_UNIQUE;
  }

  /* Chose the best index to use for each table in the FROM clause.
  **
  ** This loop fills in the following fields:
  **
  **   pWInfo->a[].pIdx      The index to use for this level of the loop.
  **   pWInfo->a[].wsFlags   WHERE_xxx flags associated with pIdx
  **   pWInfo->a[].nEq       The number of == and IN constraints
  **   pWInfo->a[].iFrom     Which term of the FROM clause is being coded
  **   pWInfo->a[].iTabCur   The VDBE cursor for the database table
  **   pWInfo->a[].iIdxCur   The VDBE cursor for the index
  **   pWInfo->a[].pTerm     When wsFlags==WO_OR, the OR-clause term
  **
  ** This loop also figures out the nesting order of tables in the FROM
  ** clause.
  */
  notReady = ~(Bitmask)0;
  andFlags = ~0;
  WHERETRACE(("*** Optimizer Start ***\n"));
  for(i=iFrom=0, pLevel=pWInfo->a; i<nTabList; i++, pLevel++){
    WhereCost bestPlan;         /* Most efficient plan seen so far */
    Index *pIdx;                /* Index for FROM table at pTabItem */
    int j;                      /* For looping over FROM tables */
    int bestJ = -1;             /* The value of j */
    Bitmask m;                  /* Bitmask value for j or bestJ */
    int isOptimal;              /* Iterator for optimal/non-optimal search */
    int nUnconstrained;         /* Number tables without INDEXED BY */
    Bitmask notIndexed;         /* Mask of tables that cannot use an index */

    memset(&bestPlan, 0, sizeof(bestPlan));
    bestPlan.rCost = SQLITE_BIG_DBL;
    WHERETRACE(("*** Begin search for loop %d ***\n", i));

    /* Loop through the remaining entries in the FROM clause to find the
    ** next nested loop. The loop tests all FROM clause entries
    ** either once or twice. 
    **
    ** The first test is always performed if there are two or more entries
    ** remaining and never performed if there is only one FROM clause entry
    ** to choose from.  The first test looks for an "optimal" scan.  In
    ** this context an optimal scan is one that uses the same strategy
    ** for the given FROM clause entry as would be selected if the entry
    ** were used as the innermost nested loop.  In other words, a table
    ** is chosen such that the cost of running that table cannot be reduced
    ** by waiting for other tables to run first.  This "optimal" test works
    ** by first assuming that the FROM clause is on the inner loop and finding
    ** its query plan, then checking to see if that query plan uses any
    ** other FROM clause terms that are notReady.  If no notReady terms are
    ** used then the "optimal" query plan works.
    **
    ** Note that the WhereCost.nRow parameter for an optimal scan might
    ** not be as small as it would be if the table really were the innermost
    ** join.  The nRow value can be reduced by WHERE clause constraints
    ** that do not use indices.  But this nRow reduction only happens if the
    ** table really is the innermost join.  
    **
    ** The second loop iteration is only performed if no optimal scan
    ** strategies were found by the first iteration. This second iteration
    ** is used to search for the lowest cost scan overall.
    **
    ** Previous versions of SQLite performed only the second iteration -
    ** the next outermost loop was always that with the lowest overall
    ** cost. However, this meant that SQLite could select the wrong plan
    ** for scripts such as the following:
    **   
    **   CREATE TABLE t1(a, b); 
    **   CREATE TABLE t2(c, d);
    **   SELECT * FROM t2, t1 WHERE t2.rowid = t1.a;
    **
    ** The best strategy is to iterate through table t1 first. However it
    ** is not possible to determine this with a simple greedy algorithm.
    ** Since the cost of a linear scan through table t2 is the same 
    ** as the cost of a linear scan through table t1, a simple greedy 
    ** algorithm may choose to use t2 for the outer loop, which is a much
    ** costlier approach.
    */
    nUnconstrained = 0;
    notIndexed = 0;
    for(isOptimal=(iFrom<nTabList-1); isOptimal>=0 && bestJ<0; isOptimal--){
      Bitmask mask;             /* Mask of tables not yet ready */
      for(j=iFrom, pTabItem=&pTabList->a[j]; j<nTabList; j++, pTabItem++){
        int doNotReorder;    /* True if this table should not be reordered */
        WhereCost sCost;     /* Cost information from best[Virtual]Index() */
        ExprList *pOrderBy;  /* ORDER BY clause for index to optimize */
        ExprList *pDist;     /* DISTINCT clause for index to optimize */
  
        doNotReorder =  (pTabItem->jointype & (JT_LEFT|JT_CROSS))!=0;
        if( j!=iFrom && doNotReorder ) break;
        m = getMask(pMaskSet, pTabItem->iCursor);
        if( (m & notReady)==0 ){
          if( j==iFrom ) iFrom++;
          continue;
        }
        mask = (isOptimal ? m : notReady);
        pOrderBy = ((i==0 && ppOrderBy )?*ppOrderBy:0);
        pDist = (i==0 ? pDistinct : 0);
        if( pTabItem->pIndex==0 ) nUnconstrained++;
  
        WHERETRACE(("=== trying table %d with isOptimal=%d ===\n",
                    j, isOptimal));
        assert( pTabItem->pTab );
#ifndef SQLITE_OMIT_VIRTUALTABLE
        if( IsVirtual(pTabItem->pTab) ){
          sqlite3_index_info **pp = &pWInfo->a[j].pIdxInfo;
          bestVirtualIndex(pParse, pWC, pTabItem, mask, notReady, pOrderBy,
                           &sCost, pp);
        }else 
#endif
        {
          bestBtreeIndex(pParse, pWC, pTabItem, mask, notReady, pOrderBy,
              pDist, &sCost);
        }
        assert( isOptimal || (sCost.used&notReady)==0 );

        /* If an INDEXED BY clause is present, then the plan must use that
        ** index if it uses any index at all */
        assert( pTabItem->pIndex==0 
                  || (sCost.plan.wsFlags & WHERE_NOT_FULLSCAN)==0
                  || sCost.plan.u.pIdx==pTabItem->pIndex );

        if( isOptimal && (sCost.plan.wsFlags & WHERE_NOT_FULLSCAN)==0 ){
          notIndexed |= m;
        }

        /* Conditions under which this table becomes the best so far:
        **
        **   (1) The table must not depend on other tables that have not
        **       yet run.
        **
        **   (2) A full-table-scan plan cannot supercede indexed plan unless
        **       the full-table-scan is an "optimal" plan as defined above.
        **
        **   (3) All tables have an INDEXED BY clause or this table lacks an
        **       INDEXED BY clause or this table uses the specific
        **       index specified by its INDEXED BY clause.  This rule ensures
        **       that a best-so-far is always selected even if an impossible
        **       combination of INDEXED BY clauses are given.  The error
        **       will be detected and relayed back to the application later.
        **       The NEVER() comes about because rule (2) above prevents
        **       An indexable full-table-scan from reaching rule (3).
        **
        **   (4) The plan cost must be lower than prior plans or else the
        **       cost must be the same and the number of rows must be lower.
        */
        if( (sCost.used&notReady)==0                       /* (1) */
            && (bestJ<0 || (notIndexed&m)!=0               /* (2) */
                || (bestPlan.plan.wsFlags & WHERE_NOT_FULLSCAN)==0
                || (sCost.plan.wsFlags & WHERE_NOT_FULLSCAN)!=0)
            && (nUnconstrained==0 || pTabItem->pIndex==0   /* (3) */
                || NEVER((sCost.plan.wsFlags & WHERE_NOT_FULLSCAN)!=0))
            && (bestJ<0 || sCost.rCost<bestPlan.rCost      /* (4) */
                || (sCost.rCost<=bestPlan.rCost 
                 && sCost.plan.nRow<bestPlan.plan.nRow))
        ){
          WHERETRACE(("=== table %d is best so far"
                      " with cost=%g and nRow=%g\n",
                      j, sCost.rCost, sCost.plan.nRow));
          bestPlan = sCost;
          bestJ = j;
        }
        if( doNotReorder ) break;
      }
    }
    assert( bestJ>=0 );
    assert( notReady & getMask(pMaskSet, pTabList->a[bestJ].iCursor) );
    WHERETRACE(("*** Optimizer selects table %d for loop %d"
                " with cost=%g and nRow=%g\n",
                bestJ, pLevel-pWInfo->a, bestPlan.rCost, bestPlan.plan.nRow));
    /* The ALWAYS() that follows was added to hush up clang scan-build */
    if( (bestPlan.plan.wsFlags & WHERE_ORDERBY)!=0 && ALWAYS(ppOrderBy) ){
      *ppOrderBy = 0;
    }
    if( (bestPlan.plan.wsFlags & WHERE_DISTINCT)!=0 ){
      assert( pWInfo->eDistinct==0 );
      pWInfo->eDistinct = WHERE_DISTINCT_ORDERED;
    }
    andFlags &= bestPlan.plan.wsFlags;
    pLevel->plan = bestPlan.plan;
    testcase( bestPlan.plan.wsFlags & WHERE_INDEXED );
    testcase( bestPlan.plan.wsFlags & WHERE_TEMP_INDEX );
    if( bestPlan.plan.wsFlags & (WHERE_INDEXED|WHERE_TEMP_INDEX) ){
      pLevel->iIdxCur = pParse->nTab++;
    }else{
      pLevel->iIdxCur = -1;
    }
    notReady &= ~getMask(pMaskSet, pTabList->a[bestJ].iCursor);
    pLevel->iFrom = (u8)bestJ;
    if( bestPlan.plan.nRow>=(double)1 ){
      pParse->nQueryLoop *= bestPlan.plan.nRow;
    }

    /* Check that if the table scanned by this loop iteration had an
    ** INDEXED BY clause attached to it, that the named index is being
    ** used for the scan. If not, then query compilation has failed.
    ** Return an error.
    */
    pIdx = pTabList->a[bestJ].pIndex;
    if( pIdx ){
      if( (bestPlan.plan.wsFlags & WHERE_INDEXED)==0 ){
        sqlite3ErrorMsg(pParse, "cannot use index: %s", pIdx->zName);
        goto whereBeginError;
      }else{
        /* If an INDEXED BY clause is used, the bestIndex() function is
        ** guaranteed to find the index specified in the INDEXED BY clause
        ** if it find an index at all. */
        assert( bestPlan.plan.u.pIdx==pIdx );
      }
    }
  }
  WHERETRACE(("*** Optimizer Finished ***\n"));
  if( pParse->nErr || db->mallocFailed ){
    goto whereBeginError;
  }

  /* If the total query only selects a single row, then the ORDER BY
  ** clause is irrelevant.
  */
  if( (andFlags & WHERE_UNIQUE)!=0 && ppOrderBy ){
    *ppOrderBy = 0;
  }

  /* If the caller is an UPDATE or DELETE statement that is requesting
  ** to use a one-pass algorithm, determine if this is appropriate.
  ** The one-pass algorithm only works if the WHERE clause constraints
  ** the statement to update a single row.
  */
  assert( (wctrlFlags & WHERE_ONEPASS_DESIRED)==0 || pWInfo->nLevel==1 );
  if( (wctrlFlags & WHERE_ONEPASS_DESIRED)!=0 && (andFlags & WHERE_UNIQUE)!=0 ){
    pWInfo->okOnePass = 1;
    pWInfo->a[0].plan.wsFlags &= ~WHERE_IDX_ONLY;
  }

  /* Open all tables in the pTabList and any indices selected for
  ** searching those tables.
  */
  sqlite3CodeVerifySchema(pParse, -1); /* Insert the cookie verifier Goto */
  notReady = ~(Bitmask)0;
  pWInfo->nRowOut = (double)1;
  for(i=0, pLevel=pWInfo->a; i<nTabList; i++, pLevel++){
    Table *pTab;     /* Table to open */
    int iDb;         /* Index of database containing table/index */

    pTabItem = &pTabList->a[pLevel->iFrom];
    pTab = pTabItem->pTab;
    pLevel->iTabCur = pTabItem->iCursor;
    pWInfo->nRowOut *= pLevel->plan.nRow;
    iDb = sqlite3SchemaToIndex(db, pTab->pSchema);
    if( (pTab->tabFlags & TF_Ephemeral)!=0 || pTab->pSelect ){
      /* Do nothing */
    }else
#ifndef SQLITE_OMIT_VIRTUALTABLE
    if( (pLevel->plan.wsFlags & WHERE_VIRTUALTABLE)!=0 ){
      const char *pVTab = (const char *)sqlite3GetVTable(db, pTab);
      int iCur = pTabItem->iCursor;
      sqlite3VdbeAddOp4(v, OP_VOpen, iCur, 0, 0, pVTab, P4_VTAB);
    }else
#endif
    if( (pLevel->plan.wsFlags & WHERE_IDX_ONLY)==0
         && (wctrlFlags & WHERE_OMIT_OPEN_CLOSE)==0 ){
      int op = pWInfo->okOnePass ? OP_OpenWrite : OP_OpenRead;
      sqlite3OpenTable(pParse, pTabItem->iCursor, iDb, pTab, op);
      testcase( pTab->nCol==BMS-1 );
      testcase( pTab->nCol==BMS );
      if( !pWInfo->okOnePass && pTab->nCol<BMS ){
        Bitmask b = pTabItem->colUsed;
        int n = 0;
        for(; b; b=b>>1, n++){}
        sqlite3VdbeChangeP4(v, sqlite3VdbeCurrentAddr(v)-1, 
                            SQLITE_INT_TO_PTR(n), P4_INT32);
        assert( n<=pTab->nCol );
      }
    }else{
      sqlite3TableLock(pParse, iDb, pTab->tnum, 0, pTab->zName);
    }
#ifndef SQLITE_OMIT_AUTOMATIC_INDEX
    if( (pLevel->plan.wsFlags & WHERE_TEMP_INDEX)!=0 ){
      constructAutomaticIndex(pParse, pWC, pTabItem, notReady, pLevel);
    }else
#endif
    if( (pLevel->plan.wsFlags & WHERE_INDEXED)!=0 ){
      Index *pIx = pLevel->plan.u.pIdx;
      KeyInfo *pKey = sqlite3IndexKeyinfo(pParse, pIx);
      int iIdxCur = pLevel->iIdxCur;
      assert( pIx->pSchema==pTab->pSchema );
      assert( iIdxCur>=0 );
      sqlite3VdbeAddOp4(v, OP_OpenRead, iIdxCur, pIx->tnum, iDb,
                        (char*)pKey, P4_KEYINFO_HANDOFF);
      VdbeComment((v, "%s", pIx->zName));
    }
    sqlite3CodeVerifySchema(pParse, iDb);
    notReady &= ~getMask(pWC->pMaskSet, pTabItem->iCursor);
  }
  pWInfo->iTop = sqlite3VdbeCurrentAddr(v);
  if( db->mallocFailed ) goto whereBeginError;

  /* Generate the code to do the search.  Each iteration of the for
  ** loop below generates code for a single nested loop of the VM
  ** program.
  */
  notReady = ~(Bitmask)0;
  for(i=0; i<nTabList; i++){
    pLevel = &pWInfo->a[i];
    explainOneScan(pParse, pTabList, pLevel, i, pLevel->iFrom, wctrlFlags);
    notReady = codeOneLoopStart(pWInfo, i, wctrlFlags, notReady, pWhere);
    pWInfo->iContinue = pLevel->addrCont;
  }

#ifdef SQLITE_TEST  /* For testing and debugging use only */
  /* Record in the query plan information about the current table
  ** and the index used to access it (if any).  If the table itself
  ** is not used, its name is just '{}'.  If no index is used
  ** the index is listed as "{}".  If the primary key is used the
  ** index name is '*'.
  */
  for(i=0; i<nTabList; i++){
    char *z;
    int n;
    pLevel = &pWInfo->a[i];
    pTabItem = &pTabList->a[pLevel->iFrom];
    z = pTabItem->zAlias;
    if( z==0 ) z = pTabItem->pTab->zName;
    n = sqlite3Strlen30(z);
    if( n+nQPlan < sizeof(sqlite3_query_plan)-10 ){
      if( pLevel->plan.wsFlags & WHERE_IDX_ONLY ){
        memcpy(&sqlite3_query_plan[nQPlan], "{}", 2);
        nQPlan += 2;
      }else{
        memcpy(&sqlite3_query_plan[nQPlan], z, n);
        nQPlan += n;
      }
      sqlite3_query_plan[nQPlan++] = ' ';
    }
    testcase( pLevel->plan.wsFlags & WHERE_ROWID_EQ );
    testcase( pLevel->plan.wsFlags & WHERE_ROWID_RANGE );
    if( pLevel->plan.wsFlags & (WHERE_ROWID_EQ|WHERE_ROWID_RANGE) ){
      memcpy(&sqlite3_query_plan[nQPlan], "* ", 2);
      nQPlan += 2;
    }else if( (pLevel->plan.wsFlags & WHERE_INDEXED)!=0 ){
      n = sqlite3Strlen30(pLevel->plan.u.pIdx->zName);
      if( n+nQPlan < sizeof(sqlite3_query_plan)-2 ){
        memcpy(&sqlite3_query_plan[nQPlan], pLevel->plan.u.pIdx->zName, n);
        nQPlan += n;
        sqlite3_query_plan[nQPlan++] = ' ';
      }
    }else{
      memcpy(&sqlite3_query_plan[nQPlan], "{} ", 3);
      nQPlan += 3;
    }
  }
  while( nQPlan>0 && sqlite3_query_plan[nQPlan-1]==' ' ){
    sqlite3_query_plan[--nQPlan] = 0;
  }
  sqlite3_query_plan[nQPlan] = 0;
  nQPlan = 0;
#endif /* SQLITE_TEST // Testing and debugging use only */

  /* Record the continuation address in the WhereInfo structure.  Then
  ** clean up and return.
  */
  return pWInfo;

  /* Jump here if malloc fails */
whereBeginError:
  if( pWInfo ){
    pParse->nQueryLoop = pWInfo->savedNQueryLoop;
    whereInfoFree(db, pWInfo);
  }
  return 0;
}

/*
** Generate the end of the WHERE loop.  See comments on 
** sqlite3WhereBegin() for additional information.
*/
SQLITE_PRIVATE void sqlite3WhereEnd(WhereInfo *pWInfo){
  Parse *pParse = pWInfo->pParse;
  Vdbe *v = pParse->pVdbe;
  int i;
  WhereLevel *pLevel;
  SrcList *pTabList = pWInfo->pTabList;
  sqlite3 *db = pParse->db;

  /* Generate loop termination code.
  */
  sqlite3ExprCacheClear(pParse);
  for(i=pWInfo->nLevel-1; i>=0; i--){
    pLevel = &pWInfo->a[i];
    sqlite3VdbeResolveLabel(v, pLevel->addrCont);
    if( pLevel->op!=OP_Noop ){
      sqlite3VdbeAddOp2(v, pLevel->op, pLevel->p1, pLevel->p2);
      sqlite3VdbeChangeP5(v, pLevel->p5);
    }
    if( pLevel->plan.wsFlags & WHERE_IN_ABLE && pLevel->u.in.nIn>0 ){
      struct InLoop *pIn;
      int j;
      sqlite3VdbeResolveLabel(v, pLevel->addrNxt);
      for(j=pLevel->u.in.nIn, pIn=&pLevel->u.in.aInLoop[j-1]; j>0; j--, pIn--){
        sqlite3VdbeJumpHere(v, pIn->addrInTop+1);
        sqlite3VdbeAddOp2(v, OP_Next, pIn->iCur, pIn->addrInTop);
        sqlite3VdbeJumpHere(v, pIn->addrInTop-1);
      }
      sqlite3DbFree(db, pLevel->u.in.aInLoop);
    }
    sqlite3VdbeResolveLabel(v, pLevel->addrBrk);
    if( pLevel->iLeftJoin ){
      int addr;
      addr = sqlite3VdbeAddOp1(v, OP_IfPos, pLevel->iLeftJoin);
      assert( (pLevel->plan.wsFlags & WHERE_IDX_ONLY)==0
           || (pLevel->plan.wsFlags & WHERE_INDEXED)!=0 );
      if( (pLevel->plan.wsFlags & WHERE_IDX_ONLY)==0 ){
        sqlite3VdbeAddOp1(v, OP_NullRow, pTabList->a[i].iCursor);
      }
      if( pLevel->iIdxCur>=0 ){
        sqlite3VdbeAddOp1(v, OP_NullRow, pLevel->iIdxCur);
      }
      if( pLevel->op==OP_Return ){
        sqlite3VdbeAddOp2(v, OP_Gosub, pLevel->p1, pLevel->addrFirst);
      }else{
        sqlite3VdbeAddOp2(v, OP_Goto, 0, pLevel->addrFirst);
      }
      sqlite3VdbeJumpHere(v, addr);
    }
  }

  /* The "break" point is here, just past the end of the outer loop.
  ** Set it.
  */
  sqlite3VdbeResolveLabel(v, pWInfo->iBreak);

  /* Close all of the cursors that were opened by sqlite3WhereBegin.
  */
  assert( pWInfo->nLevel==1 || pWInfo->nLevel==pTabList->nSrc );
  for(i=0, pLevel=pWInfo->a; i<pWInfo->nLevel; i++, pLevel++){
    struct SrcList_item *pTabItem = &pTabList->a[pLevel->iFrom];
    Table *pTab = pTabItem->pTab;
    assert( pTab!=0 );
    if( (pTab->tabFlags & TF_Ephemeral)==0
     && pTab->pSelect==0
     && (pWInfo->wctrlFlags & WHERE_OMIT_OPEN_CLOSE)==0
    ){
      int ws = pLevel->plan.wsFlags;
      if( !pWInfo->okOnePass && (ws & WHERE_IDX_ONLY)==0 ){
        sqlite3VdbeAddOp1(v, OP_Close, pTabItem->iCursor);
      }
      if( (ws & WHERE_INDEXED)!=0 && (ws & WHERE_TEMP_INDEX)==0 ){
        sqlite3VdbeAddOp1(v, OP_Close, pLevel->iIdxCur);
      }
    }

    /* If this scan uses an index, make code substitutions to read data
    ** from the index in preference to the table. Sometimes, this means
    ** the table need never be read from. This is a performance boost,
    ** as the vdbe level waits until the table is read before actually
    ** seeking the table cursor to the record corresponding to the current
    ** position in the index.
    ** 
    ** Calls to the code generator in between sqlite3WhereBegin and
    ** sqlite3WhereEnd will have created code that references the table
    ** directly.  This loop scans all that code looking for opcodes
    ** that reference the table and converts them into opcodes that
    ** reference the index.
    */
    if( (pLevel->plan.wsFlags & WHERE_INDEXED)!=0 && !db->mallocFailed){
      int k, j, last;
      VdbeOp *pOp;
      Index *pIdx = pLevel->plan.u.pIdx;

      assert( pIdx!=0 );
      pOp = sqlite3VdbeGetOp(v, pWInfo->iTop);
      last = sqlite3VdbeCurrentAddr(v);
      for(k=pWInfo->iTop; k<last; k++, pOp++){
        if( pOp->p1!=pLevel->iTabCur ) continue;
        if( pOp->opcode==OP_Column ){
          for(j=0; j<pIdx->nColumn; j++){
            if( pOp->p2==pIdx->aiColumn[j] ){
              pOp->p2 = j;
              pOp->p1 = pLevel->iIdxCur;
              break;
            }
          }
          assert( (pLevel->plan.wsFlags & WHERE_IDX_ONLY)==0
               || j<pIdx->nColumn );
        }else if( pOp->opcode==OP_Rowid ){
          pOp->p1 = pLevel->iIdxCur;
          pOp->opcode = OP_IdxRowid;
        }
      }
    }
  }

  /* Final cleanup
  */
  pParse->nQueryLoop = pWInfo->savedNQueryLoop;
  whereInfoFree(db, pWInfo);
  return;
}

/************** End of where.c ***********************************************/
/************** Begin file parse.c *******************************************/
/* Driver template for the LEMON parser generator.
** The author disclaims copyright to this source code.
**
** This version of "lempar.c" is modified, slightly, for use by SQLite.
** The only modifications are the addition of a couple of NEVER()
** macros to disable tests that are needed in the case of a general
** LALR(1) grammar but which are always false in the
** specific grammar used by SQLite.
*/
/* First off, code is included that follows the "include" declaration
** in the input grammar file. */
/* #include <stdio.h> */


/*
** Disable all error recovery processing in the parser push-down
** automaton.
*/
#define YYNOERRORRECOVERY 1

/*
** Make yytestcase() the same as testcase()
*/
#define yytestcase(X) testcase(X)

/*
** An instance of this structure holds information about the
** LIMIT clause of a SELECT statement.
*/
struct LimitVal {
  Expr *pLimit;    /* The LIMIT expression.  NULL if there is no limit */
  Expr *pOffset;   /* The OFFSET expression.  NULL if there is none */
};

/*
** An instance of this structure is used to store the LIKE,
** GLOB, NOT LIKE, and NOT GLOB operators.
*/
struct LikeOp {
  Token eOperator;  /* "like" or "glob" or "regexp" */
  int not;         /* True if the NOT keyword is present */
};

/*
** An instance of the following structure describes the event of a
** TRIGGER.  "a" is the event type, one of TK_UPDATE, TK_INSERT,
** TK_DELETE, or TK_INSTEAD.  If the event is of the form
**
**      UPDATE ON (a,b,c)
**
** Then the "b" IdList records the list "a,b,c".
*/
struct TrigEvent { int a; IdList * b; };

/*
** An instance of this structure holds the ATTACH key and the key type.
*/
struct AttachKey { int type;  Token key; };


  /* This is a utility routine used to set the ExprSpan.zStart and
  ** ExprSpan.zEnd values of pOut so that the span covers the complete
  ** range of text beginning with pStart and going to the end of pEnd.
  */
  static void spanSet(ExprSpan *pOut, Token *pStart, Token *pEnd){
    pOut->zStart = pStart->z;
    pOut->zEnd = &pEnd->z[pEnd->n];
  }

  /* Construct a new Expr object from a single identifier.  Use the
  ** new Expr to populate pOut.  Set the span of pOut to be the identifier
  ** that created the expression.
  */
  static void spanExpr(ExprSpan *pOut, Parse *pParse, int op, Token *pValue){
    pOut->pExpr = sqlite3PExpr(pParse, op, 0, 0, pValue);
    pOut->zStart = pValue->z;
    pOut->zEnd = &pValue->z[pValue->n];
  }

  /* This routine constructs a binary expression node out of two ExprSpan
  ** objects and uses the result to populate a new ExprSpan object.
  */
  static void spanBinaryExpr(
    ExprSpan *pOut,     /* Write the result here */
    Parse *pParse,      /* The parsing context.  Errors accumulate here */
    int op,             /* The binary operation */
    ExprSpan *pLeft,    /* The left operand */
    ExprSpan *pRight    /* The right operand */
  ){
    pOut->pExpr = sqlite3PExpr(pParse, op, pLeft->pExpr, pRight->pExpr, 0);
    pOut->zStart = pLeft->zStart;
    pOut->zEnd = pRight->zEnd;
  }

  /* Construct an expression node for a unary postfix operator
  */
  static void spanUnaryPostfix(
    ExprSpan *pOut,        /* Write the new expression node here */
    Parse *pParse,         /* Parsing context to record errors */
    int op,                /* The operator */
    ExprSpan *pOperand,    /* The operand */
    Token *pPostOp         /* The operand token for setting the span */
  ){
    pOut->pExpr = sqlite3PExpr(pParse, op, pOperand->pExpr, 0, 0);
    pOut->zStart = pOperand->zStart;
    pOut->zEnd = &pPostOp->z[pPostOp->n];
  }                           

  /* A routine to convert a binary TK_IS or TK_ISNOT expression into a
  ** unary TK_ISNULL or TK_NOTNULL expression. */
  static void binaryToUnaryIfNull(Parse *pParse, Expr *pY, Expr *pA, int op){
    sqlite3 *db = pParse->db;
    if( db->mallocFailed==0 && pY->op==TK_NULL ){
      pA->op = (u8)op;
      sqlite3ExprDelete(db, pA->pRight);
      pA->pRight = 0;
    }
  }

  /* Construct an expression node for a unary prefix operator
  */
  static void spanUnaryPrefix(
    ExprSpan *pOut,        /* Write the new expression node here */
    Parse *pParse,         /* Parsing context to record errors */
    int op,                /* The operator */
    ExprSpan *pOperand,    /* The operand */
    Token *pPreOp         /* The operand token for setting the span */
  ){
    pOut->pExpr = sqlite3PExpr(pParse, op, pOperand->pExpr, 0, 0);
    pOut->zStart = pPreOp->z;
    pOut->zEnd = pOperand->zEnd;
  }
/* Next is all token values, in a form suitable for use by makeheaders.
** This section will be null unless lemon is run with the -m switch.
*/
/* 
** These constants (all generated automatically by the parser generator)
** specify the various kinds of tokens (terminals) that the parser
** understands. 
**
** Each symbol here is a terminal symbol in the grammar.
*/
/* Make sure the INTERFACE macro is defined.
*/
#ifndef INTERFACE
# define INTERFACE 1
#endif
/* The next thing included is series of defines which control
** various aspects of the generated parser.
**    YYCODETYPE         is the data type used for storing terminal
**                       and nonterminal numbers.  "unsigned char" is
**                       used if there are fewer than 250 terminals
**                       and nonterminals.  "int" is used otherwise.
**    YYNOCODE           is a number of type YYCODETYPE which corresponds
**                       to no legal terminal or nonterminal number.  This
**                       number is used to fill in empty slots of the hash 
**                       table.
**    YYFALLBACK         If defined, this indicates that one or more tokens
**                       have fall-back values which should be used if the
**                       original value of the token will not parse.
**    YYACTIONTYPE       is the data type used for storing terminal
**                       and nonterminal numbers.  "unsigned char" is
**                       used if there are fewer than 250 rules and
**                       states combined.  "int" is used otherwise.
**    sqlite3ParserTOKENTYPE     is the data type used for minor tokens given 
**                       directly to the parser from the tokenizer.
**    YYMINORTYPE        is the data type used for all minor tokens.
**                       This is typically a union of many types, one of
**                       which is sqlite3ParserTOKENTYPE.  The entry in the union
**                       for base tokens is called "yy0".
**    YYSTACKDEPTH       is the maximum depth of the parser's stack.  If
**                       zero the stack is dynamically sized using realloc()
**    sqlite3ParserARG_SDECL     A static variable declaration for the %extra_argument
**    sqlite3ParserARG_PDECL     A parameter declaration for the %extra_argument
**    sqlite3ParserARG_STORE     Code to store %extra_argument into yypParser
**    sqlite3ParserARG_FETCH     Code to extract %extra_argument from yypParser
**    YYNSTATE           the combined number of states.
**    YYNRULE            the number of rules in the grammar
**    YYERRORSYMBOL      is the code number of the error symbol.  If not
**                       defined, then do no error processing.
*/
#define YYCODETYPE unsigned char
#define YYNOCODE 253
#define YYACTIONTYPE unsigned short int
#define YYWILDCARD 67
#define sqlite3ParserTOKENTYPE Token
typedef union {
  int yyinit;
  sqlite3ParserTOKENTYPE yy0;
  int yy4;
  struct TrigEvent yy90;
  ExprSpan yy118;
  TriggerStep* yy203;
  u8 yy210;
  struct {int value; int mask;} yy215;
  SrcList* yy259;
  struct LimitVal yy292;
  Expr* yy314;
  ExprList* yy322;
  struct LikeOp yy342;
  IdList* yy384;
  Select* yy387;
} YYMINORTYPE;
#ifndef YYSTACKDEPTH
#define YYSTACKDEPTH 100
#endif
#define sqlite3ParserARG_SDECL Parse *pParse;
#define sqlite3ParserARG_PDECL ,Parse *pParse
#define sqlite3ParserARG_FETCH Parse *pParse = yypParser->pParse
#define sqlite3ParserARG_STORE yypParser->pParse = pParse
#define YYNSTATE 630
#define YYNRULE 329
#define YYFALLBACK 1
#define YY_NO_ACTION      (YYNSTATE+YYNRULE+2)
#define YY_ACCEPT_ACTION  (YYNSTATE+YYNRULE+1)
#define YY_ERROR_ACTION   (YYNSTATE+YYNRULE)

/* The yyzerominor constant is used to initialize instances of
** YYMINORTYPE objects to zero. */
static const YYMINORTYPE yyzerominor = { 0 };

/* Define the yytestcase() macro to be a no-op if is not already defined
** otherwise.
**
** Applications can choose to define yytestcase() in the %include section
** to a macro that can assist in verifying code coverage.  For production
** code the yytestcase() macro should be turned off.  But it is useful
** for testing.
*/
#ifndef yytestcase
# define yytestcase(X)
#endif


/* Next are the tables used to determine what action to take based on the
** current state and lookahead token.  These tables are used to implement
** functions that take a state number and lookahead value and return an
** action integer.  
**
** Suppose the action integer is N.  Then the action is determined as
** follows
**
**   0 <= N < YYNSTATE                  Shift N.  That is, push the lookahead
**                                      token onto the stack and goto state N.
**
**   YYNSTATE <= N < YYNSTATE+YYNRULE   Reduce by rule N-YYNSTATE.
**
**   N == YYNSTATE+YYNRULE              A syntax error has occurred.
**
**   N == YYNSTATE+YYNRULE+1            The parser accepts its input.
**
**   N == YYNSTATE+YYNRULE+2            No such action.  Denotes unused
**                                      slots in the yy_action[] table.
**
** The action table is constructed as a single large table named yy_action[].
** Given state S and lookahead X, the action is computed as
**
**      yy_action[ yy_shift_ofst[S] + X ]
**
** If the index value yy_shift_ofst[S]+X is out of range or if the value
** yy_lookahead[yy_shift_ofst[S]+X] is not equal to X or if yy_shift_ofst[S]
** is equal to YY_SHIFT_USE_DFLT, it means that the action is not in the table
** and that yy_default[S] should be used instead.  
**
** The formula above is for computing the action when the lookahead is
** a terminal symbol.  If the lookahead is a non-terminal (as occurs after
** a reduce action) then the yy_reduce_ofst[] array is used in place of
** the yy_shift_ofst[] array and YY_REDUCE_USE_DFLT is used in place of
** YY_SHIFT_USE_DFLT.
**
** The following are the tables generated in this section:
**
**  yy_action[]        A single table containing all actions.
**  yy_lookahead[]     A table containing the lookahead for each entry in
**                     yy_action.  Used to detect hash collisions.
**  yy_shift_ofst[]    For each state, the offset into yy_action for
**                     shifting terminals.
**  yy_reduce_ofst[]   For each state, the offset into yy_action for
**                     shifting non-terminals after a reduce.
**  yy_default[]       Default action for each state.
*/
#define YY_ACTTAB_COUNT (1557)
static const YYACTIONTYPE yy_action[] = {
 /*     0 */   313,  960,  186,  419,    2,  172,  627,  597,   55,   55,
 /*    10 */    55,   55,   48,   53,   53,   53,   53,   52,   52,   51,
 /*    20 */    51,   51,   50,  238,  302,  283,  623,  622,  516,  515,
 /*    30 */   590,  584,   55,   55,   55,   55,  282,   53,   53,   53,
 /*    40 */    53,   52,   52,   51,   51,   51,   50,  238,    6,   56,
 /*    50 */    57,   47,  582,  581,  583,  583,   54,   54,   55,   55,
 /*    60 */    55,   55,  608,   53,   53,   53,   53,   52,   52,   51,
 /*    70 */    51,   51,   50,  238,  313,  597,  409,  330,  579,  579,
 /*    80 */    32,   53,   53,   53,   53,   52,   52,   51,   51,   51,
 /*    90 */    50,  238,  330,  217,  620,  619,  166,  411,  624,  382,
 /*   100 */   379,  378,    7,  491,  590,  584,  200,  199,  198,   58,
 /*   110 */   377,  300,  414,  621,  481,   66,  623,  622,  621,  580,
 /*   120 */   254,  601,   94,   56,   57,   47,  582,  581,  583,  583,
 /*   130 */    54,   54,   55,   55,   55,   55,  671,   53,   53,   53,
 /*   140 */    53,   52,   52,   51,   51,   51,   50,  238,  313,  532,
 /*   150 */   226,  506,  507,  133,  177,  139,  284,  385,  279,  384,
 /*   160 */   169,  197,  342,  398,  251,  226,  253,  275,  388,  167,
 /*   170 */   139,  284,  385,  279,  384,  169,  570,  236,  590,  584,
 /*   180 */   672,  240,  275,  157,  620,  619,  554,  437,   51,   51,
 /*   190 */    51,   50,  238,  343,  439,  553,  438,   56,   57,   47,
 /*   200 */   582,  581,  583,  583,   54,   54,   55,   55,   55,   55,
 /*   210 */   465,   53,   53,   53,   53,   52,   52,   51,   51,   51,
 /*   220 */    50,  238,  313,  390,   52,   52,   51,   51,   51,   50,
 /*   230 */   238,  391,  166,  491,  566,  382,  379,  378,  409,  440,
 /*   240 */   579,  579,  252,  440,  607,   66,  377,  513,  621,   49,
 /*   250 */    46,  147,  590,  584,  621,   16,  466,  189,  621,  441,
 /*   260 */   442,  673,  526,  441,  340,  577,  595,   64,  194,  482,
 /*   270 */   434,   56,   57,   47,  582,  581,  583,  583,   54,   54,
 /*   280 */    55,   55,   55,   55,   30,   53,   53,   53,   53,   52,
 /*   290 */    52,   51,   51,   51,   50,  238,  313,  593,  593,  593,
 /*   300 */   387,  578,  606,  493,  259,  351,  258,  411,    1,  623,
 /*   310 */   622,  496,  623,  622,   65,  240,  623,  622,  597,  443,
 /*   320 */   237,  239,  414,  341,  237,  602,  590,  584,   18,  603,
 /*   330 */   166,  601,   87,  382,  379,  378,   67,  623,  622,   38,
 /*   340 */   623,  622,  176,  270,  377,   56,   57,   47,  582,  581,
 /*   350 */   583,  583,   54,   54,   55,   55,   55,   55,  175,   53,
 /*   360 */    53,   53,   53,   52,   52,   51,   51,   51,   50,  238,
 /*   370 */   313,  396,  233,  411,  531,  565,  317,  620,  619,   44,
 /*   380 */   620,  619,  240,  206,  620,  619,  597,  266,  414,  268,
 /*   390 */   409,  597,  579,  579,  352,  184,  505,  601,   73,  533,
 /*   400 */   590,  584,  466,  548,  190,  620,  619,  576,  620,  619,
 /*   410 */   547,  383,  551,   35,  332,  575,  574,  600,  504,   56,
 /*   420 */    57,   47,  582,  581,  583,  583,   54,   54,   55,   55,
 /*   430 */    55,   55,  567,   53,   53,   53,   53,   52,   52,   51,
 /*   440 */    51,   51,   50,  238,  313,  411,  561,  561,  528,  364,
 /*   450 */   259,  351,  258,  183,  361,  549,  524,  374,  411,  597,
 /*   460 */   414,  240,  560,  560,  409,  604,  579,  579,  328,  601,
 /*   470 */    93,  623,  622,  414,  590,  584,  237,  564,  559,  559,
 /*   480 */   520,  402,  601,   87,  409,  210,  579,  579,  168,  421,
 /*   490 */   950,  519,  950,   56,   57,   47,  582,  581,  583,  583,
 /*   500 */    54,   54,   55,   55,   55,   55,  192,   53,   53,   53,
 /*   510 */    53,   52,   52,   51,   51,   51,   50,  238,  313,  600,
 /*   520 */   293,  563,  511,  234,  357,  146,  475,  475,  367,  411,
 /*   530 */   562,  411,  358,  542,  425,  171,  411,  215,  144,  620,
 /*   540 */   619,  544,  318,  353,  414,  203,  414,  275,  590,  584,
 /*   550 */   549,  414,  174,  601,   94,  601,   79,  558,  471,   61,
 /*   560 */   601,   79,  421,  949,  350,  949,   34,   56,   57,   47,
 /*   570 */   582,  581,  583,  583,   54,   54,   55,   55,   55,   55,
 /*   580 */   535,   53,   53,   53,   53,   52,   52,   51,   51,   51,
 /*   590 */    50,  238,  313,  307,  424,  394,  272,   49,   46,  147,
 /*   600 */   349,  322,    4,  411,  491,  312,  321,  425,  568,  492,
 /*   610 */   216,  264,  407,  575,  574,  429,   66,  549,  414,  621,
 /*   620 */   540,  602,  590,  584,   13,  603,  621,  601,   72,   12,
 /*   630 */   618,  617,  616,  202,  210,  621,  546,  469,  422,  319,
 /*   640 */   148,   56,   57,   47,  582,  581,  583,  583,   54,   54,
 /*   650 */    55,   55,   55,   55,  338,   53,   53,   53,   53,   52,
 /*   660 */    52,   51,   51,   51,   50,  238,  313,  600,  600,  411,
 /*   670 */    39,   21,   37,  170,  237,  875,  411,  572,  572,  201,
 /*   680 */   144,  473,  538,  331,  414,  474,  143,  146,  630,  628,
 /*   690 */   334,  414,  353,  601,   68,  168,  590,  584,  132,  365,
 /*   700 */   601,   96,  307,  423,  530,  336,   49,   46,  147,  568,
 /*   710 */   406,  216,  549,  360,  529,   56,   57,   47,  582,  581,
 /*   720 */   583,  583,   54,   54,   55,   55,   55,   55,  411,   53,
 /*   730 */    53,   53,   53,   52,   52,   51,   51,   51,   50,  238,
 /*   740 */   313,  411,  605,  414,  484,  510,  172,  422,  597,  318,
 /*   750 */   496,  485,  601,   99,  411,  142,  414,  411,  231,  411,
 /*   760 */   540,  411,  359,  629,    2,  601,   97,  426,  308,  414,
 /*   770 */   590,  584,  414,   20,  414,  621,  414,  621,  601,  106,
 /*   780 */   503,  601,  105,  601,  108,  601,  109,  204,   28,   56,
 /*   790 */    57,   47,  582,  581,  583,  583,   54,   54,   55,   55,
 /*   800 */    55,   55,  411,   53,   53,   53,   53,   52,   52,   51,
 /*   810 */    51,   51,   50,  238,  313,  411,  597,  414,  411,  276,
 /*   820 */   214,  600,  411,  366,  213,  381,  601,  134,  274,  500,
 /*   830 */   414,  167,  130,  414,  621,  411,  354,  414,  376,  601,
 /*   840 */   135,  129,  601,  100,  590,  584,  601,  104,  522,  521,
 /*   850 */   414,  621,  224,  273,  600,  167,  327,  282,  600,  601,
 /*   860 */   103,  468,  521,   56,   57,   47,  582,  581,  583,  583,
 /*   870 */    54,   54,   55,   55,   55,   55,  411,   53,   53,   53,
 /*   880 */    53,   52,   52,   51,   51,   51,   50,  238,  313,  411,
 /*   890 */    27,  414,  411,  375,  276,  167,  359,  544,   50,  238,
 /*   900 */   601,   95,  128,  223,  414,  411,  165,  414,  411,  621,
 /*   910 */   411,  621,  612,  601,  102,  372,  601,   76,  590,  584,
 /*   920 */   414,  570,  236,  414,  470,  414,  167,  621,  188,  601,
 /*   930 */    98,  225,  601,  138,  601,  137,  232,   56,   45,   47,
 /*   940 */   582,  581,  583,  583,   54,   54,   55,   55,   55,   55,
 /*   950 */   411,   53,   53,   53,   53,   52,   52,   51,   51,   51,
 /*   960 */    50,  238,  313,  276,  276,  414,  411,  276,  544,  459,
 /*   970 */   359,  171,  209,  479,  601,  136,  628,  334,  621,  621,
 /*   980 */   125,  414,  621,  368,  411,  621,  257,  540,  589,  588,
 /*   990 */   601,   75,  590,  584,  458,  446,   23,   23,  124,  414,
 /*  1000 */   326,  325,  621,  427,  324,  309,  600,  288,  601,   92,
 /*  1010 */   586,  585,   57,   47,  582,  581,  583,  583,   54,   54,
 /*  1020 */    55,   55,   55,   55,  411,   53,   53,   53,   53,   52,
 /*  1030 */    52,   51,   51,   51,   50,  238,  313,  587,  411,  414,
 /*  1040 */   411,  207,  611,  476,  171,  472,  160,  123,  601,   91,
 /*  1050 */   323,  261,   15,  414,  464,  414,  411,  621,  411,  354,
 /*  1060 */   222,  411,  601,   74,  601,   90,  590,  584,  159,  264,
 /*  1070 */   158,  414,  461,  414,  621,  600,  414,  121,  120,   25,
 /*  1080 */   601,   89,  601,  101,  621,  601,   88,   47,  582,  581,
 /*  1090 */   583,  583,   54,   54,   55,   55,   55,   55,  544,   53,
 /*  1100 */    53,   53,   53,   52,   52,   51,   51,   51,   50,  238,
 /*  1110 */    43,  405,  263,    3,  610,  264,  140,  415,  622,   24,
 /*  1120 */   410,   11,  456,  594,  118,  155,  219,  452,  408,  621,
 /*  1130 */   621,  621,  156,   43,  405,  621,    3,  286,  621,  113,
 /*  1140 */   415,  622,  111,  445,  411,  400,  557,  403,  545,   10,
 /*  1150 */   411,  408,  264,  110,  205,  436,  541,  566,  453,  414,
 /*  1160 */   621,  621,   63,  621,  435,  414,  411,  621,  601,   94,
 /*  1170 */   403,  621,  411,  337,  601,   86,  150,   40,   41,  534,
 /*  1180 */   566,  414,  242,  264,   42,  413,  412,  414,  600,  595,
 /*  1190 */   601,   85,  191,  333,  107,  451,  601,   84,  621,  539,
 /*  1200 */    40,   41,  420,  230,  411,  149,  316,   42,  413,  412,
 /*  1210 */   398,  127,  595,  315,  621,  399,  278,  625,  181,  414,
 /*  1220 */   593,  593,  593,  592,  591,   14,  450,  411,  601,   71,
 /*  1230 */   240,  621,   43,  405,  264,    3,  615,  180,  264,  415,
 /*  1240 */   622,  614,  414,  593,  593,  593,  592,  591,   14,  621,
 /*  1250 */   408,  601,   70,  621,  417,   33,  405,  613,    3,  411,
 /*  1260 */   264,  411,  415,  622,  418,  626,  178,  509,    8,  403,
 /*  1270 */   241,  416,  126,  408,  414,  621,  414,  449,  208,  566,
 /*  1280 */   240,  221,  621,  601,   83,  601,   82,  599,  297,  277,
 /*  1290 */   296,   30,  403,   31,  395,  264,  295,  397,  489,   40,
 /*  1300 */    41,  411,  566,  220,  621,  294,   42,  413,  412,  271,
 /*  1310 */   621,  595,  600,  621,   59,   60,  414,  269,  267,  623,
 /*  1320 */   622,   36,   40,   41,  621,  601,   81,  598,  235,   42,
 /*  1330 */   413,  412,  621,  621,  595,  265,  344,  411,  248,  556,
 /*  1340 */   173,  185,  593,  593,  593,  592,  591,   14,  218,   29,
 /*  1350 */   621,  543,  414,  305,  304,  303,  179,  301,  411,  566,
 /*  1360 */   454,  601,   80,  289,  335,  593,  593,  593,  592,  591,
 /*  1370 */    14,  411,  287,  414,  151,  392,  246,  260,  411,  196,
 /*  1380 */   195,  523,  601,   69,  411,  245,  414,  526,  537,  285,
 /*  1390 */   389,  595,  621,  414,  536,  601,   17,  362,  153,  414,
 /*  1400 */   466,  463,  601,   78,  154,  414,  462,  152,  601,   77,
 /*  1410 */   355,  255,  621,  455,  601,    9,  621,  386,  444,  517,
 /*  1420 */   247,  621,  593,  593,  593,  621,  621,  244,  621,  243,
 /*  1430 */   430,  518,  292,  621,  329,  621,  145,  393,  280,  513,
 /*  1440 */   291,  131,  621,  514,  621,  621,  311,  621,  259,  346,
 /*  1450 */   249,  621,  621,  229,  314,  621,  228,  512,  227,  240,
 /*  1460 */   494,  488,  310,  164,  487,  486,  373,  480,  163,  262,
 /*  1470 */   369,  371,  162,   26,  212,  478,  477,  161,  141,  363,
 /*  1480 */   467,  122,  339,  187,  119,  348,  347,  117,  116,  115,
 /*  1490 */   114,  112,  182,  457,  320,   22,  433,  432,  448,   19,
 /*  1500 */   609,  431,  428,   62,  193,  596,  573,  298,  555,  552,
 /*  1510 */   571,  404,  290,  380,  498,  510,  495,  306,  281,  499,
 /*  1520 */   250,    5,  497,  460,  345,  447,  569,  550,  238,  299,
 /*  1530 */   527,  525,  508,  961,  502,  501,  961,  401,  961,  211,
 /*  1540 */   490,  356,  256,  961,  483,  961,  961,  961,  961,  961,
 /*  1550 */   961,  961,  961,  961,  961,  961,  370,
};
static const YYCODETYPE yy_lookahead[] = {
 /*     0 */    19,  142,  143,  144,  145,   24,    1,   26,   77,   78,
 /*    10 */    79,   80,   81,   82,   83,   84,   85,   86,   87,   88,
 /*    20 */    89,   90,   91,   92,   15,   98,   26,   27,    7,    8,
 /*    30 */    49,   50,   77,   78,   79,   80,  109,   82,   83,   84,
 /*    40 */    85,   86,   87,   88,   89,   90,   91,   92,   22,   68,
 /*    50 */    69,   70,   71,   72,   73,   74,   75,   76,   77,   78,
 /*    60 */    79,   80,   23,   82,   83,   84,   85,   86,   87,   88,
 /*    70 */    89,   90,   91,   92,   19,   94,  112,   19,  114,  115,
 /*    80 */    25,   82,   83,   84,   85,   86,   87,   88,   89,   90,
 /*    90 */    91,   92,   19,   22,   94,   95,   96,  150,  150,   99,
 /*   100 */   100,  101,   76,  150,   49,   50,  105,  106,  107,   54,
 /*   110 */   110,  158,  165,  165,  161,  162,   26,   27,  165,  113,
 /*   120 */    16,  174,  175,   68,   69,   70,   71,   72,   73,   74,
 /*   130 */    75,   76,   77,   78,   79,   80,  118,   82,   83,   84,
 /*   140 */    85,   86,   87,   88,   89,   90,   91,   92,   19,   23,
 /*   150 */    92,   97,   98,   24,   96,   97,   98,   99,  100,  101,
 /*   160 */   102,   25,   97,  216,   60,   92,   62,  109,  221,   25,
 /*   170 */    97,   98,   99,  100,  101,  102,   86,   87,   49,   50,
 /*   180 */   118,  116,  109,   25,   94,   95,   32,   97,   88,   89,
 /*   190 */    90,   91,   92,  128,  104,   41,  106,   68,   69,   70,
 /*   200 */    71,   72,   73,   74,   75,   76,   77,   78,   79,   80,
 /*   210 */    11,   82,   83,   84,   85,   86,   87,   88,   89,   90,
 /*   220 */    91,   92,   19,   19,   86,   87,   88,   89,   90,   91,
 /*   230 */    92,   27,   96,  150,   66,   99,  100,  101,  112,  150,
 /*   240 */   114,  115,  138,  150,  161,  162,  110,  103,  165,  222,
 /*   250 */   223,  224,   49,   50,  165,   22,   57,   24,  165,  170,
 /*   260 */   171,  118,   94,  170,  171,   23,   98,   25,  185,  186,
 /*   270 */   243,   68,   69,   70,   71,   72,   73,   74,   75,   76,
 /*   280 */    77,   78,   79,   80,  126,   82,   83,   84,   85,   86,
 /*   290 */    87,   88,   89,   90,   91,   92,   19,  129,  130,  131,
 /*   300 */    88,   23,  172,  173,  105,  106,  107,  150,   22,   26,
 /*   310 */    27,  181,   26,   27,   22,  116,   26,   27,   26,  230,
 /*   320 */   231,  197,  165,  230,  231,  113,   49,   50,  204,  117,
 /*   330 */    96,  174,  175,   99,  100,  101,   22,   26,   27,  136,
 /*   340 */    26,   27,  118,   16,  110,   68,   69,   70,   71,   72,
 /*   350 */    73,   74,   75,   76,   77,   78,   79,   80,  118,   82,
 /*   360 */    83,   84,   85,   86,   87,   88,   89,   90,   91,   92,
 /*   370 */    19,  214,  215,  150,   23,   23,  155,   94,   95,   22,
 /*   380 */    94,   95,  116,  160,   94,   95,   94,   60,  165,   62,
 /*   390 */   112,   26,  114,  115,  128,   23,   36,  174,  175,   88,
 /*   400 */    49,   50,   57,  120,   22,   94,   95,   23,   94,   95,
 /*   410 */   120,   51,   25,  136,  169,  170,  171,  194,   58,   68,
 /*   420 */    69,   70,   71,   72,   73,   74,   75,   76,   77,   78,
 /*   430 */    79,   80,   23,   82,   83,   84,   85,   86,   87,   88,
 /*   440 */    89,   90,   91,   92,   19,  150,   12,   12,   23,  228,
 /*   450 */   105,  106,  107,   23,  233,   25,  165,   19,  150,   94,
 /*   460 */   165,  116,   28,   28,  112,  174,  114,  115,  108,  174,
 /*   470 */   175,   26,   27,  165,   49,   50,  231,   11,   44,   44,
 /*   480 */    46,   46,  174,  175,  112,  160,  114,  115,   50,   22,
 /*   490 */    23,   57,   25,   68,   69,   70,   71,   72,   73,   74,
 /*   500 */    75,   76,   77,   78,   79,   80,  119,   82,   83,   84,
 /*   510 */    85,   86,   87,   88,   89,   90,   91,   92,   19,  194,
 /*   520 */   225,   23,   23,  215,   19,   95,  105,  106,  107,  150,
 /*   530 */    23,  150,   27,   23,   67,   25,  150,  206,  207,   94,
 /*   540 */    95,  166,  104,  218,  165,   22,  165,  109,   49,   50,
 /*   550 */   120,  165,   25,  174,  175,  174,  175,   23,   21,  234,
 /*   560 */   174,  175,   22,   23,  239,   25,   25,   68,   69,   70,
 /*   570 */    71,   72,   73,   74,   75,   76,   77,   78,   79,   80,
 /*   580 */   205,   82,   83,   84,   85,   86,   87,   88,   89,   90,
 /*   590 */    91,   92,   19,   22,   23,  216,   23,  222,  223,  224,
 /*   600 */    63,  220,   35,  150,  150,  163,  220,   67,  166,  167,
 /*   610 */   168,  150,  169,  170,  171,  161,  162,   25,  165,  165,
 /*   620 */   150,  113,   49,   50,   25,  117,  165,  174,  175,   35,
 /*   630 */     7,    8,    9,  160,  160,  165,  120,  100,   67,  247,
 /*   640 */   248,   68,   69,   70,   71,   72,   73,   74,   75,   76,
 /*   650 */    77,   78,   79,   80,  193,   82,   83,   84,   85,   86,
 /*   660 */    87,   88,   89,   90,   91,   92,   19,  194,  194,  150,
 /*   670 */   135,   24,  137,   35,  231,  138,  150,  129,  130,  206,
 /*   680 */   207,   30,   27,  213,  165,   34,  118,   95,    0,    1,
 /*   690 */     2,  165,  218,  174,  175,   50,   49,   50,   22,   48,
 /*   700 */   174,  175,   22,   23,   23,  244,  222,  223,  224,  166,
 /*   710 */   167,  168,  120,  239,   23,   68,   69,   70,   71,   72,
 /*   720 */    73,   74,   75,   76,   77,   78,   79,   80,  150,   82,
 /*   730 */    83,   84,   85,   86,   87,   88,   89,   90,   91,   92,
 /*   740 */    19,  150,  173,  165,  181,  182,   24,   67,   26,  104,
 /*   750 */   181,  188,  174,  175,  150,   39,  165,  150,   52,  150,
 /*   760 */   150,  150,  150,  144,  145,  174,  175,  249,  250,  165,
 /*   770 */    49,   50,  165,   52,  165,  165,  165,  165,  174,  175,
 /*   780 */    29,  174,  175,  174,  175,  174,  175,  160,   22,   68,
 /*   790 */    69,   70,   71,   72,   73,   74,   75,   76,   77,   78,
 /*   800 */    79,   80,  150,   82,   83,   84,   85,   86,   87,   88,
 /*   810 */    89,   90,   91,   92,   19,  150,   94,  165,  150,  150,
 /*   820 */   160,  194,  150,  213,  160,   52,  174,  175,   23,   23,
 /*   830 */   165,   25,   22,  165,  165,  150,  150,  165,   52,  174,
 /*   840 */   175,   22,  174,  175,   49,   50,  174,  175,  190,  191,
 /*   850 */   165,  165,  240,   23,  194,   25,  187,  109,  194,  174,
 /*   860 */   175,  190,  191,   68,   69,   70,   71,   72,   73,   74,
 /*   870 */    75,   76,   77,   78,   79,   80,  150,   82,   83,   84,
 /*   880 */    85,   86,   87,   88,   89,   90,   91,   92,   19,  150,
 /*   890 */    22,  165,  150,   23,  150,   25,  150,  166,   91,   92,
 /*   900 */   174,  175,   22,  217,  165,  150,  102,  165,  150,  165,
 /*   910 */   150,  165,  150,  174,  175,   19,  174,  175,   49,   50,
 /*   920 */   165,   86,   87,  165,   23,  165,   25,  165,   24,  174,
 /*   930 */   175,  187,  174,  175,  174,  175,  205,   68,   69,   70,
 /*   940 */    71,   72,   73,   74,   75,   76,   77,   78,   79,   80,
 /*   950 */   150,   82,   83,   84,   85,   86,   87,   88,   89,   90,
 /*   960 */    91,   92,   19,  150,  150,  165,  150,  150,  166,   23,
 /*   970 */   150,   25,  160,   20,  174,  175,    1,    2,  165,  165,
 /*   980 */   104,  165,  165,   43,  150,  165,  240,  150,   49,   50,
 /*   990 */   174,  175,   49,   50,   23,   23,   25,   25,   53,  165,
 /*  1000 */   187,  187,  165,   23,  187,   25,  194,  205,  174,  175,
 /*  1010 */    71,   72,   69,   70,   71,   72,   73,   74,   75,   76,
 /*  1020 */    77,   78,   79,   80,  150,   82,   83,   84,   85,   86,
 /*  1030 */    87,   88,   89,   90,   91,   92,   19,   98,  150,  165,
 /*  1040 */   150,  160,  150,   59,   25,   53,  104,   22,  174,  175,
 /*  1050 */   213,  138,    5,  165,    1,  165,  150,  165,  150,  150,
 /*  1060 */   240,  150,  174,  175,  174,  175,   49,   50,  118,  150,
 /*  1070 */    35,  165,   27,  165,  165,  194,  165,  108,  127,   76,
 /*  1080 */   174,  175,  174,  175,  165,  174,  175,   70,   71,   72,
 /*  1090 */    73,   74,   75,   76,   77,   78,   79,   80,  166,   82,
 /*  1100 */    83,   84,   85,   86,   87,   88,   89,   90,   91,   92,
 /*  1110 */    19,   20,  193,   22,  150,  150,  150,   26,   27,   76,
 /*  1120 */   150,   22,    1,  150,  119,  121,  217,   20,   37,  165,
 /*  1130 */   165,  165,   16,   19,   20,  165,   22,  205,  165,  119,
 /*  1140 */    26,   27,  108,  128,  150,  150,  150,   56,  150,   22,
 /*  1150 */   150,   37,  150,  127,  160,   23,  150,   66,  193,  165,
 /*  1160 */   165,  165,   16,  165,   23,  165,  150,  165,  174,  175,
 /*  1170 */    56,  165,  150,   65,  174,  175,   15,   86,   87,   88,
 /*  1180 */    66,  165,  140,  150,   93,   94,   95,  165,  194,   98,
 /*  1190 */   174,  175,   22,    3,  164,  193,  174,  175,  165,  150,
 /*  1200 */    86,   87,    4,  180,  150,  248,  251,   93,   94,   95,
 /*  1210 */   216,  180,   98,  251,  165,  221,  150,  149,    6,  165,
 /*  1220 */   129,  130,  131,  132,  133,  134,  193,  150,  174,  175,
 /*  1230 */   116,  165,   19,   20,  150,   22,  149,  151,  150,   26,
 /*  1240 */    27,  149,  165,  129,  130,  131,  132,  133,  134,  165,
 /*  1250 */    37,  174,  175,  165,  149,   19,   20,   13,   22,  150,
 /*  1260 */   150,  150,   26,   27,  146,  147,  151,  150,   25,   56,
 /*  1270 */   152,  159,  154,   37,  165,  165,  165,  193,  160,   66,
 /*  1280 */   116,  193,  165,  174,  175,  174,  175,  194,  199,  150,
 /*  1290 */   200,  126,   56,  124,  123,  150,  201,  122,  150,   86,
 /*  1300 */    87,  150,   66,  193,  165,  202,   93,   94,   95,  150,
 /*  1310 */   165,   98,  194,  165,  125,   22,  165,  150,  150,   26,
 /*  1320 */    27,  135,   86,   87,  165,  174,  175,  203,  226,   93,
 /*  1330 */    94,   95,  165,  165,   98,  150,  218,  150,  193,  157,
 /*  1340 */   118,  157,  129,  130,  131,  132,  133,  134,    5,  104,
 /*  1350 */   165,  211,  165,   10,   11,   12,   13,   14,  150,   66,
 /*  1360 */    17,  174,  175,  210,  246,  129,  130,  131,  132,  133,
 /*  1370 */   134,  150,  210,  165,   31,  121,   33,  150,  150,   86,
 /*  1380 */    87,  176,  174,  175,  150,   42,  165,   94,  211,  210,
 /*  1390 */   150,   98,  165,  165,  211,  174,  175,  150,   55,  165,
 /*  1400 */    57,  150,  174,  175,   61,  165,  150,   64,  174,  175,
 /*  1410 */   150,  150,  165,  150,  174,  175,  165,  104,  150,  184,
 /*  1420 */   150,  165,  129,  130,  131,  165,  165,  150,  165,  150,
 /*  1430 */   150,  176,  150,  165,   47,  165,  150,  150,  176,  103,
 /*  1440 */   150,   22,  165,  178,  165,  165,  179,  165,  105,  106,
 /*  1450 */   107,  165,  165,  229,  111,  165,   92,  176,  229,  116,
 /*  1460 */   184,  176,  179,  156,  176,  176,   18,  157,  156,  237,
 /*  1470 */    45,  157,  156,  135,  157,  157,  238,  156,   68,  157,
 /*  1480 */   189,  189,  139,  219,   22,  157,   18,  192,  192,  192,
 /*  1490 */   192,  189,  219,  199,  157,  242,   40,  157,  199,  242,
 /*  1500 */   153,  157,   38,  245,  196,  166,  232,  198,  177,  177,
 /*  1510 */   232,  227,  209,  178,  166,  182,  166,  148,  177,  177,
 /*  1520 */   209,  196,  177,  199,  209,  199,  166,  208,   92,  195,
 /*  1530 */   174,  174,  183,  252,  183,  183,  252,  191,  252,  235,
 /*  1540 */   186,  241,  241,  252,  186,  252,  252,  252,  252,  252,
 /*  1550 */   252,  252,  252,  252,  252,  252,  236,
};
#define YY_SHIFT_USE_DFLT (-74)
#define YY_SHIFT_COUNT (418)
#define YY_SHIFT_MIN   (-73)
#define YY_SHIFT_MAX   (1468)
static const short yy_shift_ofst[] = {
 /*     0 */   975, 1114, 1343, 1114, 1213, 1213,   90,   90,    0,  -19,
 /*    10 */  1213, 1213, 1213, 1213, 1213,  345,  445,  721, 1091, 1213,
 /*    20 */  1213, 1213, 1213, 1213, 1213, 1213, 1213, 1213, 1213, 1213,
 /*    30 */  1213, 1213, 1213, 1213, 1213, 1213, 1213, 1213, 1213, 1213,
 /*    40 */  1213, 1213, 1213, 1213, 1213, 1213, 1213, 1236, 1213, 1213,
 /*    50 */  1213, 1213, 1213, 1213, 1213, 1213, 1213, 1213, 1213, 1213,
 /*    60 */  1213,  199,  445,  445,  835,  835,  365, 1164,   55,  647,
 /*    70 */   573,  499,  425,  351,  277,  203,  129,  795,  795,  795,
 /*    80 */   795,  795,  795,  795,  795,  795,  795,  795,  795,  795,
 /*    90 */   795,  795,  795,  795,  795,  869,  795,  943, 1017, 1017,
 /*   100 */   -69,  -45,  -45,  -45,  -45,  -45,   -1,   58,  138,  100,
 /*   110 */   445,  445,  445,  445,  445,  445,  445,  445,  445,  445,
 /*   120 */   445,  445,  445,  445,  445,  445,  537,  438,  445,  445,
 /*   130 */   445,  445,  445,  365,  807, 1436,  -74,  -74,  -74, 1293,
 /*   140 */    73,  434,  434,  311,  314,  290,  283,  286,  540,  467,
 /*   150 */   445,  445,  445,  445,  445,  445,  445,  445,  445,  445,
 /*   160 */   445,  445,  445,  445,  445,  445,  445,  445,  445,  445,
 /*   170 */   445,  445,  445,  445,  445,  445,  445,  445,  445,  445,
 /*   180 */   445,  445,   65,  722,  722,  722,  688,  266, 1164, 1164,
 /*   190 */  1164,  -74,  -74,  -74,  136,  168,  168,  234,  360,  360,
 /*   200 */   360,  430,  372,  435,  352,  278,  126,  -36,  -36,  -36,
 /*   210 */   -36,  421,  651,  -36,  -36,  592,  292,  212,  623,  158,
 /*   220 */   204,  204,  505,  158,  505,  144,  365,  154,  365,  154,
 /*   230 */   645,  154,  204,  154,  154,  535,  548,  548,  365,  387,
 /*   240 */   508,  233, 1464, 1222, 1222, 1456, 1456, 1222, 1462, 1410,
 /*   250 */  1165, 1468, 1468, 1468, 1468, 1222, 1165, 1462, 1410, 1410,
 /*   260 */  1222, 1448, 1338, 1425, 1222, 1222, 1448, 1222, 1448, 1222,
 /*   270 */  1448, 1419, 1313, 1313, 1313, 1387, 1364, 1364, 1419, 1313,
 /*   280 */  1336, 1313, 1387, 1313, 1313, 1254, 1245, 1254, 1245, 1254,
 /*   290 */  1245, 1222, 1222, 1186, 1189, 1175, 1169, 1171, 1165, 1164,
 /*   300 */  1243, 1244, 1244, 1212, 1212, 1212, 1212,  -74,  -74,  -74,
 /*   310 */   -74,  -74,  -74,  939,  104,  680,  571,  327,    1,  980,
 /*   320 */    26,  972,  971,  946,  901,  870,  830,  806,   54,   21,
 /*   330 */   -73,  510,  242, 1198, 1190, 1170, 1042, 1161, 1108, 1146,
 /*   340 */  1141, 1132, 1015, 1127, 1026, 1034, 1020, 1107, 1004, 1116,
 /*   350 */  1121, 1005, 1099,  951, 1043, 1003,  969, 1045, 1035,  950,
 /*   360 */  1053, 1047, 1025,  942,  913,  992, 1019,  945,  984,  940,
 /*   370 */   876,  904,  953,  896,  748,  804,  880,  786,  868,  819,
 /*   380 */   805,  810,  773,  751,  766,  706,  716,  691,  681,  568,
 /*   390 */   655,  638,  676,  516,  541,  594,  599,  567,  541,  534,
 /*   400 */   507,  527,  498,  523,  466,  382,  409,  384,  357,    6,
 /*   410 */   240,  224,  143,   62,   18,   71,   39,    9,    5,
};
#define YY_REDUCE_USE_DFLT (-142)
#define YY_REDUCE_COUNT (312)
#define YY_REDUCE_MIN   (-141)
#define YY_REDUCE_MAX   (1369)
static const short yy_reduce_ofst[] = {
 /*     0 */  -141,  994, 1118,  223,  157,  -53,   93,   89,   83,  375,
 /*    10 */   386,  381,  379,  308,  295,  325,  -47,   27, 1240, 1234,
 /*    20 */  1228, 1221, 1208, 1187, 1151, 1111, 1109, 1077, 1054, 1022,
 /*    30 */  1016, 1000,  911,  908,  906,  890,  888,  874,  834,  816,
 /*    40 */   800,  760,  758,  755,  742,  739,  726,  685,  672,  668,
 /*    50 */   665,  652,  611,  609,  607,  604,  591,  578,  526,  519,
 /*    60 */   453,  474,  454,  461,  443,  245,  442,  473,  484,  484,
 /*    70 */   484,  484,  484,  484,  484,  484,  484,  484,  484,  484,
 /*    80 */   484,  484,  484,  484,  484,  484,  484,  484,  484,  484,
 /*    90 */   484,  484,  484,  484,  484,  484,  484,  484,  484,  484,
 /*   100 */   484,  484,  484,  484,  484,  484,  484,  130,  484,  484,
 /*   110 */  1145,  909, 1110, 1088, 1084, 1033, 1002,  965,  820,  837,
 /*   120 */   746,  686,  612,  817,  610,  919,  221,  563,  814,  813,
 /*   130 */   744,  669,  470,  543,  484,  484,  484,  484,  484,  291,
 /*   140 */   569,  671,  658,  970, 1290, 1287, 1286, 1282,  518,  518,
 /*   150 */  1280, 1279, 1277, 1270, 1268, 1263, 1261, 1260, 1256, 1251,
 /*   160 */  1247, 1227, 1185, 1168, 1167, 1159, 1148, 1139, 1117, 1066,
 /*   170 */  1049, 1006,  998,  996,  995,  973,  970,  966,  964,  892,
 /*   180 */   762,  -52,  881,  932,  802,  731,  619,  812,  664,  660,
 /*   190 */   627,  392,  331,  124, 1358, 1357, 1356, 1354, 1352, 1351,
 /*   200 */  1349, 1319, 1334, 1346, 1334, 1334, 1334, 1334, 1334, 1334,
 /*   210 */  1334, 1320, 1304, 1334, 1334, 1319, 1360, 1325, 1369, 1326,
 /*   220 */  1315, 1311, 1301, 1324, 1300, 1335, 1350, 1345, 1348, 1342,
 /*   230 */  1333, 1341, 1303, 1332, 1331, 1284, 1278, 1274, 1339, 1309,
 /*   240 */  1308, 1347, 1258, 1344, 1340, 1257, 1253, 1337, 1273, 1302,
 /*   250 */  1299, 1298, 1297, 1296, 1295, 1328, 1294, 1264, 1292, 1291,
 /*   260 */  1322, 1321, 1238, 1232, 1318, 1317, 1316, 1314, 1312, 1310,
 /*   270 */  1307, 1283, 1289, 1288, 1285, 1276, 1229, 1224, 1267, 1281,
 /*   280 */  1265, 1262, 1235, 1255, 1205, 1183, 1179, 1177, 1162, 1140,
 /*   290 */  1153, 1184, 1182, 1102, 1124, 1103, 1095, 1090, 1089, 1093,
 /*   300 */  1112, 1115, 1086, 1105, 1092, 1087, 1068,  962,  955,  957,
 /*   310 */  1031, 1023, 1030,
};
static const YYACTIONTYPE yy_default[] = {
 /*     0 */   635,  870,  959,  959,  959,  870,  899,  899,  959,  759,
 /*    10 */   959,  959,  959,  959,  868,  959,  959,  933,  959,  959,
 /*    20 */   959,  959,  959,  959,  959,  959,  959,  959,  959,  959,
 /*    30 */   959,  959,  959,  959,  959,  959,  959,  959,  959,  959,
 /*    40 */   959,  959,  959,  959,  959,  959,  959,  959,  959,  959,
 /*    50 */   959,  959,  959,  959,  959,  959,  959,  959,  959,  959,
 /*    60 */   959,  959,  959,  959,  899,  899,  674,  763,  794,  959,
 /*    70 */   959,  959,  959,  959,  959,  959,  959,  932,  934,  809,
 /*    80 */   808,  802,  801,  912,  774,  799,  792,  785,  796,  871,
 /*    90 */   864,  865,  863,  867,  872,  959,  795,  831,  848,  830,
 /*   100 */   842,  847,  854,  846,  843,  833,  832,  666,  834,  835,
 /*   110 */   959,  959,  959,  959,  959,  959,  959,  959,  959,  959,
 /*   120 */   959,  959,  959,  959,  959,  959,  661,  728,  959,  959,
 /*   130 */   959,  959,  959,  959,  836,  837,  851,  850,  849,  959,
 /*   140 */   959,  959,  959,  959,  959,  959,  959,  959,  959,  959,
 /*   150 */   959,  939,  937,  959,  883,  959,  959,  959,  959,  959,
 /*   160 */   959,  959,  959,  959,  959,  959,  959,  959,  959,  959,
 /*   170 */   959,  959,  959,  959,  959,  959,  959,  959,  959,  959,
 /*   180 */   959,  641,  959,  759,  759,  759,  635,  959,  959,  959,
 /*   190 */   959,  951,  763,  753,  719,  959,  959,  959,  959,  959,
 /*   200 */   959,  959,  959,  959,  959,  959,  959,  804,  742,  922,
 /*   210 */   924,  959,  905,  740,  663,  761,  676,  751,  643,  798,
 /*   220 */   776,  776,  917,  798,  917,  700,  959,  788,  959,  788,
 /*   230 */   697,  788,  776,  788,  788,  866,  959,  959,  959,  760,
 /*   240 */   751,  959,  944,  767,  767,  936,  936,  767,  810,  732,
 /*   250 */   798,  739,  739,  739,  739,  767,  798,  810,  732,  732,
 /*   260 */   767,  658,  911,  909,  767,  767,  658,  767,  658,  767,
 /*   270 */   658,  876,  730,  730,  730,  715,  880,  880,  876,  730,
 /*   280 */   700,  730,  715,  730,  730,  780,  775,  780,  775,  780,
 /*   290 */   775,  767,  767,  959,  793,  781,  791,  789,  798,  959,
 /*   300 */   718,  651,  651,  640,  640,  640,  640,  956,  956,  951,
 /*   310 */   702,  702,  684,  959,  959,  959,  959,  959,  959,  959,
 /*   320 */   885,  959,  959,  959,  959,  959,  959,  959,  959,  959,
 /*   330 */   959,  959,  959,  959,  636,  946,  959,  959,  943,  959,
 /*   340 */   959,  959,  959,  959,  959,  959,  959,  959,  959,  959,
 /*   350 */   959,  959,  959,  959,  959,  959,  959,  959,  959,  915,
 /*   360 */   959,  959,  959,  959,  959,  959,  908,  907,  959,  959,
 /*   370 */   959,  959,  959,  959,  959,  959,  959,  959,  959,  959,
 /*   380 */   959,  959,  959,  959,  959,  959,  959,  959,  959,  959,
 /*   390 */   959,  959,  959,  959,  790,  959,  782,  959,  869,  959,
 /*   400 */   959,  959,  959,  959,  959,  959,  959,  959,  959,  745,
 /*   410 */   819,  959,  818,  822,  817,  668,  959,  649,  959,  632,
 /*   420 */   637,  955,  958,  957,  954,  953,  952,  947,  945,  942,
 /*   430 */   941,  940,  938,  935,  931,  889,  887,  894,  893,  892,
 /*   440 */   891,  890,  888,  886,  884,  805,  803,  800,  797,  930,
 /*   450 */   882,  741,  738,  737,  657,  948,  914,  923,  921,  811,
 /*   460 */   920,  919,  918,  916,  913,  900,  807,  806,  733,  874,
 /*   470 */   873,  660,  904,  903,  902,  906,  910,  901,  769,  659,
 /*   480 */   656,  665,  722,  721,  729,  727,  726,  725,  724,  723,
 /*   490 */   720,  667,  675,  686,  714,  699,  698,  879,  881,  878,
 /*   500 */   877,  707,  706,  712,  711,  710,  709,  708,  705,  704,
 /*   510 */   703,  696,  695,  701,  694,  717,  716,  713,  693,  736,
 /*   520 */   735,  734,  731,  692,  691,  690,  822,  689,  688,  828,
 /*   530 */   827,  815,  858,  756,  755,  754,  766,  765,  778,  777,
 /*   540 */   813,  812,  779,  764,  758,  757,  773,  772,  771,  770,
 /*   550 */   762,  752,  784,  787,  786,  783,  860,  768,  857,  929,
 /*   560 */   928,  927,  926,  925,  862,  861,  829,  826,  679,  680,
 /*   570 */   898,  896,  897,  895,  682,  681,  678,  677,  859,  747,
 /*   580 */   746,  855,  852,  844,  840,  856,  853,  845,  841,  839,
 /*   590 */   838,  824,  823,  821,  820,  816,  825,  670,  748,  744,
 /*   600 */   743,  814,  750,  749,  687,  685,  683,  664,  662,  655,
 /*   610 */   653,  652,  654,  650,  648,  647,  646,  645,  644,  673,
 /*   620 */   672,  671,  669,  668,  642,  639,  638,  634,  633,  631,
};

/* The next table maps tokens into fallback tokens.  If a construct
** like the following:
** 
**      %fallback ID X Y Z.
**
** appears in the grammar, then ID becomes a fallback token for X, Y,
** and Z.  Whenever one of the tokens X, Y, or Z is input to the parser
** but it does not parse, the type of the token is changed to ID and
** the parse is retried before an error is thrown.
*/
#ifdef YYFALLBACK
static const YYCODETYPE yyFallback[] = {
    0,  /*          $ => nothing */
    0,  /*       SEMI => nothing */
   26,  /*    EXPLAIN => ID */
   26,  /*      QUERY => ID */
   26,  /*       PLAN => ID */
   26,  /*      BEGIN => ID */
    0,  /* TRANSACTION => nothing */
   26,  /*   DEFERRED => ID */
   26,  /*  IMMEDIATE => ID */
   26,  /*  EXCLUSIVE => ID */
    0,  /*     COMMIT => nothing */
   26,  /*        END => ID */
   26,  /*   ROLLBACK => ID */
   26,  /*  SAVEPOINT => ID */
   26,  /*    RELEASE => ID */
    0,  /*         TO => nothing */
    0,  /*      TABLE => nothing */
    0,  /*     CREATE => nothing */
   26,  /*         IF => ID */
    0,  /*        NOT => nothing */
    0,  /*     EXISTS => nothing */
   26,  /*       TEMP => ID */
    0,  /*         LP => nothing */
    0,  /*         RP => nothing */
    0,  /*         AS => nothing */
    0,  /*      COMMA => nothing */
    0,  /*         ID => nothing */
    0,  /*    INDEXED => nothing */
   26,  /*      ABORT => ID */
   26,  /*     ACTION => ID */
   26,  /*      AFTER => ID */
   26,  /*    ANALYZE => ID */
   26,  /*        ASC => ID */
   26,  /*     ATTACH => ID */
   26,  /*     BEFORE => ID */
   26,  /*         BY => ID */
   26,  /*    CASCADE => ID */
   26,  /*       CAST => ID */
   26,  /*   COLUMNKW => ID */
   26,  /*   CONFLICT => ID */
   26,  /*   DATABASE => ID */
   26,  /*       DESC => ID */
   26,  /*     DETACH => ID */
   26,  /*       EACH => ID */
   26,  /*       FAIL => ID */
   26,  /*        FOR => ID */
   26,  /*     IGNORE => ID */
   26,  /*  INITIALLY => ID */
   26,  /*    INSTEAD => ID */
   26,  /*    LIKE_KW => ID */
   26,  /*      MATCH => ID */
   26,  /*         NO => ID */
   26,  /*        KEY => ID */
   26,  /*         OF => ID */
   26,  /*     OFFSET => ID */
   26,  /*     PRAGMA => ID */
   26,  /*      RAISE => ID */
   26,  /*    REPLACE => ID */
   26,  /*   RESTRICT => ID */
   26,  /*        ROW => ID */
   26,  /*    TRIGGER => ID */
   26,  /*     VACUUM => ID */
   26,  /*       VIEW => ID */
   26,  /*    VIRTUAL => ID */
   26,  /*    REINDEX => ID */
   26,  /*     RENAME => ID */
   26,  /*   CTIME_KW => ID */
};
#endif /* YYFALLBACK */

/* The following structure represents a single element of the
** parser's stack.  Information stored includes:
**
**   +  The state number for the parser at this level of the stack.
**
**   +  The value of the token stored at this level of the stack.
**      (In other words, the "major" token.)
**
**   +  The semantic value stored at this level of the stack.  This is
**      the information used by the action routines in the grammar.
**      It is sometimes called the "minor" token.
*/
struct yyStackEntry {
  YYACTIONTYPE stateno;  /* The state-number */
  YYCODETYPE major;      /* The major token value.  This is the code
                         ** number for the token at this stack level */
  YYMINORTYPE minor;     /* The user-supplied minor token value.  This
                         ** is the value of the token  */
};
typedef struct yyStackEntry yyStackEntry;

/* The state of the parser is completely contained in an instance of
** the following structure */
struct yyParser {
  int yyidx;                    /* Index of top element in stack */
#ifdef YYTRACKMAXSTACKDEPTH
  int yyidxMax;                 /* Maximum value of yyidx */
#endif
  int yyerrcnt;                 /* Shifts left before out of the error */
  sqlite3ParserARG_SDECL                /* A place to hold %extra_argument */
#if YYSTACKDEPTH<=0
  int yystksz;                  /* Current side of the stack */
  yyStackEntry *yystack;        /* The parser's stack */
#else
  yyStackEntry yystack[YYSTACKDEPTH];  /* The parser's stack */
#endif
};
typedef struct yyParser yyParser;

#ifndef NDEBUG
/* #include <stdio.h> */
static FILE *yyTraceFILE = 0;
static char *yyTracePrompt = 0;
#endif /* NDEBUG */

#ifndef NDEBUG
/* 
** Turn parser tracing on by giving a stream to which to write the trace
** and a prompt to preface each trace message.  Tracing is turned off
** by making either argument NULL 
**
** Inputs:
** <ul>
** <li> A FILE* to which trace output should be written.
**      If NULL, then tracing is turned off.
** <li> A prefix string written at the beginning of every
**      line of trace output.  If NULL, then tracing is
**      turned off.
** </ul>
**
** Outputs:
** None.
*/
SQLITE_PRIVATE void sqlite3ParserTrace(FILE *TraceFILE, char *zTracePrompt){
  yyTraceFILE = TraceFILE;
  yyTracePrompt = zTracePrompt;
  if( yyTraceFILE==0 ) yyTracePrompt = 0;
  else if( yyTracePrompt==0 ) yyTraceFILE = 0;
}
#endif /* NDEBUG */

#ifndef NDEBUG
/* For tracing shifts, the names of all terminals and nonterminals
** are required.  The following table supplies these names */
static const char *const yyTokenName[] = { 
  "$",             "SEMI",          "EXPLAIN",       "QUERY",       
  "PLAN",          "BEGIN",         "TRANSACTION",   "DEFERRED",    
  "IMMEDIATE",     "EXCLUSIVE",     "COMMIT",        "END",         
  "ROLLBACK",      "SAVEPOINT",     "RELEASE",       "TO",          
  "TABLE",         "CREATE",        "IF",            "NOT",         
  "EXISTS",        "TEMP",          "LP",            "RP",          
  "AS",            "COMMA",         "ID",            "INDEXED",     
  "ABORT",         "ACTION",        "AFTER",         "ANALYZE",     
  "ASC",           "ATTACH",        "BEFORE",        "BY",          
  "CASCADE",       "CAST",          "COLUMNKW",      "CONFLICT",    
  "DATABASE",      "DESC",          "DETACH",        "EACH",        
  "FAIL",          "FOR",           "IGNORE",        "INITIALLY",   
  "INSTEAD",       "LIKE_KW",       "MATCH",         "NO",          
  "KEY",           "OF",            "OFFSET",        "PRAGMA",      
  "RAISE",         "REPLACE",       "RESTRICT",      "ROW",         
  "TRIGGER",       "VACUUM",        "VIEW",          "VIRTUAL",     
  "REINDEX",       "RENAME",        "CTIME_KW",      "ANY",         
  "OR",            "AND",           "IS",            "BETWEEN",     
  "IN",            "ISNULL",        "NOTNULL",       "NE",          
  "EQ",            "GT",            "LE",            "LT",          
  "GE",            "ESCAPE",        "BITAND",        "BITOR",       
  "LSHIFT",        "RSHIFT",        "PLUS",          "MINUS",       
  "STAR",          "SLASH",         "REM",           "CONCAT",      
  "COLLATE",       "BITNOT",        "STRING",        "JOIN_KW",     
  "CONSTRAINT",    "DEFAULT",       "NULL",          "PRIMARY",     
  "UNIQUE",        "CHECK",         "REFERENCES",    "AUTOINCR",    
  "ON",            "INSERT",        "DELETE",        "UPDATE",      
  "SET",           "DEFERRABLE",    "FOREIGN",       "DROP",        
  "UNION",         "ALL",           "EXCEPT",        "INTERSECT",   
  "SELECT",        "DISTINCT",      "DOT",           "FROM",        
  "JOIN",          "USING",         "ORDER",         "GROUP",       
  "HAVING",        "LIMIT",         "WHERE",         "INTO",        
  "VALUES",        "INTEGER",       "FLOAT",         "BLOB",        
  "REGISTER",      "VARIABLE",      "CASE",          "WHEN",        
  "THEN",          "ELSE",          "INDEX",         "ALTER",       
  "ADD",           "error",         "input",         "cmdlist",     
  "ecmd",          "explain",       "cmdx",          "cmd",         
  "transtype",     "trans_opt",     "nm",            "savepoint_opt",
  "create_table",  "create_table_args",  "createkw",      "temp",        
  "ifnotexists",   "dbnm",          "columnlist",    "conslist_opt",
  "select",        "column",        "columnid",      "type",        
  "carglist",      "id",            "ids",           "typetoken",   
  "typename",      "signed",        "plus_num",      "minus_num",   
  "carg",          "ccons",         "term",          "expr",        
  "onconf",        "sortorder",     "autoinc",       "idxlist_opt", 
  "refargs",       "defer_subclause",  "refarg",        "refact",      
  "init_deferred_pred_opt",  "conslist",      "tcons",         "idxlist",     
  "defer_subclause_opt",  "orconf",        "resolvetype",   "raisetype",   
  "ifexists",      "fullname",      "oneselect",     "multiselect_op",
  "distinct",      "selcollist",    "from",          "where_opt",   
  "groupby_opt",   "having_opt",    "orderby_opt",   "limit_opt",   
  "sclp",          "as",            "seltablist",    "stl_prefix",  
  "joinop",        "indexed_opt",   "on_opt",        "using_opt",   
  "joinop2",       "inscollist",    "sortlist",      "sortitem",    
  "nexprlist",     "setlist",       "insert_cmd",    "inscollist_opt",
  "itemlist",      "exprlist",      "likeop",        "between_op",  
  "in_op",         "case_operand",  "case_exprlist",  "case_else",   
  "uniqueflag",    "collate",       "nmnum",         "plus_opt",    
  "number",        "trigger_decl",  "trigger_cmd_list",  "trigger_time",
  "trigger_event",  "foreach_clause",  "when_clause",   "trigger_cmd", 
  "trnm",          "tridxby",       "database_kw_opt",  "key_opt",     
  "add_column_fullname",  "kwcolumn_opt",  "create_vtab",   "vtabarglist", 
  "vtabarg",       "vtabargtoken",  "lp",            "anylist",     
};
#endif /* NDEBUG */

#ifndef NDEBUG
/* For tracing reduce actions, the names of all rules are required.
*/
static const char *const yyRuleName[] = {
 /*   0 */ "input ::= cmdlist",
 /*   1 */ "cmdlist ::= cmdlist ecmd",
 /*   2 */ "cmdlist ::= ecmd",
 /*   3 */ "ecmd ::= SEMI",
 /*   4 */ "ecmd ::= explain cmdx SEMI",
 /*   5 */ "explain ::=",
 /*   6 */ "explain ::= EXPLAIN",
 /*   7 */ "explain ::= EXPLAIN QUERY PLAN",
 /*   8 */ "cmdx ::= cmd",
 /*   9 */ "cmd ::= BEGIN transtype trans_opt",
 /*  10 */ "trans_opt ::=",
 /*  11 */ "trans_opt ::= TRANSACTION",
 /*  12 */ "trans_opt ::= TRANSACTION nm",
 /*  13 */ "transtype ::=",
 /*  14 */ "transtype ::= DEFERRED",
 /*  15 */ "transtype ::= IMMEDIATE",
 /*  16 */ "transtype ::= EXCLUSIVE",
 /*  17 */ "cmd ::= COMMIT trans_opt",
 /*  18 */ "cmd ::= END trans_opt",
 /*  19 */ "cmd ::= ROLLBACK trans_opt",
 /*  20 */ "savepoint_opt ::= SAVEPOINT",
 /*  21 */ "savepoint_opt ::=",
 /*  22 */ "cmd ::= SAVEPOINT nm",
 /*  23 */ "cmd ::= RELEASE savepoint_opt nm",
 /*  24 */ "cmd ::= ROLLBACK trans_opt TO savepoint_opt nm",
 /*  25 */ "cmd ::= create_table create_table_args",
 /*  26 */ "create_table ::= createkw temp TABLE ifnotexists nm dbnm",
 /*  27 */ "createkw ::= CREATE",
 /*  28 */ "ifnotexists ::=",
 /*  29 */ "ifnotexists ::= IF NOT EXISTS",
 /*  30 */ "temp ::= TEMP",
 /*  31 */ "temp ::=",
 /*  32 */ "create_table_args ::= LP columnlist conslist_opt RP",
 /*  33 */ "create_table_args ::= AS select",
 /*  34 */ "columnlist ::= columnlist COMMA column",
 /*  35 */ "columnlist ::= column",
 /*  36 */ "column ::= columnid type carglist",
 /*  37 */ "columnid ::= nm",
 /*  38 */ "id ::= ID",
 /*  39 */ "id ::= INDEXED",
 /*  40 */ "ids ::= ID|STRING",
 /*  41 */ "nm ::= id",
 /*  42 */ "nm ::= STRING",
 /*  43 */ "nm ::= JOIN_KW",
 /*  44 */ "type ::=",
 /*  45 */ "type ::= typetoken",
 /*  46 */ "typetoken ::= typename",
 /*  47 */ "typetoken ::= typename LP signed RP",
 /*  48 */ "typetoken ::= typename LP signed COMMA signed RP",
 /*  49 */ "typename ::= ids",
 /*  50 */ "typename ::= typename ids",
 /*  51 */ "signed ::= plus_num",
 /*  52 */ "signed ::= minus_num",
 /*  53 */ "carglist ::= carglist carg",
 /*  54 */ "carglist ::=",
 /*  55 */ "carg ::= CONSTRAINT nm ccons",
 /*  56 */ "carg ::= ccons",
 /*  57 */ "ccons ::= DEFAULT term",
 /*  58 */ "ccons ::= DEFAULT LP expr RP",
 /*  59 */ "ccons ::= DEFAULT PLUS term",
 /*  60 */ "ccons ::= DEFAULT MINUS term",
 /*  61 */ "ccons ::= DEFAULT id",
 /*  62 */ "ccons ::= NULL onconf",
 /*  63 */ "ccons ::= NOT NULL onconf",
 /*  64 */ "ccons ::= PRIMARY KEY sortorder onconf autoinc",
 /*  65 */ "ccons ::= UNIQUE onconf",
 /*  66 */ "ccons ::= CHECK LP expr RP",
 /*  67 */ "ccons ::= REFERENCES nm idxlist_opt refargs",
 /*  68 */ "ccons ::= defer_subclause",
 /*  69 */ "ccons ::= COLLATE ids",
 /*  70 */ "autoinc ::=",
 /*  71 */ "autoinc ::= AUTOINCR",
 /*  72 */ "refargs ::=",
 /*  73 */ "refargs ::= refargs refarg",
 /*  74 */ "refarg ::= MATCH nm",
 /*  75 */ "refarg ::= ON INSERT refact",
 /*  76 */ "refarg ::= ON DELETE refact",
 /*  77 */ "refarg ::= ON UPDATE refact",
 /*  78 */ "refact ::= SET NULL",
 /*  79 */ "refact ::= SET DEFAULT",
 /*  80 */ "refact ::= CASCADE",
 /*  81 */ "refact ::= RESTRICT",
 /*  82 */ "refact ::= NO ACTION",
 /*  83 */ "defer_subclause ::= NOT DEFERRABLE init_deferred_pred_opt",
 /*  84 */ "defer_subclause ::= DEFERRABLE init_deferred_pred_opt",
 /*  85 */ "init_deferred_pred_opt ::=",
 /*  86 */ "init_deferred_pred_opt ::= INITIALLY DEFERRED",
 /*  87 */ "init_deferred_pred_opt ::= INITIALLY IMMEDIATE",
 /*  88 */ "conslist_opt ::=",
 /*  89 */ "conslist_opt ::= COMMA conslist",
 /*  90 */ "conslist ::= conslist COMMA tcons",
 /*  91 */ "conslist ::= conslist tcons",
 /*  92 */ "conslist ::= tcons",
 /*  93 */ "tcons ::= CONSTRAINT nm",
 /*  94 */ "tcons ::= PRIMARY KEY LP idxlist autoinc RP onconf",
 /*  95 */ "tcons ::= UNIQUE LP idxlist RP onconf",
 /*  96 */ "tcons ::= CHECK LP expr RP onconf",
 /*  97 */ "tcons ::= FOREIGN KEY LP idxlist RP REFERENCES nm idxlist_opt refargs defer_subclause_opt",
 /*  98 */ "defer_subclause_opt ::=",
 /*  99 */ "defer_subclause_opt ::= defer_subclause",
 /* 100 */ "onconf ::=",
 /* 101 */ "onconf ::= ON CONFLICT resolvetype",
 /* 102 */ "orconf ::=",
 /* 103 */ "orconf ::= OR resolvetype",
 /* 104 */ "resolvetype ::= raisetype",
 /* 105 */ "resolvetype ::= IGNORE",
 /* 106 */ "resolvetype ::= REPLACE",
 /* 107 */ "cmd ::= DROP TABLE ifexists fullname",
 /* 108 */ "ifexists ::= IF EXISTS",
 /* 109 */ "ifexists ::=",
 /* 110 */ "cmd ::= createkw temp VIEW ifnotexists nm dbnm AS select",
 /* 111 */ "cmd ::= DROP VIEW ifexists fullname",
 /* 112 */ "cmd ::= select",
 /* 113 */ "select ::= oneselect",
 /* 114 */ "select ::= select multiselect_op oneselect",
 /* 115 */ "multiselect_op ::= UNION",
 /* 116 */ "multiselect_op ::= UNION ALL",
 /* 117 */ "multiselect_op ::= EXCEPT|INTERSECT",
 /* 118 */ "oneselect ::= SELECT distinct selcollist from where_opt groupby_opt having_opt orderby_opt limit_opt",
 /* 119 */ "distinct ::= DISTINCT",
 /* 120 */ "distinct ::= ALL",
 /* 121 */ "distinct ::=",
 /* 122 */ "sclp ::= selcollist COMMA",
 /* 123 */ "sclp ::=",
 /* 124 */ "selcollist ::= sclp expr as",
 /* 125 */ "selcollist ::= sclp STAR",
 /* 126 */ "selcollist ::= sclp nm DOT STAR",
 /* 127 */ "as ::= AS nm",
 /* 128 */ "as ::= ids",
 /* 129 */ "as ::=",
 /* 130 */ "from ::=",
 /* 131 */ "from ::= FROM seltablist",
 /* 132 */ "stl_prefix ::= seltablist joinop",
 /* 133 */ "stl_prefix ::=",
 /* 134 */ "seltablist ::= stl_prefix nm dbnm as indexed_opt on_opt using_opt",
 /* 135 */ "seltablist ::= stl_prefix LP select RP as on_opt using_opt",
 /* 136 */ "seltablist ::= stl_prefix LP seltablist RP as on_opt using_opt",
 /* 137 */ "dbnm ::=",
 /* 138 */ "dbnm ::= DOT nm",
 /* 139 */ "fullname ::= nm dbnm",
 /* 140 */ "joinop ::= COMMA|JOIN",
 /* 141 */ "joinop ::= JOIN_KW JOIN",
 /* 142 */ "joinop ::= JOIN_KW nm JOIN",
 /* 143 */ "joinop ::= JOIN_KW nm nm JOIN",
 /* 144 */ "on_opt ::= ON expr",
 /* 145 */ "on_opt ::=",
 /* 146 */ "indexed_opt ::=",
 /* 147 */ "indexed_opt ::= INDEXED BY nm",
 /* 148 */ "indexed_opt ::= NOT INDEXED",
 /* 149 */ "using_opt ::= USING LP inscollist RP",
 /* 150 */ "using_opt ::=",
 /* 151 */ "orderby_opt ::=",
 /* 152 */ "orderby_opt ::= ORDER BY sortlist",
 /* 153 */ "sortlist ::= sortlist COMMA sortitem sortorder",
 /* 154 */ "sortlist ::= sortitem sortorder",
 /* 155 */ "sortitem ::= expr",
 /* 156 */ "sortorder ::= ASC",
 /* 157 */ "sortorder ::= DESC",
 /* 158 */ "sortorder ::=",
 /* 159 */ "groupby_opt ::=",
 /* 160 */ "groupby_opt ::= GROUP BY nexprlist",
 /* 161 */ "having_opt ::=",
 /* 162 */ "having_opt ::= HAVING expr",
 /* 163 */ "limit_opt ::=",
 /* 164 */ "limit_opt ::= LIMIT expr",
 /* 165 */ "limit_opt ::= LIMIT expr OFFSET expr",
 /* 166 */ "limit_opt ::= LIMIT expr COMMA expr",
 /* 167 */ "cmd ::= DELETE FROM fullname indexed_opt where_opt",
 /* 168 */ "where_opt ::=",
 /* 169 */ "where_opt ::= WHERE expr",
 /* 170 */ "cmd ::= UPDATE orconf fullname indexed_opt SET setlist where_opt",
 /* 171 */ "setlist ::= setlist COMMA nm EQ expr",
 /* 172 */ "setlist ::= nm EQ expr",
 /* 173 */ "cmd ::= insert_cmd INTO fullname inscollist_opt VALUES LP itemlist RP",
 /* 174 */ "cmd ::= insert_cmd INTO fullname inscollist_opt select",
 /* 175 */ "cmd ::= insert_cmd INTO fullname inscollist_opt DEFAULT VALUES",
 /* 176 */ "insert_cmd ::= INSERT orconf",
 /* 177 */ "insert_cmd ::= REPLACE",
 /* 178 */ "itemlist ::= itemlist COMMA expr",
 /* 179 */ "itemlist ::= expr",
 /* 180 */ "inscollist_opt ::=",
 /* 181 */ "inscollist_opt ::= LP inscollist RP",
 /* 182 */ "inscollist ::= inscollist COMMA nm",
 /* 183 */ "inscollist ::= nm",
 /* 184 */ "expr ::= term",
 /* 185 */ "expr ::= LP expr RP",
 /* 186 */ "term ::= NULL",
 /* 187 */ "expr ::= id",
 /* 188 */ "expr ::= JOIN_KW",
 /* 189 */ "expr ::= nm DOT nm",
 /* 190 */ "expr ::= nm DOT nm DOT nm",
 /* 191 */ "term ::= INTEGER|FLOAT|BLOB",
 /* 192 */ "term ::= STRING",
 /* 193 */ "expr ::= REGISTER",
 /* 194 */ "expr ::= VARIABLE",
 /* 195 */ "expr ::= expr COLLATE ids",
 /* 196 */ "expr ::= CAST LP expr AS typetoken RP",
 /* 197 */ "expr ::= ID LP distinct exprlist RP",
 /* 198 */ "expr ::= ID LP STAR RP",
 /* 199 */ "term ::= CTIME_KW",
 /* 200 */ "expr ::= expr AND expr",
 /* 201 */ "expr ::= expr OR expr",
 /* 202 */ "expr ::= expr LT|GT|GE|LE expr",
 /* 203 */ "expr ::= expr EQ|NE expr",
 /* 204 */ "expr ::= expr BITAND|BITOR|LSHIFT|RSHIFT expr",
 /* 205 */ "expr ::= expr PLUS|MINUS expr",
 /* 206 */ "expr ::= expr STAR|SLASH|REM expr",
 /* 207 */ "expr ::= expr CONCAT expr",
 /* 208 */ "likeop ::= LIKE_KW",
 /* 209 */ "likeop ::= NOT LIKE_KW",
 /* 210 */ "likeop ::= MATCH",
 /* 211 */ "likeop ::= NOT MATCH",
 /* 212 */ "expr ::= expr likeop expr",
 /* 213 */ "expr ::= expr likeop expr ESCAPE expr",
 /* 214 */ "expr ::= expr ISNULL|NOTNULL",
 /* 215 */ "expr ::= expr NOT NULL",
 /* 216 */ "expr ::= expr IS expr",
 /* 217 */ "expr ::= expr IS NOT expr",
 /* 218 */ "expr ::= NOT expr",
 /* 219 */ "expr ::= BITNOT expr",
 /* 220 */ "expr ::= MINUS expr",
 /* 221 */ "expr ::= PLUS expr",
 /* 222 */ "between_op ::= BETWEEN",
 /* 223 */ "between_op ::= NOT BETWEEN",
 /* 224 */ "expr ::= expr between_op expr AND expr",
 /* 225 */ "in_op ::= IN",
 /* 226 */ "in_op ::= NOT IN",
 /* 227 */ "expr ::= expr in_op LP exprlist RP",
 /* 228 */ "expr ::= LP select RP",
 /* 229 */ "expr ::= expr in_op LP select RP",
 /* 230 */ "expr ::= expr in_op nm dbnm",
 /* 231 */ "expr ::= EXISTS LP select RP",
 /* 232 */ "expr ::= CASE case_operand case_exprlist case_else END",
 /* 233 */ "case_exprlist ::= case_exprlist WHEN expr THEN expr",
 /* 234 */ "case_exprlist ::= WHEN expr THEN expr",
 /* 235 */ "case_else ::= ELSE expr",
 /* 236 */ "case_else ::=",
 /* 237 */ "case_operand ::= expr",
 /* 238 */ "case_operand ::=",
 /* 239 */ "exprlist ::= nexprlist",
 /* 240 */ "exprlist ::=",
 /* 241 */ "nexprlist ::= nexprlist COMMA expr",
 /* 242 */ "nexprlist ::= expr",
 /* 243 */ "cmd ::= createkw uniqueflag INDEX ifnotexists nm dbnm ON nm LP idxlist RP",
 /* 244 */ "uniqueflag ::= UNIQUE",
 /* 245 */ "uniqueflag ::=",
 /* 246 */ "idxlist_opt ::=",
 /* 247 */ "idxlist_opt ::= LP idxlist RP",
 /* 248 */ "idxlist ::= idxlist COMMA nm collate sortorder",
 /* 249 */ "idxlist ::= nm collate sortorder",
 /* 250 */ "collate ::=",
 /* 251 */ "collate ::= COLLATE ids",
 /* 252 */ "cmd ::= DROP INDEX ifexists fullname",
 /* 253 */ "cmd ::= VACUUM",
 /* 254 */ "cmd ::= VACUUM nm",
 /* 255 */ "cmd ::= PRAGMA nm dbnm",
 /* 256 */ "cmd ::= PRAGMA nm dbnm EQ nmnum",
 /* 257 */ "cmd ::= PRAGMA nm dbnm LP nmnum RP",
 /* 258 */ "cmd ::= PRAGMA nm dbnm EQ minus_num",
 /* 259 */ "cmd ::= PRAGMA nm dbnm LP minus_num RP",
 /* 260 */ "nmnum ::= plus_num",
 /* 261 */ "nmnum ::= nm",
 /* 262 */ "nmnum ::= ON",
 /* 263 */ "nmnum ::= DELETE",
 /* 264 */ "nmnum ::= DEFAULT",
 /* 265 */ "plus_num ::= plus_opt number",
 /* 266 */ "minus_num ::= MINUS number",
 /* 267 */ "number ::= INTEGER|FLOAT",
 /* 268 */ "plus_opt ::= PLUS",
 /* 269 */ "plus_opt ::=",
 /* 270 */ "cmd ::= createkw trigger_decl BEGIN trigger_cmd_list END",
 /* 271 */ "trigger_decl ::= temp TRIGGER ifnotexists nm dbnm trigger_time trigger_event ON fullname foreach_clause when_clause",
 /* 272 */ "trigger_time ::= BEFORE",
 /* 273 */ "trigger_time ::= AFTER",
 /* 274 */ "trigger_time ::= INSTEAD OF",
 /* 275 */ "trigger_time ::=",
 /* 276 */ "trigger_event ::= DELETE|INSERT",
 /* 277 */ "trigger_event ::= UPDATE",
 /* 278 */ "trigger_event ::= UPDATE OF inscollist",
 /* 279 */ "foreach_clause ::=",
 /* 280 */ "foreach_clause ::= FOR EACH ROW",
 /* 281 */ "when_clause ::=",
 /* 282 */ "when_clause ::= WHEN expr",
 /* 283 */ "trigger_cmd_list ::= trigger_cmd_list trigger_cmd SEMI",
 /* 284 */ "trigger_cmd_list ::= trigger_cmd SEMI",
 /* 285 */ "trnm ::= nm",
 /* 286 */ "trnm ::= nm DOT nm",
 /* 287 */ "tridxby ::=",
 /* 288 */ "tridxby ::= INDEXED BY nm",
 /* 289 */ "tridxby ::= NOT INDEXED",
 /* 290 */ "trigger_cmd ::= UPDATE orconf trnm tridxby SET setlist where_opt",
 /* 291 */ "trigger_cmd ::= insert_cmd INTO trnm inscollist_opt VALUES LP itemlist RP",
 /* 292 */ "trigger_cmd ::= insert_cmd INTO trnm inscollist_opt select",
 /* 293 */ "trigger_cmd ::= DELETE FROM trnm tridxby where_opt",
 /* 294 */ "trigger_cmd ::= select",
 /* 295 */ "expr ::= RAISE LP IGNORE RP",
 /* 296 */ "expr ::= RAISE LP raisetype COMMA nm RP",
 /* 297 */ "raisetype ::= ROLLBACK",
 /* 298 */ "raisetype ::= ABORT",
 /* 299 */ "raisetype ::= FAIL",
 /* 300 */ "cmd ::= DROP TRIGGER ifexists fullname",
 /* 301 */ "cmd ::= ATTACH database_kw_opt expr AS expr key_opt",
 /* 302 */ "cmd ::= DETACH database_kw_opt expr",
 /* 303 */ "key_opt ::=",
 /* 304 */ "key_opt ::= KEY expr",
 /* 305 */ "database_kw_opt ::= DATABASE",
 /* 306 */ "database_kw_opt ::=",
 /* 307 */ "cmd ::= REINDEX",
 /* 308 */ "cmd ::= REINDEX nm dbnm",
 /* 309 */ "cmd ::= ANALYZE",
 /* 310 */ "cmd ::= ANALYZE nm dbnm",
 /* 311 */ "cmd ::= ALTER TABLE fullname RENAME TO nm",
 /* 312 */ "cmd ::= ALTER TABLE add_column_fullname ADD kwcolumn_opt column",
 /* 313 */ "add_column_fullname ::= fullname",
 /* 314 */ "kwcolumn_opt ::=",
 /* 315 */ "kwcolumn_opt ::= COLUMNKW",
 /* 316 */ "cmd ::= create_vtab",
 /* 317 */ "cmd ::= create_vtab LP vtabarglist RP",
 /* 318 */ "create_vtab ::= createkw VIRTUAL TABLE nm dbnm USING nm",
 /* 319 */ "vtabarglist ::= vtabarg",
 /* 320 */ "vtabarglist ::= vtabarglist COMMA vtabarg",
 /* 321 */ "vtabarg ::=",
 /* 322 */ "vtabarg ::= vtabarg vtabargtoken",
 /* 323 */ "vtabargtoken ::= ANY",
 /* 324 */ "vtabargtoken ::= lp anylist RP",
 /* 325 */ "lp ::= LP",
 /* 326 */ "anylist ::=",
 /* 327 */ "anylist ::= anylist LP anylist RP",
 /* 328 */ "anylist ::= anylist ANY",
};
#endif /* NDEBUG */


#if YYSTACKDEPTH<=0
/*
** Try to increase the size of the parser stack.
*/
static void yyGrowStack(yyParser *p){
  int newSize;
  yyStackEntry *pNew;

  newSize = p->yystksz*2 + 100;
  pNew = realloc(p->yystack, newSize*sizeof(pNew[0]));
  if( pNew ){
    p->yystack = pNew;
    p->yystksz = newSize;
#ifndef NDEBUG
    if( yyTraceFILE ){
      fprintf(yyTraceFILE,"%sStack grows to %d entries!\n",
              yyTracePrompt, p->yystksz);
    }
#endif
  }
}
#endif

/* 
** This function allocates a new parser.
** The only argument is a pointer to a function which works like
** malloc.
**
** Inputs:
** A pointer to the function used to allocate memory.
**
** Outputs:
** A pointer to a parser.  This pointer is used in subsequent calls
** to sqlite3Parser and sqlite3ParserFree.
*/
SQLITE_PRIVATE void *sqlite3ParserAlloc(void *(*mallocProc)(size_t)){
  yyParser *pParser;
  pParser = (yyParser*)(*mallocProc)( (size_t)sizeof(yyParser) );
  if( pParser ){
    pParser->yyidx = -1;
#ifdef YYTRACKMAXSTACKDEPTH
    pParser->yyidxMax = 0;
#endif
#if YYSTACKDEPTH<=0
    pParser->yystack = NULL;
    pParser->yystksz = 0;
    yyGrowStack(pParser);
#endif
  }
  return pParser;
}

/* The following function deletes the value associated with a
** symbol.  The symbol can be either a terminal or nonterminal.
** "yymajor" is the symbol code, and "yypminor" is a pointer to
** the value.
*/
static void yy_destructor(
  yyParser *yypParser,    /* The parser */
  YYCODETYPE yymajor,     /* Type code for object to destroy */
  YYMINORTYPE *yypminor   /* The object to be destroyed */
){
  sqlite3ParserARG_FETCH;
  switch( yymajor ){
    /* Here is inserted the actions which take place when a
    ** terminal or non-terminal is destroyed.  This can happen
    ** when the symbol is popped from the stack during a
    ** reduce or during error processing or when a parser is 
    ** being destroyed before it is finished parsing.
    **
    ** Note: during a reduce, the only symbols destroyed are those
    ** which appear on the RHS of the rule, but which are not used
    ** inside the C code.
    */
    case 160: /* select */
    case 194: /* oneselect */
{
sqlite3SelectDelete(pParse->db, (yypminor->yy387));
}
      break;
    case 174: /* term */
    case 175: /* expr */
{
sqlite3ExprDelete(pParse->db, (yypminor->yy118).pExpr);
}
      break;
    case 179: /* idxlist_opt */
    case 187: /* idxlist */
    case 197: /* selcollist */
    case 200: /* groupby_opt */
    case 202: /* orderby_opt */
    case 204: /* sclp */
    case 214: /* sortlist */
    case 216: /* nexprlist */
    case 217: /* setlist */
    case 220: /* itemlist */
    case 221: /* exprlist */
    case 226: /* case_exprlist */
{
sqlite3ExprListDelete(pParse->db, (yypminor->yy322));
}
      break;
    case 193: /* fullname */
    case 198: /* from */
    case 206: /* seltablist */
    case 207: /* stl_prefix */
{
sqlite3SrcListDelete(pParse->db, (yypminor->yy259));
}
      break;
    case 199: /* where_opt */
    case 201: /* having_opt */
    case 210: /* on_opt */
    case 215: /* sortitem */
    case 225: /* case_operand */
    case 227: /* case_else */
    case 238: /* when_clause */
    case 243: /* key_opt */
{
sqlite3ExprDelete(pParse->db, (yypminor->yy314));
}
      break;
    case 211: /* using_opt */
    case 213: /* inscollist */
    case 219: /* inscollist_opt */
{
sqlite3IdListDelete(pParse->db, (yypminor->yy384));
}
      break;
    case 234: /* trigger_cmd_list */
    case 239: /* trigger_cmd */
{
sqlite3DeleteTriggerStep(pParse->db, (yypminor->yy203));
}
      break;
    case 236: /* trigger_event */
{
sqlite3IdListDelete(pParse->db, (yypminor->yy90).b);
}
      break;
    default:  break;   /* If no destructor action specified: do nothing */
  }
}

/*
** Pop the parser's stack once.
**
** If there is a destructor routine associated with the token which
** is popped from the stack, then call it.
**
** Return the major token number for the symbol popped.
*/
static int yy_pop_parser_stack(yyParser *pParser){
  YYCODETYPE yymajor;
  yyStackEntry *yytos = &pParser->yystack[pParser->yyidx];

  /* There is no mechanism by which the parser stack can be popped below
  ** empty in SQLite.  */
  if( NEVER(pParser->yyidx<0) ) return 0;
#ifndef NDEBUG
  if( yyTraceFILE && pParser->yyidx>=0 ){
    fprintf(yyTraceFILE,"%sPopping %s\n",
      yyTracePrompt,
      yyTokenName[yytos->major]);
  }
#endif
  yymajor = yytos->major;
  yy_destructor(pParser, yymajor, &yytos->minor);
  pParser->yyidx--;
  return yymajor;
}

/* 
** Deallocate and destroy a parser.  Destructors are all called for
** all stack elements before shutting the parser down.
**
** Inputs:
** <ul>
** <li>  A pointer to the parser.  This should be a pointer
**       obtained from sqlite3ParserAlloc.
** <li>  A pointer to a function used to reclaim memory obtained
**       from malloc.
** </ul>
*/
SQLITE_PRIVATE void sqlite3ParserFree(
  void *p,                    /* The parser to be deleted */
  void (*freeProc)(void*)     /* Function used to reclaim memory */
){
  yyParser *pParser = (yyParser*)p;
  /* In SQLite, we never try to destroy a parser that was not successfully
  ** created in the first place. */
  if( NEVER(pParser==0) ) return;
  while( pParser->yyidx>=0 ) yy_pop_parser_stack(pParser);
#if YYSTACKDEPTH<=0
  free(pParser->yystack);
#endif
  (*freeProc)((void*)pParser);
}

/*
** Return the peak depth of the stack for a parser.
*/
#ifdef YYTRACKMAXSTACKDEPTH
SQLITE_PRIVATE int sqlite3ParserStackPeak(void *p){
  yyParser *pParser = (yyParser*)p;
  return pParser->yyidxMax;
}
#endif

/*
** Find the appropriate action for a parser given the terminal
** look-ahead token iLookAhead.
**
** If the look-ahead token is YYNOCODE, then check to see if the action is
** independent of the look-ahead.  If it is, return the action, otherwise
** return YY_NO_ACTION.
*/
static int yy_find_shift_action(
  yyParser *pParser,        /* The parser */
  YYCODETYPE iLookAhead     /* The look-ahead token */
){
  int i;
  int stateno = pParser->yystack[pParser->yyidx].stateno;
 
  if( stateno>YY_SHIFT_COUNT
   || (i = yy_shift_ofst[stateno])==YY_SHIFT_USE_DFLT ){
    return yy_default[stateno];
  }
  assert( iLookAhead!=YYNOCODE );
  i += iLookAhead;
  if( i<0 || i>=YY_ACTTAB_COUNT || yy_lookahead[i]!=iLookAhead ){
    if( iLookAhead>0 ){
#ifdef YYFALLBACK
      YYCODETYPE iFallback;            /* Fallback token */
      if( iLookAhead<sizeof(yyFallback)/sizeof(yyFallback[0])
             && (iFallback = yyFallback[iLookAhead])!=0 ){
#ifndef NDEBUG
        if( yyTraceFILE ){
          fprintf(yyTraceFILE, "%sFALLBACK %s => %s\n",
             yyTracePrompt, yyTokenName[iLookAhead], yyTokenName[iFallback]);
        }
#endif
        return yy_find_shift_action(pParser, iFallback);
      }
#endif
#ifdef YYWILDCARD
      {
        int j = i - iLookAhead + YYWILDCARD;
        if( 
#if YY_SHIFT_MIN+YYWILDCARD<0
          j>=0 &&
#endif
#if YY_SHIFT_MAX+YYWILDCARD>=YY_ACTTAB_COUNT
          j<YY_ACTTAB_COUNT &&
#endif
          yy_lookahead[j]==YYWILDCARD
        ){
#ifndef NDEBUG
          if( yyTraceFILE ){
            fprintf(yyTraceFILE, "%sWILDCARD %s => %s\n",
               yyTracePrompt, yyTokenName[iLookAhead], yyTokenName[YYWILDCARD]);
          }
#endif /* NDEBUG */
          return yy_action[j];
        }
      }
#endif /* YYWILDCARD */
    }
    return yy_default[stateno];
  }else{
    return yy_action[i];
  }
}

/*
** Find the appropriate action for a parser given the non-terminal
** look-ahead token iLookAhead.
**
** If the look-ahead token is YYNOCODE, then check to see if the action is
** independent of the look-ahead.  If it is, return the action, otherwise
** return YY_NO_ACTION.
*/
static int yy_find_reduce_action(
  int stateno,              /* Current state number */
  YYCODETYPE iLookAhead     /* The look-ahead token */
){
  int i;
#ifdef YYERRORSYMBOL
  if( stateno>YY_REDUCE_COUNT ){
    return yy_default[stateno];
  }
#else
  assert( stateno<=YY_REDUCE_COUNT );
#endif
  i = yy_reduce_ofst[stateno];
  assert( i!=YY_REDUCE_USE_DFLT );
  assert( iLookAhead!=YYNOCODE );
  i += iLookAhead;
#ifdef YYERRORSYMBOL
  if( i<0 || i>=YY_ACTTAB_COUNT || yy_lookahead[i]!=iLookAhead ){
    return yy_default[stateno];
  }
#else
  assert( i>=0 && i<YY_ACTTAB_COUNT );
  assert( yy_lookahead[i]==iLookAhead );
#endif
  return yy_action[i];
}

/*
** The following routine is called if the stack overflows.
*/
static void yyStackOverflow(yyParser *yypParser, YYMINORTYPE *yypMinor){
   sqlite3ParserARG_FETCH;
   yypParser->yyidx--;
#ifndef NDEBUG
   if( yyTraceFILE ){
     fprintf(yyTraceFILE,"%sStack Overflow!\n",yyTracePrompt);
   }
#endif
   while( yypParser->yyidx>=0 ) yy_pop_parser_stack(yypParser);
   /* Here code is inserted which will execute if the parser
   ** stack every overflows */

  UNUSED_PARAMETER(yypMinor); /* Silence some compiler warnings */
  sqlite3ErrorMsg(pParse, "parser stack overflow");
  pParse->parseError = 1;
   sqlite3ParserARG_STORE; /* Suppress warning about unused %extra_argument var */
}

/*
** Perform a shift action.
*/
static void yy_shift(
  yyParser *yypParser,          /* The parser to be shifted */
  int yyNewState,               /* The new state to shift in */
  int yyMajor,                  /* The major token to shift in */
  YYMINORTYPE *yypMinor         /* Pointer to the minor token to shift in */
){
  yyStackEntry *yytos;
  yypParser->yyidx++;
#ifdef YYTRACKMAXSTACKDEPTH
  if( yypParser->yyidx>yypParser->yyidxMax ){
    yypParser->yyidxMax = yypParser->yyidx;
  }
#endif
#if YYSTACKDEPTH>0 
  if( yypParser->yyidx>=YYSTACKDEPTH ){
    yyStackOverflow(yypParser, yypMinor);
    return;
  }
#else
  if( yypParser->yyidx>=yypParser->yystksz ){
    yyGrowStack(yypParser);
    if( yypParser->yyidx>=yypParser->yystksz ){
      yyStackOverflow(yypParser, yypMinor);
      return;
    }
  }
#endif
  yytos = &yypParser->yystack[yypParser->yyidx];
  yytos->stateno = (YYACTIONTYPE)yyNewState;
  yytos->major = (YYCODETYPE)yyMajor;
  yytos->minor = *yypMinor;
#ifndef NDEBUG
  if( yyTraceFILE && yypParser->yyidx>0 ){
    int i;
    fprintf(yyTraceFILE,"%sShift %d\n",yyTracePrompt,yyNewState);
    fprintf(yyTraceFILE,"%sStack:",yyTracePrompt);
    for(i=1; i<=yypParser->yyidx; i++)
      fprintf(yyTraceFILE," %s",yyTokenName[yypParser->yystack[i].major]);
    fprintf(yyTraceFILE,"\n");
  }
#endif
}

/* The following table contains information about every rule that
** is used during the reduce.
*/
static const struct {
  YYCODETYPE lhs;         /* Symbol on the left-hand side of the rule */
  unsigned char nrhs;     /* Number of right-hand side symbols in the rule */
} yyRuleInfo[] = {
  { 142, 1 },
  { 143, 2 },
  { 143, 1 },
  { 144, 1 },
  { 144, 3 },
  { 145, 0 },
  { 145, 1 },
  { 145, 3 },
  { 146, 1 },
  { 147, 3 },
  { 149, 0 },
  { 149, 1 },
  { 149, 2 },
  { 148, 0 },
  { 148, 1 },
  { 148, 1 },
  { 148, 1 },
  { 147, 2 },
  { 147, 2 },
  { 147, 2 },
  { 151, 1 },
  { 151, 0 },
  { 147, 2 },
  { 147, 3 },
  { 147, 5 },
  { 147, 2 },
  { 152, 6 },
  { 154, 1 },
  { 156, 0 },
  { 156, 3 },
  { 155, 1 },
  { 155, 0 },
  { 153, 4 },
  { 153, 2 },
  { 158, 3 },
  { 158, 1 },
  { 161, 3 },
  { 162, 1 },
  { 165, 1 },
  { 165, 1 },
  { 166, 1 },
  { 150, 1 },
  { 150, 1 },
  { 150, 1 },
  { 163, 0 },
  { 163, 1 },
  { 167, 1 },
  { 167, 4 },
  { 167, 6 },
  { 168, 1 },
  { 168, 2 },
  { 169, 1 },
  { 169, 1 },
  { 164, 2 },
  { 164, 0 },
  { 172, 3 },
  { 172, 1 },
  { 173, 2 },
  { 173, 4 },
  { 173, 3 },
  { 173, 3 },
  { 173, 2 },
  { 173, 2 },
  { 173, 3 },
  { 173, 5 },
  { 173, 2 },
  { 173, 4 },
  { 173, 4 },
  { 173, 1 },
  { 173, 2 },
  { 178, 0 },
  { 178, 1 },
  { 180, 0 },
  { 180, 2 },
  { 182, 2 },
  { 182, 3 },
  { 182, 3 },
  { 182, 3 },
  { 183, 2 },
  { 183, 2 },
  { 183, 1 },
  { 183, 1 },
  { 183, 2 },
  { 181, 3 },
  { 181, 2 },
  { 184, 0 },
  { 184, 2 },
  { 184, 2 },
  { 159, 0 },
  { 159, 2 },
  { 185, 3 },
  { 185, 2 },
  { 185, 1 },
  { 186, 2 },
  { 186, 7 },
  { 186, 5 },
  { 186, 5 },
  { 186, 10 },
  { 188, 0 },
  { 188, 1 },
  { 176, 0 },
  { 176, 3 },
  { 189, 0 },
  { 189, 2 },
  { 190, 1 },
  { 190, 1 },
  { 190, 1 },
  { 147, 4 },
  { 192, 2 },
  { 192, 0 },
  { 147, 8 },
  { 147, 4 },
  { 147, 1 },
  { 160, 1 },
  { 160, 3 },
  { 195, 1 },
  { 195, 2 },
  { 195, 1 },
  { 194, 9 },
  { 196, 1 },
  { 196, 1 },
  { 196, 0 },
  { 204, 2 },
  { 204, 0 },
  { 197, 3 },
  { 197, 2 },
  { 197, 4 },
  { 205, 2 },
  { 205, 1 },
  { 205, 0 },
  { 198, 0 },
  { 198, 2 },
  { 207, 2 },
  { 207, 0 },
  { 206, 7 },
  { 206, 7 },
  { 206, 7 },
  { 157, 0 },
  { 157, 2 },
  { 193, 2 },
  { 208, 1 },
  { 208, 2 },
  { 208, 3 },
  { 208, 4 },
  { 210, 2 },
  { 210, 0 },
  { 209, 0 },
  { 209, 3 },
  { 209, 2 },
  { 211, 4 },
  { 211, 0 },
  { 202, 0 },
  { 202, 3 },
  { 214, 4 },
  { 214, 2 },
  { 215, 1 },
  { 177, 1 },
  { 177, 1 },
  { 177, 0 },
  { 200, 0 },
  { 200, 3 },
  { 201, 0 },
  { 201, 2 },
  { 203, 0 },
  { 203, 2 },
  { 203, 4 },
  { 203, 4 },
  { 147, 5 },
  { 199, 0 },
  { 199, 2 },
  { 147, 7 },
  { 217, 5 },
  { 217, 3 },
  { 147, 8 },
  { 147, 5 },
  { 147, 6 },
  { 218, 2 },
  { 218, 1 },
  { 220, 3 },
  { 220, 1 },
  { 219, 0 },
  { 219, 3 },
  { 213, 3 },
  { 213, 1 },
  { 175, 1 },
  { 175, 3 },
  { 174, 1 },
  { 175, 1 },
  { 175, 1 },
  { 175, 3 },
  { 175, 5 },
  { 174, 1 },
  { 174, 1 },
  { 175, 1 },
  { 175, 1 },
  { 175, 3 },
  { 175, 6 },
  { 175, 5 },
  { 175, 4 },
  { 174, 1 },
  { 175, 3 },
  { 175, 3 },
  { 175, 3 },
  { 175, 3 },
  { 175, 3 },
  { 175, 3 },
  { 175, 3 },
  { 175, 3 },
  { 222, 1 },
  { 222, 2 },
  { 222, 1 },
  { 222, 2 },
  { 175, 3 },
  { 175, 5 },
  { 175, 2 },
  { 175, 3 },
  { 175, 3 },
  { 175, 4 },
  { 175, 2 },
  { 175, 2 },
  { 175, 2 },
  { 175, 2 },
  { 223, 1 },
  { 223, 2 },
  { 175, 5 },
  { 224, 1 },
  { 224, 2 },
  { 175, 5 },
  { 175, 3 },
  { 175, 5 },
  { 175, 4 },
  { 175, 4 },
  { 175, 5 },
  { 226, 5 },
  { 226, 4 },
  { 227, 2 },
  { 227, 0 },
  { 225, 1 },
  { 225, 0 },
  { 221, 1 },
  { 221, 0 },
  { 216, 3 },
  { 216, 1 },
  { 147, 11 },
  { 228, 1 },
  { 228, 0 },
  { 179, 0 },
  { 179, 3 },
  { 187, 5 },
  { 187, 3 },
  { 229, 0 },
  { 229, 2 },
  { 147, 4 },
  { 147, 1 },
  { 147, 2 },
  { 147, 3 },
  { 147, 5 },
  { 147, 6 },
  { 147, 5 },
  { 147, 6 },
  { 230, 1 },
  { 230, 1 },
  { 230, 1 },
  { 230, 1 },
  { 230, 1 },
  { 170, 2 },
  { 171, 2 },
  { 232, 1 },
  { 231, 1 },
  { 231, 0 },
  { 147, 5 },
  { 233, 11 },
  { 235, 1 },
  { 235, 1 },
  { 235, 2 },
  { 235, 0 },
  { 236, 1 },
  { 236, 1 },
  { 236, 3 },
  { 237, 0 },
  { 237, 3 },
  { 238, 0 },
  { 238, 2 },
  { 234, 3 },
  { 234, 2 },
  { 240, 1 },
  { 240, 3 },
  { 241, 0 },
  { 241, 3 },
  { 241, 2 },
  { 239, 7 },
  { 239, 8 },
  { 239, 5 },
  { 239, 5 },
  { 239, 1 },
  { 175, 4 },
  { 175, 6 },
  { 191, 1 },
  { 191, 1 },
  { 191, 1 },
  { 147, 4 },
  { 147, 6 },
  { 147, 3 },
  { 243, 0 },
  { 243, 2 },
  { 242, 1 },
  { 242, 0 },
  { 147, 1 },
  { 147, 3 },
  { 147, 1 },
  { 147, 3 },
  { 147, 6 },
  { 147, 6 },
  { 244, 1 },
  { 245, 0 },
  { 245, 1 },
  { 147, 1 },
  { 147, 4 },
  { 246, 7 },
  { 247, 1 },
  { 247, 3 },
  { 248, 0 },
  { 248, 2 },
  { 249, 1 },
  { 249, 3 },
  { 250, 1 },
  { 251, 0 },
  { 251, 4 },
  { 251, 2 },
};

static void yy_accept(yyParser*);  /* Forward Declaration */

/*
** Perform a reduce action and the shift that must immediately
** follow the reduce.
*/
static void yy_reduce(
  yyParser *yypParser,         /* The parser */
  int yyruleno                 /* Number of the rule by which to reduce */
){
  int yygoto;                     /* The next state */
  int yyact;                      /* The next action */
  YYMINORTYPE yygotominor;        /* The LHS of the rule reduced */
  yyStackEntry *yymsp;            /* The top of the parser's stack */
  int yysize;                     /* Amount to pop the stack */
  sqlite3ParserARG_FETCH;
  yymsp = &yypParser->yystack[yypParser->yyidx];
#ifndef NDEBUG
  if( yyTraceFILE && yyruleno>=0 
        && yyruleno<(int)(sizeof(yyRuleName)/sizeof(yyRuleName[0])) ){
    fprintf(yyTraceFILE, "%sReduce [%s].\n", yyTracePrompt,
      yyRuleName[yyruleno]);
  }
#endif /* NDEBUG */

  /* Silence complaints from purify about yygotominor being uninitialized
  ** in some cases when it is copied into the stack after the following
  ** switch.  yygotominor is uninitialized when a rule reduces that does
  ** not set the value of its left-hand side nonterminal.  Leaving the
  ** value of the nonterminal uninitialized is utterly harmless as long
  ** as the value is never used.  So really the only thing this code
  ** accomplishes is to quieten purify.  
  **
  ** 2007-01-16:  The wireshark project (www.wireshark.org) reports that
  ** without this code, their parser segfaults.  I'm not sure what there
  ** parser is doing to make this happen.  This is the second bug report
  ** from wireshark this week.  Clearly they are stressing Lemon in ways
  ** that it has not been previously stressed...  (SQLite ticket #2172)
  */
  /*memset(&yygotominor, 0, sizeof(yygotominor));*/
  yygotominor = yyzerominor;


  switch( yyruleno ){
  /* Beginning here are the reduction cases.  A typical example
  ** follows:
  **   case 0:
  **  #line <lineno> <grammarfile>
  **     { ... }           // User supplied code
  **  #line <lineno> <thisfile>
  **     break;
  */
      case 5: /* explain ::= */
{ sqlite3BeginParse(pParse, 0); }
        break;
      case 6: /* explain ::= EXPLAIN */
{ sqlite3BeginParse(pParse, 1); }
        break;
      case 7: /* explain ::= EXPLAIN QUERY PLAN */
{ sqlite3BeginParse(pParse, 2); }
        break;
      case 8: /* cmdx ::= cmd */
{ sqlite3FinishCoding(pParse); }
        break;
      case 9: /* cmd ::= BEGIN transtype trans_opt */
{sqlite3BeginTransaction(pParse, yymsp[-1].minor.yy4);}
        break;
      case 13: /* transtype ::= */
{yygotominor.yy4 = TK_DEFERRED;}
        break;
      case 14: /* transtype ::= DEFERRED */
      case 15: /* transtype ::= IMMEDIATE */ yytestcase(yyruleno==15);
      case 16: /* transtype ::= EXCLUSIVE */ yytestcase(yyruleno==16);
      case 115: /* multiselect_op ::= UNION */ yytestcase(yyruleno==115);
      case 117: /* multiselect_op ::= EXCEPT|INTERSECT */ yytestcase(yyruleno==117);
{yygotominor.yy4 = yymsp[0].major;}
        break;
      case 17: /* cmd ::= COMMIT trans_opt */
      case 18: /* cmd ::= END trans_opt */ yytestcase(yyruleno==18);
{sqlite3CommitTransaction(pParse);}
        break;
      case 19: /* cmd ::= ROLLBACK trans_opt */
{sqlite3RollbackTransaction(pParse);}
        break;
      case 22: /* cmd ::= SAVEPOINT nm */
{
  sqlite3Savepoint(pParse, SAVEPOINT_BEGIN, &yymsp[0].minor.yy0);
}
        break;
      case 23: /* cmd ::= RELEASE savepoint_opt nm */
{
  sqlite3Savepoint(pParse, SAVEPOINT_RELEASE, &yymsp[0].minor.yy0);
}
        break;
      case 24: /* cmd ::= ROLLBACK trans_opt TO savepoint_opt nm */
{
  sqlite3Savepoint(pParse, SAVEPOINT_ROLLBACK, &yymsp[0].minor.yy0);
}
        break;
      case 26: /* create_table ::= createkw temp TABLE ifnotexists nm dbnm */
{
   sqlite3StartTable(pParse,&yymsp[-1].minor.yy0,&yymsp[0].minor.yy0,yymsp[-4].minor.yy4,0,0,yymsp[-2].minor.yy4);
}
        break;
      case 27: /* createkw ::= CREATE */
{
  pParse->db->lookaside.bEnabled = 0;
  yygotominor.yy0 = yymsp[0].minor.yy0;
}
        break;
      case 28: /* ifnotexists ::= */
      case 31: /* temp ::= */ yytestcase(yyruleno==31);
      case 70: /* autoinc ::= */ yytestcase(yyruleno==70);
      case 83: /* defer_subclause ::= NOT DEFERRABLE init_deferred_pred_opt */ yytestcase(yyruleno==83);
      case 85: /* init_deferred_pred_opt ::= */ yytestcase(yyruleno==85);
      case 87: /* init_deferred_pred_opt ::= INITIALLY IMMEDIATE */ yytestcase(yyruleno==87);
      case 98: /* defer_subclause_opt ::= */ yytestcase(yyruleno==98);
      case 109: /* ifexists ::= */ yytestcase(yyruleno==109);
      case 120: /* distinct ::= ALL */ yytestcase(yyruleno==120);
      case 121: /* distinct ::= */ yytestcase(yyruleno==121);
      case 222: /* between_op ::= BETWEEN */ yytestcase(yyruleno==222);
      case 225: /* in_op ::= IN */ yytestcase(yyruleno==225);
{yygotominor.yy4 = 0;}
        break;
      case 29: /* ifnotexists ::= IF NOT EXISTS */
      case 30: /* temp ::= TEMP */ yytestcase(yyruleno==30);
      case 71: /* autoinc ::= AUTOINCR */ yytestcase(yyruleno==71);
      case 86: /* init_deferred_pred_opt ::= INITIALLY DEFERRED */ yytestcase(yyruleno==86);
      case 108: /* ifexists ::= IF EXISTS */ yytestcase(yyruleno==108);
      case 119: /* distinct ::= DISTINCT */ yytestcase(yyruleno==119);
      case 223: /* between_op ::= NOT BETWEEN */ yytestcase(yyruleno==223);
      case 226: /* in_op ::= NOT IN */ yytestcase(yyruleno==226);
{yygotominor.yy4 = 1;}
        break;
      case 32: /* create_table_args ::= LP columnlist conslist_opt RP */
{
  sqlite3EndTable(pParse,&yymsp[-1].minor.yy0,&yymsp[0].minor.yy0,0);
}
        break;
      case 33: /* create_table_args ::= AS select */
{
  sqlite3EndTable(pParse,0,0,yymsp[0].minor.yy387);
  sqlite3SelectDelete(pParse->db, yymsp[0].minor.yy387);
}
        break;
      case 36: /* column ::= columnid type carglist */
{
  yygotominor.yy0.z = yymsp[-2].minor.yy0.z;
  yygotominor.yy0.n = (int)(pParse->sLastToken.z-yymsp[-2].minor.yy0.z) + pParse->sLastToken.n;
}
        break;
      case 37: /* columnid ::= nm */
{
  sqlite3AddColumn(pParse,&yymsp[0].minor.yy0);
  yygotominor.yy0 = yymsp[0].minor.yy0;
}
        break;
      case 38: /* id ::= ID */
      case 39: /* id ::= INDEXED */ yytestcase(yyruleno==39);
      case 40: /* ids ::= ID|STRING */ yytestcase(yyruleno==40);
      case 41: /* nm ::= id */ yytestcase(yyruleno==41);
      case 42: /* nm ::= STRING */ yytestcase(yyruleno==42);
      case 43: /* nm ::= JOIN_KW */ yytestcase(yyruleno==43);
      case 46: /* typetoken ::= typename */ yytestcase(yyruleno==46);
      case 49: /* typename ::= ids */ yytestcase(yyruleno==49);
      case 127: /* as ::= AS nm */ yytestcase(yyruleno==127);
      case 128: /* as ::= ids */ yytestcase(yyruleno==128);
      case 138: /* dbnm ::= DOT nm */ yytestcase(yyruleno==138);
      case 147: /* indexed_opt ::= INDEXED BY nm */ yytestcase(yyruleno==147);
      case 251: /* collate ::= COLLATE ids */ yytestcase(yyruleno==251);
      case 260: /* nmnum ::= plus_num */ yytestcase(yyruleno==260);
      case 261: /* nmnum ::= nm */ yytestcase(yyruleno==261);
      case 262: /* nmnum ::= ON */ yytestcase(yyruleno==262);
      case 263: /* nmnum ::= DELETE */ yytestcase(yyruleno==263);
      case 264: /* nmnum ::= DEFAULT */ yytestcase(yyruleno==264);
      case 265: /* plus_num ::= plus_opt number */ yytestcase(yyruleno==265);
      case 266: /* minus_num ::= MINUS number */ yytestcase(yyruleno==266);
      case 267: /* number ::= INTEGER|FLOAT */ yytestcase(yyruleno==267);
      case 285: /* trnm ::= nm */ yytestcase(yyruleno==285);
{yygotominor.yy0 = yymsp[0].minor.yy0;}
        break;
      case 45: /* type ::= typetoken */
{sqlite3AddColumnType(pParse,&yymsp[0].minor.yy0);}
        break;
      case 47: /* typetoken ::= typename LP signed RP */
{
  yygotominor.yy0.z = yymsp[-3].minor.yy0.z;
  yygotominor.yy0.n = (int)(&yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n] - yymsp[-3].minor.yy0.z);
}
        break;
      case 48: /* typetoken ::= typename LP signed COMMA signed RP */
{
  yygotominor.yy0.z = yymsp[-5].minor.yy0.z;
  yygotominor.yy0.n = (int)(&yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n] - yymsp[-5].minor.yy0.z);
}
        break;
      case 50: /* typename ::= typename ids */
{yygotominor.yy0.z=yymsp[-1].minor.yy0.z; yygotominor.yy0.n=yymsp[0].minor.yy0.n+(int)(yymsp[0].minor.yy0.z-yymsp[-1].minor.yy0.z);}
        break;
      case 57: /* ccons ::= DEFAULT term */
      case 59: /* ccons ::= DEFAULT PLUS term */ yytestcase(yyruleno==59);
{sqlite3AddDefaultValue(pParse,&yymsp[0].minor.yy118);}
        break;
      case 58: /* ccons ::= DEFAULT LP expr RP */
{sqlite3AddDefaultValue(pParse,&yymsp[-1].minor.yy118);}
        break;
      case 60: /* ccons ::= DEFAULT MINUS term */
{
  ExprSpan v;
  v.pExpr = sqlite3PExpr(pParse, TK_UMINUS, yymsp[0].minor.yy118.pExpr, 0, 0);
  v.zStart = yymsp[-1].minor.yy0.z;
  v.zEnd = yymsp[0].minor.yy118.zEnd;
  sqlite3AddDefaultValue(pParse,&v);
}
        break;
      case 61: /* ccons ::= DEFAULT id */
{
  ExprSpan v;
  spanExpr(&v, pParse, TK_STRING, &yymsp[0].minor.yy0);
  sqlite3AddDefaultValue(pParse,&v);
}
        break;
      case 63: /* ccons ::= NOT NULL onconf */
{sqlite3AddNotNull(pParse, yymsp[0].minor.yy4);}
        break;
      case 64: /* ccons ::= PRIMARY KEY sortorder onconf autoinc */
{sqlite3AddPrimaryKey(pParse,0,yymsp[-1].minor.yy4,yymsp[0].minor.yy4,yymsp[-2].minor.yy4);}
        break;
      case 65: /* ccons ::= UNIQUE onconf */
{sqlite3CreateIndex(pParse,0,0,0,0,yymsp[0].minor.yy4,0,0,0,0);}
        break;
      case 66: /* ccons ::= CHECK LP expr RP */
{sqlite3AddCheckConstraint(pParse,yymsp[-1].minor.yy118.pExpr);}
        break;
      case 67: /* ccons ::= REFERENCES nm idxlist_opt refargs */
{sqlite3CreateForeignKey(pParse,0,&yymsp[-2].minor.yy0,yymsp[-1].minor.yy322,yymsp[0].minor.yy4);}
        break;
      case 68: /* ccons ::= defer_subclause */
{sqlite3DeferForeignKey(pParse,yymsp[0].minor.yy4);}
        break;
      case 69: /* ccons ::= COLLATE ids */
{sqlite3AddCollateType(pParse, &yymsp[0].minor.yy0);}
        break;
      case 72: /* refargs ::= */
{ yygotominor.yy4 = OE_None*0x0101; /* EV: R-19803-45884 */}
        break;
      case 73: /* refargs ::= refargs refarg */
{ yygotominor.yy4 = (yymsp[-1].minor.yy4 & ~yymsp[0].minor.yy215.mask) | yymsp[0].minor.yy215.value; }
        break;
      case 74: /* refarg ::= MATCH nm */
      case 75: /* refarg ::= ON INSERT refact */ yytestcase(yyruleno==75);
{ yygotominor.yy215.value = 0;     yygotominor.yy215.mask = 0x000000; }
        break;
      case 76: /* refarg ::= ON DELETE refact */
{ yygotominor.yy215.value = yymsp[0].minor.yy4;     yygotominor.yy215.mask = 0x0000ff; }
        break;
      case 77: /* refarg ::= ON UPDATE refact */
{ yygotominor.yy215.value = yymsp[0].minor.yy4<<8;  yygotominor.yy215.mask = 0x00ff00; }
        break;
      case 78: /* refact ::= SET NULL */
{ yygotominor.yy4 = OE_SetNull;  /* EV: R-33326-45252 */}
        break;
      case 79: /* refact ::= SET DEFAULT */
{ yygotominor.yy4 = OE_SetDflt;  /* EV: R-33326-45252 */}
        break;
      case 80: /* refact ::= CASCADE */
{ yygotominor.yy4 = OE_Cascade;  /* EV: R-33326-45252 */}
        break;
      case 81: /* refact ::= RESTRICT */
{ yygotominor.yy4 = OE_Restrict; /* EV: R-33326-45252 */}
        break;
      case 82: /* refact ::= NO ACTION */
{ yygotominor.yy4 = OE_None;     /* EV: R-33326-45252 */}
        break;
      case 84: /* defer_subclause ::= DEFERRABLE init_deferred_pred_opt */
      case 99: /* defer_subclause_opt ::= defer_subclause */ yytestcase(yyruleno==99);
      case 101: /* onconf ::= ON CONFLICT resolvetype */ yytestcase(yyruleno==101);
      case 104: /* resolvetype ::= raisetype */ yytestcase(yyruleno==104);
{yygotominor.yy4 = yymsp[0].minor.yy4;}
        break;
      case 88: /* conslist_opt ::= */
{yygotominor.yy0.n = 0; yygotominor.yy0.z = 0;}
        break;
      case 89: /* conslist_opt ::= COMMA conslist */
{yygotominor.yy0 = yymsp[-1].minor.yy0;}
        break;
      case 94: /* tcons ::= PRIMARY KEY LP idxlist autoinc RP onconf */
{sqlite3AddPrimaryKey(pParse,yymsp[-3].minor.yy322,yymsp[0].minor.yy4,yymsp[-2].minor.yy4,0);}
        break;
      case 95: /* tcons ::= UNIQUE LP idxlist RP onconf */
{sqlite3CreateIndex(pParse,0,0,0,yymsp[-2].minor.yy322,yymsp[0].minor.yy4,0,0,0,0);}
        break;
      case 96: /* tcons ::= CHECK LP expr RP onconf */
{sqlite3AddCheckConstraint(pParse,yymsp[-2].minor.yy118.pExpr);}
        break;
      case 97: /* tcons ::= FOREIGN KEY LP idxlist RP REFERENCES nm idxlist_opt refargs defer_subclause_opt */
{
    sqlite3CreateForeignKey(pParse, yymsp[-6].minor.yy322, &yymsp[-3].minor.yy0, yymsp[-2].minor.yy322, yymsp[-1].minor.yy4);
    sqlite3DeferForeignKey(pParse, yymsp[0].minor.yy4);
}
        break;
      case 100: /* onconf ::= */
{yygotominor.yy4 = OE_Default;}
        break;
      case 102: /* orconf ::= */
{yygotominor.yy210 = OE_Default;}
        break;
      case 103: /* orconf ::= OR resolvetype */
{yygotominor.yy210 = (u8)yymsp[0].minor.yy4;}
        break;
      case 105: /* resolvetype ::= IGNORE */
{yygotominor.yy4 = OE_Ignore;}
        break;
      case 106: /* resolvetype ::= REPLACE */
{yygotominor.yy4 = OE_Replace;}
        break;
      case 107: /* cmd ::= DROP TABLE ifexists fullname */
{
  sqlite3DropTable(pParse, yymsp[0].minor.yy259, 0, yymsp[-1].minor.yy4);
}
        break;
      case 110: /* cmd ::= createkw temp VIEW ifnotexists nm dbnm AS select */
{
  sqlite3CreateView(pParse, &yymsp[-7].minor.yy0, &yymsp[-3].minor.yy0, &yymsp[-2].minor.yy0, yymsp[0].minor.yy387, yymsp[-6].minor.yy4, yymsp[-4].minor.yy4);
}
        break;
      case 111: /* cmd ::= DROP VIEW ifexists fullname */
{
  sqlite3DropTable(pParse, yymsp[0].minor.yy259, 1, yymsp[-1].minor.yy4);
}
        break;
      case 112: /* cmd ::= select */
{
  SelectDest dest = {SRT_Output, 0, 0, 0, 0};
  sqlite3Select(pParse, yymsp[0].minor.yy387, &dest);
  sqlite3SelectDelete(pParse->db, yymsp[0].minor.yy387);
}
        break;
      case 113: /* select ::= oneselect */
{yygotominor.yy387 = yymsp[0].minor.yy387;}
        break;
      case 114: /* select ::= select multiselect_op oneselect */
{
  if( yymsp[0].minor.yy387 ){
    yymsp[0].minor.yy387->op = (u8)yymsp[-1].minor.yy4;
    yymsp[0].minor.yy387->pPrior = yymsp[-2].minor.yy387;
  }else{
    sqlite3SelectDelete(pParse->db, yymsp[-2].minor.yy387);
  }
  yygotominor.yy387 = yymsp[0].minor.yy387;
}
        break;
      case 116: /* multiselect_op ::= UNION ALL */
{yygotominor.yy4 = TK_ALL;}
        break;
      case 118: /* oneselect ::= SELECT distinct selcollist from where_opt groupby_opt having_opt orderby_opt limit_opt */
{
  yygotominor.yy387 = sqlite3SelectNew(pParse,yymsp[-6].minor.yy322,yymsp[-5].minor.yy259,yymsp[-4].minor.yy314,yymsp[-3].minor.yy322,yymsp[-2].minor.yy314,yymsp[-1].minor.yy322,yymsp[-7].minor.yy4,yymsp[0].minor.yy292.pLimit,yymsp[0].minor.yy292.pOffset);
}
        break;
      case 122: /* sclp ::= selcollist COMMA */
      case 247: /* idxlist_opt ::= LP idxlist RP */ yytestcase(yyruleno==247);
{yygotominor.yy322 = yymsp[-1].minor.yy322;}
        break;
      case 123: /* sclp ::= */
      case 151: /* orderby_opt ::= */ yytestcase(yyruleno==151);
      case 159: /* groupby_opt ::= */ yytestcase(yyruleno==159);
      case 240: /* exprlist ::= */ yytestcase(yyruleno==240);
      case 246: /* idxlist_opt ::= */ yytestcase(yyruleno==246);
{yygotominor.yy322 = 0;}
        break;
      case 124: /* selcollist ::= sclp expr as */
{
   yygotominor.yy322 = sqlite3ExprListAppend(pParse, yymsp[-2].minor.yy322, yymsp[-1].minor.yy118.pExpr);
   if( yymsp[0].minor.yy0.n>0 ) sqlite3ExprListSetName(pParse, yygotominor.yy322, &yymsp[0].minor.yy0, 1);
   sqlite3ExprListSetSpan(pParse,yygotominor.yy322,&yymsp[-1].minor.yy118);
}
        break;
      case 125: /* selcollist ::= sclp STAR */
{
  Expr *p = sqlite3Expr(pParse->db, TK_ALL, 0);
  yygotominor.yy322 = sqlite3ExprListAppend(pParse, yymsp[-1].minor.yy322, p);
}
        break;
      case 126: /* selcollist ::= sclp nm DOT STAR */
{
  Expr *pRight = sqlite3PExpr(pParse, TK_ALL, 0, 0, &yymsp[0].minor.yy0);
  Expr *pLeft = sqlite3PExpr(pParse, TK_ID, 0, 0, &yymsp[-2].minor.yy0);
  Expr *pDot = sqlite3PExpr(pParse, TK_DOT, pLeft, pRight, 0);
  yygotominor.yy322 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy322, pDot);
}
        break;
      case 129: /* as ::= */
{yygotominor.yy0.n = 0;}
        break;
      case 130: /* from ::= */
{yygotominor.yy259 = sqlite3DbMallocZero(pParse->db, sizeof(*yygotominor.yy259));}
        break;
      case 131: /* from ::= FROM seltablist */
{
  yygotominor.yy259 = yymsp[0].minor.yy259;
  sqlite3SrcListShiftJoinType(yygotominor.yy259);
}
        break;
      case 132: /* stl_prefix ::= seltablist joinop */
{
   yygotominor.yy259 = yymsp[-1].minor.yy259;
   if( ALWAYS(yygotominor.yy259 && yygotominor.yy259->nSrc>0) ) yygotominor.yy259->a[yygotominor.yy259->nSrc-1].jointype = (u8)yymsp[0].minor.yy4;
}
        break;
      case 133: /* stl_prefix ::= */
{yygotominor.yy259 = 0;}
        break;
      case 134: /* seltablist ::= stl_prefix nm dbnm as indexed_opt on_opt using_opt */
{
  yygotominor.yy259 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy259,&yymsp[-5].minor.yy0,&yymsp[-4].minor.yy0,&yymsp[-3].minor.yy0,0,yymsp[-1].minor.yy314,yymsp[0].minor.yy384);
  sqlite3SrcListIndexedBy(pParse, yygotominor.yy259, &yymsp[-2].minor.yy0);
}
        break;
      case 135: /* seltablist ::= stl_prefix LP select RP as on_opt using_opt */
{
    yygotominor.yy259 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy259,0,0,&yymsp[-2].minor.yy0,yymsp[-4].minor.yy387,yymsp[-1].minor.yy314,yymsp[0].minor.yy384);
  }
        break;
      case 136: /* seltablist ::= stl_prefix LP seltablist RP as on_opt using_opt */
{
    if( yymsp[-6].minor.yy259==0 && yymsp[-2].minor.yy0.n==0 && yymsp[-1].minor.yy314==0 && yymsp[0].minor.yy384==0 ){
      yygotominor.yy259 = yymsp[-4].minor.yy259;
    }else{
      Select *pSubquery;
      sqlite3SrcListShiftJoinType(yymsp[-4].minor.yy259);
      pSubquery = sqlite3SelectNew(pParse,0,yymsp[-4].minor.yy259,0,0,0,0,0,0,0);
      yygotominor.yy259 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy259,0,0,&yymsp[-2].minor.yy0,pSubquery,yymsp[-1].minor.yy314,yymsp[0].minor.yy384);
    }
  }
        break;
      case 137: /* dbnm ::= */
      case 146: /* indexed_opt ::= */ yytestcase(yyruleno==146);
{yygotominor.yy0.z=0; yygotominor.yy0.n=0;}
        break;
      case 139: /* fullname ::= nm dbnm */
{yygotominor.yy259 = sqlite3SrcListAppend(pParse->db,0,&yymsp[-1].minor.yy0,&yymsp[0].minor.yy0);}
        break;
      case 140: /* joinop ::= COMMA|JOIN */
{ yygotominor.yy4 = JT_INNER; }
        break;
      case 141: /* joinop ::= JOIN_KW JOIN */
{ yygotominor.yy4 = sqlite3JoinType(pParse,&yymsp[-1].minor.yy0,0,0); }
        break;
      case 142: /* joinop ::= JOIN_KW nm JOIN */
{ yygotominor.yy4 = sqlite3JoinType(pParse,&yymsp[-2].minor.yy0,&yymsp[-1].minor.yy0,0); }
        break;
      case 143: /* joinop ::= JOIN_KW nm nm JOIN */
{ yygotominor.yy4 = sqlite3JoinType(pParse,&yymsp[-3].minor.yy0,&yymsp[-2].minor.yy0,&yymsp[-1].minor.yy0); }
        break;
      case 144: /* on_opt ::= ON expr */
      case 155: /* sortitem ::= expr */ yytestcase(yyruleno==155);
      case 162: /* having_opt ::= HAVING expr */ yytestcase(yyruleno==162);
      case 169: /* where_opt ::= WHERE expr */ yytestcase(yyruleno==169);
      case 235: /* case_else ::= ELSE expr */ yytestcase(yyruleno==235);
      case 237: /* case_operand ::= expr */ yytestcase(yyruleno==237);
{yygotominor.yy314 = yymsp[0].minor.yy118.pExpr;}
        break;
      case 145: /* on_opt ::= */
      case 161: /* having_opt ::= */ yytestcase(yyruleno==161);
      case 168: /* where_opt ::= */ yytestcase(yyruleno==168);
      case 236: /* case_else ::= */ yytestcase(yyruleno==236);
      case 238: /* case_operand ::= */ yytestcase(yyruleno==238);
{yygotominor.yy314 = 0;}
        break;
      case 148: /* indexed_opt ::= NOT INDEXED */
{yygotominor.yy0.z=0; yygotominor.yy0.n=1;}
        break;
      case 149: /* using_opt ::= USING LP inscollist RP */
      case 181: /* inscollist_opt ::= LP inscollist RP */ yytestcase(yyruleno==181);
{yygotominor.yy384 = yymsp[-1].minor.yy384;}
        break;
      case 150: /* using_opt ::= */
      case 180: /* inscollist_opt ::= */ yytestcase(yyruleno==180);
{yygotominor.yy384 = 0;}
        break;
      case 152: /* orderby_opt ::= ORDER BY sortlist */
      case 160: /* groupby_opt ::= GROUP BY nexprlist */ yytestcase(yyruleno==160);
      case 239: /* exprlist ::= nexprlist */ yytestcase(yyruleno==239);
{yygotominor.yy322 = yymsp[0].minor.yy322;}
        break;
      case 153: /* sortlist ::= sortlist COMMA sortitem sortorder */
{
  yygotominor.yy322 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy322,yymsp[-1].minor.yy314);
  if( yygotominor.yy322 ) yygotominor.yy322->a[yygotominor.yy322->nExpr-1].sortOrder = (u8)yymsp[0].minor.yy4;
}
        break;
      case 154: /* sortlist ::= sortitem sortorder */
{
  yygotominor.yy322 = sqlite3ExprListAppend(pParse,0,yymsp[-1].minor.yy314);
  if( yygotominor.yy322 && ALWAYS(yygotominor.yy322->a) ) yygotominor.yy322->a[0].sortOrder = (u8)yymsp[0].minor.yy4;
}
        break;
      case 156: /* sortorder ::= ASC */
      case 158: /* sortorder ::= */ yytestcase(yyruleno==158);
{yygotominor.yy4 = SQLITE_SO_ASC;}
        break;
      case 157: /* sortorder ::= DESC */
{yygotominor.yy4 = SQLITE_SO_DESC;}
        break;
      case 163: /* limit_opt ::= */
{yygotominor.yy292.pLimit = 0; yygotominor.yy292.pOffset = 0;}
        break;
      case 164: /* limit_opt ::= LIMIT expr */
{yygotominor.yy292.pLimit = yymsp[0].minor.yy118.pExpr; yygotominor.yy292.pOffset = 0;}
        break;
      case 165: /* limit_opt ::= LIMIT expr OFFSET expr */
{yygotominor.yy292.pLimit = yymsp[-2].minor.yy118.pExpr; yygotominor.yy292.pOffset = yymsp[0].minor.yy118.pExpr;}
        break;
      case 166: /* limit_opt ::= LIMIT expr COMMA expr */
{yygotominor.yy292.pOffset = yymsp[-2].minor.yy118.pExpr; yygotominor.yy292.pLimit = yymsp[0].minor.yy118.pExpr;}
        break;
      case 167: /* cmd ::= DELETE FROM fullname indexed_opt where_opt */
{
  sqlite3SrcListIndexedBy(pParse, yymsp[-2].minor.yy259, &yymsp[-1].minor.yy0);
  sqlite3DeleteFrom(pParse,yymsp[-2].minor.yy259,yymsp[0].minor.yy314);
}
        break;
      case 170: /* cmd ::= UPDATE orconf fullname indexed_opt SET setlist where_opt */
{
  sqlite3SrcListIndexedBy(pParse, yymsp[-4].minor.yy259, &yymsp[-3].minor.yy0);
  sqlite3ExprListCheckLength(pParse,yymsp[-1].minor.yy322,"set list"); 
  sqlite3Update(pParse,yymsp[-4].minor.yy259,yymsp[-1].minor.yy322,yymsp[0].minor.yy314,yymsp[-5].minor.yy210);
}
        break;
      case 171: /* setlist ::= setlist COMMA nm EQ expr */
{
  yygotominor.yy322 = sqlite3ExprListAppend(pParse, yymsp[-4].minor.yy322, yymsp[0].minor.yy118.pExpr);
  sqlite3ExprListSetName(pParse, yygotominor.yy322, &yymsp[-2].minor.yy0, 1);
}
        break;
      case 172: /* setlist ::= nm EQ expr */
{
  yygotominor.yy322 = sqlite3ExprListAppend(pParse, 0, yymsp[0].minor.yy118.pExpr);
  sqlite3ExprListSetName(pParse, yygotominor.yy322, &yymsp[-2].minor.yy0, 1);
}
        break;
      case 173: /* cmd ::= insert_cmd INTO fullname inscollist_opt VALUES LP itemlist RP */
{sqlite3Insert(pParse, yymsp[-5].minor.yy259, yymsp[-1].minor.yy322, 0, yymsp[-4].minor.yy384, yymsp[-7].minor.yy210);}
        break;
      case 174: /* cmd ::= insert_cmd INTO fullname inscollist_opt select */
{sqlite3Insert(pParse, yymsp[-2].minor.yy259, 0, yymsp[0].minor.yy387, yymsp[-1].minor.yy384, yymsp[-4].minor.yy210);}
        break;
      case 175: /* cmd ::= insert_cmd INTO fullname inscollist_opt DEFAULT VALUES */
{sqlite3Insert(pParse, yymsp[-3].minor.yy259, 0, 0, yymsp[-2].minor.yy384, yymsp[-5].minor.yy210);}
        break;
      case 176: /* insert_cmd ::= INSERT orconf */
{yygotominor.yy210 = yymsp[0].minor.yy210;}
        break;
      case 177: /* insert_cmd ::= REPLACE */
{yygotominor.yy210 = OE_Replace;}
        break;
      case 178: /* itemlist ::= itemlist COMMA expr */
      case 241: /* nexprlist ::= nexprlist COMMA expr */ yytestcase(yyruleno==241);
{yygotominor.yy322 = sqlite3ExprListAppend(pParse,yymsp[-2].minor.yy322,yymsp[0].minor.yy118.pExpr);}
        break;
      case 179: /* itemlist ::= expr */
      case 242: /* nexprlist ::= expr */ yytestcase(yyruleno==242);
{yygotominor.yy322 = sqlite3ExprListAppend(pParse,0,yymsp[0].minor.yy118.pExpr);}
        break;
      case 182: /* inscollist ::= inscollist COMMA nm */
{yygotominor.yy384 = sqlite3IdListAppend(pParse->db,yymsp[-2].minor.yy384,&yymsp[0].minor.yy0);}
        break;
      case 183: /* inscollist ::= nm */
{yygotominor.yy384 = sqlite3IdListAppend(pParse->db,0,&yymsp[0].minor.yy0);}
        break;
      case 184: /* expr ::= term */
{yygotominor.yy118 = yymsp[0].minor.yy118;}
        break;
      case 185: /* expr ::= LP expr RP */
{yygotominor.yy118.pExpr = yymsp[-1].minor.yy118.pExpr; spanSet(&yygotominor.yy118,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0);}
        break;
      case 186: /* term ::= NULL */
      case 191: /* term ::= INTEGER|FLOAT|BLOB */ yytestcase(yyruleno==191);
      case 192: /* term ::= STRING */ yytestcase(yyruleno==192);
{spanExpr(&yygotominor.yy118, pParse, yymsp[0].major, &yymsp[0].minor.yy0);}
        break;
      case 187: /* expr ::= id */
      case 188: /* expr ::= JOIN_KW */ yytestcase(yyruleno==188);
{spanExpr(&yygotominor.yy118, pParse, TK_ID, &yymsp[0].minor.yy0);}
        break;
      case 189: /* expr ::= nm DOT nm */
{
  Expr *temp1 = sqlite3PExpr(pParse, TK_ID, 0, 0, &yymsp[-2].minor.yy0);
  Expr *temp2 = sqlite3PExpr(pParse, TK_ID, 0, 0, &yymsp[0].minor.yy0);
  yygotominor.yy118.pExpr = sqlite3PExpr(pParse, TK_DOT, temp1, temp2, 0);
  spanSet(&yygotominor.yy118,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0);
}
        break;
      case 190: /* expr ::= nm DOT nm DOT nm */
{
  Expr *temp1 = sqlite3PExpr(pParse, TK_ID, 0, 0, &yymsp[-4].minor.yy0);
  Expr *temp2 = sqlite3PExpr(pParse, TK_ID, 0, 0, &yymsp[-2].minor.yy0);
  Expr *temp3 = sqlite3PExpr(pParse, TK_ID, 0, 0, &yymsp[0].minor.yy0);
  Expr *temp4 = sqlite3PExpr(pParse, TK_DOT, temp2, temp3, 0);
  yygotominor.yy118.pExpr = sqlite3PExpr(pParse, TK_DOT, temp1, temp4, 0);
  spanSet(&yygotominor.yy118,&yymsp[-4].minor.yy0,&yymsp[0].minor.yy0);
}
        break;
      case 193: /* expr ::= REGISTER */
{
  /* When doing a nested parse, one can include terms in an expression
  ** that look like this:   #1 #2 ...  These terms refer to registers
  ** in the virtual machine.  #N is the N-th register. */
  if( pParse->nested==0 ){
    sqlite3ErrorMsg(pParse, "near \"%T\": syntax error", &yymsp[0].minor.yy0);
    yygotominor.yy118.pExpr = 0;
  }else{
    yygotominor.yy118.pExpr = sqlite3PExpr(pParse, TK_REGISTER, 0, 0, &yymsp[0].minor.yy0);
    if( yygotominor.yy118.pExpr ) sqlite3GetInt32(&yymsp[0].minor.yy0.z[1], &yygotominor.yy118.pExpr->iTable);
  }
  spanSet(&yygotominor.yy118, &yymsp[0].minor.yy0, &yymsp[0].minor.yy0);
}
        break;
      case 194: /* expr ::= VARIABLE */
{
  spanExpr(&yygotominor.yy118, pParse, TK_VARIABLE, &yymsp[0].minor.yy0);
  sqlite3ExprAssignVarNumber(pParse, yygotominor.yy118.pExpr);
  spanSet(&yygotominor.yy118, &yymsp[0].minor.yy0, &yymsp[0].minor.yy0);
}
        break;
      case 195: /* expr ::= expr COLLATE ids */
{
  yygotominor.yy118.pExpr = sqlite3ExprSetCollByToken(pParse, yymsp[-2].minor.yy118.pExpr, &yymsp[0].minor.yy0);
  yygotominor.yy118.zStart = yymsp[-2].minor.yy118.zStart;
  yygotominor.yy118.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
}
        break;
      case 196: /* expr ::= CAST LP expr AS typetoken RP */
{
  yygotominor.yy118.pExpr = sqlite3PExpr(pParse, TK_CAST, yymsp[-3].minor.yy118.pExpr, 0, &yymsp[-1].minor.yy0);
  spanSet(&yygotominor.yy118,&yymsp[-5].minor.yy0,&yymsp[0].minor.yy0);
}
        break;
      case 197: /* expr ::= ID LP distinct exprlist RP */
{
  if( yymsp[-1].minor.yy322 && yymsp[-1].minor.yy322->nExpr>pParse->db->aLimit[SQLITE_LIMIT_FUNCTION_ARG] ){
    sqlite3ErrorMsg(pParse, "too many arguments on function %T", &yymsp[-4].minor.yy0);
  }
  yygotominor.yy118.pExpr = sqlite3ExprFunction(pParse, yymsp[-1].minor.yy322, &yymsp[-4].minor.yy0);
  spanSet(&yygotominor.yy118,&yymsp[-4].minor.yy0,&yymsp[0].minor.yy0);
  if( yymsp[-2].minor.yy4 && yygotominor.yy118.pExpr ){
    yygotominor.yy118.pExpr->flags |= EP_Distinct;
  }
}
        break;
      case 198: /* expr ::= ID LP STAR RP */
{
  yygotominor.yy118.pExpr = sqlite3ExprFunction(pParse, 0, &yymsp[-3].minor.yy0);
  spanSet(&yygotominor.yy118,&yymsp[-3].minor.yy0,&yymsp[0].minor.yy0);
}
        break;
      case 199: /* term ::= CTIME_KW */
{
  /* The CURRENT_TIME, CURRENT_DATE, and CURRENT_TIMESTAMP values are
  ** treated as functions that return constants */
  yygotominor.yy118.pExpr = sqlite3ExprFunction(pParse, 0,&yymsp[0].minor.yy0);
  if( yygotominor.yy118.pExpr ){
    yygotominor.yy118.pExpr->op = TK_CONST_FUNC;  
  }
  spanSet(&yygotominor.yy118, &yymsp[0].minor.yy0, &yymsp[0].minor.yy0);
}
        break;
      case 200: /* expr ::= expr AND expr */
      case 201: /* expr ::= expr OR expr */ yytestcase(yyruleno==201);
      case 202: /* expr ::= expr LT|GT|GE|LE expr */ yytestcase(yyruleno==202);
      case 203: /* expr ::= expr EQ|NE expr */ yytestcase(yyruleno==203);
      case 204: /* expr ::= expr BITAND|BITOR|LSHIFT|RSHIFT expr */ yytestcase(yyruleno==204);
      case 205: /* expr ::= expr PLUS|MINUS expr */ yytestcase(yyruleno==205);
      case 206: /* expr ::= expr STAR|SLASH|REM expr */ yytestcase(yyruleno==206);
      case 207: /* expr ::= expr CONCAT expr */ yytestcase(yyruleno==207);
{spanBinaryExpr(&yygotominor.yy118,pParse,yymsp[-1].major,&yymsp[-2].minor.yy118,&yymsp[0].minor.yy118);}
        break;
      case 208: /* likeop ::= LIKE_KW */
      case 210: /* likeop ::= MATCH */ yytestcase(yyruleno==210);
{yygotominor.yy342.eOperator = yymsp[0].minor.yy0; yygotominor.yy342.not = 0;}
        break;
      case 209: /* likeop ::= NOT LIKE_KW */
      case 211: /* likeop ::= NOT MATCH */ yytestcase(yyruleno==211);
{yygotominor.yy342.eOperator = yymsp[0].minor.yy0; yygotominor.yy342.not = 1;}
        break;
      case 212: /* expr ::= expr likeop expr */
{
  ExprList *pList;
  pList = sqlite3ExprListAppend(pParse,0, yymsp[0].minor.yy118.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[-2].minor.yy118.pExpr);
  yygotominor.yy118.pExpr = sqlite3ExprFunction(pParse, pList, &yymsp[-1].minor.yy342.eOperator);
  if( yymsp[-1].minor.yy342.not ) yygotominor.yy118.pExpr = sqlite3PExpr(pParse, TK_NOT, yygotominor.yy118.pExpr, 0, 0);
  yygotominor.yy118.zStart = yymsp[-2].minor.yy118.zStart;
  yygotominor.yy118.zEnd = yymsp[0].minor.yy118.zEnd;
  if( yygotominor.yy118.pExpr ) yygotominor.yy118.pExpr->flags |= EP_InfixFunc;
}
        break;
      case 213: /* expr ::= expr likeop expr ESCAPE expr */
{
  ExprList *pList;
  pList = sqlite3ExprListAppend(pParse,0, yymsp[-2].minor.yy118.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[-4].minor.yy118.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[0].minor.yy118.pExpr);
  yygotominor.yy118.pExpr = sqlite3ExprFunction(pParse, pList, &yymsp[-3].minor.yy342.eOperator);
  if( yymsp[-3].minor.yy342.not ) yygotominor.yy118.pExpr = sqlite3PExpr(pParse, TK_NOT, yygotominor.yy118.pExpr, 0, 0);
  yygotominor.yy118.zStart = yymsp[-4].minor.yy118.zStart;
  yygotominor.yy118.zEnd = yymsp[0].minor.yy118.zEnd;
  if( yygotominor.yy118.pExpr ) yygotominor.yy118.pExpr->flags |= EP_InfixFunc;
}
        break;
      case 214: /* expr ::= expr ISNULL|NOTNULL */
{spanUnaryPostfix(&yygotominor.yy118,pParse,yymsp[0].major,&yymsp[-1].minor.yy118,&yymsp[0].minor.yy0);}
        break;
      case 215: /* expr ::= expr NOT NULL */
{spanUnaryPostfix(&yygotominor.yy118,pParse,TK_NOTNULL,&yymsp[-2].minor.yy118,&yymsp[0].minor.yy0);}
        break;
      case 216: /* expr ::= expr IS expr */
{
  spanBinaryExpr(&yygotominor.yy118,pParse,TK_IS,&yymsp[-2].minor.yy118,&yymsp[0].minor.yy118);
  binaryToUnaryIfNull(pParse, yymsp[0].minor.yy118.pExpr, yygotominor.yy118.pExpr, TK_ISNULL);
}
        break;
      case 217: /* expr ::= expr IS NOT expr */
{
  spanBinaryExpr(&yygotominor.yy118,pParse,TK_ISNOT,&yymsp[-3].minor.yy118,&yymsp[0].minor.yy118);
  binaryToUnaryIfNull(pParse, yymsp[0].minor.yy118.pExpr, yygotominor.yy118.pExpr, TK_NOTNULL);
}
        break;
      case 218: /* expr ::= NOT expr */
      case 219: /* expr ::= BITNOT expr */ yytestcase(yyruleno==219);
{spanUnaryPrefix(&yygotominor.yy118,pParse,yymsp[-1].major,&yymsp[0].minor.yy118,&yymsp[-1].minor.yy0);}
        break;
      case 220: /* expr ::= MINUS expr */
{spanUnaryPrefix(&yygotominor.yy118,pParse,TK_UMINUS,&yymsp[0].minor.yy118,&yymsp[-1].minor.yy0);}
        break;
      case 221: /* expr ::= PLUS expr */
{spanUnaryPrefix(&yygotominor.yy118,pParse,TK_UPLUS,&yymsp[0].minor.yy118,&yymsp[-1].minor.yy0);}
        break;
      case 224: /* expr ::= expr between_op expr AND expr */
{
  ExprList *pList = sqlite3ExprListAppend(pParse,0, yymsp[-2].minor.yy118.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[0].minor.yy118.pExpr);
  yygotominor.yy118.pExpr = sqlite3PExpr(pParse, TK_BETWEEN, yymsp[-4].minor.yy118.pExpr, 0, 0);
  if( yygotominor.yy118.pExpr ){
    yygotominor.yy118.pExpr->x.pList = pList;
  }else{
    sqlite3ExprListDelete(pParse->db, pList);
  } 
  if( yymsp[-3].minor.yy4 ) yygotominor.yy118.pExpr = sqlite3PExpr(pParse, TK_NOT, yygotominor.yy118.pExpr, 0, 0);
  yygotominor.yy118.zStart = yymsp[-4].minor.yy118.zStart;
  yygotominor.yy118.zEnd = yymsp[0].minor.yy118.zEnd;
}
        break;
      case 227: /* expr ::= expr in_op LP exprlist RP */
{
    if( yymsp[-1].minor.yy322==0 ){
      /* Expressions of the form
      **
      **      expr1 IN ()
      **      expr1 NOT IN ()
      **
      ** simplify to constants 0 (false) and 1 (true), respectively,
      ** regardless of the value of expr1.
      */
      yygotominor.yy118.pExpr = sqlite3PExpr(pParse, TK_INTEGER, 0, 0, &sqlite3IntTokens[yymsp[-3].minor.yy4]);
      sqlite3ExprDelete(pParse->db, yymsp[-4].minor.yy118.pExpr);
    }else{
      yygotominor.yy118.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-4].minor.yy118.pExpr, 0, 0);
      if( yygotominor.yy118.pExpr ){
        yygotominor.yy118.pExpr->x.pList = yymsp[-1].minor.yy322;
        sqlite3ExprSetHeight(pParse, yygotominor.yy118.pExpr);
      }else{
        sqlite3ExprListDelete(pParse->db, yymsp[-1].minor.yy322);
      }
      if( yymsp[-3].minor.yy4 ) yygotominor.yy118.pExpr = sqlite3PExpr(pParse, TK_NOT, yygotominor.yy118.pExpr, 0, 0);
    }
    yygotominor.yy118.zStart = yymsp[-4].minor.yy118.zStart;
    yygotominor.yy118.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
  }
        break;
      case 228: /* expr ::= LP select RP */
{
    yygotominor.yy118.pExpr = sqlite3PExpr(pParse, TK_SELECT, 0, 0, 0);
    if( yygotominor.yy118.pExpr ){
      yygotominor.yy118.pExpr->x.pSelect = yymsp[-1].minor.yy387;
      ExprSetProperty(yygotominor.yy118.pExpr, EP_xIsSelect);
      sqlite3ExprSetHeight(pParse, yygotominor.yy118.pExpr);
    }else{
      sqlite3SelectDelete(pParse->db, yymsp[-1].minor.yy387);
    }
    yygotominor.yy118.zStart = yymsp[-2].minor.yy0.z;
    yygotominor.yy118.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
  }
        break;
      case 229: /* expr ::= expr in_op LP select RP */
{
    yygotominor.yy118.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-4].minor.yy118.pExpr, 0, 0);
    if( yygotominor.yy118.pExpr ){
      yygotominor.yy118.pExpr->x.pSelect = yymsp[-1].minor.yy387;
      ExprSetProperty(yygotominor.yy118.pExpr, EP_xIsSelect);
      sqlite3ExprSetHeight(pParse, yygotominor.yy118.pExpr);
    }else{
      sqlite3SelectDelete(pParse->db, yymsp[-1].minor.yy387);
    }
    if( yymsp[-3].minor.yy4 ) yygotominor.yy118.pExpr = sqlite3PExpr(pParse, TK_NOT, yygotominor.yy118.pExpr, 0, 0);
    yygotominor.yy118.zStart = yymsp[-4].minor.yy118.zStart;
    yygotominor.yy118.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
  }
        break;
      case 230: /* expr ::= expr in_op nm dbnm */
{
    SrcList *pSrc = sqlite3SrcListAppend(pParse->db, 0,&yymsp[-1].minor.yy0,&yymsp[0].minor.yy0);
    yygotominor.yy118.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-3].minor.yy118.pExpr, 0, 0);
    if( yygotominor.yy118.pExpr ){
      yygotominor.yy118.pExpr->x.pSelect = sqlite3SelectNew(pParse, 0,pSrc,0,0,0,0,0,0,0);
      ExprSetProperty(yygotominor.yy118.pExpr, EP_xIsSelect);
      sqlite3ExprSetHeight(pParse, yygotominor.yy118.pExpr);
    }else{
      sqlite3SrcListDelete(pParse->db, pSrc);
    }
    if( yymsp[-2].minor.yy4 ) yygotominor.yy118.pExpr = sqlite3PExpr(pParse, TK_NOT, yygotominor.yy118.pExpr, 0, 0);
    yygotominor.yy118.zStart = yymsp[-3].minor.yy118.zStart;
    yygotominor.yy118.zEnd = yymsp[0].minor.yy0.z ? &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n] : &yymsp[-1].minor.yy0.z[yymsp[-1].minor.yy0.n];
  }
        break;
      case 231: /* expr ::= EXISTS LP select RP */
{
    Expr *p = yygotominor.yy118.pExpr = sqlite3PExpr(pParse, TK_EXISTS, 0, 0, 0);
    if( p ){
      p->x.pSelect = yymsp[-1].minor.yy387;
      ExprSetProperty(p, EP_xIsSelect);
      sqlite3ExprSetHeight(pParse, p);
    }else{
      sqlite3SelectDelete(pParse->db, yymsp[-1].minor.yy387);
    }
    yygotominor.yy118.zStart = yymsp[-3].minor.yy0.z;
    yygotominor.yy118.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
  }
        break;
      case 232: /* expr ::= CASE case_operand case_exprlist case_else END */
{
  yygotominor.yy118.pExpr = sqlite3PExpr(pParse, TK_CASE, yymsp[-3].minor.yy314, yymsp[-1].minor.yy314, 0);
  if( yygotominor.yy118.pExpr ){
    yygotominor.yy118.pExpr->x.pList = yymsp[-2].minor.yy322;
    sqlite3ExprSetHeight(pParse, yygotominor.yy118.pExpr);
  }else{
    sqlite3ExprListDelete(pParse->db, yymsp[-2].minor.yy322);
  }
  yygotominor.yy118.zStart = yymsp[-4].minor.yy0.z;
  yygotominor.yy118.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
}
        break;
      case 233: /* case_exprlist ::= case_exprlist WHEN expr THEN expr */
{
  yygotominor.yy322 = sqlite3ExprListAppend(pParse,yymsp[-4].minor.yy322, yymsp[-2].minor.yy118.pExpr);
  yygotominor.yy322 = sqlite3ExprListAppend(pParse,yygotominor.yy322, yymsp[0].minor.yy118.pExpr);
}
        break;
      case 234: /* case_exprlist ::= WHEN expr THEN expr */
{
  yygotominor.yy322 = sqlite3ExprListAppend(pParse,0, yymsp[-2].minor.yy118.pExpr);
  yygotominor.yy322 = sqlite3ExprListAppend(pParse,yygotominor.yy322, yymsp[0].minor.yy118.pExpr);
}
        break;
      case 243: /* cmd ::= createkw uniqueflag INDEX ifnotexists nm dbnm ON nm LP idxlist RP */
{
  sqlite3CreateIndex(pParse, &yymsp[-6].minor.yy0, &yymsp[-5].minor.yy0, 
                     sqlite3SrcListAppend(pParse->db,0,&yymsp[-3].minor.yy0,0), yymsp[-1].minor.yy322, yymsp[-9].minor.yy4,
                      &yymsp[-10].minor.yy0, &yymsp[0].minor.yy0, SQLITE_SO_ASC, yymsp[-7].minor.yy4);
}
        break;
      case 244: /* uniqueflag ::= UNIQUE */
      case 298: /* raisetype ::= ABORT */ yytestcase(yyruleno==298);
{yygotominor.yy4 = OE_Abort;}
        break;
      case 245: /* uniqueflag ::= */
{yygotominor.yy4 = OE_None;}
        break;
      case 248: /* idxlist ::= idxlist COMMA nm collate sortorder */
{
  Expr *p = 0;
  if( yymsp[-1].minor.yy0.n>0 ){
    p = sqlite3Expr(pParse->db, TK_COLUMN, 0);
    sqlite3ExprSetCollByToken(pParse, p, &yymsp[-1].minor.yy0);
  }
  yygotominor.yy322 = sqlite3ExprListAppend(pParse,yymsp[-4].minor.yy322, p);
  sqlite3ExprListSetName(pParse,yygotominor.yy322,&yymsp[-2].minor.yy0,1);
  sqlite3ExprListCheckLength(pParse, yygotominor.yy322, "index");
  if( yygotominor.yy322 ) yygotominor.yy322->a[yygotominor.yy322->nExpr-1].sortOrder = (u8)yymsp[0].minor.yy4;
}
        break;
      case 249: /* idxlist ::= nm collate sortorder */
{
  Expr *p = 0;
  if( yymsp[-1].minor.yy0.n>0 ){
    p = sqlite3PExpr(pParse, TK_COLUMN, 0, 0, 0);
    sqlite3ExprSetCollByToken(pParse, p, &yymsp[-1].minor.yy0);
  }
  yygotominor.yy322 = sqlite3ExprListAppend(pParse,0, p);
  sqlite3ExprListSetName(pParse, yygotominor.yy322, &yymsp[-2].minor.yy0, 1);
  sqlite3ExprListCheckLength(pParse, yygotominor.yy322, "index");
  if( yygotominor.yy322 ) yygotominor.yy322->a[yygotominor.yy322->nExpr-1].sortOrder = (u8)yymsp[0].minor.yy4;
}
        break;
      case 250: /* collate ::= */
{yygotominor.yy0.z = 0; yygotominor.yy0.n = 0;}
        break;
      case 252: /* cmd ::= DROP INDEX ifexists fullname */
{sqlite3DropIndex(pParse, yymsp[0].minor.yy259, yymsp[-1].minor.yy4);}
        break;
      case 253: /* cmd ::= VACUUM */
      case 254: /* cmd ::= VACUUM nm */ yytestcase(yyruleno==254);
{sqlite3Vacuum(pParse);}
        break;
      case 255: /* cmd ::= PRAGMA nm dbnm */
{sqlite3Pragma(pParse,&yymsp[-1].minor.yy0,&yymsp[0].minor.yy0,0,0);}
        break;
      case 256: /* cmd ::= PRAGMA nm dbnm EQ nmnum */
{sqlite3Pragma(pParse,&yymsp[-3].minor.yy0,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0,0);}
        break;
      case 257: /* cmd ::= PRAGMA nm dbnm LP nmnum RP */
{sqlite3Pragma(pParse,&yymsp[-4].minor.yy0,&yymsp[-3].minor.yy0,&yymsp[-1].minor.yy0,0);}
        break;
      case 258: /* cmd ::= PRAGMA nm dbnm EQ minus_num */
{sqlite3Pragma(pParse,&yymsp[-3].minor.yy0,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0,1);}
        break;
      case 259: /* cmd ::= PRAGMA nm dbnm LP minus_num RP */
{sqlite3Pragma(pParse,&yymsp[-4].minor.yy0,&yymsp[-3].minor.yy0,&yymsp[-1].minor.yy0,1);}
        break;
      case 270: /* cmd ::= createkw trigger_decl BEGIN trigger_cmd_list END */
{
  Token all;
  all.z = yymsp[-3].minor.yy0.z;
  all.n = (int)(yymsp[0].minor.yy0.z - yymsp[-3].minor.yy0.z) + yymsp[0].minor.yy0.n;
  sqlite3FinishTrigger(pParse, yymsp[-1].minor.yy203, &all);
}
        break;
      case 271: /* trigger_decl ::= temp TRIGGER ifnotexists nm dbnm trigger_time trigger_event ON fullname foreach_clause when_clause */
{
  sqlite3BeginTrigger(pParse, &yymsp[-7].minor.yy0, &yymsp[-6].minor.yy0, yymsp[-5].minor.yy4, yymsp[-4].minor.yy90.a, yymsp[-4].minor.yy90.b, yymsp[-2].minor.yy259, yymsp[0].minor.yy314, yymsp[-10].minor.yy4, yymsp[-8].minor.yy4);
  yygotominor.yy0 = (yymsp[-6].minor.yy0.n==0?yymsp[-7].minor.yy0:yymsp[-6].minor.yy0);
}
        break;
      case 272: /* trigger_time ::= BEFORE */
      case 275: /* trigger_time ::= */ yytestcase(yyruleno==275);
{ yygotominor.yy4 = TK_BEFORE; }
        break;
      case 273: /* trigger_time ::= AFTER */
{ yygotominor.yy4 = TK_AFTER;  }
        break;
      case 274: /* trigger_time ::= INSTEAD OF */
{ yygotominor.yy4 = TK_INSTEAD;}
        break;
      case 276: /* trigger_event ::= DELETE|INSERT */
      case 277: /* trigger_event ::= UPDATE */ yytestcase(yyruleno==277);
{yygotominor.yy90.a = yymsp[0].major; yygotominor.yy90.b = 0;}
        break;
      case 278: /* trigger_event ::= UPDATE OF inscollist */
{yygotominor.yy90.a = TK_UPDATE; yygotominor.yy90.b = yymsp[0].minor.yy384;}
        break;
      case 281: /* when_clause ::= */
      case 303: /* key_opt ::= */ yytestcase(yyruleno==303);
{ yygotominor.yy314 = 0; }
        break;
      case 282: /* when_clause ::= WHEN expr */
      case 304: /* key_opt ::= KEY expr */ yytestcase(yyruleno==304);
{ yygotominor.yy314 = yymsp[0].minor.yy118.pExpr; }
        break;
      case 283: /* trigger_cmd_list ::= trigger_cmd_list trigger_cmd SEMI */
{
  assert( yymsp[-2].minor.yy203!=0 );
  yymsp[-2].minor.yy203->pLast->pNext = yymsp[-1].minor.yy203;
  yymsp[-2].minor.yy203->pLast = yymsp[-1].minor.yy203;
  yygotominor.yy203 = yymsp[-2].minor.yy203;
}
        break;
      case 284: /* trigger_cmd_list ::= trigger_cmd SEMI */
{ 
  assert( yymsp[-1].minor.yy203!=0 );
  yymsp[-1].minor.yy203->pLast = yymsp[-1].minor.yy203;
  yygotominor.yy203 = yymsp[-1].minor.yy203;
}
        break;
      case 286: /* trnm ::= nm DOT nm */
{
  yygotominor.yy0 = yymsp[0].minor.yy0;
  sqlite3ErrorMsg(pParse, 
        "qualified table names are not allowed on INSERT, UPDATE, and DELETE "
        "statements within triggers");
}
        break;
      case 288: /* tridxby ::= INDEXED BY nm */
{
  sqlite3ErrorMsg(pParse,
        "the INDEXED BY clause is not allowed on UPDATE or DELETE statements "
        "within triggers");
}
        break;
      case 289: /* tridxby ::= NOT INDEXED */
{
  sqlite3ErrorMsg(pParse,
        "the NOT INDEXED clause is not allowed on UPDATE or DELETE statements "
        "within triggers");
}
        break;
      case 290: /* trigger_cmd ::= UPDATE orconf trnm tridxby SET setlist where_opt */
{ yygotominor.yy203 = sqlite3TriggerUpdateStep(pParse->db, &yymsp[-4].minor.yy0, yymsp[-1].minor.yy322, yymsp[0].minor.yy314, yymsp[-5].minor.yy210); }
        break;
      case 291: /* trigger_cmd ::= insert_cmd INTO trnm inscollist_opt VALUES LP itemlist RP */
{yygotominor.yy203 = sqlite3TriggerInsertStep(pParse->db, &yymsp[-5].minor.yy0, yymsp[-4].minor.yy384, yymsp[-1].minor.yy322, 0, yymsp[-7].minor.yy210);}
        break;
      case 292: /* trigger_cmd ::= insert_cmd INTO trnm inscollist_opt select */
{yygotominor.yy203 = sqlite3TriggerInsertStep(pParse->db, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy384, 0, yymsp[0].minor.yy387, yymsp[-4].minor.yy210);}
        break;
      case 293: /* trigger_cmd ::= DELETE FROM trnm tridxby where_opt */
{yygotominor.yy203 = sqlite3TriggerDeleteStep(pParse->db, &yymsp[-2].minor.yy0, yymsp[0].minor.yy314);}
        break;
      case 294: /* trigger_cmd ::= select */
{yygotominor.yy203 = sqlite3TriggerSelectStep(pParse->db, yymsp[0].minor.yy387); }
        break;
      case 295: /* expr ::= RAISE LP IGNORE RP */
{
  yygotominor.yy118.pExpr = sqlite3PExpr(pParse, TK_RAISE, 0, 0, 0); 
  if( yygotominor.yy118.pExpr ){
    yygotominor.yy118.pExpr->affinity = OE_Ignore;
  }
  yygotominor.yy118.zStart = yymsp[-3].minor.yy0.z;
  yygotominor.yy118.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
}
        break;
      case 296: /* expr ::= RAISE LP raisetype COMMA nm RP */
{
  yygotominor.yy118.pExpr = sqlite3PExpr(pParse, TK_RAISE, 0, 0, &yymsp[-1].minor.yy0); 
  if( yygotominor.yy118.pExpr ) {
    yygotominor.yy118.pExpr->affinity = (char)yymsp[-3].minor.yy4;
  }
  yygotominor.yy118.zStart = yymsp[-5].minor.yy0.z;
  yygotominor.yy118.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
}
        break;
      case 297: /* raisetype ::= ROLLBACK */
{yygotominor.yy4 = OE_Rollback;}
        break;
      case 299: /* raisetype ::= FAIL */
{yygotominor.yy4 = OE_Fail;}
        break;
      case 300: /* cmd ::= DROP TRIGGER ifexists fullname */
{
  sqlite3DropTrigger(pParse,yymsp[0].minor.yy259,yymsp[-1].minor.yy4);
}
        break;
      case 301: /* cmd ::= ATTACH database_kw_opt expr AS expr key_opt */
{
  sqlite3Attach(pParse, yymsp[-3].minor.yy118.pExpr, yymsp[-1].minor.yy118.pExpr, yymsp[0].minor.yy314);
}
        break;
      case 302: /* cmd ::= DETACH database_kw_opt expr */
{
  sqlite3Detach(pParse, yymsp[0].minor.yy118.pExpr);
}
        break;
      case 307: /* cmd ::= REINDEX */
{sqlite3Reindex(pParse, 0, 0);}
        break;
      case 308: /* cmd ::= REINDEX nm dbnm */
{sqlite3Reindex(pParse, &yymsp[-1].minor.yy0, &yymsp[0].minor.yy0);}
        break;
      case 309: /* cmd ::= ANALYZE */
{sqlite3Analyze(pParse, 0, 0);}
        break;
      case 310: /* cmd ::= ANALYZE nm dbnm */
{sqlite3Analyze(pParse, &yymsp[-1].minor.yy0, &yymsp[0].minor.yy0);}
        break;
      case 311: /* cmd ::= ALTER TABLE fullname RENAME TO nm */
{
  sqlite3AlterRenameTable(pParse,yymsp[-3].minor.yy259,&yymsp[0].minor.yy0);
}
        break;
      case 312: /* cmd ::= ALTER TABLE add_column_fullname ADD kwcolumn_opt column */
{
  sqlite3AlterFinishAddColumn(pParse, &yymsp[0].minor.yy0);
}
        break;
      case 313: /* add_column_fullname ::= fullname */
{
  pParse->db->lookaside.bEnabled = 0;
  sqlite3AlterBeginAddColumn(pParse, yymsp[0].minor.yy259);
}
        break;
      case 316: /* cmd ::= create_vtab */
{sqlite3VtabFinishParse(pParse,0);}
        break;
      case 317: /* cmd ::= create_vtab LP vtabarglist RP */
{sqlite3VtabFinishParse(pParse,&yymsp[0].minor.yy0);}
        break;
      case 318: /* create_vtab ::= createkw VIRTUAL TABLE nm dbnm USING nm */
{
    sqlite3VtabBeginParse(pParse, &yymsp[-3].minor.yy0, &yymsp[-2].minor.yy0, &yymsp[0].minor.yy0);
}
        break;
      case 321: /* vtabarg ::= */
{sqlite3VtabArgInit(pParse);}
        break;
      case 323: /* vtabargtoken ::= ANY */
      case 324: /* vtabargtoken ::= lp anylist RP */ yytestcase(yyruleno==324);
      case 325: /* lp ::= LP */ yytestcase(yyruleno==325);
{sqlite3VtabArgExtend(pParse,&yymsp[0].minor.yy0);}
        break;
      default:
      /* (0) input ::= cmdlist */ yytestcase(yyruleno==0);
      /* (1) cmdlist ::= cmdlist ecmd */ yytestcase(yyruleno==1);
      /* (2) cmdlist ::= ecmd */ yytestcase(yyruleno==2);
      /* (3) ecmd ::= SEMI */ yytestcase(yyruleno==3);
      /* (4) ecmd ::= explain cmdx SEMI */ yytestcase(yyruleno==4);
      /* (10) trans_opt ::= */ yytestcase(yyruleno==10);
      /* (11) trans_opt ::= TRANSACTION */ yytestcase(yyruleno==11);
      /* (12) trans_opt ::= TRANSACTION nm */ yytestcase(yyruleno==12);
      /* (20) savepoint_opt ::= SAVEPOINT */ yytestcase(yyruleno==20);
      /* (21) savepoint_opt ::= */ yytestcase(yyruleno==21);
      /* (25) cmd ::= create_table create_table_args */ yytestcase(yyruleno==25);
      /* (34) columnlist ::= columnlist COMMA column */ yytestcase(yyruleno==34);
      /* (35) columnlist ::= column */ yytestcase(yyruleno==35);
      /* (44) type ::= */ yytestcase(yyruleno==44);
      /* (51) signed ::= plus_num */ yytestcase(yyruleno==51);
      /* (52) signed ::= minus_num */ yytestcase(yyruleno==52);
      /* (53) carglist ::= carglist carg */ yytestcase(yyruleno==53);
      /* (54) carglist ::= */ yytestcase(yyruleno==54);
      /* (55) carg ::= CONSTRAINT nm ccons */ yytestcase(yyruleno==55);
      /* (56) carg ::= ccons */ yytestcase(yyruleno==56);
      /* (62) ccons ::= NULL onconf */ yytestcase(yyruleno==62);
      /* (90) conslist ::= conslist COMMA tcons */ yytestcase(yyruleno==90);
      /* (91) conslist ::= conslist tcons */ yytestcase(yyruleno==91);
      /* (92) conslist ::= tcons */ yytestcase(yyruleno==92);
      /* (93) tcons ::= CONSTRAINT nm */ yytestcase(yyruleno==93);
      /* (268) plus_opt ::= PLUS */ yytestcase(yyruleno==268);
      /* (269) plus_opt ::= */ yytestcase(yyruleno==269);
      /* (279) foreach_clause ::= */ yytestcase(yyruleno==279);
      /* (280) foreach_clause ::= FOR EACH ROW */ yytestcase(yyruleno==280);
      /* (287) tridxby ::= */ yytestcase(yyruleno==287);
      /* (305) database_kw_opt ::= DATABASE */ yytestcase(yyruleno==305);
      /* (306) database_kw_opt ::= */ yytestcase(yyruleno==306);
      /* (314) kwcolumn_opt ::= */ yytestcase(yyruleno==314);
      /* (315) kwcolumn_opt ::= COLUMNKW */ yytestcase(yyruleno==315);
      /* (319) vtabarglist ::= vtabarg */ yytestcase(yyruleno==319);
      /* (320) vtabarglist ::= vtabarglist COMMA vtabarg */ yytestcase(yyruleno==320);
      /* (322) vtabarg ::= vtabarg vtabargtoken */ yytestcase(yyruleno==322);
      /* (326) anylist ::= */ yytestcase(yyruleno==326);
      /* (327) anylist ::= anylist LP anylist RP */ yytestcase(yyruleno==327);
      /* (328) anylist ::= anylist ANY */ yytestcase(yyruleno==328);
        break;
  };
  yygoto = yyRuleInfo[yyruleno].lhs;
  yysize = yyRuleInfo[yyruleno].nrhs;
  yypParser->yyidx -= yysize;
  yyact = yy_find_reduce_action(yymsp[-yysize].stateno,(YYCODETYPE)yygoto);
  if( yyact < YYNSTATE ){
#ifdef NDEBUG
    /* If we are not debugging and the reduce action popped at least
    ** one element off the stack, then we can push the new element back
    ** onto the stack here, and skip the stack overflow test in yy_shift().
    ** That gives a significant speed improvement. */
    if( yysize ){
      yypParser->yyidx++;
      yymsp -= yysize-1;
      yymsp->stateno = (YYACTIONTYPE)yyact;
      yymsp->major = (YYCODETYPE)yygoto;
      yymsp->minor = yygotominor;
    }else
#endif
    {
      yy_shift(yypParser,yyact,yygoto,&yygotominor);
    }
  }else{
    assert( yyact == YYNSTATE + YYNRULE + 1 );
    yy_accept(yypParser);
  }
}

/*
** The following code executes when the parse fails
*/
#ifndef YYNOERRORRECOVERY
static void yy_parse_failed(
  yyParser *yypParser           /* The parser */
){
  sqlite3ParserARG_FETCH;
#ifndef NDEBUG
  if( yyTraceFILE ){
    fprintf(yyTraceFILE,"%sFail!\n",yyTracePrompt);
  }
#endif
  while( yypParser->yyidx>=0 ) yy_pop_parser_stack(yypParser);
  /* Here code is inserted which will be executed whenever the
  ** parser fails */
  sqlite3ParserARG_STORE; /* Suppress warning about unused %extra_argument variable */
}
#endif /* YYNOERRORRECOVERY */

/*
** The following code executes when a syntax error first occurs.
*/
static void yy_syntax_error(
  yyParser *yypParser,           /* The parser */
  int yymajor,                   /* The major type of the error token */
  YYMINORTYPE yyminor            /* The minor type of the error token */
){
  sqlite3ParserARG_FETCH;
#define TOKEN (yyminor.yy0)

  UNUSED_PARAMETER(yymajor);  /* Silence some compiler warnings */
  assert( TOKEN.z[0] );  /* The tokenizer always gives us a token */
  sqlite3ErrorMsg(pParse, "near \"%T\": syntax error", &TOKEN);
  pParse->parseError = 1;
  sqlite3ParserARG_STORE; /* Suppress warning about unused %extra_argument variable */
}

/*
** The following is executed when the parser accepts
*/
static void yy_accept(
  yyParser *yypParser           /* The parser */
){
  sqlite3ParserARG_FETCH;
#ifndef NDEBUG
  if( yyTraceFILE ){
    fprintf(yyTraceFILE,"%sAccept!\n",yyTracePrompt);
  }
#endif
  while( yypParser->yyidx>=0 ) yy_pop_parser_stack(yypParser);
  /* Here code is inserted which will be executed whenever the
  ** parser accepts */
  sqlite3ParserARG_STORE; /* Suppress warning about unused %extra_argument variable */
}

/* The main parser program.
** The first argument is a pointer to a structure obtained from
** "sqlite3ParserAlloc" which describes the current state of the parser.
** The second argument is the major token number.  The third is
** the minor token.  The fourth optional argument is whatever the
** user wants (and specified in the grammar) and is available for
** use by the action routines.
**
** Inputs:
** <ul>
** <li> A pointer to the parser (an opaque structure.)
** <li> The major token number.
** <li> The minor token number.
** <li> An option argument of a grammar-specified type.
** </ul>
**
** Outputs:
** None.
*/
SQLITE_PRIVATE void sqlite3Parser(
  void *yyp,                   /* The parser */
  int yymajor,                 /* The major token code number */
  sqlite3ParserTOKENTYPE yyminor       /* The value for the token */
  sqlite3ParserARG_PDECL               /* Optional %extra_argument parameter */
){
  YYMINORTYPE yyminorunion;
  int yyact;            /* The parser action. */
#if !defined(YYERRORSYMBOL) && !defined(YYNOERRORRECOVERY)
  int yyendofinput;     /* True if we are at the end of input */
#endif
#ifdef YYERRORSYMBOL
  int yyerrorhit = 0;   /* True if yymajor has invoked an error */
#endif
  yyParser *yypParser;  /* The parser */

  /* (re)initialize the parser, if necessary */
  yypParser = (yyParser*)yyp;
  if( yypParser->yyidx<0 ){
#if YYSTACKDEPTH<=0
    if( yypParser->yystksz <=0 ){
      /*memset(&yyminorunion, 0, sizeof(yyminorunion));*/
      yyminorunion = yyzerominor;
      yyStackOverflow(yypParser, &yyminorunion);
      return;
    }
#endif
    yypParser->yyidx = 0;
    yypParser->yyerrcnt = -1;
    yypParser->yystack[0].stateno = 0;
    yypParser->yystack[0].major = 0;
  }
  yyminorunion.yy0 = yyminor;
#if !defined(YYERRORSYMBOL) && !defined(YYNOERRORRECOVERY)
  yyendofinput = (yymajor==0);
#endif
  sqlite3ParserARG_STORE;

#ifndef NDEBUG
  if( yyTraceFILE ){
    fprintf(yyTraceFILE,"%sInput %s\n",yyTracePrompt,yyTokenName[yymajor]);
  }
#endif

  do{
    yyact = yy_find_shift_action(yypParser,(YYCODETYPE)yymajor);
    if( yyact<YYNSTATE ){
      yy_shift(yypParser,yyact,yymajor,&yyminorunion);
      yypParser->yyerrcnt--;
      yymajor = YYNOCODE;
    }else if( yyact < YYNSTATE + YYNRULE ){
      yy_reduce(yypParser,yyact-YYNSTATE);
    }else{
      assert( yyact == YY_ERROR_ACTION );
#ifdef YYERRORSYMBOL
      int yymx;
#endif
#ifndef NDEBUG
      if( yyTraceFILE ){
        fprintf(yyTraceFILE,"%sSyntax Error!\n",yyTracePrompt);
      }
#endif
#ifdef YYERRORSYMBOL
      /* A syntax error has occurred.
      ** The response to an error depends upon whether or not the
      ** grammar defines an error token "ERROR".  
      **
      ** This is what we do if the grammar does define ERROR:
      **
      **  * Call the %syntax_error function.
      **
      **  * Begin popping the stack until we enter a state where
      **    it is legal to shift the error symbol, then shift
      **    the error symbol.
      **
      **  * Set the error count to three.
      **
      **  * Begin accepting and shifting new tokens.  No new error
      **    processing will occur until three tokens have been
      **    shifted successfully.
      **
      */
      if( yypParser->yyerrcnt<0 ){
        yy_syntax_error(yypParser,yymajor,yyminorunion);
      }
      yymx = yypParser->yystack[yypParser->yyidx].major;
      if( yymx==YYERRORSYMBOL || yyerrorhit ){
#ifndef NDEBUG
        if( yyTraceFILE ){
          fprintf(yyTraceFILE,"%sDiscard input token %s\n",
             yyTracePrompt,yyTokenName[yymajor]);
        }
#endif
        yy_destructor(yypParser, (YYCODETYPE)yymajor,&yyminorunion);
        yymajor = YYNOCODE;
      }else{
         while(
          yypParser->yyidx >= 0 &&
          yymx != YYERRORSYMBOL &&
          (yyact = yy_find_reduce_action(
                        yypParser->yystack[yypParser->yyidx].stateno,
                        YYERRORSYMBOL)) >= YYNSTATE
        ){
          yy_pop_parser_stack(yypParser);
        }
        if( yypParser->yyidx < 0 || yymajor==0 ){
          yy_destructor(yypParser,(YYCODETYPE)yymajor,&yyminorunion);
          yy_parse_failed(yypParser);
          yymajor = YYNOCODE;
        }else if( yymx!=YYERRORSYMBOL ){
          YYMINORTYPE u2;
          u2.YYERRSYMDT = 0;
          yy_shift(yypParser,yyact,YYERRORSYMBOL,&u2);
        }
      }
      yypParser->yyerrcnt = 3;
      yyerrorhit = 1;
#elif defined(YYNOERRORRECOVERY)
      /* If the YYNOERRORRECOVERY macro is defined, then do not attempt to
      ** do any kind of error recovery.  Instead, simply invoke the syntax
      ** error routine and continue going as if nothing had happened.
      **
      ** Applications can set this macro (for example inside %include) if
      ** they intend to abandon the parse upon the first syntax error seen.
      */
      yy_syntax_error(yypParser,yymajor,yyminorunion);
      yy_destructor(yypParser,(YYCODETYPE)yymajor,&yyminorunion);
      yymajor = YYNOCODE;
      
#else  /* YYERRORSYMBOL is not defined */
      /* This is what we do if the grammar does not define ERROR:
      **
      **  * Report an error message, and throw away the input token.
      **
      **  * If the input token is $, then fail the parse.
      **
      ** As before, subsequent error messages are suppressed until
      ** three input tokens have been successfully shifted.
      */
      if( yypParser->yyerrcnt<=0 ){
        yy_syntax_error(yypParser,yymajor,yyminorunion);
      }
      yypParser->yyerrcnt = 3;
      yy_destructor(yypParser,(YYCODETYPE)yymajor,&yyminorunion);
      if( yyendofinput ){
        yy_parse_failed(yypParser);
      }
      yymajor = YYNOCODE;
#endif
    }
  }while( yymajor!=YYNOCODE && yypParser->yyidx>=0 );
  return;
}

/************** End of parse.c ***********************************************/
/************** Begin file tokenize.c ****************************************/
/*
** 2001 September 15
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** An tokenizer for SQL
**
** This file contains C code that splits an SQL input string up into
** individual tokens and sends those tokens one-by-one over to the
** parser for analysis.
*/
/* #include <stdlib.h> */

/*
** The charMap() macro maps alphabetic characters into their
** lower-case ASCII equivalent.  On ASCII machines, this is just
** an upper-to-lower case map.  On EBCDIC machines we also need
** to adjust the encoding.  Only alphabetic characters and underscores
** need to be translated.
*/
#ifdef SQLITE_ASCII
# define charMap(X) sqlite3UpperToLower[(unsigned char)X]
#endif
#ifdef SQLITE_EBCDIC
# define charMap(X) ebcdicToAscii[(unsigned char)X]
const unsigned char ebcdicToAscii[] = {
/* 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F */
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  /* 0x */
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  /* 1x */
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  /* 2x */
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  /* 3x */
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  /* 4x */
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  /* 5x */
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 95,  0,  0,  /* 6x */
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  /* 7x */
   0, 97, 98, 99,100,101,102,103,104,105,  0,  0,  0,  0,  0,  0,  /* 8x */
   0,106,107,108,109,110,111,112,113,114,  0,  0,  0,  0,  0,  0,  /* 9x */
   0,  0,115,116,117,118,119,120,121,122,  0,  0,  0,  0,  0,  0,  /* Ax */
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  /* Bx */
   0, 97, 98, 99,100,101,102,103,104,105,  0,  0,  0,  0,  0,  0,  /* Cx */
   0,106,107,108,109,110,111,112,113,114,  0,  0,  0,  0,  0,  0,  /* Dx */
   0,  0,115,116,117,118,119,120,121,122,  0,  0,  0,  0,  0,  0,  /* Ex */
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  /* Fx */
};
#endif

/*
** The sqlite3KeywordCode function looks up an identifier to determine if
** it is a keyword.  If it is a keyword, the token code of that keyword is 
** returned.  If the input is not a keyword, TK_ID is returned.
**
** The implementation of this routine was generated by a program,
** mkkeywordhash.h, located in the tool subdirectory of the distribution.
** The output of the mkkeywordhash.c program is written into a file
** named keywordhash.h and then included into this source file by
** the #include below.
*/
/************** Include keywordhash.h in the middle of tokenize.c ************/
/************** Begin file keywordhash.h *************************************/
/***** This file contains automatically generated code ******
**
** The code in this file has been automatically generated by
**
**   sqlite/tool/mkkeywordhash.c
**
** The code in this file implements a function that determines whether
** or not a given identifier is really an SQL keyword.  The same thing
** might be implemented more directly using a hand-written hash table.
** But by using this automatically generated code, the size of the code
** is substantially reduced.  This is important for embedded applications
** on platforms with limited memory.
*/
/* Hash score: 175 */
static int keywordCode(const char *z, int n){
  /* zText[] encodes 811 bytes of keywords in 541 bytes */
  /*   REINDEXEDESCAPEACHECKEYBEFOREIGNOREGEXPLAINSTEADDATABASELECT       */
  /*   ABLEFTHENDEFERRABLELSEXCEPTRANSACTIONATURALTERAISEXCLUSIVE         */
  /*   XISTSAVEPOINTERSECTRIGGEREFERENCESCONSTRAINTOFFSETEMPORARY         */
  /*   UNIQUERYATTACHAVINGROUPDATEBEGINNERELEASEBETWEENOTNULLIKE          */
  /*   CASCADELETECASECOLLATECREATECURRENT_DATEDETACHIMMEDIATEJOIN        */
  /*   SERTMATCHPLANALYZEPRAGMABORTVALUESVIRTUALIMITWHENWHERENAME         */
  /*   AFTEREPLACEANDEFAULTAUTOINCREMENTCASTCOLUMNCOMMITCONFLICTCROSS     */
  /*   CURRENT_TIMESTAMPRIMARYDEFERREDISTINCTDROPFAILFROMFULLGLOBYIF      */
  /*   ISNULLORDERESTRICTOUTERIGHTROLLBACKROWUNIONUSINGVACUUMVIEW         */
  /*   INITIALLY                                                          */
  static const char zText[540] = {
    'R','E','I','N','D','E','X','E','D','E','S','C','A','P','E','A','C','H',
    'E','C','K','E','Y','B','E','F','O','R','E','I','G','N','O','R','E','G',
    'E','X','P','L','A','I','N','S','T','E','A','D','D','A','T','A','B','A',
    'S','E','L','E','C','T','A','B','L','E','F','T','H','E','N','D','E','F',
    'E','R','R','A','B','L','E','L','S','E','X','C','E','P','T','R','A','N',
    'S','A','C','T','I','O','N','A','T','U','R','A','L','T','E','R','A','I',
    'S','E','X','C','L','U','S','I','V','E','X','I','S','T','S','A','V','E',
    'P','O','I','N','T','E','R','S','E','C','T','R','I','G','G','E','R','E',
    'F','E','R','E','N','C','E','S','C','O','N','S','T','R','A','I','N','T',
    'O','F','F','S','E','T','E','M','P','O','R','A','R','Y','U','N','I','Q',
    'U','E','R','Y','A','T','T','A','C','H','A','V','I','N','G','R','O','U',
    'P','D','A','T','E','B','E','G','I','N','N','E','R','E','L','E','A','S',
    'E','B','E','T','W','E','E','N','O','T','N','U','L','L','I','K','E','C',
    'A','S','C','A','D','E','L','E','T','E','C','A','S','E','C','O','L','L',
    'A','T','E','C','R','E','A','T','E','C','U','R','R','E','N','T','_','D',
    'A','T','E','D','E','T','A','C','H','I','M','M','E','D','I','A','T','E',
    'J','O','I','N','S','E','R','T','M','A','T','C','H','P','L','A','N','A',
    'L','Y','Z','E','P','R','A','G','M','A','B','O','R','T','V','A','L','U',
    'E','S','V','I','R','T','U','A','L','I','M','I','T','W','H','E','N','W',
    'H','E','R','E','N','A','M','E','A','F','T','E','R','E','P','L','A','C',
    'E','A','N','D','E','F','A','U','L','T','A','U','T','O','I','N','C','R',
    'E','M','E','N','T','C','A','S','T','C','O','L','U','M','N','C','O','M',
    'M','I','T','C','O','N','F','L','I','C','T','C','R','O','S','S','C','U',
    'R','R','E','N','T','_','T','I','M','E','S','T','A','M','P','R','I','M',
    'A','R','Y','D','E','F','E','R','R','E','D','I','S','T','I','N','C','T',
    'D','R','O','P','F','A','I','L','F','R','O','M','F','U','L','L','G','L',
    'O','B','Y','I','F','I','S','N','U','L','L','O','R','D','E','R','E','S',
    'T','R','I','C','T','O','U','T','E','R','I','G','H','T','R','O','L','L',
    'B','A','C','K','R','O','W','U','N','I','O','N','U','S','I','N','G','V',
    'A','C','U','U','M','V','I','E','W','I','N','I','T','I','A','L','L','Y',
  };
  static const unsigned char aHash[127] = {
      72, 101, 114,  70,   0,  45,   0,   0,  78,   0,  73,   0,   0,
      42,  12,  74,  15,   0, 113,  81,  50, 108,   0,  19,   0,   0,
     118,   0, 116, 111,   0,  22,  89,   0,   9,   0,   0,  66,  67,
       0,  65,   6,   0,  48,  86,  98,   0, 115,  97,   0,   0,  44,
       0,  99,  24,   0,  17,   0, 119,  49,  23,   0,   5, 106,  25,
      92,   0,   0, 121, 102,  56, 120,  53,  28,  51,   0,  87,   0,
      96,  26,   0,  95,   0,   0,   0,  91,  88,  93,  84, 105,  14,
      39, 104,   0,  77,   0,  18,  85, 107,  32,   0, 117,  76, 109,
      58,  46,  80,   0,   0,  90,  40,   0, 112,   0,  36,   0,   0,
      29,   0,  82,  59,  60,   0,  20,  57,   0,  52,
  };
  static const unsigned char aNext[121] = {
       0,   0,   0,   0,   4,   0,   0,   0,   0,   0,   0,   0,   0,
       0,   2,   0,   0,   0,   0,   0,   0,  13,   0,   0,   0,   0,
       0,   7,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,  33,   0,  21,   0,   0,   0,  43,   3,  47,
       0,   0,   0,   0,  30,   0,  54,   0,  38,   0,   0,   0,   1,
      62,   0,   0,  63,   0,  41,   0,   0,   0,   0,   0,   0,   0,
      61,   0,   0,   0,   0,  31,  55,  16,  34,  10,   0,   0,   0,
       0,   0,   0,   0,  11,  68,  75,   0,   8,   0, 100,  94,   0,
     103,   0,  83,   0,  71,   0,   0, 110,  27,  37,  69,  79,   0,
      35,  64,   0,   0,
  };
  static const unsigned char aLen[121] = {
       7,   7,   5,   4,   6,   4,   5,   3,   6,   7,   3,   6,   6,
       7,   7,   3,   8,   2,   6,   5,   4,   4,   3,  10,   4,   6,
      11,   6,   2,   7,   5,   5,   9,   6,   9,   9,   7,  10,  10,
       4,   6,   2,   3,   9,   4,   2,   6,   5,   6,   6,   5,   6,
       5,   5,   7,   7,   7,   3,   2,   4,   4,   7,   3,   6,   4,
       7,   6,  12,   6,   9,   4,   6,   5,   4,   7,   6,   5,   6,
       7,   5,   4,   5,   6,   5,   7,   3,   7,  13,   2,   2,   4,
       6,   6,   8,   5,  17,  12,   7,   8,   8,   2,   4,   4,   4,
       4,   4,   2,   2,   6,   5,   8,   5,   5,   8,   3,   5,   5,
       6,   4,   9,   3,
  };
  static const unsigned short int aOffset[121] = {
       0,   2,   2,   8,   9,  14,  16,  20,  23,  25,  25,  29,  33,
      36,  41,  46,  48,  53,  54,  59,  62,  65,  67,  69,  78,  81,
      86,  91,  95,  96, 101, 105, 109, 117, 122, 128, 136, 142, 152,
     159, 162, 162, 165, 167, 167, 171, 176, 179, 184, 189, 194, 197,
     203, 206, 210, 217, 223, 223, 223, 226, 229, 233, 234, 238, 244,
     248, 255, 261, 273, 279, 288, 290, 296, 301, 303, 310, 315, 320,
     326, 332, 337, 341, 344, 350, 354, 361, 363, 370, 372, 374, 383,
     387, 393, 399, 407, 412, 412, 428, 435, 442, 443, 450, 454, 458,
     462, 466, 469, 471, 473, 479, 483, 491, 495, 500, 508, 511, 516,
     521, 527, 531, 536,
  };
  static const unsigned char aCode[121] = {
    TK_REINDEX,    TK_INDEXED,    TK_INDEX,      TK_DESC,       TK_ESCAPE,     
    TK_EACH,       TK_CHECK,      TK_KEY,        TK_BEFORE,     TK_FOREIGN,    
    TK_FOR,        TK_IGNORE,     TK_LIKE_KW,    TK_EXPLAIN,    TK_INSTEAD,    
    TK_ADD,        TK_DATABASE,   TK_AS,         TK_SELECT,     TK_TABLE,      
    TK_JOIN_KW,    TK_THEN,       TK_END,        TK_DEFERRABLE, TK_ELSE,       
    TK_EXCEPT,     TK_TRANSACTION,TK_ACTION,     TK_ON,         TK_JOIN_KW,    
    TK_ALTER,      TK_RAISE,      TK_EXCLUSIVE,  TK_EXISTS,     TK_SAVEPOINT,  
    TK_INTERSECT,  TK_TRIGGER,    TK_REFERENCES, TK_CONSTRAINT, TK_INTO,       
    TK_OFFSET,     TK_OF,         TK_SET,        TK_TEMP,       TK_TEMP,       
    TK_OR,         TK_UNIQUE,     TK_QUERY,      TK_ATTACH,     TK_HAVING,     
    TK_GROUP,      TK_UPDATE,     TK_BEGIN,      TK_JOIN_KW,    TK_RELEASE,    
    TK_BETWEEN,    TK_NOTNULL,    TK_NOT,        TK_NO,         TK_NULL,       
    TK_LIKE_KW,    TK_CASCADE,    TK_ASC,        TK_DELETE,     TK_CASE,       
    TK_COLLATE,    TK_CREATE,     TK_CTIME_KW,   TK_DETACH,     TK_IMMEDIATE,  
    TK_JOIN,       TK_INSERT,     TK_MATCH,      TK_PLAN,       TK_ANALYZE,    
    TK_PRAGMA,     TK_ABORT,      TK_VALUES,     TK_VIRTUAL,    TK_LIMIT,      
    TK_WHEN,       TK_WHERE,      TK_RENAME,     TK_AFTER,      TK_REPLACE,    
    TK_AND,        TK_DEFAULT,    TK_AUTOINCR,   TK_TO,         TK_IN,         
    TK_CAST,       TK_COLUMNKW,   TK_COMMIT,     TK_CONFLICT,   TK_JOIN_KW,    
    TK_CTIME_KW,   TK_CTIME_KW,   TK_PRIMARY,    TK_DEFERRED,   TK_DISTINCT,   
    TK_IS,         TK_DROP,       TK_FAIL,       TK_FROM,       TK_JOIN_KW,    
    TK_LIKE_KW,    TK_BY,         TK_IF,         TK_ISNULL,     TK_ORDER,      
    TK_RESTRICT,   TK_JOIN_KW,    TK_JOIN_KW,    TK_ROLLBACK,   TK_ROW,        
    TK_UNION,      TK_USING,      TK_VACUUM,     TK_VIEW,       TK_INITIALLY,  
    TK_ALL,        
  };
  int h, i;
  if( n<2 ) return TK_ID;
  h = ((charMap(z[0])*4) ^
      (charMap(z[n-1])*3) ^
      n) % 127;
  for(i=((int)aHash[h])-1; i>=0; i=((int)aNext[i])-1){
    if( aLen[i]==n && sqlite3StrNICmp(&zText[aOffset[i]],z,n)==0 ){
      testcase( i==0 ); /* REINDEX */
      testcase( i==1 ); /* INDEXED */
      testcase( i==2 ); /* INDEX */
      testcase( i==3 ); /* DESC */
      testcase( i==4 ); /* ESCAPE */
      testcase( i==5 ); /* EACH */
      testcase( i==6 ); /* CHECK */
      testcase( i==7 ); /* KEY */
      testcase( i==8 ); /* BEFORE */
      testcase( i==9 ); /* FOREIGN */
      testcase( i==10 ); /* FOR */
      testcase( i==11 ); /* IGNORE */
      testcase( i==12 ); /* REGEXP */
      testcase( i==13 ); /* EXPLAIN */
      testcase( i==14 ); /* INSTEAD */
      testcase( i==15 ); /* ADD */
      testcase( i==16 ); /* DATABASE */
      testcase( i==17 ); /* AS */
      testcase( i==18 ); /* SELECT */
      testcase( i==19 ); /* TABLE */
      testcase( i==20 ); /* LEFT */
      testcase( i==21 ); /* THEN */
      testcase( i==22 ); /* END */
      testcase( i==23 ); /* DEFERRABLE */
      testcase( i==24 ); /* ELSE */
      testcase( i==25 ); /* EXCEPT */
      testcase( i==26 ); /* TRANSACTION */
      testcase( i==27 ); /* ACTION */
      testcase( i==28 ); /* ON */
      testcase( i==29 ); /* NATURAL */
      testcase( i==30 ); /* ALTER */
      testcase( i==31 ); /* RAISE */
      testcase( i==32 ); /* EXCLUSIVE */
      testcase( i==33 ); /* EXISTS */
      testcase( i==34 ); /* SAVEPOINT */
      testcase( i==35 ); /* INTERSECT */
      testcase( i==36 ); /* TRIGGER */
      testcase( i==37 ); /* REFERENCES */
      testcase( i==38 ); /* CONSTRAINT */
      testcase( i==39 ); /* INTO */
      testcase( i==40 ); /* OFFSET */
      testcase( i==41 ); /* OF */
      testcase( i==42 ); /* SET */
      testcase( i==43 ); /* TEMPORARY */
      testcase( i==44 ); /* TEMP */
      testcase( i==45 ); /* OR */
      testcase( i==46 ); /* UNIQUE */
      testcase( i==47 ); /* QUERY */
      testcase( i==48 ); /* ATTACH */
      testcase( i==49 ); /* HAVING */
      testcase( i==50 ); /* GROUP */
      testcase( i==51 ); /* UPDATE */
      testcase( i==52 ); /* BEGIN */
      testcase( i==53 ); /* INNER */
      testcase( i==54 ); /* RELEASE */
      testcase( i==55 ); /* BETWEEN */
      testcase( i==56 ); /* NOTNULL */
      testcase( i==57 ); /* NOT */
      testcase( i==58 ); /* NO */
      testcase( i==59 ); /* NULL */
      testcase( i==60 ); /* LIKE */
      testcase( i==61 ); /* CASCADE */
      testcase( i==62 ); /* ASC */
      testcase( i==63 ); /* DELETE */
      testcase( i==64 ); /* CASE */
      testcase( i==65 ); /* COLLATE */
      testcase( i==66 ); /* CREATE */
      testcase( i==67 ); /* CURRENT_DATE */
      testcase( i==68 ); /* DETACH */
      testcase( i==69 ); /* IMMEDIATE */
      testcase( i==70 ); /* JOIN */
      testcase( i==71 ); /* INSERT */
      testcase( i==72 ); /* MATCH */
      testcase( i==73 ); /* PLAN */
      testcase( i==74 ); /* ANALYZE */
      testcase( i==75 ); /* PRAGMA */
      testcase( i==76 ); /* ABORT */
      testcase( i==77 ); /* VALUES */
      testcase( i==78 ); /* VIRTUAL */
      testcase( i==79 ); /* LIMIT */
      testcase( i==80 ); /* WHEN */
      testcase( i==81 ); /* WHERE */
      testcase( i==82 ); /* RENAME */
      testcase( i==83 ); /* AFTER */
      testcase( i==84 ); /* REPLACE */
      testcase( i==85 ); /* AND */
      testcase( i==86 ); /* DEFAULT */
      testcase( i==87 ); /* AUTOINCREMENT */
      testcase( i==88 ); /* TO */
      testcase( i==89 ); /* IN */
      testcase( i==90 ); /* CAST */
      testcase( i==91 ); /* COLUMN */
      testcase( i==92 ); /* COMMIT */
      testcase( i==93 ); /* CONFLICT */
      testcase( i==94 ); /* CROSS */
      testcase( i==95 ); /* CURRENT_TIMESTAMP */
      testcase( i==96 ); /* CURRENT_TIME */
      testcase( i==97 ); /* PRIMARY */
      testcase( i==98 ); /* DEFERRED */
      testcase( i==99 ); /* DISTINCT */
      testcase( i==100 ); /* IS */
      testcase( i==101 ); /* DROP */
      testcase( i==102 ); /* FAIL */
      testcase( i==103 ); /* FROM */
      testcase( i==104 ); /* FULL */
      testcase( i==105 ); /* GLOB */
      testcase( i==106 ); /* BY */
      testcase( i==107 ); /* IF */
      testcase( i==108 ); /* ISNULL */
      testcase( i==109 ); /* ORDER */
      testcase( i==110 ); /* RESTRICT */
      testcase( i==111 ); /* OUTER */
      testcase( i==112 ); /* RIGHT */
      testcase( i==113 ); /* ROLLBACK */
      testcase( i==114 ); /* ROW */
      testcase( i==115 ); /* UNION */
      testcase( i==116 ); /* USING */
      testcase( i==117 ); /* VACUUM */
      testcase( i==118 ); /* VIEW */
      testcase( i==119 ); /* INITIALLY */
      testcase( i==120 ); /* ALL */
      return aCode[i];
    }
  }
  return TK_ID;
}
SQLITE_PRIVATE int sqlite3KeywordCode(const unsigned char *z, int n){
  return keywordCode((char*)z, n);
}
#define SQLITE_N_KEYWORD 121

/************** End of keywordhash.h *****************************************/
/************** Continuing where we left off in tokenize.c *******************/


/*
** If X is a character that can be used in an identifier then
** IdChar(X) will be true.  Otherwise it is false.
**
** For ASCII, any character with the high-order bit set is
** allowed in an identifier.  For 7-bit characters, 
** sqlite3IsIdChar[X] must be 1.
**
** For EBCDIC, the rules are more complex but have the same
** end result.
**
** Ticket #1066.  the SQL standard does not allow '$' in the
** middle of identfiers.  But many SQL implementations do. 
** SQLite will allow '$' in identifiers for compatibility.
** But the feature is undocumented.
*/
#ifdef SQLITE_ASCII
#define IdChar(C)  ((sqlite3CtypeMap[(unsigned char)C]&0x46)!=0)
#endif
#ifdef SQLITE_EBCDIC
SQLITE_PRIVATE const char sqlite3IsEbcdicIdChar[] = {
/* x0 x1 x2 x3 x4 x5 x6 x7 x8 x9 xA xB xC xD xE xF */
    0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,  /* 4x */
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0,  /* 5x */
    0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0,  /* 6x */
    0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0,  /* 7x */
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0,  /* 8x */
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 1, 0,  /* 9x */
    1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0,  /* Ax */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* Bx */
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1,  /* Cx */
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1,  /* Dx */
    0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1,  /* Ex */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0,  /* Fx */
};
#define IdChar(C)  (((c=C)>=0x42 && sqlite3IsEbcdicIdChar[c-0x40]))
#endif


/*
** Return the length of the token that begins at z[0]. 
** Store the token type in *tokenType before returning.
*/
SQLITE_PRIVATE int sqlite3GetToken(const unsigned char *z, int *tokenType){
  int i, c;
  switch( *z ){
    case ' ': case '\t': case '\n': case '\f': case '\r': {
      testcase( z[0]==' ' );
      testcase( z[0]=='\t' );
      testcase( z[0]=='\n' );
      testcase( z[0]=='\f' );
      testcase( z[0]=='\r' );
      for(i=1; sqlite3Isspace(z[i]); i++){}
      *tokenType = TK_SPACE;
      return i;
    }
    case '-': {
      if( z[1]=='-' ){
        /* IMP: R-15891-05542 -- syntax diagram for comments */
        for(i=2; (c=z[i])!=0 && c!='\n'; i++){}
        *tokenType = TK_SPACE;   /* IMP: R-22934-25134 */
        return i;
      }
      *tokenType = TK_MINUS;
      return 1;
    }
    case '(': {
      *tokenType = TK_LP;
      return 1;
    }
    case ')': {
      *tokenType = TK_RP;
      return 1;
    }
    case ';': {
      *tokenType = TK_SEMI;
      return 1;
    }
    case '+': {
      *tokenType = TK_PLUS;
      return 1;
    }
    case '*': {
      *tokenType = TK_STAR;
      return 1;
    }
    case '/': {
      if( z[1]!='*' || z[2]==0 ){
        *tokenType = TK_SLASH;
        return 1;
      }
      /* IMP: R-15891-05542 -- syntax diagram for comments */
      for(i=3, c=z[2]; (c!='*' || z[i]!='/') && (c=z[i])!=0; i++){}
      if( c ) i++;
      *tokenType = TK_SPACE;   /* IMP: R-22934-25134 */
      return i;
    }
    case '%': {
      *tokenType = TK_REM;
      return 1;
    }
    case '=': {
      *tokenType = TK_EQ;
      return 1 + (z[1]=='=');
    }
    case '<': {
      if( (c=z[1])=='=' ){
        *tokenType = TK_LE;
        return 2;
      }else if( c=='>' ){
        *tokenType = TK_NE;
        return 2;
      }else if( c=='<' ){
        *tokenType = TK_LSHIFT;
        return 2;
      }else{
        *tokenType = TK_LT;
        return 1;
      }
    }
    case '>': {
      if( (c=z[1])=='=' ){
        *tokenType = TK_GE;
        return 2;
      }else if( c=='>' ){
        *tokenType = TK_RSHIFT;
        return 2;
      }else{
        *tokenType = TK_GT;
        return 1;
      }
    }
    case '!': {
      if( z[1]!='=' ){
        *tokenType = TK_ILLEGAL;
        return 2;
      }else{
        *tokenType = TK_NE;
        return 2;
      }
    }
    case '|': {
      if( z[1]!='|' ){
        *tokenType = TK_BITOR;
        return 1;
      }else{
        *tokenType = TK_CONCAT;
        return 2;
      }
    }
    case ',': {
      *tokenType = TK_COMMA;
      return 1;
    }
    case '&': {
      *tokenType = TK_BITAND;
      return 1;
    }
    case '~': {
      *tokenType = TK_BITNOT;
      return 1;
    }
    case '`':
    case '\'':
    case '"': {
      int delim = z[0];
      testcase( delim=='`' );
      testcase( delim=='\'' );
      testcase( delim=='"' );
      for(i=1; (c=z[i])!=0; i++){
        if( c==delim ){
          if( z[i+1]==delim ){
            i++;
          }else{
            break;
          }
        }
      }
      if( c=='\'' ){
        *tokenType = TK_STRING;
        return i+1;
      }else if( c!=0 ){
        *tokenType = TK_ID;
        return i+1;
      }else{
        *tokenType = TK_ILLEGAL;
        return i;
      }
    }
    case '.': {
#ifndef SQLITE_OMIT_FLOATING_POINT
      if( !sqlite3Isdigit(z[1]) )
#endif
      {
        *tokenType = TK_DOT;
        return 1;
      }
      /* If the next character is a digit, this is a floating point
      ** number that begins with ".".  Fall thru into the next case */
    }
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9': {
      testcase( z[0]=='0' );  testcase( z[0]=='1' );  testcase( z[0]=='2' );
      testcase( z[0]=='3' );  testcase( z[0]=='4' );  testcase( z[0]=='5' );
      testcase( z[0]=='6' );  testcase( z[0]=='7' );  testcase( z[0]=='8' );
      testcase( z[0]=='9' );
      *tokenType = TK_INTEGER;
      for(i=0; sqlite3Isdigit(z[i]); i++){}
#ifndef SQLITE_OMIT_FLOATING_POINT
      if( z[i]=='.' ){
        i++;
        while( sqlite3Isdigit(z[i]) ){ i++; }
        *tokenType = TK_FLOAT;
      }
      if( (z[i]=='e' || z[i]=='E') &&
           ( sqlite3Isdigit(z[i+1]) 
            || ((z[i+1]=='+' || z[i+1]=='-') && sqlite3Isdigit(z[i+2]))
           )
      ){
        i += 2;
        while( sqlite3Isdigit(z[i]) ){ i++; }
        *tokenType = TK_FLOAT;
      }
#endif
      while( IdChar(z[i]) ){
        *tokenType = TK_ILLEGAL;
        i++;
      }
      return i;
    }
    case '[': {
      for(i=1, c=z[0]; c!=']' && (c=z[i])!=0; i++){}
      *tokenType = c==']' ? TK_ID : TK_ILLEGAL;
      return i;
    }
    case '?': {
      *tokenType = TK_VARIABLE;
      for(i=1; sqlite3Isdigit(z[i]); i++){}
      return i;
    }
    case '#': {
      for(i=1; sqlite3Isdigit(z[i]); i++){}
      if( i>1 ){
        /* Parameters of the form #NNN (where NNN is a number) are used
        ** internally by sqlite3NestedParse.  */
        *tokenType = TK_REGISTER;
        return i;
      }
      /* Fall through into the next case if the '#' is not followed by
      ** a digit. Try to match #AAAA where AAAA is a parameter name. */
    }
#ifndef SQLITE_OMIT_TCL_VARIABLE
    case '$':
#endif
    case '@':  /* For compatibility with MS SQL Server */
    case ':': {
      int n = 0;
      testcase( z[0]=='$' );  testcase( z[0]=='@' );  testcase( z[0]==':' );
      *tokenType = TK_VARIABLE;
      for(i=1; (c=z[i])!=0; i++){
        if( IdChar(c) ){
          n++;
#ifndef SQLITE_OMIT_TCL_VARIABLE
        }else if( c=='(' && n>0 ){
          do{
            i++;
          }while( (c=z[i])!=0 && !sqlite3Isspace(c) && c!=')' );
          if( c==')' ){
            i++;
          }else{
            *tokenType = TK_ILLEGAL;
          }
          break;
        }else if( c==':' && z[i+1]==':' ){
          i++;
#endif
        }else{
          break;
        }
      }
      if( n==0 ) *tokenType = TK_ILLEGAL;
      return i;
    }
#ifndef SQLITE_OMIT_BLOB_LITERAL
    case 'x': case 'X': {
      testcase( z[0]=='x' ); testcase( z[0]=='X' );
      if( z[1]=='\'' ){
        *tokenType = TK_BLOB;
        for(i=2; sqlite3Isxdigit(z[i]); i++){}
        if( z[i]!='\'' || i%2 ){
          *tokenType = TK_ILLEGAL;
          while( z[i] && z[i]!='\'' ){ i++; }
        }
        if( z[i] ) i++;
        return i;
      }
      /* Otherwise fall through to the next case */
    }
#endif
    default: {
      if( !IdChar(*z) ){
        break;
      }
      for(i=1; IdChar(z[i]); i++){}
      *tokenType = keywordCode((char*)z, i);
      return i;
    }
  }
  *tokenType = TK_ILLEGAL;
  return 1;
}

/*
** Run the parser on the given SQL string.  The parser structure is
** passed in.  An SQLITE_ status code is returned.  If an error occurs
** then an and attempt is made to write an error message into 
** memory obtained from sqlite3_malloc() and to make *pzErrMsg point to that
** error message.
*/
SQLITE_PRIVATE int sqlite3RunParser(Parse *pParse, const char *zSql, char **pzErrMsg){
  int nErr = 0;                   /* Number of errors encountered */
  int i;                          /* Loop counter */
  void *pEngine;                  /* The LEMON-generated LALR(1) parser */
  int tokenType;                  /* type of the next token */
  int lastTokenParsed = -1;       /* type of the previous token */
  u8 enableLookaside;             /* Saved value of db->lookaside.bEnabled */
  sqlite3 *db = pParse->db;       /* The database connection */
  int mxSqlLen;                   /* Max length of an SQL string */


  mxSqlLen = db->aLimit[SQLITE_LIMIT_SQL_LENGTH];
  if( db->activeVdbeCnt==0 ){
    db->u1.isInterrupted = 0;
  }
  pParse->rc = SQLITE_OK;
  pParse->zTail = zSql;
  i = 0;
  assert( pzErrMsg!=0 );
  pEngine = sqlite3ParserAlloc((void*(*)(size_t))sqlite3Malloc);
  if( pEngine==0 ){
    db->mallocFailed = 1;
    return SQLITE_NOMEM;
  }
  assert( pParse->pNewTable==0 );
  assert( pParse->pNewTrigger==0 );
  assert( pParse->nVar==0 );
  assert( pParse->nzVar==0 );
  assert( pParse->azVar==0 );
  enableLookaside = db->lookaside.bEnabled;
  if( db->lookaside.pStart ) db->lookaside.bEnabled = 1;
  while( !db->mallocFailed && zSql[i]!=0 ){
    assert( i>=0 );
    pParse->sLastToken.z = &zSql[i];
    pParse->sLastToken.n = sqlite3GetToken((unsigned char*)&zSql[i],&tokenType);
    i += pParse->sLastToken.n;
    if( i>mxSqlLen ){
      pParse->rc = SQLITE_TOOBIG;
      break;
    }
    switch( tokenType ){
      case TK_SPACE: {
        if( db->u1.isInterrupted ){
          sqlite3ErrorMsg(pParse, "interrupt");
          pParse->rc = SQLITE_INTERRUPT;
          goto abort_parse;
        }
        break;
      }
      case TK_ILLEGAL: {
        sqlite3DbFree(db, *pzErrMsg);
        *pzErrMsg = sqlite3MPrintf(db, "unrecognized token: \"%T\"",
                        &pParse->sLastToken);
        nErr++;
        goto abort_parse;
      }
      case TK_SEMI: {
        pParse->zTail = &zSql[i];
        /* Fall thru into the default case */
      }
      default: {
        sqlite3Parser(pEngine, tokenType, pParse->sLastToken, pParse);
        lastTokenParsed = tokenType;
        if( pParse->rc!=SQLITE_OK ){
          goto abort_parse;
        }
        break;
      }
    }
  }
abort_parse:
  if( zSql[i]==0 && nErr==0 && pParse->rc==SQLITE_OK ){
    if( lastTokenParsed!=TK_SEMI ){
      sqlite3Parser(pEngine, TK_SEMI, pParse->sLastToken, pParse);
      pParse->zTail = &zSql[i];
    }
    sqlite3Parser(pEngine, 0, pParse->sLastToken, pParse);
  }
#ifdef YYTRACKMAXSTACKDEPTH
  sqlite3StatusSet(SQLITE_STATUS_PARSER_STACK,
      sqlite3ParserStackPeak(pEngine)
  );
#endif /* YYDEBUG */
  sqlite3ParserFree(pEngine, sqlite3_free);
  db->lookaside.bEnabled = enableLookaside;
  if( db->mallocFailed ){
    pParse->rc = SQLITE_NOMEM;
  }
  if( pParse->rc!=SQLITE_OK && pParse->rc!=SQLITE_DONE && pParse->zErrMsg==0 ){
    sqlite3SetString(&pParse->zErrMsg, db, "%s", sqlite3ErrStr(pParse->rc));
  }
  assert( pzErrMsg!=0 );
  if( pParse->zErrMsg ){
    *pzErrMsg = pParse->zErrMsg;
    sqlite3_log(pParse->rc, "%s", *pzErrMsg);
    pParse->zErrMsg = 0;
    nErr++;
  }
  if( pParse->pVdbe && pParse->nErr>0 && pParse->nested==0 ){
    sqlite3VdbeDelete(pParse->pVdbe);
    pParse->pVdbe = 0;
  }
#ifndef SQLITE_OMIT_SHARED_CACHE
  if( pParse->nested==0 ){
    sqlite3DbFree(db, pParse->aTableLock);
    pParse->aTableLock = 0;
    pParse->nTableLock = 0;
  }
#endif
#ifndef SQLITE_OMIT_VIRTUALTABLE
  sqlite3_free(pParse->apVtabLock);
#endif

  if( !IN_DECLARE_VTAB ){
    /* If the pParse->declareVtab flag is set, do not delete any table 
    ** structure built up in pParse->pNewTable. The calling code (see vtab.c)
    ** will take responsibility for freeing the Table structure.
    */
    sqlite3DeleteTable(db, pParse->pNewTable);
  }

  sqlite3DeleteTrigger(db, pParse->pNewTrigger);
  for(i=pParse->nzVar-1; i>=0; i--) sqlite3DbFree(db, pParse->azVar[i]);
  sqlite3DbFree(db, pParse->azVar);
  sqlite3DbFree(db, pParse->aAlias);
  while( pParse->pAinc ){
    AutoincInfo *p = pParse->pAinc;
    pParse->pAinc = p->pNext;
    sqlite3DbFree(db, p);
  }
  while( pParse->pZombieTab ){
    Table *p = pParse->pZombieTab;
    pParse->pZombieTab = p->pNextZombie;
    sqlite3DeleteTable(db, p);
  }
  if( nErr>0 && pParse->rc==SQLITE_OK ){
    pParse->rc = SQLITE_ERROR;
  }
  return nErr;
}

/************** End of tokenize.c ********************************************/
/************** Begin file complete.c ****************************************/
/*
** 2001 September 15
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** An tokenizer for SQL
**
** This file contains C code that implements the sqlite3_complete() API.
** This code used to be part of the tokenizer.c source file.  But by
** separating it out, the code will be automatically omitted from
** static links that do not use it.
*/
#ifndef SQLITE_OMIT_COMPLETE

/*
** This is defined in tokenize.c.  We just have to import the definition.
*/
#ifndef SQLITE_AMALGAMATION
#ifdef SQLITE_ASCII
#define IdChar(C)  ((sqlite3CtypeMap[(unsigned char)C]&0x46)!=0)
#endif
#ifdef SQLITE_EBCDIC
SQLITE_PRIVATE const char sqlite3IsEbcdicIdChar[];
#define IdChar(C)  (((c=C)>=0x42 && sqlite3IsEbcdicIdChar[c-0x40]))
#endif
#endif /* SQLITE_AMALGAMATION */


/*
** Token types used by the sqlite3_complete() routine.  See the header
** comments on that procedure for additional information.
*/
#define tkSEMI    0
#define tkWS      1
#define tkOTHER   2
#ifndef SQLITE_OMIT_TRIGGER
#define tkEXPLAIN 3
#define tkCREATE  4
#define tkTEMP    5
#define tkTRIGGER 6
#define tkEND     7
#endif

/*
** Return TRUE if the given SQL string ends in a semicolon.
**
** Special handling is require for CREATE TRIGGER statements.
** Whenever the CREATE TRIGGER keywords are seen, the statement
** must end with ";END;".
**
** This implementation uses a state machine with 8 states:
**
**   (0) INVALID   We have not yet seen a non-whitespace character.
**
**   (1) START     At the beginning or end of an SQL statement.  This routine
**                 returns 1 if it ends in the START state and 0 if it ends
**                 in any other state.
**
**   (2) NORMAL    We are in the middle of statement which ends with a single
**                 semicolon.
**
**   (3) EXPLAIN   The keyword EXPLAIN has been seen at the beginning of 
**                 a statement.
**
**   (4) CREATE    The keyword CREATE has been seen at the beginning of a
**                 statement, possibly preceeded by EXPLAIN and/or followed by
**                 TEMP or TEMPORARY
**
**   (5) TRIGGER   We are in the middle of a trigger definition that must be
**                 ended by a semicolon, the keyword END, and another semicolon.
**
**   (6) SEMI      We've seen the first semicolon in the ";END;" that occurs at
**                 the end of a trigger definition.
**
**   (7) END       We've seen the ";END" of the ";END;" that occurs at the end
**                 of a trigger difinition.
**
** Transitions between states above are determined by tokens extracted
** from the input.  The following tokens are significant:
**
**   (0) tkSEMI      A semicolon.
**   (1) tkWS        Whitespace.
**   (2) tkOTHER     Any other SQL token.
**   (3) tkEXPLAIN   The "explain" keyword.
**   (4) tkCREATE    The "create" keyword.
**   (5) tkTEMP      The "temp" or "temporary" keyword.
**   (6) tkTRIGGER   The "trigger" keyword.
**   (7) tkEND       The "end" keyword.
**
** Whitespace never causes a state transition and is always ignored.
** This means that a SQL string of all whitespace is invalid.
**
** If we compile with SQLITE_OMIT_TRIGGER, all of the computation needed
** to recognize the end of a trigger can be omitted.  All we have to do
** is look for a semicolon that is not part of an string or comment.
*/
SQLITE_API int sqlite3_complete(const char *zSql){
  u8 state = 0;   /* Current state, using numbers defined in header comment */
  u8 token;       /* Value of the next token */

#ifndef SQLITE_OMIT_TRIGGER
  /* A complex statement machine used to detect the end of a CREATE TRIGGER
  ** statement.  This is the normal case.
  */
  static const u8 trans[8][8] = {
                     /* Token:                                                */
     /* State:       **  SEMI  WS  OTHER  EXPLAIN  CREATE  TEMP  TRIGGER  END */
     /* 0 INVALID: */ {    1,  0,     2,       3,      4,    2,       2,   2, },
     /* 1   START: */ {    1,  1,     2,       3,      4,    2,       2,   2, },
     /* 2  NORMAL: */ {    1,  2,     2,       2,      2,    2,       2,   2, },
     /* 3 EXPLAIN: */ {    1,  3,     3,       2,      4,    2,       2,   2, },
     /* 4  CREATE: */ {    1,  4,     2,       2,      2,    4,       5,   2, },
     /* 5 TRIGGER: */ {    6,  5,     5,       5,      5,    5,       5,   5, },
     /* 6    SEMI: */ {    6,  6,     5,       5,      5,    5,       5,   7, },
     /* 7     END: */ {    1,  7,     5,       5,      5,    5,       5,   5, },
  };
#else
  /* If triggers are not supported by this compile then the statement machine
  ** used to detect the end of a statement is much simplier
  */
  static const u8 trans[3][3] = {
                     /* Token:           */
     /* State:       **  SEMI  WS  OTHER */
     /* 0 INVALID: */ {    1,  0,     2, },
     /* 1   START: */ {    1,  1,     2, },
     /* 2  NORMAL: */ {    1,  2,     2, },
  };
#endif /* SQLITE_OMIT_TRIGGER */

  while( *zSql ){
    switch( *zSql ){
      case ';': {  /* A semicolon */
        token = tkSEMI;
        break;
      }
      case ' ':
      case '\r':
      case '\t':
      case '\n':
      case '\f': {  /* White space is ignored */
        token = tkWS;
        break;
      }
      case '/': {   /* C-style comments */
        if( zSql[1]!='*' ){
          token = tkOTHER;
          break;
        }
        zSql += 2;
        while( zSql[0] && (zSql[0]!='*' || zSql[1]!='/') ){ zSql++; }
        if( zSql[0]==0 ) return 0;
        zSql++;
        token = tkWS;
        break;
      }
      case '-': {   /* SQL-style comments from "--" to end of line */
        if( zSql[1]!='-' ){
          token = tkOTHER;
          break;
        }
        while( *zSql && *zSql!='\n' ){ zSql++; }
        if( *zSql==0 ) return state==1;
        token = tkWS;
        break;
      }
      case '[': {   /* Microsoft-style identifiers in [...] */
        zSql++;
        while( *zSql && *zSql!=']' ){ zSql++; }
        if( *zSql==0 ) return 0;
        token = tkOTHER;
        break;
      }
      case '`':     /* Grave-accent quoted symbols used by MySQL */
      case '"':     /* single- and double-quoted strings */
      case '\'': {
        int c = *zSql;
        zSql++;
        while( *zSql && *zSql!=c ){ zSql++; }
        if( *zSql==0 ) return 0;
        token = tkOTHER;
        break;
      }
      default: {
#ifdef SQLITE_EBCDIC
        unsigned char c;
#endif
        if( IdChar((u8)*zSql) ){
          /* Keywords and unquoted identifiers */
          int nId;
          for(nId=1; IdChar(zSql[nId]); nId++){}
#ifdef SQLITE_OMIT_TRIGGER
          token = tkOTHER;
#else
          switch( *zSql ){
            case 'c': case 'C': {
              if( nId==6 && sqlite3StrNICmp(zSql, "create", 6)==0 ){
                token = tkCREATE;
              }else{
                token = tkOTHER;
              }
              break;
            }
            case 't': case 'T': {
              if( nId==7 && sqlite3StrNICmp(zSql, "trigger", 7)==0 ){
                token = tkTRIGGER;
              }else if( nId==4 && sqlite3StrNICmp(zSql, "temp", 4)==0 ){
                token = tkTEMP;
              }else if( nId==9 && sqlite3StrNICmp(zSql, "temporary", 9)==0 ){
                token = tkTEMP;
              }else{
                token = tkOTHER;
              }
              break;
            }
            case 'e':  case 'E': {
              if( nId==3 && sqlite3StrNICmp(zSql, "end", 3)==0 ){
                token = tkEND;
              }else
#ifndef SQLITE_OMIT_EXPLAIN
              if( nId==7 && sqlite3StrNICmp(zSql, "explain", 7)==0 ){
                token = tkEXPLAIN;
              }else
#endif
              {
                token = tkOTHER;
              }
              break;
            }
            default: {
              token = tkOTHER;
              break;
            }
          }
#endif /* SQLITE_OMIT_TRIGGER */
          zSql += nId-1;
        }else{
          /* Operators and special symbols */
          token = tkOTHER;
        }
        break;
      }
    }
    state = trans[state][token];
    zSql++;
  }
  return state==1;
}

#ifndef SQLITE_OMIT_UTF16
/*
** This routine is the same as the sqlite3_complete() routine described
** above, except that the parameter is required to be UTF-16 encoded, not
** UTF-8.
*/
SQLITE_API int sqlite3_complete16(const void *zSql){
  sqlite3_value *pVal;
  char const *zSql8;
  int rc = SQLITE_NOMEM;

#ifndef SQLITE_OMIT_AUTOINIT
  rc = sqlite3_initialize();
  if( rc ) return rc;
#endif
  pVal = sqlite3ValueNew(0);
  sqlite3ValueSetStr(pVal, -1, zSql, SQLITE_UTF16NATIVE, SQLITE_STATIC);
  zSql8 = sqlite3ValueText(pVal, SQLITE_UTF8);
  if( zSql8 ){
    rc = sqlite3_complete(zSql8);
  }else{
    rc = SQLITE_NOMEM;
  }
  sqlite3ValueFree(pVal);
  return sqlite3ApiExit(0, rc);
}
#endif /* SQLITE_OMIT_UTF16 */
#endif /* SQLITE_OMIT_COMPLETE */

/************** End of complete.c ********************************************/
/************** Begin file main.c ********************************************/
/*
** 2001 September 15
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** Main file for the SQLite library.  The routines in this file
** implement the programmer interface to the library.  Routines in
** other files are for internal use by SQLite and should not be
** accessed by users of the library.
*/

#ifdef SQLITE_ENABLE_FTS3
/************** Include fts3.h in the middle of main.c ***********************/
/************** Begin file fts3.h ********************************************/
/*
** 2006 Oct 10
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
** This header file is used by programs that want to link against the
** FTS3 library.  All it does is declare the sqlite3Fts3Init() interface.
*/

#if 0
extern "C" {
#endif  /* __cplusplus */

SQLITE_PRIVATE int sqlite3Fts3Init(sqlite3 *db);

#if 0
}  /* extern "C" */
#endif  /* __cplusplus */

/************** End of fts3.h ************************************************/
/************** Continuing where we left off in main.c ***********************/
#endif
#ifdef SQLITE_ENABLE_RTREE
/************** Include rtree.h in the middle of main.c **********************/
/************** Begin file rtree.h *******************************************/
/*
** 2008 May 26
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
** This header file is used by programs that want to link against the
** RTREE library.  All it does is declare the sqlite3RtreeInit() interface.
*/

#if 0
extern "C" {
#endif  /* __cplusplus */

SQLITE_PRIVATE int sqlite3RtreeInit(sqlite3 *db);

#if 0
}  /* extern "C" */
#endif  /* __cplusplus */

/************** End of rtree.h ***********************************************/
/************** Continuing where we left off in main.c ***********************/
#endif
#ifdef SQLITE_ENABLE_ICU
/************** Include sqliteicu.h in the middle of main.c ******************/
/************** Begin file sqliteicu.h ***************************************/
/*
** 2008 May 26
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
** This header file is used by programs that want to link against the
** ICU extension.  All it does is declare the sqlite3IcuInit() interface.
*/

#if 0
extern "C" {
#endif  /* __cplusplus */

SQLITE_PRIVATE int sqlite3IcuInit(sqlite3 *db);

#if 0
}  /* extern "C" */
#endif  /* __cplusplus */


/************** End of sqliteicu.h *******************************************/
/************** Continuing where we left off in main.c ***********************/
#endif

#ifndef SQLITE_AMALGAMATION
/* IMPLEMENTATION-OF: R-46656-45156 The sqlite3_version[] string constant
** contains the text of SQLITE_VERSION macro. 
*/
SQLITE_API const char sqlite3_version[] = SQLITE_VERSION;
#endif

/* IMPLEMENTATION-OF: R-53536-42575 The sqlite3_libversion() function returns
** a pointer to the to the sqlite3_version[] string constant. 
*/
SQLITE_API const char *sqlite3_libversion(void){ return sqlite3_version; }

/* IMPLEMENTATION-OF: R-63124-39300 The sqlite3_sourceid() function returns a
** pointer to a string constant whose value is the same as the
** SQLITE_SOURCE_ID C preprocessor macro. 
*/
SQLITE_API const char *sqlite3_sourceid(void){ return SQLITE_SOURCE_ID; }

/* IMPLEMENTATION-OF: R-35210-63508 The sqlite3_libversion_number() function
** returns an integer equal to SQLITE_VERSION_NUMBER.
*/
SQLITE_API int sqlite3_libversion_number(void){ return SQLITE_VERSION_NUMBER; }

/* IMPLEMENTATION-OF: R-54823-41343 The sqlite3_threadsafe() function returns
** zero if and only if SQLite was compiled mutexing code omitted due to
** the SQLITE_THREADSAFE compile-time option being set to 0.
*/
SQLITE_API int sqlite3_threadsafe(void){ return SQLITE_THREADSAFE; }

#if !defined(SQLITE_OMIT_TRACE) && defined(SQLITE_ENABLE_IOTRACE)
/*
** If the following function pointer is not NULL and if
** SQLITE_ENABLE_IOTRACE is enabled, then messages describing
** I/O active are written using this function.  These messages
** are intended for debugging activity only.
*/
SQLITE_PRIVATE void (*sqlite3IoTrace)(const char*, ...) = 0;
#endif

/*
** If the following global variable points to a string which is the
** name of a directory, then that directory will be used to store
** temporary files.
**
** See also the "PRAGMA temp_store_directory" SQL command.
*/
SQLITE_API char *sqlite3_temp_directory = 0;

/*
** Initialize SQLite.  
**
** This routine must be called to initialize the memory allocation,
** VFS, and mutex subsystems prior to doing any serious work with
** SQLite.  But as long as you do not compile with SQLITE_OMIT_AUTOINIT
** this routine will be called automatically by key routines such as
** sqlite3_open().  
**
** This routine is a no-op except on its very first call for the process,
** or for the first call after a call to sqlite3_shutdown.
**
** The first thread to call this routine runs the initialization to
** completion.  If subsequent threads call this routine before the first
** thread has finished the initialization process, then the subsequent
** threads must block until the first thread finishes with the initialization.
**
** The first thread might call this routine recursively.  Recursive
** calls to this routine should not block, of course.  Otherwise the
** initialization process would never complete.
**
** Let X be the first thread to enter this routine.  Let Y be some other
** thread.  Then while the initial invocation of this routine by X is
** incomplete, it is required that:
**
**    *  Calls to this routine from Y must block until the outer-most
**       call by X completes.
**
**    *  Recursive calls to this routine from thread X return immediately
**       without blocking.
*/
SQLITE_API int sqlite3_initialize(void){
  MUTEX_LOGIC( sqlite3_mutex *pMaster; )       /* The main static mutex */
  int rc;                                      /* Result code */

#ifdef SQLITE_OMIT_WSD
  rc = sqlite3_wsd_init(4096, 24);
  if( rc!=SQLITE_OK ){
    return rc;
  }
#endif

  /* If SQLite is already completely initialized, then this call
  ** to sqlite3_initialize() should be a no-op.  But the initialization
  ** must be complete.  So isInit must not be set until the very end
  ** of this routine.
  */
  if( sqlite3GlobalConfig.isInit ) return SQLITE_OK;

  /* Make sure the mutex subsystem is initialized.  If unable to 
  ** initialize the mutex subsystem, return early with the error.
  ** If the system is so sick that we are unable to allocate a mutex,
  ** there is not much SQLite is going to be able to do.
  **
  ** The mutex subsystem must take care of serializing its own
  ** initialization.
  */
  rc = sqlite3MutexInit();
  if( rc ) return rc;

  /* Initialize the malloc() system and the recursive pInitMutex mutex.
  ** This operation is protected by the STATIC_MASTER mutex.  Note that
  ** MutexAlloc() is called for a static mutex prior to initializing the
  ** malloc subsystem - this implies that the allocation of a static
  ** mutex must not require support from the malloc subsystem.
  */
  MUTEX_LOGIC( pMaster = sqlite3MutexAlloc(SQLITE_MUTEX_STATIC_MASTER); )
  sqlite3_mutex_enter(pMaster);
  sqlite3GlobalConfig.isMutexInit = 1;
  if( !sqlite3GlobalConfig.isMallocInit ){
    rc = sqlite3MallocInit();
  }
  if( rc==SQLITE_OK ){
    sqlite3GlobalConfig.isMallocInit = 1;
    if( !sqlite3GlobalConfig.pInitMutex ){
      sqlite3GlobalConfig.pInitMutex =
           sqlite3MutexAlloc(SQLITE_MUTEX_RECURSIVE);
      if( sqlite3GlobalConfig.bCoreMutex && !sqlite3GlobalConfig.pInitMutex ){
        rc = SQLITE_NOMEM;
      }
    }
  }
  if( rc==SQLITE_OK ){
    sqlite3GlobalConfig.nRefInitMutex++;
  }
  sqlite3_mutex_leave(pMaster);

  /* If rc is not SQLITE_OK at this point, then either the malloc
  ** subsystem could not be initialized or the system failed to allocate
  ** the pInitMutex mutex. Return an error in either case.  */
  if( rc!=SQLITE_OK ){
    return rc;
  }

  /* Do the rest of the initialization under the recursive mutex so
  ** that we will be able to handle recursive calls into
  ** sqlite3_initialize().  The recursive calls normally come through
  ** sqlite3_os_init() when it invokes sqlite3_vfs_register(), but other
  ** recursive calls might also be possible.
  **
  ** IMPLEMENTATION-OF: R-00140-37445 SQLite automatically serializes calls
  ** to the xInit method, so the xInit method need not be threadsafe.
  **
  ** The following mutex is what serializes access to the appdef pcache xInit
  ** methods.  The sqlite3_pcache_methods.xInit() all is embedded in the
  ** call to sqlite3PcacheInitialize().
  */
  sqlite3_mutex_enter(sqlite3GlobalConfig.pInitMutex);
  if( sqlite3GlobalConfig.isInit==0 && sqlite3GlobalConfig.inProgress==0 ){
    FuncDefHash *pHash = &GLOBAL(FuncDefHash, sqlite3GlobalFunctions);
    sqlite3GlobalConfig.inProgress = 1;
    memset(pHash, 0, sizeof(sqlite3GlobalFunctions));
    sqlite3RegisterGlobalFunctions();
    if( sqlite3GlobalConfig.isPCacheInit==0 ){
      rc = sqlite3PcacheInitialize();
    }
    if( rc==SQLITE_OK ){
      sqlite3GlobalConfig.isPCacheInit = 1;
      rc = sqlite3OsInit();
    }
    if( rc==SQLITE_OK ){
      sqlite3PCacheBufferSetup( sqlite3GlobalConfig.pPage, 
          sqlite3GlobalConfig.szPage, sqlite3GlobalConfig.nPage);
      sqlite3GlobalConfig.isInit = 1;
    }
    sqlite3GlobalConfig.inProgress = 0;
  }
  sqlite3_mutex_leave(sqlite3GlobalConfig.pInitMutex);

  /* Go back under the static mutex and clean up the recursive
  ** mutex to prevent a resource leak.
  */
  sqlite3_mutex_enter(pMaster);
  sqlite3GlobalConfig.nRefInitMutex--;
  if( sqlite3GlobalConfig.nRefInitMutex<=0 ){
    assert( sqlite3GlobalConfig.nRefInitMutex==0 );
    sqlite3_mutex_free(sqlite3GlobalConfig.pInitMutex);
    sqlite3GlobalConfig.pInitMutex = 0;
  }
  sqlite3_mutex_leave(pMaster);

  /* The following is just a sanity check to make sure SQLite has
  ** been compiled correctly.  It is important to run this code, but
  ** we don't want to run it too often and soak up CPU cycles for no
  ** reason.  So we run it once during initialization.
  */
#ifndef NDEBUG
#ifndef SQLITE_OMIT_FLOATING_POINT
  /* This section of code's only "output" is via assert() statements. */
  if ( rc==SQLITE_OK ){
    u64 x = (((u64)1)<<63)-1;
    double y;
    assert(sizeof(x)==8);
    assert(sizeof(x)==sizeof(y));
    memcpy(&y, &x, 8);
    assert( sqlite3IsNaN(y) );
  }
#endif
#endif

  /* Do extra initialization steps requested by the SQLITE_EXTRA_INIT
  ** compile-time option.
  */
#ifdef SQLITE_EXTRA_INIT
  if( rc==SQLITE_OK && sqlite3GlobalConfig.isInit ){
    int SQLITE_EXTRA_INIT(void);
    rc = SQLITE_EXTRA_INIT();
  }
#endif

  return rc;
}

/*
** Undo the effects of sqlite3_initialize().  Must not be called while
** there are outstanding database connections or memory allocations or
** while any part of SQLite is otherwise in use in any thread.  This
** routine is not threadsafe.  But it is safe to invoke this routine
** on when SQLite is already shut down.  If SQLite is already shut down
** when this routine is invoked, then this routine is a harmless no-op.
*/
SQLITE_API int sqlite3_shutdown(void){
  if( sqlite3GlobalConfig.isInit ){
    sqlite3_os_end();
    sqlite3_reset_auto_extension();
    sqlite3GlobalConfig.isInit = 0;
  }
  if( sqlite3GlobalConfig.isPCacheInit ){
    sqlite3PcacheShutdown();
    sqlite3GlobalConfig.isPCacheInit = 0;
  }
  if( sqlite3GlobalConfig.isMallocInit ){
    sqlite3MallocEnd();
    sqlite3GlobalConfig.isMallocInit = 0;
  }
  if( sqlite3GlobalConfig.isMutexInit ){
    sqlite3MutexEnd();
    sqlite3GlobalConfig.isMutexInit = 0;
  }

  return SQLITE_OK;
}

/*
** This API allows applications to modify the global configuration of
** the SQLite library at run-time.
**
** This routine should only be called when there are no outstanding
** database connections or memory allocations.  This routine is not
** threadsafe.  Failure to heed these warnings can lead to unpredictable
** behavior.
*/
SQLITE_API int sqlite3_config(int op, ...){
  va_list ap;
  int rc = SQLITE_OK;

  /* sqlite3_config() shall return SQLITE_MISUSE if it is invoked while
  ** the SQLite library is in use. */
  if( sqlite3GlobalConfig.isInit ) return SQLITE_MISUSE_BKPT;

  va_start(ap, op);
  switch( op ){

    /* Mutex configuration options are only available in a threadsafe
    ** compile. 
    */
#if defined(SQLITE_THREADSAFE) && SQLITE_THREADSAFE>0
    case SQLITE_CONFIG_SINGLETHREAD: {
      /* Disable all mutexing */
      sqlite3GlobalConfig.bCoreMutex = 0;
      sqlite3GlobalConfig.bFullMutex = 0;
      break;
    }
    case SQLITE_CONFIG_MULTITHREAD: {
      /* Disable mutexing of database connections */
      /* Enable mutexing of core data structures */
      sqlite3GlobalConfig.bCoreMutex = 1;
      sqlite3GlobalConfig.bFullMutex = 0;
      break;
    }
    case SQLITE_CONFIG_SERIALIZED: {
      /* Enable all mutexing */
      sqlite3GlobalConfig.bCoreMutex = 1;
      sqlite3GlobalConfig.bFullMutex = 1;
      break;
    }
    case SQLITE_CONFIG_MUTEX: {
      /* Specify an alternative mutex implementation */
      sqlite3GlobalConfig.mutex = *va_arg(ap, sqlite3_mutex_methods*);
      break;
    }
    case SQLITE_CONFIG_GETMUTEX: {
      /* Retrieve the current mutex implementation */
      *va_arg(ap, sqlite3_mutex_methods*) = sqlite3GlobalConfig.mutex;
      break;
    }
#endif


    case SQLITE_CONFIG_MALLOC: {
      /* Specify an alternative malloc implementation */
      sqlite3GlobalConfig.m = *va_arg(ap, sqlite3_mem_methods*);
      break;
    }
    case SQLITE_CONFIG_GETMALLOC: {
      /* Retrieve the current malloc() implementation */
      if( sqlite3GlobalConfig.m.xMalloc==0 ) sqlite3MemSetDefault();
      *va_arg(ap, sqlite3_mem_methods*) = sqlite3GlobalConfig.m;
      break;
    }
    case SQLITE_CONFIG_MEMSTATUS: {
      /* Enable or disable the malloc status collection */
      sqlite3GlobalConfig.bMemstat = va_arg(ap, int);
      break;
    }
    case SQLITE_CONFIG_SCRATCH: {
      /* Designate a buffer for scratch memory space */
      sqlite3GlobalConfig.pScratch = va_arg(ap, void*);
      sqlite3GlobalConfig.szScratch = va_arg(ap, int);
      sqlite3GlobalConfig.nScratch = va_arg(ap, int);
      break;
    }
    case SQLITE_CONFIG_PAGECACHE: {
      /* Designate a buffer for page cache memory space */
      sqlite3GlobalConfig.pPage = va_arg(ap, void*);
      sqlite3GlobalConfig.szPage = va_arg(ap, int);
      sqlite3GlobalConfig.nPage = va_arg(ap, int);
      break;
    }

    case SQLITE_CONFIG_PCACHE: {
      /* Specify an alternative page cache implementation */
      sqlite3GlobalConfig.pcache = *va_arg(ap, sqlite3_pcache_methods*);
      break;
    }

    case SQLITE_CONFIG_GETPCACHE: {
      if( sqlite3GlobalConfig.pcache.xInit==0 ){
        sqlite3PCacheSetDefault();
      }
      *va_arg(ap, sqlite3_pcache_methods*) = sqlite3GlobalConfig.pcache;
      break;
    }

#if defined(SQLITE_ENABLE_MEMSYS3) || defined(SQLITE_ENABLE_MEMSYS5)
    case SQLITE_CONFIG_HEAP: {
      /* Designate a buffer for heap memory space */
      sqlite3GlobalConfig.pHeap = va_arg(ap, void*);
      sqlite3GlobalConfig.nHeap = va_arg(ap, int);
      sqlite3GlobalConfig.mnReq = va_arg(ap, int);

      if( sqlite3GlobalConfig.mnReq<1 ){
        sqlite3GlobalConfig.mnReq = 1;
      }else if( sqlite3GlobalConfig.mnReq>(1<<12) ){
        /* cap min request size at 2^12 */
        sqlite3GlobalConfig.mnReq = (1<<12);
      }

      if( sqlite3GlobalConfig.pHeap==0 ){
        /* If the heap pointer is NULL, then restore the malloc implementation
        ** back to NULL pointers too.  This will cause the malloc to go
        ** back to its default implementation when sqlite3_initialize() is
        ** run.
        */
        memset(&sqlite3GlobalConfig.m, 0, sizeof(sqlite3GlobalConfig.m));
      }else{
        /* The heap pointer is not NULL, then install one of the
        ** mem5.c/mem3.c methods. If neither ENABLE_MEMSYS3 nor
        ** ENABLE_MEMSYS5 is defined, return an error.
        */
#ifdef SQLITE_ENABLE_MEMSYS3
        sqlite3GlobalConfig.m = *sqlite3MemGetMemsys3();
#endif
#ifdef SQLITE_ENABLE_MEMSYS5
        sqlite3GlobalConfig.m = *sqlite3MemGetMemsys5();
#endif
      }
      break;
    }
#endif

    case SQLITE_CONFIG_LOOKASIDE: {
      sqlite3GlobalConfig.szLookaside = va_arg(ap, int);
      sqlite3GlobalConfig.nLookaside = va_arg(ap, int);
      break;
    }
    
    /* Record a pointer to the logger funcction and its first argument.
    ** The default is NULL.  Logging is disabled if the function pointer is
    ** NULL.
    */
    case SQLITE_CONFIG_LOG: {
      /* MSVC is picky about pulling func ptrs from va lists.
      ** http://support.microsoft.com/kb/47961
      ** sqlite3GlobalConfig.xLog = va_arg(ap, void(*)(void*,int,const char*));
      */
      typedef void(*LOGFUNC_t)(void*,int,const char*);
      sqlite3GlobalConfig.xLog = va_arg(ap, LOGFUNC_t);
      sqlite3GlobalConfig.pLogArg = va_arg(ap, void*);
      break;
    }

    case SQLITE_CONFIG_URI: {
      sqlite3GlobalConfig.bOpenUri = va_arg(ap, int);
      break;
    }

    default: {
      rc = SQLITE_ERROR;
      break;
    }
  }
  va_end(ap);
  return rc;
}

/*
** Set up the lookaside buffers for a database connection.
** Return SQLITE_OK on success.  
** If lookaside is already active, return SQLITE_BUSY.
**
** The sz parameter is the number of bytes in each lookaside slot.
** The cnt parameter is the number of slots.  If pStart is NULL the
** space for the lookaside memory is obtained from sqlite3_malloc().
** If pStart is not NULL then it is sz*cnt bytes of memory to use for
** the lookaside memory.
*/
static int setupLookaside(sqlite3 *db, void *pBuf, int sz, int cnt){
  void *pStart;
  if( db->lookaside.nOut ){
    return SQLITE_BUSY;
  }
  /* Free any existing lookaside buffer for this handle before
  ** allocating a new one so we don't have to have space for 
  ** both at the same time.
  */
  if( db->lookaside.bMalloced ){
    sqlite3_free(db->lookaside.pStart);
  }
  /* The size of a lookaside slot needs to be larger than a pointer
  ** to be useful.
  */
  if( sz<=(int)sizeof(LookasideSlot*) ) sz = 0;
  if( cnt<0 ) cnt = 0;
  if( sz==0 || cnt==0 ){
    sz = 0;
    pStart = 0;
  }else if( pBuf==0 ){
    sz = ROUNDDOWN8(sz); /* IMP: R-33038-09382 */
    sqlite3BeginBenignMalloc();
    pStart = sqlite3Malloc( sz*cnt );  /* IMP: R-61949-35727 */
    sqlite3EndBenignMalloc();
  }else{
    sz = ROUNDDOWN8(sz); /* IMP: R-33038-09382 */
    pStart = pBuf;
  }
  db->lookaside.pStart = pStart;
  db->lookaside.pFree = 0;
  db->lookaside.sz = (u16)sz;
  if( pStart ){
    int i;
    LookasideSlot *p;
    assert( sz > (int)sizeof(LookasideSlot*) );
    p = (LookasideSlot*)pStart;
    for(i=cnt-1; i>=0; i--){
      p->pNext = db->lookaside.pFree;
      db->lookaside.pFree = p;
      p = (LookasideSlot*)&((u8*)p)[sz];
    }
    db->lookaside.pEnd = p;
    db->lookaside.bEnabled = 1;
    db->lookaside.bMalloced = pBuf==0 ?1:0;
  }else{
    db->lookaside.pEnd = 0;
    db->lookaside.bEnabled = 0;
    db->lookaside.bMalloced = 0;
  }
  return SQLITE_OK;
}

/*
** Return the mutex associated with a database connection.
*/
SQLITE_API sqlite3_mutex *sqlite3_db_mutex(sqlite3 *db){
  return db->mutex;
}

/*
** Configuration settings for an individual database connection
*/
SQLITE_API int sqlite3_db_config(sqlite3 *db, int op, ...){
  va_list ap;
  int rc;
  va_start(ap, op);
  switch( op ){
    case SQLITE_DBCONFIG_LOOKASIDE: {
      void *pBuf = va_arg(ap, void*); /* IMP: R-26835-10964 */
      int sz = va_arg(ap, int);       /* IMP: R-47871-25994 */
      int cnt = va_arg(ap, int);      /* IMP: R-04460-53386 */
      rc = setupLookaside(db, pBuf, sz, cnt);
      break;
    }
    default: {
      static const struct {
        int op;      /* The opcode */
        u32 mask;    /* Mask of the bit in sqlite3.flags to set/clear */
      } aFlagOp[] = {
        { SQLITE_DBCONFIG_ENABLE_FKEY,    SQLITE_ForeignKeys    },
        { SQLITE_DBCONFIG_ENABLE_TRIGGER, SQLITE_EnableTrigger  },
      };
      unsigned int i;
      rc = SQLITE_ERROR; /* IMP: R-42790-23372 */
      for(i=0; i<ArraySize(aFlagOp); i++){
        if( aFlagOp[i].op==op ){
          int onoff = va_arg(ap, int);
          int *pRes = va_arg(ap, int*);
          int oldFlags = db->flags;
          if( onoff>0 ){
            db->flags |= aFlagOp[i].mask;
          }else if( onoff==0 ){
            db->flags &= ~aFlagOp[i].mask;
          }
          if( oldFlags!=db->flags ){
            sqlite3ExpirePreparedStatements(db);
          }
          if( pRes ){
            *pRes = (db->flags & aFlagOp[i].mask)!=0;
          }
          rc = SQLITE_OK;
          break;
        }
      }
      break;
    }
  }
  va_end(ap);
  return rc;
}


/*
** Return true if the buffer z[0..n-1] contains all spaces.
*/
static int allSpaces(const char *z, int n){
  while( n>0 && z[n-1]==' ' ){ n--; }
  return n==0;
}

/*
** This is the default collating function named "BINARY" which is always
** available.
**
** If the padFlag argument is not NULL then space padding at the end
** of strings is ignored.  This implements the RTRIM collation.
*/
static int binCollFunc(
  void *padFlag,
  int nKey1, const void *pKey1,
  int nKey2, const void *pKey2
){
  int rc, n;
  n = nKey1<nKey2 ? nKey1 : nKey2;
  rc = memcmp(pKey1, pKey2, n);
  if( rc==0 ){
    if( padFlag
     && allSpaces(((char*)pKey1)+n, nKey1-n)
     && allSpaces(((char*)pKey2)+n, nKey2-n)
    ){
      /* Leave rc unchanged at 0 */
    }else{
      rc = nKey1 - nKey2;
    }
  }
  return rc;
}

/*
** Another built-in collating sequence: NOCASE. 
**
** This collating sequence is intended to be used for "case independant
** comparison". SQLite's knowledge of upper and lower case equivalents
** extends only to the 26 characters used in the English language.
**
** At the moment there is only a UTF-8 implementation.
*/
static int nocaseCollatingFunc(
  void *NotUsed,
  int nKey1, const void *pKey1,
  int nKey2, const void *pKey2
){
  int r = sqlite3StrNICmp(
      (const char *)pKey1, (const char *)pKey2, (nKey1<nKey2)?nKey1:nKey2);
  UNUSED_PARAMETER(NotUsed);
  if( 0==r ){
    r = nKey1-nKey2;
  }
  return r;
}

/*
** Return the ROWID of the most recent insert
*/
SQLITE_API sqlite_int64 sqlite3_last_insert_rowid(sqlite3 *db){
  return db->lastRowid;
}

/*
** Return the number of changes in the most recent call to sqlite3_exec().
*/
SQLITE_API int sqlite3_changes(sqlite3 *db){
  return db->nChange;
}

/*
** Return the number of changes since the database handle was opened.
*/
SQLITE_API int sqlite3_total_changes(sqlite3 *db){
  return db->nTotalChange;
}

/*
** Close all open savepoints. This function only manipulates fields of the
** database handle object, it does not close any savepoints that may be open
** at the b-tree/pager level.
*/
SQLITE_PRIVATE void sqlite3CloseSavepoints(sqlite3 *db){
  while( db->pSavepoint ){
    Savepoint *pTmp = db->pSavepoint;
    db->pSavepoint = pTmp->pNext;
    sqlite3DbFree(db, pTmp);
  }
  db->nSavepoint = 0;
  db->nStatement = 0;
  db->isTransactionSavepoint = 0;
}

/*
** Invoke the destructor function associated with FuncDef p, if any. Except,
** if this is not the last copy of the function, do not invoke it. Multiple
** copies of a single function are created when create_function() is called
** with SQLITE_ANY as the encoding.
*/
static void functionDestroy(sqlite3 *db, FuncDef *p){
  FuncDestructor *pDestructor = p->pDestructor;
  if( pDestructor ){
    pDestructor->nRef--;
    if( pDestructor->nRef==0 ){
      pDestructor->xDestroy(pDestructor->pUserData);
      sqlite3DbFree(db, pDestructor);
    }
  }
}

/*
** Close an existing SQLite database
*/
SQLITE_API int sqlite3_close(sqlite3 *db){
  HashElem *i;                    /* Hash table iterator */
  int j;

  if( !db ){
    return SQLITE_OK;
  }
  if( !sqlite3SafetyCheckSickOrOk(db) ){
    return SQLITE_MISUSE_BKPT;
  }
  sqlite3_mutex_enter(db->mutex);

  /* Force xDestroy calls on all virtual tables */
  sqlite3ResetInternalSchema(db, -1);

  /* If a transaction is open, the ResetInternalSchema() call above
  ** will not have called the xDisconnect() method on any virtual
  ** tables in the db->aVTrans[] array. The following sqlite3VtabRollback()
  ** call will do so. We need to do this before the check for active
  ** SQL statements below, as the v-table implementation may be storing
  ** some prepared statements internally.
  */
  sqlite3VtabRollback(db);

  /* If there are any outstanding VMs, return SQLITE_BUSY. */
  if( db->pVdbe ){
    sqlite3Error(db, SQLITE_BUSY, 
        "unable to close due to unfinalised statements");
    sqlite3_mutex_leave(db->mutex);
    return SQLITE_BUSY;
  }
  assert( sqlite3SafetyCheckSickOrOk(db) );

  for(j=0; j<db->nDb; j++){
    Btree *pBt = db->aDb[j].pBt;
    if( pBt && sqlite3BtreeIsInBackup(pBt) ){
      sqlite3Error(db, SQLITE_BUSY, 
          "unable to close due to unfinished backup operation");
      sqlite3_mutex_leave(db->mutex);
      return SQLITE_BUSY;
    }
  }

  /* Free any outstanding Savepoint structures. */
  sqlite3CloseSavepoints(db);

  for(j=0; j<db->nDb; j++){
    struct Db *pDb = &db->aDb[j];
    if( pDb->pBt ){
      sqlite3BtreeClose(pDb->pBt);
      pDb->pBt = 0;
      if( j!=1 ){
        pDb->pSchema = 0;
      }
    }
  }
  sqlite3ResetInternalSchema(db, -1);

  /* Tell the code in notify.c that the connection no longer holds any
  ** locks and does not require any further unlock-notify callbacks.
  */
  sqlite3ConnectionClosed(db);

  assert( db->nDb<=2 );
  assert( db->aDb==db->aDbStatic );
  for(j=0; j<ArraySize(db->aFunc.a); j++){
    FuncDef *pNext, *pHash, *p;
    for(p=db->aFunc.a[j]; p; p=pHash){
      pHash = p->pHash;
      while( p ){
        functionDestroy(db, p);
        pNext = p->pNext;
        sqlite3DbFree(db, p);
        p = pNext;
      }
    }
  }
  for(i=sqliteHashFirst(&db->aCollSeq); i; i=sqliteHashNext(i)){
    CollSeq *pColl = (CollSeq *)sqliteHashData(i);
    /* Invoke any destructors registered for collation sequence user data. */
    for(j=0; j<3; j++){
      if( pColl[j].xDel ){
        pColl[j].xDel(pColl[j].pUser);
      }
    }
    sqlite3DbFree(db, pColl);
  }
  sqlite3HashClear(&db->aCollSeq);
#ifndef SQLITE_OMIT_VIRTUALTABLE
  for(i=sqliteHashFirst(&db->aModule); i; i=sqliteHashNext(i)){
    Module *pMod = (Module *)sqliteHashData(i);
    if( pMod->xDestroy ){
      pMod->xDestroy(pMod->pAux);
    }
    sqlite3DbFree(db, pMod);
  }
  sqlite3HashClear(&db->aModule);
#endif

  sqlite3Error(db, SQLITE_OK, 0); /* Deallocates any cached error strings. */
  if( db->pErr ){
    sqlite3ValueFree(db->pErr);
  }
  sqlite3CloseExtensions(db);

  db->magic = SQLITE_MAGIC_ERROR;

  /* The temp-database schema is allocated differently from the other schema
  ** objects (using sqliteMalloc() directly, instead of sqlite3BtreeSchema()).
  ** So it needs to be freed here. Todo: Why not roll the temp schema into
  ** the same sqliteMalloc() as the one that allocates the database 
  ** structure?
  */
  sqlite3DbFree(db, db->aDb[1].pSchema);
  sqlite3_mutex_leave(db->mutex);
  db->magic = SQLITE_MAGIC_CLOSED;
  sqlite3_mutex_free(db->mutex);
  assert( db->lookaside.nOut==0 );  /* Fails on a lookaside memory leak */
  if( db->lookaside.bMalloced ){
    sqlite3_free(db->lookaside.pStart);
  }
  sqlite3_free(db);
  return SQLITE_OK;
}

/*
** Rollback all database files.
*/
SQLITE_PRIVATE void sqlite3RollbackAll(sqlite3 *db){
  int i;
  int inTrans = 0;
  assert( sqlite3_mutex_held(db->mutex) );
  sqlite3BeginBenignMalloc();
  for(i=0; i<db->nDb; i++){
    if( db->aDb[i].pBt ){
      if( sqlite3BtreeIsInTrans(db->aDb[i].pBt) ){
        inTrans = 1;
      }
      sqlite3BtreeRollback(db->aDb[i].pBt);
      db->aDb[i].inTrans = 0;
    }
  }
  sqlite3VtabRollback(db);
  sqlite3EndBenignMalloc();

  if( db->flags&SQLITE_InternChanges ){
    sqlite3ExpirePreparedStatements(db);
    sqlite3ResetInternalSchema(db, -1);
  }

  /* Any deferred constraint violations have now been resolved. */
  db->nDeferredCons = 0;

  /* If one has been configured, invoke the rollback-hook callback */
  if( db->xRollbackCallback && (inTrans || !db->autoCommit) ){
    db->xRollbackCallback(db->pRollbackArg);
  }
}

/*
** Return a static string that describes the kind of error specified in the
** argument.
*/
SQLITE_PRIVATE const char *sqlite3ErrStr(int rc){
  static const char* const aMsg[] = {
    /* SQLITE_OK          */ "not an error",
    /* SQLITE_ERROR       */ "SQL logic error or missing database",
    /* SQLITE_INTERNAL    */ 0,
    /* SQLITE_PERM        */ "access permission denied",
    /* SQLITE_ABORT       */ "callback requested query abort",
    /* SQLITE_BUSY        */ "database is locked",
    /* SQLITE_LOCKED      */ "database table is locked",
    /* SQLITE_NOMEM       */ "out of memory",
    /* SQLITE_READONLY    */ "attempt to write a readonly database",
    /* SQLITE_INTERRUPT   */ "interrupted",
    /* SQLITE_IOERR       */ "disk I/O error",
    /* SQLITE_CORRUPT     */ "database disk image is malformed",
    /* SQLITE_NOTFOUND    */ "unknown operation",
    /* SQLITE_FULL        */ "database or disk is full",
    /* SQLITE_CANTOPEN    */ "unable to open database file",
    /* SQLITE_PROTOCOL    */ "locking protocol",
    /* SQLITE_EMPTY       */ "table contains no data",
    /* SQLITE_SCHEMA      */ "database schema has changed",
    /* SQLITE_TOOBIG      */ "string or blob too big",
    /* SQLITE_CONSTRAINT  */ "constraint failed",
    /* SQLITE_MISMATCH    */ "datatype mismatch",
    /* SQLITE_MISUSE      */ "library routine called out of sequence",
    /* SQLITE_NOLFS       */ "large file support is disabled",
    /* SQLITE_AUTH        */ "authorization denied",
    /* SQLITE_FORMAT      */ "auxiliary database format error",
    /* SQLITE_RANGE       */ "bind or column index out of range",
    /* SQLITE_NOTADB      */ "file is encrypted or is not a database",
  };
  rc &= 0xff;
  if( ALWAYS(rc>=0) && rc<(int)(sizeof(aMsg)/sizeof(aMsg[0])) && aMsg[rc]!=0 ){
    return aMsg[rc];
  }else{
    return "unknown error";
  }
}

/*
** This routine implements a busy callback that sleeps and tries
** again until a timeout value is reached.  The timeout value is
** an integer number of milliseconds passed in as the first
** argument.
*/
static int sqliteDefaultBusyCallback(
 void *ptr,               /* Database connection */
 int count                /* Number of times table has been busy */
){
#if SQLITE_OS_WIN || (defined(HAVE_USLEEP) && HAVE_USLEEP)
  static const u8 delays[] =
     { 1, 2, 5, 10, 15, 20, 25, 25,  25,  50,  50, 100 };
  static const u8 totals[] =
     { 0, 1, 3,  8, 18, 33, 53, 78, 103, 128, 178, 228 };
# define NDELAY ArraySize(delays)
  sqlite3 *db = (sqlite3 *)ptr;
  int timeout = db->busyTimeout;
  int delay, prior;

  assert( count>=0 );
  if( count < NDELAY ){
    delay = delays[count];
    prior = totals[count];
  }else{
    delay = delays[NDELAY-1];
    prior = totals[NDELAY-1] + delay*(count-(NDELAY-1));
  }
  if( prior + delay > timeout ){
    delay = timeout - prior;
    if( delay<=0 ) return 0;
  }
  sqlite3OsSleep(db->pVfs, delay*1000);
  return 1;
#else
  sqlite3 *db = (sqlite3 *)ptr;
  int timeout = ((sqlite3 *)ptr)->busyTimeout;
  if( (count+1)*1000 > timeout ){
    return 0;
  }
  sqlite3OsSleep(db->pVfs, 1000000);
  return 1;
#endif
}

/*
** Invoke the given busy handler.
**
** This routine is called when an operation failed with a lock.
** If this routine returns non-zero, the lock is retried.  If it
** returns 0, the operation aborts with an SQLITE_BUSY error.
*/
SQLITE_PRIVATE int sqlite3InvokeBusyHandler(BusyHandler *p){
  int rc;
  if( NEVER(p==0) || p->xFunc==0 || p->nBusy<0 ) return 0;
  rc = p->xFunc(p->pArg, p->nBusy);
  if( rc==0 ){
    p->nBusy = -1;
  }else{
    p->nBusy++;
  }
  return rc; 
}

/*
** This routine sets the busy callback for an Sqlite database to the
** given callback function with the given argument.
*/
SQLITE_API int sqlite3_busy_handler(
  sqlite3 *db,
  int (*xBusy)(void*,int),
  void *pArg
){
  sqlite3_mutex_enter(db->mutex);
  db->busyHandler.xFunc = xBusy;
  db->busyHandler.pArg = pArg;
  db->busyHandler.nBusy = 0;
  sqlite3_mutex_leave(db->mutex);
  return SQLITE_OK;
}

#ifndef SQLITE_OMIT_PROGRESS_CALLBACK
/*
** This routine sets the progress callback for an Sqlite database to the
** given callback function with the given argument. The progress callback will
** be invoked every nOps opcodes.
*/
SQLITE_API void sqlite3_progress_handler(
  sqlite3 *db, 
  int nOps,
  int (*xProgress)(void*), 
  void *pArg
){
  sqlite3_mutex_enter(db->mutex);
  if( nOps>0 ){
    db->xProgress = xProgress;
    db->nProgressOps = nOps;
    db->pProgressArg = pArg;
  }else{
    db->xProgress = 0;
    db->nProgressOps = 0;
    db->pProgressArg = 0;
  }
  sqlite3_mutex_leave(db->mutex);
}
#endif


/*
** This routine installs a default busy handler that waits for the
** specified number of milliseconds before returning 0.
*/
SQLITE_API int sqlite3_busy_timeout(sqlite3 *db, int ms){
  if( ms>0 ){
    db->busyTimeout = ms;
    sqlite3_busy_handler(db, sqliteDefaultBusyCallback, (void*)db);
  }else{
    sqlite3_busy_handler(db, 0, 0);
  }
  return SQLITE_OK;
}

/*
** Cause any pending operation to stop at its earliest opportunity.
*/
SQLITE_API void sqlite3_interrupt(sqlite3 *db){
  db->u1.isInterrupted = 1;
}


/*
** This function is exactly the same as sqlite3_create_function(), except
** that it is designed to be called by internal code. The difference is
** that if a malloc() fails in sqlite3_create_function(), an error code
** is returned and the mallocFailed flag cleared. 
*/
SQLITE_PRIVATE int sqlite3CreateFunc(
  sqlite3 *db,
  const char *zFunctionName,
  int nArg,
  int enc,
  void *pUserData,
  void (*xFunc)(sqlite3_context*,int,sqlite3_value **),
  void (*xStep)(sqlite3_context*,int,sqlite3_value **),
  void (*xFinal)(sqlite3_context*),
  FuncDestructor *pDestructor
){
  FuncDef *p;
  int nName;

  assert( sqlite3_mutex_held(db->mutex) );
  if( zFunctionName==0 ||
      (xFunc && (xFinal || xStep)) || 
      (!xFunc && (xFinal && !xStep)) ||
      (!xFunc && (!xFinal && xStep)) ||
      (nArg<-1 || nArg>SQLITE_MAX_FUNCTION_ARG) ||
      (255<(nName = sqlite3Strlen30( zFunctionName))) ){
    return SQLITE_MISUSE_BKPT;
  }
  
#ifndef SQLITE_OMIT_UTF16
  /* If SQLITE_UTF16 is specified as the encoding type, transform this
  ** to one of SQLITE_UTF16LE or SQLITE_UTF16BE using the
  ** SQLITE_UTF16NATIVE macro. SQLITE_UTF16 is not used internally.
  **
  ** If SQLITE_ANY is specified, add three versions of the function
  ** to the hash table.
  */
  if( enc==SQLITE_UTF16 ){
    enc = SQLITE_UTF16NATIVE;
  }else if( enc==SQLITE_ANY ){
    int rc;
    rc = sqlite3CreateFunc(db, zFunctionName, nArg, SQLITE_UTF8,
         pUserData, xFunc, xStep, xFinal, pDestructor);
    if( rc==SQLITE_OK ){
      rc = sqlite3CreateFunc(db, zFunctionName, nArg, SQLITE_UTF16LE,
          pUserData, xFunc, xStep, xFinal, pDestructor);
    }
    if( rc!=SQLITE_OK ){
      return rc;
    }
    enc = SQLITE_UTF16BE;
  }
#else
  enc = SQLITE_UTF8;
#endif
  
  /* Check if an existing function is being overridden or deleted. If so,
  ** and there are active VMs, then return SQLITE_BUSY. If a function
  ** is being overridden/deleted but there are no active VMs, allow the
  ** operation to continue but invalidate all precompiled statements.
  */
  p = sqlite3FindFunction(db, zFunctionName, nName, nArg, (u8)enc, 0);
  if( p && p->iPrefEnc==enc && p->nArg==nArg ){
    if( db->activeVdbeCnt ){
      sqlite3Error(db, SQLITE_BUSY, 
        "unable to delete/modify user-function due to active statements");
      assert( !db->mallocFailed );
      return SQLITE_BUSY;
    }else{
      sqlite3ExpirePreparedStatements(db);
    }
  }

  p = sqlite3FindFunction(db, zFunctionName, nName, nArg, (u8)enc, 1);
  assert(p || db->mallocFailed);
  if( !p ){
    return SQLITE_NOMEM;
  }

  /* If an older version of the function with a configured destructor is
  ** being replaced invoke the destructor function here. */
  functionDestroy(db, p);

  if( pDestructor ){
    pDestructor->nRef++;
  }
  p->pDestructor = pDestructor;
  p->flags = 0;
  p->xFunc = xFunc;
  p->xStep = xStep;
  p->xFinalize = xFinal;
  p->pUserData = pUserData;
  p->nArg = (u16)nArg;
  return SQLITE_OK;
}

/*
** Create new user functions.
*/
SQLITE_API int sqlite3_create_function(
  sqlite3 *db,
  const char *zFunc,
  int nArg,
  int enc,
  void *p,
  void (*xFunc)(sqlite3_context*,int,sqlite3_value **),
  void (*xStep)(sqlite3_context*,int,sqlite3_value **),
  void (*xFinal)(sqlite3_context*)
){
  return sqlite3_create_function_v2(db, zFunc, nArg, enc, p, xFunc, xStep,
                                    xFinal, 0);
}

SQLITE_API int sqlite3_create_function_v2(
  sqlite3 *db,
  const char *zFunc,
  int nArg,
  int enc,
  void *p,
  void (*xFunc)(sqlite3_context*,int,sqlite3_value **),
  void (*xStep)(sqlite3_context*,int,sqlite3_value **),
  void (*xFinal)(sqlite3_context*),
  void (*xDestroy)(void *)
){
  int rc = SQLITE_ERROR;
  FuncDestructor *pArg = 0;
  sqlite3_mutex_enter(db->mutex);
  if( xDestroy ){
    pArg = (FuncDestructor *)sqlite3DbMallocZero(db, sizeof(FuncDestructor));
    if( !pArg ){
      xDestroy(p);
      goto out;
    }
    pArg->xDestroy = xDestroy;
    pArg->pUserData = p;
  }
  rc = sqlite3CreateFunc(db, zFunc, nArg, enc, p, xFunc, xStep, xFinal, pArg);
  if( pArg && pArg->nRef==0 ){
    assert( rc!=SQLITE_OK );
    xDestroy(p);
    sqlite3DbFree(db, pArg);
  }

 out:
  rc = sqlite3ApiExit(db, rc);
  sqlite3_mutex_leave(db->mutex);
  return rc;
}

#ifndef SQLITE_OMIT_UTF16
SQLITE_API int sqlite3_create_function16(
  sqlite3 *db,
  const void *zFunctionName,
  int nArg,
  int eTextRep,
  void *p,
  void (*xFunc)(sqlite3_context*,int,sqlite3_value**),
  void (*xStep)(sqlite3_context*,int,sqlite3_value**),
  void (*xFinal)(sqlite3_context*)
){
  int rc;
  char *zFunc8;
  sqlite3_mutex_enter(db->mutex);
  assert( !db->mallocFailed );
  zFunc8 = sqlite3Utf16to8(db, zFunctionName, -1, SQLITE_UTF16NATIVE);
  rc = sqlite3CreateFunc(db, zFunc8, nArg, eTextRep, p, xFunc, xStep, xFinal,0);
  sqlite3DbFree(db, zFunc8);
  rc = sqlite3ApiExit(db, rc);
  sqlite3_mutex_leave(db->mutex);
  return rc;
}
#endif


/*
** Declare that a function has been overloaded by a virtual table.
**
** If the function already exists as a regular global function, then
** this routine is a no-op.  If the function does not exist, then create
** a new one that always throws a run-time error.  
**
** When virtual tables intend to provide an overloaded function, they
** should call this routine to make sure the global function exists.
** A global function must exist in order for name resolution to work
** properly.
*/
SQLITE_API int sqlite3_overload_function(
  sqlite3 *db,
  const char *zName,
  int nArg
){
  int nName = sqlite3Strlen30(zName);
  int rc = SQLITE_OK;
  sqlite3_mutex_enter(db->mutex);
  if( sqlite3FindFunction(db, zName, nName, nArg, SQLITE_UTF8, 0)==0 ){
    rc = sqlite3CreateFunc(db, zName, nArg, SQLITE_UTF8,
                           0, sqlite3InvalidFunction, 0, 0, 0);
  }
  rc = sqlite3ApiExit(db, rc);
  sqlite3_mutex_leave(db->mutex);
  return rc;
}

#ifndef SQLITE_OMIT_TRACE
/*
** Register a trace function.  The pArg from the previously registered trace
** is returned.  
**
** A NULL trace function means that no tracing is executes.  A non-NULL
** trace is a pointer to a function that is invoked at the start of each
** SQL statement.
*/
SQLITE_API void *sqlite3_trace(sqlite3 *db, void (*xTrace)(void*,const char*), void *pArg){
  void *pOld;
  sqlite3_mutex_enter(db->mutex);
  pOld = db->pTraceArg;
  db->xTrace = xTrace;
  db->pTraceArg = pArg;
  sqlite3_mutex_leave(db->mutex);
  return pOld;
}
/*
** Register a profile function.  The pArg from the previously registered 
** profile function is returned.  
**
** A NULL profile function means that no profiling is executes.  A non-NULL
** profile is a pointer to a function that is invoked at the conclusion of
** each SQL statement that is run.
*/
SQLITE_API void *sqlite3_profile(
  sqlite3 *db,
  void (*xProfile)(void*,const char*,sqlite_uint64),
  void *pArg
){
  void *pOld;
  sqlite3_mutex_enter(db->mutex);
  pOld = db->pProfileArg;
  db->xProfile = xProfile;
  db->pProfileArg = pArg;
  sqlite3_mutex_leave(db->mutex);
  return pOld;
}
#endif /* SQLITE_OMIT_TRACE */

/*** EXPERIMENTAL ***
**
** Register a function to be invoked when a transaction comments.
** If the invoked function returns non-zero, then the commit becomes a
** rollback.
*/
SQLITE_API void *sqlite3_commit_hook(
  sqlite3 *db,              /* Attach the hook to this database */
  int (*xCallback)(void*),  /* Function to invoke on each commit */
  void *pArg                /* Argument to the function */
){
  void *pOld;
  sqlite3_mutex_enter(db->mutex);
  pOld = db->pCommitArg;
  db->xCommitCallback = xCallback;
  db->pCommitArg = pArg;
  sqlite3_mutex_leave(db->mutex);
  return pOld;
}

/*
** Register a callback to be invoked each time a row is updated,
** inserted or deleted using this database connection.
*/
SQLITE_API void *sqlite3_update_hook(
  sqlite3 *db,              /* Attach the hook to this database */
  void (*xCallback)(void*,int,char const *,char const *,sqlite_int64),
  void *pArg                /* Argument to the function */
){
  void *pRet;
  sqlite3_mutex_enter(db->mutex);
  pRet = db->pUpdateArg;
  db->xUpdateCallback = xCallback;
  db->pUpdateArg = pArg;
  sqlite3_mutex_leave(db->mutex);
  return pRet;
}

/*
** Register a callback to be invoked each time a transaction is rolled
** back by this database connection.
*/
SQLITE_API void *sqlite3_rollback_hook(
  sqlite3 *db,              /* Attach the hook to this database */
  void (*xCallback)(void*), /* Callback function */
  void *pArg                /* Argument to the function */
){
  void *pRet;
  sqlite3_mutex_enter(db->mutex);
  pRet = db->pRollbackArg;
  db->xRollbackCallback = xCallback;
  db->pRollbackArg = pArg;
  sqlite3_mutex_leave(db->mutex);
  return pRet;
}

#ifndef SQLITE_OMIT_WAL
/*
** The sqlite3_wal_hook() callback registered by sqlite3_wal_autocheckpoint().
** Invoke sqlite3_wal_checkpoint if the number of frames in the log file
** is greater than sqlite3.pWalArg cast to an integer (the value configured by
** wal_autocheckpoint()).
*/ 
SQLITE_PRIVATE int sqlite3WalDefaultHook(
  void *pClientData,     /* Argument */
  sqlite3 *db,           /* Connection */
  const char *zDb,       /* Database */
  int nFrame             /* Size of WAL */
){
  if( nFrame>=SQLITE_PTR_TO_INT(pClientData) ){
    sqlite3BeginBenignMalloc();
    sqlite3_wal_checkpoint(db, zDb);
    sqlite3EndBenignMalloc();
  }
  return SQLITE_OK;
}
#endif /* SQLITE_OMIT_WAL */

/*
** Configure an sqlite3_wal_hook() callback to automatically checkpoint
** a database after committing a transaction if there are nFrame or
** more frames in the log file. Passing zero or a negative value as the
** nFrame parameter disables automatic checkpoints entirely.
**
** The callback registered by this function replaces any existing callback
** registered using sqlite3_wal_hook(). Likewise, registering a callback
** using sqlite3_wal_hook() disables the automatic checkpoint mechanism
** configured by this function.
*/
SQLITE_API int sqlite3_wal_autocheckpoint(sqlite3 *db, int nFrame){
#ifdef SQLITE_OMIT_WAL
  UNUSED_PARAMETER(db);
  UNUSED_PARAMETER(nFrame);
#else
  if( nFrame>0 ){
    sqlite3_wal_hook(db, sqlite3WalDefaultHook, SQLITE_INT_TO_PTR(nFrame));
  }else{
    sqlite3_wal_hook(db, 0, 0);
  }
#endif
  return SQLITE_OK;
}

/*
** Register a callback to be invoked each time a transaction is written
** into the write-ahead-log by this database connection.
*/
SQLITE_API void *sqlite3_wal_hook(
  sqlite3 *db,                    /* Attach the hook to this db handle */
  int(*xCallback)(void *, sqlite3*, const char*, int),
  void *pArg                      /* First argument passed to xCallback() */
){
#ifndef SQLITE_OMIT_WAL
  void *pRet;
  sqlite3_mutex_enter(db->mutex);
  pRet = db->pWalArg;
  db->xWalCallback = xCallback;
  db->pWalArg = pArg;
  sqlite3_mutex_leave(db->mutex);
  return pRet;
#else
  return 0;
#endif
}

/*
** Checkpoint database zDb.
*/
SQLITE_API int sqlite3_wal_checkpoint_v2(
  sqlite3 *db,                    /* Database handle */
  const char *zDb,                /* Name of attached database (or NULL) */
  int eMode,                      /* SQLITE_CHECKPOINT_* value */
  int *pnLog,                     /* OUT: Size of WAL log in frames */
  int *pnCkpt                     /* OUT: Total number of frames checkpointed */
){
#ifdef SQLITE_OMIT_WAL
  return SQLITE_OK;
#else
  int rc;                         /* Return code */
  int iDb = SQLITE_MAX_ATTACHED;  /* sqlite3.aDb[] index of db to checkpoint */

  /* Initialize the output variables to -1 in case an error occurs. */
  if( pnLog ) *pnLog = -1;
  if( pnCkpt ) *pnCkpt = -1;

  assert( SQLITE_CHECKPOINT_FULL>SQLITE_CHECKPOINT_PASSIVE );
  assert( SQLITE_CHECKPOINT_FULL<SQLITE_CHECKPOINT_RESTART );
  assert( SQLITE_CHECKPOINT_PASSIVE+2==SQLITE_CHECKPOINT_RESTART );
  if( eMode<SQLITE_CHECKPOINT_PASSIVE || eMode>SQLITE_CHECKPOINT_RESTART ){
    return SQLITE_MISUSE;
  }

  sqlite3_mutex_enter(db->mutex);
  if( zDb && zDb[0] ){
    iDb = sqlite3FindDbName(db, zDb);
  }
  if( iDb<0 ){
    rc = SQLITE_ERROR;
    sqlite3Error(db, SQLITE_ERROR, "unknown database: %s", zDb);
  }else{
    rc = sqlite3Checkpoint(db, iDb, eMode, pnLog, pnCkpt);
    sqlite3Error(db, rc, 0);
  }
  rc = sqlite3ApiExit(db, rc);
  sqlite3_mutex_leave(db->mutex);
  return rc;
#endif
}


/*
** Checkpoint database zDb. If zDb is NULL, or if the buffer zDb points
** to contains a zero-length string, all attached databases are 
** checkpointed.
*/
SQLITE_API int sqlite3_wal_checkpoint(sqlite3 *db, const char *zDb){
  return sqlite3_wal_checkpoint_v2(db, zDb, SQLITE_CHECKPOINT_PASSIVE, 0, 0);
}

#ifndef SQLITE_OMIT_WAL
/*
** Run a checkpoint on database iDb. This is a no-op if database iDb is
** not currently open in WAL mode.
**
** If a transaction is open on the database being checkpointed, this 
** function returns SQLITE_LOCKED and a checkpoint is not attempted. If 
** an error occurs while running the checkpoint, an SQLite error code is 
** returned (i.e. SQLITE_IOERR). Otherwise, SQLITE_OK.
**
** The mutex on database handle db should be held by the caller. The mutex
** associated with the specific b-tree being checkpointed is taken by
** this function while the checkpoint is running.
**
** If iDb is passed SQLITE_MAX_ATTACHED, then all attached databases are
** checkpointed. If an error is encountered it is returned immediately -
** no attempt is made to checkpoint any remaining databases.
**
** Parameter eMode is one of SQLITE_CHECKPOINT_PASSIVE, FULL or RESTART.
*/
SQLITE_PRIVATE int sqlite3Checkpoint(sqlite3 *db, int iDb, int eMode, int *pnLog, int *pnCkpt){
  int rc = SQLITE_OK;             /* Return code */
  int i;                          /* Used to iterate through attached dbs */
  int bBusy = 0;                  /* True if SQLITE_BUSY has been encountered */

  assert( sqlite3_mutex_held(db->mutex) );
  assert( !pnLog || *pnLog==-1 );
  assert( !pnCkpt || *pnCkpt==-1 );

  for(i=0; i<db->nDb && rc==SQLITE_OK; i++){
    if( i==iDb || iDb==SQLITE_MAX_ATTACHED ){
      rc = sqlite3BtreeCheckpoint(db->aDb[i].pBt, eMode, pnLog, pnCkpt);
      pnLog = 0;
      pnCkpt = 0;
      if( rc==SQLITE_BUSY ){
        bBusy = 1;
        rc = SQLITE_OK;
      }
    }
  }

  return (rc==SQLITE_OK && bBusy) ? SQLITE_BUSY : rc;
}
#endif /* SQLITE_OMIT_WAL */

/*
** This function returns true if main-memory should be used instead of
** a temporary file for transient pager files and statement journals.
** The value returned depends on the value of db->temp_store (runtime
** parameter) and the compile time value of SQLITE_TEMP_STORE. The
** following table describes the relationship between these two values
** and this functions return value.
**
**   SQLITE_TEMP_STORE     db->temp_store     Location of temporary database
**   -----------------     --------------     ------------------------------
**   0                     any                file      (return 0)
**   1                     1                  file      (return 0)
**   1                     2                  memory    (return 1)
**   1                     0                  file      (return 0)
**   2                     1                  file      (return 0)
**   2                     2                  memory    (return 1)
**   2                     0                  memory    (return 1)
**   3                     any                memory    (return 1)
*/
SQLITE_PRIVATE int sqlite3TempInMemory(const sqlite3 *db){
#if SQLITE_TEMP_STORE==1
  return ( db->temp_store==2 );
#endif
#if SQLITE_TEMP_STORE==2
  return ( db->temp_store!=1 );
#endif
#if SQLITE_TEMP_STORE==3
  return 1;
#endif
#if SQLITE_TEMP_STORE<1 || SQLITE_TEMP_STORE>3
  return 0;
#endif
}

/*
** Return UTF-8 encoded English language explanation of the most recent
** error.
*/
SQLITE_API const char *sqlite3_errmsg(sqlite3 *db){
  const char *z;
  if( !db ){
    return sqlite3ErrStr(SQLITE_NOMEM);
  }
  if( !sqlite3SafetyCheckSickOrOk(db) ){
    return sqlite3ErrStr(SQLITE_MISUSE_BKPT);
  }
  sqlite3_mutex_enter(db->mutex);
  if( db->mallocFailed ){
    z = sqlite3ErrStr(SQLITE_NOMEM);
  }else{
    z = (char*)sqlite3_value_text(db->pErr);
    assert( !db->mallocFailed );
    if( z==0 ){
      z = sqlite3ErrStr(db->errCode);
    }
  }
  sqlite3_mutex_leave(db->mutex);
  return z;
}

#ifndef SQLITE_OMIT_UTF16
/*
** Return UTF-16 encoded English language explanation of the most recent
** error.
*/
SQLITE_API const void *sqlite3_errmsg16(sqlite3 *db){
  static const u16 outOfMem[] = {
    'o', 'u', 't', ' ', 'o', 'f', ' ', 'm', 'e', 'm', 'o', 'r', 'y', 0
  };
  static const u16 misuse[] = {
    'l', 'i', 'b', 'r', 'a', 'r', 'y', ' ', 
    'r', 'o', 'u', 't', 'i', 'n', 'e', ' ', 
    'c', 'a', 'l', 'l', 'e', 'd', ' ', 
    'o', 'u', 't', ' ', 
    'o', 'f', ' ', 
    's', 'e', 'q', 'u', 'e', 'n', 'c', 'e', 0
  };

  const void *z;
  if( !db ){
    return (void *)outOfMem;
  }
  if( !sqlite3SafetyCheckSickOrOk(db) ){
    return (void *)misuse;
  }
  sqlite3_mutex_enter(db->mutex);
  if( db->mallocFailed ){
    z = (void *)outOfMem;
  }else{
    z = sqlite3_value_text16(db->pErr);
    if( z==0 ){
      sqlite3ValueSetStr(db->pErr, -1, sqlite3ErrStr(db->errCode),
           SQLITE_UTF8, SQLITE_STATIC);
      z = sqlite3_value_text16(db->pErr);
    }
    /* A malloc() may have failed within the call to sqlite3_value_text16()
    ** above. If this is the case, then the db->mallocFailed flag needs to
    ** be cleared before returning. Do this directly, instead of via
    ** sqlite3ApiExit(), to avoid setting the database handle error message.
    */
    db->mallocFailed = 0;
  }
  sqlite3_mutex_leave(db->mutex);
  return z;
}
#endif /* SQLITE_OMIT_UTF16 */

/*
** Return the most recent error code generated by an SQLite routine. If NULL is
** passed to this function, we assume a malloc() failed during sqlite3_open().
*/
SQLITE_API int sqlite3_errcode(sqlite3 *db){
  if( db && !sqlite3SafetyCheckSickOrOk(db) ){
    return SQLITE_MISUSE_BKPT;
  }
  if( !db || db->mallocFailed ){
    return SQLITE_NOMEM;
  }
  return db->errCode & db->errMask;
}
SQLITE_API int sqlite3_extended_errcode(sqlite3 *db){
  if( db && !sqlite3SafetyCheckSickOrOk(db) ){
    return SQLITE_MISUSE_BKPT;
  }
  if( !db || db->mallocFailed ){
    return SQLITE_NOMEM;
  }
  return db->errCode;
}

/*
** Create a new collating function for database "db".  The name is zName
** and the encoding is enc.
*/
static int createCollation(
  sqlite3* db,
  const char *zName, 
  u8 enc,
  u8 collType,
  void* pCtx,
  int(*xCompare)(void*,int,const void*,int,const void*),
  void(*xDel)(void*)
){
  CollSeq *pColl;
  int enc2;
  int nName = sqlite3Strlen30(zName);
  
  assert( sqlite3_mutex_held(db->mutex) );

  /* If SQLITE_UTF16 is specified as the encoding type, transform this
  ** to one of SQLITE_UTF16LE or SQLITE_UTF16BE using the
  ** SQLITE_UTF16NATIVE macro. SQLITE_UTF16 is not used internally.
  */
  enc2 = enc;
  testcase( enc2==SQLITE_UTF16 );
  testcase( enc2==SQLITE_UTF16_ALIGNED );
  if( enc2==SQLITE_UTF16 || enc2==SQLITE_UTF16_ALIGNED ){
    enc2 = SQLITE_UTF16NATIVE;
  }
  if( enc2<SQLITE_UTF8 || enc2>SQLITE_UTF16BE ){
    return SQLITE_MISUSE_BKPT;
  }

  /* Check if this call is removing or replacing an existing collation 
  ** sequence. If so, and there are active VMs, return busy. If there
  ** are no active VMs, invalidate any pre-compiled statements.
  */
  pColl = sqlite3FindCollSeq(db, (u8)enc2, zName, 0);
  if( pColl && pColl->xCmp ){
    if( db->activeVdbeCnt ){
      sqlite3Error(db, SQLITE_BUSY, 
        "unable to delete/modify collation sequence due to active statements");
      return SQLITE_BUSY;
    }
    sqlite3ExpirePreparedStatements(db);

    /* If collation sequence pColl was created directly by a call to
    ** sqlite3_create_collation, and not generated by synthCollSeq(),
    ** then any copies made by synthCollSeq() need to be invalidated.
    ** Also, collation destructor - CollSeq.xDel() - function may need
    ** to be called.
    */ 
    if( (pColl->enc & ~SQLITE_UTF16_ALIGNED)==enc2 ){
      CollSeq *aColl = sqlite3HashFind(&db->aCollSeq, zName, nName);
      int j;
      for(j=0; j<3; j++){
        CollSeq *p = &aColl[j];
        if( p->enc==pColl->enc ){
          if( p->xDel ){
            p->xDel(p->pUser);
          }
          p->xCmp = 0;
        }
      }
    }
  }

  pColl = sqlite3FindCollSeq(db, (u8)enc2, zName, 1);
  if( pColl==0 ) return SQLITE_NOMEM;
  pColl->xCmp = xCompare;
  pColl->pUser = pCtx;
  pColl->xDel = xDel;
  pColl->enc = (u8)(enc2 | (enc & SQLITE_UTF16_ALIGNED));
  pColl->type = collType;
  sqlite3Error(db, SQLITE_OK, 0);
  return SQLITE_OK;
}


/*
** This array defines hard upper bounds on limit values.  The
** initializer must be kept in sync with the SQLITE_LIMIT_*
** #defines in sqlite3.h.
*/
static const int aHardLimit[] = {
  SQLITE_MAX_LENGTH,
  SQLITE_MAX_SQL_LENGTH,
  SQLITE_MAX_COLUMN,
  SQLITE_MAX_EXPR_DEPTH,
  SQLITE_MAX_COMPOUND_SELECT,
  SQLITE_MAX_VDBE_OP,
  SQLITE_MAX_FUNCTION_ARG,
  SQLITE_MAX_ATTACHED,
  SQLITE_MAX_LIKE_PATTERN_LENGTH,
  SQLITE_MAX_VARIABLE_NUMBER,
  SQLITE_MAX_TRIGGER_DEPTH,
};

/*
** Make sure the hard limits are set to reasonable values
*/
#if SQLITE_MAX_LENGTH<100
# error SQLITE_MAX_LENGTH must be at least 100
#endif
#if SQLITE_MAX_SQL_LENGTH<100
# error SQLITE_MAX_SQL_LENGTH must be at least 100
#endif
#if SQLITE_MAX_SQL_LENGTH>SQLITE_MAX_LENGTH
# error SQLITE_MAX_SQL_LENGTH must not be greater than SQLITE_MAX_LENGTH
#endif
#if SQLITE_MAX_COMPOUND_SELECT<2
# error SQLITE_MAX_COMPOUND_SELECT must be at least 2
#endif
#if SQLITE_MAX_VDBE_OP<40
# error SQLITE_MAX_VDBE_OP must be at least 40
#endif
#if SQLITE_MAX_FUNCTION_ARG<0 || SQLITE_MAX_FUNCTION_ARG>1000
# error SQLITE_MAX_FUNCTION_ARG must be between 0 and 1000
#endif
#if SQLITE_MAX_ATTACHED<0 || SQLITE_MAX_ATTACHED>62
# error SQLITE_MAX_ATTACHED must be between 0 and 62
#endif
#if SQLITE_MAX_LIKE_PATTERN_LENGTH<1
# error SQLITE_MAX_LIKE_PATTERN_LENGTH must be at least 1
#endif
#if SQLITE_MAX_COLUMN>32767
# error SQLITE_MAX_COLUMN must not exceed 32767
#endif
#if SQLITE_MAX_TRIGGER_DEPTH<1
# error SQLITE_MAX_TRIGGER_DEPTH must be at least 1
#endif


/*
** Change the value of a limit.  Report the old value.
** If an invalid limit index is supplied, report -1.
** Make no changes but still report the old value if the
** new limit is negative.
**
** A new lower limit does not shrink existing constructs.
** It merely prevents new constructs that exceed the limit
** from forming.
*/
SQLITE_API int sqlite3_limit(sqlite3 *db, int limitId, int newLimit){
  int oldLimit;


  /* EVIDENCE-OF: R-30189-54097 For each limit category SQLITE_LIMIT_NAME
  ** there is a hard upper bound set at compile-time by a C preprocessor
  ** macro called SQLITE_MAX_NAME. (The "_LIMIT_" in the name is changed to
  ** "_MAX_".)
  */
  assert( aHardLimit[SQLITE_LIMIT_LENGTH]==SQLITE_MAX_LENGTH );
  assert( aHardLimit[SQLITE_LIMIT_SQL_LENGTH]==SQLITE_MAX_SQL_LENGTH );
  assert( aHardLimit[SQLITE_LIMIT_COLUMN]==SQLITE_MAX_COLUMN );
  assert( aHardLimit[SQLITE_LIMIT_EXPR_DEPTH]==SQLITE_MAX_EXPR_DEPTH );
  assert( aHardLimit[SQLITE_LIMIT_COMPOUND_SELECT]==SQLITE_MAX_COMPOUND_SELECT);
  assert( aHardLimit[SQLITE_LIMIT_VDBE_OP]==SQLITE_MAX_VDBE_OP );
  assert( aHardLimit[SQLITE_LIMIT_FUNCTION_ARG]==SQLITE_MAX_FUNCTION_ARG );
  assert( aHardLimit[SQLITE_LIMIT_ATTACHED]==SQLITE_MAX_ATTACHED );
  assert( aHardLimit[SQLITE_LIMIT_LIKE_PATTERN_LENGTH]==
                                               SQLITE_MAX_LIKE_PATTERN_LENGTH );
  assert( aHardLimit[SQLITE_LIMIT_VARIABLE_NUMBER]==SQLITE_MAX_VARIABLE_NUMBER);
  assert( aHardLimit[SQLITE_LIMIT_TRIGGER_DEPTH]==SQLITE_MAX_TRIGGER_DEPTH );
  assert( SQLITE_LIMIT_TRIGGER_DEPTH==(SQLITE_N_LIMIT-1) );


  if( limitId<0 || limitId>=SQLITE_N_LIMIT ){
    return -1;
  }
  oldLimit = db->aLimit[limitId];
  if( newLimit>=0 ){                   /* IMP: R-52476-28732 */
    if( newLimit>aHardLimit[limitId] ){
      newLimit = aHardLimit[limitId];  /* IMP: R-51463-25634 */
    }
    db->aLimit[limitId] = newLimit;
  }
  return oldLimit;                     /* IMP: R-53341-35419 */
}

/*
** This function is used to parse both URIs and non-URI filenames passed by the
** user to API functions sqlite3_open() or sqlite3_open_v2(), and for database
** URIs specified as part of ATTACH statements.
**
** The first argument to this function is the name of the VFS to use (or
** a NULL to signify the default VFS) if the URI does not contain a "vfs=xxx"
** query parameter. The second argument contains the URI (or non-URI filename)
** itself. When this function is called the *pFlags variable should contain
** the default flags to open the database handle with. The value stored in
** *pFlags may be updated before returning if the URI filename contains 
** "cache=xxx" or "mode=xxx" query parameters.
**
** If successful, SQLITE_OK is returned. In this case *ppVfs is set to point to
** the VFS that should be used to open the database file. *pzFile is set to
** point to a buffer containing the name of the file to open. It is the 
** responsibility of the caller to eventually call sqlite3_free() to release
** this buffer.
**
** If an error occurs, then an SQLite error code is returned and *pzErrMsg
** may be set to point to a buffer containing an English language error 
** message. It is the responsibility of the caller to eventually release
** this buffer by calling sqlite3_free().
*/
SQLITE_PRIVATE int sqlite3ParseUri(
  const char *zDefaultVfs,        /* VFS to use if no "vfs=xxx" query option */
  const char *zUri,               /* Nul-terminated URI to parse */
  unsigned int *pFlags,           /* IN/OUT: SQLITE_OPEN_XXX flags */
  sqlite3_vfs **ppVfs,            /* OUT: VFS to use */ 
  char **pzFile,                  /* OUT: Filename component of URI */
  char **pzErrMsg                 /* OUT: Error message (if rc!=SQLITE_OK) */
){
  int rc = SQLITE_OK;
  unsigned int flags = *pFlags;
  const char *zVfs = zDefaultVfs;
  char *zFile;
  char c;
  int nUri = sqlite3Strlen30(zUri);

  assert( *pzErrMsg==0 );

  if( ((flags & SQLITE_OPEN_URI) || sqlite3GlobalConfig.bOpenUri) 
   && nUri>=5 && memcmp(zUri, "file:", 5)==0 
  ){
    char *zOpt;
    int eState;                   /* Parser state when parsing URI */
    int iIn;                      /* Input character index */
    int iOut = 0;                 /* Output character index */
    int nByte = nUri+2;           /* Bytes of space to allocate */

    /* Make sure the SQLITE_OPEN_URI flag is set to indicate to the VFS xOpen 
    ** method that there may be extra parameters following the file-name.  */
    flags |= SQLITE_OPEN_URI;

    for(iIn=0; iIn<nUri; iIn++) nByte += (zUri[iIn]=='&');
    zFile = sqlite3_malloc(nByte);
    if( !zFile ) return SQLITE_NOMEM;

    /* Discard the scheme and authority segments of the URI. */
    if( zUri[5]=='/' && zUri[6]=='/' ){
      iIn = 7;
      while( zUri[iIn] && zUri[iIn]!='/' ) iIn++;

      if( iIn!=7 && (iIn!=16 || memcmp("localhost", &zUri[7], 9)) ){
        *pzErrMsg = sqlite3_mprintf("invalid uri authority: %.*s", 
            iIn-7, &zUri[7]);
        rc = SQLITE_ERROR;
        goto parse_uri_out;
      }
    }else{
      iIn = 5;
    }

    /* Copy the filename and any query parameters into the zFile buffer. 
    ** Decode %HH escape codes along the way. 
    **
    ** Within this loop, variable eState may be set to 0, 1 or 2, depending
    ** on the parsing context. As follows:
    **
    **   0: Parsing file-name.
    **   1: Parsing name section of a name=value query parameter.
    **   2: Parsing value section of a name=value query parameter.
    */
    eState = 0;
    while( (c = zUri[iIn])!=0 && c!='#' ){
      iIn++;
      if( c=='%' 
       && sqlite3Isxdigit(zUri[iIn]) 
       && sqlite3Isxdigit(zUri[iIn+1]) 
      ){
        int octet = (sqlite3HexToInt(zUri[iIn++]) << 4);
        octet += sqlite3HexToInt(zUri[iIn++]);

        assert( octet>=0 && octet<256 );
        if( octet==0 ){
          /* This branch is taken when "%00" appears within the URI. In this
          ** case we ignore all text in the remainder of the path, name or
          ** value currently being parsed. So ignore the current character
          ** and skip to the next "?", "=" or "&", as appropriate. */
          while( (c = zUri[iIn])!=0 && c!='#' 
              && (eState!=0 || c!='?')
              && (eState!=1 || (c!='=' && c!='&'))
              && (eState!=2 || c!='&')
          ){
            iIn++;
          }
          continue;
        }
        c = octet;
      }else if( eState==1 && (c=='&' || c=='=') ){
        if( zFile[iOut-1]==0 ){
          /* An empty option name. Ignore this option altogether. */
          while( zUri[iIn] && zUri[iIn]!='#' && zUri[iIn-1]!='&' ) iIn++;
          continue;
        }
        if( c=='&' ){
          zFile[iOut++] = '\0';
        }else{
          eState = 2;
        }
        c = 0;
      }else if( (eState==0 && c=='?') || (eState==2 && c=='&') ){
        c = 0;
        eState = 1;
      }
      zFile[iOut++] = c;
    }
    if( eState==1 ) zFile[iOut++] = '\0';
    zFile[iOut++] = '\0';
    zFile[iOut++] = '\0';

    /* Check if there were any options specified that should be interpreted 
    ** here. Options that are interpreted here include "vfs" and those that
    ** correspond to flags that may be passed to the sqlite3_open_v2()
    ** method. */
    zOpt = &zFile[sqlite3Strlen30(zFile)+1];
    while( zOpt[0] ){
      int nOpt = sqlite3Strlen30(zOpt);
      char *zVal = &zOpt[nOpt+1];
      int nVal = sqlite3Strlen30(zVal);

      if( nOpt==3 && memcmp("vfs", zOpt, 3)==0 ){
        zVfs = zVal;
      }else{
        struct OpenMode {
          const char *z;
          int mode;
        } *aMode = 0;
        char *zModeType = 0;
        int mask = 0;
        int limit = 0;

        if( nOpt==5 && memcmp("cache", zOpt, 5)==0 ){
          static struct OpenMode aCacheMode[] = {
            { "shared",  SQLITE_OPEN_SHAREDCACHE },
            { "private", SQLITE_OPEN_PRIVATECACHE },
            { 0, 0 }
          };

          mask = SQLITE_OPEN_SHAREDCACHE|SQLITE_OPEN_PRIVATECACHE;
          aMode = aCacheMode;
          limit = mask;
          zModeType = "cache";
        }
        if( nOpt==4 && memcmp("mode", zOpt, 4)==0 ){
          static struct OpenMode aOpenMode[] = {
            { "ro",  SQLITE_OPEN_READONLY },
            { "rw",  SQLITE_OPEN_READWRITE }, 
            { "rwc", SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE },
            { 0, 0 }
          };

          mask = SQLITE_OPEN_READONLY|SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE;
          aMode = aOpenMode;
          limit = mask & flags;
          zModeType = "access";
        }

        if( aMode ){
          int i;
          int mode = 0;
          for(i=0; aMode[i].z; i++){
            const char *z = aMode[i].z;
            if( nVal==sqlite3Strlen30(z) && 0==memcmp(zVal, z, nVal) ){
              mode = aMode[i].mode;
              break;
            }
          }
          if( mode==0 ){
            *pzErrMsg = sqlite3_mprintf("no such %s mode: %s", zModeType, zVal);
            rc = SQLITE_ERROR;
            goto parse_uri_out;
          }
          if( mode>limit ){
            *pzErrMsg = sqlite3_mprintf("%s mode not allowed: %s",
                                        zModeType, zVal);
            rc = SQLITE_PERM;
            goto parse_uri_out;
          }
          flags = (flags & ~mask) | mode;
        }
      }

      zOpt = &zVal[nVal+1];
    }

  }else{
    zFile = sqlite3_malloc(nUri+2);
    if( !zFile ) return SQLITE_NOMEM;
    memcpy(zFile, zUri, nUri);
    zFile[nUri] = '\0';
    zFile[nUri+1] = '\0';
  }

  *ppVfs = sqlite3_vfs_find(zVfs);
  if( *ppVfs==0 ){
    *pzErrMsg = sqlite3_mprintf("no such vfs: %s", zVfs);
    rc = SQLITE_ERROR;
  }
 parse_uri_out:
  if( rc!=SQLITE_OK ){
    sqlite3_free(zFile);
    zFile = 0;
  }
  *pFlags = flags;
  *pzFile = zFile;
  return rc;
}


/*
** This routine does the work of opening a database on behalf of
** sqlite3_open() and sqlite3_open16(). The database filename "zFilename"  
** is UTF-8 encoded.
*/
static int openDatabase(
  const char *zFilename, /* Database filename UTF-8 encoded */
  sqlite3 **ppDb,        /* OUT: Returned database handle */
  unsigned int flags,    /* Operational flags */
  const char *zVfs       /* Name of the VFS to use */
){
  sqlite3 *db;                    /* Store allocated handle here */
  int rc;                         /* Return code */
  int isThreadsafe;               /* True for threadsafe connections */
  char *zOpen = 0;                /* Filename argument to pass to BtreeOpen() */
  char *zErrMsg = 0;              /* Error message from sqlite3ParseUri() */

  *ppDb = 0;
#ifndef SQLITE_OMIT_AUTOINIT
  rc = sqlite3_initialize();
  if( rc ) return rc;
#endif

  /* Only allow sensible combinations of bits in the flags argument.  
  ** Throw an error if any non-sense combination is used.  If we
  ** do not block illegal combinations here, it could trigger
  ** assert() statements in deeper layers.  Sensible combinations
  ** are:
  **
  **  1:  SQLITE_OPEN_READONLY
  **  2:  SQLITE_OPEN_READWRITE
  **  6:  SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE
  */
  assert( SQLITE_OPEN_READONLY  == 0x01 );
  assert( SQLITE_OPEN_READWRITE == 0x02 );
  assert( SQLITE_OPEN_CREATE    == 0x04 );
  testcase( (1<<(flags&7))==0x02 ); /* READONLY */
  testcase( (1<<(flags&7))==0x04 ); /* READWRITE */
  testcase( (1<<(flags&7))==0x40 ); /* READWRITE | CREATE */
  if( ((1<<(flags&7)) & 0x46)==0 ) return SQLITE_MISUSE_BKPT;

  if( sqlite3GlobalConfig.bCoreMutex==0 ){
    isThreadsafe = 0;
  }else if( flags & SQLITE_OPEN_NOMUTEX ){
    isThreadsafe = 0;
  }else if( flags & SQLITE_OPEN_FULLMUTEX ){
    isThreadsafe = 1;
  }else{
    isThreadsafe = sqlite3GlobalConfig.bFullMutex;
  }
  if( flags & SQLITE_OPEN_PRIVATECACHE ){
    flags &= ~SQLITE_OPEN_SHAREDCACHE;
  }else if( sqlite3GlobalConfig.sharedCacheEnabled ){
    flags |= SQLITE_OPEN_SHAREDCACHE;
  }

  /* Remove harmful bits from the flags parameter
  **
  ** The SQLITE_OPEN_NOMUTEX and SQLITE_OPEN_FULLMUTEX flags were
  ** dealt with in the previous code block.  Besides these, the only
  ** valid input flags for sqlite3_open_v2() are SQLITE_OPEN_READONLY,
  ** SQLITE_OPEN_READWRITE, SQLITE_OPEN_CREATE, SQLITE_OPEN_SHAREDCACHE,
  ** SQLITE_OPEN_PRIVATECACHE, and some reserved bits.  Silently mask
  ** off all other flags.
  */
  flags &=  ~( SQLITE_OPEN_DELETEONCLOSE |
               SQLITE_OPEN_EXCLUSIVE |
               SQLITE_OPEN_MAIN_DB |
               SQLITE_OPEN_TEMP_DB | 
               SQLITE_OPEN_TRANSIENT_DB | 
               SQLITE_OPEN_MAIN_JOURNAL | 
               SQLITE_OPEN_TEMP_JOURNAL | 
               SQLITE_OPEN_SUBJOURNAL | 
               SQLITE_OPEN_MASTER_JOURNAL |
               SQLITE_OPEN_NOMUTEX |
               SQLITE_OPEN_FULLMUTEX |
               SQLITE_OPEN_WAL
             );

  /* Allocate the sqlite data structure */
  db = sqlite3MallocZero( sizeof(sqlite3) );
  if( db==0 ) goto opendb_out;
  if( isThreadsafe ){
    db->mutex = sqlite3MutexAlloc(SQLITE_MUTEX_RECURSIVE);
    if( db->mutex==0 ){
      sqlite3_free(db);
      db = 0;
      goto opendb_out;
    }
  }
  sqlite3_mutex_enter(db->mutex);
  db->errMask = 0xff;
  db->nDb = 2;
  db->magic = SQLITE_MAGIC_BUSY;
  db->aDb = db->aDbStatic;

  assert( sizeof(db->aLimit)==sizeof(aHardLimit) );
  memcpy(db->aLimit, aHardLimit, sizeof(db->aLimit));
  db->autoCommit = 1;
  db->nextAutovac = -1;
  db->nextPagesize = 0;
  db->flags |= SQLITE_ShortColNames | SQLITE_AutoIndex | SQLITE_EnableTrigger
#if SQLITE_DEFAULT_FILE_FORMAT<4
                 | SQLITE_LegacyFileFmt
#endif
#ifdef SQLITE_ENABLE_LOAD_EXTENSION
                 | SQLITE_LoadExtension
#endif
#if SQLITE_DEFAULT_RECURSIVE_TRIGGERS
                 | SQLITE_RecTriggers
#endif
#if defined(SQLITE_DEFAULT_FOREIGN_KEYS) && SQLITE_DEFAULT_FOREIGN_KEYS
                 | SQLITE_ForeignKeys
#endif
      ;
  sqlite3HashInit(&db->aCollSeq);
#ifndef SQLITE_OMIT_VIRTUALTABLE
  sqlite3HashInit(&db->aModule);
#endif

  /* Add the default collation sequence BINARY. BINARY works for both UTF-8
  ** and UTF-16, so add a version for each to avoid any unnecessary
  ** conversions. The only error that can occur here is a malloc() failure.
  */
  createCollation(db, "BINARY", SQLITE_UTF8, SQLITE_COLL_BINARY, 0,
                  binCollFunc, 0);
  createCollation(db, "BINARY", SQLITE_UTF16BE, SQLITE_COLL_BINARY, 0,
                  binCollFunc, 0);
  createCollation(db, "BINARY", SQLITE_UTF16LE, SQLITE_COLL_BINARY, 0,
                  binCollFunc, 0);
  createCollation(db, "RTRIM", SQLITE_UTF8, SQLITE_COLL_USER, (void*)1,
                  binCollFunc, 0);
  if( db->mallocFailed ){
    goto opendb_out;
  }
  db->pDfltColl = sqlite3FindCollSeq(db, SQLITE_UTF8, "BINARY", 0);
  assert( db->pDfltColl!=0 );

  /* Also add a UTF-8 case-insensitive collation sequence. */
  createCollation(db, "NOCASE", SQLITE_UTF8, SQLITE_COLL_NOCASE, 0,
                  nocaseCollatingFunc, 0);

  /* Parse the filename/URI argument. */
  db->openFlags = flags;
  rc = sqlite3ParseUri(zVfs, zFilename, &flags, &db->pVfs, &zOpen, &zErrMsg);
  if( rc!=SQLITE_OK ){
    if( rc==SQLITE_NOMEM ) db->mallocFailed = 1;
    sqlite3Error(db, rc, zErrMsg ? "%s" : 0, zErrMsg);
    sqlite3_free(zErrMsg);
    goto opendb_out;
  }

  /* Open the backend database driver */
  rc = sqlite3BtreeOpen(db->pVfs, zOpen, db, &db->aDb[0].pBt, 0,
                        flags | SQLITE_OPEN_MAIN_DB);
  if( rc!=SQLITE_OK ){
    if( rc==SQLITE_IOERR_NOMEM ){
      rc = SQLITE_NOMEM;
    }
    sqlite3Error(db, rc, 0);
    goto opendb_out;
  }
  db->aDb[0].pSchema = sqlite3SchemaGet(db, db->aDb[0].pBt);
  db->aDb[1].pSchema = sqlite3SchemaGet(db, 0);


  /* The default safety_level for the main database is 'full'; for the temp
  ** database it is 'NONE'. This matches the pager layer defaults.  
  */
  db->aDb[0].zName = "main";
  db->aDb[0].safety_level = 3;
  db->aDb[1].zName = "temp";
  db->aDb[1].safety_level = 1;

  db->magic = SQLITE_MAGIC_OPEN;
  if( db->mallocFailed ){
    goto opendb_out;
  }

  /* Register all built-in functions, but do not attempt to read the
  ** database schema yet. This is delayed until the first time the database
  ** is accessed.
  */
  sqlite3Error(db, SQLITE_OK, 0);
  sqlite3RegisterBuiltinFunctions(db);

  /* Load automatic extensions - extensions that have been registered
  ** using the sqlite3_automatic_extension() API.
  */
  sqlite3AutoLoadExtensions(db);
  rc = sqlite3_errcode(db);
  if( rc!=SQLITE_OK ){
    goto opendb_out;
  }

#ifdef SQLITE_ENABLE_FTS1
  if( !db->mallocFailed ){
    extern int sqlite3Fts1Init(sqlite3*);
    rc = sqlite3Fts1Init(db);
  }
#endif

#ifdef SQLITE_ENABLE_FTS2
  if( !db->mallocFailed && rc==SQLITE_OK ){
    extern int sqlite3Fts2Init(sqlite3*);
    rc = sqlite3Fts2Init(db);
  }
#endif

#ifdef SQLITE_ENABLE_FTS3
  if( !db->mallocFailed && rc==SQLITE_OK ){
    rc = sqlite3Fts3Init(db);
  }
#endif

#ifdef SQLITE_ENABLE_ICU
  if( !db->mallocFailed && rc==SQLITE_OK ){
    rc = sqlite3IcuInit(db);
  }
#endif

#ifdef SQLITE_ENABLE_RTREE
  if( !db->mallocFailed && rc==SQLITE_OK){
    rc = sqlite3RtreeInit(db);
  }
#endif

  sqlite3Error(db, rc, 0);

  /* -DSQLITE_DEFAULT_LOCKING_MODE=1 makes EXCLUSIVE the default locking
  ** mode.  -DSQLITE_DEFAULT_LOCKING_MODE=0 make NORMAL the default locking
  ** mode.  Doing nothing at all also makes NORMAL the default.
  */
#ifdef SQLITE_DEFAULT_LOCKING_MODE
  db->dfltLockMode = SQLITE_DEFAULT_LOCKING_MODE;
  sqlite3PagerLockingMode(sqlite3BtreePager(db->aDb[0].pBt),
                          SQLITE_DEFAULT_LOCKING_MODE);
#endif

  /* Enable the lookaside-malloc subsystem */
  setupLookaside(db, 0, sqlite3GlobalConfig.szLookaside,
                        sqlite3GlobalConfig.nLookaside);

  sqlite3_wal_autocheckpoint(db, SQLITE_DEFAULT_WAL_AUTOCHECKPOINT);

opendb_out:
  sqlite3_free(zOpen);
  if( db ){
    assert( db->mutex!=0 || isThreadsafe==0 || sqlite3GlobalConfig.bFullMutex==0 );
    sqlite3_mutex_leave(db->mutex);
  }
  rc = sqlite3_errcode(db);
  assert( db!=0 || rc==SQLITE_NOMEM );
  if( rc==SQLITE_NOMEM ){
    sqlite3_close(db);
    db = 0;
  }else if( rc!=SQLITE_OK ){
    db->magic = SQLITE_MAGIC_SICK;
  }
  *ppDb = db;
  return sqlite3ApiExit(0, rc);
}

/*
** Open a new database handle.
*/
SQLITE_API int sqlite3_open(
  const char *zFilename, 
  sqlite3 **ppDb 
){
  return openDatabase(zFilename, ppDb,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 0);
}
SQLITE_API int sqlite3_open_v2(
  const char *filename,   /* Database filename (UTF-8) */
  sqlite3 **ppDb,         /* OUT: SQLite db handle */
  int flags,              /* Flags */
  const char *zVfs        /* Name of VFS module to use */
){
  return openDatabase(filename, ppDb, (unsigned int)flags, zVfs);
}

#ifndef SQLITE_OMIT_UTF16
/*
** Open a new database handle.
*/
SQLITE_API int sqlite3_open16(
  const void *zFilename, 
  sqlite3 **ppDb
){
  char const *zFilename8;   /* zFilename encoded in UTF-8 instead of UTF-16 */
  sqlite3_value *pVal;
  int rc;

  assert( zFilename );
  assert( ppDb );
  *ppDb = 0;
#ifndef SQLITE_OMIT_AUTOINIT
  rc = sqlite3_initialize();
  if( rc ) return rc;
#endif
  pVal = sqlite3ValueNew(0);
  sqlite3ValueSetStr(pVal, -1, zFilename, SQLITE_UTF16NATIVE, SQLITE_STATIC);
  zFilename8 = sqlite3ValueText(pVal, SQLITE_UTF8);
  if( zFilename8 ){
    rc = openDatabase(zFilename8, ppDb,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 0);
    assert( *ppDb || rc==SQLITE_NOMEM );
    if( rc==SQLITE_OK && !DbHasProperty(*ppDb, 0, DB_SchemaLoaded) ){
      ENC(*ppDb) = SQLITE_UTF16NATIVE;
    }
  }else{
    rc = SQLITE_NOMEM;
  }
  sqlite3ValueFree(pVal);

  return sqlite3ApiExit(0, rc);
}
#endif /* SQLITE_OMIT_UTF16 */

/*
** Register a new collation sequence with the database handle db.
*/
SQLITE_API int sqlite3_create_collation(
  sqlite3* db, 
  const char *zName, 
  int enc, 
  void* pCtx,
  int(*xCompare)(void*,int,const void*,int,const void*)
){
  int rc;
  sqlite3_mutex_enter(db->mutex);
  assert( !db->mallocFailed );
  rc = createCollation(db, zName, (u8)enc, SQLITE_COLL_USER, pCtx, xCompare, 0);
  rc = sqlite3ApiExit(db, rc);
  sqlite3_mutex_leave(db->mutex);
  return rc;
}

/*
** Register a new collation sequence with the database handle db.
*/
SQLITE_API int sqlite3_create_collation_v2(
  sqlite3* db, 
  const char *zName, 
  int enc, 
  void* pCtx,
  int(*xCompare)(void*,int,const void*,int,const void*),
  void(*xDel)(void*)
){
  int rc;
  sqlite3_mutex_enter(db->mutex);
  assert( !db->mallocFailed );
  rc = createCollation(db, zName, (u8)enc, SQLITE_COLL_USER, pCtx, xCompare, xDel);
  rc = sqlite3ApiExit(db, rc);
  sqlite3_mutex_leave(db->mutex);
  return rc;
}

#ifndef SQLITE_OMIT_UTF16
/*
** Register a new collation sequence with the database handle db.
*/
SQLITE_API int sqlite3_create_collation16(
  sqlite3* db, 
  const void *zName,
  int enc, 
  void* pCtx,
  int(*xCompare)(void*,int,const void*,int,const void*)
){
  int rc = SQLITE_OK;
  char *zName8;
  sqlite3_mutex_enter(db->mutex);
  assert( !db->mallocFailed );
  zName8 = sqlite3Utf16to8(db, zName, -1, SQLITE_UTF16NATIVE);
  if( zName8 ){
    rc = createCollation(db, zName8, (u8)enc, SQLITE_COLL_USER, pCtx, xCompare, 0);
    sqlite3DbFree(db, zName8);
  }
  rc = sqlite3ApiExit(db, rc);
  sqlite3_mutex_leave(db->mutex);
  return rc;
}
#endif /* SQLITE_OMIT_UTF16 */

/*
** Register a collation sequence factory callback with the database handle
** db. Replace any previously installed collation sequence factory.
*/
SQLITE_API int sqlite3_collation_needed(
  sqlite3 *db, 
  void *pCollNeededArg, 
  void(*xCollNeeded)(void*,sqlite3*,int eTextRep,const char*)
){
  sqlite3_mutex_enter(db->mutex);
  db->xCollNeeded = xCollNeeded;
  db->xCollNeeded16 = 0;
  db->pCollNeededArg = pCollNeededArg;
  sqlite3_mutex_leave(db->mutex);
  return SQLITE_OK;
}

#ifndef SQLITE_OMIT_UTF16
/*
** Register a collation sequence factory callback with the database handle
** db. Replace any previously installed collation sequence factory.
*/
SQLITE_API int sqlite3_collation_needed16(
  sqlite3 *db, 
  void *pCollNeededArg, 
  void(*xCollNeeded16)(void*,sqlite3*,int eTextRep,const void*)
){
  sqlite3_mutex_enter(db->mutex);
  db->xCollNeeded = 0;
  db->xCollNeeded16 = xCollNeeded16;
  db->pCollNeededArg = pCollNeededArg;
  sqlite3_mutex_leave(db->mutex);
  return SQLITE_OK;
}
#endif /* SQLITE_OMIT_UTF16 */

#ifndef SQLITE_OMIT_DEPRECATED
/*
** This function is now an anachronism. It used to be used to recover from a
** malloc() failure, but SQLite now does this automatically.
*/
SQLITE_API int sqlite3_global_recover(void){
  return SQLITE_OK;
}
#endif

/*
** Test to see whether or not the database connection is in autocommit
** mode.  Return TRUE if it is and FALSE if not.  Autocommit mode is on
** by default.  Autocommit is disabled by a BEGIN statement and reenabled
** by the next COMMIT or ROLLBACK.
**
******* THIS IS AN EXPERIMENTAL API AND IS SUBJECT TO CHANGE ******
*/
SQLITE_API int sqlite3_get_autocommit(sqlite3 *db){
  return db->autoCommit;
}

/*
** The following routines are subtitutes for constants SQLITE_CORRUPT,
** SQLITE_MISUSE, SQLITE_CANTOPEN, SQLITE_IOERR and possibly other error
** constants.  They server two purposes:
**
**   1.  Serve as a convenient place to set a breakpoint in a debugger
**       to detect when version error conditions occurs.
**
**   2.  Invoke sqlite3_log() to provide the source code location where
**       a low-level error is first detected.
*/
SQLITE_PRIVATE int sqlite3CorruptError(int lineno){
  testcase( sqlite3GlobalConfig.xLog!=0 );
  sqlite3_log(SQLITE_CORRUPT,
              "database corruption at line %d of [%.10s]",
              lineno, 20+sqlite3_sourceid());
  return SQLITE_CORRUPT;
}
SQLITE_PRIVATE int sqlite3MisuseError(int lineno){
  testcase( sqlite3GlobalConfig.xLog!=0 );
  sqlite3_log(SQLITE_MISUSE, 
              "misuse at line %d of [%.10s]",
              lineno, 20+sqlite3_sourceid());
  return SQLITE_MISUSE;
}
SQLITE_PRIVATE int sqlite3CantopenError(int lineno){
  testcase( sqlite3GlobalConfig.xLog!=0 );
  sqlite3_log(SQLITE_CANTOPEN, 
              "cannot open file at line %d of [%.10s]",
              lineno, 20+sqlite3_sourceid());
  return SQLITE_CANTOPEN;
}


#ifndef SQLITE_OMIT_DEPRECATED
/*
** This is a convenience routine that makes sure that all thread-specific
** data for this thread has been deallocated.
**
** SQLite no longer uses thread-specific data so this routine is now a
** no-op.  It is retained for historical compatibility.
*/
SQLITE_API void sqlite3_thread_cleanup(void){
}
#endif

/*
** Return meta information about a specific column of a database table.
** See comment in sqlite3.h (sqlite.h.in) for details.
*/
#ifdef SQLITE_ENABLE_COLUMN_METADATA
SQLITE_API int sqlite3_table_column_metadata(
  sqlite3 *db,                /* Connection handle */
  const char *zDbName,        /* Database name or NULL */
  const char *zTableName,     /* Table name */
  const char *zColumnName,    /* Column name */
  char const **pzDataType,    /* OUTPUT: Declared data type */
  char const **pzCollSeq,     /* OUTPUT: Collation sequence name */
  int *pNotNull,              /* OUTPUT: True if NOT NULL constraint exists */
  int *pPrimaryKey,           /* OUTPUT: True if column part of PK */
  int *pAutoinc               /* OUTPUT: True if column is auto-increment */
){
  int rc;
  char *zErrMsg = 0;
  Table *pTab = 0;
  Column *pCol = 0;
  int iCol;

  char const *zDataType = 0;
  char const *zCollSeq = 0;
  int notnull = 0;
  int primarykey = 0;
  int autoinc = 0;

  /* Ensure the database schema has been loaded */
  sqlite3_mutex_enter(db->mutex);
  sqlite3BtreeEnterAll(db);
  rc = sqlite3Init(db, &zErrMsg);
  if( SQLITE_OK!=rc ){
    goto error_out;
  }

  /* Locate the table in question */
  pTab = sqlite3FindTable(db, zTableName, zDbName);
  if( !pTab || pTab->pSelect ){
    pTab = 0;
    goto error_out;
  }

  /* Find the column for which info is requested */
  if( sqlite3IsRowid(zColumnName) ){
    iCol = pTab->iPKey;
    if( iCol>=0 ){
      pCol = &pTab->aCol[iCol];
    }
  }else{
    for(iCol=0; iCol<pTab->nCol; iCol++){
      pCol = &pTab->aCol[iCol];
      if( 0==sqlite3StrICmp(pCol->zName, zColumnName) ){
        break;
      }
    }
    if( iCol==pTab->nCol ){
      pTab = 0;
      goto error_out;
    }
  }

  /* The following block stores the meta information that will be returned
  ** to the caller in local variables zDataType, zCollSeq, notnull, primarykey
  ** and autoinc. At this point there are two possibilities:
  ** 
  **     1. The specified column name was rowid", "oid" or "_rowid_" 
  **        and there is no explicitly declared IPK column. 
  **
  **     2. The table is not a view and the column name identified an 
  **        explicitly declared column. Copy meta information from *pCol.
  */ 
  if( pCol ){
    zDataType = pCol->zType;
    zCollSeq = pCol->zColl;
    notnull = pCol->notNull!=0;
    primarykey  = pCol->isPrimKey!=0;
    autoinc = pTab->iPKey==iCol && (pTab->tabFlags & TF_Autoincrement)!=0;
  }else{
    zDataType = "INTEGER";
    primarykey = 1;
  }
  if( !zCollSeq ){
    zCollSeq = "BINARY";
  }

error_out:
  sqlite3BtreeLeaveAll(db);

  /* Whether the function call succeeded or failed, set the output parameters
  ** to whatever their local counterparts contain. If an error did occur,
  ** this has the effect of zeroing all output parameters.
  */
  if( pzDataType ) *pzDataType = zDataType;
  if( pzCollSeq ) *pzCollSeq = zCollSeq;
  if( pNotNull ) *pNotNull = notnull;
  if( pPrimaryKey ) *pPrimaryKey = primarykey;
  if( pAutoinc ) *pAutoinc = autoinc;

  if( SQLITE_OK==rc && !pTab ){
    sqlite3DbFree(db, zErrMsg);
    zErrMsg = sqlite3MPrintf(db, "no such table column: %s.%s", zTableName,
        zColumnName);
    rc = SQLITE_ERROR;
  }
  sqlite3Error(db, rc, (zErrMsg?"%s":0), zErrMsg);
  sqlite3DbFree(db, zErrMsg);
  rc = sqlite3ApiExit(db, rc);
  sqlite3_mutex_leave(db->mutex);
  return rc;
}
#endif

/*
** Sleep for a little while.  Return the amount of time slept.
*/
SQLITE_API int sqlite3_sleep(int ms){
  sqlite3_vfs *pVfs;
  int rc;
  pVfs = sqlite3_vfs_find(0);
  if( pVfs==0 ) return 0;

  /* This function works in milliseconds, but the underlying OsSleep() 
  ** API uses microseconds. Hence the 1000's.
  */
  rc = (sqlite3OsSleep(pVfs, 1000*ms)/1000);
  return rc;
}

/*
** Enable or disable the extended result codes.
*/
SQLITE_API int sqlite3_extended_result_codes(sqlite3 *db, int onoff){
  sqlite3_mutex_enter(db->mutex);
  db->errMask = onoff ? 0xffffffff : 0xff;
  sqlite3_mutex_leave(db->mutex);
  return SQLITE_OK;
}

/*
** Invoke the xFileControl method on a particular database.
*/
SQLITE_API int sqlite3_file_control(sqlite3 *db, const char *zDbName, int op, void *pArg){
  int rc = SQLITE_ERROR;
  int iDb;
  sqlite3_mutex_enter(db->mutex);
  if( zDbName==0 ){
    iDb = 0;
  }else{
    for(iDb=0; iDb<db->nDb; iDb++){
      if( strcmp(db->aDb[iDb].zName, zDbName)==0 ) break;
    }
  }
  if( iDb<db->nDb ){
    Btree *pBtree = db->aDb[iDb].pBt;
    if( pBtree ){
      Pager *pPager;
      sqlite3_file *fd;
      sqlite3BtreeEnter(pBtree);
      pPager = sqlite3BtreePager(pBtree);
      assert( pPager!=0 );
      fd = sqlite3PagerFile(pPager);
      assert( fd!=0 );
      if( op==SQLITE_FCNTL_FILE_POINTER ){
        *(sqlite3_file**)pArg = fd;
        rc = SQLITE_OK;
      }else if( fd->pMethods ){
        rc = sqlite3OsFileControl(fd, op, pArg);
      }else{
        rc = SQLITE_NOTFOUND;
      }
      sqlite3BtreeLeave(pBtree);
    }
  }
  sqlite3_mutex_leave(db->mutex);
  return rc;   
}

/*
** Interface to the testing logic.
*/
SQLITE_API int sqlite3_test_control(int op, ...){
  int rc = 0;
#ifndef SQLITE_OMIT_BUILTIN_TEST
  va_list ap;
  va_start(ap, op);
  switch( op ){

    /*
    ** Save the current state of the PRNG.
    */
    case SQLITE_TESTCTRL_PRNG_SAVE: {
      sqlite3PrngSaveState();
      break;
    }

    /*
    ** Restore the state of the PRNG to the last state saved using
    ** PRNG_SAVE.  If PRNG_SAVE has never before been called, then
    ** this verb acts like PRNG_RESET.
    */
    case SQLITE_TESTCTRL_PRNG_RESTORE: {
      sqlite3PrngRestoreState();
      break;
    }

    /*
    ** Reset the PRNG back to its uninitialized state.  The next call
    ** to sqlite3_randomness() will reseed the PRNG using a single call
    ** to the xRandomness method of the default VFS.
    */
    case SQLITE_TESTCTRL_PRNG_RESET: {
      sqlite3PrngResetState();
      break;
    }

    /*
    **  sqlite3_test_control(BITVEC_TEST, size, program)
    **
    ** Run a test against a Bitvec object of size.  The program argument
    ** is an array of integers that defines the test.  Return -1 on a
    ** memory allocation error, 0 on success, or non-zero for an error.
    ** See the sqlite3BitvecBuiltinTest() for additional information.
    */
    case SQLITE_TESTCTRL_BITVEC_TEST: {
      int sz = va_arg(ap, int);
      int *aProg = va_arg(ap, int*);
      rc = sqlite3BitvecBuiltinTest(sz, aProg);
      break;
    }

    /*
    **  sqlite3_test_control(BENIGN_MALLOC_HOOKS, xBegin, xEnd)
    **
    ** Register hooks to call to indicate which malloc() failures 
    ** are benign.
    */
    case SQLITE_TESTCTRL_BENIGN_MALLOC_HOOKS: {
      typedef void (*void_function)(void);
      void_function xBenignBegin;
      void_function xBenignEnd;
      xBenignBegin = va_arg(ap, void_function);
      xBenignEnd = va_arg(ap, void_function);
      sqlite3BenignMallocHooks(xBenignBegin, xBenignEnd);
      break;
    }

    /*
    **  sqlite3_test_control(SQLITE_TESTCTRL_PENDING_BYTE, unsigned int X)
    **
    ** Set the PENDING byte to the value in the argument, if X>0.
    ** Make no changes if X==0.  Return the value of the pending byte
    ** as it existing before this routine was called.
    **
    ** IMPORTANT:  Changing the PENDING byte from 0x40000000 results in
    ** an incompatible database file format.  Changing the PENDING byte
    ** while any database connection is open results in undefined and
    ** dileterious behavior.
    */
    case SQLITE_TESTCTRL_PENDING_BYTE: {
      rc = PENDING_BYTE;
#ifndef SQLITE_OMIT_WSD
      {
        unsigned int newVal = va_arg(ap, unsigned int);
        if( newVal ) sqlite3PendingByte = newVal;
      }
#endif
      break;
    }

    /*
    **  sqlite3_test_control(SQLITE_TESTCTRL_ASSERT, int X)
    **
    ** This action provides a run-time test to see whether or not
    ** assert() was enabled at compile-time.  If X is true and assert()
    ** is enabled, then the return value is true.  If X is true and
    ** assert() is disabled, then the return value is zero.  If X is
    ** false and assert() is enabled, then the assertion fires and the
    ** process aborts.  If X is false and assert() is disabled, then the
    ** return value is zero.
    */
    case SQLITE_TESTCTRL_ASSERT: {
      volatile int x = 0;
      assert( (x = va_arg(ap,int))!=0 );
      rc = x;
      break;
    }


    /*
    **  sqlite3_test_control(SQLITE_TESTCTRL_ALWAYS, int X)
    **
    ** This action provides a run-time test to see how the ALWAYS and
    ** NEVER macros were defined at compile-time.
    **
    ** The return value is ALWAYS(X).  
    **
    ** The recommended test is X==2.  If the return value is 2, that means
    ** ALWAYS() and NEVER() are both no-op pass-through macros, which is the
    ** default setting.  If the return value is 1, then ALWAYS() is either
    ** hard-coded to true or else it asserts if its argument is false.
    ** The first behavior (hard-coded to true) is the case if
    ** SQLITE_TESTCTRL_ASSERT shows that assert() is disabled and the second
    ** behavior (assert if the argument to ALWAYS() is false) is the case if
    ** SQLITE_TESTCTRL_ASSERT shows that assert() is enabled.
    **
    ** The run-time test procedure might look something like this:
    **
    **    if( sqlite3_test_control(SQLITE_TESTCTRL_ALWAYS, 2)==2 ){
    **      // ALWAYS() and NEVER() are no-op pass-through macros
    **    }else if( sqlite3_test_control(SQLITE_TESTCTRL_ASSERT, 1) ){
    **      // ALWAYS(x) asserts that x is true. NEVER(x) asserts x is false.
    **    }else{
    **      // ALWAYS(x) is a constant 1.  NEVER(x) is a constant 0.
    **    }
    */
    case SQLITE_TESTCTRL_ALWAYS: {
      int x = va_arg(ap,int);
      rc = ALWAYS(x);
      break;
    }

    /*   sqlite3_test_control(SQLITE_TESTCTRL_RESERVE, sqlite3 *db, int N)
    **
    ** Set the nReserve size to N for the main database on the database
    ** connection db.
    */
    case SQLITE_TESTCTRL_RESERVE: {
      sqlite3 *db = va_arg(ap, sqlite3*);
      int x = va_arg(ap,int);
      sqlite3_mutex_enter(db->mutex);
      sqlite3BtreeSetPageSize(db->aDb[0].pBt, 0, x, 0);
      sqlite3_mutex_leave(db->mutex);
      break;
    }

    /*  sqlite3_test_control(SQLITE_TESTCTRL_OPTIMIZATIONS, sqlite3 *db, int N)
    **
    ** Enable or disable various optimizations for testing purposes.  The 
    ** argument N is a bitmask of optimizations to be disabled.  For normal
    ** operation N should be 0.  The idea is that a test program (like the
    ** SQL Logic Test or SLT test module) can run the same SQL multiple times
    ** with various optimizations disabled to verify that the same answer
    ** is obtained in every case.
    */
    case SQLITE_TESTCTRL_OPTIMIZATIONS: {
      sqlite3 *db = va_arg(ap, sqlite3*);
      int x = va_arg(ap,int);
      db->flags = (x & SQLITE_OptMask) | (db->flags & ~SQLITE_OptMask);
      break;
    }

#ifdef SQLITE_N_KEYWORD
    /* sqlite3_test_control(SQLITE_TESTCTRL_ISKEYWORD, const char *zWord)
    **
    ** If zWord is a keyword recognized by the parser, then return the
    ** number of keywords.  Or if zWord is not a keyword, return 0.
    ** 
    ** This test feature is only available in the amalgamation since
    ** the SQLITE_N_KEYWORD macro is not defined in this file if SQLite
    ** is built using separate source files.
    */
    case SQLITE_TESTCTRL_ISKEYWORD: {
      const char *zWord = va_arg(ap, const char*);
      int n = sqlite3Strlen30(zWord);
      rc = (sqlite3KeywordCode((u8*)zWord, n)!=TK_ID) ? SQLITE_N_KEYWORD : 0;
      break;
    }
#endif 

    /* sqlite3_test_control(SQLITE_TESTCTRL_PGHDRSZ)
    **
    ** Return the size of a pcache header in bytes.
    */
    case SQLITE_TESTCTRL_PGHDRSZ: {
      rc = sizeof(PgHdr);
      break;
    }

    /* sqlite3_test_control(SQLITE_TESTCTRL_SCRATCHMALLOC, sz, &pNew, pFree);
    **
    ** Pass pFree into sqlite3ScratchFree(). 
    ** If sz>0 then allocate a scratch buffer into pNew.  
    */
    case SQLITE_TESTCTRL_SCRATCHMALLOC: {
      void *pFree, **ppNew;
      int sz;
      sz = va_arg(ap, int);
      ppNew = va_arg(ap, void**);
      pFree = va_arg(ap, void*);
      if( sz ) *ppNew = sqlite3ScratchMalloc(sz);
      sqlite3ScratchFree(pFree);
      break;
    }

    /*   sqlite3_test_control(SQLITE_TESTCTRL_LOCALTIME_FAULT, int onoff);
    **
    ** If parameter onoff is non-zero, configure the wrappers so that all
    ** subsequent calls to localtime() and variants fail. If onoff is zero,
    ** undo this setting.
    */
    case SQLITE_TESTCTRL_LOCALTIME_FAULT: {
      sqlite3GlobalConfig.bLocaltimeFault = va_arg(ap, int);
      break;
    }

  }
  va_end(ap);
#endif /* SQLITE_OMIT_BUILTIN_TEST */
  return rc;
}

/*
** This is a utility routine, useful to VFS implementations, that checks
** to see if a database file was a URI that contained a specific query 
** parameter, and if so obtains the value of the query parameter.
**
** The zFilename argument is the filename pointer passed into the xOpen()
** method of a VFS implementation.  The zParam argument is the name of the
** query parameter we seek.  This routine returns the value of the zParam
** parameter if it exists.  If the parameter does not exist, this routine
** returns a NULL pointer.
*/
SQLITE_API const char *sqlite3_uri_parameter(const char *zFilename, const char *zParam){
  zFilename += sqlite3Strlen30(zFilename) + 1;
  while( zFilename[0] ){
    int x = strcmp(zFilename, zParam);
    zFilename += sqlite3Strlen30(zFilename) + 1;
    if( x==0 ) return zFilename;
    zFilename += sqlite3Strlen30(zFilename) + 1;
  }
  return 0;
}

/************** End of main.c ************************************************/
/************** Begin file notify.c ******************************************/
/*
** 2009 March 3
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
**
** This file contains the implementation of the sqlite3_unlock_notify()
** API method and its associated functionality.
*/

/* Omit this entire file if SQLITE_ENABLE_UNLOCK_NOTIFY is not defined. */
#ifdef SQLITE_ENABLE_UNLOCK_NOTIFY

/*
** Public interfaces:
**
**   sqlite3ConnectionBlocked()
**   sqlite3ConnectionUnlocked()
**   sqlite3ConnectionClosed()
**   sqlite3_unlock_notify()
*/

#define assertMutexHeld() \
  assert( sqlite3_mutex_held(sqlite3MutexAlloc(SQLITE_MUTEX_STATIC_MASTER)) )

/*
** Head of a linked list of all sqlite3 objects created by this process
** for which either sqlite3.pBlockingConnection or sqlite3.pUnlockConnection
** is not NULL. This variable may only accessed while the STATIC_MASTER
** mutex is held.
*/
static sqlite3 *SQLITE_WSD sqlite3BlockedList = 0;

#ifndef NDEBUG
/*
** This function is a complex assert() that verifies the following 
** properties of the blocked connections list:
**
**   1) Each entry in the list has a non-NULL value for either 
**      pUnlockConnection or pBlockingConnection, or both.
**
**   2) All entries in the list that share a common value for 
**      xUnlockNotify are grouped together.
**
**   3) If the argument db is not NULL, then none of the entries in the
**      blocked connections list have pUnlockConnection or pBlockingConnection
**      set to db. This is used when closing connection db.
*/
static void checkListProperties(sqlite3 *db){
  sqlite3 *p;
  for(p=sqlite3BlockedList; p; p=p->pNextBlocked){
    int seen = 0;
    sqlite3 *p2;

    /* Verify property (1) */
    assert( p->pUnlockConnection || p->pBlockingConnection );

    /* Verify property (2) */
    for(p2=sqlite3BlockedList; p2!=p; p2=p2->pNextBlocked){
      if( p2->xUnlockNotify==p->xUnlockNotify ) seen = 1;
      assert( p2->xUnlockNotify==p->xUnlockNotify || !seen );
      assert( db==0 || p->pUnlockConnection!=db );
      assert( db==0 || p->pBlockingConnection!=db );
    }
  }
}
#else
# define checkListProperties(x)
#endif

/*
** Remove connection db from the blocked connections list. If connection
** db is not currently a part of the list, this function is a no-op.
*/
static void removeFromBlockedList(sqlite3 *db){
  sqlite3 **pp;
  assertMutexHeld();
  for(pp=&sqlite3BlockedList; *pp; pp = &(*pp)->pNextBlocked){
    if( *pp==db ){
      *pp = (*pp)->pNextBlocked;
      break;
    }
  }
}

/*
** Add connection db to the blocked connections list. It is assumed
** that it is not already a part of the list.
*/
static void addToBlockedList(sqlite3 *db){
  sqlite3 **pp;
  assertMutexHeld();
  for(
    pp=&sqlite3BlockedList; 
    *pp && (*pp)->xUnlockNotify!=db->xUnlockNotify; 
    pp=&(*pp)->pNextBlocked
  );
  db->pNextBlocked = *pp;
  *pp = db;
}

/*
** Obtain the STATIC_MASTER mutex.
*/
static void enterMutex(void){
  sqlite3_mutex_enter(sqlite3MutexAlloc(SQLITE_MUTEX_STATIC_MASTER));
  checkListProperties(0);
}

/*
** Release the STATIC_MASTER mutex.
*/
static void leaveMutex(void){
  assertMutexHeld();
  checkListProperties(0);
  sqlite3_mutex_leave(sqlite3MutexAlloc(SQLITE_MUTEX_STATIC_MASTER));
}

/*
** Register an unlock-notify callback.
**
** This is called after connection "db" has attempted some operation
** but has received an SQLITE_LOCKED error because another connection
** (call it pOther) in the same process was busy using the same shared
** cache.  pOther is found by looking at db->pBlockingConnection.
**
** If there is no blocking connection, the callback is invoked immediately,
** before this routine returns.
**
** If pOther is already blocked on db, then report SQLITE_LOCKED, to indicate
** a deadlock.
**
** Otherwise, make arrangements to invoke xNotify when pOther drops
** its locks.
**
** Each call to this routine overrides any prior callbacks registered
** on the same "db".  If xNotify==0 then any prior callbacks are immediately
** cancelled.
*/
SQLITE_API int sqlite3_unlock_notify(
  sqlite3 *db,
  void (*xNotify)(void **, int),
  void *pArg
){
  int rc = SQLITE_OK;

  sqlite3_mutex_enter(db->mutex);
  enterMutex();

  if( xNotify==0 ){
    removeFromBlockedList(db);
    db->pBlockingConnection = 0;
    db->pUnlockConnection = 0;
    db->xUnlockNotify = 0;
    db->pUnlockArg = 0;
  }else if( 0==db->pBlockingConnection ){
    /* The blocking transaction has been concluded. Or there never was a 
    ** blocking transaction. In either case, invoke the notify callback
    ** immediately. 
    */
    xNotify(&pArg, 1);
  }else{
    sqlite3 *p;

    for(p=db->pBlockingConnection; p && p!=db; p=p->pUnlockConnection){}
    if( p ){
      rc = SQLITE_LOCKED;              /* Deadlock detected. */
    }else{
      db->pUnlockConnection = db->pBlockingConnection;
      db->xUnlockNotify = xNotify;
      db->pUnlockArg = pArg;
      removeFromBlockedList(db);
      addToBlockedList(db);
    }
  }

  leaveMutex();
  assert( !db->mallocFailed );
  sqlite3Error(db, rc, (rc?"database is deadlocked":0));
  sqlite3_mutex_leave(db->mutex);
  return rc;
}

/*
** This function is called while stepping or preparing a statement 
** associated with connection db. The operation will return SQLITE_LOCKED
** to the user because it requires a lock that will not be available
** until connection pBlocker concludes its current transaction.
*/
SQLITE_PRIVATE void sqlite3ConnectionBlocked(sqlite3 *db, sqlite3 *pBlocker){
  enterMutex();
  if( db->pBlockingConnection==0 && db->pUnlockConnection==0 ){
    addToBlockedList(db);
  }
  db->pBlockingConnection = pBlocker;
  leaveMutex();
}

/*
** This function is called when
** the transaction opened by database db has just finished. Locks held 
** by database connection db have been released.
**
** This function loops through each entry in the blocked connections
** list and does the following:
**
**   1) If the sqlite3.pBlockingConnection member of a list entry is
**      set to db, then set pBlockingConnection=0.
**
**   2) If the sqlite3.pUnlockConnection member of a list entry is
**      set to db, then invoke the configured unlock-notify callback and
**      set pUnlockConnection=0.
**
**   3) If the two steps above mean that pBlockingConnection==0 and
**      pUnlockConnection==0, remove the entry from the blocked connections
**      list.
*/
SQLITE_PRIVATE void sqlite3ConnectionUnlocked(sqlite3 *db){
  void (*xUnlockNotify)(void **, int) = 0; /* Unlock-notify cb to invoke */
  int nArg = 0;                            /* Number of entries in aArg[] */
  sqlite3 **pp;                            /* Iterator variable */
  void **aArg;               /* Arguments to the unlock callback */
  void **aDyn = 0;           /* Dynamically allocated space for aArg[] */
  void *aStatic[16];         /* Starter space for aArg[].  No malloc required */

  aArg = aStatic;
  enterMutex();         /* Enter STATIC_MASTER mutex */

  /* This loop runs once for each entry in the blocked-connections list. */
  for(pp=&sqlite3BlockedList; *pp; /* no-op */ ){
    sqlite3 *p = *pp;

    /* Step 1. */
    if( p->pBlockingConnection==db ){
      p->pBlockingConnection = 0;
    }

    /* Step 2. */
    if( p->pUnlockConnection==db ){
      assert( p->xUnlockNotify );
      if( p->xUnlockNotify!=xUnlockNotify && nArg!=0 ){
        xUnlockNotify(aArg, nArg);
        nArg = 0;
      }

      sqlite3BeginBenignMalloc();
      assert( aArg==aDyn || (aDyn==0 && aArg==aStatic) );
      assert( nArg<=(int)ArraySize(aStatic) || aArg==aDyn );
      if( (!aDyn && nArg==(int)ArraySize(aStatic))
       || (aDyn && nArg==(int)(sqlite3MallocSize(aDyn)/sizeof(void*)))
      ){
        /* The aArg[] array needs to grow. */
        void **pNew = (void **)sqlite3Malloc(nArg*sizeof(void *)*2);
        if( pNew ){
          memcpy(pNew, aArg, nArg*sizeof(void *));
          sqlite3_free(aDyn);
          aDyn = aArg = pNew;
        }else{
          /* This occurs when the array of context pointers that need to
          ** be passed to the unlock-notify callback is larger than the
          ** aStatic[] array allocated on the stack and the attempt to 
          ** allocate a larger array from the heap has failed.
          **
          ** This is a difficult situation to handle. Returning an error
          ** code to the caller is insufficient, as even if an error code
          ** is returned the transaction on connection db will still be
          ** closed and the unlock-notify callbacks on blocked connections
          ** will go unissued. This might cause the application to wait
          ** indefinitely for an unlock-notify callback that will never 
          ** arrive.
          **
          ** Instead, invoke the unlock-notify callback with the context
          ** array already accumulated. We can then clear the array and
          ** begin accumulating any further context pointers without 
          ** requiring any dynamic allocation. This is sub-optimal because
          ** it means that instead of one callback with a large array of
          ** context pointers the application will receive two or more
          ** callbacks with smaller arrays of context pointers, which will
          ** reduce the applications ability to prioritize multiple 
          ** connections. But it is the best that can be done under the
          ** circumstances.
          */
          xUnlockNotify(aArg, nArg);
          nArg = 0;
        }
      }
      sqlite3EndBenignMalloc();

      aArg[nArg++] = p->pUnlockArg;
      xUnlockNotify = p->xUnlockNotify;
      p->pUnlockConnection = 0;
      p->xUnlockNotify = 0;
      p->pUnlockArg = 0;
    }

    /* Step 3. */
    if( p->pBlockingConnection==0 && p->pUnlockConnection==0 ){
      /* Remove connection p from the blocked connections list. */
      *pp = p->pNextBlocked;
      p->pNextBlocked = 0;
    }else{
      pp = &p->pNextBlocked;
    }
  }

  if( nArg!=0 ){
    xUnlockNotify(aArg, nArg);
  }
  sqlite3_free(aDyn);
  leaveMutex();         /* Leave STATIC_MASTER mutex */
}

/*
** This is called when the database connection passed as an argument is 
** being closed. The connection is removed from the blocked list.
*/
SQLITE_PRIVATE void sqlite3ConnectionClosed(sqlite3 *db){
  sqlite3ConnectionUnlocked(db);
  enterMutex();
  removeFromBlockedList(db);
  checkListProperties(db);
  leaveMutex();
}
#endif

/************** End of notify.c **********************************************/
/************** Begin file fts3.c ********************************************/
/*
** 2006 Oct 10
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
** This is an SQLite module implementing full-text search.
*/

/*
** The code in this file is only compiled if:
**
**     * The FTS3 module is being built as an extension
**       (in which case SQLITE_CORE is not defined), or
**
**     * The FTS3 module is being built into the core of
**       SQLite (in which case SQLITE_ENABLE_FTS3 is defined).
*/

/* The full-text index is stored in a series of b+tree (-like)
** structures called segments which map terms to doclists.  The
** structures are like b+trees in layout, but are constructed from the
** bottom up in optimal fashion and are not updatable.  Since trees
** are built from the bottom up, things will be described from the
** bottom up.
**
**
**** Varints ****
** The basic unit of encoding is a variable-length integer called a
** varint.  We encode variable-length integers in little-endian order
** using seven bits * per byte as follows:
**
** KEY:
**         A = 0xxxxxxx    7 bits of data and one flag bit
**         B = 1xxxxxxx    7 bits of data and one flag bit
**
**  7 bits - A
** 14 bits - BA
** 21 bits - BBA
** and so on.
**
** This is similar in concept to how sqlite encodes "varints" but
** the encoding is not the same.  SQLite varints are big-endian
** are are limited to 9 bytes in length whereas FTS3 varints are
** little-endian and can be up to 10 bytes in length (in theory).
**
** Example encodings:
**
**     1:    0x01
**   127:    0x7f
**   128:    0x81 0x00
**
**
**** Document lists ****
** A doclist (document list) holds a docid-sorted list of hits for a
** given term.  Doclists hold docids and associated token positions.
** A docid is the unique integer identifier for a single document.
** A position is the index of a word within the document.  The first 
** word of the document has a position of 0.
**
** FTS3 used to optionally store character offsets using a compile-time
** option.  But that functionality is no longer supported.
**
** A doclist is stored like this:
**
** array {
**   varint docid;
**   array {                (position list for column 0)
**     varint position;     (2 more than the delta from previous position)
**   }
**   array {
**     varint POS_COLUMN;   (marks start of position list for new column)
**     varint column;       (index of new column)
**     array {
**       varint position;   (2 more than the delta from previous position)
**     }
**   }
**   varint POS_END;        (marks end of positions for this document.
** }
**
** Here, array { X } means zero or more occurrences of X, adjacent in
** memory.  A "position" is an index of a token in the token stream
** generated by the tokenizer. Note that POS_END and POS_COLUMN occur 
** in the same logical place as the position element, and act as sentinals
** ending a position list array.  POS_END is 0.  POS_COLUMN is 1.
** The positions numbers are not stored literally but rather as two more
** than the difference from the prior position, or the just the position plus
** 2 for the first position.  Example:
**
**   label:       A B C D E  F  G H   I  J K
**   value:     123 5 9 1 1 14 35 0 234 72 0
**
** The 123 value is the first docid.  For column zero in this document
** there are two matches at positions 3 and 10 (5-2 and 9-2+3).  The 1
** at D signals the start of a new column; the 1 at E indicates that the
** new column is column number 1.  There are two positions at 12 and 45
** (14-2 and 35-2+12).  The 0 at H indicate the end-of-document.  The
** 234 at I is the next docid.  It has one position 72 (72-2) and then
** terminates with the 0 at K.
**
** A "position-list" is the list of positions for multiple columns for
** a single docid.  A "column-list" is the set of positions for a single
** column.  Hence, a position-list consists of one or more column-lists,
** a document record consists of a docid followed by a position-list and
** a doclist consists of one or more document records.
**
** A bare doclist omits the position information, becoming an 
** array of varint-encoded docids.
**
**** Segment leaf nodes ****
** Segment leaf nodes store terms and doclists, ordered by term.  Leaf
** nodes are written using LeafWriter, and read using LeafReader (to
** iterate through a single leaf node's data) and LeavesReader (to
** iterate through a segment's entire leaf layer).  Leaf nodes have
** the format:
**
** varint iHeight;             (height from leaf level, always 0)
** varint nTerm;               (length of first term)
** char pTerm[nTerm];          (content of first term)
** varint nDoclist;            (length of term's associated doclist)
** char pDoclist[nDoclist];    (content of doclist)
** array {
**                             (further terms are delta-encoded)
**   varint nPrefix;           (length of prefix shared with previous term)
**   varint nSuffix;           (length of unshared suffix)
**   char pTermSuffix[nSuffix];(unshared suffix of next term)
**   varint nDoclist;          (length of term's associated doclist)
**   char pDoclist[nDoclist];  (content of doclist)
** }
**
** Here, array { X } means zero or more occurrences of X, adjacent in
** memory.
**
** Leaf nodes are broken into blocks which are stored contiguously in
** the %_segments table in sorted order.  This means that when the end
** of a node is reached, the next term is in the node with the next
** greater node id.
**
** New data is spilled to a new leaf node when the current node
** exceeds LEAF_MAX bytes (default 2048).  New data which itself is
** larger than STANDALONE_MIN (default 1024) is placed in a standalone
** node (a leaf node with a single term and doclist).  The goal of
** these settings is to pack together groups of small doclists while
** making it efficient to directly access large doclists.  The
** assumption is that large doclists represent terms which are more
** likely to be query targets.
**
** TODO(shess) It may be useful for blocking decisions to be more
** dynamic.  For instance, it may make more sense to have a 2.5k leaf
** node rather than splitting into 2k and .5k nodes.  My intuition is
** that this might extend through 2x or 4x the pagesize.
**
**
**** Segment interior nodes ****
** Segment interior nodes store blockids for subtree nodes and terms
** to describe what data is stored by the each subtree.  Interior
** nodes are written using InteriorWriter, and read using
** InteriorReader.  InteriorWriters are created as needed when
** SegmentWriter creates new leaf nodes, or when an interior node
** itself grows too big and must be split.  The format of interior
** nodes:
**
** varint iHeight;           (height from leaf level, always >0)
** varint iBlockid;          (block id of node's leftmost subtree)
** optional {
**   varint nTerm;           (length of first term)
**   char pTerm[nTerm];      (content of first term)
**   array {
**                                (further terms are delta-encoded)
**     varint nPrefix;            (length of shared prefix with previous term)
**     varint nSuffix;            (length of unshared suffix)
**     char pTermSuffix[nSuffix]; (unshared suffix of next term)
**   }
** }
**
** Here, optional { X } means an optional element, while array { X }
** means zero or more occurrences of X, adjacent in memory.
**
** An interior node encodes n terms separating n+1 subtrees.  The
** subtree blocks are contiguous, so only the first subtree's blockid
** is encoded.  The subtree at iBlockid will contain all terms less
** than the first term encoded (or all terms if no term is encoded).
** Otherwise, for terms greater than or equal to pTerm[i] but less
** than pTerm[i+1], the subtree for that term will be rooted at
** iBlockid+i.  Interior nodes only store enough term data to
** distinguish adjacent children (if the rightmost term of the left
** child is "something", and the leftmost term of the right child is
** "wicked", only "w" is stored).
**
** New data is spilled to a new interior node at the same height when
** the current node exceeds INTERIOR_MAX bytes (default 2048).
** INTERIOR_MIN_TERMS (default 7) keeps large terms from monopolizing
** interior nodes and making the tree too skinny.  The interior nodes
** at a given height are naturally tracked by interior nodes at
** height+1, and so on.
**
**
**** Segment directory ****
** The segment directory in table %_segdir stores meta-information for
** merging and deleting segments, and also the root node of the
** segment's tree.
**
** The root node is the top node of the segment's tree after encoding
** the entire segment, restricted to ROOT_MAX bytes (default 1024).
** This could be either a leaf node or an interior node.  If the top
** node requires more than ROOT_MAX bytes, it is flushed to %_segments
** and a new root interior node is generated (which should always fit
** within ROOT_MAX because it only needs space for 2 varints, the
** height and the blockid of the previous root).
**
** The meta-information in the segment directory is:
**   level               - segment level (see below)
**   idx                 - index within level
**                       - (level,idx uniquely identify a segment)
**   start_block         - first leaf node
**   leaves_end_block    - last leaf node
**   end_block           - last block (including interior nodes)
**   root                - contents of root node
**
** If the root node is a leaf node, then start_block,
** leaves_end_block, and end_block are all 0.
**
**
**** Segment merging ****
** To amortize update costs, segments are grouped into levels and
** merged in batches.  Each increase in level represents exponentially
** more documents.
**
** New documents (actually, document updates) are tokenized and
** written individually (using LeafWriter) to a level 0 segment, with
** incrementing idx.  When idx reaches MERGE_COUNT (default 16), all
** level 0 segments are merged into a single level 1 segment.  Level 1
** is populated like level 0, and eventually MERGE_COUNT level 1
** segments are merged to a single level 2 segment (representing
** MERGE_COUNT^2 updates), and so on.
**
** A segment merge traverses all segments at a given level in
** parallel, performing a straightforward sorted merge.  Since segment
** leaf nodes are written in to the %_segments table in order, this
** merge traverses the underlying sqlite disk structures efficiently.
** After the merge, all segment blocks from the merged level are
** deleted.
**
** MERGE_COUNT controls how often we merge segments.  16 seems to be
** somewhat of a sweet spot for insertion performance.  32 and 64 show
** very similar performance numbers to 16 on insertion, though they're
** a tiny bit slower (perhaps due to more overhead in merge-time
** sorting).  8 is about 20% slower than 16, 4 about 50% slower than
** 16, 2 about 66% slower than 16.
**
** At query time, high MERGE_COUNT increases the number of segments
** which need to be scanned and merged.  For instance, with 100k docs
** inserted:
**
**    MERGE_COUNT   segments
**       16           25
**        8           12
**        4           10
**        2            6
**
** This appears to have only a moderate impact on queries for very
** frequent terms (which are somewhat dominated by segment merge
** costs), and infrequent and non-existent terms still seem to be fast
** even with many segments.
**
** TODO(shess) That said, it would be nice to have a better query-side
** argument for MERGE_COUNT of 16.  Also, it is possible/likely that
** optimizations to things like doclist merging will swing the sweet
** spot around.
**
**
**
**** Handling of deletions and updates ****
** Since we're using a segmented structure, with no docid-oriented
** index into the term index, we clearly cannot simply update the term
** index when a document is deleted or updated.  For deletions, we
** write an empty doclist (varint(docid) varint(POS_END)), for updates
** we simply write the new doclist.  Segment merges overwrite older
** data for a particular docid with newer data, so deletes or updates
** will eventually overtake the earlier data and knock it out.  The
** query logic likewise merges doclists so that newer data knocks out
** older data.
**
** TODO(shess) Provide a VACUUM type operation to clear out all
** deletions and duplications.  This would basically be a forced merge
** into a single segment.
*/

/************** Include fts3Int.h in the middle of fts3.c ********************/
/************** Begin file fts3Int.h *****************************************/
/*
** 2009 Nov 12
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
*/
#ifndef _FTSINT_H
#define _FTSINT_H

#if !defined(NDEBUG) && !defined(SQLITE_DEBUG) 
# define NDEBUG 1
#endif

/*
** FTS4 is really an extension for FTS3.  It is enabled using the
** SQLITE_ENABLE_FTS3 macro.  But to avoid confusion we also all
** the SQLITE_ENABLE_FTS4 macro to serve as an alisse for SQLITE_ENABLE_FTS3.
*/
#if defined(SQLITE_ENABLE_FTS4) && !defined(SQLITE_ENABLE_FTS3)
# define SQLITE_ENABLE_FTS3
#endif

#if !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS3)

/* If not building as part of the core, include sqlite3ext.h. */
#ifndef SQLITE_CORE
SQLITE_API extern const sqlite3_api_routines *sqlite3_api;
#endif

/************** Include fts3_tokenizer.h in the middle of fts3Int.h **********/
/************** Begin file fts3_tokenizer.h **********************************/
/*
** 2006 July 10
**
** The author disclaims copyright to this source code.
**
*************************************************************************
** Defines the interface to tokenizers used by fulltext-search.  There
** are three basic components:
**
** sqlite3_tokenizer_module is a singleton defining the tokenizer
** interface functions.  This is essentially the class structure for
** tokenizers.
**
** sqlite3_tokenizer is used to define a particular tokenizer, perhaps
** including customization information defined at creation time.
**
** sqlite3_tokenizer_cursor is generated by a tokenizer to generate
** tokens from a particular input.
*/
#ifndef _FTS3_TOKENIZER_H_
#define _FTS3_TOKENIZER_H_

/* TODO(shess) Only used for SQLITE_OK and SQLITE_DONE at this time.
** If tokenizers are to be allowed to call sqlite3_*() functions, then
** we will need a way to register the API consistently.
*/

/*
** Structures used by the tokenizer interface. When a new tokenizer
** implementation is registered, the caller provides a pointer to
** an sqlite3_tokenizer_module containing pointers to the callback
** functions that make up an implementation.
**
** When an fts3 table is created, it passes any arguments passed to
** the tokenizer clause of the CREATE VIRTUAL TABLE statement to the
** sqlite3_tokenizer_module.xCreate() function of the requested tokenizer
** implementation. The xCreate() function in turn returns an 
** sqlite3_tokenizer structure representing the specific tokenizer to
** be used for the fts3 table (customized by the tokenizer clause arguments).
**
** To tokenize an input buffer, the sqlite3_tokenizer_module.xOpen()
** method is called. It returns an sqlite3_tokenizer_cursor object
** that may be used to tokenize a specific input buffer based on
** the tokenization rules supplied by a specific sqlite3_tokenizer
** object.
*/
typedef struct sqlite3_tokenizer_module sqlite3_tokenizer_module;
typedef struct sqlite3_tokenizer sqlite3_tokenizer;
typedef struct sqlite3_tokenizer_cursor sqlite3_tokenizer_cursor;

struct sqlite3_tokenizer_module {

  /*
  ** Structure version. Should always be set to 0.
  */
  int iVersion;

  /*
  ** Create a new tokenizer. The values in the argv[] array are the
  ** arguments passed to the "tokenizer" clause of the CREATE VIRTUAL
  ** TABLE statement that created the fts3 table. For example, if
  ** the following SQL is executed:
  **
  **   CREATE .. USING fts3( ... , tokenizer <tokenizer-name> arg1 arg2)
  **
  ** then argc is set to 2, and the argv[] array contains pointers
  ** to the strings "arg1" and "arg2".
  **
  ** This method should return either SQLITE_OK (0), or an SQLite error 
  ** code. If SQLITE_OK is returned, then *ppTokenizer should be set
  ** to point at the newly created tokenizer structure. The generic
  ** sqlite3_tokenizer.pModule variable should not be initialised by
  ** this callback. The caller will do so.
  */
  int (*xCreate)(
    int argc,                           /* Size of argv array */
    const char *const*argv,             /* Tokenizer argument strings */
    sqlite3_tokenizer **ppTokenizer     /* OUT: Created tokenizer */
  );

  /*
  ** Destroy an existing tokenizer. The fts3 module calls this method
  ** exactly once for each successful call to xCreate().
  */
  int (*xDestroy)(sqlite3_tokenizer *pTokenizer);

  /*
  ** Create a tokenizer cursor to tokenize an input buffer. The caller
  ** is responsible for ensuring that the input buffer remains valid
  ** until the cursor is closed (using the xClose() method). 
  */
  int (*xOpen)(
    sqlite3_tokenizer *pTokenizer,       /* Tokenizer object */
    const char *pInput, int nBytes,      /* Input buffer */
    sqlite3_tokenizer_cursor **ppCursor  /* OUT: Created tokenizer cursor */
  );

  /*
  ** Destroy an existing tokenizer cursor. The fts3 module calls this 
  ** method exactly once for each successful call to xOpen().
  */
  int (*xClose)(sqlite3_tokenizer_cursor *pCursor);

  /*
  ** Retrieve the next token from the tokenizer cursor pCursor. This
  ** method should either return SQLITE_OK and set the values of the
  ** "OUT" variables identified below, or SQLITE_DONE to indicate that
  ** the end of the buffer has been reached, or an SQLite error code.
  **
  ** *ppToken should be set to point at a buffer containing the 
  ** normalized version of the token (i.e. after any case-folding and/or
  ** stemming has been performed). *pnBytes should be set to the length
  ** of this buffer in bytes. The input text that generated the token is
  ** identified by the byte offsets returned in *piStartOffset and
  ** *piEndOffset. *piStartOffset should be set to the index of the first
  ** byte of the token in the input buffer. *piEndOffset should be set
  ** to the index of the first byte just past the end of the token in
  ** the input buffer.
  **
  ** The buffer *ppToken is set to point at is managed by the tokenizer
  ** implementation. It is only required to be valid until the next call
  ** to xNext() or xClose(). 
  */
  /* TODO(shess) current implementation requires pInput to be
  ** nul-terminated.  This should either be fixed, or pInput/nBytes
  ** should be converted to zInput.
  */
  int (*xNext)(
    sqlite3_tokenizer_cursor *pCursor,   /* Tokenizer cursor */
    const char **ppToken, int *pnBytes,  /* OUT: Normalized text for token */
    int *piStartOffset,  /* OUT: Byte offset of token in input buffer */
    int *piEndOffset,    /* OUT: Byte offset of end of token in input buffer */
    int *piPosition      /* OUT: Number of tokens returned before this one */
  );
};

struct sqlite3_tokenizer {
  const sqlite3_tokenizer_module *pModule;  /* The module for this tokenizer */
  /* Tokenizer implementations will typically add additional fields */
};

struct sqlite3_tokenizer_cursor {
  sqlite3_tokenizer *pTokenizer;       /* Tokenizer for this cursor. */
  /* Tokenizer implementations will typically add additional fields */
};

int fts3_global_term_cnt(int iTerm, int iCol);
int fts3_term_cnt(int iTerm, int iCol);


#endif /* _FTS3_TOKENIZER_H_ */

/************** End of fts3_tokenizer.h **************************************/
/************** Continuing where we left off in fts3Int.h ********************/
/************** Include fts3_hash.h in the middle of fts3Int.h ***************/
/************** Begin file fts3_hash.h ***************************************/
/*
** 2001 September 22
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This is the header file for the generic hash-table implemenation
** used in SQLite.  We've modified it slightly to serve as a standalone
** hash table implementation for the full-text indexing module.
**
*/
#ifndef _FTS3_HASH_H_
#define _FTS3_HASH_H_

/* Forward declarations of structures. */
typedef struct Fts3Hash Fts3Hash;
typedef struct Fts3HashElem Fts3HashElem;

/* A complete hash table is an instance of the following structure.
** The internals of this structure are intended to be opaque -- client
** code should not attempt to access or modify the fields of this structure
** directly.  Change this structure only by using the routines below.
** However, many of the "procedures" and "functions" for modifying and
** accessing this structure are really macros, so we can't really make
** this structure opaque.
*/
struct Fts3Hash {
  char keyClass;          /* HASH_INT, _POINTER, _STRING, _BINARY */
  char copyKey;           /* True if copy of key made on insert */
  int count;              /* Number of entries in this table */
  Fts3HashElem *first;    /* The first element of the array */
  int htsize;             /* Number of buckets in the hash table */
  struct _fts3ht {        /* the hash table */
    int count;               /* Number of entries with this hash */
    Fts3HashElem *chain;     /* Pointer to first entry with this hash */
  } *ht;
};

/* Each element in the hash table is an instance of the following 
** structure.  All elements are stored on a single doubly-linked list.
**
** Again, this structure is intended to be opaque, but it can't really
** be opaque because it is used by macros.
*/
struct Fts3HashElem {
  Fts3HashElem *next, *prev; /* Next and previous elements in the table */
  void *data;                /* Data associated with this element */
  void *pKey; int nKey;      /* Key associated with this element */
};

/*
** There are 2 different modes of operation for a hash table:
**
**   FTS3_HASH_STRING        pKey points to a string that is nKey bytes long
**                           (including the null-terminator, if any).  Case
**                           is respected in comparisons.
**
**   FTS3_HASH_BINARY        pKey points to binary data nKey bytes long. 
**                           memcmp() is used to compare keys.
**
** A copy of the key is made if the copyKey parameter to fts3HashInit is 1.  
*/
#define FTS3_HASH_STRING    1
#define FTS3_HASH_BINARY    2

/*
** Access routines.  To delete, insert a NULL pointer.
*/
SQLITE_PRIVATE void sqlite3Fts3HashInit(Fts3Hash *pNew, char keyClass, char copyKey);
SQLITE_PRIVATE void *sqlite3Fts3HashInsert(Fts3Hash*, const void *pKey, int nKey, void *pData);
SQLITE_PRIVATE void *sqlite3Fts3HashFind(const Fts3Hash*, const void *pKey, int nKey);
SQLITE_PRIVATE void sqlite3Fts3HashClear(Fts3Hash*);
SQLITE_PRIVATE Fts3HashElem *sqlite3Fts3HashFindElem(const Fts3Hash *, const void *, int);

/*
** Shorthand for the functions above
*/
#define fts3HashInit     sqlite3Fts3HashInit
#define fts3HashInsert   sqlite3Fts3HashInsert
#define fts3HashFind     sqlite3Fts3HashFind
#define fts3HashClear    sqlite3Fts3HashClear
#define fts3HashFindElem sqlite3Fts3HashFindElem

/*
** Macros for looping over all elements of a hash table.  The idiom is
** like this:
**
**   Fts3Hash h;
**   Fts3HashElem *p;
**   ...
**   for(p=fts3HashFirst(&h); p; p=fts3HashNext(p)){
**     SomeStructure *pData = fts3HashData(p);
**     // do something with pData
**   }
*/
#define fts3HashFirst(H)  ((H)->first)
#define fts3HashNext(E)   ((E)->next)
#define fts3HashData(E)   ((E)->data)
#define fts3HashKey(E)    ((E)->pKey)
#define fts3HashKeysize(E) ((E)->nKey)

/*
** Number of entries in a hash table
*/
#define fts3HashCount(H)  ((H)->count)

#endif /* _FTS3_HASH_H_ */

/************** End of fts3_hash.h *******************************************/
/************** Continuing where we left off in fts3Int.h ********************/

/*
** This constant controls how often segments are merged. Once there are
** FTS3_MERGE_COUNT segments of level N, they are merged into a single
** segment of level N+1.
*/
#define FTS3_MERGE_COUNT 16

/*
** This is the maximum amount of data (in bytes) to store in the 
** Fts3Table.pendingTerms hash table. Normally, the hash table is
** populated as documents are inserted/updated/deleted in a transaction
** and used to create a new segment when the transaction is committed.
** However if this limit is reached midway through a transaction, a new 
** segment is created and the hash table cleared immediately.
*/
#define FTS3_MAX_PENDING_DATA (1*1024*1024)

/*
** Macro to return the number of elements in an array. SQLite has a
** similar macro called ArraySize(). Use a different name to avoid
** a collision when building an amalgamation with built-in FTS3.
*/
#define SizeofArray(X) ((int)(sizeof(X)/sizeof(X[0])))


#ifndef MIN
# define MIN(x,y) ((x)<(y)?(x):(y))
#endif

/*
** Maximum length of a varint encoded integer. The varint format is different
** from that used by SQLite, so the maximum length is 10, not 9.
*/
#define FTS3_VARINT_MAX 10

/*
** FTS4 virtual tables may maintain multiple indexes - one index of all terms
** in the document set and zero or more prefix indexes. All indexes are stored
** as one or more b+-trees in the %_segments and %_segdir tables. 
**
** It is possible to determine which index a b+-tree belongs to based on the
** value stored in the "%_segdir.level" column. Given this value L, the index
** that the b+-tree belongs to is (L<<10). In other words, all b+-trees with
** level values between 0 and 1023 (inclusive) belong to index 0, all levels
** between 1024 and 2047 to index 1, and so on.
**
** It is considered impossible for an index to use more than 1024 levels. In 
** theory though this may happen, but only after at least 
** (FTS3_MERGE_COUNT^1024) separate flushes of the pending-terms tables.
*/
#define FTS3_SEGDIR_MAXLEVEL      1024
#define FTS3_SEGDIR_MAXLEVEL_STR "1024"

/*
** The testcase() macro is only used by the amalgamation.  If undefined,
** make it a no-op.
*/
#ifndef testcase
# define testcase(X)
#endif

/*
** Terminator values for position-lists and column-lists.
*/
#define POS_COLUMN  (1)     /* Column-list terminator */
#define POS_END     (0)     /* Position-list terminator */ 

/*
** This section provides definitions to allow the
** FTS3 extension to be compiled outside of the 
** amalgamation.
*/
#ifndef SQLITE_AMALGAMATION
/*
** Macros indicating that conditional expressions are always true or
** false.
*/
#ifdef SQLITE_COVERAGE_TEST
# define ALWAYS(x) (1)
# define NEVER(X)  (0)
#else
# define ALWAYS(x) (x)
# define NEVER(X)  (x)
#endif

/*
** Internal types used by SQLite.
*/
typedef unsigned char u8;         /* 1-byte (or larger) unsigned integer */
typedef short int i16;            /* 2-byte (or larger) signed integer */
typedef unsigned int u32;         /* 4-byte unsigned integer */
typedef sqlite3_uint64 u64;       /* 8-byte unsigned integer */

/*
** Macro used to suppress compiler warnings for unused parameters.
*/
#define UNUSED_PARAMETER(x) (void)(x)

/*
** Activate assert() only if SQLITE_TEST is enabled.
*/
#if !defined(NDEBUG) && !defined(SQLITE_DEBUG) 
# define NDEBUG 1
#endif

/*
** The TESTONLY macro is used to enclose variable declarations or
** other bits of code that are needed to support the arguments
** within testcase() and assert() macros.
*/
#if defined(SQLITE_DEBUG) || defined(SQLITE_COVERAGE_TEST)
# define TESTONLY(X)  X
#else
# define TESTONLY(X)
#endif

#endif /* SQLITE_AMALGAMATION */

#ifdef SQLITE_DEBUG
SQLITE_PRIVATE int sqlite3Fts3Corrupt(void);
# define FTS_CORRUPT_VTAB sqlite3Fts3Corrupt()
#else
# define FTS_CORRUPT_VTAB SQLITE_CORRUPT_VTAB
#endif

typedef struct Fts3Table Fts3Table;
typedef struct Fts3Cursor Fts3Cursor;
typedef struct Fts3Expr Fts3Expr;
typedef struct Fts3Phrase Fts3Phrase;
typedef struct Fts3PhraseToken Fts3PhraseToken;

typedef struct Fts3Doclist Fts3Doclist;
typedef struct Fts3SegFilter Fts3SegFilter;
typedef struct Fts3DeferredToken Fts3DeferredToken;
typedef struct Fts3SegReader Fts3SegReader;
typedef struct Fts3MultiSegReader Fts3MultiSegReader;

/*
** A connection to a fulltext index is an instance of the following
** structure. The xCreate and xConnect methods create an instance
** of this structure and xDestroy and xDisconnect free that instance.
** All other methods receive a pointer to the structure as one of their
** arguments.
*/
struct Fts3Table {
  sqlite3_vtab base;              /* Base class used by SQLite core */
  sqlite3 *db;                    /* The database connection */
  const char *zDb;                /* logical database name */
  const char *zName;              /* virtual table name */
  int nColumn;                    /* number of named columns in virtual table */
  char **azColumn;                /* column names.  malloced */
  sqlite3_tokenizer *pTokenizer;  /* tokenizer for inserts and queries */
  char *zContentTbl;              /* content=xxx option, or NULL */

  /* Precompiled statements used by the implementation. Each of these 
  ** statements is run and reset within a single virtual table API call. 
  */
  sqlite3_stmt *aStmt[27];

  char *zReadExprlist;
  char *zWriteExprlist;

  int nNodeSize;                  /* Soft limit for node size */
  u8 bHasStat;                    /* True if %_stat table exists */
  u8 bHasDocsize;                 /* True if %_docsize table exists */
  u8 bDescIdx;                    /* True if doclists are in reverse order */
  int nPgsz;                      /* Page size for host database */
  char *zSegmentsTbl;             /* Name of %_segments table */
  sqlite3_blob *pSegments;        /* Blob handle open on %_segments table */

  /* TODO: Fix the first paragraph of this comment.
  **
  ** The following hash table is used to buffer pending index updates during
  ** transactions. Variable nPendingData estimates the memory size of the 
  ** pending data, including hash table overhead, but not malloc overhead. 
  ** When nPendingData exceeds nMaxPendingData, the buffer is flushed 
  ** automatically. Variable iPrevDocid is the docid of the most recently
  ** inserted record.
  **
  ** A single FTS4 table may have multiple full-text indexes. For each index
  ** there is an entry in the aIndex[] array. Index 0 is an index of all the
  ** terms that appear in the document set. Each subsequent index in aIndex[]
  ** is an index of prefixes of a specific length.
  */
  int nIndex;                     /* Size of aIndex[] */
  struct Fts3Index {
    int nPrefix;                  /* Prefix length (0 for main terms index) */
    Fts3Hash hPending;            /* Pending terms table for this index */
  } *aIndex;
  int nMaxPendingData;            /* Max pending data before flush to disk */
  int nPendingData;               /* Current bytes of pending data */
  sqlite_int64 iPrevDocid;        /* Docid of most recently inserted document */

#if defined(SQLITE_DEBUG) || defined(SQLITE_COVERAGE_TEST)
  /* State variables used for validating that the transaction control
  ** methods of the virtual table are called at appropriate times.  These
  ** values do not contribution to the FTS computation; they are used for
  ** verifying the SQLite core.
  */
  int inTransaction;     /* True after xBegin but before xCommit/xRollback */
  int mxSavepoint;       /* Largest valid xSavepoint integer */
#endif
};

/*
** When the core wants to read from the virtual table, it creates a
** virtual table cursor (an instance of the following structure) using
** the xOpen method. Cursors are destroyed using the xClose method.
*/
struct Fts3Cursor {
  sqlite3_vtab_cursor base;       /* Base class used by SQLite core */
  i16 eSearch;                    /* Search strategy (see below) */
  u8 isEof;                       /* True if at End Of Results */
  u8 isRequireSeek;               /* True if must seek pStmt to %_content row */
  sqlite3_stmt *pStmt;            /* Prepared statement in use by the cursor */
  Fts3Expr *pExpr;                /* Parsed MATCH query string */
  int nPhrase;                    /* Number of matchable phrases in query */
  Fts3DeferredToken *pDeferred;   /* Deferred search tokens, if any */
  sqlite3_int64 iPrevId;          /* Previous id read from aDoclist */
  char *pNextId;                  /* Pointer into the body of aDoclist */
  char *aDoclist;                 /* List of docids for full-text queries */
  int nDoclist;                   /* Size of buffer at aDoclist */
  u8 bDesc;                       /* True to sort in descending order */
  int eEvalmode;                  /* An FTS3_EVAL_XX constant */
  int nRowAvg;                    /* Average size of database rows, in pages */
  sqlite3_int64 nDoc;             /* Documents in table */

  int isMatchinfoNeeded;          /* True when aMatchinfo[] needs filling in */
  u32 *aMatchinfo;                /* Information about most recent match */
  int nMatchinfo;                 /* Number of elements in aMatchinfo[] */
  char *zMatchinfo;               /* Matchinfo specification */
};

#define FTS3_EVAL_FILTER    0
#define FTS3_EVAL_NEXT      1
#define FTS3_EVAL_MATCHINFO 2

/*
** The Fts3Cursor.eSearch member is always set to one of the following.
** Actualy, Fts3Cursor.eSearch can be greater than or equal to
** FTS3_FULLTEXT_SEARCH.  If so, then Fts3Cursor.eSearch - 2 is the index
** of the column to be searched.  For example, in
**
**     CREATE VIRTUAL TABLE ex1 USING fts3(a,b,c,d);
**     SELECT docid FROM ex1 WHERE b MATCH 'one two three';
** 
** Because the LHS of the MATCH operator is 2nd column "b",
** Fts3Cursor.eSearch will be set to FTS3_FULLTEXT_SEARCH+1.  (+0 for a,
** +1 for b, +2 for c, +3 for d.)  If the LHS of MATCH were "ex1" 
** indicating that all columns should be searched,
** then eSearch would be set to FTS3_FULLTEXT_SEARCH+4.
*/
#define FTS3_FULLSCAN_SEARCH 0    /* Linear scan of %_content table */
#define FTS3_DOCID_SEARCH    1    /* Lookup by rowid on %_content table */
#define FTS3_FULLTEXT_SEARCH 2    /* Full-text index search */


struct Fts3Doclist {
  char *aAll;                    /* Array containing doclist (or NULL) */
  int nAll;                      /* Size of a[] in bytes */
  char *pNextDocid;              /* Pointer to next docid */

  sqlite3_int64 iDocid;          /* Current docid (if pList!=0) */
  int bFreeList;                 /* True if pList should be sqlite3_free()d */
  char *pList;                   /* Pointer to position list following iDocid */
  int nList;                     /* Length of position list */
};

/*
** A "phrase" is a sequence of one or more tokens that must match in
** sequence.  A single token is the base case and the most common case.
** For a sequence of tokens contained in double-quotes (i.e. "one two three")
** nToken will be the number of tokens in the string.
*/
struct Fts3PhraseToken {
  char *z;                        /* Text of the token */
  int n;                          /* Number of bytes in buffer z */
  int isPrefix;                   /* True if token ends with a "*" character */
  int bFirst;                     /* True if token must appear at position 0 */

  /* Variables above this point are populated when the expression is
  ** parsed (by code in fts3_expr.c). Below this point the variables are
  ** used when evaluating the expression. */
  Fts3DeferredToken *pDeferred;   /* Deferred token object for this token */
  Fts3MultiSegReader *pSegcsr;    /* Segment-reader for this token */
};

struct Fts3Phrase {
  /* Cache of doclist for this phrase. */
  Fts3Doclist doclist;
  int bIncr;                 /* True if doclist is loaded incrementally */
  int iDoclistToken;

  /* Variables below this point are populated by fts3_expr.c when parsing 
  ** a MATCH expression. Everything above is part of the evaluation phase. 
  */
  int nToken;                /* Number of tokens in the phrase */
  int iColumn;               /* Index of column this phrase must match */
  Fts3PhraseToken aToken[1]; /* One entry for each token in the phrase */
};

/*
** A tree of these objects forms the RHS of a MATCH operator.
**
** If Fts3Expr.eType is FTSQUERY_PHRASE and isLoaded is true, then aDoclist 
** points to a malloced buffer, size nDoclist bytes, containing the results 
** of this phrase query in FTS3 doclist format. As usual, the initial 
** "Length" field found in doclists stored on disk is omitted from this 
** buffer.
**
** Variable aMI is used only for FTSQUERY_NEAR nodes to store the global
** matchinfo data. If it is not NULL, it points to an array of size nCol*3,
** where nCol is the number of columns in the queried FTS table. The array
** is populated as follows:
**
**   aMI[iCol*3 + 0] = Undefined
**   aMI[iCol*3 + 1] = Number of occurrences
**   aMI[iCol*3 + 2] = Number of rows containing at least one instance
**
** The aMI array is allocated using sqlite3_malloc(). It should be freed 
** when the expression node is.
*/
struct Fts3Expr {
  int eType;                 /* One of the FTSQUERY_XXX values defined below */
  int nNear;                 /* Valid if eType==FTSQUERY_NEAR */
  Fts3Expr *pParent;         /* pParent->pLeft==this or pParent->pRight==this */
  Fts3Expr *pLeft;           /* Left operand */
  Fts3Expr *pRight;          /* Right operand */
  Fts3Phrase *pPhrase;       /* Valid if eType==FTSQUERY_PHRASE */

  /* The following are used by the fts3_eval.c module. */
  sqlite3_int64 iDocid;      /* Current docid */
  u8 bEof;                   /* True this expression is at EOF already */
  u8 bStart;                 /* True if iDocid is valid */
  u8 bDeferred;              /* True if this expression is entirely deferred */

  u32 *aMI;
};

/*
** Candidate values for Fts3Query.eType. Note that the order of the first
** four values is in order of precedence when parsing expressions. For 
** example, the following:
**
**   "a OR b AND c NOT d NEAR e"
**
** is equivalent to:
**
**   "a OR (b AND (c NOT (d NEAR e)))"
*/
#define FTSQUERY_NEAR   1
#define FTSQUERY_NOT    2
#define FTSQUERY_AND    3
#define FTSQUERY_OR     4
#define FTSQUERY_PHRASE 5


/* fts3_write.c */
SQLITE_PRIVATE int sqlite3Fts3UpdateMethod(sqlite3_vtab*,int,sqlite3_value**,sqlite3_int64*);
SQLITE_PRIVATE int sqlite3Fts3PendingTermsFlush(Fts3Table *);
SQLITE_PRIVATE void sqlite3Fts3PendingTermsClear(Fts3Table *);
SQLITE_PRIVATE int sqlite3Fts3Optimize(Fts3Table *);
SQLITE_PRIVATE int sqlite3Fts3SegReaderNew(int, sqlite3_int64,
  sqlite3_int64, sqlite3_int64, const char *, int, Fts3SegReader**);
SQLITE_PRIVATE int sqlite3Fts3SegReaderPending(
  Fts3Table*,int,const char*,int,int,Fts3SegReader**);
SQLITE_PRIVATE void sqlite3Fts3SegReaderFree(Fts3SegReader *);
SQLITE_PRIVATE int sqlite3Fts3AllSegdirs(Fts3Table*, int, int, sqlite3_stmt **);
SQLITE_PRIVATE int sqlite3Fts3ReadLock(Fts3Table *);
SQLITE_PRIVATE int sqlite3Fts3ReadBlock(Fts3Table*, sqlite3_int64, char **, int*, int*);

SQLITE_PRIVATE int sqlite3Fts3SelectDoctotal(Fts3Table *, sqlite3_stmt **);
SQLITE_PRIVATE int sqlite3Fts3SelectDocsize(Fts3Table *, sqlite3_int64, sqlite3_stmt **);

SQLITE_PRIVATE void sqlite3Fts3FreeDeferredTokens(Fts3Cursor *);
SQLITE_PRIVATE int sqlite3Fts3DeferToken(Fts3Cursor *, Fts3PhraseToken *, int);
SQLITE_PRIVATE int sqlite3Fts3CacheDeferredDoclists(Fts3Cursor *);
SQLITE_PRIVATE void sqlite3Fts3FreeDeferredDoclists(Fts3Cursor *);
SQLITE_PRIVATE void sqlite3Fts3SegmentsClose(Fts3Table *);

/* Special values interpreted by sqlite3SegReaderCursor() */
#define FTS3_SEGCURSOR_PENDING        -1
#define FTS3_SEGCURSOR_ALL            -2

SQLITE_PRIVATE int sqlite3Fts3SegReaderStart(Fts3Table*, Fts3MultiSegReader*, Fts3SegFilter*);
SQLITE_PRIVATE int sqlite3Fts3SegReaderStep(Fts3Table *, Fts3MultiSegReader *);
SQLITE_PRIVATE void sqlite3Fts3SegReaderFinish(Fts3MultiSegReader *);

SQLITE_PRIVATE int sqlite3Fts3SegReaderCursor(
    Fts3Table *, int, int, const char *, int, int, int, Fts3MultiSegReader *);

/* Flags allowed as part of the 4th argument to SegmentReaderIterate() */
#define FTS3_SEGMENT_REQUIRE_POS   0x00000001
#define FTS3_SEGMENT_IGNORE_EMPTY  0x00000002
#define FTS3_SEGMENT_COLUMN_FILTER 0x00000004
#define FTS3_SEGMENT_PREFIX        0x00000008
#define FTS3_SEGMENT_SCAN          0x00000010
#define FTS3_SEGMENT_FIRST         0x00000020

/* Type passed as 4th argument to SegmentReaderIterate() */
struct Fts3SegFilter {
  const char *zTerm;
  int nTerm;
  int iCol;
  int flags;
};

struct Fts3MultiSegReader {
  /* Used internally by sqlite3Fts3SegReaderXXX() calls */
  Fts3SegReader **apSegment;      /* Array of Fts3SegReader objects */
  int nSegment;                   /* Size of apSegment array */
  int nAdvance;                   /* How many seg-readers to advance */
  Fts3SegFilter *pFilter;         /* Pointer to filter object */
  char *aBuffer;                  /* Buffer to merge doclists in */
  int nBuffer;                    /* Allocated size of aBuffer[] in bytes */

  int iColFilter;                 /* If >=0, filter for this column */
  int bRestart;

  /* Used by fts3.c only. */
  int nCost;                      /* Cost of running iterator */
  int bLookup;                    /* True if a lookup of a single entry. */

  /* Output values. Valid only after Fts3SegReaderStep() returns SQLITE_ROW. */
  char *zTerm;                    /* Pointer to term buffer */
  int nTerm;                      /* Size of zTerm in bytes */
  char *aDoclist;                 /* Pointer to doclist buffer */
  int nDoclist;                   /* Size of aDoclist[] in bytes */
};

/* fts3.c */
SQLITE_PRIVATE int sqlite3Fts3PutVarint(char *, sqlite3_int64);
SQLITE_PRIVATE int sqlite3Fts3GetVarint(const char *, sqlite_int64 *);
SQLITE_PRIVATE int sqlite3Fts3GetVarint32(const char *, int *);
SQLITE_PRIVATE int sqlite3Fts3VarintLen(sqlite3_uint64);
SQLITE_PRIVATE void sqlite3Fts3Dequote(char *);
SQLITE_PRIVATE void sqlite3Fts3DoclistPrev(int,char*,int,char**,sqlite3_int64*,int*,u8*);
SQLITE_PRIVATE int sqlite3Fts3EvalPhraseStats(Fts3Cursor *, Fts3Expr *, u32 *);
SQLITE_PRIVATE int sqlite3Fts3FirstFilter(sqlite3_int64, char *, int, char *);

/* fts3_tokenizer.c */
SQLITE_PRIVATE const char *sqlite3Fts3NextToken(const char *, int *);
SQLITE_PRIVATE int sqlite3Fts3InitHashTable(sqlite3 *, Fts3Hash *, const char *);
SQLITE_PRIVATE int sqlite3Fts3InitTokenizer(Fts3Hash *pHash, const char *, 
    sqlite3_tokenizer **, char **
);
SQLITE_PRIVATE int sqlite3Fts3IsIdChar(char);

/* fts3_snippet.c */
SQLITE_PRIVATE void sqlite3Fts3Offsets(sqlite3_context*, Fts3Cursor*);
SQLITE_PRIVATE void sqlite3Fts3Snippet(sqlite3_context *, Fts3Cursor *, const char *,
  const char *, const char *, int, int
);
SQLITE_PRIVATE void sqlite3Fts3Matchinfo(sqlite3_context *, Fts3Cursor *, const char *);

/* fts3_expr.c */
SQLITE_PRIVATE int sqlite3Fts3ExprParse(sqlite3_tokenizer *, 
  char **, int, int, int, const char *, int, Fts3Expr **
);
SQLITE_PRIVATE void sqlite3Fts3ExprFree(Fts3Expr *);
#ifdef SQLITE_TEST
SQLITE_PRIVATE int sqlite3Fts3ExprInitTestInterface(sqlite3 *db);
SQLITE_PRIVATE int sqlite3Fts3InitTerm(sqlite3 *db);
#endif

/* fts3_aux.c */
SQLITE_PRIVATE int sqlite3Fts3InitAux(sqlite3 *db);

SQLITE_PRIVATE void sqlite3Fts3EvalPhraseCleanup(Fts3Phrase *);

SQLITE_PRIVATE int sqlite3Fts3MsrIncrStart(
    Fts3Table*, Fts3MultiSegReader*, int, const char*, int);
SQLITE_PRIVATE int sqlite3Fts3MsrIncrNext(
    Fts3Table *, Fts3MultiSegReader *, sqlite3_int64 *, char **, int *);
SQLITE_PRIVATE char *sqlite3Fts3EvalPhrasePoslist(Fts3Cursor *, Fts3Expr *, int iCol); 
SQLITE_PRIVATE int sqlite3Fts3MsrOvfl(Fts3Cursor *, Fts3MultiSegReader *, int *);
SQLITE_PRIVATE int sqlite3Fts3MsrIncrRestart(Fts3MultiSegReader *pCsr);

SQLITE_PRIVATE int sqlite3Fts3DeferredTokenList(Fts3DeferredToken *, char **, int *);

#endif /* !SQLITE_CORE || SQLITE_ENABLE_FTS3 */
#endif /* _FTSINT_H */

/************** End of fts3Int.h *********************************************/
/************** Continuing where we left off in fts3.c ***********************/
#if !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS3)

#if defined(SQLITE_ENABLE_FTS3) && !defined(SQLITE_CORE)
# define SQLITE_CORE 1
#endif

/* #include <assert.h> */
/* #include <stdlib.h> */
/* #include <stddef.h> */
/* #include <stdio.h> */
/* #include <string.h> */
/* #include <stdarg.h> */

#ifndef SQLITE_CORE 
  SQLITE_EXTENSION_INIT1
#endif

static int fts3EvalNext(Fts3Cursor *pCsr);
static int fts3EvalStart(Fts3Cursor *pCsr);
static int fts3TermSegReaderCursor(
    Fts3Cursor *, const char *, int, int, Fts3MultiSegReader **);

/* 
** Write a 64-bit variable-length integer to memory starting at p[0].
** The length of data written will be between 1 and FTS3_VARINT_MAX bytes.
** The number of bytes written is returned.
*/
SQLITE_PRIVATE int sqlite3Fts3PutVarint(char *p, sqlite_int64 v){
  unsigned char *q = (unsigned char *) p;
  sqlite_uint64 vu = v;
  do{
    *q++ = (unsigned char) ((vu & 0x7f) | 0x80);
    vu >>= 7;
  }while( vu!=0 );
  q[-1] &= 0x7f;  /* turn off high bit in final byte */
  assert( q - (unsigned char *)p <= FTS3_VARINT_MAX );
  return (int) (q - (unsigned char *)p);
}

/* 
** Read a 64-bit variable-length integer from memory starting at p[0].
** Return the number of bytes read, or 0 on error.
** The value is stored in *v.
*/
SQLITE_PRIVATE int sqlite3Fts3GetVarint(const char *p, sqlite_int64 *v){
  const unsigned char *q = (const unsigned char *) p;
  sqlite_uint64 x = 0, y = 1;
  while( (*q&0x80)==0x80 && q-(unsigned char *)p<FTS3_VARINT_MAX ){
    x += y * (*q++ & 0x7f);
    y <<= 7;
  }
  x += y * (*q++);
  *v = (sqlite_int64) x;
  return (int) (q - (unsigned char *)p);
}

/*
** Similar to sqlite3Fts3GetVarint(), except that the output is truncated to a
** 32-bit integer before it is returned.
*/
SQLITE_PRIVATE int sqlite3Fts3GetVarint32(const char *p, int *pi){
 sqlite_int64 i;
 int ret = sqlite3Fts3GetVarint(p, &i);
 *pi = (int) i;
 return ret;
}

/*
** Return the number of bytes required to encode v as a varint
*/
SQLITE_PRIVATE int sqlite3Fts3VarintLen(sqlite3_uint64 v){
  int i = 0;
  do{
    i++;
    v >>= 7;
  }while( v!=0 );
  return i;
}

/*
** Convert an SQL-style quoted string into a normal string by removing
** the quote characters.  The conversion is done in-place.  If the
** input does not begin with a quote character, then this routine
** is a no-op.
**
** Examples:
**
**     "abc"   becomes   abc
**     'xyz'   becomes   xyz
**     [pqr]   becomes   pqr
**     `mno`   becomes   mno
**
*/
SQLITE_PRIVATE void sqlite3Fts3Dequote(char *z){
  char quote;                     /* Quote character (if any ) */

  quote = z[0];
  if( quote=='[' || quote=='\'' || quote=='"' || quote=='`' ){
    int iIn = 1;                  /* Index of next byte to read from input */
    int iOut = 0;                 /* Index of next byte to write to output */

    /* If the first byte was a '[', then the close-quote character is a ']' */
    if( quote=='[' ) quote = ']';  

    while( ALWAYS(z[iIn]) ){
      if( z[iIn]==quote ){
        if( z[iIn+1]!=quote ) break;
        z[iOut++] = quote;
        iIn += 2;
      }else{
        z[iOut++] = z[iIn++];
      }
    }
    z[iOut] = '\0';
  }
}

/*
** Read a single varint from the doclist at *pp and advance *pp to point
** to the first byte past the end of the varint.  Add the value of the varint
** to *pVal.
*/
static void fts3GetDeltaVarint(char **pp, sqlite3_int64 *pVal){
  sqlite3_int64 iVal;
  *pp += sqlite3Fts3GetVarint(*pp, &iVal);
  *pVal += iVal;
}

/*
** When this function is called, *pp points to the first byte following a
** varint that is part of a doclist (or position-list, or any other list
** of varints). This function moves *pp to point to the start of that varint,
** and sets *pVal by the varint value.
**
** Argument pStart points to the first byte of the doclist that the
** varint is part of.
*/
static void fts3GetReverseVarint(
  char **pp, 
  char *pStart, 
  sqlite3_int64 *pVal
){
  sqlite3_int64 iVal;
  char *p;

  /* Pointer p now points at the first byte past the varint we are 
  ** interested in. So, unless the doclist is corrupt, the 0x80 bit is
  ** clear on character p[-1]. */
  for(p = (*pp)-2; p>=pStart && *p&0x80; p--);
  p++;
  *pp = p;

  sqlite3Fts3GetVarint(p, &iVal);
  *pVal = iVal;
}

/*
** The xDisconnect() virtual table method.
*/
static int fts3DisconnectMethod(sqlite3_vtab *pVtab){
  Fts3Table *p = (Fts3Table *)pVtab;
  int i;

  assert( p->nPendingData==0 );
  assert( p->pSegments==0 );

  /* Free any prepared statements held */
  for(i=0; i<SizeofArray(p->aStmt); i++){
    sqlite3_finalize(p->aStmt[i]);
  }
  sqlite3_free(p->zSegmentsTbl);
  sqlite3_free(p->zReadExprlist);
  sqlite3_free(p->zWriteExprlist);
  sqlite3_free(p->zContentTbl);

  /* Invoke the tokenizer destructor to free the tokenizer. */
  p->pTokenizer->pModule->xDestroy(p->pTokenizer);

  sqlite3_free(p);
  return SQLITE_OK;
}

/*
** Construct one or more SQL statements from the format string given
** and then evaluate those statements. The success code is written
** into *pRc.
**
** If *pRc is initially non-zero then this routine is a no-op.
*/
static void fts3DbExec(
  int *pRc,              /* Success code */
  sqlite3 *db,           /* Database in which to run SQL */
  const char *zFormat,   /* Format string for SQL */
  ...                    /* Arguments to the format string */
){
  va_list ap;
  char *zSql;
  if( *pRc ) return;
  va_start(ap, zFormat);
  zSql = sqlite3_vmprintf(zFormat, ap);
  va_end(ap);
  if( zSql==0 ){
    *pRc = SQLITE_NOMEM;
  }else{
    *pRc = sqlite3_exec(db, zSql, 0, 0, 0);
    sqlite3_free(zSql);
  }
}

/*
** The xDestroy() virtual table method.
*/
static int fts3DestroyMethod(sqlite3_vtab *pVtab){
  Fts3Table *p = (Fts3Table *)pVtab;
  int rc = SQLITE_OK;              /* Return code */
  const char *zDb = p->zDb;        /* Name of database (e.g. "main", "temp") */
  sqlite3 *db = p->db;             /* Database handle */

  /* Drop the shadow tables */
  if( p->zContentTbl==0 ){
    fts3DbExec(&rc, db, "DROP TABLE IF EXISTS %Q.'%q_content'", zDb, p->zName);
  }
  fts3DbExec(&rc, db, "DROP TABLE IF EXISTS %Q.'%q_segments'", zDb,p->zName);
  fts3DbExec(&rc, db, "DROP TABLE IF EXISTS %Q.'%q_segdir'", zDb, p->zName);
  fts3DbExec(&rc, db, "DROP TABLE IF EXISTS %Q.'%q_docsize'", zDb, p->zName);
  fts3DbExec(&rc, db, "DROP TABLE IF EXISTS %Q.'%q_stat'", zDb, p->zName);

  /* If everything has worked, invoke fts3DisconnectMethod() to free the
  ** memory associated with the Fts3Table structure and return SQLITE_OK.
  ** Otherwise, return an SQLite error code.
  */
  return (rc==SQLITE_OK ? fts3DisconnectMethod(pVtab) : rc);
}


/*
** Invoke sqlite3_declare_vtab() to declare the schema for the FTS3 table
** passed as the first argument. This is done as part of the xConnect()
** and xCreate() methods.
**
** If *pRc is non-zero when this function is called, it is a no-op. 
** Otherwise, if an error occurs, an SQLite error code is stored in *pRc
** before returning.
*/
static void fts3DeclareVtab(int *pRc, Fts3Table *p){
  if( *pRc==SQLITE_OK ){
    int i;                        /* Iterator variable */
    int rc;                       /* Return code */
    char *zSql;                   /* SQL statement passed to declare_vtab() */
    char *zCols;                  /* List of user defined columns */

    sqlite3_vtab_config(p->db, SQLITE_VTAB_CONSTRAINT_SUPPORT, 1);

    /* Create a list of user columns for the virtual table */
    zCols = sqlite3_mprintf("%Q, ", p->azColumn[0]);
    for(i=1; zCols && i<p->nColumn; i++){
      zCols = sqlite3_mprintf("%z%Q, ", zCols, p->azColumn[i]);
    }

    /* Create the whole "CREATE TABLE" statement to pass to SQLite */
    zSql = sqlite3_mprintf(
        "CREATE TABLE x(%s %Q HIDDEN, docid HIDDEN)", zCols, p->zName
    );
    if( !zCols || !zSql ){
      rc = SQLITE_NOMEM;
    }else{
      rc = sqlite3_declare_vtab(p->db, zSql);
    }

    sqlite3_free(zSql);
    sqlite3_free(zCols);
    *pRc = rc;
  }
}

/*
** Create the backing store tables (%_content, %_segments and %_segdir)
** required by the FTS3 table passed as the only argument. This is done
** as part of the vtab xCreate() method.
**
** If the p->bHasDocsize boolean is true (indicating that this is an
** FTS4 table, not an FTS3 table) then also create the %_docsize and
** %_stat tables required by FTS4.
*/
static int fts3CreateTables(Fts3Table *p){
  int rc = SQLITE_OK;             /* Return code */
  int i;                          /* Iterator variable */
  sqlite3 *db = p->db;            /* The database connection */

  if( p->zContentTbl==0 ){
    char *zContentCols;           /* Columns of %_content table */

    /* Create a list of user columns for the content table */
    zContentCols = sqlite3_mprintf("docid INTEGER PRIMARY KEY");
    for(i=0; zContentCols && i<p->nColumn; i++){
      char *z = p->azColumn[i];
      zContentCols = sqlite3_mprintf("%z, 'c%d%q'", zContentCols, i, z);
    }
    if( zContentCols==0 ) rc = SQLITE_NOMEM;
  
    /* Create the content table */
    fts3DbExec(&rc, db, 
       "CREATE TABLE %Q.'%q_content'(%s)",
       p->zDb, p->zName, zContentCols
    );
    sqlite3_free(zContentCols);
  }

  /* Create other tables */
  fts3DbExec(&rc, db, 
      "CREATE TABLE %Q.'%q_segments'(blockid INTEGER PRIMARY KEY, block BLOB);",
      p->zDb, p->zName
  );
  fts3DbExec(&rc, db, 
      "CREATE TABLE %Q.'%q_segdir'("
        "level INTEGER,"
        "idx INTEGER,"
        "start_block INTEGER,"
        "leaves_end_block INTEGER,"
        "end_block INTEGER,"
        "root BLOB,"
        "PRIMARY KEY(level, idx)"
      ");",
      p->zDb, p->zName
  );
  if( p->bHasDocsize ){
    fts3DbExec(&rc, db, 
        "CREATE TABLE %Q.'%q_docsize'(docid INTEGER PRIMARY KEY, size BLOB);",
        p->zDb, p->zName
    );
  }
  if( p->bHasStat ){
    fts3DbExec(&rc, db, 
        "CREATE TABLE %Q.'%q_stat'(id INTEGER PRIMARY KEY, value BLOB);",
        p->zDb, p->zName
    );
  }
  return rc;
}

/*
** Store the current database page-size in bytes in p->nPgsz.
**
** If *pRc is non-zero when this function is called, it is a no-op. 
** Otherwise, if an error occurs, an SQLite error code is stored in *pRc
** before returning.
*/
static void fts3DatabasePageSize(int *pRc, Fts3Table *p){
  if( *pRc==SQLITE_OK ){
    int rc;                       /* Return code */
    char *zSql;                   /* SQL text "PRAGMA %Q.page_size" */
    sqlite3_stmt *pStmt;          /* Compiled "PRAGMA %Q.page_size" statement */
  
    zSql = sqlite3_mprintf("PRAGMA %Q.page_size", p->zDb);
    if( !zSql ){
      rc = SQLITE_NOMEM;
    }else{
      rc = sqlite3_prepare(p->db, zSql, -1, &pStmt, 0);
      if( rc==SQLITE_OK ){
        sqlite3_step(pStmt);
        p->nPgsz = sqlite3_column_int(pStmt, 0);
        rc = sqlite3_finalize(pStmt);
      }else if( rc==SQLITE_AUTH ){
        p->nPgsz = 1024;
        rc = SQLITE_OK;
      }
    }
    assert( p->nPgsz>0 || rc!=SQLITE_OK );
    sqlite3_free(zSql);
    *pRc = rc;
  }
}

/*
** "Special" FTS4 arguments are column specifications of the following form:
**
**   <key> = <value>
**
** There may not be whitespace surrounding the "=" character. The <value> 
** term may be quoted, but the <key> may not.
*/
static int fts3IsSpecialColumn(
  const char *z, 
  int *pnKey,
  char **pzValue
){
  char *zValue;
  const char *zCsr = z;

  while( *zCsr!='=' ){
    if( *zCsr=='\0' ) return 0;
    zCsr++;
  }

  *pnKey = (int)(zCsr-z);
  zValue = sqlite3_mprintf("%s", &zCsr[1]);
  if( zValue ){
    sqlite3Fts3Dequote(zValue);
  }
  *pzValue = zValue;
  return 1;
}

/*
** Append the output of a printf() style formatting to an existing string.
*/
static void fts3Appendf(
  int *pRc,                       /* IN/OUT: Error code */
  char **pz,                      /* IN/OUT: Pointer to string buffer */
  const char *zFormat,            /* Printf format string to append */
  ...                             /* Arguments for printf format string */
){
  if( *pRc==SQLITE_OK ){
    va_list ap;
    char *z;
    va_start(ap, zFormat);
    z = sqlite3_vmprintf(zFormat, ap);
    if( z && *pz ){
      char *z2 = sqlite3_mprintf("%s%s", *pz, z);
      sqlite3_free(z);
      z = z2;
    }
    if( z==0 ) *pRc = SQLITE_NOMEM;
    sqlite3_free(*pz);
    *pz = z;
  }
}

/*
** Return a copy of input string zInput enclosed in double-quotes (") and
** with all double quote characters escaped. For example:
**
**     fts3QuoteId("un \"zip\"")   ->    "un \"\"zip\"\""
**
** The pointer returned points to memory obtained from sqlite3_malloc(). It
** is the callers responsibility to call sqlite3_free() to release this
** memory.
*/
static char *fts3QuoteId(char const *zInput){
  int nRet;
  char *zRet;
  nRet = 2 + strlen(zInput)*2 + 1;
  zRet = sqlite3_malloc(nRet);
  if( zRet ){
    int i;
    char *z = zRet;
    *(z++) = '"';
    for(i=0; zInput[i]; i++){
      if( zInput[i]=='"' ) *(z++) = '"';
      *(z++) = zInput[i];
    }
    *(z++) = '"';
    *(z++) = '\0';
  }
  return zRet;
}

/*
** Return a list of comma separated SQL expressions and a FROM clause that 
** could be used in a SELECT statement such as the following:
**
**     SELECT <list of expressions> FROM %_content AS x ...
**
** to return the docid, followed by each column of text data in order
** from left to write. If parameter zFunc is not NULL, then instead of
** being returned directly each column of text data is passed to an SQL
** function named zFunc first. For example, if zFunc is "unzip" and the
** table has the three user-defined columns "a", "b", and "c", the following
** string is returned:
**
**     "docid, unzip(x.'a'), unzip(x.'b'), unzip(x.'c') FROM %_content AS x"
**
** The pointer returned points to a buffer allocated by sqlite3_malloc(). It
** is the responsibility of the caller to eventually free it.
**
** If *pRc is not SQLITE_OK when this function is called, it is a no-op (and
** a NULL pointer is returned). Otherwise, if an OOM error is encountered
** by this function, NULL is returned and *pRc is set to SQLITE_NOMEM. If
** no error occurs, *pRc is left unmodified.
*/
static char *fts3ReadExprList(Fts3Table *p, const char *zFunc, int *pRc){
  char *zRet = 0;
  char *zFree = 0;
  char *zFunction;
  int i;

  if( p->zContentTbl==0 ){
    if( !zFunc ){
      zFunction = "";
    }else{
      zFree = zFunction = fts3QuoteId(zFunc);
    }
    fts3Appendf(pRc, &zRet, "docid");
    for(i=0; i<p->nColumn; i++){
      fts3Appendf(pRc, &zRet, ",%s(x.'c%d%q')", zFunction, i, p->azColumn[i]);
    }
    sqlite3_free(zFree);
  }else{
    fts3Appendf(pRc, &zRet, "rowid");
    for(i=0; i<p->nColumn; i++){
      fts3Appendf(pRc, &zRet, ", x.'%q'", p->azColumn[i]);
    }
  }
  fts3Appendf(pRc, &zRet, "FROM '%q'.'%q%s' AS x", 
      p->zDb,
      (p->zContentTbl ? p->zContentTbl : p->zName),
      (p->zContentTbl ? "" : "_content")
  );
  return zRet;
}

/*
** Return a list of N comma separated question marks, where N is the number
** of columns in the %_content table (one for the docid plus one for each
** user-defined text column).
**
** If argument zFunc is not NULL, then all but the first question mark
** is preceded by zFunc and an open bracket, and followed by a closed
** bracket. For example, if zFunc is "zip" and the FTS3 table has three 
** user-defined text columns, the following string is returned:
**
**     "?, zip(?), zip(?), zip(?)"
**
** The pointer returned points to a buffer allocated by sqlite3_malloc(). It
** is the responsibility of the caller to eventually free it.
**
** If *pRc is not SQLITE_OK when this function is called, it is a no-op (and
** a NULL pointer is returned). Otherwise, if an OOM error is encountered
** by this function, NULL is returned and *pRc is set to SQLITE_NOMEM. If
** no error occurs, *pRc is left unmodified.
*/
static char *fts3WriteExprList(Fts3Table *p, const char *zFunc, int *pRc){
  char *zRet = 0;
  char *zFree = 0;
  char *zFunction;
  int i;

  if( !zFunc ){
    zFunction = "";
  }else{
    zFree = zFunction = fts3QuoteId(zFunc);
  }
  fts3Appendf(pRc, &zRet, "?");
  for(i=0; i<p->nColumn; i++){
    fts3Appendf(pRc, &zRet, ",%s(?)", zFunction);
  }
  sqlite3_free(zFree);
  return zRet;
}

/*
** This function interprets the string at (*pp) as a non-negative integer
** value. It reads the integer and sets *pnOut to the value read, then 
** sets *pp to point to the byte immediately following the last byte of
** the integer value.
**
** Only decimal digits ('0'..'9') may be part of an integer value. 
**
** If *pp does not being with a decimal digit SQLITE_ERROR is returned and
** the output value undefined. Otherwise SQLITE_OK is returned.
**
** This function is used when parsing the "prefix=" FTS4 parameter.
*/
static int fts3GobbleInt(const char **pp, int *pnOut){
  const char *p;                  /* Iterator pointer */
  int nInt = 0;                   /* Output value */

  for(p=*pp; p[0]>='0' && p[0]<='9'; p++){
    nInt = nInt * 10 + (p[0] - '0');
  }
  if( p==*pp ) return SQLITE_ERROR;
  *pnOut = nInt;
  *pp = p;
  return SQLITE_OK;
}

/*
** This function is called to allocate an array of Fts3Index structures
** representing the indexes maintained by the current FTS table. FTS tables
** always maintain the main "terms" index, but may also maintain one or
** more "prefix" indexes, depending on the value of the "prefix=" parameter
** (if any) specified as part of the CREATE VIRTUAL TABLE statement.
**
** Argument zParam is passed the value of the "prefix=" option if one was
** specified, or NULL otherwise.
**
** If no error occurs, SQLITE_OK is returned and *apIndex set to point to
** the allocated array. *pnIndex is set to the number of elements in the
** array. If an error does occur, an SQLite error code is returned.
**
** Regardless of whether or not an error is returned, it is the responsibility
** of the caller to call sqlite3_free() on the output array to free it.
*/
static int fts3PrefixParameter(
  const char *zParam,             /* ABC in prefix=ABC parameter to parse */
  int *pnIndex,                   /* OUT: size of *apIndex[] array */
  struct Fts3Index **apIndex      /* OUT: Array of indexes for this table */
){
  struct Fts3Index *aIndex;       /* Allocated array */
  int nIndex = 1;                 /* Number of entries in array */

  if( zParam && zParam[0] ){
    const char *p;
    nIndex++;
    for(p=zParam; *p; p++){
      if( *p==',' ) nIndex++;
    }
  }

  aIndex = sqlite3_malloc(sizeof(struct Fts3Index) * nIndex);
  *apIndex = aIndex;
  *pnIndex = nIndex;
  if( !aIndex ){
    return SQLITE_NOMEM;
  }

  memset(aIndex, 0, sizeof(struct Fts3Index) * nIndex);
  if( zParam ){
    const char *p = zParam;
    int i;
    for(i=1; i<nIndex; i++){
      int nPrefix;
      if( fts3GobbleInt(&p, &nPrefix) ) return SQLITE_ERROR;
      aIndex[i].nPrefix = nPrefix;
      p++;
    }
  }

  return SQLITE_OK;
}

/*
** This function is called when initializing an FTS4 table that uses the
** content=xxx option. It determines the number of and names of the columns
** of the new FTS4 table.
**
** The third argument passed to this function is the value passed to the
** config=xxx option (i.e. "xxx"). This function queries the database for
** a table of that name. If found, the output variables are populated
** as follows:
**
**   *pnCol:   Set to the number of columns table xxx has,
**
**   *pnStr:   Set to the total amount of space required to store a copy
**             of each columns name, including the nul-terminator.
**
**   *pazCol:  Set to point to an array of *pnCol strings. Each string is
**             the name of the corresponding column in table xxx. The array
**             and its contents are allocated using a single allocation. It
**             is the responsibility of the caller to free this allocation
**             by eventually passing the *pazCol value to sqlite3_free().
**
** If the table cannot be found, an error code is returned and the output
** variables are undefined. Or, if an OOM is encountered, SQLITE_NOMEM is
** returned (and the output variables are undefined).
*/
static int fts3ContentColumns(
  sqlite3 *db,                    /* Database handle */
  const char *zDb,                /* Name of db (i.e. "main", "temp" etc.) */
  const char *zTbl,               /* Name of content table */
  const char ***pazCol,           /* OUT: Malloc'd array of column names */
  int *pnCol,                     /* OUT: Size of array *pazCol */
  int *pnStr                      /* OUT: Bytes of string content */
){
  int rc = SQLITE_OK;             /* Return code */
  char *zSql;                     /* "SELECT *" statement on zTbl */  
  sqlite3_stmt *pStmt = 0;        /* Compiled version of zSql */

  zSql = sqlite3_mprintf("SELECT * FROM %Q.%Q", zDb, zTbl);
  if( !zSql ){
    rc = SQLITE_NOMEM;
  }else{
    rc = sqlite3_prepare(db, zSql, -1, &pStmt, 0);
  }
  sqlite3_free(zSql);

  if( rc==SQLITE_OK ){
    const char **azCol;           /* Output array */
    int nStr = 0;                 /* Size of all column names (incl. 0x00) */
    int nCol;                     /* Number of table columns */
    int i;                        /* Used to iterate through columns */

    /* Loop through the returned columns. Set nStr to the number of bytes of
    ** space required to store a copy of each column name, including the
    ** nul-terminator byte.  */
    nCol = sqlite3_column_count(pStmt);
    for(i=0; i<nCol; i++){
      const char *zCol = sqlite3_column_name(pStmt, i);
      nStr += strlen(zCol) + 1;
    }

    /* Allocate and populate the array to return. */
    azCol = (const char **)sqlite3_malloc(sizeof(char *) * nCol + nStr);
    if( azCol==0 ){
      rc = SQLITE_NOMEM;
    }else{
      char *p = (char *)&azCol[nCol];
      for(i=0; i<nCol; i++){
        const char *zCol = sqlite3_column_name(pStmt, i);
        int n = strlen(zCol)+1;
        memcpy(p, zCol, n);
        azCol[i] = p;
        p += n;
      }
    }
    sqlite3_finalize(pStmt);

    /* Set the output variables. */
    *pnCol = nCol;
    *pnStr = nStr;
    *pazCol = azCol;
  }

  return rc;
}

/*
** This function is the implementation of both the xConnect and xCreate
** methods of the FTS3 virtual table.
**
** The argv[] array contains the following:
**
**   argv[0]   -> module name  ("fts3" or "fts4")
**   argv[1]   -> database name
**   argv[2]   -> table name
**   argv[...] -> "column name" and other module argument fields.
*/
static int fts3InitVtab(
  int isCreate,                   /* True for xCreate, false for xConnect */
  sqlite3 *db,                    /* The SQLite database connection */
  void *pAux,                     /* Hash table containing tokenizers */
  int argc,                       /* Number of elements in argv array */
  const char * const *argv,       /* xCreate/xConnect argument array */
  sqlite3_vtab **ppVTab,          /* Write the resulting vtab structure here */
  char **pzErr                    /* Write any error message here */
){
  Fts3Hash *pHash = (Fts3Hash *)pAux;
  Fts3Table *p = 0;               /* Pointer to allocated vtab */
  int rc = SQLITE_OK;             /* Return code */
  int i;                          /* Iterator variable */
  int nByte;                      /* Size of allocation used for *p */
  int iCol;                       /* Column index */
  int nString = 0;                /* Bytes required to hold all column names */
  int nCol = 0;                   /* Number of columns in the FTS table */
  char *zCsr;                     /* Space for holding column names */
  int nDb;                        /* Bytes required to hold database name */
  int nName;                      /* Bytes required to hold table name */
  int isFts4 = (argv[0][3]=='4'); /* True for FTS4, false for FTS3 */
  const char **aCol;              /* Array of column names */
  sqlite3_tokenizer *pTokenizer = 0;        /* Tokenizer for this table */

  int nIndex;                     /* Size of aIndex[] array */
  struct Fts3Index *aIndex = 0;   /* Array of indexes for this table */

  /* The results of parsing supported FTS4 key=value options: */
  int bNoDocsize = 0;             /* True to omit %_docsize table */
  int bDescIdx = 0;               /* True to store descending indexes */
  char *zPrefix = 0;              /* Prefix parameter value (or NULL) */
  char *zCompress = 0;            /* compress=? parameter (or NULL) */
  char *zUncompress = 0;          /* uncompress=? parameter (or NULL) */
  char *zContent = 0;             /* content=? parameter (or NULL) */

  assert( strlen(argv[0])==4 );
  assert( (sqlite3_strnicmp(argv[0], "fts4", 4)==0 && isFts4)
       || (sqlite3_strnicmp(argv[0], "fts3", 4)==0 && !isFts4)
  );

  nDb = (int)strlen(argv[1]) + 1;
  nName = (int)strlen(argv[2]) + 1;

  aCol = (const char **)sqlite3_malloc(sizeof(const char *) * (argc-2) );
  if( !aCol ) return SQLITE_NOMEM;
  memset((void *)aCol, 0, sizeof(const char *) * (argc-2));

  /* Loop through all of the arguments passed by the user to the FTS3/4
  ** module (i.e. all the column names and special arguments). This loop
  ** does the following:
  **
  **   + Figures out the number of columns the FTSX table will have, and
  **     the number of bytes of space that must be allocated to store copies
  **     of the column names.
  **
  **   + If there is a tokenizer specification included in the arguments,
  **     initializes the tokenizer pTokenizer.
  */
  for(i=3; rc==SQLITE_OK && i<argc; i++){
    char const *z = argv[i];
    int nKey;
    char *zVal;

    /* Check if this is a tokenizer specification */
    if( !pTokenizer 
     && strlen(z)>8
     && 0==sqlite3_strnicmp(z, "tokenize", 8) 
     && 0==sqlite3Fts3IsIdChar(z[8])
    ){
      rc = sqlite3Fts3InitTokenizer(pHash, &z[9], &pTokenizer, pzErr);
    }

    /* Check if it is an FTS4 special argument. */
    else if( isFts4 && fts3IsSpecialColumn(z, &nKey, &zVal) ){
      struct Fts4Option {
        const char *zOpt;
        int nOpt;
      } aFts4Opt[] = {
        { "matchinfo",   9 },     /* 0 -> MATCHINFO */
        { "prefix",      6 },     /* 1 -> PREFIX */
        { "compress",    8 },     /* 2 -> COMPRESS */
        { "uncompress", 10 },     /* 3 -> UNCOMPRESS */
        { "order",       5 },     /* 4 -> ORDER */
        { "content",     7 }      /* 5 -> CONTENT */
      };

      int iOpt;
      if( !zVal ){
        rc = SQLITE_NOMEM;
      }else{
        for(iOpt=0; iOpt<SizeofArray(aFts4Opt); iOpt++){
          struct Fts4Option *pOp = &aFts4Opt[iOpt];
          if( nKey==pOp->nOpt && !sqlite3_strnicmp(z, pOp->zOpt, pOp->nOpt) ){
            break;
          }
        }
        if( iOpt==SizeofArray(aFts4Opt) ){
          *pzErr = sqlite3_mprintf("unrecognized parameter: %s", z);
          rc = SQLITE_ERROR;
        }else{
          switch( iOpt ){
            case 0:               /* MATCHINFO */
              if( strlen(zVal)!=4 || sqlite3_strnicmp(zVal, "fts3", 4) ){
                *pzErr = sqlite3_mprintf("unrecognized matchinfo: %s", zVal);
                rc = SQLITE_ERROR;
              }
              bNoDocsize = 1;
              break;

            case 1:               /* PREFIX */
              sqlite3_free(zPrefix);
              zPrefix = zVal;
              zVal = 0;
              break;

            case 2:               /* COMPRESS */
              sqlite3_free(zCompress);
              zCompress = zVal;
              zVal = 0;
              break;

            case 3:               /* UNCOMPRESS */
              sqlite3_free(zUncompress);
              zUncompress = zVal;
              zVal = 0;
              break;

            case 4:               /* ORDER */
              if( (strlen(zVal)!=3 || sqlite3_strnicmp(zVal, "asc", 3)) 
               && (strlen(zVal)!=4 || sqlite3_strnicmp(zVal, "desc", 4)) 
              ){
                *pzErr = sqlite3_mprintf("unrecognized order: %s", zVal);
                rc = SQLITE_ERROR;
              }
              bDescIdx = (zVal[0]=='d' || zVal[0]=='D');
              break;

            default:              /* CONTENT */
              assert( iOpt==5 );
              sqlite3_free(zUncompress);
              zContent = zVal;
              zVal = 0;
              break;
          }
        }
        sqlite3_free(zVal);
      }
    }

    /* Otherwise, the argument is a column name. */
    else {
      nString += (int)(strlen(z) + 1);
      aCol[nCol++] = z;
    }
  }

  /* If a content=xxx option was specified, the following:
  **
  **   1. Ignore any compress= and uncompress= options.
  **
  **   2. If no column names were specified as part of the CREATE VIRTUAL
  **      TABLE statement, use all columns from the content table.
  */
  if( rc==SQLITE_OK && zContent ){
    sqlite3_free(zCompress); 
    sqlite3_free(zUncompress); 
    zCompress = 0;
    zUncompress = 0;
    if( nCol==0 ){
      sqlite3_free((void*)aCol); 
      aCol = 0;
      rc = fts3ContentColumns(db, argv[1], zContent, &aCol, &nCol, &nString);
    }
    assert( rc!=SQLITE_OK || nCol>0 );
  }
  if( rc!=SQLITE_OK ) goto fts3_init_out;

  if( nCol==0 ){
    assert( nString==0 );
    aCol[0] = "content";
    nString = 8;
    nCol = 1;
  }

  if( pTokenizer==0 ){
    rc = sqlite3Fts3InitTokenizer(pHash, "simple", &pTokenizer, pzErr);
    if( rc!=SQLITE_OK ) goto fts3_init_out;
  }
  assert( pTokenizer );

  rc = fts3PrefixParameter(zPrefix, &nIndex, &aIndex);
  if( rc==SQLITE_ERROR ){
    assert( zPrefix );
    *pzErr = sqlite3_mprintf("error parsing prefix parameter: %s", zPrefix);
  }
  if( rc!=SQLITE_OK ) goto fts3_init_out;

  /* Allocate and populate the Fts3Table structure. */
  nByte = sizeof(Fts3Table) +                  /* Fts3Table */
          nCol * sizeof(char *) +              /* azColumn */
          nIndex * sizeof(struct Fts3Index) +  /* aIndex */
          nName +                              /* zName */
          nDb +                                /* zDb */
          nString;                             /* Space for azColumn strings */
  p = (Fts3Table*)sqlite3_malloc(nByte);
  if( p==0 ){
    rc = SQLITE_NOMEM;
    goto fts3_init_out;
  }
  memset(p, 0, nByte);
  p->db = db;
  p->nColumn = nCol;
  p->nPendingData = 0;
  p->azColumn = (char **)&p[1];
  p->pTokenizer = pTokenizer;
  p->nMaxPendingData = FTS3_MAX_PENDING_DATA;
  p->bHasDocsize = (isFts4 && bNoDocsize==0);
  p->bHasStat = isFts4;
  p->bDescIdx = bDescIdx;
  p->zContentTbl = zContent;
  zContent = 0;
  TESTONLY( p->inTransaction = -1 );
  TESTONLY( p->mxSavepoint = -1 );

  p->aIndex = (struct Fts3Index *)&p->azColumn[nCol];
  memcpy(p->aIndex, aIndex, sizeof(struct Fts3Index) * nIndex);
  p->nIndex = nIndex;
  for(i=0; i<nIndex; i++){
    fts3HashInit(&p->aIndex[i].hPending, FTS3_HASH_STRING, 1);
  }

  /* Fill in the zName and zDb fields of the vtab structure. */
  zCsr = (char *)&p->aIndex[nIndex];
  p->zName = zCsr;
  memcpy(zCsr, argv[2], nName);
  zCsr += nName;
  p->zDb = zCsr;
  memcpy(zCsr, argv[1], nDb);
  zCsr += nDb;

  /* Fill in the azColumn array */
  for(iCol=0; iCol<nCol; iCol++){
    char *z; 
    int n = 0;
    z = (char *)sqlite3Fts3NextToken(aCol[iCol], &n);
    memcpy(zCsr, z, n);
    zCsr[n] = '\0';
    sqlite3Fts3Dequote(zCsr);
    p->azColumn[iCol] = zCsr;
    zCsr += n+1;
    assert( zCsr <= &((char *)p)[nByte] );
  }

  if( (zCompress==0)!=(zUncompress==0) ){
    char const *zMiss = (zCompress==0 ? "compress" : "uncompress");
    rc = SQLITE_ERROR;
    *pzErr = sqlite3_mprintf("missing %s parameter in fts4 constructor", zMiss);
  }
  p->zReadExprlist = fts3ReadExprList(p, zUncompress, &rc);
  p->zWriteExprlist = fts3WriteExprList(p, zCompress, &rc);
  if( rc!=SQLITE_OK ) goto fts3_init_out;

  /* If this is an xCreate call, create the underlying tables in the 
  ** database. TODO: For xConnect(), it could verify that said tables exist.
  */
  if( isCreate ){
    rc = fts3CreateTables(p);
  }

  /* Figure out the page-size for the database. This is required in order to
  ** estimate the cost of loading large doclists from the database.  */
  fts3DatabasePageSize(&rc, p);
  p->nNodeSize = p->nPgsz-35;

  /* Declare the table schema to SQLite. */
  fts3DeclareVtab(&rc, p);

fts3_init_out:
  sqlite3_free(zPrefix);
  sqlite3_free(aIndex);
  sqlite3_free(zCompress);
  sqlite3_free(zUncompress);
  sqlite3_free(zContent);
  sqlite3_free((void *)aCol);
  if( rc!=SQLITE_OK ){
    if( p ){
      fts3DisconnectMethod((sqlite3_vtab *)p);
    }else if( pTokenizer ){
      pTokenizer->pModule->xDestroy(pTokenizer);
    }
  }else{
    assert( p->pSegments==0 );
    *ppVTab = &p->base;
  }
  return rc;
}

/*
** The xConnect() and xCreate() methods for the virtual table. All the
** work is done in function fts3InitVtab().
*/
static int fts3ConnectMethod(
  sqlite3 *db,                    /* Database connection */
  void *pAux,                     /* Pointer to tokenizer hash table */
  int argc,                       /* Number of elements in argv array */
  const char * const *argv,       /* xCreate/xConnect argument array */
  sqlite3_vtab **ppVtab,          /* OUT: New sqlite3_vtab object */
  char **pzErr                    /* OUT: sqlite3_malloc'd error message */
){
  return fts3InitVtab(0, db, pAux, argc, argv, ppVtab, pzErr);
}
static int fts3CreateMethod(
  sqlite3 *db,                    /* Database connection */
  void *pAux,                     /* Pointer to tokenizer hash table */
  int argc,                       /* Number of elements in argv array */
  const char * const *argv,       /* xCreate/xConnect argument array */
  sqlite3_vtab **ppVtab,          /* OUT: New sqlite3_vtab object */
  char **pzErr                    /* OUT: sqlite3_malloc'd error message */
){
  return fts3InitVtab(1, db, pAux, argc, argv, ppVtab, pzErr);
}

/* 
** Implementation of the xBestIndex method for FTS3 tables. There
** are three possible strategies, in order of preference:
**
**   1. Direct lookup by rowid or docid. 
**   2. Full-text search using a MATCH operator on a non-docid column.
**   3. Linear scan of %_content table.
*/
static int fts3BestIndexMethod(sqlite3_vtab *pVTab, sqlite3_index_info *pInfo){
  Fts3Table *p = (Fts3Table *)pVTab;
  int i;                          /* Iterator variable */
  int iCons = -1;                 /* Index of constraint to use */

  /* By default use a full table scan. This is an expensive option,
  ** so search through the constraints to see if a more efficient 
  ** strategy is possible.
  */
  pInfo->idxNum = FTS3_FULLSCAN_SEARCH;
  pInfo->estimatedCost = 500000;
  for(i=0; i<pInfo->nConstraint; i++){
    struct sqlite3_index_constraint *pCons = &pInfo->aConstraint[i];
    if( pCons->usable==0 ) continue;

    /* A direct lookup on the rowid or docid column. Assign a cost of 1.0. */
    if( pCons->op==SQLITE_INDEX_CONSTRAINT_EQ 
     && (pCons->iColumn<0 || pCons->iColumn==p->nColumn+1 )
    ){
      pInfo->idxNum = FTS3_DOCID_SEARCH;
      pInfo->estimatedCost = 1.0;
      iCons = i;
    }

    /* A MATCH constraint. Use a full-text search.
    **
    ** If there is more than one MATCH constraint available, use the first
    ** one encountered. If there is both a MATCH constraint and a direct
    ** rowid/docid lookup, prefer the MATCH strategy. This is done even 
    ** though the rowid/docid lookup is faster than a MATCH query, selecting
    ** it would lead to an "unable to use function MATCH in the requested 
    ** context" error.
    */
    if( pCons->op==SQLITE_INDEX_CONSTRAINT_MATCH 
     && pCons->iColumn>=0 && pCons->iColumn<=p->nColumn
    ){
      pInfo->idxNum = FTS3_FULLTEXT_SEARCH + pCons->iColumn;
      pInfo->estimatedCost = 2.0;
      iCons = i;
      break;
    }
  }

  if( iCons>=0 ){
    pInfo->aConstraintUsage[iCons].argvIndex = 1;
    pInfo->aConstraintUsage[iCons].omit = 1;
  } 

  /* Regardless of the strategy selected, FTS can deliver rows in rowid (or
  ** docid) order. Both ascending and descending are possible. 
  */
  if( pInfo->nOrderBy==1 ){
    struct sqlite3_index_orderby *pOrder = &pInfo->aOrderBy[0];
    if( pOrder->iColumn<0 || pOrder->iColumn==p->nColumn+1 ){
      if( pOrder->desc ){
        pInfo->idxStr = "DESC";
      }else{
        pInfo->idxStr = "ASC";
      }
      pInfo->orderByConsumed = 1;
    }
  }

  assert( p->pSegments==0 );
  return SQLITE_OK;
}

/*
** Implementation of xOpen method.
*/
static int fts3OpenMethod(sqlite3_vtab *pVTab, sqlite3_vtab_cursor **ppCsr){
  sqlite3_vtab_cursor *pCsr;               /* Allocated cursor */

  UNUSED_PARAMETER(pVTab);

  /* Allocate a buffer large enough for an Fts3Cursor structure. If the
  ** allocation succeeds, zero it and return SQLITE_OK. Otherwise, 
  ** if the allocation fails, return SQLITE_NOMEM.
  */
  *ppCsr = pCsr = (sqlite3_vtab_cursor *)sqlite3_malloc(sizeof(Fts3Cursor));
  if( !pCsr ){
    return SQLITE_NOMEM;
  }
  memset(pCsr, 0, sizeof(Fts3Cursor));
  return SQLITE_OK;
}

/*
** Close the cursor.  For additional information see the documentation
** on the xClose method of the virtual table interface.
*/
static int fts3CloseMethod(sqlite3_vtab_cursor *pCursor){
  Fts3Cursor *pCsr = (Fts3Cursor *)pCursor;
  assert( ((Fts3Table *)pCsr->base.pVtab)->pSegments==0 );
  sqlite3_finalize(pCsr->pStmt);
  sqlite3Fts3ExprFree(pCsr->pExpr);
  sqlite3Fts3FreeDeferredTokens(pCsr);
  sqlite3_free(pCsr->aDoclist);
  sqlite3_free(pCsr->aMatchinfo);
  assert( ((Fts3Table *)pCsr->base.pVtab)->pSegments==0 );
  sqlite3_free(pCsr);
  return SQLITE_OK;
}

/*
** If pCsr->pStmt has not been prepared (i.e. if pCsr->pStmt==0), then
** compose and prepare an SQL statement of the form:
**
**    "SELECT <columns> FROM %_content WHERE rowid = ?"
**
** (or the equivalent for a content=xxx table) and set pCsr->pStmt to
** it. If an error occurs, return an SQLite error code.
**
** Otherwise, set *ppStmt to point to pCsr->pStmt and return SQLITE_OK.
*/
static int fts3CursorSeekStmt(Fts3Cursor *pCsr, sqlite3_stmt **ppStmt){
  int rc = SQLITE_OK;
  if( pCsr->pStmt==0 ){
    Fts3Table *p = (Fts3Table *)pCsr->base.pVtab;
    char *zSql;
    zSql = sqlite3_mprintf("SELECT %s WHERE rowid = ?", p->zReadExprlist);
    if( !zSql ) return SQLITE_NOMEM;
    rc = sqlite3_prepare_v2(p->db, zSql, -1, &pCsr->pStmt, 0);
    sqlite3_free(zSql);
  }
  *ppStmt = pCsr->pStmt;
  return rc;
}

/*
** Position the pCsr->pStmt statement so that it is on the row
** of the %_content table that contains the last match.  Return
** SQLITE_OK on success.  
*/
static int fts3CursorSeek(sqlite3_context *pContext, Fts3Cursor *pCsr){
  int rc = SQLITE_OK;
  if( pCsr->isRequireSeek ){
    sqlite3_stmt *pStmt = 0;

    rc = fts3CursorSeekStmt(pCsr, &pStmt);
    if( rc==SQLITE_OK ){
      sqlite3_bind_int64(pCsr->pStmt, 1, pCsr->iPrevId);
      pCsr->isRequireSeek = 0;
      if( SQLITE_ROW==sqlite3_step(pCsr->pStmt) ){
        return SQLITE_OK;
      }else{
        rc = sqlite3_reset(pCsr->pStmt);
        if( rc==SQLITE_OK && ((Fts3Table *)pCsr->base.pVtab)->zContentTbl==0 ){
          /* If no row was found and no error has occured, then the %_content
          ** table is missing a row that is present in the full-text index.
          ** The data structures are corrupt.  */
          rc = FTS_CORRUPT_VTAB;
          pCsr->isEof = 1;
        }
      }
    }
  }

  if( rc!=SQLITE_OK && pContext ){
    sqlite3_result_error_code(pContext, rc);
  }
  return rc;
}

/*
** This function is used to process a single interior node when searching
** a b-tree for a term or term prefix. The node data is passed to this 
** function via the zNode/nNode parameters. The term to search for is
** passed in zTerm/nTerm.
**
** If piFirst is not NULL, then this function sets *piFirst to the blockid
** of the child node that heads the sub-tree that may contain the term.
**
** If piLast is not NULL, then *piLast is set to the right-most child node
** that heads a sub-tree that may contain a term for which zTerm/nTerm is
** a prefix.
**
** If an OOM error occurs, SQLITE_NOMEM is returned. Otherwise, SQLITE_OK.
*/
static int fts3ScanInteriorNode(
  const char *zTerm,              /* Term to select leaves for */
  int nTerm,                      /* Size of term zTerm in bytes */
  const char *zNode,              /* Buffer containing segment interior node */
  int nNode,                      /* Size of buffer at zNode */
  sqlite3_int64 *piFirst,         /* OUT: Selected child node */
  sqlite3_int64 *piLast           /* OUT: Selected child node */
){
  int rc = SQLITE_OK;             /* Return code */
  const char *zCsr = zNode;       /* Cursor to iterate through node */
  const char *zEnd = &zCsr[nNode];/* End of interior node buffer */
  char *zBuffer = 0;              /* Buffer to load terms into */
  int nAlloc = 0;                 /* Size of allocated buffer */
  int isFirstTerm = 1;            /* True when processing first term on page */
  sqlite3_int64 iChild;           /* Block id of child node to descend to */

  /* Skip over the 'height' varint that occurs at the start of every 
  ** interior node. Then load the blockid of the left-child of the b-tree
  ** node into variable iChild.  
  **
  ** Even if the data structure on disk is corrupted, this (reading two
  ** varints from the buffer) does not risk an overread. If zNode is a
  ** root node, then the buffer comes from a SELECT statement. SQLite does
  ** not make this guarantee explicitly, but in practice there are always
  ** either more than 20 bytes of allocated space following the nNode bytes of
  ** contents, or two zero bytes. Or, if the node is read from the %_segments
  ** table, then there are always 20 bytes of zeroed padding following the
  ** nNode bytes of content (see sqlite3Fts3ReadBlock() for details).
  */
  zCsr += sqlite3Fts3GetVarint(zCsr, &iChild);
  zCsr += sqlite3Fts3GetVarint(zCsr, &iChild);
  if( zCsr>zEnd ){
    return FTS_CORRUPT_VTAB;
  }
  
  while( zCsr<zEnd && (piFirst || piLast) ){
    int cmp;                      /* memcmp() result */
    int nSuffix;                  /* Size of term suffix */
    int nPrefix = 0;              /* Size of term prefix */
    int nBuffer;                  /* Total term size */
  
    /* Load the next term on the node into zBuffer. Use realloc() to expand
    ** the size of zBuffer if required.  */
    if( !isFirstTerm ){
      zCsr += sqlite3Fts3GetVarint32(zCsr, &nPrefix);
    }
    isFirstTerm = 0;
    zCsr += sqlite3Fts3GetVarint32(zCsr, &nSuffix);
    
    if( nPrefix<0 || nSuffix<0 || &zCsr[nSuffix]>zEnd ){
      rc = FTS_CORRUPT_VTAB;
      goto finish_scan;
    }
    if( nPrefix+nSuffix>nAlloc ){
      char *zNew;
      nAlloc = (nPrefix+nSuffix) * 2;
      zNew = (char *)sqlite3_realloc(zBuffer, nAlloc);
      if( !zNew ){
        rc = SQLITE_NOMEM;
        goto finish_scan;
      }
      zBuffer = zNew;
    }
    assert( zBuffer );
    memcpy(&zBuffer[nPrefix], zCsr, nSuffix);
    nBuffer = nPrefix + nSuffix;
    zCsr += nSuffix;

    /* Compare the term we are searching for with the term just loaded from
    ** the interior node. If the specified term is greater than or equal
    ** to the term from the interior node, then all terms on the sub-tree 
    ** headed by node iChild are smaller than zTerm. No need to search 
    ** iChild.
    **
    ** If the interior node term is larger than the specified term, then
    ** the tree headed by iChild may contain the specified term.
    */
    cmp = memcmp(zTerm, zBuffer, (nBuffer>nTerm ? nTerm : nBuffer));
    if( piFirst && (cmp<0 || (cmp==0 && nBuffer>nTerm)) ){
      *piFirst = iChild;
      piFirst = 0;
    }

    if( piLast && cmp<0 ){
      *piLast = iChild;
      piLast = 0;
    }

    iChild++;
  };

  if( piFirst ) *piFirst = iChild;
  if( piLast ) *piLast = iChild;

 finish_scan:
  sqlite3_free(zBuffer);
  return rc;
}


/*
** The buffer pointed to by argument zNode (size nNode bytes) contains an
** interior node of a b-tree segment. The zTerm buffer (size nTerm bytes)
** contains a term. This function searches the sub-tree headed by the zNode
** node for the range of leaf nodes that may contain the specified term
** or terms for which the specified term is a prefix.
**
** If piLeaf is not NULL, then *piLeaf is set to the blockid of the 
** left-most leaf node in the tree that may contain the specified term.
** If piLeaf2 is not NULL, then *piLeaf2 is set to the blockid of the
** right-most leaf node that may contain a term for which the specified
** term is a prefix.
**
** It is possible that the range of returned leaf nodes does not contain 
** the specified term or any terms for which it is a prefix. However, if the 
** segment does contain any such terms, they are stored within the identified
** range. Because this function only inspects interior segment nodes (and
** never loads leaf nodes into memory), it is not possible to be sure.
**
** If an error occurs, an error code other than SQLITE_OK is returned.
*/ 
static int fts3SelectLeaf(
  Fts3Table *p,                   /* Virtual table handle */
  const char *zTerm,              /* Term to select leaves for */
  int nTerm,                      /* Size of term zTerm in bytes */
  const char *zNode,              /* Buffer containing segment interior node */
  int nNode,                      /* Size of buffer at zNode */
  sqlite3_int64 *piLeaf,          /* Selected leaf node */
  sqlite3_int64 *piLeaf2          /* Selected leaf node */
){
  int rc;                         /* Return code */
  int iHeight;                    /* Height of this node in tree */

  assert( piLeaf || piLeaf2 );

  sqlite3Fts3GetVarint32(zNode, &iHeight);
  rc = fts3ScanInteriorNode(zTerm, nTerm, zNode, nNode, piLeaf, piLeaf2);
  assert( !piLeaf2 || !piLeaf || rc!=SQLITE_OK || (*piLeaf<=*piLeaf2) );

  if( rc==SQLITE_OK && iHeight>1 ){
    char *zBlob = 0;              /* Blob read from %_segments table */
    int nBlob;                    /* Size of zBlob in bytes */

    if( piLeaf && piLeaf2 && (*piLeaf!=*piLeaf2) ){
      rc = sqlite3Fts3ReadBlock(p, *piLeaf, &zBlob, &nBlob, 0);
      if( rc==SQLITE_OK ){
        rc = fts3SelectLeaf(p, zTerm, nTerm, zBlob, nBlob, piLeaf, 0);
      }
      sqlite3_free(zBlob);
      piLeaf = 0;
      zBlob = 0;
    }

    if( rc==SQLITE_OK ){
      rc = sqlite3Fts3ReadBlock(p, piLeaf?*piLeaf:*piLeaf2, &zBlob, &nBlob, 0);
    }
    if( rc==SQLITE_OK ){
      rc = fts3SelectLeaf(p, zTerm, nTerm, zBlob, nBlob, piLeaf, piLeaf2);
    }
    sqlite3_free(zBlob);
  }

  return rc;
}

/*
** This function is used to create delta-encoded serialized lists of FTS3 
** varints. Each call to this function appends a single varint to a list.
*/
static void fts3PutDeltaVarint(
  char **pp,                      /* IN/OUT: Output pointer */
  sqlite3_int64 *piPrev,          /* IN/OUT: Previous value written to list */
  sqlite3_int64 iVal              /* Write this value to the list */
){
  assert( iVal-*piPrev > 0 || (*piPrev==0 && iVal==0) );
  *pp += sqlite3Fts3PutVarint(*pp, iVal-*piPrev);
  *piPrev = iVal;
}

/*
** When this function is called, *ppPoslist is assumed to point to the 
** start of a position-list. After it returns, *ppPoslist points to the
** first byte after the position-list.
**
** A position list is list of positions (delta encoded) and columns for 
** a single document record of a doclist.  So, in other words, this
** routine advances *ppPoslist so that it points to the next docid in
** the doclist, or to the first byte past the end of the doclist.
**
** If pp is not NULL, then the contents of the position list are copied
** to *pp. *pp is set to point to the first byte past the last byte copied
** before this function returns.
*/
static void fts3PoslistCopy(char **pp, char **ppPoslist){
  char *pEnd = *ppPoslist;
  char c = 0;

  /* The end of a position list is marked by a zero encoded as an FTS3 
  ** varint. A single POS_END (0) byte. Except, if the 0 byte is preceded by
  ** a byte with the 0x80 bit set, then it is not a varint 0, but the tail
  ** of some other, multi-byte, value.
  **
  ** The following while-loop moves pEnd to point to the first byte that is not 
  ** immediately preceded by a byte with the 0x80 bit set. Then increments
  ** pEnd once more so that it points to the byte immediately following the
  ** last byte in the position-list.
  */
  while( *pEnd | c ){
    c = *pEnd++ & 0x80;
    testcase( c!=0 && (*pEnd)==0 );
  }
  pEnd++;  /* Advance past the POS_END terminator byte */

  if( pp ){
    int n = (int)(pEnd - *ppPoslist);
    char *p = *pp;
    memcpy(p, *ppPoslist, n);
    p += n;
    *pp = p;
  }
  *ppPoslist = pEnd;
}

/*
** When this function is called, *ppPoslist is assumed to point to the 
** start of a column-list. After it returns, *ppPoslist points to the
** to the terminator (POS_COLUMN or POS_END) byte of the column-list.
**
** A column-list is list of delta-encoded positions for a single column
** within a single document within a doclist.
**
** The column-list is terminated either by a POS_COLUMN varint (1) or
** a POS_END varint (0).  This routine leaves *ppPoslist pointing to
** the POS_COLUMN or POS_END that terminates the column-list.
**
** If pp is not NULL, then the contents of the column-list are copied
** to *pp. *pp is set to point to the first byte past the last byte copied
** before this function returns.  The POS_COLUMN or POS_END terminator
** is not copied into *pp.
*/
static void fts3ColumnlistCopy(char **pp, char **ppPoslist){
  char *pEnd = *ppPoslist;
  char c = 0;

  /* A column-list is terminated by either a 0x01 or 0x00 byte that is
  ** not part of a multi-byte varint.
  */
  while( 0xFE & (*pEnd | c) ){
    c = *pEnd++ & 0x80;
    testcase( c!=0 && ((*pEnd)&0xfe)==0 );
  }
  if( pp ){
    int n = (int)(pEnd - *ppPoslist);
    char *p = *pp;
    memcpy(p, *ppPoslist, n);
    p += n;
    *pp = p;
  }
  *ppPoslist = pEnd;
}

/*
** Value used to signify the end of an position-list. This is safe because
** it is not possible to have a document with 2^31 terms.
*/
#define POSITION_LIST_END 0x7fffffff

/*
** This function is used to help parse position-lists. When this function is
** called, *pp may point to the start of the next varint in the position-list
** being parsed, or it may point to 1 byte past the end of the position-list
** (in which case **pp will be a terminator bytes POS_END (0) or
** (1)).
**
** If *pp points past the end of the current position-list, set *pi to 
** POSITION_LIST_END and return. Otherwise, read the next varint from *pp,
** increment the current value of *pi by the value read, and set *pp to
** point to the next value before returning.
**
** Before calling this routine *pi must be initialized to the value of
** the previous position, or zero if we are reading the first position
** in the position-list.  Because positions are delta-encoded, the value
** of the previous position is needed in order to compute the value of
** the next position.
*/
static void fts3ReadNextPos(
  char **pp,                    /* IN/OUT: Pointer into position-list buffer */
  sqlite3_int64 *pi             /* IN/OUT: Value read from position-list */
){
  if( (**pp)&0xFE ){
    fts3GetDeltaVarint(pp, pi);
    *pi -= 2;
  }else{
    *pi = POSITION_LIST_END;
  }
}

/*
** If parameter iCol is not 0, write an POS_COLUMN (1) byte followed by
** the value of iCol encoded as a varint to *pp.   This will start a new
** column list.
**
** Set *pp to point to the byte just after the last byte written before 
** returning (do not modify it if iCol==0). Return the total number of bytes
** written (0 if iCol==0).
*/
static int fts3PutColNumber(char **pp, int iCol){
  int n = 0;                      /* Number of bytes written */
  if( iCol ){
    char *p = *pp;                /* Output pointer */
    n = 1 + sqlite3Fts3PutVarint(&p[1], iCol);
    *p = 0x01;
    *pp = &p[n];
  }
  return n;
}

/*
** Compute the union of two position lists.  The output written
** into *pp contains all positions of both *pp1 and *pp2 in sorted
** order and with any duplicates removed.  All pointers are
** updated appropriately.   The caller is responsible for insuring
** that there is enough space in *pp to hold the complete output.
*/
static void fts3PoslistMerge(
  char **pp,                      /* Output buffer */
  char **pp1,                     /* Left input list */
  char **pp2                      /* Right input list */
){
  char *p = *pp;
  char *p1 = *pp1;
  char *p2 = *pp2;

  while( *p1 || *p2 ){
    int iCol1;         /* The current column index in pp1 */
    int iCol2;         /* The current column index in pp2 */

    if( *p1==POS_COLUMN ) sqlite3Fts3GetVarint32(&p1[1], &iCol1);
    else if( *p1==POS_END ) iCol1 = POSITION_LIST_END;
    else iCol1 = 0;

    if( *p2==POS_COLUMN ) sqlite3Fts3GetVarint32(&p2[1], &iCol2);
    else if( *p2==POS_END ) iCol2 = POSITION_LIST_END;
    else iCol2 = 0;

    if( iCol1==iCol2 ){
      sqlite3_int64 i1 = 0;       /* Last position from pp1 */
      sqlite3_int64 i2 = 0;       /* Last position from pp2 */
      sqlite3_int64 iPrev = 0;
      int n = fts3PutColNumber(&p, iCol1);
      p1 += n;
      p2 += n;

      /* At this point, both p1 and p2 point to the start of column-lists
      ** for the same column (the column with index iCol1 and iCol2).
      ** A column-list is a list of non-negative delta-encoded varints, each 
      ** incremented by 2 before being stored. Each list is terminated by a
      ** POS_END (0) or POS_COLUMN (1). The following block merges the two lists
      ** and writes the results to buffer p. p is left pointing to the byte
      ** after the list written. No terminator (POS_END or POS_COLUMN) is
      ** written to the output.
      */
      fts3GetDeltaVarint(&p1, &i1);
      fts3GetDeltaVarint(&p2, &i2);
      do {
        fts3PutDeltaVarint(&p, &iPrev, (i1<i2) ? i1 : i2); 
        iPrev -= 2;
        if( i1==i2 ){
          fts3ReadNextPos(&p1, &i1);
          fts3ReadNextPos(&p2, &i2);
        }else if( i1<i2 ){
          fts3ReadNextPos(&p1, &i1);
        }else{
          fts3ReadNextPos(&p2, &i2);
        }
      }while( i1!=POSITION_LIST_END || i2!=POSITION_LIST_END );
    }else if( iCol1<iCol2 ){
      p1 += fts3PutColNumber(&p, iCol1);
      fts3ColumnlistCopy(&p, &p1);
    }else{
      p2 += fts3PutColNumber(&p, iCol2);
      fts3ColumnlistCopy(&p, &p2);
    }
  }

  *p++ = POS_END;
  *pp = p;
  *pp1 = p1 + 1;
  *pp2 = p2 + 1;
}

/*
** This function is used to merge two position lists into one. When it is
** called, *pp1 and *pp2 must both point to position lists. A position-list is
** the part of a doclist that follows each document id. For example, if a row
** contains:
**
**     'a b c'|'x y z'|'a b b a'
**
** Then the position list for this row for token 'b' would consist of:
**
**     0x02 0x01 0x02 0x03 0x03 0x00
**
** When this function returns, both *pp1 and *pp2 are left pointing to the
** byte following the 0x00 terminator of their respective position lists.
**
** If isSaveLeft is 0, an entry is added to the output position list for 
** each position in *pp2 for which there exists one or more positions in
** *pp1 so that (pos(*pp2)>pos(*pp1) && pos(*pp2)-pos(*pp1)<=nToken). i.e.
** when the *pp1 token appears before the *pp2 token, but not more than nToken
** slots before it.
**
** e.g. nToken==1 searches for adjacent positions.
*/
static int fts3PoslistPhraseMerge(
  char **pp,                      /* IN/OUT: Preallocated output buffer */
  int nToken,                     /* Maximum difference in token positions */
  int isSaveLeft,                 /* Save the left position */
  int isExact,                    /* If *pp1 is exactly nTokens before *pp2 */
  char **pp1,                     /* IN/OUT: Left input list */
  char **pp2                      /* IN/OUT: Right input list */
){
  char *p = *pp;
  char *p1 = *pp1;
  char *p2 = *pp2;
  int iCol1 = 0;
  int iCol2 = 0;

  /* Never set both isSaveLeft and isExact for the same invocation. */
  assert( isSaveLeft==0 || isExact==0 );

  assert( p!=0 && *p1!=0 && *p2!=0 );
  if( *p1==POS_COLUMN ){ 
    p1++;
    p1 += sqlite3Fts3GetVarint32(p1, &iCol1);
  }
  if( *p2==POS_COLUMN ){ 
    p2++;
    p2 += sqlite3Fts3GetVarint32(p2, &iCol2);
  }

  while( 1 ){
    if( iCol1==iCol2 ){
      char *pSave = p;
      sqlite3_int64 iPrev = 0;
      sqlite3_int64 iPos1 = 0;
      sqlite3_int64 iPos2 = 0;

      if( iCol1 ){
        *p++ = POS_COLUMN;
        p += sqlite3Fts3PutVarint(p, iCol1);
      }

      assert( *p1!=POS_END && *p1!=POS_COLUMN );
      assert( *p2!=POS_END && *p2!=POS_COLUMN );
      fts3GetDeltaVarint(&p1, &iPos1); iPos1 -= 2;
      fts3GetDeltaVarint(&p2, &iPos2); iPos2 -= 2;

      while( 1 ){
        if( iPos2==iPos1+nToken 
         || (isExact==0 && iPos2>iPos1 && iPos2<=iPos1+nToken) 
        ){
          sqlite3_int64 iSave;
          iSave = isSaveLeft ? iPos1 : iPos2;
          fts3PutDeltaVarint(&p, &iPrev, iSave+2); iPrev -= 2;
          pSave = 0;
          assert( p );
        }
        if( (!isSaveLeft && iPos2<=(iPos1+nToken)) || iPos2<=iPos1 ){
          if( (*p2&0xFE)==0 ) break;
          fts3GetDeltaVarint(&p2, &iPos2); iPos2 -= 2;
        }else{
          if( (*p1&0xFE)==0 ) break;
          fts3GetDeltaVarint(&p1, &iPos1); iPos1 -= 2;
        }
      }

      if( pSave ){
        assert( pp && p );
        p = pSave;
      }

      fts3ColumnlistCopy(0, &p1);
      fts3ColumnlistCopy(0, &p2);
      assert( (*p1&0xFE)==0 && (*p2&0xFE)==0 );
      if( 0==*p1 || 0==*p2 ) break;

      p1++;
      p1 += sqlite3Fts3GetVarint32(p1, &iCol1);
      p2++;
      p2 += sqlite3Fts3GetVarint32(p2, &iCol2);
    }

    /* Advance pointer p1 or p2 (whichever corresponds to the smaller of
    ** iCol1 and iCol2) so that it points to either the 0x00 that marks the
    ** end of the position list, or the 0x01 that precedes the next 
    ** column-number in the position list. 
    */
    else if( iCol1<iCol2 ){
      fts3ColumnlistCopy(0, &p1);
      if( 0==*p1 ) break;
      p1++;
      p1 += sqlite3Fts3GetVarint32(p1, &iCol1);
    }else{
      fts3ColumnlistCopy(0, &p2);
      if( 0==*p2 ) break;
      p2++;
      p2 += sqlite3Fts3GetVarint32(p2, &iCol2);
    }
  }

  fts3PoslistCopy(0, &p2);
  fts3PoslistCopy(0, &p1);
  *pp1 = p1;
  *pp2 = p2;
  if( *pp==p ){
    return 0;
  }
  *p++ = 0x00;
  *pp = p;
  return 1;
}

/*
** Merge two position-lists as required by the NEAR operator. The argument
** position lists correspond to the left and right phrases of an expression 
** like:
**
**     "phrase 1" NEAR "phrase number 2"
**
** Position list *pp1 corresponds to the left-hand side of the NEAR 
** expression and *pp2 to the right. As usual, the indexes in the position 
** lists are the offsets of the last token in each phrase (tokens "1" and "2" 
** in the example above).
**
** The output position list - written to *pp - is a copy of *pp2 with those
** entries that are not sufficiently NEAR entries in *pp1 removed.
*/
static int fts3PoslistNearMerge(
  char **pp,                      /* Output buffer */
  char *aTmp,                     /* Temporary buffer space */
  int nRight,                     /* Maximum difference in token positions */
  int nLeft,                      /* Maximum difference in token positions */
  char **pp1,                     /* IN/OUT: Left input list */
  char **pp2                      /* IN/OUT: Right input list */
){
  char *p1 = *pp1;
  char *p2 = *pp2;

  char *pTmp1 = aTmp;
  char *pTmp2;
  char *aTmp2;
  int res = 1;

  fts3PoslistPhraseMerge(&pTmp1, nRight, 0, 0, pp1, pp2);
  aTmp2 = pTmp2 = pTmp1;
  *pp1 = p1;
  *pp2 = p2;
  fts3PoslistPhraseMerge(&pTmp2, nLeft, 1, 0, pp2, pp1);
  if( pTmp1!=aTmp && pTmp2!=aTmp2 ){
    fts3PoslistMerge(pp, &aTmp, &aTmp2);
  }else if( pTmp1!=aTmp ){
    fts3PoslistCopy(pp, &aTmp);
  }else if( pTmp2!=aTmp2 ){
    fts3PoslistCopy(pp, &aTmp2);
  }else{
    res = 0;
  }

  return res;
}

/* 
** An instance of this function is used to merge together the (potentially
** large number of) doclists for each term that matches a prefix query.
** See function fts3TermSelectMerge() for details.
*/
typedef struct TermSelect TermSelect;
struct TermSelect {
  char *aaOutput[16];             /* Malloc'd output buffers */
  int anOutput[16];               /* Size each output buffer in bytes */
};

/*
** This function is used to read a single varint from a buffer. Parameter
** pEnd points 1 byte past the end of the buffer. When this function is
** called, if *pp points to pEnd or greater, then the end of the buffer
** has been reached. In this case *pp is set to 0 and the function returns.
**
** If *pp does not point to or past pEnd, then a single varint is read
** from *pp. *pp is then set to point 1 byte past the end of the read varint.
**
** If bDescIdx is false, the value read is added to *pVal before returning.
** If it is true, the value read is subtracted from *pVal before this 
** function returns.
*/
static void fts3GetDeltaVarint3(
  char **pp,                      /* IN/OUT: Point to read varint from */
  char *pEnd,                     /* End of buffer */
  int bDescIdx,                   /* True if docids are descending */
  sqlite3_int64 *pVal             /* IN/OUT: Integer value */
){
  if( *pp>=pEnd ){
    *pp = 0;
  }else{
    sqlite3_int64 iVal;
    *pp += sqlite3Fts3GetVarint(*pp, &iVal);
    if( bDescIdx ){
      *pVal -= iVal;
    }else{
      *pVal += iVal;
    }
  }
}

/*
** This function is used to write a single varint to a buffer. The varint
** is written to *pp. Before returning, *pp is set to point 1 byte past the
** end of the value written.
**
** If *pbFirst is zero when this function is called, the value written to
** the buffer is that of parameter iVal. 
**
** If *pbFirst is non-zero when this function is called, then the value 
** written is either (iVal-*piPrev) (if bDescIdx is zero) or (*piPrev-iVal)
** (if bDescIdx is non-zero).
**
** Before returning, this function always sets *pbFirst to 1 and *piPrev
** to the value of parameter iVal.
*/
static void fts3PutDeltaVarint3(
  char **pp,                      /* IN/OUT: Output pointer */
  int bDescIdx,                   /* True for descending docids */
  sqlite3_int64 *piPrev,          /* IN/OUT: Previous value written to list */
  int *pbFirst,                   /* IN/OUT: True after first int written */
  sqlite3_int64 iVal              /* Write this value to the list */
){
  sqlite3_int64 iWrite;
  if( bDescIdx==0 || *pbFirst==0 ){
    iWrite = iVal - *piPrev;
  }else{
    iWrite = *piPrev - iVal;
  }
  assert( *pbFirst || *piPrev==0 );
  assert( *pbFirst==0 || iWrite>0 );
  *pp += sqlite3Fts3PutVarint(*pp, iWrite);
  *piPrev = iVal;
  *pbFirst = 1;
}


/*
** This macro is used by various functions that merge doclists. The two
** arguments are 64-bit docid values. If the value of the stack variable
** bDescDoclist is 0 when this macro is invoked, then it returns (i1-i2). 
** Otherwise, (i2-i1).
**
** Using this makes it easier to write code that can merge doclists that are
** sorted in either ascending or descending order.
*/
#define DOCID_CMP(i1, i2) ((bDescDoclist?-1:1) * (i1-i2))

/*
** This function does an "OR" merge of two doclists (output contains all
** positions contained in either argument doclist). If the docids in the 
** input doclists are sorted in ascending order, parameter bDescDoclist
** should be false. If they are sorted in ascending order, it should be
** passed a non-zero value.
**
** If no error occurs, *paOut is set to point at an sqlite3_malloc'd buffer
** containing the output doclist and SQLITE_OK is returned. In this case
** *pnOut is set to the number of bytes in the output doclist.
**
** If an error occurs, an SQLite error code is returned. The output values
** are undefined in this case.
*/
static int fts3DoclistOrMerge(
  int bDescDoclist,               /* True if arguments are desc */
  char *a1, int n1,               /* First doclist */
  char *a2, int n2,               /* Second doclist */
  char **paOut, int *pnOut        /* OUT: Malloc'd doclist */
){
  sqlite3_int64 i1 = 0;
  sqlite3_int64 i2 = 0;
  sqlite3_int64 iPrev = 0;
  char *pEnd1 = &a1[n1];
  char *pEnd2 = &a2[n2];
  char *p1 = a1;
  char *p2 = a2;
  char *p;
  char *aOut;
  int bFirstOut = 0;

  *paOut = 0;
  *pnOut = 0;

  /* Allocate space for the output. Both the input and output doclists
  ** are delta encoded. If they are in ascending order (bDescDoclist==0),
  ** then the first docid in each list is simply encoded as a varint. For
  ** each subsequent docid, the varint stored is the difference between the
  ** current and previous docid (a positive number - since the list is in
  ** ascending order).
  **
  ** The first docid written to the output is therefore encoded using the 
  ** same number of bytes as it is in whichever of the input lists it is
  ** read from. And each subsequent docid read from the same input list 
  ** consumes either the same or less bytes as it did in the input (since
  ** the difference between it and the previous value in the output must
  ** be a positive value less than or equal to the delta value read from 
  ** the input list). The same argument applies to all but the first docid
  ** read from the 'other' list. And to the contents of all position lists
  ** that will be copied and merged from the input to the output.
  **
  ** However, if the first docid copied to the output is a negative number,
  ** then the encoding of the first docid from the 'other' input list may
  ** be larger in the output than it was in the input (since the delta value
  ** may be a larger positive integer than the actual docid).
  **
  ** The space required to store the output is therefore the sum of the
  ** sizes of the two inputs, plus enough space for exactly one of the input
  ** docids to grow. 
  **
  ** A symetric argument may be made if the doclists are in descending 
  ** order.
  */
  aOut = sqlite3_malloc(n1+n2+FTS3_VARINT_MAX-1);
  if( !aOut ) return SQLITE_NOMEM;

  p = aOut;
  fts3GetDeltaVarint3(&p1, pEnd1, 0, &i1);
  fts3GetDeltaVarint3(&p2, pEnd2, 0, &i2);
  while( p1 || p2 ){
    sqlite3_int64 iDiff = DOCID_CMP(i1, i2);

    if( p2 && p1 && iDiff==0 ){
      fts3PutDeltaVarint3(&p, bDescDoclist, &iPrev, &bFirstOut, i1);
      fts3PoslistMerge(&p, &p1, &p2);
      fts3GetDeltaVarint3(&p1, pEnd1, bDescDoclist, &i1);
      fts3GetDeltaVarint3(&p2, pEnd2, bDescDoclist, &i2);
    }else if( !p2 || (p1 && iDiff<0) ){
      fts3PutDeltaVarint3(&p, bDescDoclist, &iPrev, &bFirstOut, i1);
      fts3PoslistCopy(&p, &p1);
      fts3GetDeltaVarint3(&p1, pEnd1, bDescDoclist, &i1);
    }else{
      fts3PutDeltaVarint3(&p, bDescDoclist, &iPrev, &bFirstOut, i2);
      fts3PoslistCopy(&p, &p2);
      fts3GetDeltaVarint3(&p2, pEnd2, bDescDoclist, &i2);
    }
  }

  *paOut = aOut;
  *pnOut = (p-aOut);
  assert( *pnOut<=n1+n2+FTS3_VARINT_MAX-1 );
  return SQLITE_OK;
}

/*
** This function does a "phrase" merge of two doclists. In a phrase merge,
** the output contains a copy of each position from the right-hand input
** doclist for which there is a position in the left-hand input doclist
** exactly nDist tokens before it.
**
** If the docids in the input doclists are sorted in ascending order,
** parameter bDescDoclist should be false. If they are sorted in ascending 
** order, it should be passed a non-zero value.
**
** The right-hand input doclist is overwritten by this function.
*/
static void fts3DoclistPhraseMerge(
  int bDescDoclist,               /* True if arguments are desc */
  int nDist,                      /* Distance from left to right (1=adjacent) */
  char *aLeft, int nLeft,         /* Left doclist */
  char *aRight, int *pnRight      /* IN/OUT: Right/output doclist */
){
  sqlite3_int64 i1 = 0;
  sqlite3_int64 i2 = 0;
  sqlite3_int64 iPrev = 0;
  char *pEnd1 = &aLeft[nLeft];
  char *pEnd2 = &aRight[*pnRight];
  char *p1 = aLeft;
  char *p2 = aRight;
  char *p;
  int bFirstOut = 0;
  char *aOut = aRight;

  assert( nDist>0 );

  p = aOut;
  fts3GetDeltaVarint3(&p1, pEnd1, 0, &i1);
  fts3GetDeltaVarint3(&p2, pEnd2, 0, &i2);

  while( p1 && p2 ){
    sqlite3_int64 iDiff = DOCID_CMP(i1, i2);
    if( iDiff==0 ){
      char *pSave = p;
      sqlite3_int64 iPrevSave = iPrev;
      int bFirstOutSave = bFirstOut;

      fts3PutDeltaVarint3(&p, bDescDoclist, &iPrev, &bFirstOut, i1);
      if( 0==fts3PoslistPhraseMerge(&p, nDist, 0, 1, &p1, &p2) ){
        p = pSave;
        iPrev = iPrevSave;
        bFirstOut = bFirstOutSave;
      }
      fts3GetDeltaVarint3(&p1, pEnd1, bDescDoclist, &i1);
      fts3GetDeltaVarint3(&p2, pEnd2, bDescDoclist, &i2);
    }else if( iDiff<0 ){
      fts3PoslistCopy(0, &p1);
      fts3GetDeltaVarint3(&p1, pEnd1, bDescDoclist, &i1);
    }else{
      fts3PoslistCopy(0, &p2);
      fts3GetDeltaVarint3(&p2, pEnd2, bDescDoclist, &i2);
    }
  }

  *pnRight = p - aOut;
}

/*
** Argument pList points to a position list nList bytes in size. This
** function checks to see if the position list contains any entries for
** a token in position 0 (of any column). If so, it writes argument iDelta
** to the output buffer pOut, followed by a position list consisting only
** of the entries from pList at position 0, and terminated by an 0x00 byte.
** The value returned is the number of bytes written to pOut (if any).
*/
SQLITE_PRIVATE int sqlite3Fts3FirstFilter(
  sqlite3_int64 iDelta,           /* Varint that may be written to pOut */
  char *pList,                    /* Position list (no 0x00 term) */
  int nList,                      /* Size of pList in bytes */
  char *pOut                      /* Write output here */
){
  int nOut = 0;
  int bWritten = 0;               /* True once iDelta has been written */
  char *p = pList;
  char *pEnd = &pList[nList];

  if( *p!=0x01 ){
    if( *p==0x02 ){
      nOut += sqlite3Fts3PutVarint(&pOut[nOut], iDelta);
      pOut[nOut++] = 0x02;
      bWritten = 1;
    }
    fts3ColumnlistCopy(0, &p);
  }

  while( p<pEnd && *p==0x01 ){
    sqlite3_int64 iCol;
    p++;
    p += sqlite3Fts3GetVarint(p, &iCol);
    if( *p==0x02 ){
      if( bWritten==0 ){
        nOut += sqlite3Fts3PutVarint(&pOut[nOut], iDelta);
        bWritten = 1;
      }
      pOut[nOut++] = 0x01;
      nOut += sqlite3Fts3PutVarint(&pOut[nOut], iCol);
      pOut[nOut++] = 0x02;
    }
    fts3ColumnlistCopy(0, &p);
  }
  if( bWritten ){
    pOut[nOut++] = 0x00;
  }

  return nOut;
}


/*
** Merge all doclists in the TermSelect.aaOutput[] array into a single
** doclist stored in TermSelect.aaOutput[0]. If successful, delete all
** other doclists (except the aaOutput[0] one) and return SQLITE_OK.
**
** If an OOM error occurs, return SQLITE_NOMEM. In this case it is
** the responsibility of the caller to free any doclists left in the
** TermSelect.aaOutput[] array.
*/
static int fts3TermSelectFinishMerge(Fts3Table *p, TermSelect *pTS){
  char *aOut = 0;
  int nOut = 0;
  int i;

  /* Loop through the doclists in the aaOutput[] array. Merge them all
  ** into a single doclist.
  */
  for(i=0; i<SizeofArray(pTS->aaOutput); i++){
    if( pTS->aaOutput[i] ){
      if( !aOut ){
        aOut = pTS->aaOutput[i];
        nOut = pTS->anOutput[i];
        pTS->aaOutput[i] = 0;
      }else{
        int nNew;
        char *aNew;

        int rc = fts3DoclistOrMerge(p->bDescIdx, 
            pTS->aaOutput[i], pTS->anOutput[i], aOut, nOut, &aNew, &nNew
        );
        if( rc!=SQLITE_OK ){
          sqlite3_free(aOut);
          return rc;
        }

        sqlite3_free(pTS->aaOutput[i]);
        sqlite3_free(aOut);
        pTS->aaOutput[i] = 0;
        aOut = aNew;
        nOut = nNew;
      }
    }
  }

  pTS->aaOutput[0] = aOut;
  pTS->anOutput[0] = nOut;
  return SQLITE_OK;
}

/*
** Merge the doclist aDoclist/nDoclist into the TermSelect object passed
** as the first argument. The merge is an "OR" merge (see function
** fts3DoclistOrMerge() for details).
**
** This function is called with the doclist for each term that matches
** a queried prefix. It merges all these doclists into one, the doclist
** for the specified prefix. Since there can be a very large number of
** doclists to merge, the merging is done pair-wise using the TermSelect
** object.
**
** This function returns SQLITE_OK if the merge is successful, or an
** SQLite error code (SQLITE_NOMEM) if an error occurs.
*/
static int fts3TermSelectMerge(
  Fts3Table *p,                   /* FTS table handle */
  TermSelect *pTS,                /* TermSelect object to merge into */
  char *aDoclist,                 /* Pointer to doclist */
  int nDoclist                    /* Size of aDoclist in bytes */
){
  if( pTS->aaOutput[0]==0 ){
    /* If this is the first term selected, copy the doclist to the output
    ** buffer using memcpy(). */
    pTS->aaOutput[0] = sqlite3_malloc(nDoclist);
    pTS->anOutput[0] = nDoclist;
    if( pTS->aaOutput[0] ){
      memcpy(pTS->aaOutput[0], aDoclist, nDoclist);
    }else{
      return SQLITE_NOMEM;
    }
  }else{
    char *aMerge = aDoclist;
    int nMerge = nDoclist;
    int iOut;

    for(iOut=0; iOut<SizeofArray(pTS->aaOutput); iOut++){
      if( pTS->aaOutput[iOut]==0 ){
        assert( iOut>0 );
        pTS->aaOutput[iOut] = aMerge;
        pTS->anOutput[iOut] = nMerge;
        break;
      }else{
        char *aNew;
        int nNew;

        int rc = fts3DoclistOrMerge(p->bDescIdx, aMerge, nMerge, 
            pTS->aaOutput[iOut], pTS->anOutput[iOut], &aNew, &nNew
        );
        if( rc!=SQLITE_OK ){
          if( aMerge!=aDoclist ) sqlite3_free(aMerge);
          return rc;
        }

        if( aMerge!=aDoclist ) sqlite3_free(aMerge);
        sqlite3_free(pTS->aaOutput[iOut]);
        pTS->aaOutput[iOut] = 0;
  
        aMerge = aNew;
        nMerge = nNew;
        if( (iOut+1)==SizeofArray(pTS->aaOutput) ){
          pTS->aaOutput[iOut] = aMerge;
          pTS->anOutput[iOut] = nMerge;
        }
      }
    }
  }
  return SQLITE_OK;
}

/*
** Append SegReader object pNew to the end of the pCsr->apSegment[] array.
*/
static int fts3SegReaderCursorAppend(
  Fts3MultiSegReader *pCsr, 
  Fts3SegReader *pNew
){
  if( (pCsr->nSegment%16)==0 ){
    Fts3SegReader **apNew;
    int nByte = (pCsr->nSegment + 16)*sizeof(Fts3SegReader*);
    apNew = (Fts3SegReader **)sqlite3_realloc(pCsr->apSegment, nByte);
    if( !apNew ){
      sqlite3Fts3SegReaderFree(pNew);
      return SQLITE_NOMEM;
    }
    pCsr->apSegment = apNew;
  }
  pCsr->apSegment[pCsr->nSegment++] = pNew;
  return SQLITE_OK;
}

/*
** Add seg-reader objects to the Fts3MultiSegReader object passed as the
** 8th argument.
**
** This function returns SQLITE_OK if successful, or an SQLite error code
** otherwise.
*/
static int fts3SegReaderCursor(
  Fts3Table *p,                   /* FTS3 table handle */
  int iIndex,                     /* Index to search (from 0 to p->nIndex-1) */
  int iLevel,                     /* Level of segments to scan */
  const char *zTerm,              /* Term to query for */
  int nTerm,                      /* Size of zTerm in bytes */
  int isPrefix,                   /* True for a prefix search */
  int isScan,                     /* True to scan from zTerm to EOF */
  Fts3MultiSegReader *pCsr        /* Cursor object to populate */
){
  int rc = SQLITE_OK;             /* Error code */
  sqlite3_stmt *pStmt = 0;        /* Statement to iterate through segments */
  int rc2;                        /* Result of sqlite3_reset() */

  /* If iLevel is less than 0 and this is not a scan, include a seg-reader 
  ** for the pending-terms. If this is a scan, then this call must be being
  ** made by an fts4aux module, not an FTS table. In this case calling
  ** Fts3SegReaderPending might segfault, as the data structures used by 
  ** fts4aux are not completely populated. So it's easiest to filter these
  ** calls out here.  */
  if( iLevel<0 && p->aIndex ){
    Fts3SegReader *pSeg = 0;
    rc = sqlite3Fts3SegReaderPending(p, iIndex, zTerm, nTerm, isPrefix, &pSeg);
    if( rc==SQLITE_OK && pSeg ){
      rc = fts3SegReaderCursorAppend(pCsr, pSeg);
    }
  }

  if( iLevel!=FTS3_SEGCURSOR_PENDING ){
    if( rc==SQLITE_OK ){
      rc = sqlite3Fts3AllSegdirs(p, iIndex, iLevel, &pStmt);
    }

    while( rc==SQLITE_OK && SQLITE_ROW==(rc = sqlite3_step(pStmt)) ){
      Fts3SegReader *pSeg = 0;

      /* Read the values returned by the SELECT into local variables. */
      sqlite3_int64 iStartBlock = sqlite3_column_int64(pStmt, 1);
      sqlite3_int64 iLeavesEndBlock = sqlite3_column_int64(pStmt, 2);
      sqlite3_int64 iEndBlock = sqlite3_column_int64(pStmt, 3);
      int nRoot = sqlite3_column_bytes(pStmt, 4);
      char const *zRoot = sqlite3_column_blob(pStmt, 4);

      /* If zTerm is not NULL, and this segment is not stored entirely on its
      ** root node, the range of leaves scanned can be reduced. Do this. */
      if( iStartBlock && zTerm ){
        sqlite3_int64 *pi = (isPrefix ? &iLeavesEndBlock : 0);
        rc = fts3SelectLeaf(p, zTerm, nTerm, zRoot, nRoot, &iStartBlock, pi);
        if( rc!=SQLITE_OK ) goto finished;
        if( isPrefix==0 && isScan==0 ) iLeavesEndBlock = iStartBlock;
      }
 
      rc = sqlite3Fts3SegReaderNew(pCsr->nSegment+1, 
          iStartBlock, iLeavesEndBlock, iEndBlock, zRoot, nRoot, &pSeg
      );
      if( rc!=SQLITE_OK ) goto finished;
      rc = fts3SegReaderCursorAppend(pCsr, pSeg);
    }
  }

 finished:
  rc2 = sqlite3_reset(pStmt);
  if( rc==SQLITE_DONE ) rc = rc2;

  return rc;
}

/*
** Set up a cursor object for iterating through a full-text index or a 
** single level therein.
*/
SQLITE_PRIVATE int sqlite3Fts3SegReaderCursor(
  Fts3Table *p,                   /* FTS3 table handle */
  int iIndex,                     /* Index to search (from 0 to p->nIndex-1) */
  int iLevel,                     /* Level of segments to scan */
  const char *zTerm,              /* Term to query for */
  int nTerm,                      /* Size of zTerm in bytes */
  int isPrefix,                   /* True for a prefix search */
  int isScan,                     /* True to scan from zTerm to EOF */
  Fts3MultiSegReader *pCsr       /* Cursor object to populate */
){
  assert( iIndex>=0 && iIndex<p->nIndex );
  assert( iLevel==FTS3_SEGCURSOR_ALL
      ||  iLevel==FTS3_SEGCURSOR_PENDING 
      ||  iLevel>=0
  );
  assert( iLevel<FTS3_SEGDIR_MAXLEVEL );
  assert( FTS3_SEGCURSOR_ALL<0 && FTS3_SEGCURSOR_PENDING<0 );
  assert( isPrefix==0 || isScan==0 );

  /* "isScan" is only set to true by the ft4aux module, an ordinary
  ** full-text tables. */
  assert( isScan==0 || p->aIndex==0 );

  memset(pCsr, 0, sizeof(Fts3MultiSegReader));

  return fts3SegReaderCursor(
      p, iIndex, iLevel, zTerm, nTerm, isPrefix, isScan, pCsr
  );
}

/*
** In addition to its current configuration, have the Fts3MultiSegReader
** passed as the 4th argument also scan the doclist for term zTerm/nTerm.
**
** SQLITE_OK is returned if no error occurs, otherwise an SQLite error code.
*/
static int fts3SegReaderCursorAddZero(
  Fts3Table *p,                   /* FTS virtual table handle */
  const char *zTerm,              /* Term to scan doclist of */
  int nTerm,                      /* Number of bytes in zTerm */
  Fts3MultiSegReader *pCsr        /* Fts3MultiSegReader to modify */
){
  return fts3SegReaderCursor(p, 0, FTS3_SEGCURSOR_ALL, zTerm, nTerm, 0, 0,pCsr);
}

/*
** Open an Fts3MultiSegReader to scan the doclist for term zTerm/nTerm. Or,
** if isPrefix is true, to scan the doclist for all terms for which 
** zTerm/nTerm is a prefix. If successful, return SQLITE_OK and write
** a pointer to the new Fts3MultiSegReader to *ppSegcsr. Otherwise, return
** an SQLite error code.
**
** It is the responsibility of the caller to free this object by eventually
** passing it to fts3SegReaderCursorFree() 
**
** SQLITE_OK is returned if no error occurs, otherwise an SQLite error code.
** Output parameter *ppSegcsr is set to 0 if an error occurs.
*/
static int fts3TermSegReaderCursor(
  Fts3Cursor *pCsr,               /* Virtual table cursor handle */
  const char *zTerm,              /* Term to query for */
  int nTerm,                      /* Size of zTerm in bytes */
  int isPrefix,                   /* True for a prefix search */
  Fts3MultiSegReader **ppSegcsr   /* OUT: Allocated seg-reader cursor */
){
  Fts3MultiSegReader *pSegcsr;    /* Object to allocate and return */
  int rc = SQLITE_NOMEM;          /* Return code */

  pSegcsr = sqlite3_malloc(sizeof(Fts3MultiSegReader));
  if( pSegcsr ){
    int i;
    int bFound = 0;               /* True once an index has been found */
    Fts3Table *p = (Fts3Table *)pCsr->base.pVtab;

    if( isPrefix ){
      for(i=1; bFound==0 && i<p->nIndex; i++){
        if( p->aIndex[i].nPrefix==nTerm ){
          bFound = 1;
          rc = sqlite3Fts3SegReaderCursor(
              p, i, FTS3_SEGCURSOR_ALL, zTerm, nTerm, 0, 0, pSegcsr);
          pSegcsr->bLookup = 1;
        }
      }

      for(i=1; bFound==0 && i<p->nIndex; i++){
        if( p->aIndex[i].nPrefix==nTerm+1 ){
          bFound = 1;
          rc = sqlite3Fts3SegReaderCursor(
              p, i, FTS3_SEGCURSOR_ALL, zTerm, nTerm, 1, 0, pSegcsr
          );
          if( rc==SQLITE_OK ){
            rc = fts3SegReaderCursorAddZero(p, zTerm, nTerm, pSegcsr);
          }
        }
      }
    }

    if( bFound==0 ){
      rc = sqlite3Fts3SegReaderCursor(
          p, 0, FTS3_SEGCURSOR_ALL, zTerm, nTerm, isPrefix, 0, pSegcsr
      );
      pSegcsr->bLookup = !isPrefix;
    }
  }

  *ppSegcsr = pSegcsr;
  return rc;
}

/*
** Free an Fts3MultiSegReader allocated by fts3TermSegReaderCursor().
*/
static void fts3SegReaderCursorFree(Fts3MultiSegReader *pSegcsr){
  sqlite3Fts3SegReaderFinish(pSegcsr);
  sqlite3_free(pSegcsr);
}

/*
** This function retreives the doclist for the specified term (or term
** prefix) from the database.
*/
static int fts3TermSelect(
  Fts3Table *p,                   /* Virtual table handle */
  Fts3PhraseToken *pTok,          /* Token to query for */
  int iColumn,                    /* Column to query (or -ve for all columns) */
  int *pnOut,                     /* OUT: Size of buffer at *ppOut */
  char **ppOut                    /* OUT: Malloced result buffer */
){
  int rc;                         /* Return code */
  Fts3MultiSegReader *pSegcsr;    /* Seg-reader cursor for this term */
  TermSelect tsc;                 /* Object for pair-wise doclist merging */
  Fts3SegFilter filter;           /* Segment term filter configuration */

  pSegcsr = pTok->pSegcsr;
  memset(&tsc, 0, sizeof(TermSelect));

  filter.flags = FTS3_SEGMENT_IGNORE_EMPTY | FTS3_SEGMENT_REQUIRE_POS
        | (pTok->isPrefix ? FTS3_SEGMENT_PREFIX : 0)
        | (pTok->bFirst ? FTS3_SEGMENT_FIRST : 0)
        | (iColumn<p->nColumn ? FTS3_SEGMENT_COLUMN_FILTER : 0);
  filter.iCol = iColumn;
  filter.zTerm = pTok->z;
  filter.nTerm = pTok->n;

  rc = sqlite3Fts3SegReaderStart(p, pSegcsr, &filter);
  while( SQLITE_OK==rc
      && SQLITE_ROW==(rc = sqlite3Fts3SegReaderStep(p, pSegcsr)) 
  ){
    rc = fts3TermSelectMerge(p, &tsc, pSegcsr->aDoclist, pSegcsr->nDoclist);
  }

  if( rc==SQLITE_OK ){
    rc = fts3TermSelectFinishMerge(p, &tsc);
  }
  if( rc==SQLITE_OK ){
    *ppOut = tsc.aaOutput[0];
    *pnOut = tsc.anOutput[0];
  }else{
    int i;
    for(i=0; i<SizeofArray(tsc.aaOutput); i++){
      sqlite3_free(tsc.aaOutput[i]);
    }
  }

  fts3SegReaderCursorFree(pSegcsr);
  pTok->pSegcsr = 0;
  return rc;
}

/*
** This function counts the total number of docids in the doclist stored
** in buffer aList[], size nList bytes.
**
** If the isPoslist argument is true, then it is assumed that the doclist
** contains a position-list following each docid. Otherwise, it is assumed
** that the doclist is simply a list of docids stored as delta encoded 
** varints.
*/
static int fts3DoclistCountDocids(char *aList, int nList){
  int nDoc = 0;                   /* Return value */
  if( aList ){
    char *aEnd = &aList[nList];   /* Pointer to one byte after EOF */
    char *p = aList;              /* Cursor */
    while( p<aEnd ){
      nDoc++;
      while( (*p++)&0x80 );     /* Skip docid varint */
      fts3PoslistCopy(0, &p);   /* Skip over position list */
    }
  }

  return nDoc;
}

/*
** Advance the cursor to the next row in the %_content table that
** matches the search criteria.  For a MATCH search, this will be
** the next row that matches. For a full-table scan, this will be
** simply the next row in the %_content table.  For a docid lookup,
** this routine simply sets the EOF flag.
**
** Return SQLITE_OK if nothing goes wrong.  SQLITE_OK is returned
** even if we reach end-of-file.  The fts3EofMethod() will be called
** subsequently to determine whether or not an EOF was hit.
*/
static int fts3NextMethod(sqlite3_vtab_cursor *pCursor){
  int rc;
  Fts3Cursor *pCsr = (Fts3Cursor *)pCursor;
  if( pCsr->eSearch==FTS3_DOCID_SEARCH || pCsr->eSearch==FTS3_FULLSCAN_SEARCH ){
    if( SQLITE_ROW!=sqlite3_step(pCsr->pStmt) ){
      pCsr->isEof = 1;
      rc = sqlite3_reset(pCsr->pStmt);
    }else{
      pCsr->iPrevId = sqlite3_column_int64(pCsr->pStmt, 0);
      rc = SQLITE_OK;
    }
  }else{
    rc = fts3EvalNext((Fts3Cursor *)pCursor);
  }
  assert( ((Fts3Table *)pCsr->base.pVtab)->pSegments==0 );
  return rc;
}

/*
** This is the xFilter interface for the virtual table.  See
** the virtual table xFilter method documentation for additional
** information.
**
** If idxNum==FTS3_FULLSCAN_SEARCH then do a full table scan against
** the %_content table.
**
** If idxNum==FTS3_DOCID_SEARCH then do a docid lookup for a single entry
** in the %_content table.
**
** If idxNum>=FTS3_FULLTEXT_SEARCH then use the full text index.  The
** column on the left-hand side of the MATCH operator is column
** number idxNum-FTS3_FULLTEXT_SEARCH, 0 indexed.  argv[0] is the right-hand
** side of the MATCH operator.
*/
static int fts3FilterMethod(
  sqlite3_vtab_cursor *pCursor,   /* The cursor used for this query */
  int idxNum,                     /* Strategy index */
  const char *idxStr,             /* Unused */
  int nVal,                       /* Number of elements in apVal */
  sqlite3_value **apVal           /* Arguments for the indexing scheme */
){
  int rc;
  char *zSql;                     /* SQL statement used to access %_content */
  Fts3Table *p = (Fts3Table *)pCursor->pVtab;
  Fts3Cursor *pCsr = (Fts3Cursor *)pCursor;

  UNUSED_PARAMETER(idxStr);
  UNUSED_PARAMETER(nVal);

  assert( idxNum>=0 && idxNum<=(FTS3_FULLTEXT_SEARCH+p->nColumn) );
  assert( nVal==0 || nVal==1 );
  assert( (nVal==0)==(idxNum==FTS3_FULLSCAN_SEARCH) );
  assert( p->pSegments==0 );

  /* In case the cursor has been used before, clear it now. */
  sqlite3_finalize(pCsr->pStmt);
  sqlite3_free(pCsr->aDoclist);
  sqlite3Fts3ExprFree(pCsr->pExpr);
  memset(&pCursor[1], 0, sizeof(Fts3Cursor)-sizeof(sqlite3_vtab_cursor));

  if( idxStr ){
    pCsr->bDesc = (idxStr[0]=='D');
  }else{
    pCsr->bDesc = p->bDescIdx;
  }
  pCsr->eSearch = (i16)idxNum;

  if( idxNum!=FTS3_DOCID_SEARCH && idxNum!=FTS3_FULLSCAN_SEARCH ){
    int iCol = idxNum-FTS3_FULLTEXT_SEARCH;
    const char *zQuery = (const char *)sqlite3_value_text(apVal[0]);

    if( zQuery==0 && sqlite3_value_type(apVal[0])!=SQLITE_NULL ){
      return SQLITE_NOMEM;
    }

    rc = sqlite3Fts3ExprParse(p->pTokenizer, p->azColumn, p->bHasStat, 
        p->nColumn, iCol, zQuery, -1, &pCsr->pExpr
    );
    if( rc!=SQLITE_OK ){
      if( rc==SQLITE_ERROR ){
        static const char *zErr = "malformed MATCH expression: [%s]";
        p->base.zErrMsg = sqlite3_mprintf(zErr, zQuery);
      }
      return rc;
    }

    rc = sqlite3Fts3ReadLock(p);
    if( rc!=SQLITE_OK ) return rc;

    rc = fts3EvalStart(pCsr);

    sqlite3Fts3SegmentsClose(p);
    if( rc!=SQLITE_OK ) return rc;
    pCsr->pNextId = pCsr->aDoclist;
    pCsr->iPrevId = 0;
  }

  /* Compile a SELECT statement for this cursor. For a full-table-scan, the
  ** statement loops through all rows of the %_content table. For a
  ** full-text query or docid lookup, the statement retrieves a single
  ** row by docid.
  */
  if( idxNum==FTS3_FULLSCAN_SEARCH ){
    zSql = sqlite3_mprintf(
        "SELECT %s ORDER BY rowid %s",
        p->zReadExprlist, (pCsr->bDesc ? "DESC" : "ASC")
    );
    if( zSql ){
      rc = sqlite3_prepare_v2(p->db, zSql, -1, &pCsr->pStmt, 0);
      sqlite3_free(zSql);
    }else{
      rc = SQLITE_NOMEM;
    }
  }else if( idxNum==FTS3_DOCID_SEARCH ){
    rc = fts3CursorSeekStmt(pCsr, &pCsr->pStmt);
    if( rc==SQLITE_OK ){
      rc = sqlite3_bind_value(pCsr->pStmt, 1, apVal[0]);
    }
  }
  if( rc!=SQLITE_OK ) return rc;

  return fts3NextMethod(pCursor);
}

/* 
** This is the xEof method of the virtual table. SQLite calls this 
** routine to find out if it has reached the end of a result set.
*/
static int fts3EofMethod(sqlite3_vtab_cursor *pCursor){
  return ((Fts3Cursor *)pCursor)->isEof;
}

/* 
** This is the xRowid method. The SQLite core calls this routine to
** retrieve the rowid for the current row of the result set. fts3
** exposes %_content.docid as the rowid for the virtual table. The
** rowid should be written to *pRowid.
*/
static int fts3RowidMethod(sqlite3_vtab_cursor *pCursor, sqlite_int64 *pRowid){
  Fts3Cursor *pCsr = (Fts3Cursor *) pCursor;
  *pRowid = pCsr->iPrevId;
  return SQLITE_OK;
}

/* 
** This is the xColumn method, called by SQLite to request a value from
** the row that the supplied cursor currently points to.
*/
static int fts3ColumnMethod(
  sqlite3_vtab_cursor *pCursor,   /* Cursor to retrieve value from */
  sqlite3_context *pContext,      /* Context for sqlite3_result_xxx() calls */
  int iCol                        /* Index of column to read value from */
){
  int rc = SQLITE_OK;             /* Return Code */
  Fts3Cursor *pCsr = (Fts3Cursor *) pCursor;
  Fts3Table *p = (Fts3Table *)pCursor->pVtab;

  /* The column value supplied by SQLite must be in range. */
  assert( iCol>=0 && iCol<=p->nColumn+1 );

  if( iCol==p->nColumn+1 ){
    /* This call is a request for the "docid" column. Since "docid" is an 
    ** alias for "rowid", use the xRowid() method to obtain the value.
    */
    sqlite3_result_int64(pContext, pCsr->iPrevId);
  }else if( iCol==p->nColumn ){
    /* The extra column whose name is the same as the table.
    ** Return a blob which is a pointer to the cursor.
    */
    sqlite3_result_blob(pContext, &pCsr, sizeof(pCsr), SQLITE_TRANSIENT);
  }else{
    rc = fts3CursorSeek(0, pCsr);
    if( rc==SQLITE_OK && sqlite3_data_count(pCsr->pStmt)>(iCol+1) ){
      sqlite3_result_value(pContext, sqlite3_column_value(pCsr->pStmt, iCol+1));
    }
  }

  assert( ((Fts3Table *)pCsr->base.pVtab)->pSegments==0 );
  return rc;
}

/* 
** This function is the implementation of the xUpdate callback used by 
** FTS3 virtual tables. It is invoked by SQLite each time a row is to be
** inserted, updated or deleted.
*/
static int fts3UpdateMethod(
  sqlite3_vtab *pVtab,            /* Virtual table handle */
  int nArg,                       /* Size of argument array */
  sqlite3_value **apVal,          /* Array of arguments */
  sqlite_int64 *pRowid            /* OUT: The affected (or effected) rowid */
){
  return sqlite3Fts3UpdateMethod(pVtab, nArg, apVal, pRowid);
}

/*
** Implementation of xSync() method. Flush the contents of the pending-terms
** hash-table to the database.
*/
static int fts3SyncMethod(sqlite3_vtab *pVtab){
  int rc = sqlite3Fts3PendingTermsFlush((Fts3Table *)pVtab);
  sqlite3Fts3SegmentsClose((Fts3Table *)pVtab);
  return rc;
}

/*
** Implementation of xBegin() method. This is a no-op.
*/
static int fts3BeginMethod(sqlite3_vtab *pVtab){
  TESTONLY( Fts3Table *p = (Fts3Table*)pVtab );
  UNUSED_PARAMETER(pVtab);
  assert( p->pSegments==0 );
  assert( p->nPendingData==0 );
  assert( p->inTransaction!=1 );
  TESTONLY( p->inTransaction = 1 );
  TESTONLY( p->mxSavepoint = -1; );
  return SQLITE_OK;
}

/*
** Implementation of xCommit() method. This is a no-op. The contents of
** the pending-terms hash-table have already been flushed into the database
** by fts3SyncMethod().
*/
static int fts3CommitMethod(sqlite3_vtab *pVtab){
  TESTONLY( Fts3Table *p = (Fts3Table*)pVtab );
  UNUSED_PARAMETER(pVtab);
  assert( p->nPendingData==0 );
  assert( p->inTransaction!=0 );
  assert( p->pSegments==0 );
  TESTONLY( p->inTransaction = 0 );
  TESTONLY( p->mxSavepoint = -1; );
  return SQLITE_OK;
}

/*
** Implementation of xRollback(). Discard the contents of the pending-terms
** hash-table. Any changes made to the database are reverted by SQLite.
*/
static int fts3RollbackMethod(sqlite3_vtab *pVtab){
  Fts3Table *p = (Fts3Table*)pVtab;
  sqlite3Fts3PendingTermsClear(p);
  assert( p->inTransaction!=0 );
  TESTONLY( p->inTransaction = 0 );
  TESTONLY( p->mxSavepoint = -1; );
  return SQLITE_OK;
}

/*
** When called, *ppPoslist must point to the byte immediately following the
** end of a position-list. i.e. ( (*ppPoslist)[-1]==POS_END ). This function
** moves *ppPoslist so that it instead points to the first byte of the
** same position list.
*/
static void fts3ReversePoslist(char *pStart, char **ppPoslist){
  char *p = &(*ppPoslist)[-2];
  char c = 0;

  while( p>pStart && (c=*p--)==0 );
  while( p>pStart && (*p & 0x80) | c ){ 
    c = *p--; 
  }
  if( p>pStart ){ p = &p[2]; }
  while( *p++&0x80 );
  *ppPoslist = p;
}

/*
** Helper function used by the implementation of the overloaded snippet(),
** offsets() and optimize() SQL functions.
**
** If the value passed as the third argument is a blob of size
** sizeof(Fts3Cursor*), then the blob contents are copied to the 
** output variable *ppCsr and SQLITE_OK is returned. Otherwise, an error
** message is written to context pContext and SQLITE_ERROR returned. The
** string passed via zFunc is used as part of the error message.
*/
static int fts3FunctionArg(
  sqlite3_context *pContext,      /* SQL function call context */
  const char *zFunc,              /* Function name */
  sqlite3_value *pVal,            /* argv[0] passed to function */
  Fts3Cursor **ppCsr              /* OUT: Store cursor handle here */
){
  Fts3Cursor *pRet;
  if( sqlite3_value_type(pVal)!=SQLITE_BLOB 
   || sqlite3_value_bytes(pVal)!=sizeof(Fts3Cursor *)
  ){
    char *zErr = sqlite3_mprintf("illegal first argument to %s", zFunc);
    sqlite3_result_error(pContext, zErr, -1);
    sqlite3_free(zErr);
    return SQLITE_ERROR;
  }
  memcpy(&pRet, sqlite3_value_blob(pVal), sizeof(Fts3Cursor *));
  *ppCsr = pRet;
  return SQLITE_OK;
}

/*
** Implementation of the snippet() function for FTS3
*/
static void fts3SnippetFunc(
  sqlite3_context *pContext,      /* SQLite function call context */
  int nVal,                       /* Size of apVal[] array */
  sqlite3_value **apVal           /* Array of arguments */
){
  Fts3Cursor *pCsr;               /* Cursor handle passed through apVal[0] */
  const char *zStart = "<b>";
  const char *zEnd = "</b>";
  const char *zEllipsis = "<b>...</b>";
  int iCol = -1;
  int nToken = 15;                /* Default number of tokens in snippet */

  /* There must be at least one argument passed to this function (otherwise
  ** the non-overloaded version would have been called instead of this one).
  */
  assert( nVal>=1 );

  if( nVal>6 ){
    sqlite3_result_error(pContext, 
        "wrong number of arguments to function snippet()", -1);
    return;
  }
  if( fts3FunctionArg(pContext, "snippet", apVal[0], &pCsr) ) return;

  switch( nVal ){
    case 6: nToken = sqlite3_value_int(apVal[5]);
    case 5: iCol = sqlite3_value_int(apVal[4]);
    case 4: zEllipsis = (const char*)sqlite3_value_text(apVal[3]);
    case 3: zEnd = (const char*)sqlite3_value_text(apVal[2]);
    case 2: zStart = (const char*)sqlite3_value_text(apVal[1]);
  }
  if( !zEllipsis || !zEnd || !zStart ){
    sqlite3_result_error_nomem(pContext);
  }else if( SQLITE_OK==fts3CursorSeek(pContext, pCsr) ){
    sqlite3Fts3Snippet(pContext, pCsr, zStart, zEnd, zEllipsis, iCol, nToken);
  }
}

/*
** Implementation of the offsets() function for FTS3
*/
static void fts3OffsetsFunc(
  sqlite3_context *pContext,      /* SQLite function call context */
  int nVal,                       /* Size of argument array */
  sqlite3_value **apVal           /* Array of arguments */
){
  Fts3Cursor *pCsr;               /* Cursor handle passed through apVal[0] */

  UNUSED_PARAMETER(nVal);

  assert( nVal==1 );
  if( fts3FunctionArg(pContext, "offsets", apVal[0], &pCsr) ) return;
  assert( pCsr );
  if( SQLITE_OK==fts3CursorSeek(pContext, pCsr) ){
    sqlite3Fts3Offsets(pContext, pCsr);
  }
}

/* 
** Implementation of the special optimize() function for FTS3. This 
** function merges all segments in the database to a single segment.
** Example usage is:
**
**   SELECT optimize(t) FROM t LIMIT 1;
**
** where 't' is the name of an FTS3 table.
*/
static void fts3OptimizeFunc(
  sqlite3_context *pContext,      /* SQLite function call context */
  int nVal,                       /* Size of argument array */
  sqlite3_value **apVal           /* Array of arguments */
){
  int rc;                         /* Return code */
  Fts3Table *p;                   /* Virtual table handle */
  Fts3Cursor *pCursor;            /* Cursor handle passed through apVal[0] */

  UNUSED_PARAMETER(nVal);

  assert( nVal==1 );
  if( fts3FunctionArg(pContext, "optimize", apVal[0], &pCursor) ) return;
  p = (Fts3Table *)pCursor->base.pVtab;
  assert( p );

  rc = sqlite3Fts3Optimize(p);

  switch( rc ){
    case SQLITE_OK:
      sqlite3_result_text(pContext, "Index optimized", -1, SQLITE_STATIC);
      break;
    case SQLITE_DONE:
      sqlite3_result_text(pContext, "Index already optimal", -1, SQLITE_STATIC);
      break;
    default:
      sqlite3_result_error_code(pContext, rc);
      break;
  }
}

/*
** Implementation of the matchinfo() function for FTS3
*/
static void fts3MatchinfoFunc(
  sqlite3_context *pContext,      /* SQLite function call context */
  int nVal,                       /* Size of argument array */
  sqlite3_value **apVal           /* Array of arguments */
){
  Fts3Cursor *pCsr;               /* Cursor handle passed through apVal[0] */
  assert( nVal==1 || nVal==2 );
  if( SQLITE_OK==fts3FunctionArg(pContext, "matchinfo", apVal[0], &pCsr) ){
    const char *zArg = 0;
    if( nVal>1 ){
      zArg = (const char *)sqlite3_value_text(apVal[1]);
    }
    sqlite3Fts3Matchinfo(pContext, pCsr, zArg);
  }
}

/*
** This routine implements the xFindFunction method for the FTS3
** virtual table.
*/
static int fts3FindFunctionMethod(
  sqlite3_vtab *pVtab,            /* Virtual table handle */
  int nArg,                       /* Number of SQL function arguments */
  const char *zName,              /* Name of SQL function */
  void (**pxFunc)(sqlite3_context*,int,sqlite3_value**), /* OUT: Result */
  void **ppArg                    /* Unused */
){
  struct Overloaded {
    const char *zName;
    void (*xFunc)(sqlite3_context*,int,sqlite3_value**);
  } aOverload[] = {
    { "snippet", fts3SnippetFunc },
    { "offsets", fts3OffsetsFunc },
    { "optimize", fts3OptimizeFunc },
    { "matchinfo", fts3MatchinfoFunc },
  };
  int i;                          /* Iterator variable */

  UNUSED_PARAMETER(pVtab);
  UNUSED_PARAMETER(nArg);
  UNUSED_PARAMETER(ppArg);

  for(i=0; i<SizeofArray(aOverload); i++){
    if( strcmp(zName, aOverload[i].zName)==0 ){
      *pxFunc = aOverload[i].xFunc;
      return 1;
    }
  }

  /* No function of the specified name was found. Return 0. */
  return 0;
}

/*
** Implementation of FTS3 xRename method. Rename an fts3 table.
*/
static int fts3RenameMethod(
  sqlite3_vtab *pVtab,            /* Virtual table handle */
  const char *zName               /* New name of table */
){
  Fts3Table *p = (Fts3Table *)pVtab;
  sqlite3 *db = p->db;            /* Database connection */
  int rc;                         /* Return Code */

  /* As it happens, the pending terms table is always empty here. This is
  ** because an "ALTER TABLE RENAME TABLE" statement inside a transaction 
  ** always opens a savepoint transaction. And the xSavepoint() method 
  ** flushes the pending terms table. But leave the (no-op) call to
  ** PendingTermsFlush() in in case that changes.
  */
  assert( p->nPendingData==0 );
  rc = sqlite3Fts3PendingTermsFlush(p);

  if( p->zContentTbl==0 ){
    fts3DbExec(&rc, db,
      "ALTER TABLE %Q.'%q_content'  RENAME TO '%q_content';",
      p->zDb, p->zName, zName
    );
  }

  if( p->bHasDocsize ){
    fts3DbExec(&rc, db,
      "ALTER TABLE %Q.'%q_docsize'  RENAME TO '%q_docsize';",
      p->zDb, p->zName, zName
    );
  }
  if( p->bHasStat ){
    fts3DbExec(&rc, db,
      "ALTER TABLE %Q.'%q_stat'  RENAME TO '%q_stat';",
      p->zDb, p->zName, zName
    );
  }
  fts3DbExec(&rc, db,
    "ALTER TABLE %Q.'%q_segments' RENAME TO '%q_segments';",
    p->zDb, p->zName, zName
  );
  fts3DbExec(&rc, db,
    "ALTER TABLE %Q.'%q_segdir'   RENAME TO '%q_segdir';",
    p->zDb, p->zName, zName
  );
  return rc;
}

/*
** The xSavepoint() method.
**
** Flush the contents of the pending-terms table to disk.
*/
static int fts3SavepointMethod(sqlite3_vtab *pVtab, int iSavepoint){
  UNUSED_PARAMETER(iSavepoint);
  assert( ((Fts3Table *)pVtab)->inTransaction );
  assert( ((Fts3Table *)pVtab)->mxSavepoint < iSavepoint );
  TESTONLY( ((Fts3Table *)pVtab)->mxSavepoint = iSavepoint );
  return fts3SyncMethod(pVtab);
}

/*
** The xRelease() method.
**
** This is a no-op.
*/
static int fts3ReleaseMethod(sqlite3_vtab *pVtab, int iSavepoint){
  TESTONLY( Fts3Table *p = (Fts3Table*)pVtab );
  UNUSED_PARAMETER(iSavepoint);
  UNUSED_PARAMETER(pVtab);
  assert( p->inTransaction );
  assert( p->mxSavepoint >= iSavepoint );
  TESTONLY( p->mxSavepoint = iSavepoint-1 );
  return SQLITE_OK;
}

/*
** The xRollbackTo() method.
**
** Discard the contents of the pending terms table.
*/
static int fts3RollbackToMethod(sqlite3_vtab *pVtab, int iSavepoint){
  Fts3Table *p = (Fts3Table*)pVtab;
  UNUSED_PARAMETER(iSavepoint);
  assert( p->inTransaction );
  assert( p->mxSavepoint >= iSavepoint );
  TESTONLY( p->mxSavepoint = iSavepoint );
  sqlite3Fts3PendingTermsClear(p);
  return SQLITE_OK;
}

static const sqlite3_module fts3Module = {
  /* iVersion      */ 2,
  /* xCreate       */ fts3CreateMethod,
  /* xConnect      */ fts3ConnectMethod,
  /* xBestIndex    */ fts3BestIndexMethod,
  /* xDisconnect   */ fts3DisconnectMethod,
  /* xDestroy      */ fts3DestroyMethod,
  /* xOpen         */ fts3OpenMethod,
  /* xClose        */ fts3CloseMethod,
  /* xFilter       */ fts3FilterMethod,
  /* xNext         */ fts3NextMethod,
  /* xEof          */ fts3EofMethod,
  /* xColumn       */ fts3ColumnMethod,
  /* xRowid        */ fts3RowidMethod,
  /* xUpdate       */ fts3UpdateMethod,
  /* xBegin        */ fts3BeginMethod,
  /* xSync         */ fts3SyncMethod,
  /* xCommit       */ fts3CommitMethod,
  /* xRollback     */ fts3RollbackMethod,
  /* xFindFunction */ fts3FindFunctionMethod,
  /* xRename */       fts3RenameMethod,
  /* xSavepoint    */ fts3SavepointMethod,
  /* xRelease      */ fts3ReleaseMethod,
  /* xRollbackTo   */ fts3RollbackToMethod,
};

/*
** This function is registered as the module destructor (called when an
** FTS3 enabled database connection is closed). It frees the memory
** allocated for the tokenizer hash table.
*/
static void hashDestroy(void *p){
  Fts3Hash *pHash = (Fts3Hash *)p;
  sqlite3Fts3HashClear(pHash);
  sqlite3_free(pHash);
}

/*
** The fts3 built-in tokenizers - "simple", "porter" and "icu"- are 
** implemented in files fts3_tokenizer1.c, fts3_porter.c and fts3_icu.c
** respectively. The following three forward declarations are for functions
** declared in these files used to retrieve the respective implementations.
**
** Calling sqlite3Fts3SimpleTokenizerModule() sets the value pointed
** to by the argument to point to the "simple" tokenizer implementation.
** And so on.
*/
SQLITE_PRIVATE void sqlite3Fts3SimpleTokenizerModule(sqlite3_tokenizer_module const**ppModule);
SQLITE_PRIVATE void sqlite3Fts3PorterTokenizerModule(sqlite3_tokenizer_module const**ppModule);
#ifdef SQLITE_ENABLE_ICU
SQLITE_PRIVATE void sqlite3Fts3IcuTokenizerModule(sqlite3_tokenizer_module const**ppModule);
#endif

/*
** Initialise the fts3 extension. If this extension is built as part
** of the sqlite library, then this function is called directly by
** SQLite. If fts3 is built as a dynamically loadable extension, this
** function is called by the sqlite3_extension_init() entry point.
*/
SQLITE_PRIVATE int sqlite3Fts3Init(sqlite3 *db){
  int rc = SQLITE_OK;
  Fts3Hash *pHash = 0;
  const sqlite3_tokenizer_module *pSimple = 0;
  const sqlite3_tokenizer_module *pPorter = 0;

#ifdef SQLITE_ENABLE_ICU
  const sqlite3_tokenizer_module *pIcu = 0;
  sqlite3Fts3IcuTokenizerModule(&pIcu);
#endif

#ifdef SQLITE_TEST
  rc = sqlite3Fts3InitTerm(db);
  if( rc!=SQLITE_OK ) return rc;
#endif

  rc = sqlite3Fts3InitAux(db);
  if( rc!=SQLITE_OK ) return rc;

  sqlite3Fts3SimpleTokenizerModule(&pSimple);
  sqlite3Fts3PorterTokenizerModule(&pPorter);

  /* Allocate and initialise the hash-table used to store tokenizers. */
  pHash = sqlite3_malloc(sizeof(Fts3Hash));
  if( !pHash ){
    rc = SQLITE_NOMEM;
  }else{
    sqlite3Fts3HashInit(pHash, FTS3_HASH_STRING, 1);
  }

  /* Load the built-in tokenizers into the hash table */
  if( rc==SQLITE_OK ){
    if( sqlite3Fts3HashInsert(pHash, "simple", 7, (void *)pSimple)
     || sqlite3Fts3HashInsert(pHash, "porter", 7, (void *)pPorter) 
#ifdef SQLITE_ENABLE_ICU
     || (pIcu && sqlite3Fts3HashInsert(pHash, "icu", 4, (void *)pIcu))
#endif
    ){
      rc = SQLITE_NOMEM;
    }
  }

#ifdef SQLITE_TEST
  if( rc==SQLITE_OK ){
    rc = sqlite3Fts3ExprInitTestInterface(db);
  }
#endif

  /* Create the virtual table wrapper around the hash-table and overload 
  ** the two scalar functions. If this is successful, register the
  ** module with sqlite.
  */
  if( SQLITE_OK==rc 
   && SQLITE_OK==(rc = sqlite3Fts3InitHashTable(db, pHash, "fts3_tokenizer"))
   && SQLITE_OK==(rc = sqlite3_overload_function(db, "snippet", -1))
   && SQLITE_OK==(rc = sqlite3_overload_function(db, "offsets", 1))
   && SQLITE_OK==(rc = sqlite3_overload_function(db, "matchinfo", 1))
   && SQLITE_OK==(rc = sqlite3_overload_function(db, "matchinfo", 2))
   && SQLITE_OK==(rc = sqlite3_overload_function(db, "optimize", 1))
  ){
    rc = sqlite3_create_module_v2(
        db, "fts3", &fts3Module, (void *)pHash, hashDestroy
    );
    if( rc==SQLITE_OK ){
      rc = sqlite3_create_module_v2(
          db, "fts4", &fts3Module, (void *)pHash, 0
      );
    }
    return rc;
  }

  /* An error has occurred. Delete the hash table and return the error code. */
  assert( rc!=SQLITE_OK );
  if( pHash ){
    sqlite3Fts3HashClear(pHash);
    sqlite3_free(pHash);
  }
  return rc;
}

/*
** Allocate an Fts3MultiSegReader for each token in the expression headed
** by pExpr. 
**
** An Fts3SegReader object is a cursor that can seek or scan a range of
** entries within a single segment b-tree. An Fts3MultiSegReader uses multiple
** Fts3SegReader objects internally to provide an interface to seek or scan
** within the union of all segments of a b-tree. Hence the name.
**
** If the allocated Fts3MultiSegReader just seeks to a single entry in a
** segment b-tree (if the term is not a prefix or it is a prefix for which
** there exists prefix b-tree of the right length) then it may be traversed
** and merged incrementally. Otherwise, it has to be merged into an in-memory 
** doclist and then traversed.
*/
static void fts3EvalAllocateReaders(
  Fts3Cursor *pCsr,               /* FTS cursor handle */
  Fts3Expr *pExpr,                /* Allocate readers for this expression */
  int *pnToken,                   /* OUT: Total number of tokens in phrase. */
  int *pnOr,                      /* OUT: Total number of OR nodes in expr. */
  int *pRc                        /* IN/OUT: Error code */
){
  if( pExpr && SQLITE_OK==*pRc ){
    if( pExpr->eType==FTSQUERY_PHRASE ){
      int i;
      int nToken = pExpr->pPhrase->nToken;
      *pnToken += nToken;
      for(i=0; i<nToken; i++){
        Fts3PhraseToken *pToken = &pExpr->pPhrase->aToken[i];
        int rc = fts3TermSegReaderCursor(pCsr, 
            pToken->z, pToken->n, pToken->isPrefix, &pToken->pSegcsr
        );
        if( rc!=SQLITE_OK ){
          *pRc = rc;
          return;
        }
      }
      assert( pExpr->pPhrase->iDoclistToken==0 );
      pExpr->pPhrase->iDoclistToken = -1;
    }else{
      *pnOr += (pExpr->eType==FTSQUERY_OR);
      fts3EvalAllocateReaders(pCsr, pExpr->pLeft, pnToken, pnOr, pRc);
      fts3EvalAllocateReaders(pCsr, pExpr->pRight, pnToken, pnOr, pRc);
    }
  }
}

/*
** Arguments pList/nList contain the doclist for token iToken of phrase p.
** It is merged into the main doclist stored in p->doclist.aAll/nAll.
**
** This function assumes that pList points to a buffer allocated using
** sqlite3_malloc(). This function takes responsibility for eventually
** freeing the buffer.
*/
static void fts3EvalPhraseMergeToken(
  Fts3Table *pTab,                /* FTS Table pointer */
  Fts3Phrase *p,                  /* Phrase to merge pList/nList into */
  int iToken,                     /* Token pList/nList corresponds to */
  char *pList,                    /* Pointer to doclist */
  int nList                       /* Number of bytes in pList */
){
  assert( iToken!=p->iDoclistToken );

  if( pList==0 ){
    sqlite3_free(p->doclist.aAll);
    p->doclist.aAll = 0;
    p->doclist.nAll = 0;
  }

  else if( p->iDoclistToken<0 ){
    p->doclist.aAll = pList;
    p->doclist.nAll = nList;
  }

  else if( p->doclist.aAll==0 ){
    sqlite3_free(pList);
  }

  else {
    char *pLeft;
    char *pRight;
    int nLeft;
    int nRight;
    int nDiff;

    if( p->iDoclistToken<iToken ){
      pLeft = p->doclist.aAll;
      nLeft = p->doclist.nAll;
      pRight = pList;
      nRight = nList;
      nDiff = iToken - p->iDoclistToken;
    }else{
      pRight = p->doclist.aAll;
      nRight = p->doclist.nAll;
      pLeft = pList;
      nLeft = nList;
      nDiff = p->iDoclistToken - iToken;
    }

    fts3DoclistPhraseMerge(pTab->bDescIdx, nDiff, pLeft, nLeft, pRight,&nRight);
    sqlite3_free(pLeft);
    p->doclist.aAll = pRight;
    p->doclist.nAll = nRight;
  }

  if( iToken>p->iDoclistToken ) p->iDoclistToken = iToken;
}

/*
** Load the doclist for phrase p into p->doclist.aAll/nAll. The loaded doclist
** does not take deferred tokens into account.
**
** SQLITE_OK is returned if no error occurs, otherwise an SQLite error code.
*/
static int fts3EvalPhraseLoad(
  Fts3Cursor *pCsr,               /* FTS Cursor handle */
  Fts3Phrase *p                   /* Phrase object */
){
  Fts3Table *pTab = (Fts3Table *)pCsr->base.pVtab;
  int iToken;
  int rc = SQLITE_OK;

  for(iToken=0; rc==SQLITE_OK && iToken<p->nToken; iToken++){
    Fts3PhraseToken *pToken = &p->aToken[iToken];
    assert( pToken->pDeferred==0 || pToken->pSegcsr==0 );

    if( pToken->pSegcsr ){
      int nThis = 0;
      char *pThis = 0;
      rc = fts3TermSelect(pTab, pToken, p->iColumn, &nThis, &pThis);
      if( rc==SQLITE_OK ){
        fts3EvalPhraseMergeToken(pTab, p, iToken, pThis, nThis);
      }
    }
    assert( pToken->pSegcsr==0 );
  }

  return rc;
}

/*
** This function is called on each phrase after the position lists for
** any deferred tokens have been loaded into memory. It updates the phrases
** current position list to include only those positions that are really
** instances of the phrase (after considering deferred tokens). If this
** means that the phrase does not appear in the current row, doclist.pList
** and doclist.nList are both zeroed.
**
** SQLITE_OK is returned if no error occurs, otherwise an SQLite error code.
*/
static int fts3EvalDeferredPhrase(Fts3Cursor *pCsr, Fts3Phrase *pPhrase){
  int iToken;                     /* Used to iterate through phrase tokens */
  char *aPoslist = 0;             /* Position list for deferred tokens */
  int nPoslist = 0;               /* Number of bytes in aPoslist */
  int iPrev = -1;                 /* Token number of previous deferred token */

  assert( pPhrase->doclist.bFreeList==0 );

  for(iToken=0; iToken<pPhrase->nToken; iToken++){
    Fts3PhraseToken *pToken = &pPhrase->aToken[iToken];
    Fts3DeferredToken *pDeferred = pToken->pDeferred;

    if( pDeferred ){
      char *pList;
      int nList;
      int rc = sqlite3Fts3DeferredTokenList(pDeferred, &pList, &nList);
      if( rc!=SQLITE_OK ) return rc;

      if( pList==0 ){
        sqlite3_free(aPoslist);
        pPhrase->doclist.pList = 0;
        pPhrase->doclist.nList = 0;
        return SQLITE_OK;

      }else if( aPoslist==0 ){
        aPoslist = pList;
        nPoslist = nList;

      }else{
        char *aOut = pList;
        char *p1 = aPoslist;
        char *p2 = aOut;

        assert( iPrev>=0 );
        fts3PoslistPhraseMerge(&aOut, iToken-iPrev, 0, 1, &p1, &p2);
        sqlite3_free(aPoslist);
        aPoslist = pList;
        nPoslist = aOut - aPoslist;
        if( nPoslist==0 ){
          sqlite3_free(aPoslist);
          pPhrase->doclist.pList = 0;
          pPhrase->doclist.nList = 0;
          return SQLITE_OK;
        }
      }
      iPrev = iToken;
    }
  }

  if( iPrev>=0 ){
    int nMaxUndeferred = pPhrase->iDoclistToken;
    if( nMaxUndeferred<0 ){
      pPhrase->doclist.pList = aPoslist;
      pPhrase->doclist.nList = nPoslist;
      pPhrase->doclist.iDocid = pCsr->iPrevId;
      pPhrase->doclist.bFreeList = 1;
    }else{
      int nDistance;
      char *p1;
      char *p2;
      char *aOut;

      if( nMaxUndeferred>iPrev ){
        p1 = aPoslist;
        p2 = pPhrase->doclist.pList;
        nDistance = nMaxUndeferred - iPrev;
      }else{
        p1 = pPhrase->doclist.pList;
        p2 = aPoslist;
        nDistance = iPrev - nMaxUndeferred;
      }

      aOut = (char *)sqlite3_malloc(nPoslist+8);
      if( !aOut ){
        sqlite3_free(aPoslist);
        return SQLITE_NOMEM;
      }
      
      pPhrase->doclist.pList = aOut;
      if( fts3PoslistPhraseMerge(&aOut, nDistance, 0, 1, &p1, &p2) ){
        pPhrase->doclist.bFreeList = 1;
        pPhrase->doclist.nList = (aOut - pPhrase->doclist.pList);
      }else{
        sqlite3_free(aOut);
        pPhrase->doclist.pList = 0;
        pPhrase->doclist.nList = 0;
      }
      sqlite3_free(aPoslist);
    }
  }

  return SQLITE_OK;
}

/*
** This function is called for each Fts3Phrase in a full-text query 
** expression to initialize the mechanism for returning rows. Once this
** function has been called successfully on an Fts3Phrase, it may be
** used with fts3EvalPhraseNext() to iterate through the matching docids.
**
** If parameter bOptOk is true, then the phrase may (or may not) use the
** incremental loading strategy. Otherwise, the entire doclist is loaded into
** memory within this call.
**
** SQLITE_OK is returned if no error occurs, otherwise an SQLite error code.
*/
static int fts3EvalPhraseStart(Fts3Cursor *pCsr, int bOptOk, Fts3Phrase *p){
  int rc;                         /* Error code */
  Fts3PhraseToken *pFirst = &p->aToken[0];
  Fts3Table *pTab = (Fts3Table *)pCsr->base.pVtab;

  if( pCsr->bDesc==pTab->bDescIdx 
   && bOptOk==1 
   && p->nToken==1 
   && pFirst->pSegcsr 
   && pFirst->pSegcsr->bLookup 
   && pFirst->bFirst==0
  ){
    /* Use the incremental approach. */
    int iCol = (p->iColumn >= pTab->nColumn ? -1 : p->iColumn);
    rc = sqlite3Fts3MsrIncrStart(
        pTab, pFirst->pSegcsr, iCol, pFirst->z, pFirst->n);
    p->bIncr = 1;

  }else{
    /* Load the full doclist for the phrase into memory. */
    rc = fts3EvalPhraseLoad(pCsr, p);
    p->bIncr = 0;
  }

  assert( rc!=SQLITE_OK || p->nToken<1 || p->aToken[0].pSegcsr==0 || p->bIncr );
  return rc;
}

/*
** This function is used to iterate backwards (from the end to start) 
** through doclists. It is used by this module to iterate through phrase
** doclists in reverse and by the fts3_write.c module to iterate through
** pending-terms lists when writing to databases with "order=desc".
**
** The doclist may be sorted in ascending (parameter bDescIdx==0) or 
** descending (parameter bDescIdx==1) order of docid. Regardless, this
** function iterates from the end of the doclist to the beginning.
*/
SQLITE_PRIVATE void sqlite3Fts3DoclistPrev(
  int bDescIdx,                   /* True if the doclist is desc */
  char *aDoclist,                 /* Pointer to entire doclist */
  int nDoclist,                   /* Length of aDoclist in bytes */
  char **ppIter,                  /* IN/OUT: Iterator pointer */
  sqlite3_int64 *piDocid,         /* IN/OUT: Docid pointer */
  int *pnList,                    /* IN/OUT: List length pointer */
  u8 *pbEof                       /* OUT: End-of-file flag */
){
  char *p = *ppIter;

  assert( nDoclist>0 );
  assert( *pbEof==0 );
  assert( p || *piDocid==0 );
  assert( !p || (p>aDoclist && p<&aDoclist[nDoclist]) );

  if( p==0 ){
    sqlite3_int64 iDocid = 0;
    char *pNext = 0;
    char *pDocid = aDoclist;
    char *pEnd = &aDoclist[nDoclist];
    int iMul = 1;

    while( pDocid<pEnd ){
      sqlite3_int64 iDelta;
      pDocid += sqlite3Fts3GetVarint(pDocid, &iDelta);
      iDocid += (iMul * iDelta);
      pNext = pDocid;
      fts3PoslistCopy(0, &pDocid);
      while( pDocid<pEnd && *pDocid==0 ) pDocid++;
      iMul = (bDescIdx ? -1 : 1);
    }

    *pnList = pEnd - pNext;
    *ppIter = pNext;
    *piDocid = iDocid;
  }else{
    int iMul = (bDescIdx ? -1 : 1);
    sqlite3_int64 iDelta;
    fts3GetReverseVarint(&p, aDoclist, &iDelta);
    *piDocid -= (iMul * iDelta);

    if( p==aDoclist ){
      *pbEof = 1;
    }else{
      char *pSave = p;
      fts3ReversePoslist(aDoclist, &p);
      *pnList = (pSave - p);
    }
    *ppIter = p;
  }
}

/*
** Attempt to move the phrase iterator to point to the next matching docid. 
** If an error occurs, return an SQLite error code. Otherwise, return 
** SQLITE_OK.
**
** If there is no "next" entry and no error occurs, then *pbEof is set to
** 1 before returning. Otherwise, if no error occurs and the iterator is
** successfully advanced, *pbEof is set to 0.
*/
static int fts3EvalPhraseNext(
  Fts3Cursor *pCsr,               /* FTS Cursor handle */
  Fts3Phrase *p,                  /* Phrase object to advance to next docid */
  u8 *pbEof                       /* OUT: Set to 1 if EOF */
){
  int rc = SQLITE_OK;
  Fts3Doclist *pDL = &p->doclist;
  Fts3Table *pTab = (Fts3Table *)pCsr->base.pVtab;

  if( p->bIncr ){
    assert( p->nToken==1 );
    assert( pDL->pNextDocid==0 );
    rc = sqlite3Fts3MsrIncrNext(pTab, p->aToken[0].pSegcsr, 
        &pDL->iDocid, &pDL->pList, &pDL->nList
    );
    if( rc==SQLITE_OK && !pDL->pList ){
      *pbEof = 1;
    }
  }else if( pCsr->bDesc!=pTab->bDescIdx && pDL->nAll ){
    sqlite3Fts3DoclistPrev(pTab->bDescIdx, pDL->aAll, pDL->nAll, 
        &pDL->pNextDocid, &pDL->iDocid, &pDL->nList, pbEof
    );
    pDL->pList = pDL->pNextDocid;
  }else{
    char *pIter;                            /* Used to iterate through aAll */
    char *pEnd = &pDL->aAll[pDL->nAll];     /* 1 byte past end of aAll */
    if( pDL->pNextDocid ){
      pIter = pDL->pNextDocid;
    }else{
      pIter = pDL->aAll;
    }

    if( pIter>=pEnd ){
      /* We have already reached the end of this doclist. EOF. */
      *pbEof = 1;
    }else{
      sqlite3_int64 iDelta;
      pIter += sqlite3Fts3GetVarint(pIter, &iDelta);
      if( pTab->bDescIdx==0 || pDL->pNextDocid==0 ){
        pDL->iDocid += iDelta;
      }else{
        pDL->iDocid -= iDelta;
      }
      pDL->pList = pIter;
      fts3PoslistCopy(0, &pIter);
      pDL->nList = (pIter - pDL->pList);

      /* pIter now points just past the 0x00 that terminates the position-
      ** list for document pDL->iDocid. However, if this position-list was
      ** edited in place by fts3EvalNearTrim(), then pIter may not actually
      ** point to the start of the next docid value. The following line deals
      ** with this case by advancing pIter past the zero-padding added by
      ** fts3EvalNearTrim().  */
      while( pIter<pEnd && *pIter==0 ) pIter++;

      pDL->pNextDocid = pIter;
      assert( pIter>=&pDL->aAll[pDL->nAll] || *pIter );
      *pbEof = 0;
    }
  }

  return rc;
}

/*
**
** If *pRc is not SQLITE_OK when this function is called, it is a no-op.
** Otherwise, fts3EvalPhraseStart() is called on all phrases within the
** expression. Also the Fts3Expr.bDeferred variable is set to true for any
** expressions for which all descendent tokens are deferred.
**
** If parameter bOptOk is zero, then it is guaranteed that the
** Fts3Phrase.doclist.aAll/nAll variables contain the entire doclist for
** each phrase in the expression (subject to deferred token processing).
** Or, if bOptOk is non-zero, then one or more tokens within the expression
** may be loaded incrementally, meaning doclist.aAll/nAll is not available.
**
** If an error occurs within this function, *pRc is set to an SQLite error
** code before returning.
*/
static void fts3EvalStartReaders(
  Fts3Cursor *pCsr,               /* FTS Cursor handle */
  Fts3Expr *pExpr,                /* Expression to initialize phrases in */
  int bOptOk,                     /* True to enable incremental loading */
  int *pRc                        /* IN/OUT: Error code */
){
  if( pExpr && SQLITE_OK==*pRc ){
    if( pExpr->eType==FTSQUERY_PHRASE ){
      int i;
      int nToken = pExpr->pPhrase->nToken;
      for(i=0; i<nToken; i++){
        if( pExpr->pPhrase->aToken[i].pDeferred==0 ) break;
      }
      pExpr->bDeferred = (i==nToken);
      *pRc = fts3EvalPhraseStart(pCsr, bOptOk, pExpr->pPhrase);
    }else{
      fts3EvalStartReaders(pCsr, pExpr->pLeft, bOptOk, pRc);
      fts3EvalStartReaders(pCsr, pExpr->pRight, bOptOk, pRc);
      pExpr->bDeferred = (pExpr->pLeft->bDeferred && pExpr->pRight->bDeferred);
    }
  }
}

/*
** An array of the following structures is assembled as part of the process
** of selecting tokens to defer before the query starts executing (as part
** of the xFilter() method). There is one element in the array for each
** token in the FTS expression.
**
** Tokens are divided into AND/NEAR clusters. All tokens in a cluster belong
** to phrases that are connected only by AND and NEAR operators (not OR or
** NOT). When determining tokens to defer, each AND/NEAR cluster is considered
** separately. The root of a tokens AND/NEAR cluster is stored in 
** Fts3TokenAndCost.pRoot.
*/
typedef struct Fts3TokenAndCost Fts3TokenAndCost;
struct Fts3TokenAndCost {
  Fts3Phrase *pPhrase;            /* The phrase the token belongs to */
  int iToken;                     /* Position of token in phrase */
  Fts3PhraseToken *pToken;        /* The token itself */
  Fts3Expr *pRoot;                /* Root of NEAR/AND cluster */
  int nOvfl;                      /* Number of overflow pages to load doclist */
  int iCol;                       /* The column the token must match */
};

/*
** This function is used to populate an allocated Fts3TokenAndCost array.
**
** If *pRc is not SQLITE_OK when this function is called, it is a no-op.
** Otherwise, if an error occurs during execution, *pRc is set to an
** SQLite error code.
*/
static void fts3EvalTokenCosts(
  Fts3Cursor *pCsr,               /* FTS Cursor handle */
  Fts3Expr *pRoot,                /* Root of current AND/NEAR cluster */
  Fts3Expr *pExpr,                /* Expression to consider */
  Fts3TokenAndCost **ppTC,        /* Write new entries to *(*ppTC)++ */
  Fts3Expr ***ppOr,               /* Write new OR root to *(*ppOr)++ */
  int *pRc                        /* IN/OUT: Error code */
){
  if( *pRc==SQLITE_OK ){
    if( pExpr->eType==FTSQUERY_PHRASE ){
      Fts3Phrase *pPhrase = pExpr->pPhrase;
      int i;
      for(i=0; *pRc==SQLITE_OK && i<pPhrase->nToken; i++){
        Fts3TokenAndCost *pTC = (*ppTC)++;
        pTC->pPhrase = pPhrase;
        pTC->iToken = i;
        pTC->pRoot = pRoot;
        pTC->pToken = &pPhrase->aToken[i];
        pTC->iCol = pPhrase->iColumn;
        *pRc = sqlite3Fts3MsrOvfl(pCsr, pTC->pToken->pSegcsr, &pTC->nOvfl);
      }
    }else if( pExpr->eType!=FTSQUERY_NOT ){
      assert( pExpr->eType==FTSQUERY_OR
           || pExpr->eType==FTSQUERY_AND
           || pExpr->eType==FTSQUERY_NEAR
      );
      assert( pExpr->pLeft && pExpr->pRight );
      if( pExpr->eType==FTSQUERY_OR ){
        pRoot = pExpr->pLeft;
        **ppOr = pRoot;
        (*ppOr)++;
      }
      fts3EvalTokenCosts(pCsr, pRoot, pExpr->pLeft, ppTC, ppOr, pRc);
      if( pExpr->eType==FTSQUERY_OR ){
        pRoot = pExpr->pRight;
        **ppOr = pRoot;
        (*ppOr)++;
      }
      fts3EvalTokenCosts(pCsr, pRoot, pExpr->pRight, ppTC, ppOr, pRc);
    }
  }
}

/*
** Determine the average document (row) size in pages. If successful,
** write this value to *pnPage and return SQLITE_OK. Otherwise, return
** an SQLite error code.
**
** The average document size in pages is calculated by first calculating 
** determining the average size in bytes, B. If B is less than the amount
** of data that will fit on a single leaf page of an intkey table in
** this database, then the average docsize is 1. Otherwise, it is 1 plus
** the number of overflow pages consumed by a record B bytes in size.
*/
static int fts3EvalAverageDocsize(Fts3Cursor *pCsr, int *pnPage){
  if( pCsr->nRowAvg==0 ){
    /* The average document size, which is required to calculate the cost
    ** of each doclist, has not yet been determined. Read the required 
    ** data from the %_stat table to calculate it.
    **
    ** Entry 0 of the %_stat table is a blob containing (nCol+1) FTS3 
    ** varints, where nCol is the number of columns in the FTS3 table.
    ** The first varint is the number of documents currently stored in
    ** the table. The following nCol varints contain the total amount of
    ** data stored in all rows of each column of the table, from left
    ** to right.
    */
    int rc;
    Fts3Table *p = (Fts3Table*)pCsr->base.pVtab;
    sqlite3_stmt *pStmt;
    sqlite3_int64 nDoc = 0;
    sqlite3_int64 nByte = 0;
    const char *pEnd;
    const char *a;

    rc = sqlite3Fts3SelectDoctotal(p, &pStmt);
    if( rc!=SQLITE_OK ) return rc;
    a = sqlite3_column_blob(pStmt, 0);
    assert( a );

    pEnd = &a[sqlite3_column_bytes(pStmt, 0)];
    a += sqlite3Fts3GetVarint(a, &nDoc);
    while( a<pEnd ){
      a += sqlite3Fts3GetVarint(a, &nByte);
    }
    if( nDoc==0 || nByte==0 ){
      sqlite3_reset(pStmt);
      return FTS_CORRUPT_VTAB;
    }

    pCsr->nDoc = nDoc;
    pCsr->nRowAvg = (int)(((nByte / nDoc) + p->nPgsz) / p->nPgsz);
    assert( pCsr->nRowAvg>0 ); 
    rc = sqlite3_reset(pStmt);
    if( rc!=SQLITE_OK ) return rc;
  }

  *pnPage = pCsr->nRowAvg;
  return SQLITE_OK;
}

/*
** This function is called to select the tokens (if any) that will be 
** deferred. The array aTC[] has already been populated when this is
** called.
**
** This function is called once for each AND/NEAR cluster in the 
** expression. Each invocation determines which tokens to defer within
** the cluster with root node pRoot. See comments above the definition
** of struct Fts3TokenAndCost for more details.
**
** If no error occurs, SQLITE_OK is returned and sqlite3Fts3DeferToken()
** called on each token to defer. Otherwise, an SQLite error code is
** returned.
*/
static int fts3EvalSelectDeferred(
  Fts3Cursor *pCsr,               /* FTS Cursor handle */
  Fts3Expr *pRoot,                /* Consider tokens with this root node */
  Fts3TokenAndCost *aTC,          /* Array of expression tokens and costs */
  int nTC                         /* Number of entries in aTC[] */
){
  Fts3Table *pTab = (Fts3Table *)pCsr->base.pVtab;
  int nDocSize = 0;               /* Number of pages per doc loaded */
  int rc = SQLITE_OK;             /* Return code */
  int ii;                         /* Iterator variable for various purposes */
  int nOvfl = 0;                  /* Total overflow pages used by doclists */
  int nToken = 0;                 /* Total number of tokens in cluster */

  int nMinEst = 0;                /* The minimum count for any phrase so far. */
  int nLoad4 = 1;                 /* (Phrases that will be loaded)^4. */

  /* Tokens are never deferred for FTS tables created using the content=xxx
  ** option. The reason being that it is not guaranteed that the content
  ** table actually contains the same data as the index. To prevent this from
  ** causing any problems, the deferred token optimization is completely
  ** disabled for content=xxx tables. */
  if( pTab->zContentTbl ){
    return SQLITE_OK;
  }

  /* Count the tokens in this AND/NEAR cluster. If none of the doclists
  ** associated with the tokens spill onto overflow pages, or if there is
  ** only 1 token, exit early. No tokens to defer in this case. */
  for(ii=0; ii<nTC; ii++){
    if( aTC[ii].pRoot==pRoot ){
      nOvfl += aTC[ii].nOvfl;
      nToken++;
    }
  }
  if( nOvfl==0 || nToken<2 ) return SQLITE_OK;

  /* Obtain the average docsize (in pages). */
  rc = fts3EvalAverageDocsize(pCsr, &nDocSize);
  assert( rc!=SQLITE_OK || nDocSize>0 );


  /* Iterate through all tokens in this AND/NEAR cluster, in ascending order 
  ** of the number of overflow pages that will be loaded by the pager layer 
  ** to retrieve the entire doclist for the token from the full-text index.
  ** Load the doclists for tokens that are either:
  **
  **   a. The cheapest token in the entire query (i.e. the one visited by the
  **      first iteration of this loop), or
  **
  **   b. Part of a multi-token phrase.
  **
  ** After each token doclist is loaded, merge it with the others from the
  ** same phrase and count the number of documents that the merged doclist
  ** contains. Set variable "nMinEst" to the smallest number of documents in 
  ** any phrase doclist for which 1 or more token doclists have been loaded.
  ** Let nOther be the number of other phrases for which it is certain that
  ** one or more tokens will not be deferred.
  **
  ** Then, for each token, defer it if loading the doclist would result in
  ** loading N or more overflow pages into memory, where N is computed as:
  **
  **    (nMinEst + 4^nOther - 1) / (4^nOther)
  */
  for(ii=0; ii<nToken && rc==SQLITE_OK; ii++){
    int iTC;                      /* Used to iterate through aTC[] array. */
    Fts3TokenAndCost *pTC = 0;    /* Set to cheapest remaining token. */

    /* Set pTC to point to the cheapest remaining token. */
    for(iTC=0; iTC<nTC; iTC++){
      if( aTC[iTC].pToken && aTC[iTC].pRoot==pRoot 
       && (!pTC || aTC[iTC].nOvfl<pTC->nOvfl) 
      ){
        pTC = &aTC[iTC];
      }
    }
    assert( pTC );

    if( ii && pTC->nOvfl>=((nMinEst+(nLoad4/4)-1)/(nLoad4/4))*nDocSize ){
      /* The number of overflow pages to load for this (and therefore all
      ** subsequent) tokens is greater than the estimated number of pages 
      ** that will be loaded if all subsequent tokens are deferred.
      */
      Fts3PhraseToken *pToken = pTC->pToken;
      rc = sqlite3Fts3DeferToken(pCsr, pToken, pTC->iCol);
      fts3SegReaderCursorFree(pToken->pSegcsr);
      pToken->pSegcsr = 0;
    }else{
      /* Set nLoad4 to the value of (4^nOther) for the next iteration of the
      ** for-loop. Except, limit the value to 2^24 to prevent it from 
      ** overflowing the 32-bit integer it is stored in. */
      if( ii<12 ) nLoad4 = nLoad4*4;

      if( ii==0 || pTC->pPhrase->nToken>1 ){
        /* Either this is the cheapest token in the entire query, or it is
        ** part of a multi-token phrase. Either way, the entire doclist will
        ** (eventually) be loaded into memory. It may as well be now. */
        Fts3PhraseToken *pToken = pTC->pToken;
        int nList = 0;
        char *pList = 0;
        rc = fts3TermSelect(pTab, pToken, pTC->iCol, &nList, &pList);
        assert( rc==SQLITE_OK || pList==0 );
        if( rc==SQLITE_OK ){
          int nCount;
          fts3EvalPhraseMergeToken(pTab, pTC->pPhrase, pTC->iToken,pList,nList);
          nCount = fts3DoclistCountDocids(
              pTC->pPhrase->doclist.aAll, pTC->pPhrase->doclist.nAll
          );
          if( ii==0 || nCount<nMinEst ) nMinEst = nCount;
        }
      }
    }
    pTC->pToken = 0;
  }

  return rc;
}

/*
** This function is called from within the xFilter method. It initializes
** the full-text query currently stored in pCsr->pExpr. To iterate through
** the results of a query, the caller does:
**
**    fts3EvalStart(pCsr);
**    while( 1 ){
**      fts3EvalNext(pCsr);
**      if( pCsr->bEof ) break;
**      ... return row pCsr->iPrevId to the caller ...
**    }
*/
static int fts3EvalStart(Fts3Cursor *pCsr){
  Fts3Table *pTab = (Fts3Table *)pCsr->base.pVtab;
  int rc = SQLITE_OK;
  int nToken = 0;
  int nOr = 0;

  /* Allocate a MultiSegReader for each token in the expression. */
  fts3EvalAllocateReaders(pCsr, pCsr->pExpr, &nToken, &nOr, &rc);

  /* Determine which, if any, tokens in the expression should be deferred. */
  if( rc==SQLITE_OK && nToken>1 && pTab->bHasStat ){
    Fts3TokenAndCost *aTC;
    Fts3Expr **apOr;
    aTC = (Fts3TokenAndCost *)sqlite3_malloc(
        sizeof(Fts3TokenAndCost) * nToken
      + sizeof(Fts3Expr *) * nOr * 2
    );
    apOr = (Fts3Expr **)&aTC[nToken];

    if( !aTC ){
      rc = SQLITE_NOMEM;
    }else{
      int ii;
      Fts3TokenAndCost *pTC = aTC;
      Fts3Expr **ppOr = apOr;

      fts3EvalTokenCosts(pCsr, 0, pCsr->pExpr, &pTC, &ppOr, &rc);
      nToken = pTC-aTC;
      nOr = ppOr-apOr;

      if( rc==SQLITE_OK ){
        rc = fts3EvalSelectDeferred(pCsr, 0, aTC, nToken);
        for(ii=0; rc==SQLITE_OK && ii<nOr; ii++){
          rc = fts3EvalSelectDeferred(pCsr, apOr[ii], aTC, nToken);
        }
      }

      sqlite3_free(aTC);
    }
  }

  fts3EvalStartReaders(pCsr, pCsr->pExpr, 1, &rc);
  return rc;
}

/*
** Invalidate the current position list for phrase pPhrase.
*/
static void fts3EvalInvalidatePoslist(Fts3Phrase *pPhrase){
  if( pPhrase->doclist.bFreeList ){
    sqlite3_free(pPhrase->doclist.pList);
  }
  pPhrase->doclist.pList = 0;
  pPhrase->doclist.nList = 0;
  pPhrase->doclist.bFreeList = 0;
}

/*
** This function is called to edit the position list associated with
** the phrase object passed as the fifth argument according to a NEAR
** condition. For example:
**
**     abc NEAR/5 "def ghi"
**
** Parameter nNear is passed the NEAR distance of the expression (5 in
** the example above). When this function is called, *paPoslist points to
** the position list, and *pnToken is the number of phrase tokens in, the
** phrase on the other side of the NEAR operator to pPhrase. For example,
** if pPhrase refers to the "def ghi" phrase, then *paPoslist points to
** the position list associated with phrase "abc".
**
** All positions in the pPhrase position list that are not sufficiently
** close to a position in the *paPoslist position list are removed. If this
** leaves 0 positions, zero is returned. Otherwise, non-zero.
**
** Before returning, *paPoslist is set to point to the position lsit 
** associated with pPhrase. And *pnToken is set to the number of tokens in
** pPhrase.
*/
static int fts3EvalNearTrim(
  int nNear,                      /* NEAR distance. As in "NEAR/nNear". */
  char *aTmp,                     /* Temporary space to use */
  char **paPoslist,               /* IN/OUT: Position list */
  int *pnToken,                   /* IN/OUT: Tokens in phrase of *paPoslist */
  Fts3Phrase *pPhrase             /* The phrase object to trim the doclist of */
){
  int nParam1 = nNear + pPhrase->nToken;
  int nParam2 = nNear + *pnToken;
  int nNew;
  char *p2; 
  char *pOut; 
  int res;

  assert( pPhrase->doclist.pList );

  p2 = pOut = pPhrase->doclist.pList;
  res = fts3PoslistNearMerge(
    &pOut, aTmp, nParam1, nParam2, paPoslist, &p2
  );
  if( res ){
    nNew = (pOut - pPhrase->doclist.pList) - 1;
    assert( pPhrase->doclist.pList[nNew]=='\0' );
    assert( nNew<=pPhrase->doclist.nList && nNew>0 );
    memset(&pPhrase->doclist.pList[nNew], 0, pPhrase->doclist.nList - nNew);
    pPhrase->doclist.nList = nNew;
    *paPoslist = pPhrase->doclist.pList;
    *pnToken = pPhrase->nToken;
  }

  return res;
}

/*
** This function is a no-op if *pRc is other than SQLITE_OK when it is called.
** Otherwise, it advances the expression passed as the second argument to
** point to the next matching row in the database. Expressions iterate through
** matching rows in docid order. Ascending order if Fts3Cursor.bDesc is zero,
** or descending if it is non-zero.
**
** If an error occurs, *pRc is set to an SQLite error code. Otherwise, if
** successful, the following variables in pExpr are set:
**
**   Fts3Expr.bEof                (non-zero if EOF - there is no next row)
**   Fts3Expr.iDocid              (valid if bEof==0. The docid of the next row)
**
** If the expression is of type FTSQUERY_PHRASE, and the expression is not
** at EOF, then the following variables are populated with the position list
** for the phrase for the visited row:
**
**   FTs3Expr.pPhrase->doclist.nList        (length of pList in bytes)
**   FTs3Expr.pPhrase->doclist.pList        (pointer to position list)
**
** It says above that this function advances the expression to the next
** matching row. This is usually true, but there are the following exceptions:
**
**   1. Deferred tokens are not taken into account. If a phrase consists
**      entirely of deferred tokens, it is assumed to match every row in
**      the db. In this case the position-list is not populated at all. 
**
**      Or, if a phrase contains one or more deferred tokens and one or
**      more non-deferred tokens, then the expression is advanced to the 
**      next possible match, considering only non-deferred tokens. In other
**      words, if the phrase is "A B C", and "B" is deferred, the expression
**      is advanced to the next row that contains an instance of "A * C", 
**      where "*" may match any single token. The position list in this case
**      is populated as for "A * C" before returning.
**
**   2. NEAR is treated as AND. If the expression is "x NEAR y", it is 
**      advanced to point to the next row that matches "x AND y".
** 
** See fts3EvalTestDeferredAndNear() for details on testing if a row is
** really a match, taking into account deferred tokens and NEAR operators.
*/
static void fts3EvalNextRow(
  Fts3Cursor *pCsr,               /* FTS Cursor handle */
  Fts3Expr *pExpr,                /* Expr. to advance to next matching row */
  int *pRc                        /* IN/OUT: Error code */
){
  if( *pRc==SQLITE_OK ){
    int bDescDoclist = pCsr->bDesc;         /* Used by DOCID_CMP() macro */
    assert( pExpr->bEof==0 );
    pExpr->bStart = 1;

    switch( pExpr->eType ){
      case FTSQUERY_NEAR:
      case FTSQUERY_AND: {
        Fts3Expr *pLeft = pExpr->pLeft;
        Fts3Expr *pRight = pExpr->pRight;
        assert( !pLeft->bDeferred || !pRight->bDeferred );

        if( pLeft->bDeferred ){
          /* LHS is entirely deferred. So we assume it matches every row.
          ** Advance the RHS iterator to find the next row visited. */
          fts3EvalNextRow(pCsr, pRight, pRc);
          pExpr->iDocid = pRight->iDocid;
          pExpr->bEof = pRight->bEof;
        }else if( pRight->bDeferred ){
          /* RHS is entirely deferred. So we assume it matches every row.
          ** Advance the LHS iterator to find the next row visited. */
          fts3EvalNextRow(pCsr, pLeft, pRc);
          pExpr->iDocid = pLeft->iDocid;
          pExpr->bEof = pLeft->bEof;
        }else{
          /* Neither the RHS or LHS are deferred. */
          fts3EvalNextRow(pCsr, pLeft, pRc);
          fts3EvalNextRow(pCsr, pRight, pRc);
          while( !pLeft->bEof && !pRight->bEof && *pRc==SQLITE_OK ){
            sqlite3_int64 iDiff = DOCID_CMP(pLeft->iDocid, pRight->iDocid);
            if( iDiff==0 ) break;
            if( iDiff<0 ){
              fts3EvalNextRow(pCsr, pLeft, pRc);
            }else{
              fts3EvalNextRow(pCsr, pRight, pRc);
            }
          }
          pExpr->iDocid = pLeft->iDocid;
          pExpr->bEof = (pLeft->bEof || pRight->bEof);
        }
        break;
      }
  
      case FTSQUERY_OR: {
        Fts3Expr *pLeft = pExpr->pLeft;
        Fts3Expr *pRight = pExpr->pRight;
        sqlite3_int64 iCmp = DOCID_CMP(pLeft->iDocid, pRight->iDocid);

        assert( pLeft->bStart || pLeft->iDocid==pRight->iDocid );
        assert( pRight->bStart || pLeft->iDocid==pRight->iDocid );

        if( pRight->bEof || (pLeft->bEof==0 && iCmp<0) ){
          fts3EvalNextRow(pCsr, pLeft, pRc);
        }else if( pLeft->bEof || (pRight->bEof==0 && iCmp>0) ){
          fts3EvalNextRow(pCsr, pRight, pRc);
        }else{
          fts3EvalNextRow(pCsr, pLeft, pRc);
          fts3EvalNextRow(pCsr, pRight, pRc);
        }

        pExpr->bEof = (pLeft->bEof && pRight->bEof);
        iCmp = DOCID_CMP(pLeft->iDocid, pRight->iDocid);
        if( pRight->bEof || (pLeft->bEof==0 &&  iCmp<0) ){
          pExpr->iDocid = pLeft->iDocid;
        }else{
          pExpr->iDocid = pRight->iDocid;
        }

        break;
      }

      case FTSQUERY_NOT: {
        Fts3Expr *pLeft = pExpr->pLeft;
        Fts3Expr *pRight = pExpr->pRight;

        if( pRight->bStart==0 ){
          fts3EvalNextRow(pCsr, pRight, pRc);
          assert( *pRc!=SQLITE_OK || pRight->bStart );
        }

        fts3EvalNextRow(pCsr, pLeft, pRc);
        if( pLeft->bEof==0 ){
          while( !*pRc 
              && !pRight->bEof 
              && DOCID_CMP(pLeft->iDocid, pRight->iDocid)>0 
          ){
            fts3EvalNextRow(pCsr, pRight, pRc);
          }
        }
        pExpr->iDocid = pLeft->iDocid;
        pExpr->bEof = pLeft->bEof;
        break;
      }

      default: {
        Fts3Phrase *pPhrase = pExpr->pPhrase;
        fts3EvalInvalidatePoslist(pPhrase);
        *pRc = fts3EvalPhraseNext(pCsr, pPhrase, &pExpr->bEof);
        pExpr->iDocid = pPhrase->doclist.iDocid;
        break;
      }
    }
  }
}

/*
** If *pRc is not SQLITE_OK, or if pExpr is not the root node of a NEAR
** cluster, then this function returns 1 immediately.
**
** Otherwise, it checks if the current row really does match the NEAR 
** expression, using the data currently stored in the position lists 
** (Fts3Expr->pPhrase.doclist.pList/nList) for each phrase in the expression. 
**
** If the current row is a match, the position list associated with each
** phrase in the NEAR expression is edited in place to contain only those
** phrase instances sufficiently close to their peers to satisfy all NEAR
** constraints. In this case it returns 1. If the NEAR expression does not 
** match the current row, 0 is returned. The position lists may or may not
** be edited if 0 is returned.
*/
static int fts3EvalNearTest(Fts3Expr *pExpr, int *pRc){
  int res = 1;

  /* The following block runs if pExpr is the root of a NEAR query.
  ** For example, the query:
  **
  **         "w" NEAR "x" NEAR "y" NEAR "z"
  **
  ** which is represented in tree form as:
  **
  **                               |
  **                          +--NEAR--+      <-- root of NEAR query
  **                          |        |
  **                     +--NEAR--+   "z"
  **                     |        |
  **                +--NEAR--+   "y"
  **                |        |
  **               "w"      "x"
  **
  ** The right-hand child of a NEAR node is always a phrase. The 
  ** left-hand child may be either a phrase or a NEAR node. There are
  ** no exceptions to this - it's the way the parser in fts3_expr.c works.
  */
  if( *pRc==SQLITE_OK 
   && pExpr->eType==FTSQUERY_NEAR 
   && pExpr->bEof==0
   && (pExpr->pParent==0 || pExpr->pParent->eType!=FTSQUERY_NEAR)
  ){
    Fts3Expr *p; 
    int nTmp = 0;                 /* Bytes of temp space */
    char *aTmp;                   /* Temp space for PoslistNearMerge() */

    /* Allocate temporary working space. */
    for(p=pExpr; p->pLeft; p=p->pLeft){
      nTmp += p->pRight->pPhrase->doclist.nList;
    }
    nTmp += p->pPhrase->doclist.nList;
    aTmp = sqlite3_malloc(nTmp*2);
    if( !aTmp ){
      *pRc = SQLITE_NOMEM;
      res = 0;
    }else{
      char *aPoslist = p->pPhrase->doclist.pList;
      int nToken = p->pPhrase->nToken;

      for(p=p->pParent;res && p && p->eType==FTSQUERY_NEAR; p=p->pParent){
        Fts3Phrase *pPhrase = p->pRight->pPhrase;
        int nNear = p->nNear;
        res = fts3EvalNearTrim(nNear, aTmp, &aPoslist, &nToken, pPhrase);
      }
  
      aPoslist = pExpr->pRight->pPhrase->doclist.pList;
      nToken = pExpr->pRight->pPhrase->nToken;
      for(p=pExpr->pLeft; p && res; p=p->pLeft){
        int nNear;
        Fts3Phrase *pPhrase;
        assert( p->pParent && p->pParent->pLeft==p );
        nNear = p->pParent->nNear;
        pPhrase = (
            p->eType==FTSQUERY_NEAR ? p->pRight->pPhrase : p->pPhrase
        );
        res = fts3EvalNearTrim(nNear, aTmp, &aPoslist, &nToken, pPhrase);
      }
    }

    sqlite3_free(aTmp);
  }

  return res;
}

/*
** This function is a helper function for fts3EvalTestDeferredAndNear().
** Assuming no error occurs or has occurred, It returns non-zero if the
** expression passed as the second argument matches the row that pCsr 
** currently points to, or zero if it does not.
**
** If *pRc is not SQLITE_OK when this function is called, it is a no-op.
** If an error occurs during execution of this function, *pRc is set to 
** the appropriate SQLite error code. In this case the returned value is 
** undefined.
*/
static int fts3EvalTestExpr(
  Fts3Cursor *pCsr,               /* FTS cursor handle */
  Fts3Expr *pExpr,                /* Expr to test. May or may not be root. */
  int *pRc                        /* IN/OUT: Error code */
){
  int bHit = 1;                   /* Return value */
  if( *pRc==SQLITE_OK ){
    switch( pExpr->eType ){
      case FTSQUERY_NEAR:
      case FTSQUERY_AND:
        bHit = (
            fts3EvalTestExpr(pCsr, pExpr->pLeft, pRc)
         && fts3EvalTestExpr(pCsr, pExpr->pRight, pRc)
         && fts3EvalNearTest(pExpr, pRc)
        );

        /* If the NEAR expression does not match any rows, zero the doclist for 
        ** all phrases involved in the NEAR. This is because the snippet(),
        ** offsets() and matchinfo() functions are not supposed to recognize 
        ** any instances of phrases that are part of unmatched NEAR queries. 
        ** For example if this expression:
        **
        **    ... MATCH 'a OR (b NEAR c)'
        **
        ** is matched against a row containing:
        **
        **        'a b d e'
        **
        ** then any snippet() should ony highlight the "a" term, not the "b"
        ** (as "b" is part of a non-matching NEAR clause).
        */
        if( bHit==0 
         && pExpr->eType==FTSQUERY_NEAR 
         && (pExpr->pParent==0 || pExpr->pParent->eType!=FTSQUERY_NEAR)
        ){
          Fts3Expr *p;
          for(p=pExpr; p->pPhrase==0; p=p->pLeft){
            if( p->pRight->iDocid==pCsr->iPrevId ){
              fts3EvalInvalidatePoslist(p->pRight->pPhrase);
            }
          }
          if( p->iDocid==pCsr->iPrevId ){
            fts3EvalInvalidatePoslist(p->pPhrase);
          }
        }

        break;

      case FTSQUERY_OR: {
        int bHit1 = fts3EvalTestExpr(pCsr, pExpr->pLeft, pRc);
        int bHit2 = fts3EvalTestExpr(pCsr, pExpr->pRight, pRc);
        bHit = bHit1 || bHit2;
        break;
      }

      case FTSQUERY_NOT:
        bHit = (
            fts3EvalTestExpr(pCsr, pExpr->pLeft, pRc)
         && !fts3EvalTestExpr(pCsr, pExpr->pRight, pRc)
        );
        break;

      default: {
        if( pCsr->pDeferred 
         && (pExpr->iDocid==pCsr->iPrevId || pExpr->bDeferred)
        ){
          Fts3Phrase *pPhrase = pExpr->pPhrase;
          assert( pExpr->bDeferred || pPhrase->doclist.bFreeList==0 );
          if( pExpr->bDeferred ){
            fts3EvalInvalidatePoslist(pPhrase);
          }
          *pRc = fts3EvalDeferredPhrase(pCsr, pPhrase);
          bHit = (pPhrase->doclist.pList!=0);
          pExpr->iDocid = pCsr->iPrevId;
        }else{
          bHit = (pExpr->bEof==0 && pExpr->iDocid==pCsr->iPrevId);
        }
        break;
      }
    }
  }
  return bHit;
}

/*
** This function is called as the second part of each xNext operation when
** iterating through the results of a full-text query. At this point the
** cursor points to a row that matches the query expression, with the
** following caveats:
**
**   * Up until this point, "NEAR" operators in the expression have been
**     treated as "AND".
**
**   * Deferred tokens have not yet been considered.
**
** If *pRc is not SQLITE_OK when this function is called, it immediately
** returns 0. Otherwise, it tests whether or not after considering NEAR
** operators and deferred tokens the current row is still a match for the
** expression. It returns 1 if both of the following are true:
**
**   1. *pRc is SQLITE_OK when this function returns, and
**
**   2. After scanning the current FTS table row for the deferred tokens,
**      it is determined that the row does *not* match the query.
**
** Or, if no error occurs and it seems the current row does match the FTS
** query, return 0.
*/
static int fts3EvalTestDeferredAndNear(Fts3Cursor *pCsr, int *pRc){
  int rc = *pRc;
  int bMiss = 0;
  if( rc==SQLITE_OK ){

    /* If there are one or more deferred tokens, load the current row into
    ** memory and scan it to determine the position list for each deferred
    ** token. Then, see if this row is really a match, considering deferred
    ** tokens and NEAR operators (neither of which were taken into account
    ** earlier, by fts3EvalNextRow()). 
    */
    if( pCsr->pDeferred ){
      rc = fts3CursorSeek(0, pCsr);
      if( rc==SQLITE_OK ){
        rc = sqlite3Fts3CacheDeferredDoclists(pCsr);
      }
    }
    bMiss = (0==fts3EvalTestExpr(pCsr, pCsr->pExpr, &rc));

    /* Free the position-lists accumulated for each deferred token above. */
    sqlite3Fts3FreeDeferredDoclists(pCsr);
    *pRc = rc;
  }
  return (rc==SQLITE_OK && bMiss);
}

/*
** Advance to the next document that matches the FTS expression in
** Fts3Cursor.pExpr.
*/
static int fts3EvalNext(Fts3Cursor *pCsr){
  int rc = SQLITE_OK;             /* Return Code */
  Fts3Expr *pExpr = pCsr->pExpr;
  assert( pCsr->isEof==0 );
  if( pExpr==0 ){
    pCsr->isEof = 1;
  }else{
    do {
      if( pCsr->isRequireSeek==0 ){
        sqlite3_reset(pCsr->pStmt);
      }
      assert( sqlite3_data_count(pCsr->pStmt)==0 );
      fts3EvalNextRow(pCsr, pExpr, &rc);
      pCsr->isEof = pExpr->bEof;
      pCsr->isRequireSeek = 1;
      pCsr->isMatchinfoNeeded = 1;
      pCsr->iPrevId = pExpr->iDocid;
    }while( pCsr->isEof==0 && fts3EvalTestDeferredAndNear(pCsr, &rc) );
  }
  return rc;
}

/*
** Restart interation for expression pExpr so that the next call to
** fts3EvalNext() visits the first row. Do not allow incremental 
** loading or merging of phrase doclists for this iteration.
**
** If *pRc is other than SQLITE_OK when this function is called, it is
** a no-op. If an error occurs within this function, *pRc is set to an
** SQLite error code before returning.
*/
static void fts3EvalRestart(
  Fts3Cursor *pCsr,
  Fts3Expr *pExpr,
  int *pRc
){
  if( pExpr && *pRc==SQLITE_OK ){
    Fts3Phrase *pPhrase = pExpr->pPhrase;

    if( pPhrase ){
      fts3EvalInvalidatePoslist(pPhrase);
      if( pPhrase->bIncr ){
        assert( pPhrase->nToken==1 );
        assert( pPhrase->aToken[0].pSegcsr );
        sqlite3Fts3MsrIncrRestart(pPhrase->aToken[0].pSegcsr);
        *pRc = fts3EvalPhraseStart(pCsr, 0, pPhrase);
      }

      pPhrase->doclist.pNextDocid = 0;
      pPhrase->doclist.iDocid = 0;
    }

    pExpr->iDocid = 0;
    pExpr->bEof = 0;
    pExpr->bStart = 0;

    fts3EvalRestart(pCsr, pExpr->pLeft, pRc);
    fts3EvalRestart(pCsr, pExpr->pRight, pRc);
  }
}

/*
** After allocating the Fts3Expr.aMI[] array for each phrase in the 
** expression rooted at pExpr, the cursor iterates through all rows matched
** by pExpr, calling this function for each row. This function increments
** the values in Fts3Expr.aMI[] according to the position-list currently
** found in Fts3Expr.pPhrase->doclist.pList for each of the phrase 
** expression nodes.
*/
static void fts3EvalUpdateCounts(Fts3Expr *pExpr){
  if( pExpr ){
    Fts3Phrase *pPhrase = pExpr->pPhrase;
    if( pPhrase && pPhrase->doclist.pList ){
      int iCol = 0;
      char *p = pPhrase->doclist.pList;

      assert( *p );
      while( 1 ){
        u8 c = 0;
        int iCnt = 0;
        while( 0xFE & (*p | c) ){
          if( (c&0x80)==0 ) iCnt++;
          c = *p++ & 0x80;
        }

        /* aMI[iCol*3 + 1] = Number of occurrences
        ** aMI[iCol*3 + 2] = Number of rows containing at least one instance
        */
        pExpr->aMI[iCol*3 + 1] += iCnt;
        pExpr->aMI[iCol*3 + 2] += (iCnt>0);
        if( *p==0x00 ) break;
        p++;
        p += sqlite3Fts3GetVarint32(p, &iCol);
      }
    }

    fts3EvalUpdateCounts(pExpr->pLeft);
    fts3EvalUpdateCounts(pExpr->pRight);
  }
}

/*
** Expression pExpr must be of type FTSQUERY_PHRASE.
**
** If it is not already allocated and populated, this function allocates and
** populates the Fts3Expr.aMI[] array for expression pExpr. If pExpr is part
** of a NEAR expression, then it also allocates and populates the same array
** for all other phrases that are part of the NEAR expression.
**
** SQLITE_OK is returned if the aMI[] array is successfully allocated and
** populated. Otherwise, if an error occurs, an SQLite error code is returned.
*/
static int fts3EvalGatherStats(
  Fts3Cursor *pCsr,               /* Cursor object */
  Fts3Expr *pExpr                 /* FTSQUERY_PHRASE expression */
){
  int rc = SQLITE_OK;             /* Return code */

  assert( pExpr->eType==FTSQUERY_PHRASE );
  if( pExpr->aMI==0 ){
    Fts3Table *pTab = (Fts3Table *)pCsr->base.pVtab;
    Fts3Expr *pRoot;                /* Root of NEAR expression */
    Fts3Expr *p;                    /* Iterator used for several purposes */

    sqlite3_int64 iPrevId = pCsr->iPrevId;
    sqlite3_int64 iDocid;
    u8 bEof;

    /* Find the root of the NEAR expression */
    pRoot = pExpr;
    while( pRoot->pParent && pRoot->pParent->eType==FTSQUERY_NEAR ){
      pRoot = pRoot->pParent;
    }
    iDocid = pRoot->iDocid;
    bEof = pRoot->bEof;
    assert( pRoot->bStart );

    /* Allocate space for the aMSI[] array of each FTSQUERY_PHRASE node */
    for(p=pRoot; p; p=p->pLeft){
      Fts3Expr *pE = (p->eType==FTSQUERY_PHRASE?p:p->pRight);
      assert( pE->aMI==0 );
      pE->aMI = (u32 *)sqlite3_malloc(pTab->nColumn * 3 * sizeof(u32));
      if( !pE->aMI ) return SQLITE_NOMEM;
      memset(pE->aMI, 0, pTab->nColumn * 3 * sizeof(u32));
    }

    fts3EvalRestart(pCsr, pRoot, &rc);

    while( pCsr->isEof==0 && rc==SQLITE_OK ){

      do {
        /* Ensure the %_content statement is reset. */
        if( pCsr->isRequireSeek==0 ) sqlite3_reset(pCsr->pStmt);
        assert( sqlite3_data_count(pCsr->pStmt)==0 );

        /* Advance to the next document */
        fts3EvalNextRow(pCsr, pRoot, &rc);
        pCsr->isEof = pRoot->bEof;
        pCsr->isRequireSeek = 1;
        pCsr->isMatchinfoNeeded = 1;
        pCsr->iPrevId = pRoot->iDocid;
      }while( pCsr->isEof==0 
           && pRoot->eType==FTSQUERY_NEAR 
           && fts3EvalTestDeferredAndNear(pCsr, &rc) 
      );

      if( rc==SQLITE_OK && pCsr->isEof==0 ){
        fts3EvalUpdateCounts(pRoot);
      }
    }

    pCsr->isEof = 0;
    pCsr->iPrevId = iPrevId;

    if( bEof ){
      pRoot->bEof = bEof;
    }else{
      /* Caution: pRoot may iterate through docids in ascending or descending
      ** order. For this reason, even though it seems more defensive, the 
      ** do loop can not be written:
      **
      **   do {...} while( pRoot->iDocid<iDocid && rc==SQLITE_OK );
      */
      fts3EvalRestart(pCsr, pRoot, &rc);
      do {
        fts3EvalNextRow(pCsr, pRoot, &rc);
        assert( pRoot->bEof==0 );
      }while( pRoot->iDocid!=iDocid && rc==SQLITE_OK );
      fts3EvalTestDeferredAndNear(pCsr, &rc);
    }
  }
  return rc;
}

/*
** This function is used by the matchinfo() module to query a phrase 
** expression node for the following information:
**
**   1. The total number of occurrences of the phrase in each column of 
**      the FTS table (considering all rows), and
**
**   2. For each column, the number of rows in the table for which the
**      column contains at least one instance of the phrase.
**
** If no error occurs, SQLITE_OK is returned and the values for each column
** written into the array aiOut as follows:
**
**   aiOut[iCol*3 + 1] = Number of occurrences
**   aiOut[iCol*3 + 2] = Number of rows containing at least one instance
**
** Caveats:
**
**   * If a phrase consists entirely of deferred tokens, then all output 
**     values are set to the number of documents in the table. In other
**     words we assume that very common tokens occur exactly once in each 
**     column of each row of the table.
**
**   * If a phrase contains some deferred tokens (and some non-deferred 
**     tokens), count the potential occurrence identified by considering
**     the non-deferred tokens instead of actual phrase occurrences.
**
**   * If the phrase is part of a NEAR expression, then only phrase instances
**     that meet the NEAR constraint are included in the counts.
*/
SQLITE_PRIVATE int sqlite3Fts3EvalPhraseStats(
  Fts3Cursor *pCsr,               /* FTS cursor handle */
  Fts3Expr *pExpr,                /* Phrase expression */
  u32 *aiOut                      /* Array to write results into (see above) */
){
  Fts3Table *pTab = (Fts3Table *)pCsr->base.pVtab;
  int rc = SQLITE_OK;
  int iCol;

  if( pExpr->bDeferred && pExpr->pParent->eType!=FTSQUERY_NEAR ){
    assert( pCsr->nDoc>0 );
    for(iCol=0; iCol<pTab->nColumn; iCol++){
      aiOut[iCol*3 + 1] = (u32)pCsr->nDoc;
      aiOut[iCol*3 + 2] = (u32)pCsr->nDoc;
    }
  }else{
    rc = fts3EvalGatherStats(pCsr, pExpr);
    if( rc==SQLITE_OK ){
      assert( pExpr->aMI );
      for(iCol=0; iCol<pTab->nColumn; iCol++){
        aiOut[iCol*3 + 1] = pExpr->aMI[iCol*3 + 1];
        aiOut[iCol*3 + 2] = pExpr->aMI[iCol*3 + 2];
      }
    }
  }

  return rc;
}

/*
** The expression pExpr passed as the second argument to this function
** must be of type FTSQUERY_PHRASE. 
**
** The returned value is either NULL or a pointer to a buffer containing
** a position-list indicating the occurrences of the phrase in column iCol
** of the current row. 
**
** More specifically, the returned buffer contains 1 varint for each 
** occurence of the phrase in the column, stored using the normal (delta+2) 
** compression and is terminated by either an 0x01 or 0x00 byte. For example,
** if the requested column contains "a b X c d X X" and the position-list
** for 'X' is requested, the buffer returned may contain:
**
**     0x04 0x05 0x03 0x01   or   0x04 0x05 0x03 0x00
**
** This function works regardless of whether or not the phrase is deferred,
** incremental, or neither.
*/
SQLITE_PRIVATE char *sqlite3Fts3EvalPhrasePoslist(
  Fts3Cursor *pCsr,               /* FTS3 cursor object */
  Fts3Expr *pExpr,                /* Phrase to return doclist for */
  int iCol                        /* Column to return position list for */
){
  Fts3Phrase *pPhrase = pExpr->pPhrase;
  Fts3Table *pTab = (Fts3Table *)pCsr->base.pVtab;
  char *pIter = pPhrase->doclist.pList;
  int iThis;

  assert( iCol>=0 && iCol<pTab->nColumn );
  if( !pIter 
   || pExpr->bEof 
   || pExpr->iDocid!=pCsr->iPrevId
   || (pPhrase->iColumn<pTab->nColumn && pPhrase->iColumn!=iCol) 
  ){
    return 0;
  }

  assert( pPhrase->doclist.nList>0 );
  if( *pIter==0x01 ){
    pIter++;
    pIter += sqlite3Fts3GetVarint32(pIter, &iThis);
  }else{
    iThis = 0;
  }
  while( iThis<iCol ){
    fts3ColumnlistCopy(0, &pIter);
    if( *pIter==0x00 ) return 0;
    pIter++;
    pIter += sqlite3Fts3GetVarint32(pIter, &iThis);
  }

  return ((iCol==iThis)?pIter:0);
}

/*
** Free all components of the Fts3Phrase structure that were allocated by
** the eval module. Specifically, this means to free:
**
**   * the contents of pPhrase->doclist, and
**   * any Fts3MultiSegReader objects held by phrase tokens.
*/
SQLITE_PRIVATE void sqlite3Fts3EvalPhraseCleanup(Fts3Phrase *pPhrase){
  if( pPhrase ){
    int i;
    sqlite3_free(pPhrase->doclist.aAll);
    fts3EvalInvalidatePoslist(pPhrase);
    memset(&pPhrase->doclist, 0, sizeof(Fts3Doclist));
    for(i=0; i<pPhrase->nToken; i++){
      fts3SegReaderCursorFree(pPhrase->aToken[i].pSegcsr);
      pPhrase->aToken[i].pSegcsr = 0;
    }
  }
}

/*
** Return SQLITE_CORRUPT_VTAB.
*/
#ifdef SQLITE_DEBUG
SQLITE_PRIVATE int sqlite3Fts3Corrupt(){
  return SQLITE_CORRUPT_VTAB;
}
#endif

#if !SQLITE_CORE
/*
** Initialize API pointer table, if required.
*/
SQLITE_API int sqlite3_extension_init(
  sqlite3 *db, 
  char **pzErrMsg,
  const sqlite3_api_routines *pApi
){
  SQLITE_EXTENSION_INIT2(pApi)
  return sqlite3Fts3Init(db);
}
#endif

#endif

/************** End of fts3.c ************************************************/
/************** Begin file fts3_aux.c ****************************************/
/*
** 2011 Jan 27
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
*/
#if !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS3)

/* #include <string.h> */
/* #include <assert.h> */

typedef struct Fts3auxTable Fts3auxTable;
typedef struct Fts3auxCursor Fts3auxCursor;

struct Fts3auxTable {
  sqlite3_vtab base;              /* Base class used by SQLite core */
  Fts3Table *pFts3Tab;
};

struct Fts3auxCursor {
  sqlite3_vtab_cursor base;       /* Base class used by SQLite core */
  Fts3MultiSegReader csr;        /* Must be right after "base" */
  Fts3SegFilter filter;
  char *zStop;
  int nStop;                      /* Byte-length of string zStop */
  int isEof;                      /* True if cursor is at EOF */
  sqlite3_int64 iRowid;           /* Current rowid */

  int iCol;                       /* Current value of 'col' column */
  int nStat;                      /* Size of aStat[] array */
  struct Fts3auxColstats {
    sqlite3_int64 nDoc;           /* 'documents' values for current csr row */
    sqlite3_int64 nOcc;           /* 'occurrences' values for current csr row */
  } *aStat;
};

/*
** Schema of the terms table.
*/
#define FTS3_TERMS_SCHEMA "CREATE TABLE x(term, col, documents, occurrences)"

/*
** This function does all the work for both the xConnect and xCreate methods.
** These tables have no persistent representation of their own, so xConnect
** and xCreate are identical operations.
*/
static int fts3auxConnectMethod(
  sqlite3 *db,                    /* Database connection */
  void *pUnused,                  /* Unused */
  int argc,                       /* Number of elements in argv array */
  const char * const *argv,       /* xCreate/xConnect argument array */
  sqlite3_vtab **ppVtab,          /* OUT: New sqlite3_vtab object */
  char **pzErr                    /* OUT: sqlite3_malloc'd error message */
){
  char const *zDb;                /* Name of database (e.g. "main") */
  char const *zFts3;              /* Name of fts3 table */
  int nDb;                        /* Result of strlen(zDb) */
  int nFts3;                      /* Result of strlen(zFts3) */
  int nByte;                      /* Bytes of space to allocate here */
  int rc;                         /* value returned by declare_vtab() */
  Fts3auxTable *p;                /* Virtual table object to return */

  UNUSED_PARAMETER(pUnused);

  /* The user should specify a single argument - the name of an fts3 table. */
  if( argc!=4 ){
    *pzErr = sqlite3_mprintf(
        "wrong number of arguments to fts4aux constructor"
    );
    return SQLITE_ERROR;
  }

  zDb = argv[1]; 
  nDb = strlen(zDb);
  zFts3 = argv[3];
  nFts3 = strlen(zFts3);

  rc = sqlite3_declare_vtab(db, FTS3_TERMS_SCHEMA);
  if( rc!=SQLITE_OK ) return rc;

  nByte = sizeof(Fts3auxTable) + sizeof(Fts3Table) + nDb + nFts3 + 2;
  p = (Fts3auxTable *)sqlite3_malloc(nByte);
  if( !p ) return SQLITE_NOMEM;
  memset(p, 0, nByte);

  p->pFts3Tab = (Fts3Table *)&p[1];
  p->pFts3Tab->zDb = (char *)&p->pFts3Tab[1];
  p->pFts3Tab->zName = &p->pFts3Tab->zDb[nDb+1];
  p->pFts3Tab->db = db;
  p->pFts3Tab->nIndex = 1;

  memcpy((char *)p->pFts3Tab->zDb, zDb, nDb);
  memcpy((char *)p->pFts3Tab->zName, zFts3, nFts3);
  sqlite3Fts3Dequote((char *)p->pFts3Tab->zName);

  *ppVtab = (sqlite3_vtab *)p;
  return SQLITE_OK;
}

/*
** This function does the work for both the xDisconnect and xDestroy methods.
** These tables have no persistent representation of their own, so xDisconnect
** and xDestroy are identical operations.
*/
static int fts3auxDisconnectMethod(sqlite3_vtab *pVtab){
  Fts3auxTable *p = (Fts3auxTable *)pVtab;
  Fts3Table *pFts3 = p->pFts3Tab;
  int i;

  /* Free any prepared statements held */
  for(i=0; i<SizeofArray(pFts3->aStmt); i++){
    sqlite3_finalize(pFts3->aStmt[i]);
  }
  sqlite3_free(pFts3->zSegmentsTbl);
  sqlite3_free(p);
  return SQLITE_OK;
}

#define FTS4AUX_EQ_CONSTRAINT 1
#define FTS4AUX_GE_CONSTRAINT 2
#define FTS4AUX_LE_CONSTRAINT 4

/*
** xBestIndex - Analyze a WHERE and ORDER BY clause.
*/
static int fts3auxBestIndexMethod(
  sqlite3_vtab *pVTab, 
  sqlite3_index_info *pInfo
){
  int i;
  int iEq = -1;
  int iGe = -1;
  int iLe = -1;

  UNUSED_PARAMETER(pVTab);

  /* This vtab delivers always results in "ORDER BY term ASC" order. */
  if( pInfo->nOrderBy==1 
   && pInfo->aOrderBy[0].iColumn==0 
   && pInfo->aOrderBy[0].desc==0
  ){
    pInfo->orderByConsumed = 1;
  }

  /* Search for equality and range constraints on the "term" column. */
  for(i=0; i<pInfo->nConstraint; i++){
    if( pInfo->aConstraint[i].usable && pInfo->aConstraint[i].iColumn==0 ){
      int op = pInfo->aConstraint[i].op;
      if( op==SQLITE_INDEX_CONSTRAINT_EQ ) iEq = i;
      if( op==SQLITE_INDEX_CONSTRAINT_LT ) iLe = i;
      if( op==SQLITE_INDEX_CONSTRAINT_LE ) iLe = i;
      if( op==SQLITE_INDEX_CONSTRAINT_GT ) iGe = i;
      if( op==SQLITE_INDEX_CONSTRAINT_GE ) iGe = i;
    }
  }

  if( iEq>=0 ){
    pInfo->idxNum = FTS4AUX_EQ_CONSTRAINT;
    pInfo->aConstraintUsage[iEq].argvIndex = 1;
    pInfo->estimatedCost = 5;
  }else{
    pInfo->idxNum = 0;
    pInfo->estimatedCost = 20000;
    if( iGe>=0 ){
      pInfo->idxNum += FTS4AUX_GE_CONSTRAINT;
      pInfo->aConstraintUsage[iGe].argvIndex = 1;
      pInfo->estimatedCost /= 2;
    }
    if( iLe>=0 ){
      pInfo->idxNum += FTS4AUX_LE_CONSTRAINT;
      pInfo->aConstraintUsage[iLe].argvIndex = 1 + (iGe>=0);
      pInfo->estimatedCost /= 2;
    }
  }

  return SQLITE_OK;
}

/*
** xOpen - Open a cursor.
*/
static int fts3auxOpenMethod(sqlite3_vtab *pVTab, sqlite3_vtab_cursor **ppCsr){
  Fts3auxCursor *pCsr;            /* Pointer to cursor object to return */

  UNUSED_PARAMETER(pVTab);

  pCsr = (Fts3auxCursor *)sqlite3_malloc(sizeof(Fts3auxCursor));
  if( !pCsr ) return SQLITE_NOMEM;
  memset(pCsr, 0, sizeof(Fts3auxCursor));

  *ppCsr = (sqlite3_vtab_cursor *)pCsr;
  return SQLITE_OK;
}

/*
** xClose - Close a cursor.
*/
static int fts3auxCloseMethod(sqlite3_vtab_cursor *pCursor){
  Fts3Table *pFts3 = ((Fts3auxTable *)pCursor->pVtab)->pFts3Tab;
  Fts3auxCursor *pCsr = (Fts3auxCursor *)pCursor;

  sqlite3Fts3SegmentsClose(pFts3);
  sqlite3Fts3SegReaderFinish(&pCsr->csr);
  sqlite3_free((void *)pCsr->filter.zTerm);
  sqlite3_free(pCsr->zStop);
  sqlite3_free(pCsr->aStat);
  sqlite3_free(pCsr);
  return SQLITE_OK;
}

static int fts3auxGrowStatArray(Fts3auxCursor *pCsr, int nSize){
  if( nSize>pCsr->nStat ){
    struct Fts3auxColstats *aNew;
    aNew = (struct Fts3auxColstats *)sqlite3_realloc(pCsr->aStat, 
        sizeof(struct Fts3auxColstats) * nSize
    );
    if( aNew==0 ) return SQLITE_NOMEM;
    memset(&aNew[pCsr->nStat], 0, 
        sizeof(struct Fts3auxColstats) * (nSize - pCsr->nStat)
    );
    pCsr->aStat = aNew;
    pCsr->nStat = nSize;
  }
  return SQLITE_OK;
}

/*
** xNext - Advance the cursor to the next row, if any.
*/
static int fts3auxNextMethod(sqlite3_vtab_cursor *pCursor){
  Fts3auxCursor *pCsr = (Fts3auxCursor *)pCursor;
  Fts3Table *pFts3 = ((Fts3auxTable *)pCursor->pVtab)->pFts3Tab;
  int rc;

  /* Increment our pretend rowid value. */
  pCsr->iRowid++;

  for(pCsr->iCol++; pCsr->iCol<pCsr->nStat; pCsr->iCol++){
    if( pCsr->aStat[pCsr->iCol].nDoc>0 ) return SQLITE_OK;
  }

  rc = sqlite3Fts3SegReaderStep(pFts3, &pCsr->csr);
  if( rc==SQLITE_ROW ){
    int i = 0;
    int nDoclist = pCsr->csr.nDoclist;
    char *aDoclist = pCsr->csr.aDoclist;
    int iCol;

    int eState = 0;

    if( pCsr->zStop ){
      int n = (pCsr->nStop<pCsr->csr.nTerm) ? pCsr->nStop : pCsr->csr.nTerm;
      int mc = memcmp(pCsr->zStop, pCsr->csr.zTerm, n);
      if( mc<0 || (mc==0 && pCsr->csr.nTerm>pCsr->nStop) ){
        pCsr->isEof = 1;
        return SQLITE_OK;
      }
    }

    if( fts3auxGrowStatArray(pCsr, 2) ) return SQLITE_NOMEM;
    memset(pCsr->aStat, 0, sizeof(struct Fts3auxColstats) * pCsr->nStat);
    iCol = 0;

    while( i<nDoclist ){
      sqlite3_int64 v = 0;

      i += sqlite3Fts3GetVarint(&aDoclist[i], &v);
      switch( eState ){
        /* State 0. In this state the integer just read was a docid. */
        case 0:
          pCsr->aStat[0].nDoc++;
          eState = 1;
          iCol = 0;
          break;

        /* State 1. In this state we are expecting either a 1, indicating
        ** that the following integer will be a column number, or the
        ** start of a position list for column 0.  
        ** 
        ** The only difference between state 1 and state 2 is that if the
        ** integer encountered in state 1 is not 0 or 1, then we need to
        ** increment the column 0 "nDoc" count for this term.
        */
        case 1:
          assert( iCol==0 );
          if( v>1 ){
            pCsr->aStat[1].nDoc++;
          }
          eState = 2;
          /* fall through */

        case 2:
          if( v==0 ){       /* 0x00. Next integer will be a docid. */
            eState = 0;
          }else if( v==1 ){ /* 0x01. Next integer will be a column number. */
            eState = 3;
          }else{            /* 2 or greater. A position. */
            pCsr->aStat[iCol+1].nOcc++;
            pCsr->aStat[0].nOcc++;
          }
          break;

        /* State 3. The integer just read is a column number. */
        default: assert( eState==3 );
          iCol = (int)v;
          if( fts3auxGrowStatArray(pCsr, iCol+2) ) return SQLITE_NOMEM;
          pCsr->aStat[iCol+1].nDoc++;
          eState = 2;
          break;
      }
    }

    pCsr->iCol = 0;
    rc = SQLITE_OK;
  }else{
    pCsr->isEof = 1;
  }
  return rc;
}

/*
** xFilter - Initialize a cursor to point at the start of its data.
*/
static int fts3auxFilterMethod(
  sqlite3_vtab_cursor *pCursor,   /* The cursor used for this query */
  int idxNum,                     /* Strategy index */
  const char *idxStr,             /* Unused */
  int nVal,                       /* Number of elements in apVal */
  sqlite3_value **apVal           /* Arguments for the indexing scheme */
){
  Fts3auxCursor *pCsr = (Fts3auxCursor *)pCursor;
  Fts3Table *pFts3 = ((Fts3auxTable *)pCursor->pVtab)->pFts3Tab;
  int rc;
  int isScan;

  UNUSED_PARAMETER(nVal);
  UNUSED_PARAMETER(idxStr);

  assert( idxStr==0 );
  assert( idxNum==FTS4AUX_EQ_CONSTRAINT || idxNum==0
       || idxNum==FTS4AUX_LE_CONSTRAINT || idxNum==FTS4AUX_GE_CONSTRAINT
       || idxNum==(FTS4AUX_LE_CONSTRAINT|FTS4AUX_GE_CONSTRAINT)
  );
  isScan = (idxNum!=FTS4AUX_EQ_CONSTRAINT);

  /* In case this cursor is being reused, close and zero it. */
  testcase(pCsr->filter.zTerm);
  sqlite3Fts3SegReaderFinish(&pCsr->csr);
  sqlite3_free((void *)pCsr->filter.zTerm);
  sqlite3_free(pCsr->aStat);
  memset(&pCsr->csr, 0, ((u8*)&pCsr[1]) - (u8*)&pCsr->csr);

  pCsr->filter.flags = FTS3_SEGMENT_REQUIRE_POS|FTS3_SEGMENT_IGNORE_EMPTY;
  if( isScan ) pCsr->filter.flags |= FTS3_SEGMENT_SCAN;

  if( idxNum&(FTS4AUX_EQ_CONSTRAINT|FTS4AUX_GE_CONSTRAINT) ){
    const unsigned char *zStr = sqlite3_value_text(apVal[0]);
    if( zStr ){
      pCsr->filter.zTerm = sqlite3_mprintf("%s", zStr);
      pCsr->filter.nTerm = sqlite3_value_bytes(apVal[0]);
      if( pCsr->filter.zTerm==0 ) return SQLITE_NOMEM;
    }
  }
  if( idxNum&FTS4AUX_LE_CONSTRAINT ){
    int iIdx = (idxNum&FTS4AUX_GE_CONSTRAINT) ? 1 : 0;
    pCsr->zStop = sqlite3_mprintf("%s", sqlite3_value_text(apVal[iIdx]));
    pCsr->nStop = sqlite3_value_bytes(apVal[iIdx]);
    if( pCsr->zStop==0 ) return SQLITE_NOMEM;
  }

  rc = sqlite3Fts3SegReaderCursor(pFts3, 0, FTS3_SEGCURSOR_ALL,
      pCsr->filter.zTerm, pCsr->filter.nTerm, 0, isScan, &pCsr->csr
  );
  if( rc==SQLITE_OK ){
    rc = sqlite3Fts3SegReaderStart(pFts3, &pCsr->csr, &pCsr->filter);
  }

  if( rc==SQLITE_OK ) rc = fts3auxNextMethod(pCursor);
  return rc;
}

/*
** xEof - Return true if the cursor is at EOF, or false otherwise.
*/
static int fts3auxEofMethod(sqlite3_vtab_cursor *pCursor){
  Fts3auxCursor *pCsr = (Fts3auxCursor *)pCursor;
  return pCsr->isEof;
}

/*
** xColumn - Return a column value.
*/
static int fts3auxColumnMethod(
  sqlite3_vtab_cursor *pCursor,   /* Cursor to retrieve value from */
  sqlite3_context *pContext,      /* Context for sqlite3_result_xxx() calls */
  int iCol                        /* Index of column to read value from */
){
  Fts3auxCursor *p = (Fts3auxCursor *)pCursor;

  assert( p->isEof==0 );
  if( iCol==0 ){        /* Column "term" */
    sqlite3_result_text(pContext, p->csr.zTerm, p->csr.nTerm, SQLITE_TRANSIENT);
  }else if( iCol==1 ){  /* Column "col" */
    if( p->iCol ){
      sqlite3_result_int(pContext, p->iCol-1);
    }else{
      sqlite3_result_text(pContext, "*", -1, SQLITE_STATIC);
    }
  }else if( iCol==2 ){  /* Column "documents" */
    sqlite3_result_int64(pContext, p->aStat[p->iCol].nDoc);
  }else{                /* Column "occurrences" */
    sqlite3_result_int64(pContext, p->aStat[p->iCol].nOcc);
  }

  return SQLITE_OK;
}

/*
** xRowid - Return the current rowid for the cursor.
*/
static int fts3auxRowidMethod(
  sqlite3_vtab_cursor *pCursor,   /* Cursor to retrieve value from */
  sqlite_int64 *pRowid            /* OUT: Rowid value */
){
  Fts3auxCursor *pCsr = (Fts3auxCursor *)pCursor;
  *pRowid = pCsr->iRowid;
  return SQLITE_OK;
}

/*
** Register the fts3aux module with database connection db. Return SQLITE_OK
** if successful or an error code if sqlite3_create_module() fails.
*/
SQLITE_PRIVATE int sqlite3Fts3InitAux(sqlite3 *db){
  static const sqlite3_module fts3aux_module = {
     0,                           /* iVersion      */
     fts3auxConnectMethod,        /* xCreate       */
     fts3auxConnectMethod,        /* xConnect      */
     fts3auxBestIndexMethod,      /* xBestIndex    */
     fts3auxDisconnectMethod,     /* xDisconnect   */
     fts3auxDisconnectMethod,     /* xDestroy      */
     fts3auxOpenMethod,           /* xOpen         */
     fts3auxCloseMethod,          /* xClose        */
     fts3auxFilterMethod,         /* xFilter       */
     fts3auxNextMethod,           /* xNext         */
     fts3auxEofMethod,            /* xEof          */
     fts3auxColumnMethod,         /* xColumn       */
     fts3auxRowidMethod,          /* xRowid        */
     0,                           /* xUpdate       */
     0,                           /* xBegin        */
     0,                           /* xSync         */
     0,                           /* xCommit       */
     0,                           /* xRollback     */
     0,                           /* xFindFunction */
     0,                           /* xRename       */
     0,                           /* xSavepoint    */
     0,                           /* xRelease      */
     0                            /* xRollbackTo   */
  };
  int rc;                         /* Return code */

  rc = sqlite3_create_module(db, "fts4aux", &fts3aux_module, 0);
  return rc;
}

#endif /* !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS3) */

/************** End of fts3_aux.c ********************************************/
/************** Begin file fts3_expr.c ***************************************/
/*
** 2008 Nov 28
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
** This module contains code that implements a parser for fts3 query strings
** (the right-hand argument to the MATCH operator). Because the supported 
** syntax is relatively simple, the whole tokenizer/parser system is
** hand-coded. 
*/
#if !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS3)

/*
** By default, this module parses the legacy syntax that has been 
** traditionally used by fts3. Or, if SQLITE_ENABLE_FTS3_PARENTHESIS
** is defined, then it uses the new syntax. The differences between
** the new and the old syntaxes are:
**
**  a) The new syntax supports parenthesis. The old does not.
**
**  b) The new syntax supports the AND and NOT operators. The old does not.
**
**  c) The old syntax supports the "-" token qualifier. This is not 
**     supported by the new syntax (it is replaced by the NOT operator).
**
**  d) When using the old syntax, the OR operator has a greater precedence
**     than an implicit AND. When using the new, both implicity and explicit
**     AND operators have a higher precedence than OR.
**
** If compiled with SQLITE_TEST defined, then this module exports the
** symbol "int sqlite3_fts3_enable_parentheses". Setting this variable
** to zero causes the module to use the old syntax. If it is set to 
** non-zero the new syntax is activated. This is so both syntaxes can
** be tested using a single build of testfixture.
**
** The following describes the syntax supported by the fts3 MATCH
** operator in a similar format to that used by the lemon parser
** generator. This module does not use actually lemon, it uses a
** custom parser.
**
**   query ::= andexpr (OR andexpr)*.
**
**   andexpr ::= notexpr (AND? notexpr)*.
**
**   notexpr ::= nearexpr (NOT nearexpr|-TOKEN)*.
**   notexpr ::= LP query RP.
**
**   nearexpr ::= phrase (NEAR distance_opt nearexpr)*.
**
**   distance_opt ::= .
**   distance_opt ::= / INTEGER.
**
**   phrase ::= TOKEN.
**   phrase ::= COLUMN:TOKEN.
**   phrase ::= "TOKEN TOKEN TOKEN...".
*/

#ifdef SQLITE_TEST
SQLITE_API int sqlite3_fts3_enable_parentheses = 0;
#else
# ifdef SQLITE_ENABLE_FTS3_PARENTHESIS 
#  define sqlite3_fts3_enable_parentheses 1
# else
#  define sqlite3_fts3_enable_parentheses 0
# endif
#endif

/*
** Default span for NEAR operators.
*/
#define SQLITE_FTS3_DEFAULT_NEAR_PARAM 10

/* #include <string.h> */
/* #include <assert.h> */

/*
** isNot:
**   This variable is used by function getNextNode(). When getNextNode() is
**   called, it sets ParseContext.isNot to true if the 'next node' is a 
**   FTSQUERY_PHRASE with a unary "-" attached to it. i.e. "mysql" in the
**   FTS3 query "sqlite -mysql". Otherwise, ParseContext.isNot is set to
**   zero.
*/
typedef struct ParseContext ParseContext;
struct ParseContext {
  sqlite3_tokenizer *pTokenizer;      /* Tokenizer module */
  const char **azCol;                 /* Array of column names for fts3 table */
  int bFts4;                          /* True to allow FTS4-only syntax */
  int nCol;                           /* Number of entries in azCol[] */
  int iDefaultCol;                    /* Default column to query */
  int isNot;                          /* True if getNextNode() sees a unary - */
  sqlite3_context *pCtx;              /* Write error message here */
  int nNest;                          /* Number of nested brackets */
};

/*
** This function is equivalent to the standard isspace() function. 
**
** The standard isspace() can be awkward to use safely, because although it
** is defined to accept an argument of type int, its behaviour when passed
** an integer that falls outside of the range of the unsigned char type
** is undefined (and sometimes, "undefined" means segfault). This wrapper
** is defined to accept an argument of type char, and always returns 0 for
** any values that fall outside of the range of the unsigned char type (i.e.
** negative values).
*/
static int fts3isspace(char c){
  return c==' ' || c=='\t' || c=='\n' || c=='\r' || c=='\v' || c=='\f';
}

/*
** Allocate nByte bytes of memory using sqlite3_malloc(). If successful,
** zero the memory before returning a pointer to it. If unsuccessful, 
** return NULL.
*/
static void *fts3MallocZero(int nByte){
  void *pRet = sqlite3_malloc(nByte);
  if( pRet ) memset(pRet, 0, nByte);
  return pRet;
}


/*
** Extract the next token from buffer z (length n) using the tokenizer
** and other information (column names etc.) in pParse. Create an Fts3Expr
** structure of type FTSQUERY_PHRASE containing a phrase consisting of this
** single token and set *ppExpr to point to it. If the end of the buffer is
** reached before a token is found, set *ppExpr to zero. It is the
** responsibility of the caller to eventually deallocate the allocated 
** Fts3Expr structure (if any) by passing it to sqlite3_free().
**
** Return SQLITE_OK if successful, or SQLITE_NOMEM if a memory allocation
** fails.
*/
static int getNextToken(
  ParseContext *pParse,                   /* fts3 query parse context */
  int iCol,                               /* Value for Fts3Phrase.iColumn */
  const char *z, int n,                   /* Input string */
  Fts3Expr **ppExpr,                      /* OUT: expression */
  int *pnConsumed                         /* OUT: Number of bytes consumed */
){
  sqlite3_tokenizer *pTokenizer = pParse->pTokenizer;
  sqlite3_tokenizer_module const *pModule = pTokenizer->pModule;
  int rc;
  sqlite3_tokenizer_cursor *pCursor;
  Fts3Expr *pRet = 0;
  int nConsumed = 0;

  rc = pModule->xOpen(pTokenizer, z, n, &pCursor);
  if( rc==SQLITE_OK ){
    const char *zToken;
    int nToken, iStart, iEnd, iPosition;
    int nByte;                               /* total space to allocate */

    pCursor->pTokenizer = pTokenizer;
    rc = pModule->xNext(pCursor, &zToken, &nToken, &iStart, &iEnd, &iPosition);

    if( rc==SQLITE_OK ){
      nByte = sizeof(Fts3Expr) + sizeof(Fts3Phrase) + nToken;
      pRet = (Fts3Expr *)fts3MallocZero(nByte);
      if( !pRet ){
        rc = SQLITE_NOMEM;
      }else{
        pRet->eType = FTSQUERY_PHRASE;
        pRet->pPhrase = (Fts3Phrase *)&pRet[1];
        pRet->pPhrase->nToken = 1;
        pRet->pPhrase->iColumn = iCol;
        pRet->pPhrase->aToken[0].n = nToken;
        pRet->pPhrase->aToken[0].z = (char *)&pRet->pPhrase[1];
        memcpy(pRet->pPhrase->aToken[0].z, zToken, nToken);

        if( iEnd<n && z[iEnd]=='*' ){
          pRet->pPhrase->aToken[0].isPrefix = 1;
          iEnd++;
        }

        while( 1 ){
          if( !sqlite3_fts3_enable_parentheses 
           && iStart>0 && z[iStart-1]=='-' 
          ){
            pParse->isNot = 1;
            iStart--;
          }else if( pParse->bFts4 && iStart>0 && z[iStart-1]=='^' ){
            pRet->pPhrase->aToken[0].bFirst = 1;
            iStart--;
          }else{
            break;
          }
        }

      }
      nConsumed = iEnd;
    }

    pModule->xClose(pCursor);
  }
  
  *pnConsumed = nConsumed;
  *ppExpr = pRet;
  return rc;
}


/*
** Enlarge a memory allocation.  If an out-of-memory allocation occurs,
** then free the old allocation.
*/
static void *fts3ReallocOrFree(void *pOrig, int nNew){
  void *pRet = sqlite3_realloc(pOrig, nNew);
  if( !pRet ){
    sqlite3_free(pOrig);
  }
  return pRet;
}

/*
** Buffer zInput, length nInput, contains the contents of a quoted string
** that appeared as part of an fts3 query expression. Neither quote character
** is included in the buffer. This function attempts to tokenize the entire
** input buffer and create an Fts3Expr structure of type FTSQUERY_PHRASE 
** containing the results.
**
** If successful, SQLITE_OK is returned and *ppExpr set to point at the
** allocated Fts3Expr structure. Otherwise, either SQLITE_NOMEM (out of memory
** error) or SQLITE_ERROR (tokenization error) is returned and *ppExpr set
** to 0.
*/
static int getNextString(
  ParseContext *pParse,                   /* fts3 query parse context */
  const char *zInput, int nInput,         /* Input string */
  Fts3Expr **ppExpr                       /* OUT: expression */
){
  sqlite3_tokenizer *pTokenizer = pParse->pTokenizer;
  sqlite3_tokenizer_module const *pModule = pTokenizer->pModule;
  int rc;
  Fts3Expr *p = 0;
  sqlite3_tokenizer_cursor *pCursor = 0;
  char *zTemp = 0;
  int nTemp = 0;

  const int nSpace = sizeof(Fts3Expr) + sizeof(Fts3Phrase);
  int nToken = 0;

  /* The final Fts3Expr data structure, including the Fts3Phrase,
  ** Fts3PhraseToken structures token buffers are all stored as a single 
  ** allocation so that the expression can be freed with a single call to
  ** sqlite3_free(). Setting this up requires a two pass approach.
  **
  ** The first pass, in the block below, uses a tokenizer cursor to iterate
  ** through the tokens in the expression. This pass uses fts3ReallocOrFree()
  ** to assemble data in two dynamic buffers:
  **
  **   Buffer p: Points to the Fts3Expr structure, followed by the Fts3Phrase
  **             structure, followed by the array of Fts3PhraseToken 
  **             structures. This pass only populates the Fts3PhraseToken array.
  **
  **   Buffer zTemp: Contains copies of all tokens.
  **
  ** The second pass, in the block that begins "if( rc==SQLITE_DONE )" below,
  ** appends buffer zTemp to buffer p, and fills in the Fts3Expr and Fts3Phrase
  ** structures.
  */
  rc = pModule->xOpen(pTokenizer, zInput, nInput, &pCursor);
  if( rc==SQLITE_OK ){
    int ii;
    pCursor->pTokenizer = pTokenizer;
    for(ii=0; rc==SQLITE_OK; ii++){
      const char *zByte;
      int nByte, iBegin, iEnd, iPos;
      rc = pModule->xNext(pCursor, &zByte, &nByte, &iBegin, &iEnd, &iPos);
      if( rc==SQLITE_OK ){
        Fts3PhraseToken *pToken;

        p = fts3ReallocOrFree(p, nSpace + ii*sizeof(Fts3PhraseToken));
        if( !p ) goto no_mem;

        zTemp = fts3ReallocOrFree(zTemp, nTemp + nByte);
        if( !zTemp ) goto no_mem;

        assert( nToken==ii );
        pToken = &((Fts3Phrase *)(&p[1]))->aToken[ii];
        memset(pToken, 0, sizeof(Fts3PhraseToken));

        memcpy(&zTemp[nTemp], zByte, nByte);
        nTemp += nByte;

        pToken->n = nByte;
        pToken->isPrefix = (iEnd<nInput && zInput[iEnd]=='*');
        pToken->bFirst = (iBegin>0 && zInput[iBegin-1]=='^');
        nToken = ii+1;
      }
    }

    pModule->xClose(pCursor);
    pCursor = 0;
  }

  if( rc==SQLITE_DONE ){
    int jj;
    char *zBuf = 0;

    p = fts3ReallocOrFree(p, nSpace + nToken*sizeof(Fts3PhraseToken) + nTemp);
    if( !p ) goto no_mem;
    memset(p, 0, (char *)&(((Fts3Phrase *)&p[1])->aToken[0])-(char *)p);
    p->eType = FTSQUERY_PHRASE;
    p->pPhrase = (Fts3Phrase *)&p[1];
    p->pPhrase->iColumn = pParse->iDefaultCol;
    p->pPhrase->nToken = nToken;

    zBuf = (char *)&p->pPhrase->aToken[nToken];
    if( zTemp ){
      memcpy(zBuf, zTemp, nTemp);
      sqlite3_free(zTemp);
    }else{
      assert( nTemp==0 );
    }

    for(jj=0; jj<p->pPhrase->nToken; jj++){
      p->pPhrase->aToken[jj].z = zBuf;
      zBuf += p->pPhrase->aToken[jj].n;
    }
    rc = SQLITE_OK;
  }

  *ppExpr = p;
  return rc;
no_mem:

  if( pCursor ){
    pModule->xClose(pCursor);
  }
  sqlite3_free(zTemp);
  sqlite3_free(p);
  *ppExpr = 0;
  return SQLITE_NOMEM;
}

/*
** Function getNextNode(), which is called by fts3ExprParse(), may itself
** call fts3ExprParse(). So this forward declaration is required.
*/
static int fts3ExprParse(ParseContext *, const char *, int, Fts3Expr **, int *);

/*
** The output variable *ppExpr is populated with an allocated Fts3Expr 
** structure, or set to 0 if the end of the input buffer is reached.
**
** Returns an SQLite error code. SQLITE_OK if everything works, SQLITE_NOMEM
** if a malloc failure occurs, or SQLITE_ERROR if a parse error is encountered.
** If SQLITE_ERROR is returned, pContext is populated with an error message.
*/
static int getNextNode(
  ParseContext *pParse,                   /* fts3 query parse context */
  const char *z, int n,                   /* Input string */
  Fts3Expr **ppExpr,                      /* OUT: expression */
  int *pnConsumed                         /* OUT: Number of bytes consumed */
){
  static const struct Fts3Keyword {
    char *z;                              /* Keyword text */
    unsigned char n;                      /* Length of the keyword */
    unsigned char parenOnly;              /* Only valid in paren mode */
    unsigned char eType;                  /* Keyword code */
  } aKeyword[] = {
    { "OR" ,  2, 0, FTSQUERY_OR   },
    { "AND",  3, 1, FTSQUERY_AND  },
    { "NOT",  3, 1, FTSQUERY_NOT  },
    { "NEAR", 4, 0, FTSQUERY_NEAR }
  };
  int ii;
  int iCol;
  int iColLen;
  int rc;
  Fts3Expr *pRet = 0;

  const char *zInput = z;
  int nInput = n;

  pParse->isNot = 0;

  /* Skip over any whitespace before checking for a keyword, an open or
  ** close bracket, or a quoted string. 
  */
  while( nInput>0 && fts3isspace(*zInput) ){
    nInput--;
    zInput++;
  }
  if( nInput==0 ){
    return SQLITE_DONE;
  }

  /* See if we are dealing with a keyword. */
  for(ii=0; ii<(int)(sizeof(aKeyword)/sizeof(struct Fts3Keyword)); ii++){
    const struct Fts3Keyword *pKey = &aKeyword[ii];

    if( (pKey->parenOnly & ~sqlite3_fts3_enable_parentheses)!=0 ){
      continue;
    }

    if( nInput>=pKey->n && 0==memcmp(zInput, pKey->z, pKey->n) ){
      int nNear = SQLITE_FTS3_DEFAULT_NEAR_PARAM;
      int nKey = pKey->n;
      char cNext;

      /* If this is a "NEAR" keyword, check for an explicit nearness. */
      if( pKey->eType==FTSQUERY_NEAR ){
        assert( nKey==4 );
        if( zInput[4]=='/' && zInput[5]>='0' && zInput[5]<='9' ){
          nNear = 0;
          for(nKey=5; zInput[nKey]>='0' && zInput[nKey]<='9'; nKey++){
            nNear = nNear * 10 + (zInput[nKey] - '0');
          }
        }
      }

      /* At this point this is probably a keyword. But for that to be true,
      ** the next byte must contain either whitespace, an open or close
      ** parenthesis, a quote character, or EOF. 
      */
      cNext = zInput[nKey];
      if( fts3isspace(cNext) 
       || cNext=='"' || cNext=='(' || cNext==')' || cNext==0
      ){
        pRet = (Fts3Expr *)fts3MallocZero(sizeof(Fts3Expr));
        if( !pRet ){
          return SQLITE_NOMEM;
        }
        pRet->eType = pKey->eType;
        pRet->nNear = nNear;
        *ppExpr = pRet;
        *pnConsumed = (int)((zInput - z) + nKey);
        return SQLITE_OK;
      }

      /* Turns out that wasn't a keyword after all. This happens if the
      ** user has supplied a token such as "ORacle". Continue.
      */
    }
  }

  /* Check for an open bracket. */
  if( sqlite3_fts3_enable_parentheses ){
    if( *zInput=='(' ){
      int nConsumed;
      pParse->nNest++;
      rc = fts3ExprParse(pParse, &zInput[1], nInput-1, ppExpr, &nConsumed);
      if( rc==SQLITE_OK && !*ppExpr ){
        rc = SQLITE_DONE;
      }
      *pnConsumed = (int)((zInput - z) + 1 + nConsumed);
      return rc;
    }
  
    /* Check for a close bracket. */
    if( *zInput==')' ){
      pParse->nNest--;
      *pnConsumed = (int)((zInput - z) + 1);
      return SQLITE_DONE;
    }
  }

  /* See if we are dealing with a quoted phrase. If this is the case, then
  ** search for the closing quote and pass the whole string to getNextString()
  ** for processing. This is easy to do, as fts3 has no syntax for escaping
  ** a quote character embedded in a string.
  */
  if( *zInput=='"' ){
    for(ii=1; ii<nInput && zInput[ii]!='"'; ii++);
    *pnConsumed = (int)((zInput - z) + ii + 1);
    if( ii==nInput ){
      return SQLITE_ERROR;
    }
    return getNextString(pParse, &zInput[1], ii-1, ppExpr);
  }


  /* If control flows to this point, this must be a regular token, or 
  ** the end of the input. Read a regular token using the sqlite3_tokenizer
  ** interface. Before doing so, figure out if there is an explicit
  ** column specifier for the token. 
  **
  ** TODO: Strangely, it is not possible to associate a column specifier
  ** with a quoted phrase, only with a single token. Not sure if this was
  ** an implementation artifact or an intentional decision when fts3 was
  ** first implemented. Whichever it was, this module duplicates the 
  ** limitation.
  */
  iCol = pParse->iDefaultCol;
  iColLen = 0;
  for(ii=0; ii<pParse->nCol; ii++){
    const char *zStr = pParse->azCol[ii];
    int nStr = (int)strlen(zStr);
    if( nInput>nStr && zInput[nStr]==':' 
     && sqlite3_strnicmp(zStr, zInput, nStr)==0 
    ){
      iCol = ii;
      iColLen = (int)((zInput - z) + nStr + 1);
      break;
    }
  }
  rc = getNextToken(pParse, iCol, &z[iColLen], n-iColLen, ppExpr, pnConsumed);
  *pnConsumed += iColLen;
  return rc;
}

/*
** The argument is an Fts3Expr structure for a binary operator (any type
** except an FTSQUERY_PHRASE). Return an integer value representing the
** precedence of the operator. Lower values have a higher precedence (i.e.
** group more tightly). For example, in the C language, the == operator
** groups more tightly than ||, and would therefore have a higher precedence.
**
** When using the new fts3 query syntax (when SQLITE_ENABLE_FTS3_PARENTHESIS
** is defined), the order of the operators in precedence from highest to
** lowest is:
**
**   NEAR
**   NOT
**   AND (including implicit ANDs)
**   OR
**
** Note that when using the old query syntax, the OR operator has a higher
** precedence than the AND operator.
*/
static int opPrecedence(Fts3Expr *p){
  assert( p->eType!=FTSQUERY_PHRASE );
  if( sqlite3_fts3_enable_parentheses ){
    return p->eType;
  }else if( p->eType==FTSQUERY_NEAR ){
    return 1;
  }else if( p->eType==FTSQUERY_OR ){
    return 2;
  }
  assert( p->eType==FTSQUERY_AND );
  return 3;
}

/*
** Argument ppHead contains a pointer to the current head of a query 
** expression tree being parsed. pPrev is the expression node most recently
** inserted into the tree. This function adds pNew, which is always a binary
** operator node, into the expression tree based on the relative precedence
** of pNew and the existing nodes of the tree. This may result in the head
** of the tree changing, in which case *ppHead is set to the new root node.
*/
static void insertBinaryOperator(
  Fts3Expr **ppHead,       /* Pointer to the root node of a tree */
  Fts3Expr *pPrev,         /* Node most recently inserted into the tree */
  Fts3Expr *pNew           /* New binary node to insert into expression tree */
){
  Fts3Expr *pSplit = pPrev;
  while( pSplit->pParent && opPrecedence(pSplit->pParent)<=opPrecedence(pNew) ){
    pSplit = pSplit->pParent;
  }

  if( pSplit->pParent ){
    assert( pSplit->pParent->pRight==pSplit );
    pSplit->pParent->pRight = pNew;
    pNew->pParent = pSplit->pParent;
  }else{
    *ppHead = pNew;
  }
  pNew->pLeft = pSplit;
  pSplit->pParent = pNew;
}

/*
** Parse the fts3 query expression found in buffer z, length n. This function
** returns either when the end of the buffer is reached or an unmatched 
** closing bracket - ')' - is encountered.
**
** If successful, SQLITE_OK is returned, *ppExpr is set to point to the
** parsed form of the expression and *pnConsumed is set to the number of
** bytes read from buffer z. Otherwise, *ppExpr is set to 0 and SQLITE_NOMEM
** (out of memory error) or SQLITE_ERROR (parse error) is returned.
*/
static int fts3ExprParse(
  ParseContext *pParse,                   /* fts3 query parse context */
  const char *z, int n,                   /* Text of MATCH query */
  Fts3Expr **ppExpr,                      /* OUT: Parsed query structure */
  int *pnConsumed                         /* OUT: Number of bytes consumed */
){
  Fts3Expr *pRet = 0;
  Fts3Expr *pPrev = 0;
  Fts3Expr *pNotBranch = 0;               /* Only used in legacy parse mode */
  int nIn = n;
  const char *zIn = z;
  int rc = SQLITE_OK;
  int isRequirePhrase = 1;

  while( rc==SQLITE_OK ){
    Fts3Expr *p = 0;
    int nByte = 0;
    rc = getNextNode(pParse, zIn, nIn, &p, &nByte);
    if( rc==SQLITE_OK ){
      int isPhrase;

      if( !sqlite3_fts3_enable_parentheses 
       && p->eType==FTSQUERY_PHRASE && pParse->isNot 
      ){
        /* Create an implicit NOT operator. */
        Fts3Expr *pNot = fts3MallocZero(sizeof(Fts3Expr));
        if( !pNot ){
          sqlite3Fts3ExprFree(p);
          rc = SQLITE_NOMEM;
          goto exprparse_out;
        }
        pNot->eType = FTSQUERY_NOT;
        pNot->pRight = p;
        if( pNotBranch ){
          pNot->pLeft = pNotBranch;
        }
        pNotBranch = pNot;
        p = pPrev;
      }else{
        int eType = p->eType;
        isPhrase = (eType==FTSQUERY_PHRASE || p->pLeft);

        /* The isRequirePhrase variable is set to true if a phrase or
        ** an expression contained in parenthesis is required. If a
        ** binary operator (AND, OR, NOT or NEAR) is encounted when
        ** isRequirePhrase is set, this is a syntax error.
        */
        if( !isPhrase && isRequirePhrase ){
          sqlite3Fts3ExprFree(p);
          rc = SQLITE_ERROR;
          goto exprparse_out;
        }
  
        if( isPhrase && !isRequirePhrase ){
          /* Insert an implicit AND operator. */
          Fts3Expr *pAnd;
          assert( pRet && pPrev );
          pAnd = fts3MallocZero(sizeof(Fts3Expr));
          if( !pAnd ){
            sqlite3Fts3ExprFree(p);
            rc = SQLITE_NOMEM;
            goto exprparse_out;
          }
          pAnd->eType = FTSQUERY_AND;
          insertBinaryOperator(&pRet, pPrev, pAnd);
          pPrev = pAnd;
        }

        /* This test catches attempts to make either operand of a NEAR
        ** operator something other than a phrase. For example, either of
        ** the following:
        **
        **    (bracketed expression) NEAR phrase
        **    phrase NEAR (bracketed expression)
        **
        ** Return an error in either case.
        */
        if( pPrev && (
            (eType==FTSQUERY_NEAR && !isPhrase && pPrev->eType!=FTSQUERY_PHRASE)
         || (eType!=FTSQUERY_PHRASE && isPhrase && pPrev->eType==FTSQUERY_NEAR)
        )){
          sqlite3Fts3ExprFree(p);
          rc = SQLITE_ERROR;
          goto exprparse_out;
        }
  
        if( isPhrase ){
          if( pRet ){
            assert( pPrev && pPrev->pLeft && pPrev->pRight==0 );
            pPrev->pRight = p;
            p->pParent = pPrev;
          }else{
            pRet = p;
          }
        }else{
          insertBinaryOperator(&pRet, pPrev, p);
        }
        isRequirePhrase = !isPhrase;
      }
      assert( nByte>0 );
    }
    assert( rc!=SQLITE_OK || (nByte>0 && nByte<=nIn) );
    nIn -= nByte;
    zIn += nByte;
    pPrev = p;
  }

  if( rc==SQLITE_DONE && pRet && isRequirePhrase ){
    rc = SQLITE_ERROR;
  }

  if( rc==SQLITE_DONE ){
    rc = SQLITE_OK;
    if( !sqlite3_fts3_enable_parentheses && pNotBranch ){
      if( !pRet ){
        rc = SQLITE_ERROR;
      }else{
        Fts3Expr *pIter = pNotBranch;
        while( pIter->pLeft ){
          pIter = pIter->pLeft;
        }
        pIter->pLeft = pRet;
        pRet = pNotBranch;
      }
    }
  }
  *pnConsumed = n - nIn;

exprparse_out:
  if( rc!=SQLITE_OK ){
    sqlite3Fts3ExprFree(pRet);
    sqlite3Fts3ExprFree(pNotBranch);
    pRet = 0;
  }
  *ppExpr = pRet;
  return rc;
}

/*
** Parameters z and n contain a pointer to and length of a buffer containing
** an fts3 query expression, respectively. This function attempts to parse the
** query expression and create a tree of Fts3Expr structures representing the
** parsed expression. If successful, *ppExpr is set to point to the head
** of the parsed expression tree and SQLITE_OK is returned. If an error
** occurs, either SQLITE_NOMEM (out-of-memory error) or SQLITE_ERROR (parse
** error) is returned and *ppExpr is set to 0.
**
** If parameter n is a negative number, then z is assumed to point to a
** nul-terminated string and the length is determined using strlen().
**
** The first parameter, pTokenizer, is passed the fts3 tokenizer module to
** use to normalize query tokens while parsing the expression. The azCol[]
** array, which is assumed to contain nCol entries, should contain the names
** of each column in the target fts3 table, in order from left to right. 
** Column names must be nul-terminated strings.
**
** The iDefaultCol parameter should be passed the index of the table column
** that appears on the left-hand-side of the MATCH operator (the default
** column to match against for tokens for which a column name is not explicitly
** specified as part of the query string), or -1 if tokens may by default
** match any table column.
*/
SQLITE_PRIVATE int sqlite3Fts3ExprParse(
  sqlite3_tokenizer *pTokenizer,      /* Tokenizer module */
  char **azCol,                       /* Array of column names for fts3 table */
  int bFts4,                          /* True to allow FTS4-only syntax */
  int nCol,                           /* Number of entries in azCol[] */
  int iDefaultCol,                    /* Default column to query */
  const char *z, int n,               /* Text of MATCH query */
  Fts3Expr **ppExpr                   /* OUT: Parsed query structure */
){
  int nParsed;
  int rc;
  ParseContext sParse;
  sParse.pTokenizer = pTokenizer;
  sParse.azCol = (const char **)azCol;
  sParse.nCol = nCol;
  sParse.iDefaultCol = iDefaultCol;
  sParse.nNest = 0;
  sParse.bFts4 = bFts4;
  if( z==0 ){
    *ppExpr = 0;
    return SQLITE_OK;
  }
  if( n<0 ){
    n = (int)strlen(z);
  }
  rc = fts3ExprParse(&sParse, z, n, ppExpr, &nParsed);

  /* Check for mismatched parenthesis */
  if( rc==SQLITE_OK && sParse.nNest ){
    rc = SQLITE_ERROR;
    sqlite3Fts3ExprFree(*ppExpr);
    *ppExpr = 0;
  }

  return rc;
}

/*
** Free a parsed fts3 query expression allocated by sqlite3Fts3ExprParse().
*/
SQLITE_PRIVATE void sqlite3Fts3ExprFree(Fts3Expr *p){
  if( p ){
    assert( p->eType==FTSQUERY_PHRASE || p->pPhrase==0 );
    sqlite3Fts3ExprFree(p->pLeft);
    sqlite3Fts3ExprFree(p->pRight);
    sqlite3Fts3EvalPhraseCleanup(p->pPhrase);
    sqlite3_free(p->aMI);
    sqlite3_free(p);
  }
}

/****************************************************************************
*****************************************************************************
** Everything after this point is just test code.
*/

#ifdef SQLITE_TEST

/* #include <stdio.h> */

/*
** Function to query the hash-table of tokenizers (see README.tokenizers).
*/
static int queryTestTokenizer(
  sqlite3 *db, 
  const char *zName,  
  const sqlite3_tokenizer_module **pp
){
  int rc;
  sqlite3_stmt *pStmt;
  const char zSql[] = "SELECT fts3_tokenizer(?)";

  *pp = 0;
  rc = sqlite3_prepare_v2(db, zSql, -1, &pStmt, 0);
  if( rc!=SQLITE_OK ){
    return rc;
  }

  sqlite3_bind_text(pStmt, 1, zName, -1, SQLITE_STATIC);
  if( SQLITE_ROW==sqlite3_step(pStmt) ){
    if( sqlite3_column_type(pStmt, 0)==SQLITE_BLOB ){
      memcpy((void *)pp, sqlite3_column_blob(pStmt, 0), sizeof(*pp));
    }
  }

  return sqlite3_finalize(pStmt);
}

/*
** Return a pointer to a buffer containing a text representation of the
** expression passed as the first argument. The buffer is obtained from
** sqlite3_malloc(). It is the responsibility of the caller to use 
** sqlite3_free() to release the memory. If an OOM condition is encountered,
** NULL is returned.
**
** If the second argument is not NULL, then its contents are prepended to 
** the returned expression text and then freed using sqlite3_free().
*/
static char *exprToString(Fts3Expr *pExpr, char *zBuf){
  switch( pExpr->eType ){
    case FTSQUERY_PHRASE: {
      Fts3Phrase *pPhrase = pExpr->pPhrase;
      int i;
      zBuf = sqlite3_mprintf(
          "%zPHRASE %d 0", zBuf, pPhrase->iColumn);
      for(i=0; zBuf && i<pPhrase->nToken; i++){
        zBuf = sqlite3_mprintf("%z %.*s%s", zBuf, 
            pPhrase->aToken[i].n, pPhrase->aToken[i].z,
            (pPhrase->aToken[i].isPrefix?"+":"")
        );
      }
      return zBuf;
    }

    case FTSQUERY_NEAR:
      zBuf = sqlite3_mprintf("%zNEAR/%d ", zBuf, pExpr->nNear);
      break;
    case FTSQUERY_NOT:
      zBuf = sqlite3_mprintf("%zNOT ", zBuf);
      break;
    case FTSQUERY_AND:
      zBuf = sqlite3_mprintf("%zAND ", zBuf);
      break;
    case FTSQUERY_OR:
      zBuf = sqlite3_mprintf("%zOR ", zBuf);
      break;
  }

  if( zBuf ) zBuf = sqlite3_mprintf("%z{", zBuf);
  if( zBuf ) zBuf = exprToString(pExpr->pLeft, zBuf);
  if( zBuf ) zBuf = sqlite3_mprintf("%z} {", zBuf);

  if( zBuf ) zBuf = exprToString(pExpr->pRight, zBuf);
  if( zBuf ) zBuf = sqlite3_mprintf("%z}", zBuf);

  return zBuf;
}

/*
** This is the implementation of a scalar SQL function used to test the 
** expression parser. It should be called as follows:
**
**   fts3_exprtest(<tokenizer>, <expr>, <column 1>, ...);
**
** The first argument, <tokenizer>, is the name of the fts3 tokenizer used
** to parse the query expression (see README.tokenizers). The second argument
** is the query expression to parse. Each subsequent argument is the name
** of a column of the fts3 table that the query expression may refer to.
** For example:
**
**   SELECT fts3_exprtest('simple', 'Bill col2:Bloggs', 'col1', 'col2');
*/
static void fts3ExprTest(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  sqlite3_tokenizer_module const *pModule = 0;
  sqlite3_tokenizer *pTokenizer = 0;
  int rc;
  char **azCol = 0;
  const char *zExpr;
  int nExpr;
  int nCol;
  int ii;
  Fts3Expr *pExpr;
  char *zBuf = 0;
  sqlite3 *db = sqlite3_context_db_handle(context);

  if( argc<3 ){
    sqlite3_result_error(context, 
        "Usage: fts3_exprtest(tokenizer, expr, col1, ...", -1
    );
    return;
  }

  rc = queryTestTokenizer(db,
                          (const char *)sqlite3_value_text(argv[0]), &pModule);
  if( rc==SQLITE_NOMEM ){
    sqlite3_result_error_nomem(context);
    goto exprtest_out;
  }else if( !pModule ){
    sqlite3_result_error(context, "No such tokenizer module", -1);
    goto exprtest_out;
  }

  rc = pModule->xCreate(0, 0, &pTokenizer);
  assert( rc==SQLITE_NOMEM || rc==SQLITE_OK );
  if( rc==SQLITE_NOMEM ){
    sqlite3_result_error_nomem(context);
    goto exprtest_out;
  }
  pTokenizer->pModule = pModule;

  zExpr = (const char *)sqlite3_value_text(argv[1]);
  nExpr = sqlite3_value_bytes(argv[1]);
  nCol = argc-2;
  azCol = (char **)sqlite3_malloc(nCol*sizeof(char *));
  if( !azCol ){
    sqlite3_result_error_nomem(context);
    goto exprtest_out;
  }
  for(ii=0; ii<nCol; ii++){
    azCol[ii] = (char *)sqlite3_value_text(argv[ii+2]);
  }

  rc = sqlite3Fts3ExprParse(
      pTokenizer, azCol, 0, nCol, nCol, zExpr, nExpr, &pExpr
  );
  if( rc!=SQLITE_OK && rc!=SQLITE_NOMEM ){
    sqlite3_result_error(context, "Error parsing expression", -1);
  }else if( rc==SQLITE_NOMEM || !(zBuf = exprToString(pExpr, 0)) ){
    sqlite3_result_error_nomem(context);
  }else{
    sqlite3_result_text(context, zBuf, -1, SQLITE_TRANSIENT);
    sqlite3_free(zBuf);
  }

  sqlite3Fts3ExprFree(pExpr);

exprtest_out:
  if( pModule && pTokenizer ){
    rc = pModule->xDestroy(pTokenizer);
  }
  sqlite3_free(azCol);
}

/*
** Register the query expression parser test function fts3_exprtest() 
** with database connection db. 
*/
SQLITE_PRIVATE int sqlite3Fts3ExprInitTestInterface(sqlite3* db){
  return sqlite3_create_function(
      db, "fts3_exprtest", -1, SQLITE_UTF8, 0, fts3ExprTest, 0, 0
  );
}

#endif
#endif /* !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS3) */

/************** End of fts3_expr.c *******************************************/
/************** Begin file fts3_hash.c ***************************************/
/*
** 2001 September 22
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This is the implementation of generic hash-tables used in SQLite.
** We've modified it slightly to serve as a standalone hash table
** implementation for the full-text indexing module.
*/

/*
** The code in this file is only compiled if:
**
**     * The FTS3 module is being built as an extension
**       (in which case SQLITE_CORE is not defined), or
**
**     * The FTS3 module is being built into the core of
**       SQLite (in which case SQLITE_ENABLE_FTS3 is defined).
*/
#if !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS3)

/* #include <assert.h> */
/* #include <stdlib.h> */
/* #include <string.h> */


/*
** Malloc and Free functions
*/
static void *fts3HashMalloc(int n){
  void *p = sqlite3_malloc(n);
  if( p ){
    memset(p, 0, n);
  }
  return p;
}
static void fts3HashFree(void *p){
  sqlite3_free(p);
}

/* Turn bulk memory into a hash table object by initializing the
** fields of the Hash structure.
**
** "pNew" is a pointer to the hash table that is to be initialized.
** keyClass is one of the constants 
** FTS3_HASH_BINARY or FTS3_HASH_STRING.  The value of keyClass 
** determines what kind of key the hash table will use.  "copyKey" is
** true if the hash table should make its own private copy of keys and
** false if it should just use the supplied pointer.
*/
SQLITE_PRIVATE void sqlite3Fts3HashInit(Fts3Hash *pNew, char keyClass, char copyKey){
  assert( pNew!=0 );
  assert( keyClass>=FTS3_HASH_STRING && keyClass<=FTS3_HASH_BINARY );
  pNew->keyClass = keyClass;
  pNew->copyKey = copyKey;
  pNew->first = 0;
  pNew->count = 0;
  pNew->htsize = 0;
  pNew->ht = 0;
}

/* Remove all entries from a hash table.  Reclaim all memory.
** Call this routine to delete a hash table or to reset a hash table
** to the empty state.
*/
SQLITE_PRIVATE void sqlite3Fts3HashClear(Fts3Hash *pH){
  Fts3HashElem *elem;         /* For looping over all elements of the table */

  assert( pH!=0 );
  elem = pH->first;
  pH->first = 0;
  fts3HashFree(pH->ht);
  pH->ht = 0;
  pH->htsize = 0;
  while( elem ){
    Fts3HashElem *next_elem = elem->next;
    if( pH->copyKey && elem->pKey ){
      fts3HashFree(elem->pKey);
    }
    fts3HashFree(elem);
    elem = next_elem;
  }
  pH->count = 0;
}

/*
** Hash and comparison functions when the mode is FTS3_HASH_STRING
*/
static int fts3StrHash(const void *pKey, int nKey){
  const char *z = (const char *)pKey;
  int h = 0;
  if( nKey<=0 ) nKey = (int) strlen(z);
  while( nKey > 0  ){
    h = (h<<3) ^ h ^ *z++;
    nKey--;
  }
  return h & 0x7fffffff;
}
static int fts3StrCompare(const void *pKey1, int n1, const void *pKey2, int n2){
  if( n1!=n2 ) return 1;
  return strncmp((const char*)pKey1,(const char*)pKey2,n1);
}

/*
** Hash and comparison functions when the mode is FTS3_HASH_BINARY
*/
static int fts3BinHash(const void *pKey, int nKey){
  int h = 0;
  const char *z = (const char *)pKey;
  while( nKey-- > 0 ){
    h = (h<<3) ^ h ^ *(z++);
  }
  return h & 0x7fffffff;
}
static int fts3BinCompare(const void *pKey1, int n1, const void *pKey2, int n2){
  if( n1!=n2 ) return 1;
  return memcmp(pKey1,pKey2,n1);
}

/*
** Return a pointer to the appropriate hash function given the key class.
**
** The C syntax in this function definition may be unfamilar to some 
** programmers, so we provide the following additional explanation:
**
** The name of the function is "ftsHashFunction".  The function takes a
** single parameter "keyClass".  The return value of ftsHashFunction()
** is a pointer to another function.  Specifically, the return value
** of ftsHashFunction() is a pointer to a function that takes two parameters
** with types "const void*" and "int" and returns an "int".
*/
static int (*ftsHashFunction(int keyClass))(const void*,int){
  if( keyClass==FTS3_HASH_STRING ){
    return &fts3StrHash;
  }else{
    assert( keyClass==FTS3_HASH_BINARY );
    return &fts3BinHash;
  }
}

/*
** Return a pointer to the appropriate hash function given the key class.
**
** For help in interpreted the obscure C code in the function definition,
** see the header comment on the previous function.
*/
static int (*ftsCompareFunction(int keyClass))(const void*,int,const void*,int){
  if( keyClass==FTS3_HASH_STRING ){
    return &fts3StrCompare;
  }else{
    assert( keyClass==FTS3_HASH_BINARY );
    return &fts3BinCompare;
  }
}

/* Link an element into the hash table
*/
static void fts3HashInsertElement(
  Fts3Hash *pH,            /* The complete hash table */
  struct _fts3ht *pEntry,  /* The entry into which pNew is inserted */
  Fts3HashElem *pNew       /* The element to be inserted */
){
  Fts3HashElem *pHead;     /* First element already in pEntry */
  pHead = pEntry->chain;
  if( pHead ){
    pNew->next = pHead;
    pNew->prev = pHead->prev;
    if( pHead->prev ){ pHead->prev->next = pNew; }
    else             { pH->first = pNew; }
    pHead->prev = pNew;
  }else{
    pNew->next = pH->first;
    if( pH->first ){ pH->first->prev = pNew; }
    pNew->prev = 0;
    pH->first = pNew;
  }
  pEntry->count++;
  pEntry->chain = pNew;
}


/* Resize the hash table so that it cantains "new_size" buckets.
** "new_size" must be a power of 2.  The hash table might fail 
** to resize if sqliteMalloc() fails.
**
** Return non-zero if a memory allocation error occurs.
*/
static int fts3Rehash(Fts3Hash *pH, int new_size){
  struct _fts3ht *new_ht;          /* The new hash table */
  Fts3HashElem *elem, *next_elem;  /* For looping over existing elements */
  int (*xHash)(const void*,int);   /* The hash function */

  assert( (new_size & (new_size-1))==0 );
  new_ht = (struct _fts3ht *)fts3HashMalloc( new_size*sizeof(struct _fts3ht) );
  if( new_ht==0 ) return 1;
  fts3HashFree(pH->ht);
  pH->ht = new_ht;
  pH->htsize = new_size;
  xHash = ftsHashFunction(pH->keyClass);
  for(elem=pH->first, pH->first=0; elem; elem = next_elem){
    int h = (*xHash)(elem->pKey, elem->nKey) & (new_size-1);
    next_elem = elem->next;
    fts3HashInsertElement(pH, &new_ht[h], elem);
  }
  return 0;
}

/* This function (for internal use only) locates an element in an
** hash table that matches the given key.  The hash for this key has
** already been computed and is passed as the 4th parameter.
*/
static Fts3HashElem *fts3FindElementByHash(
  const Fts3Hash *pH, /* The pH to be searched */
  const void *pKey,   /* The key we are searching for */
  int nKey,
  int h               /* The hash for this key. */
){
  Fts3HashElem *elem;            /* Used to loop thru the element list */
  int count;                     /* Number of elements left to test */
  int (*xCompare)(const void*,int,const void*,int);  /* comparison function */

  if( pH->ht ){
    struct _fts3ht *pEntry = &pH->ht[h];
    elem = pEntry->chain;
    count = pEntry->count;
    xCompare = ftsCompareFunction(pH->keyClass);
    while( count-- && elem ){
      if( (*xCompare)(elem->pKey,elem->nKey,pKey,nKey)==0 ){ 
        return elem;
      }
      elem = elem->next;
    }
  }
  return 0;
}

/* Remove a single entry from the hash table given a pointer to that
** element and a hash on the element's key.
*/
static void fts3RemoveElementByHash(
  Fts3Hash *pH,         /* The pH containing "elem" */
  Fts3HashElem* elem,   /* The element to be removed from the pH */
  int h                 /* Hash value for the element */
){
  struct _fts3ht *pEntry;
  if( elem->prev ){
    elem->prev->next = elem->next; 
  }else{
    pH->first = elem->next;
  }
  if( elem->next ){
    elem->next->prev = elem->prev;
  }
  pEntry = &pH->ht[h];
  if( pEntry->chain==elem ){
    pEntry->chain = elem->next;
  }
  pEntry->count--;
  if( pEntry->count<=0 ){
    pEntry->chain = 0;
  }
  if( pH->copyKey && elem->pKey ){
    fts3HashFree(elem->pKey);
  }
  fts3HashFree( elem );
  pH->count--;
  if( pH->count<=0 ){
    assert( pH->first==0 );
    assert( pH->count==0 );
    fts3HashClear(pH);
  }
}

SQLITE_PRIVATE Fts3HashElem *sqlite3Fts3HashFindElem(
  const Fts3Hash *pH, 
  const void *pKey, 
  int nKey
){
  int h;                          /* A hash on key */
  int (*xHash)(const void*,int);  /* The hash function */

  if( pH==0 || pH->ht==0 ) return 0;
  xHash = ftsHashFunction(pH->keyClass);
  assert( xHash!=0 );
  h = (*xHash)(pKey,nKey);
  assert( (pH->htsize & (pH->htsize-1))==0 );
  return fts3FindElementByHash(pH,pKey,nKey, h & (pH->htsize-1));
}

/* 
** Attempt to locate an element of the hash table pH with a key
** that matches pKey,nKey.  Return the data for this element if it is
** found, or NULL if there is no match.
*/
SQLITE_PRIVATE void *sqlite3Fts3HashFind(const Fts3Hash *pH, const void *pKey, int nKey){
  Fts3HashElem *pElem;            /* The element that matches key (if any) */

  pElem = sqlite3Fts3HashFindElem(pH, pKey, nKey);
  return pElem ? pElem->data : 0;
}

/* Insert an element into the hash table pH.  The key is pKey,nKey
** and the data is "data".
**
** If no element exists with a matching key, then a new
** element is created.  A copy of the key is made if the copyKey
** flag is set.  NULL is returned.
**
** If another element already exists with the same key, then the
** new data replaces the old data and the old data is returned.
** The key is not copied in this instance.  If a malloc fails, then
** the new data is returned and the hash table is unchanged.
**
** If the "data" parameter to this function is NULL, then the
** element corresponding to "key" is removed from the hash table.
*/
SQLITE_PRIVATE void *sqlite3Fts3HashInsert(
  Fts3Hash *pH,        /* The hash table to insert into */
  const void *pKey,    /* The key */
  int nKey,            /* Number of bytes in the key */
  void *data           /* The data */
){
  int hraw;                 /* Raw hash value of the key */
  int h;                    /* the hash of the key modulo hash table size */
  Fts3HashElem *elem;       /* Used to loop thru the element list */
  Fts3HashElem *new_elem;   /* New element added to the pH */
  int (*xHash)(const void*,int);  /* The hash function */

  assert( pH!=0 );
  xHash = ftsHashFunction(pH->keyClass);
  assert( xHash!=0 );
  hraw = (*xHash)(pKey, nKey);
  assert( (pH->htsize & (pH->htsize-1))==0 );
  h = hraw & (pH->htsize-1);
  elem = fts3FindElementByHash(pH,pKey,nKey,h);
  if( elem ){
    void *old_data = elem->data;
    if( data==0 ){
      fts3RemoveElementByHash(pH,elem,h);
    }else{
      elem->data = data;
    }
    return old_data;
  }
  if( data==0 ) return 0;
  if( (pH->htsize==0 && fts3Rehash(pH,8))
   || (pH->count>=pH->htsize && fts3Rehash(pH, pH->htsize*2))
  ){
    pH->count = 0;
    return data;
  }
  assert( pH->htsize>0 );
  new_elem = (Fts3HashElem*)fts3HashMalloc( sizeof(Fts3HashElem) );
  if( new_elem==0 ) return data;
  if( pH->copyKey && pKey!=0 ){
    new_elem->pKey = fts3HashMalloc( nKey );
    if( new_elem->pKey==0 ){
      fts3HashFree(new_elem);
      return data;
    }
    memcpy((void*)new_elem->pKey, pKey, nKey);
  }else{
    new_elem->pKey = (void*)pKey;
  }
  new_elem->nKey = nKey;
  pH->count++;
  assert( pH->htsize>0 );
  assert( (pH->htsize & (pH->htsize-1))==0 );
  h = hraw & (pH->htsize-1);
  fts3HashInsertElement(pH, &pH->ht[h], new_elem);
  new_elem->data = data;
  return 0;
}

#endif /* !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS3) */

/************** End of fts3_hash.c *******************************************/
/************** Begin file fts3_porter.c *************************************/
/*
** 2006 September 30
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** Implementation of the full-text-search tokenizer that implements
** a Porter stemmer.
*/

/*
** The code in this file is only compiled if:
**
**     * The FTS3 module is being built as an extension
**       (in which case SQLITE_CORE is not defined), or
**
**     * The FTS3 module is being built into the core of
**       SQLite (in which case SQLITE_ENABLE_FTS3 is defined).
*/
#if !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS3)

/* #include <assert.h> */
/* #include <stdlib.h> */
/* #include <stdio.h> */
/* #include <string.h> */


/*
** Class derived from sqlite3_tokenizer
*/
typedef struct porter_tokenizer {
  sqlite3_tokenizer base;      /* Base class */
} porter_tokenizer;

/*
** Class derived from sqlit3_tokenizer_cursor
*/
typedef struct porter_tokenizer_cursor {
  sqlite3_tokenizer_cursor base;
  const char *zInput;          /* input we are tokenizing */
  int nInput;                  /* size of the input */
  int iOffset;                 /* current position in zInput */
  int iToken;                  /* index of next token to be returned */
  char *zToken;                /* storage for current token */
  int nAllocated;              /* space allocated to zToken buffer */
} porter_tokenizer_cursor;


/*
** Create a new tokenizer instance.
*/
static int porterCreate(
  int argc, const char * const *argv,
  sqlite3_tokenizer **ppTokenizer
){
  porter_tokenizer *t;

  UNUSED_PARAMETER(argc);
  UNUSED_PARAMETER(argv);

  t = (porter_tokenizer *) sqlite3_malloc(sizeof(*t));
  if( t==NULL ) return SQLITE_NOMEM;
  memset(t, 0, sizeof(*t));
  *ppTokenizer = &t->base;
  return SQLITE_OK;
}

/*
** Destroy a tokenizer
*/
static int porterDestroy(sqlite3_tokenizer *pTokenizer){
  sqlite3_free(pTokenizer);
  return SQLITE_OK;
}

/*
** Prepare to begin tokenizing a particular string.  The input
** string to be tokenized is zInput[0..nInput-1].  A cursor
** used to incrementally tokenize this string is returned in 
** *ppCursor.
*/
static int porterOpen(
  sqlite3_tokenizer *pTokenizer,         /* The tokenizer */
  const char *zInput, int nInput,        /* String to be tokenized */
  sqlite3_tokenizer_cursor **ppCursor    /* OUT: Tokenization cursor */
){
  porter_tokenizer_cursor *c;

  UNUSED_PARAMETER(pTokenizer);

  c = (porter_tokenizer_cursor *) sqlite3_malloc(sizeof(*c));
  if( c==NULL ) return SQLITE_NOMEM;

  c->zInput = zInput;
  if( zInput==0 ){
    c->nInput = 0;
  }else if( nInput<0 ){
    c->nInput = (int)strlen(zInput);
  }else{
    c->nInput = nInput;
  }
  c->iOffset = 0;                 /* start tokenizing at the beginning */
  c->iToken = 0;
  c->zToken = NULL;               /* no space allocated, yet. */
  c->nAllocated = 0;

  *ppCursor = &c->base;
  return SQLITE_OK;
}

/*
** Close a tokenization cursor previously opened by a call to
** porterOpen() above.
*/
static int porterClose(sqlite3_tokenizer_cursor *pCursor){
  porter_tokenizer_cursor *c = (porter_tokenizer_cursor *) pCursor;
  sqlite3_free(c->zToken);
  sqlite3_free(c);
  return SQLITE_OK;
}
/*
** Vowel or consonant
*/
static const char cType[] = {
   0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0,
   1, 1, 1, 2, 1
};

/*
** isConsonant() and isVowel() determine if their first character in
** the string they point to is a consonant or a vowel, according
** to Porter ruls.  
**
** A consonate is any letter other than 'a', 'e', 'i', 'o', or 'u'.
** 'Y' is a consonant unless it follows another consonant,
** in which case it is a vowel.
**
** In these routine, the letters are in reverse order.  So the 'y' rule
** is that 'y' is a consonant unless it is followed by another
** consonent.
*/
static int isVowel(const char*);
static int isConsonant(const char *z){
  int j;
  char x = *z;
  if( x==0 ) return 0;
  assert( x>='a' && x<='z' );
  j = cType[x-'a'];
  if( j<2 ) return j;
  return z[1]==0 || isVowel(z + 1);
}
static int isVowel(const char *z){
  int j;
  char x = *z;
  if( x==0 ) return 0;
  assert( x>='a' && x<='z' );
  j = cType[x-'a'];
  if( j<2 ) return 1-j;
  return isConsonant(z + 1);
}

/*
** Let any sequence of one or more vowels be represented by V and let
** C be sequence of one or more consonants.  Then every word can be
** represented as:
**
**           [C] (VC){m} [V]
**
** In prose:  A word is an optional consonant followed by zero or
** vowel-consonant pairs followed by an optional vowel.  "m" is the
** number of vowel consonant pairs.  This routine computes the value
** of m for the first i bytes of a word.
**
** Return true if the m-value for z is 1 or more.  In other words,
** return true if z contains at least one vowel that is followed
** by a consonant.
**
** In this routine z[] is in reverse order.  So we are really looking
** for an instance of of a consonant followed by a vowel.
*/
static int m_gt_0(const char *z){
  while( isVowel(z) ){ z++; }
  if( *z==0 ) return 0;
  while( isConsonant(z) ){ z++; }
  return *z!=0;
}

/* Like mgt0 above except we are looking for a value of m which is
** exactly 1
*/
static int m_eq_1(const char *z){
  while( isVowel(z) ){ z++; }
  if( *z==0 ) return 0;
  while( isConsonant(z) ){ z++; }
  if( *z==0 ) return 0;
  while( isVowel(z) ){ z++; }
  if( *z==0 ) return 1;
  while( isConsonant(z) ){ z++; }
  return *z==0;
}

/* Like mgt0 above except we are looking for a value of m>1 instead
** or m>0
*/
static int m_gt_1(const char *z){
  while( isVowel(z) ){ z++; }
  if( *z==0 ) return 0;
  while( isConsonant(z) ){ z++; }
  if( *z==0 ) return 0;
  while( isVowel(z) ){ z++; }
  if( *z==0 ) return 0;
  while( isConsonant(z) ){ z++; }
  return *z!=0;
}

/*
** Return TRUE if there is a vowel anywhere within z[0..n-1]
*/
static int hasVowel(const char *z){
  while( isConsonant(z) ){ z++; }
  return *z!=0;
}

/*
** Return TRUE if the word ends in a double consonant.
**
** The text is reversed here. So we are really looking at
** the first two characters of z[].
*/
static int doubleConsonant(const char *z){
  return isConsonant(z) && z[0]==z[1];
}

/*
** Return TRUE if the word ends with three letters which
** are consonant-vowel-consonent and where the final consonant
** is not 'w', 'x', or 'y'.
**
** The word is reversed here.  So we are really checking the
** first three letters and the first one cannot be in [wxy].
*/
static int star_oh(const char *z){
  return
    isConsonant(z) &&
    z[0]!='w' && z[0]!='x' && z[0]!='y' &&
    isVowel(z+1) &&
    isConsonant(z+2);
}

/*
** If the word ends with zFrom and xCond() is true for the stem
** of the word that preceeds the zFrom ending, then change the 
** ending to zTo.
**
** The input word *pz and zFrom are both in reverse order.  zTo
** is in normal order. 
**
** Return TRUE if zFrom matches.  Return FALSE if zFrom does not
** match.  Not that TRUE is returned even if xCond() fails and
** no substitution occurs.
*/
static int stem(
  char **pz,             /* The word being stemmed (Reversed) */
  const char *zFrom,     /* If the ending matches this... (Reversed) */
  const char *zTo,       /* ... change the ending to this (not reversed) */
  int (*xCond)(const char*)   /* Condition that must be true */
){
  char *z = *pz;
  while( *zFrom && *zFrom==*z ){ z++; zFrom++; }
  if( *zFrom!=0 ) return 0;
  if( xCond && !xCond(z) ) return 1;
  while( *zTo ){
    *(--z) = *(zTo++);
  }
  *pz = z;
  return 1;
}

/*
** This is the fallback stemmer used when the porter stemmer is
** inappropriate.  The input word is copied into the output with
** US-ASCII case folding.  If the input word is too long (more
** than 20 bytes if it contains no digits or more than 6 bytes if
** it contains digits) then word is truncated to 20 or 6 bytes
** by taking 10 or 3 bytes from the beginning and end.
*/
static void copy_stemmer(const char *zIn, int nIn, char *zOut, int *pnOut){
  int i, mx, j;
  int hasDigit = 0;
  for(i=0; i<nIn; i++){
    char c = zIn[i];
    if( c>='A' && c<='Z' ){
      zOut[i] = c - 'A' + 'a';
    }else{
      if( c>='0' && c<='9' ) hasDigit = 1;
      zOut[i] = c;
    }
  }
  mx = hasDigit ? 3 : 10;
  if( nIn>mx*2 ){
    for(j=mx, i=nIn-mx; i<nIn; i++, j++){
      zOut[j] = zOut[i];
    }
    i = j;
  }
  zOut[i] = 0;
  *pnOut = i;
}


/*
** Stem the input word zIn[0..nIn-1].  Store the output in zOut.
** zOut is at least big enough to hold nIn bytes.  Write the actual
** size of the output word (exclusive of the '\0' terminator) into *pnOut.
**
** Any upper-case characters in the US-ASCII character set ([A-Z])
** are converted to lower case.  Upper-case UTF characters are
** unchanged.
**
** Words that are longer than about 20 bytes are stemmed by retaining
** a few bytes from the beginning and the end of the word.  If the
** word contains digits, 3 bytes are taken from the beginning and
** 3 bytes from the end.  For long words without digits, 10 bytes
** are taken from each end.  US-ASCII case folding still applies.
** 
** If the input word contains not digits but does characters not 
** in [a-zA-Z] then no stemming is attempted and this routine just 
** copies the input into the input into the output with US-ASCII
** case folding.
**
** Stemming never increases the length of the word.  So there is
** no chance of overflowing the zOut buffer.
*/
static void porter_stemmer(const char *zIn, int nIn, char *zOut, int *pnOut){
  int i, j;
  char zReverse[28];
  char *z, *z2;
  if( nIn<3 || nIn>=(int)sizeof(zReverse)-7 ){
    /* The word is too big or too small for the porter stemmer.
    ** Fallback to the copy stemmer */
    copy_stemmer(zIn, nIn, zOut, pnOut);
    return;
  }
  for(i=0, j=sizeof(zReverse)-6; i<nIn; i++, j--){
    char c = zIn[i];
    if( c>='A' && c<='Z' ){
      zReverse[j] = c + 'a' - 'A';
    }else if( c>='a' && c<='z' ){
      zReverse[j] = c;
    }else{
      /* The use of a character not in [a-zA-Z] means that we fallback
      ** to the copy stemmer */
      copy_stemmer(zIn, nIn, zOut, pnOut);
      return;
    }
  }
  memset(&zReverse[sizeof(zReverse)-5], 0, 5);
  z = &zReverse[j+1];


  /* Step 1a */
  if( z[0]=='s' ){
    if(
     !stem(&z, "sess", "ss", 0) &&
     !stem(&z, "sei", "i", 0)  &&
     !stem(&z, "ss", "ss", 0)
    ){
      z++;
    }
  }

  /* Step 1b */  
  z2 = z;
  if( stem(&z, "dee", "ee", m_gt_0) ){
    /* Do nothing.  The work was all in the test */
  }else if( 
     (stem(&z, "gni", "", hasVowel) || stem(&z, "de", "", hasVowel))
      && z!=z2
  ){
     if( stem(&z, "ta", "ate", 0) ||
         stem(&z, "lb", "ble", 0) ||
         stem(&z, "zi", "ize", 0) ){
       /* Do nothing.  The work was all in the test */
     }else if( doubleConsonant(z) && (*z!='l' && *z!='s' && *z!='z') ){
       z++;
     }else if( m_eq_1(z) && star_oh(z) ){
       *(--z) = 'e';
     }
  }

  /* Step 1c */
  if( z[0]=='y' && hasVowel(z+1) ){
    z[0] = 'i';
  }

  /* Step 2 */
  switch( z[1] ){
   case 'a':
     stem(&z, "lanoita", "ate", m_gt_0) ||
     stem(&z, "lanoit", "tion", m_gt_0);
     break;
   case 'c':
     stem(&z, "icne", "ence", m_gt_0) ||
     stem(&z, "icna", "ance", m_gt_0);
     break;
   case 'e':
     stem(&z, "rezi", "ize", m_gt_0);
     break;
   case 'g':
     stem(&z, "igol", "log", m_gt_0);
     break;
   case 'l':
     stem(&z, "ilb", "ble", m_gt_0) ||
     stem(&z, "illa", "al", m_gt_0) ||
     stem(&z, "iltne", "ent", m_gt_0) ||
     stem(&z, "ile", "e", m_gt_0) ||
     stem(&z, "ilsuo", "ous", m_gt_0);
     break;
   case 'o':
     stem(&z, "noitazi", "ize", m_gt_0) ||
     stem(&z, "noita", "ate", m_gt_0) ||
     stem(&z, "rota", "ate", m_gt_0);
     break;
   case 's':
     stem(&z, "msila", "al", m_gt_0) ||
     stem(&z, "ssenevi", "ive", m_gt_0) ||
     stem(&z, "ssenluf", "ful", m_gt_0) ||
     stem(&z, "ssensuo", "ous", m_gt_0);
     break;
   case 't':
     stem(&z, "itila", "al", m_gt_0) ||
     stem(&z, "itivi", "ive", m_gt_0) ||
     stem(&z, "itilib", "ble", m_gt_0);
     break;
  }

  /* Step 3 */
  switch( z[0] ){
   case 'e':
     stem(&z, "etaci", "ic", m_gt_0) ||
     stem(&z, "evita", "", m_gt_0)   ||
     stem(&z, "ezila", "al", m_gt_0);
     break;
   case 'i':
     stem(&z, "itici", "ic", m_gt_0);
     break;
   case 'l':
     stem(&z, "laci", "ic", m_gt_0) ||
     stem(&z, "luf", "", m_gt_0);
     break;
   case 's':
     stem(&z, "ssen", "", m_gt_0);
     break;
  }

  /* Step 4 */
  switch( z[1] ){
   case 'a':
     if( z[0]=='l' && m_gt_1(z+2) ){
       z += 2;
     }
     break;
   case 'c':
     if( z[0]=='e' && z[2]=='n' && (z[3]=='a' || z[3]=='e')  && m_gt_1(z+4)  ){
       z += 4;
     }
     break;
   case 'e':
     if( z[0]=='r' && m_gt_1(z+2) ){
       z += 2;
     }
     break;
   case 'i':
     if( z[0]=='c' && m_gt_1(z+2) ){
       z += 2;
     }
     break;
   case 'l':
     if( z[0]=='e' && z[2]=='b' && (z[3]=='a' || z[3]=='i') && m_gt_1(z+4) ){
       z += 4;
     }
     break;
   case 'n':
     if( z[0]=='t' ){
       if( z[2]=='a' ){
         if( m_gt_1(z+3) ){
           z += 3;
         }
       }else if( z[2]=='e' ){
         stem(&z, "tneme", "", m_gt_1) ||
         stem(&z, "tnem", "", m_gt_1) ||
         stem(&z, "tne", "", m_gt_1);
       }
     }
     break;
   case 'o':
     if( z[0]=='u' ){
       if( m_gt_1(z+2) ){
         z += 2;
       }
     }else if( z[3]=='s' || z[3]=='t' ){
       stem(&z, "noi", "", m_gt_1);
     }
     break;
   case 's':
     if( z[0]=='m' && z[2]=='i' && m_gt_1(z+3) ){
       z += 3;
     }
     break;
   case 't':
     stem(&z, "eta", "", m_gt_1) ||
     stem(&z, "iti", "", m_gt_1);
     break;
   case 'u':
     if( z[0]=='s' && z[2]=='o' && m_gt_1(z+3) ){
       z += 3;
     }
     break;
   case 'v':
   case 'z':
     if( z[0]=='e' && z[2]=='i' && m_gt_1(z+3) ){
       z += 3;
     }
     break;
  }

  /* Step 5a */
  if( z[0]=='e' ){
    if( m_gt_1(z+1) ){
      z++;
    }else if( m_eq_1(z+1) && !star_oh(z+1) ){
      z++;
    }
  }

  /* Step 5b */
  if( m_gt_1(z) && z[0]=='l' && z[1]=='l' ){
    z++;
  }

  /* z[] is now the stemmed word in reverse order.  Flip it back
  ** around into forward order and return.
  */
  *pnOut = i = (int)strlen(z);
  zOut[i] = 0;
  while( *z ){
    zOut[--i] = *(z++);
  }
}

/*
** Characters that can be part of a token.  We assume any character
** whose value is greater than 0x80 (any UTF character) can be
** part of a token.  In other words, delimiters all must have
** values of 0x7f or lower.
*/
static const char porterIdChar[] = {
/* x0 x1 x2 x3 x4 x5 x6 x7 x8 x9 xA xB xC xD xE xF */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,  /* 3x */
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  /* 4x */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,  /* 5x */
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  /* 6x */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,  /* 7x */
};
#define isDelim(C) (((ch=C)&0x80)==0 && (ch<0x30 || !porterIdChar[ch-0x30]))

/*
** Extract the next token from a tokenization cursor.  The cursor must
** have been opened by a prior call to porterOpen().
*/
static int porterNext(
  sqlite3_tokenizer_cursor *pCursor,  /* Cursor returned by porterOpen */
  const char **pzToken,               /* OUT: *pzToken is the token text */
  int *pnBytes,                       /* OUT: Number of bytes in token */
  int *piStartOffset,                 /* OUT: Starting offset of token */
  int *piEndOffset,                   /* OUT: Ending offset of token */
  int *piPosition                     /* OUT: Position integer of token */
){
  porter_tokenizer_cursor *c = (porter_tokenizer_cursor *) pCursor;
  const char *z = c->zInput;

  while( c->iOffset<c->nInput ){
    int iStartOffset, ch;

    /* Scan past delimiter characters */
    while( c->iOffset<c->nInput && isDelim(z[c->iOffset]) ){
      c->iOffset++;
    }

    /* Count non-delimiter characters. */
    iStartOffset = c->iOffset;
    while( c->iOffset<c->nInput && !isDelim(z[c->iOffset]) ){
      c->iOffset++;
    }

    if( c->iOffset>iStartOffset ){
      int n = c->iOffset-iStartOffset;
      if( n>c->nAllocated ){
        char *pNew;
        c->nAllocated = n+20;
        pNew = sqlite3_realloc(c->zToken, c->nAllocated);
        if( !pNew ) return SQLITE_NOMEM;
        c->zToken = pNew;
      }
      porter_stemmer(&z[iStartOffset], n, c->zToken, pnBytes);
      *pzToken = c->zToken;
      *piStartOffset = iStartOffset;
      *piEndOffset = c->iOffset;
      *piPosition = c->iToken++;
      return SQLITE_OK;
    }
  }
  return SQLITE_DONE;
}

/*
** The set of routines that implement the porter-stemmer tokenizer
*/
static const sqlite3_tokenizer_module porterTokenizerModule = {
  0,
  porterCreate,
  porterDestroy,
  porterOpen,
  porterClose,
  porterNext,
};

/*
** Allocate a new porter tokenizer.  Return a pointer to the new
** tokenizer in *ppModule
*/
SQLITE_PRIVATE void sqlite3Fts3PorterTokenizerModule(
  sqlite3_tokenizer_module const**ppModule
){
  *ppModule = &porterTokenizerModule;
}

#endif /* !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS3) */

/************** End of fts3_porter.c *****************************************/
/************** Begin file fts3_tokenizer.c **********************************/
/*
** 2007 June 22
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
** This is part of an SQLite module implementing full-text search.
** This particular file implements the generic tokenizer interface.
*/

/*
** The code in this file is only compiled if:
**
**     * The FTS3 module is being built as an extension
**       (in which case SQLITE_CORE is not defined), or
**
**     * The FTS3 module is being built into the core of
**       SQLite (in which case SQLITE_ENABLE_FTS3 is defined).
*/
#if !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS3)

/* #include <assert.h> */
/* #include <string.h> */

/*
** Implementation of the SQL scalar function for accessing the underlying 
** hash table. This function may be called as follows:
**
**   SELECT <function-name>(<key-name>);
**   SELECT <function-name>(<key-name>, <pointer>);
**
** where <function-name> is the name passed as the second argument
** to the sqlite3Fts3InitHashTable() function (e.g. 'fts3_tokenizer').
**
** If the <pointer> argument is specified, it must be a blob value
** containing a pointer to be stored as the hash data corresponding
** to the string <key-name>. If <pointer> is not specified, then
** the string <key-name> must already exist in the has table. Otherwise,
** an error is returned.
**
** Whether or not the <pointer> argument is specified, the value returned
** is a blob containing the pointer stored as the hash data corresponding
** to string <key-name> (after the hash-table is updated, if applicable).
*/
static void scalarFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  Fts3Hash *pHash;
  void *pPtr = 0;
  const unsigned char *zName;
  int nName;

  assert( argc==1 || argc==2 );

  pHash = (Fts3Hash *)sqlite3_user_data(context);

  zName = sqlite3_value_text(argv[0]);
  nName = sqlite3_value_bytes(argv[0])+1;

  if( argc==2 ){
    void *pOld;
    int n = sqlite3_value_bytes(argv[1]);
    if( n!=sizeof(pPtr) ){
      sqlite3_result_error(context, "argument type mismatch", -1);
      return;
    }
    pPtr = *(void **)sqlite3_value_blob(argv[1]);
    pOld = sqlite3Fts3HashInsert(pHash, (void *)zName, nName, pPtr);
    if( pOld==pPtr ){
      sqlite3_result_error(context, "out of memory", -1);
      return;
    }
  }else{
    pPtr = sqlite3Fts3HashFind(pHash, zName, nName);
    if( !pPtr ){
      char *zErr = sqlite3_mprintf("unknown tokenizer: %s", zName);
      sqlite3_result_error(context, zErr, -1);
      sqlite3_free(zErr);
      return;
    }
  }

  sqlite3_result_blob(context, (void *)&pPtr, sizeof(pPtr), SQLITE_TRANSIENT);
}

SQLITE_PRIVATE int sqlite3Fts3IsIdChar(char c){
  static const char isFtsIdChar[] = {
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x */
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 1x */
      0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 2x */
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,  /* 3x */
      0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  /* 4x */
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,  /* 5x */
      0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  /* 6x */
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,  /* 7x */
  };
  return (c&0x80 || isFtsIdChar[(int)(c)]);
}

SQLITE_PRIVATE const char *sqlite3Fts3NextToken(const char *zStr, int *pn){
  const char *z1;
  const char *z2 = 0;

  /* Find the start of the next token. */
  z1 = zStr;
  while( z2==0 ){
    char c = *z1;
    switch( c ){
      case '\0': return 0;        /* No more tokens here */
      case '\'':
      case '"':
      case '`': {
        z2 = z1;
        while( *++z2 && (*z2!=c || *++z2==c) );
        break;
      }
      case '[':
        z2 = &z1[1];
        while( *z2 && z2[0]!=']' ) z2++;
        if( *z2 ) z2++;
        break;

      default:
        if( sqlite3Fts3IsIdChar(*z1) ){
          z2 = &z1[1];
          while( sqlite3Fts3IsIdChar(*z2) ) z2++;
        }else{
          z1++;
        }
    }
  }

  *pn = (int)(z2-z1);
  return z1;
}

SQLITE_PRIVATE int sqlite3Fts3InitTokenizer(
  Fts3Hash *pHash,                /* Tokenizer hash table */
  const char *zArg,               /* Tokenizer name */
  sqlite3_tokenizer **ppTok,      /* OUT: Tokenizer (if applicable) */
  char **pzErr                    /* OUT: Set to malloced error message */
){
  int rc;
  char *z = (char *)zArg;
  int n = 0;
  char *zCopy;
  char *zEnd;                     /* Pointer to nul-term of zCopy */
  sqlite3_tokenizer_module *m;

  zCopy = sqlite3_mprintf("%s", zArg);
  if( !zCopy ) return SQLITE_NOMEM;
  zEnd = &zCopy[strlen(zCopy)];

  z = (char *)sqlite3Fts3NextToken(zCopy, &n);
  z[n] = '\0';
  sqlite3Fts3Dequote(z);

  m = (sqlite3_tokenizer_module *)sqlite3Fts3HashFind(pHash,z,(int)strlen(z)+1);
  if( !m ){
    *pzErr = sqlite3_mprintf("unknown tokenizer: %s", z);
    rc = SQLITE_ERROR;
  }else{
    char const **aArg = 0;
    int iArg = 0;
    z = &z[n+1];
    while( z<zEnd && (NULL!=(z = (char *)sqlite3Fts3NextToken(z, &n))) ){
      int nNew = sizeof(char *)*(iArg+1);
      char const **aNew = (const char **)sqlite3_realloc((void *)aArg, nNew);
      if( !aNew ){
        sqlite3_free(zCopy);
        sqlite3_free((void *)aArg);
        return SQLITE_NOMEM;
      }
      aArg = aNew;
      aArg[iArg++] = z;
      z[n] = '\0';
      sqlite3Fts3Dequote(z);
      z = &z[n+1];
    }
    rc = m->xCreate(iArg, aArg, ppTok);
    assert( rc!=SQLITE_OK || *ppTok );
    if( rc!=SQLITE_OK ){
      *pzErr = sqlite3_mprintf("unknown tokenizer");
    }else{
      (*ppTok)->pModule = m; 
    }
    sqlite3_free((void *)aArg);
  }

  sqlite3_free(zCopy);
  return rc;
}


#ifdef SQLITE_TEST

/* #include <tcl.h> */
/* #include <string.h> */

/*
** Implementation of a special SQL scalar function for testing tokenizers 
** designed to be used in concert with the Tcl testing framework. This
** function must be called with two arguments:
**
**   SELECT <function-name>(<key-name>, <input-string>);
**   SELECT <function-name>(<key-name>, <pointer>);
**
** where <function-name> is the name passed as the second argument
** to the sqlite3Fts3InitHashTable() function (e.g. 'fts3_tokenizer')
** concatenated with the string '_test' (e.g. 'fts3_tokenizer_test').
**
** The return value is a string that may be interpreted as a Tcl
** list. For each token in the <input-string>, three elements are
** added to the returned list. The first is the token position, the 
** second is the token text (folded, stemmed, etc.) and the third is the
** substring of <input-string> associated with the token. For example, 
** using the built-in "simple" tokenizer:
**
**   SELECT fts_tokenizer_test('simple', 'I don't see how');
**
** will return the string:
**
**   "{0 i I 1 dont don't 2 see see 3 how how}"
**   
*/
static void testFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  Fts3Hash *pHash;
  sqlite3_tokenizer_module *p;
  sqlite3_tokenizer *pTokenizer = 0;
  sqlite3_tokenizer_cursor *pCsr = 0;

  const char *zErr = 0;

  const char *zName;
  int nName;
  const char *zInput;
  int nInput;

  const char *zArg = 0;

  const char *zToken;
  int nToken;
  int iStart;
  int iEnd;
  int iPos;

  Tcl_Obj *pRet;

  assert( argc==2 || argc==3 );

  nName = sqlite3_value_bytes(argv[0]);
  zName = (const char *)sqlite3_value_text(argv[0]);
  nInput = sqlite3_value_bytes(argv[argc-1]);
  zInput = (const char *)sqlite3_value_text(argv[argc-1]);

  if( argc==3 ){
    zArg = (const char *)sqlite3_value_text(argv[1]);
  }

  pHash = (Fts3Hash *)sqlite3_user_data(context);
  p = (sqlite3_tokenizer_module *)sqlite3Fts3HashFind(pHash, zName, nName+1);

  if( !p ){
    char *zErr = sqlite3_mprintf("unknown tokenizer: %s", zName);
    sqlite3_result_error(context, zErr, -1);
    sqlite3_free(zErr);
    return;
  }

  pRet = Tcl_NewObj();
  Tcl_IncrRefCount(pRet);

  if( SQLITE_OK!=p->xCreate(zArg ? 1 : 0, &zArg, &pTokenizer) ){
    zErr = "error in xCreate()";
    goto finish;
  }
  pTokenizer->pModule = p;
  if( SQLITE_OK!=p->xOpen(pTokenizer, zInput, nInput, &pCsr) ){
    zErr = "error in xOpen()";
    goto finish;
  }
  pCsr->pTokenizer = pTokenizer;

  while( SQLITE_OK==p->xNext(pCsr, &zToken, &nToken, &iStart, &iEnd, &iPos) ){
    Tcl_ListObjAppendElement(0, pRet, Tcl_NewIntObj(iPos));
    Tcl_ListObjAppendElement(0, pRet, Tcl_NewStringObj(zToken, nToken));
    zToken = &zInput[iStart];
    nToken = iEnd-iStart;
    Tcl_ListObjAppendElement(0, pRet, Tcl_NewStringObj(zToken, nToken));
  }

  if( SQLITE_OK!=p->xClose(pCsr) ){
    zErr = "error in xClose()";
    goto finish;
  }
  if( SQLITE_OK!=p->xDestroy(pTokenizer) ){
    zErr = "error in xDestroy()";
    goto finish;
  }

finish:
  if( zErr ){
    sqlite3_result_error(context, zErr, -1);
  }else{
    sqlite3_result_text(context, Tcl_GetString(pRet), -1, SQLITE_TRANSIENT);
  }
  Tcl_DecrRefCount(pRet);
}

static
int registerTokenizer(
  sqlite3 *db, 
  char *zName, 
  const sqlite3_tokenizer_module *p
){
  int rc;
  sqlite3_stmt *pStmt;
  const char zSql[] = "SELECT fts3_tokenizer(?, ?)";

  rc = sqlite3_prepare_v2(db, zSql, -1, &pStmt, 0);
  if( rc!=SQLITE_OK ){
    return rc;
  }

  sqlite3_bind_text(pStmt, 1, zName, -1, SQLITE_STATIC);
  sqlite3_bind_blob(pStmt, 2, &p, sizeof(p), SQLITE_STATIC);
  sqlite3_step(pStmt);

  return sqlite3_finalize(pStmt);
}

static
int queryTokenizer(
  sqlite3 *db, 
  char *zName,  
  const sqlite3_tokenizer_module **pp
){
  int rc;
  sqlite3_stmt *pStmt;
  const char zSql[] = "SELECT fts3_tokenizer(?)";

  *pp = 0;
  rc = sqlite3_prepare_v2(db, zSql, -1, &pStmt, 0);
  if( rc!=SQLITE_OK ){
    return rc;
  }

  sqlite3_bind_text(pStmt, 1, zName, -1, SQLITE_STATIC);
  if( SQLITE_ROW==sqlite3_step(pStmt) ){
    if( sqlite3_column_type(pStmt, 0)==SQLITE_BLOB ){
      memcpy((void *)pp, sqlite3_column_blob(pStmt, 0), sizeof(*pp));
    }
  }

  return sqlite3_finalize(pStmt);
}

SQLITE_PRIVATE void sqlite3Fts3SimpleTokenizerModule(sqlite3_tokenizer_module const**ppModule);

/*
** Implementation of the scalar function fts3_tokenizer_internal_test().
** This function is used for testing only, it is not included in the
** build unless SQLITE_TEST is defined.
**
** The purpose of this is to test that the fts3_tokenizer() function
** can be used as designed by the C-code in the queryTokenizer and
** registerTokenizer() functions above. These two functions are repeated
** in the README.tokenizer file as an example, so it is important to
** test them.
**
** To run the tests, evaluate the fts3_tokenizer_internal_test() scalar
** function with no arguments. An assert() will fail if a problem is
** detected. i.e.:
**
**     SELECT fts3_tokenizer_internal_test();
**
*/
static void intTestFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  int rc;
  const sqlite3_tokenizer_module *p1;
  const sqlite3_tokenizer_module *p2;
  sqlite3 *db = (sqlite3 *)sqlite3_user_data(context);

  UNUSED_PARAMETER(argc);
  UNUSED_PARAMETER(argv);

  /* Test the query function */
  sqlite3Fts3SimpleTokenizerModule(&p1);
  rc = queryTokenizer(db, "simple", &p2);
  assert( rc==SQLITE_OK );
  assert( p1==p2 );
  rc = queryTokenizer(db, "nosuchtokenizer", &p2);
  assert( rc==SQLITE_ERROR );
  assert( p2==0 );
  assert( 0==strcmp(sqlite3_errmsg(db), "unknown tokenizer: nosuchtokenizer") );

  /* Test the storage function */
  rc = registerTokenizer(db, "nosuchtokenizer", p1);
  assert( rc==SQLITE_OK );
  rc = queryTokenizer(db, "nosuchtokenizer", &p2);
  assert( rc==SQLITE_OK );
  assert( p2==p1 );

  sqlite3_result_text(context, "ok", -1, SQLITE_STATIC);
}

#endif

/*
** Set up SQL objects in database db used to access the contents of
** the hash table pointed to by argument pHash. The hash table must
** been initialised to use string keys, and to take a private copy 
** of the key when a value is inserted. i.e. by a call similar to:
**
**    sqlite3Fts3HashInit(pHash, FTS3_HASH_STRING, 1);
**
** This function adds a scalar function (see header comment above
** scalarFunc() in this file for details) and, if ENABLE_TABLE is
** defined at compilation time, a temporary virtual table (see header 
** comment above struct HashTableVtab) to the database schema. Both 
** provide read/write access to the contents of *pHash.
**
** The third argument to this function, zName, is used as the name
** of both the scalar and, if created, the virtual table.
*/
SQLITE_PRIVATE int sqlite3Fts3InitHashTable(
  sqlite3 *db, 
  Fts3Hash *pHash, 
  const char *zName
){
  int rc = SQLITE_OK;
  void *p = (void *)pHash;
  const int any = SQLITE_ANY;

#ifdef SQLITE_TEST
  char *zTest = 0;
  char *zTest2 = 0;
  void *pdb = (void *)db;
  zTest = sqlite3_mprintf("%s_test", zName);
  zTest2 = sqlite3_mprintf("%s_internal_test", zName);
  if( !zTest || !zTest2 ){
    rc = SQLITE_NOMEM;
  }
#endif

  if( SQLITE_OK==rc ){
    rc = sqlite3_create_function(db, zName, 1, any, p, scalarFunc, 0, 0);
  }
  if( SQLITE_OK==rc ){
    rc = sqlite3_create_function(db, zName, 2, any, p, scalarFunc, 0, 0);
  }
#ifdef SQLITE_TEST
  if( SQLITE_OK==rc ){
    rc = sqlite3_create_function(db, zTest, 2, any, p, testFunc, 0, 0);
  }
  if( SQLITE_OK==rc ){
    rc = sqlite3_create_function(db, zTest, 3, any, p, testFunc, 0, 0);
  }
  if( SQLITE_OK==rc ){
    rc = sqlite3_create_function(db, zTest2, 0, any, pdb, intTestFunc, 0, 0);
  }
#endif

#ifdef SQLITE_TEST
  sqlite3_free(zTest);
  sqlite3_free(zTest2);
#endif

  return rc;
}

#endif /* !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS3) */

/************** End of fts3_tokenizer.c **************************************/
/************** Begin file fts3_tokenizer1.c *********************************/
/*
** 2006 Oct 10
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
** Implementation of the "simple" full-text-search tokenizer.
*/

/*
** The code in this file is only compiled if:
**
**     * The FTS3 module is being built as an extension
**       (in which case SQLITE_CORE is not defined), or
**
**     * The FTS3 module is being built into the core of
**       SQLite (in which case SQLITE_ENABLE_FTS3 is defined).
*/
#if !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS3)

/* #include <assert.h> */
/* #include <stdlib.h> */
/* #include <stdio.h> */
/* #include <string.h> */


typedef struct simple_tokenizer {
  sqlite3_tokenizer base;
  char delim[128];             /* flag ASCII delimiters */
} simple_tokenizer;

typedef struct simple_tokenizer_cursor {
  sqlite3_tokenizer_cursor base;
  const char *pInput;          /* input we are tokenizing */
  int nBytes;                  /* size of the input */
  int iOffset;                 /* current position in pInput */
  int iToken;                  /* index of next token to be returned */
  char *pToken;                /* storage for current token */
  int nTokenAllocated;         /* space allocated to zToken buffer */
} simple_tokenizer_cursor;


static int simpleDelim(simple_tokenizer *t, unsigned char c){
  return c<0x80 && t->delim[c];
}
static int fts3_isalnum(int x){
  return (x>='0' && x<='9') || (x>='A' && x<='Z') || (x>='a' && x<='z');
}

/*
** Create a new tokenizer instance.
*/
static int simpleCreate(
  int argc, const char * const *argv,
  sqlite3_tokenizer **ppTokenizer
){
  simple_tokenizer *t;

  t = (simple_tokenizer *) sqlite3_malloc(sizeof(*t));
  if( t==NULL ) return SQLITE_NOMEM;
  memset(t, 0, sizeof(*t));

  /* TODO(shess) Delimiters need to remain the same from run to run,
  ** else we need to reindex.  One solution would be a meta-table to
  ** track such information in the database, then we'd only want this
  ** information on the initial create.
  */
  if( argc>1 ){
    int i, n = (int)strlen(argv[1]);
    for(i=0; i<n; i++){
      unsigned char ch = argv[1][i];
      /* We explicitly don't support UTF-8 delimiters for now. */
      if( ch>=0x80 ){
        sqlite3_free(t);
        return SQLITE_ERROR;
      }
      t->delim[ch] = 1;
    }
  } else {
    /* Mark non-alphanumeric ASCII characters as delimiters */
    int i;
    for(i=1; i<0x80; i++){
      t->delim[i] = !fts3_isalnum(i) ? -1 : 0;
    }
  }

  *ppTokenizer = &t->base;
  return SQLITE_OK;
}

/*
** Destroy a tokenizer
*/
static int simpleDestroy(sqlite3_tokenizer *pTokenizer){
  sqlite3_free(pTokenizer);
  return SQLITE_OK;
}

/*
** Prepare to begin tokenizing a particular string.  The input
** string to be tokenized is pInput[0..nBytes-1].  A cursor
** used to incrementally tokenize this string is returned in 
** *ppCursor.
*/
static int simpleOpen(
  sqlite3_tokenizer *pTokenizer,         /* The tokenizer */
  const char *pInput, int nBytes,        /* String to be tokenized */
  sqlite3_tokenizer_cursor **ppCursor    /* OUT: Tokenization cursor */
){
  simple_tokenizer_cursor *c;

  UNUSED_PARAMETER(pTokenizer);

  c = (simple_tokenizer_cursor *) sqlite3_malloc(sizeof(*c));
  if( c==NULL ) return SQLITE_NOMEM;

  c->pInput = pInput;
  if( pInput==0 ){
    c->nBytes = 0;
  }else if( nBytes<0 ){
    c->nBytes = (int)strlen(pInput);
  }else{
    c->nBytes = nBytes;
  }
  c->iOffset = 0;                 /* start tokenizing at the beginning */
  c->iToken = 0;
  c->pToken = NULL;               /* no space allocated, yet. */
  c->nTokenAllocated = 0;

  *ppCursor = &c->base;
  return SQLITE_OK;
}

/*
** Close a tokenization cursor previously opened by a call to
** simpleOpen() above.
*/
static int simpleClose(sqlite3_tokenizer_cursor *pCursor){
  simple_tokenizer_cursor *c = (simple_tokenizer_cursor *) pCursor;
  sqlite3_free(c->pToken);
  sqlite3_free(c);
  return SQLITE_OK;
}

/*
** Extract the next token from a tokenization cursor.  The cursor must
** have been opened by a prior call to simpleOpen().
*/
static int simpleNext(
  sqlite3_tokenizer_cursor *pCursor,  /* Cursor returned by simpleOpen */
  const char **ppToken,               /* OUT: *ppToken is the token text */
  int *pnBytes,                       /* OUT: Number of bytes in token */
  int *piStartOffset,                 /* OUT: Starting offset of token */
  int *piEndOffset,                   /* OUT: Ending offset of token */
  int *piPosition                     /* OUT: Position integer of token */
){
  simple_tokenizer_cursor *c = (simple_tokenizer_cursor *) pCursor;
  simple_tokenizer *t = (simple_tokenizer *) pCursor->pTokenizer;
  unsigned char *p = (unsigned char *)c->pInput;

  while( c->iOffset<c->nBytes ){
    int iStartOffset;

    /* Scan past delimiter characters */
    while( c->iOffset<c->nBytes && simpleDelim(t, p[c->iOffset]) ){
      c->iOffset++;
    }

    /* Count non-delimiter characters. */
    iStartOffset = c->iOffset;
    while( c->iOffset<c->nBytes && !simpleDelim(t, p[c->iOffset]) ){
      c->iOffset++;
    }

    if( c->iOffset>iStartOffset ){
      int i, n = c->iOffset-iStartOffset;
      if( n>c->nTokenAllocated ){
        char *pNew;
        c->nTokenAllocated = n+20;
        pNew = sqlite3_realloc(c->pToken, c->nTokenAllocated);
        if( !pNew ) return SQLITE_NOMEM;
        c->pToken = pNew;
      }
      for(i=0; i<n; i++){
        /* TODO(shess) This needs expansion to handle UTF-8
        ** case-insensitivity.
        */
        unsigned char ch = p[iStartOffset+i];
        c->pToken[i] = (char)((ch>='A' && ch<='Z') ? ch-'A'+'a' : ch);
      }
      *ppToken = c->pToken;
      *pnBytes = n;
      *piStartOffset = iStartOffset;
      *piEndOffset = c->iOffset;
      *piPosition = c->iToken++;

      return SQLITE_OK;
    }
  }
  return SQLITE_DONE;
}

/*
** The set of routines that implement the simple tokenizer
*/
static const sqlite3_tokenizer_module simpleTokenizerModule = {
  0,
  simpleCreate,
  simpleDestroy,
  simpleOpen,
  simpleClose,
  simpleNext,
};

/*
** Allocate a new simple tokenizer.  Return a pointer to the new
** tokenizer in *ppModule
*/
SQLITE_PRIVATE void sqlite3Fts3SimpleTokenizerModule(
  sqlite3_tokenizer_module const**ppModule
){
  *ppModule = &simpleTokenizerModule;
}

#endif /* !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS3) */

/************** End of fts3_tokenizer1.c *************************************/
/************** Begin file fts3_write.c **************************************/
/*
** 2009 Oct 23
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
** This file is part of the SQLite FTS3 extension module. Specifically,
** this file contains code to insert, update and delete rows from FTS3
** tables. It also contains code to merge FTS3 b-tree segments. Some
** of the sub-routines used to merge segments are also used by the query 
** code in fts3.c.
*/

#if !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS3)

/* #include <string.h> */
/* #include <assert.h> */
/* #include <stdlib.h> */

/*
** When full-text index nodes are loaded from disk, the buffer that they
** are loaded into has the following number of bytes of padding at the end 
** of it. i.e. if a full-text index node is 900 bytes in size, then a buffer
** of 920 bytes is allocated for it.
**
** This means that if we have a pointer into a buffer containing node data,
** it is always safe to read up to two varints from it without risking an
** overread, even if the node data is corrupted.
*/
#define FTS3_NODE_PADDING (FTS3_VARINT_MAX*2)

/*
** Under certain circumstances, b-tree nodes (doclists) can be loaded into
** memory incrementally instead of all at once. This can be a big performance
** win (reduced IO and CPU) if SQLite stops calling the virtual table xNext()
** method before retrieving all query results (as may happen, for example,
** if a query has a LIMIT clause).
**
** Incremental loading is used for b-tree nodes FTS3_NODE_CHUNK_THRESHOLD 
** bytes and larger. Nodes are loaded in chunks of FTS3_NODE_CHUNKSIZE bytes.
** The code is written so that the hard lower-limit for each of these values 
** is 1. Clearly such small values would be inefficient, but can be useful 
** for testing purposes.
**
** If this module is built with SQLITE_TEST defined, these constants may
** be overridden at runtime for testing purposes. File fts3_test.c contains
** a Tcl interface to read and write the values.
*/
#ifdef SQLITE_TEST
int test_fts3_node_chunksize = (4*1024);
int test_fts3_node_chunk_threshold = (4*1024)*4;
# define FTS3_NODE_CHUNKSIZE       test_fts3_node_chunksize
# define FTS3_NODE_CHUNK_THRESHOLD test_fts3_node_chunk_threshold
#else
# define FTS3_NODE_CHUNKSIZE (4*1024) 
# define FTS3_NODE_CHUNK_THRESHOLD (FTS3_NODE_CHUNKSIZE*4)
#endif

typedef struct PendingList PendingList;
typedef struct SegmentNode SegmentNode;
typedef struct SegmentWriter SegmentWriter;

/*
** An instance of the following data structure is used to build doclists
** incrementally. See function fts3PendingListAppend() for details.
*/
struct PendingList {
  int nData;
  char *aData;
  int nSpace;
  sqlite3_int64 iLastDocid;
  sqlite3_int64 iLastCol;
  sqlite3_int64 iLastPos;
};


/*
** Each cursor has a (possibly empty) linked list of the following objects.
*/
struct Fts3DeferredToken {
  Fts3PhraseToken *pToken;        /* Pointer to corresponding expr token */
  int iCol;                       /* Column token must occur in */
  Fts3DeferredToken *pNext;       /* Next in list of deferred tokens */
  PendingList *pList;             /* Doclist is assembled here */
};

/*
** An instance of this structure is used to iterate through the terms on
** a contiguous set of segment b-tree leaf nodes. Although the details of
** this structure are only manipulated by code in this file, opaque handles
** of type Fts3SegReader* are also used by code in fts3.c to iterate through
** terms when querying the full-text index. See functions:
**
**   sqlite3Fts3SegReaderNew()
**   sqlite3Fts3SegReaderFree()
**   sqlite3Fts3SegReaderIterate()
**
** Methods used to manipulate Fts3SegReader structures:
**
**   fts3SegReaderNext()
**   fts3SegReaderFirstDocid()
**   fts3SegReaderNextDocid()
*/
struct Fts3SegReader {
  int iIdx;                       /* Index within level, or 0x7FFFFFFF for PT */

  sqlite3_int64 iStartBlock;      /* Rowid of first leaf block to traverse */
  sqlite3_int64 iLeafEndBlock;    /* Rowid of final leaf block to traverse */
  sqlite3_int64 iEndBlock;        /* Rowid of final block in segment (or 0) */
  sqlite3_int64 iCurrentBlock;    /* Current leaf block (or 0) */

  char *aNode;                    /* Pointer to node data (or NULL) */
  int nNode;                      /* Size of buffer at aNode (or 0) */
  int nPopulate;                  /* If >0, bytes of buffer aNode[] loaded */
  sqlite3_blob *pBlob;            /* If not NULL, blob handle to read node */

  Fts3HashElem **ppNextElem;

  /* Variables set by fts3SegReaderNext(). These may be read directly
  ** by the caller. They are valid from the time SegmentReaderNew() returns
  ** until SegmentReaderNext() returns something other than SQLITE_OK
  ** (i.e. SQLITE_DONE).
  */
  int nTerm;                      /* Number of bytes in current term */
  char *zTerm;                    /* Pointer to current term */
  int nTermAlloc;                 /* Allocated size of zTerm buffer */
  char *aDoclist;                 /* Pointer to doclist of current entry */
  int nDoclist;                   /* Size of doclist in current entry */

  /* The following variables are used by fts3SegReaderNextDocid() to iterate 
  ** through the current doclist (aDoclist/nDoclist).
  */
  char *pOffsetList;
  int nOffsetList;                /* For descending pending seg-readers only */
  sqlite3_int64 iDocid;
};

#define fts3SegReaderIsPending(p) ((p)->ppNextElem!=0)
#define fts3SegReaderIsRootOnly(p) ((p)->aNode==(char *)&(p)[1])

/*
** An instance of this structure is used to create a segment b-tree in the
** database. The internal details of this type are only accessed by the
** following functions:
**
**   fts3SegWriterAdd()
**   fts3SegWriterFlush()
**   fts3SegWriterFree()
*/
struct SegmentWriter {
  SegmentNode *pTree;             /* Pointer to interior tree structure */
  sqlite3_int64 iFirst;           /* First slot in %_segments written */
  sqlite3_int64 iFree;            /* Next free slot in %_segments */
  char *zTerm;                    /* Pointer to previous term buffer */
  int nTerm;                      /* Number of bytes in zTerm */
  int nMalloc;                    /* Size of malloc'd buffer at zMalloc */
  char *zMalloc;                  /* Malloc'd space (possibly) used for zTerm */
  int nSize;                      /* Size of allocation at aData */
  int nData;                      /* Bytes of data in aData */
  char *aData;                    /* Pointer to block from malloc() */
};

/*
** Type SegmentNode is used by the following three functions to create
** the interior part of the segment b+-tree structures (everything except
** the leaf nodes). These functions and type are only ever used by code
** within the fts3SegWriterXXX() family of functions described above.
**
**   fts3NodeAddTerm()
**   fts3NodeWrite()
**   fts3NodeFree()
**
** When a b+tree is written to the database (either as a result of a merge
** or the pending-terms table being flushed), leaves are written into the 
** database file as soon as they are completely populated. The interior of
** the tree is assembled in memory and written out only once all leaves have
** been populated and stored. This is Ok, as the b+-tree fanout is usually
** very large, meaning that the interior of the tree consumes relatively 
** little memory.
*/
struct SegmentNode {
  SegmentNode *pParent;           /* Parent node (or NULL for root node) */
  SegmentNode *pRight;            /* Pointer to right-sibling */
  SegmentNode *pLeftmost;         /* Pointer to left-most node of this depth */
  int nEntry;                     /* Number of terms written to node so far */
  char *zTerm;                    /* Pointer to previous term buffer */
  int nTerm;                      /* Number of bytes in zTerm */
  int nMalloc;                    /* Size of malloc'd buffer at zMalloc */
  char *zMalloc;                  /* Malloc'd space (possibly) used for zTerm */
  int nData;                      /* Bytes of valid data so far */
  char *aData;                    /* Node data */
};

/*
** Valid values for the second argument to fts3SqlStmt().
*/
#define SQL_DELETE_CONTENT             0
#define SQL_IS_EMPTY                   1
#define SQL_DELETE_ALL_CONTENT         2 
#define SQL_DELETE_ALL_SEGMENTS        3
#define SQL_DELETE_ALL_SEGDIR          4
#define SQL_DELETE_ALL_DOCSIZE         5
#define SQL_DELETE_ALL_STAT            6
#define SQL_SELECT_CONTENT_BY_ROWID    7
#define SQL_NEXT_SEGMENT_INDEX         8
#define SQL_INSERT_SEGMENTS            9
#define SQL_NEXT_SEGMENTS_ID          10
#define SQL_INSERT_SEGDIR             11
#define SQL_SELECT_LEVEL              12
#define SQL_SELECT_LEVEL_RANGE        13
#define SQL_SELECT_LEVEL_COUNT        14
#define SQL_SELECT_SEGDIR_MAX_LEVEL   15
#define SQL_DELETE_SEGDIR_LEVEL       16
#define SQL_DELETE_SEGMENTS_RANGE     17
#define SQL_CONTENT_INSERT            18
#define SQL_DELETE_DOCSIZE            19
#define SQL_REPLACE_DOCSIZE           20
#define SQL_SELECT_DOCSIZE            21
#define SQL_SELECT_DOCTOTAL           22
#define SQL_REPLACE_DOCTOTAL          23

#define SQL_SELECT_ALL_PREFIX_LEVEL   24
#define SQL_DELETE_ALL_TERMS_SEGDIR   25

#define SQL_DELETE_SEGDIR_RANGE       26

/*
** This function is used to obtain an SQLite prepared statement handle
** for the statement identified by the second argument. If successful,
** *pp is set to the requested statement handle and SQLITE_OK returned.
** Otherwise, an SQLite error code is returned and *pp is set to 0.
**
** If argument apVal is not NULL, then it must point to an array with
** at least as many entries as the requested statement has bound 
** parameters. The values are bound to the statements parameters before
** returning.
*/
static int fts3SqlStmt(
  Fts3Table *p,                   /* Virtual table handle */
  int eStmt,                      /* One of the SQL_XXX constants above */
  sqlite3_stmt **pp,              /* OUT: Statement handle */
  sqlite3_value **apVal           /* Values to bind to statement */
){
  const char *azSql[] = {
/* 0  */  "DELETE FROM %Q.'%q_content' WHERE rowid = ?",
/* 1  */  "SELECT NOT EXISTS(SELECT docid FROM %Q.'%q_content' WHERE rowid!=?)",
/* 2  */  "DELETE FROM %Q.'%q_content'",
/* 3  */  "DELETE FROM %Q.'%q_segments'",
/* 4  */  "DELETE FROM %Q.'%q_segdir'",
/* 5  */  "DELETE FROM %Q.'%q_docsize'",
/* 6  */  "DELETE FROM %Q.'%q_stat'",
/* 7  */  "SELECT %s WHERE rowid=?",
/* 8  */  "SELECT (SELECT max(idx) FROM %Q.'%q_segdir' WHERE level = ?) + 1",
/* 9  */  "INSERT INTO %Q.'%q_segments'(blockid, block) VALUES(?, ?)",
/* 10 */  "SELECT coalesce((SELECT max(blockid) FROM %Q.'%q_segments') + 1, 1)",
/* 11 */  "INSERT INTO %Q.'%q_segdir' VALUES(?,?,?,?,?,?)",

          /* Return segments in order from oldest to newest.*/ 
/* 12 */  "SELECT idx, start_block, leaves_end_block, end_block, root "
            "FROM %Q.'%q_segdir' WHERE level = ? ORDER BY idx ASC",
/* 13 */  "SELECT idx, start_block, leaves_end_block, end_block, root "
            "FROM %Q.'%q_segdir' WHERE level BETWEEN ? AND ?"
            "ORDER BY level DESC, idx ASC",

/* 14 */  "SELECT count(*) FROM %Q.'%q_segdir' WHERE level = ?",
/* 15 */  "SELECT max(level) FROM %Q.'%q_segdir' WHERE level BETWEEN ? AND ?",

/* 16 */  "DELETE FROM %Q.'%q_segdir' WHERE level = ?",
/* 17 */  "DELETE FROM %Q.'%q_segments' WHERE blockid BETWEEN ? AND ?",
/* 18 */  "INSERT INTO %Q.'%q_content' VALUES(%s)",
/* 19 */  "DELETE FROM %Q.'%q_docsize' WHERE docid = ?",
/* 20 */  "REPLACE INTO %Q.'%q_docsize' VALUES(?,?)",
/* 21 */  "SELECT size FROM %Q.'%q_docsize' WHERE docid=?",
/* 22 */  "SELECT value FROM %Q.'%q_stat' WHERE id=0",
/* 23 */  "REPLACE INTO %Q.'%q_stat' VALUES(0,?)",
/* 24 */  "",
/* 25 */  "",

/* 26 */ "DELETE FROM %Q.'%q_segdir' WHERE level BETWEEN ? AND ?",

  };
  int rc = SQLITE_OK;
  sqlite3_stmt *pStmt;

  assert( SizeofArray(azSql)==SizeofArray(p->aStmt) );
  assert( eStmt<SizeofArray(azSql) && eStmt>=0 );
  
  pStmt = p->aStmt[eStmt];
  if( !pStmt ){
    char *zSql;
    if( eStmt==SQL_CONTENT_INSERT ){
      zSql = sqlite3_mprintf(azSql[eStmt], p->zDb, p->zName, p->zWriteExprlist);
    }else if( eStmt==SQL_SELECT_CONTENT_BY_ROWID ){
      zSql = sqlite3_mprintf(azSql[eStmt], p->zReadExprlist);
    }else{
      zSql = sqlite3_mprintf(azSql[eStmt], p->zDb, p->zName);
    }
    if( !zSql ){
      rc = SQLITE_NOMEM;
    }else{
      rc = sqlite3_prepare_v2(p->db, zSql, -1, &pStmt, NULL);
      sqlite3_free(zSql);
      assert( rc==SQLITE_OK || pStmt==0 );
      p->aStmt[eStmt] = pStmt;
    }
  }
  if( apVal ){
    int i;
    int nParam = sqlite3_bind_parameter_count(pStmt);
    for(i=0; rc==SQLITE_OK && i<nParam; i++){
      rc = sqlite3_bind_value(pStmt, i+1, apVal[i]);
    }
  }
  *pp = pStmt;
  return rc;
}

static int fts3SelectDocsize(
  Fts3Table *pTab,                /* FTS3 table handle */
  int eStmt,                      /* Either SQL_SELECT_DOCSIZE or DOCTOTAL */
  sqlite3_int64 iDocid,           /* Docid to bind for SQL_SELECT_DOCSIZE */
  sqlite3_stmt **ppStmt           /* OUT: Statement handle */
){
  sqlite3_stmt *pStmt = 0;        /* Statement requested from fts3SqlStmt() */
  int rc;                         /* Return code */

  assert( eStmt==SQL_SELECT_DOCSIZE || eStmt==SQL_SELECT_DOCTOTAL );

  rc = fts3SqlStmt(pTab, eStmt, &pStmt, 0);
  if( rc==SQLITE_OK ){
    if( eStmt==SQL_SELECT_DOCSIZE ){
      sqlite3_bind_int64(pStmt, 1, iDocid);
    }
    rc = sqlite3_step(pStmt);
    if( rc!=SQLITE_ROW || sqlite3_column_type(pStmt, 0)!=SQLITE_BLOB ){
      rc = sqlite3_reset(pStmt);
      if( rc==SQLITE_OK ) rc = FTS_CORRUPT_VTAB;
      pStmt = 0;
    }else{
      rc = SQLITE_OK;
    }
  }

  *ppStmt = pStmt;
  return rc;
}

SQLITE_PRIVATE int sqlite3Fts3SelectDoctotal(
  Fts3Table *pTab,                /* Fts3 table handle */
  sqlite3_stmt **ppStmt           /* OUT: Statement handle */
){
  return fts3SelectDocsize(pTab, SQL_SELECT_DOCTOTAL, 0, ppStmt);
}

SQLITE_PRIVATE int sqlite3Fts3SelectDocsize(
  Fts3Table *pTab,                /* Fts3 table handle */
  sqlite3_int64 iDocid,           /* Docid to read size data for */
  sqlite3_stmt **ppStmt           /* OUT: Statement handle */
){
  return fts3SelectDocsize(pTab, SQL_SELECT_DOCSIZE, iDocid, ppStmt);
}

/*
** Similar to fts3SqlStmt(). Except, after binding the parameters in
** array apVal[] to the SQL statement identified by eStmt, the statement
** is executed.
**
** Returns SQLITE_OK if the statement is successfully executed, or an
** SQLite error code otherwise.
*/
static void fts3SqlExec(
  int *pRC,                /* Result code */
  Fts3Table *p,            /* The FTS3 table */
  int eStmt,               /* Index of statement to evaluate */
  sqlite3_value **apVal    /* Parameters to bind */
){
  sqlite3_stmt *pStmt;
  int rc;
  if( *pRC ) return;
  rc = fts3SqlStmt(p, eStmt, &pStmt, apVal); 
  if( rc==SQLITE_OK ){
    sqlite3_step(pStmt);
    rc = sqlite3_reset(pStmt);
  }
  *pRC = rc;
}


/*
** This function ensures that the caller has obtained a shared-cache
** table-lock on the %_content table. This is required before reading
** data from the fts3 table. If this lock is not acquired first, then
** the caller may end up holding read-locks on the %_segments and %_segdir
** tables, but no read-lock on the %_content table. If this happens 
** a second connection will be able to write to the fts3 table, but
** attempting to commit those writes might return SQLITE_LOCKED or
** SQLITE_LOCKED_SHAREDCACHE (because the commit attempts to obtain 
** write-locks on the %_segments and %_segdir ** tables). 
**
** We try to avoid this because if FTS3 returns any error when committing
** a transaction, the whole transaction will be rolled back. And this is
** not what users expect when they get SQLITE_LOCKED_SHAREDCACHE. It can
** still happen if the user reads data directly from the %_segments or
** %_segdir tables instead of going through FTS3 though.
**
** This reasoning does not apply to a content=xxx table.
*/
SQLITE_PRIVATE int sqlite3Fts3ReadLock(Fts3Table *p){
  int rc;                         /* Return code */
  sqlite3_stmt *pStmt;            /* Statement used to obtain lock */

  if( p->zContentTbl==0 ){
    rc = fts3SqlStmt(p, SQL_SELECT_CONTENT_BY_ROWID, &pStmt, 0);
    if( rc==SQLITE_OK ){
      sqlite3_bind_null(pStmt, 1);
      sqlite3_step(pStmt);
      rc = sqlite3_reset(pStmt);
    }
  }else{
    rc = SQLITE_OK;
  }

  return rc;
}

/*
** Set *ppStmt to a statement handle that may be used to iterate through
** all rows in the %_segdir table, from oldest to newest. If successful,
** return SQLITE_OK. If an error occurs while preparing the statement, 
** return an SQLite error code.
**
** There is only ever one instance of this SQL statement compiled for
** each FTS3 table.
**
** The statement returns the following columns from the %_segdir table:
**
**   0: idx
**   1: start_block
**   2: leaves_end_block
**   3: end_block
**   4: root
*/
SQLITE_PRIVATE int sqlite3Fts3AllSegdirs(
  Fts3Table *p,                   /* FTS3 table */
  int iIndex,                     /* Index for p->aIndex[] */
  int iLevel,                     /* Level to select */
  sqlite3_stmt **ppStmt           /* OUT: Compiled statement */
){
  int rc;
  sqlite3_stmt *pStmt = 0;

  assert( iLevel==FTS3_SEGCURSOR_ALL || iLevel>=0 );
  assert( iLevel<FTS3_SEGDIR_MAXLEVEL );
  assert( iIndex>=0 && iIndex<p->nIndex );

  if( iLevel<0 ){
    /* "SELECT * FROM %_segdir WHERE level BETWEEN ? AND ? ORDER BY ..." */
    rc = fts3SqlStmt(p, SQL_SELECT_LEVEL_RANGE, &pStmt, 0);
    if( rc==SQLITE_OK ){ 
      sqlite3_bind_int(pStmt, 1, iIndex*FTS3_SEGDIR_MAXLEVEL);
      sqlite3_bind_int(pStmt, 2, (iIndex+1)*FTS3_SEGDIR_MAXLEVEL-1);
    }
  }else{
    /* "SELECT * FROM %_segdir WHERE level = ? ORDER BY ..." */
    rc = fts3SqlStmt(p, SQL_SELECT_LEVEL, &pStmt, 0);
    if( rc==SQLITE_OK ){ 
      sqlite3_bind_int(pStmt, 1, iLevel+iIndex*FTS3_SEGDIR_MAXLEVEL);
    }
  }
  *ppStmt = pStmt;
  return rc;
}


/*
** Append a single varint to a PendingList buffer. SQLITE_OK is returned
** if successful, or an SQLite error code otherwise.
**
** This function also serves to allocate the PendingList structure itself.
** For example, to create a new PendingList structure containing two
** varints:
**
**   PendingList *p = 0;
**   fts3PendingListAppendVarint(&p, 1);
**   fts3PendingListAppendVarint(&p, 2);
*/
static int fts3PendingListAppendVarint(
  PendingList **pp,               /* IN/OUT: Pointer to PendingList struct */
  sqlite3_int64 i                 /* Value to append to data */
){
  PendingList *p = *pp;

  /* Allocate or grow the PendingList as required. */
  if( !p ){
    p = sqlite3_malloc(sizeof(*p) + 100);
    if( !p ){
      return SQLITE_NOMEM;
    }
    p->nSpace = 100;
    p->aData = (char *)&p[1];
    p->nData = 0;
  }
  else if( p->nData+FTS3_VARINT_MAX+1>p->nSpace ){
    int nNew = p->nSpace * 2;
    p = sqlite3_realloc(p, sizeof(*p) + nNew);
    if( !p ){
      sqlite3_free(*pp);
      *pp = 0;
      return SQLITE_NOMEM;
    }
    p->nSpace = nNew;
    p->aData = (char *)&p[1];
  }

  /* Append the new serialized varint to the end of the list. */
  p->nData += sqlite3Fts3PutVarint(&p->aData[p->nData], i);
  p->aData[p->nData] = '\0';
  *pp = p;
  return SQLITE_OK;
}

/*
** Add a docid/column/position entry to a PendingList structure. Non-zero
** is returned if the structure is sqlite3_realloced as part of adding
** the entry. Otherwise, zero.
**
** If an OOM error occurs, *pRc is set to SQLITE_NOMEM before returning.
** Zero is always returned in this case. Otherwise, if no OOM error occurs,
** it is set to SQLITE_OK.
*/
static int fts3PendingListAppend(
  PendingList **pp,               /* IN/OUT: PendingList structure */
  sqlite3_int64 iDocid,           /* Docid for entry to add */
  sqlite3_int64 iCol,             /* Column for entry to add */
  sqlite3_int64 iPos,             /* Position of term for entry to add */
  int *pRc                        /* OUT: Return code */
){
  PendingList *p = *pp;
  int rc = SQLITE_OK;

  assert( !p || p->iLastDocid<=iDocid );

  if( !p || p->iLastDocid!=iDocid ){
    sqlite3_int64 iDelta = iDocid - (p ? p->iLastDocid : 0);
    if( p ){
      assert( p->nData<p->nSpace );
      assert( p->aData[p->nData]==0 );
      p->nData++;
    }
    if( SQLITE_OK!=(rc = fts3PendingListAppendVarint(&p, iDelta)) ){
      goto pendinglistappend_out;
    }
    p->iLastCol = -1;
    p->iLastPos = 0;
    p->iLastDocid = iDocid;
  }
  if( iCol>0 && p->iLastCol!=iCol ){
    if( SQLITE_OK!=(rc = fts3PendingListAppendVarint(&p, 1))
     || SQLITE_OK!=(rc = fts3PendingListAppendVarint(&p, iCol))
    ){
      goto pendinglistappend_out;
    }
    p->iLastCol = iCol;
    p->iLastPos = 0;
  }
  if( iCol>=0 ){
    assert( iPos>p->iLastPos || (iPos==0 && p->iLastPos==0) );
    rc = fts3PendingListAppendVarint(&p, 2+iPos-p->iLastPos);
    if( rc==SQLITE_OK ){
      p->iLastPos = iPos;
    }
  }

 pendinglistappend_out:
  *pRc = rc;
  if( p!=*pp ){
    *pp = p;
    return 1;
  }
  return 0;
}

/*
** Free a PendingList object allocated by fts3PendingListAppend().
*/
static void fts3PendingListDelete(PendingList *pList){
  sqlite3_free(pList);
}

/*
** Add an entry to one of the pending-terms hash tables.
*/
static int fts3PendingTermsAddOne(
  Fts3Table *p,
  int iCol,
  int iPos,
  Fts3Hash *pHash,                /* Pending terms hash table to add entry to */
  const char *zToken,
  int nToken
){
  PendingList *pList;
  int rc = SQLITE_OK;

  pList = (PendingList *)fts3HashFind(pHash, zToken, nToken);
  if( pList ){
    p->nPendingData -= (pList->nData + nToken + sizeof(Fts3HashElem));
  }
  if( fts3PendingListAppend(&pList, p->iPrevDocid, iCol, iPos, &rc) ){
    if( pList==fts3HashInsert(pHash, zToken, nToken, pList) ){
      /* Malloc failed while inserting the new entry. This can only 
      ** happen if there was no previous entry for this token.
      */
      assert( 0==fts3HashFind(pHash, zToken, nToken) );
      sqlite3_free(pList);
      rc = SQLITE_NOMEM;
    }
  }
  if( rc==SQLITE_OK ){
    p->nPendingData += (pList->nData + nToken + sizeof(Fts3HashElem));
  }
  return rc;
}

/*
** Tokenize the nul-terminated string zText and add all tokens to the
** pending-terms hash-table. The docid used is that currently stored in
** p->iPrevDocid, and the column is specified by argument iCol.
**
** If successful, SQLITE_OK is returned. Otherwise, an SQLite error code.
*/
static int fts3PendingTermsAdd(
  Fts3Table *p,                   /* Table into which text will be inserted */
  const char *zText,              /* Text of document to be inserted */
  int iCol,                       /* Column into which text is being inserted */
  u32 *pnWord                     /* OUT: Number of tokens inserted */
){
  int rc;
  int iStart;
  int iEnd;
  int iPos;
  int nWord = 0;

  char const *zToken;
  int nToken;

  sqlite3_tokenizer *pTokenizer = p->pTokenizer;
  sqlite3_tokenizer_module const *pModule = pTokenizer->pModule;
  sqlite3_tokenizer_cursor *pCsr;
  int (*xNext)(sqlite3_tokenizer_cursor *pCursor,
      const char**,int*,int*,int*,int*);

  assert( pTokenizer && pModule );

  /* If the user has inserted a NULL value, this function may be called with
  ** zText==0. In this case, add zero token entries to the hash table and 
  ** return early. */
  if( zText==0 ){
    *pnWord = 0;
    return SQLITE_OK;
  }

  rc = pModule->xOpen(pTokenizer, zText, -1, &pCsr);
  if( rc!=SQLITE_OK ){
    return rc;
  }
  pCsr->pTokenizer = pTokenizer;

  xNext = pModule->xNext;
  while( SQLITE_OK==rc
      && SQLITE_OK==(rc = xNext(pCsr, &zToken, &nToken, &iStart, &iEnd, &iPos))
  ){
    int i;
    if( iPos>=nWord ) nWord = iPos+1;

    /* Positions cannot be negative; we use -1 as a terminator internally.
    ** Tokens must have a non-zero length.
    */
    if( iPos<0 || !zToken || nToken<=0 ){
      rc = SQLITE_ERROR;
      break;
    }

    /* Add the term to the terms index */
    rc = fts3PendingTermsAddOne(
        p, iCol, iPos, &p->aIndex[0].hPending, zToken, nToken
    );
    
    /* Add the term to each of the prefix indexes that it is not too 
    ** short for. */
    for(i=1; rc==SQLITE_OK && i<p->nIndex; i++){
      struct Fts3Index *pIndex = &p->aIndex[i];
      if( nToken<pIndex->nPrefix ) continue;
      rc = fts3PendingTermsAddOne(
          p, iCol, iPos, &pIndex->hPending, zToken, pIndex->nPrefix
      );
    }
  }

  pModule->xClose(pCsr);
  *pnWord = nWord;
  return (rc==SQLITE_DONE ? SQLITE_OK : rc);
}

/* 
** Calling this function indicates that subsequent calls to 
** fts3PendingTermsAdd() are to add term/position-list pairs for the
** contents of the document with docid iDocid.
*/
static int fts3PendingTermsDocid(Fts3Table *p, sqlite_int64 iDocid){
  /* TODO(shess) Explore whether partially flushing the buffer on
  ** forced-flush would provide better performance.  I suspect that if
  ** we ordered the doclists by size and flushed the largest until the
  ** buffer was half empty, that would let the less frequent terms
  ** generate longer doclists.
  */
  if( iDocid<=p->iPrevDocid || p->nPendingData>p->nMaxPendingData ){
    int rc = sqlite3Fts3PendingTermsFlush(p);
    if( rc!=SQLITE_OK ) return rc;
  }
  p->iPrevDocid = iDocid;
  return SQLITE_OK;
}

/*
** Discard the contents of the pending-terms hash tables. 
*/
SQLITE_PRIVATE void sqlite3Fts3PendingTermsClear(Fts3Table *p){
  int i;
  for(i=0; i<p->nIndex; i++){
    Fts3HashElem *pElem;
    Fts3Hash *pHash = &p->aIndex[i].hPending;
    for(pElem=fts3HashFirst(pHash); pElem; pElem=fts3HashNext(pElem)){
      PendingList *pList = (PendingList *)fts3HashData(pElem);
      fts3PendingListDelete(pList);
    }
    fts3HashClear(pHash);
  }
  p->nPendingData = 0;
}

/*
** This function is called by the xUpdate() method as part of an INSERT
** operation. It adds entries for each term in the new record to the
** pendingTerms hash table.
**
** Argument apVal is the same as the similarly named argument passed to
** fts3InsertData(). Parameter iDocid is the docid of the new row.
*/
static int fts3InsertTerms(Fts3Table *p, sqlite3_value **apVal, u32 *aSz){
  int i;                          /* Iterator variable */
  for(i=2; i<p->nColumn+2; i++){
    const char *zText = (const char *)sqlite3_value_text(apVal[i]);
    int rc = fts3PendingTermsAdd(p, zText, i-2, &aSz[i-2]);
    if( rc!=SQLITE_OK ){
      return rc;
    }
    aSz[p->nColumn] += sqlite3_value_bytes(apVal[i]);
  }
  return SQLITE_OK;
}

/*
** This function is called by the xUpdate() method for an INSERT operation.
** The apVal parameter is passed a copy of the apVal argument passed by
** SQLite to the xUpdate() method. i.e:
**
**   apVal[0]                Not used for INSERT.
**   apVal[1]                rowid
**   apVal[2]                Left-most user-defined column
**   ...
**   apVal[p->nColumn+1]     Right-most user-defined column
**   apVal[p->nColumn+2]     Hidden column with same name as table
**   apVal[p->nColumn+3]     Hidden "docid" column (alias for rowid)
*/
static int fts3InsertData(
  Fts3Table *p,                   /* Full-text table */
  sqlite3_value **apVal,          /* Array of values to insert */
  sqlite3_int64 *piDocid          /* OUT: Docid for row just inserted */
){
  int rc;                         /* Return code */
  sqlite3_stmt *pContentInsert;   /* INSERT INTO %_content VALUES(...) */

  if( p->zContentTbl ){
    sqlite3_value *pRowid = apVal[p->nColumn+3];
    if( sqlite3_value_type(pRowid)==SQLITE_NULL ){
      pRowid = apVal[1];
    }
    if( sqlite3_value_type(pRowid)!=SQLITE_INTEGER ){
      return SQLITE_CONSTRAINT;
    }
    *piDocid = sqlite3_value_int64(pRowid);
    return SQLITE_OK;
  }

  /* Locate the statement handle used to insert data into the %_content
  ** table. The SQL for this statement is:
  **
  **   INSERT INTO %_content VALUES(?, ?, ?, ...)
  **
  ** The statement features N '?' variables, where N is the number of user
  ** defined columns in the FTS3 table, plus one for the docid field.
  */
  rc = fts3SqlStmt(p, SQL_CONTENT_INSERT, &pContentInsert, &apVal[1]);
  if( rc!=SQLITE_OK ){
    return rc;
  }

  /* There is a quirk here. The users INSERT statement may have specified
  ** a value for the "rowid" field, for the "docid" field, or for both.
  ** Which is a problem, since "rowid" and "docid" are aliases for the
  ** same value. For example:
  **
  **   INSERT INTO fts3tbl(rowid, docid) VALUES(1, 2);
  **
  ** In FTS3, this is an error. It is an error to specify non-NULL values
  ** for both docid and some other rowid alias.
  */
  if( SQLITE_NULL!=sqlite3_value_type(apVal[3+p->nColumn]) ){
    if( SQLITE_NULL==sqlite3_value_type(apVal[0])
     && SQLITE_NULL!=sqlite3_value_type(apVal[1])
    ){
      /* A rowid/docid conflict. */
      return SQLITE_ERROR;
    }
    rc = sqlite3_bind_value(pContentInsert, 1, apVal[3+p->nColumn]);
    if( rc!=SQLITE_OK ) return rc;
  }

  /* Execute the statement to insert the record. Set *piDocid to the 
  ** new docid value. 
  */
  sqlite3_step(pContentInsert);
  rc = sqlite3_reset(pContentInsert);

  *piDocid = sqlite3_last_insert_rowid(p->db);
  return rc;
}



/*
** Remove all data from the FTS3 table. Clear the hash table containing
** pending terms.
*/
static int fts3DeleteAll(Fts3Table *p, int bContent){
  int rc = SQLITE_OK;             /* Return code */

  /* Discard the contents of the pending-terms hash table. */
  sqlite3Fts3PendingTermsClear(p);

  /* Delete everything from the shadow tables. Except, leave %_content as
  ** is if bContent is false.  */
  assert( p->zContentTbl==0 || bContent==0 );
  if( bContent ) fts3SqlExec(&rc, p, SQL_DELETE_ALL_CONTENT, 0);
  fts3SqlExec(&rc, p, SQL_DELETE_ALL_SEGMENTS, 0);
  fts3SqlExec(&rc, p, SQL_DELETE_ALL_SEGDIR, 0);
  if( p->bHasDocsize ){
    fts3SqlExec(&rc, p, SQL_DELETE_ALL_DOCSIZE, 0);
  }
  if( p->bHasStat ){
    fts3SqlExec(&rc, p, SQL_DELETE_ALL_STAT, 0);
  }
  return rc;
}

/*
** The first element in the apVal[] array is assumed to contain the docid
** (an integer) of a row about to be deleted. Remove all terms from the
** full-text index.
*/
static void fts3DeleteTerms( 
  int *pRC,               /* Result code */
  Fts3Table *p,           /* The FTS table to delete from */
  sqlite3_value *pRowid,  /* The docid to be deleted */
  u32 *aSz                /* Sizes of deleted document written here */
){
  int rc;
  sqlite3_stmt *pSelect;

  if( *pRC ) return;
  rc = fts3SqlStmt(p, SQL_SELECT_CONTENT_BY_ROWID, &pSelect, &pRowid);
  if( rc==SQLITE_OK ){
    if( SQLITE_ROW==sqlite3_step(pSelect) ){
      int i;
      for(i=1; i<=p->nColumn; i++){
        const char *zText = (const char *)sqlite3_column_text(pSelect, i);
        rc = fts3PendingTermsAdd(p, zText, -1, &aSz[i-1]);
        if( rc!=SQLITE_OK ){
          sqlite3_reset(pSelect);
          *pRC = rc;
          return;
        }
        aSz[p->nColumn] += sqlite3_column_bytes(pSelect, i);
      }
    }
    rc = sqlite3_reset(pSelect);
  }else{
    sqlite3_reset(pSelect);
  }
  *pRC = rc;
}

/*
** Forward declaration to account for the circular dependency between
** functions fts3SegmentMerge() and fts3AllocateSegdirIdx().
*/
static int fts3SegmentMerge(Fts3Table *, int, int);

/* 
** This function allocates a new level iLevel index in the segdir table.
** Usually, indexes are allocated within a level sequentially starting
** with 0, so the allocated index is one greater than the value returned
** by:
**
**   SELECT max(idx) FROM %_segdir WHERE level = :iLevel
**
** However, if there are already FTS3_MERGE_COUNT indexes at the requested
** level, they are merged into a single level (iLevel+1) segment and the 
** allocated index is 0.
**
** If successful, *piIdx is set to the allocated index slot and SQLITE_OK
** returned. Otherwise, an SQLite error code is returned.
*/
static int fts3AllocateSegdirIdx(
  Fts3Table *p, 
  int iIndex,                     /* Index for p->aIndex */
  int iLevel, 
  int *piIdx
){
  int rc;                         /* Return Code */
  sqlite3_stmt *pNextIdx;         /* Query for next idx at level iLevel */
  int iNext = 0;                  /* Result of query pNextIdx */

  /* Set variable iNext to the next available segdir index at level iLevel. */
  rc = fts3SqlStmt(p, SQL_NEXT_SEGMENT_INDEX, &pNextIdx, 0);
  if( rc==SQLITE_OK ){
    sqlite3_bind_int(pNextIdx, 1, iIndex*FTS3_SEGDIR_MAXLEVEL + iLevel);
    if( SQLITE_ROW==sqlite3_step(pNextIdx) ){
      iNext = sqlite3_column_int(pNextIdx, 0);
    }
    rc = sqlite3_reset(pNextIdx);
  }

  if( rc==SQLITE_OK ){
    /* If iNext is FTS3_MERGE_COUNT, indicating that level iLevel is already
    ** full, merge all segments in level iLevel into a single iLevel+1
    ** segment and allocate (newly freed) index 0 at level iLevel. Otherwise,
    ** if iNext is less than FTS3_MERGE_COUNT, allocate index iNext.
    */
    if( iNext>=FTS3_MERGE_COUNT ){
      rc = fts3SegmentMerge(p, iIndex, iLevel);
      *piIdx = 0;
    }else{
      *piIdx = iNext;
    }
  }

  return rc;
}

/*
** The %_segments table is declared as follows:
**
**   CREATE TABLE %_segments(blockid INTEGER PRIMARY KEY, block BLOB)
**
** This function reads data from a single row of the %_segments table. The
** specific row is identified by the iBlockid parameter. If paBlob is not
** NULL, then a buffer is allocated using sqlite3_malloc() and populated
** with the contents of the blob stored in the "block" column of the 
** identified table row is. Whether or not paBlob is NULL, *pnBlob is set
** to the size of the blob in bytes before returning.
**
** If an error occurs, or the table does not contain the specified row,
** an SQLite error code is returned. Otherwise, SQLITE_OK is returned. If
** paBlob is non-NULL, then it is the responsibility of the caller to
** eventually free the returned buffer.
**
** This function may leave an open sqlite3_blob* handle in the
** Fts3Table.pSegments variable. This handle is reused by subsequent calls
** to this function. The handle may be closed by calling the
** sqlite3Fts3SegmentsClose() function. Reusing a blob handle is a handy
** performance improvement, but the blob handle should always be closed
** before control is returned to the user (to prevent a lock being held
** on the database file for longer than necessary). Thus, any virtual table
** method (xFilter etc.) that may directly or indirectly call this function
** must call sqlite3Fts3SegmentsClose() before returning.
*/
SQLITE_PRIVATE int sqlite3Fts3ReadBlock(
  Fts3Table *p,                   /* FTS3 table handle */
  sqlite3_int64 iBlockid,         /* Access the row with blockid=$iBlockid */
  char **paBlob,                  /* OUT: Blob data in malloc'd buffer */
  int *pnBlob,                    /* OUT: Size of blob data */
  int *pnLoad                     /* OUT: Bytes actually loaded */
){
  int rc;                         /* Return code */

  /* pnBlob must be non-NULL. paBlob may be NULL or non-NULL. */
  assert( pnBlob);

  if( p->pSegments ){
    rc = sqlite3_blob_reopen(p->pSegments, iBlockid);
  }else{
    if( 0==p->zSegmentsTbl ){
      p->zSegmentsTbl = sqlite3_mprintf("%s_segments", p->zName);
      if( 0==p->zSegmentsTbl ) return SQLITE_NOMEM;
    }
    rc = sqlite3_blob_open(
       p->db, p->zDb, p->zSegmentsTbl, "block", iBlockid, 0, &p->pSegments
    );
  }

  if( rc==SQLITE_OK ){
    int nByte = sqlite3_blob_bytes(p->pSegments);
    *pnBlob = nByte;
    if( paBlob ){
      char *aByte = sqlite3_malloc(nByte + FTS3_NODE_PADDING);
      if( !aByte ){
        rc = SQLITE_NOMEM;
      }else{
        if( pnLoad && nByte>(FTS3_NODE_CHUNK_THRESHOLD) ){
          nByte = FTS3_NODE_CHUNKSIZE;
          *pnLoad = nByte;
        }
        rc = sqlite3_blob_read(p->pSegments, aByte, nByte, 0);
        memset(&aByte[nByte], 0, FTS3_NODE_PADDING);
        if( rc!=SQLITE_OK ){
          sqlite3_free(aByte);
          aByte = 0;
        }
      }
      *paBlob = aByte;
    }
  }

  return rc;
}

/*
** Close the blob handle at p->pSegments, if it is open. See comments above
** the sqlite3Fts3ReadBlock() function for details.
*/
SQLITE_PRIVATE void sqlite3Fts3SegmentsClose(Fts3Table *p){
  sqlite3_blob_close(p->pSegments);
  p->pSegments = 0;
}
    
static int fts3SegReaderIncrRead(Fts3SegReader *pReader){
  int nRead;                      /* Number of bytes to read */
  int rc;                         /* Return code */

  nRead = MIN(pReader->nNode - pReader->nPopulate, FTS3_NODE_CHUNKSIZE);
  rc = sqlite3_blob_read(
      pReader->pBlob, 
      &pReader->aNode[pReader->nPopulate],
      nRead,
      pReader->nPopulate
  );

  if( rc==SQLITE_OK ){
    pReader->nPopulate += nRead;
    memset(&pReader->aNode[pReader->nPopulate], 0, FTS3_NODE_PADDING);
    if( pReader->nPopulate==pReader->nNode ){
      sqlite3_blob_close(pReader->pBlob);
      pReader->pBlob = 0;
      pReader->nPopulate = 0;
    }
  }
  return rc;
}

static int fts3SegReaderRequire(Fts3SegReader *pReader, char *pFrom, int nByte){
  int rc = SQLITE_OK;
  assert( !pReader->pBlob 
       || (pFrom>=pReader->aNode && pFrom<&pReader->aNode[pReader->nNode])
  );
  while( pReader->pBlob && rc==SQLITE_OK 
     &&  (pFrom - pReader->aNode + nByte)>pReader->nPopulate
  ){
    rc = fts3SegReaderIncrRead(pReader);
  }
  return rc;
}

/*
** Move the iterator passed as the first argument to the next term in the
** segment. If successful, SQLITE_OK is returned. If there is no next term,
** SQLITE_DONE. Otherwise, an SQLite error code.
*/
static int fts3SegReaderNext(
  Fts3Table *p, 
  Fts3SegReader *pReader,
  int bIncr
){
  int rc;                         /* Return code of various sub-routines */
  char *pNext;                    /* Cursor variable */
  int nPrefix;                    /* Number of bytes in term prefix */
  int nSuffix;                    /* Number of bytes in term suffix */

  if( !pReader->aDoclist ){
    pNext = pReader->aNode;
  }else{
    pNext = &pReader->aDoclist[pReader->nDoclist];
  }

  if( !pNext || pNext>=&pReader->aNode[pReader->nNode] ){

    if( fts3SegReaderIsPending(pReader) ){
      Fts3HashElem *pElem = *(pReader->ppNextElem);
      if( pElem==0 ){
        pReader->aNode = 0;
      }else{
        PendingList *pList = (PendingList *)fts3HashData(pElem);
        pReader->zTerm = (char *)fts3HashKey(pElem);
        pReader->nTerm = fts3HashKeysize(pElem);
        pReader->nNode = pReader->nDoclist = pList->nData + 1;
        pReader->aNode = pReader->aDoclist = pList->aData;
        pReader->ppNextElem++;
        assert( pReader->aNode );
      }
      return SQLITE_OK;
    }

    if( !fts3SegReaderIsRootOnly(pReader) ){
      sqlite3_free(pReader->aNode);
      sqlite3_blob_close(pReader->pBlob);
      pReader->pBlob = 0;
    }
    pReader->aNode = 0;

    /* If iCurrentBlock>=iLeafEndBlock, this is an EOF condition. All leaf 
    ** blocks have already been traversed.  */
    assert( pReader->iCurrentBlock<=pReader->iLeafEndBlock );
    if( pReader->iCurrentBlock>=pReader->iLeafEndBlock ){
      return SQLITE_OK;
    }

    rc = sqlite3Fts3ReadBlock(
        p, ++pReader->iCurrentBlock, &pReader->aNode, &pReader->nNode, 
        (bIncr ? &pReader->nPopulate : 0)
    );
    if( rc!=SQLITE_OK ) return rc;
    assert( pReader->pBlob==0 );
    if( bIncr && pReader->nPopulate<pReader->nNode ){
      pReader->pBlob = p->pSegments;
      p->pSegments = 0;
    }
    pNext = pReader->aNode;
  }

  assert( !fts3SegReaderIsPending(pReader) );

  rc = fts3SegReaderRequire(pReader, pNext, FTS3_VARINT_MAX*2);
  if( rc!=SQLITE_OK ) return rc;
  
  /* Because of the FTS3_NODE_PADDING bytes of padding, the following is 
  ** safe (no risk of overread) even if the node data is corrupted. */
  pNext += sqlite3Fts3GetVarint32(pNext, &nPrefix);
  pNext += sqlite3Fts3GetVarint32(pNext, &nSuffix);
  if( nPrefix<0 || nSuffix<=0 
   || &pNext[nSuffix]>&pReader->aNode[pReader->nNode] 
  ){
    return FTS_CORRUPT_VTAB;
  }

  if( nPrefix+nSuffix>pReader->nTermAlloc ){
    int nNew = (nPrefix+nSuffix)*2;
    char *zNew = sqlite3_realloc(pReader->zTerm, nNew);
    if( !zNew ){
      return SQLITE_NOMEM;
    }
    pReader->zTerm = zNew;
    pReader->nTermAlloc = nNew;
  }

  rc = fts3SegReaderRequire(pReader, pNext, nSuffix+FTS3_VARINT_MAX);
  if( rc!=SQLITE_OK ) return rc;

  memcpy(&pReader->zTerm[nPrefix], pNext, nSuffix);
  pReader->nTerm = nPrefix+nSuffix;
  pNext += nSuffix;
  pNext += sqlite3Fts3GetVarint32(pNext, &pReader->nDoclist);
  pReader->aDoclist = pNext;
  pReader->pOffsetList = 0;

  /* Check that the doclist does not appear to extend past the end of the
  ** b-tree node. And that the final byte of the doclist is 0x00. If either 
  ** of these statements is untrue, then the data structure is corrupt.
  */
  if( &pReader->aDoclist[pReader->nDoclist]>&pReader->aNode[pReader->nNode] 
   || (pReader->nPopulate==0 && pReader->aDoclist[pReader->nDoclist-1])
  ){
    return FTS_CORRUPT_VTAB;
  }
  return SQLITE_OK;
}

/*
** Set the SegReader to point to the first docid in the doclist associated
** with the current term.
*/
static int fts3SegReaderFirstDocid(Fts3Table *pTab, Fts3SegReader *pReader){
  int rc = SQLITE_OK;
  assert( pReader->aDoclist );
  assert( !pReader->pOffsetList );
  if( pTab->bDescIdx && fts3SegReaderIsPending(pReader) ){
    u8 bEof = 0;
    pReader->iDocid = 0;
    pReader->nOffsetList = 0;
    sqlite3Fts3DoclistPrev(0,
        pReader->aDoclist, pReader->nDoclist, &pReader->pOffsetList, 
        &pReader->iDocid, &pReader->nOffsetList, &bEof
    );
  }else{
    rc = fts3SegReaderRequire(pReader, pReader->aDoclist, FTS3_VARINT_MAX);
    if( rc==SQLITE_OK ){
      int n = sqlite3Fts3GetVarint(pReader->aDoclist, &pReader->iDocid);
      pReader->pOffsetList = &pReader->aDoclist[n];
    }
  }
  return rc;
}

/*
** Advance the SegReader to point to the next docid in the doclist
** associated with the current term.
** 
** If arguments ppOffsetList and pnOffsetList are not NULL, then 
** *ppOffsetList is set to point to the first column-offset list
** in the doclist entry (i.e. immediately past the docid varint).
** *pnOffsetList is set to the length of the set of column-offset
** lists, not including the nul-terminator byte. For example:
*/
static int fts3SegReaderNextDocid(
  Fts3Table *pTab,
  Fts3SegReader *pReader,         /* Reader to advance to next docid */
  char **ppOffsetList,            /* OUT: Pointer to current position-list */
  int *pnOffsetList               /* OUT: Length of *ppOffsetList in bytes */
){
  int rc = SQLITE_OK;
  char *p = pReader->pOffsetList;
  char c = 0;

  assert( p );

  if( pTab->bDescIdx && fts3SegReaderIsPending(pReader) ){
    /* A pending-terms seg-reader for an FTS4 table that uses order=desc.
    ** Pending-terms doclists are always built up in ascending order, so
    ** we have to iterate through them backwards here. */
    u8 bEof = 0;
    if( ppOffsetList ){
      *ppOffsetList = pReader->pOffsetList;
      *pnOffsetList = pReader->nOffsetList - 1;
    }
    sqlite3Fts3DoclistPrev(0,
        pReader->aDoclist, pReader->nDoclist, &p, &pReader->iDocid,
        &pReader->nOffsetList, &bEof
    );
    if( bEof ){
      pReader->pOffsetList = 0;
    }else{
      pReader->pOffsetList = p;
    }
  }else{
    char *pEnd = &pReader->aDoclist[pReader->nDoclist];

    /* Pointer p currently points at the first byte of an offset list. The
    ** following block advances it to point one byte past the end of
    ** the same offset list. */
    while( 1 ){
  
      /* The following line of code (and the "p++" below the while() loop) is
      ** normally all that is required to move pointer p to the desired 
      ** position. The exception is if this node is being loaded from disk
      ** incrementally and pointer "p" now points to the first byte passed
      ** the populated part of pReader->aNode[].
      */
      while( *p | c ) c = *p++ & 0x80;
      assert( *p==0 );
  
      if( pReader->pBlob==0 || p<&pReader->aNode[pReader->nPopulate] ) break;
      rc = fts3SegReaderIncrRead(pReader);
      if( rc!=SQLITE_OK ) return rc;
    }
    p++;
  
    /* If required, populate the output variables with a pointer to and the
    ** size of the previous offset-list.
    */
    if( ppOffsetList ){
      *ppOffsetList = pReader->pOffsetList;
      *pnOffsetList = (int)(p - pReader->pOffsetList - 1);
    }

    while( p<pEnd && *p==0 ) p++;
  
    /* If there are no more entries in the doclist, set pOffsetList to
    ** NULL. Otherwise, set Fts3SegReader.iDocid to the next docid and
    ** Fts3SegReader.pOffsetList to point to the next offset list before
    ** returning.
    */
    if( p>=pEnd ){
      pReader->pOffsetList = 0;
    }else{
      rc = fts3SegReaderRequire(pReader, p, FTS3_VARINT_MAX);
      if( rc==SQLITE_OK ){
        sqlite3_int64 iDelta;
        pReader->pOffsetList = p + sqlite3Fts3GetVarint(p, &iDelta);
        if( pTab->bDescIdx ){
          pReader->iDocid -= iDelta;
        }else{
          pReader->iDocid += iDelta;
        }
      }
    }
  }

  return SQLITE_OK;
}


SQLITE_PRIVATE int sqlite3Fts3MsrOvfl(
  Fts3Cursor *pCsr, 
  Fts3MultiSegReader *pMsr,
  int *pnOvfl
){
  Fts3Table *p = (Fts3Table*)pCsr->base.pVtab;
  int nOvfl = 0;
  int ii;
  int rc = SQLITE_OK;
  int pgsz = p->nPgsz;

  assert( p->bHasStat );
  assert( pgsz>0 );

  for(ii=0; rc==SQLITE_OK && ii<pMsr->nSegment; ii++){
    Fts3SegReader *pReader = pMsr->apSegment[ii];
    if( !fts3SegReaderIsPending(pReader) 
     && !fts3SegReaderIsRootOnly(pReader) 
    ){
      sqlite3_int64 jj;
      for(jj=pReader->iStartBlock; jj<=pReader->iLeafEndBlock; jj++){
        int nBlob;
        rc = sqlite3Fts3ReadBlock(p, jj, 0, &nBlob, 0);
        if( rc!=SQLITE_OK ) break;
        if( (nBlob+35)>pgsz ){
          nOvfl += (nBlob + 34)/pgsz;
        }
      }
    }
  }
  *pnOvfl = nOvfl;
  return rc;
}

/*
** Free all allocations associated with the iterator passed as the 
** second argument.
*/
SQLITE_PRIVATE void sqlite3Fts3SegReaderFree(Fts3SegReader *pReader){
  if( pReader && !fts3SegReaderIsPending(pReader) ){
    sqlite3_free(pReader->zTerm);
    if( !fts3SegReaderIsRootOnly(pReader) ){
      sqlite3_free(pReader->aNode);
      sqlite3_blob_close(pReader->pBlob);
    }
  }
  sqlite3_free(pReader);
}

/*
** Allocate a new SegReader object.
*/
SQLITE_PRIVATE int sqlite3Fts3SegReaderNew(
  int iAge,                       /* Segment "age". */
  sqlite3_int64 iStartLeaf,       /* First leaf to traverse */
  sqlite3_int64 iEndLeaf,         /* Final leaf to traverse */
  sqlite3_int64 iEndBlock,        /* Final block of segment */
  const char *zRoot,              /* Buffer containing root node */
  int nRoot,                      /* Size of buffer containing root node */
  Fts3SegReader **ppReader        /* OUT: Allocated Fts3SegReader */
){
  int rc = SQLITE_OK;             /* Return code */
  Fts3SegReader *pReader;         /* Newly allocated SegReader object */
  int nExtra = 0;                 /* Bytes to allocate segment root node */

  assert( iStartLeaf<=iEndLeaf );
  if( iStartLeaf==0 ){
    nExtra = nRoot + FTS3_NODE_PADDING;
  }

  pReader = (Fts3SegReader *)sqlite3_malloc(sizeof(Fts3SegReader) + nExtra);
  if( !pReader ){
    return SQLITE_NOMEM;
  }
  memset(pReader, 0, sizeof(Fts3SegReader));
  pReader->iIdx = iAge;
  pReader->iStartBlock = iStartLeaf;
  pReader->iLeafEndBlock = iEndLeaf;
  pReader->iEndBlock = iEndBlock;

  if( nExtra ){
    /* The entire segment is stored in the root node. */
    pReader->aNode = (char *)&pReader[1];
    pReader->nNode = nRoot;
    memcpy(pReader->aNode, zRoot, nRoot);
    memset(&pReader->aNode[nRoot], 0, FTS3_NODE_PADDING);
  }else{
    pReader->iCurrentBlock = iStartLeaf-1;
  }

  if( rc==SQLITE_OK ){
    *ppReader = pReader;
  }else{
    sqlite3Fts3SegReaderFree(pReader);
  }
  return rc;
}

/*
** This is a comparison function used as a qsort() callback when sorting
** an array of pending terms by term. This occurs as part of flushing
** the contents of the pending-terms hash table to the database.
*/
static int fts3CompareElemByTerm(const void *lhs, const void *rhs){
  char *z1 = fts3HashKey(*(Fts3HashElem **)lhs);
  char *z2 = fts3HashKey(*(Fts3HashElem **)rhs);
  int n1 = fts3HashKeysize(*(Fts3HashElem **)lhs);
  int n2 = fts3HashKeysize(*(Fts3HashElem **)rhs);

  int n = (n1<n2 ? n1 : n2);
  int c = memcmp(z1, z2, n);
  if( c==0 ){
    c = n1 - n2;
  }
  return c;
}

/*
** This function is used to allocate an Fts3SegReader that iterates through
** a subset of the terms stored in the Fts3Table.pendingTerms array.
**
** If the isPrefixIter parameter is zero, then the returned SegReader iterates
** through each term in the pending-terms table. Or, if isPrefixIter is
** non-zero, it iterates through each term and its prefixes. For example, if
** the pending terms hash table contains the terms "sqlite", "mysql" and
** "firebird", then the iterator visits the following 'terms' (in the order
** shown):
**
**   f fi fir fire fireb firebi firebir firebird
**   m my mys mysq mysql
**   s sq sql sqli sqlit sqlite
**
** Whereas if isPrefixIter is zero, the terms visited are:
**
**   firebird mysql sqlite
*/
SQLITE_PRIVATE int sqlite3Fts3SegReaderPending(
  Fts3Table *p,                   /* Virtual table handle */
  int iIndex,                     /* Index for p->aIndex */
  const char *zTerm,              /* Term to search for */
  int nTerm,                      /* Size of buffer zTerm */
  int bPrefix,                    /* True for a prefix iterator */
  Fts3SegReader **ppReader        /* OUT: SegReader for pending-terms */
){
  Fts3SegReader *pReader = 0;     /* Fts3SegReader object to return */
  Fts3HashElem **aElem = 0;       /* Array of term hash entries to scan */
  int nElem = 0;                  /* Size of array at aElem */
  int rc = SQLITE_OK;             /* Return Code */
  Fts3Hash *pHash;

  pHash = &p->aIndex[iIndex].hPending;
  if( bPrefix ){
    int nAlloc = 0;               /* Size of allocated array at aElem */
    Fts3HashElem *pE = 0;         /* Iterator variable */

    for(pE=fts3HashFirst(pHash); pE; pE=fts3HashNext(pE)){
      char *zKey = (char *)fts3HashKey(pE);
      int nKey = fts3HashKeysize(pE);
      if( nTerm==0 || (nKey>=nTerm && 0==memcmp(zKey, zTerm, nTerm)) ){
        if( nElem==nAlloc ){
          Fts3HashElem **aElem2;
          nAlloc += 16;
          aElem2 = (Fts3HashElem **)sqlite3_realloc(
              aElem, nAlloc*sizeof(Fts3HashElem *)
          );
          if( !aElem2 ){
            rc = SQLITE_NOMEM;
            nElem = 0;
            break;
          }
          aElem = aElem2;
        }

        aElem[nElem++] = pE;
      }
    }

    /* If more than one term matches the prefix, sort the Fts3HashElem
    ** objects in term order using qsort(). This uses the same comparison
    ** callback as is used when flushing terms to disk.
    */
    if( nElem>1 ){
      qsort(aElem, nElem, sizeof(Fts3HashElem *), fts3CompareElemByTerm);
    }

  }else{
    /* The query is a simple term lookup that matches at most one term in
    ** the index. All that is required is a straight hash-lookup. */
    Fts3HashElem *pE = fts3HashFindElem(pHash, zTerm, nTerm);
    if( pE ){
      aElem = &pE;
      nElem = 1;
    }
  }

  if( nElem>0 ){
    int nByte = sizeof(Fts3SegReader) + (nElem+1)*sizeof(Fts3HashElem *);
    pReader = (Fts3SegReader *)sqlite3_malloc(nByte);
    if( !pReader ){
      rc = SQLITE_NOMEM;
    }else{
      memset(pReader, 0, nByte);
      pReader->iIdx = 0x7FFFFFFF;
      pReader->ppNextElem = (Fts3HashElem **)&pReader[1];
      memcpy(pReader->ppNextElem, aElem, nElem*sizeof(Fts3HashElem *));
    }
  }

  if( bPrefix ){
    sqlite3_free(aElem);
  }
  *ppReader = pReader;
  return rc;
}

/*
** Compare the entries pointed to by two Fts3SegReader structures. 
** Comparison is as follows:
**
**   1) EOF is greater than not EOF.
**
**   2) The current terms (if any) are compared using memcmp(). If one
**      term is a prefix of another, the longer term is considered the
**      larger.
**
**   3) By segment age. An older segment is considered larger.
*/
static int fts3SegReaderCmp(Fts3SegReader *pLhs, Fts3SegReader *pRhs){
  int rc;
  if( pLhs->aNode && pRhs->aNode ){
    int rc2 = pLhs->nTerm - pRhs->nTerm;
    if( rc2<0 ){
      rc = memcmp(pLhs->zTerm, pRhs->zTerm, pLhs->nTerm);
    }else{
      rc = memcmp(pLhs->zTerm, pRhs->zTerm, pRhs->nTerm);
    }
    if( rc==0 ){
      rc = rc2;
    }
  }else{
    rc = (pLhs->aNode==0) - (pRhs->aNode==0);
  }
  if( rc==0 ){
    rc = pRhs->iIdx - pLhs->iIdx;
  }
  assert( rc!=0 );
  return rc;
}

/*
** A different comparison function for SegReader structures. In this
** version, it is assumed that each SegReader points to an entry in
** a doclist for identical terms. Comparison is made as follows:
**
**   1) EOF (end of doclist in this case) is greater than not EOF.
**
**   2) By current docid.
**
**   3) By segment age. An older segment is considered larger.
*/
static int fts3SegReaderDoclistCmp(Fts3SegReader *pLhs, Fts3SegReader *pRhs){
  int rc = (pLhs->pOffsetList==0)-(pRhs->pOffsetList==0);
  if( rc==0 ){
    if( pLhs->iDocid==pRhs->iDocid ){
      rc = pRhs->iIdx - pLhs->iIdx;
    }else{
      rc = (pLhs->iDocid > pRhs->iDocid) ? 1 : -1;
    }
  }
  assert( pLhs->aNode && pRhs->aNode );
  return rc;
}
static int fts3SegReaderDoclistCmpRev(Fts3SegReader *pLhs, Fts3SegReader *pRhs){
  int rc = (pLhs->pOffsetList==0)-(pRhs->pOffsetList==0);
  if( rc==0 ){
    if( pLhs->iDocid==pRhs->iDocid ){
      rc = pRhs->iIdx - pLhs->iIdx;
    }else{
      rc = (pLhs->iDocid < pRhs->iDocid) ? 1 : -1;
    }
  }
  assert( pLhs->aNode && pRhs->aNode );
  return rc;
}

/*
** Compare the term that the Fts3SegReader object passed as the first argument
** points to with the term specified by arguments zTerm and nTerm. 
**
** If the pSeg iterator is already at EOF, return 0. Otherwise, return
** -ve if the pSeg term is less than zTerm/nTerm, 0 if the two terms are
** equal, or +ve if the pSeg term is greater than zTerm/nTerm.
*/
static int fts3SegReaderTermCmp(
  Fts3SegReader *pSeg,            /* Segment reader object */
  const char *zTerm,              /* Term to compare to */
  int nTerm                       /* Size of term zTerm in bytes */
){
  int res = 0;
  if( pSeg->aNode ){
    if( pSeg->nTerm>nTerm ){
      res = memcmp(pSeg->zTerm, zTerm, nTerm);
    }else{
      res = memcmp(pSeg->zTerm, zTerm, pSeg->nTerm);
    }
    if( res==0 ){
      res = pSeg->nTerm-nTerm;
    }
  }
  return res;
}

/*
** Argument apSegment is an array of nSegment elements. It is known that
** the final (nSegment-nSuspect) members are already in sorted order
** (according to the comparison function provided). This function shuffles
** the array around until all entries are in sorted order.
*/
static void fts3SegReaderSort(
  Fts3SegReader **apSegment,                     /* Array to sort entries of */
  int nSegment,                                  /* Size of apSegment array */
  int nSuspect,                                  /* Unsorted entry count */
  int (*xCmp)(Fts3SegReader *, Fts3SegReader *)  /* Comparison function */
){
  int i;                          /* Iterator variable */

  assert( nSuspect<=nSegment );

  if( nSuspect==nSegment ) nSuspect--;
  for(i=nSuspect-1; i>=0; i--){
    int j;
    for(j=i; j<(nSegment-1); j++){
      Fts3SegReader *pTmp;
      if( xCmp(apSegment[j], apSegment[j+1])<0 ) break;
      pTmp = apSegment[j+1];
      apSegment[j+1] = apSegment[j];
      apSegment[j] = pTmp;
    }
  }

#ifndef NDEBUG
  /* Check that the list really is sorted now. */
  for(i=0; i<(nSuspect-1); i++){
    assert( xCmp(apSegment[i], apSegment[i+1])<0 );
  }
#endif
}

/* 
** Insert a record into the %_segments table.
*/
static int fts3WriteSegment(
  Fts3Table *p,                   /* Virtual table handle */
  sqlite3_int64 iBlock,           /* Block id for new block */
  char *z,                        /* Pointer to buffer containing block data */
  int n                           /* Size of buffer z in bytes */
){
  sqlite3_stmt *pStmt;
  int rc = fts3SqlStmt(p, SQL_INSERT_SEGMENTS, &pStmt, 0);
  if( rc==SQLITE_OK ){
    sqlite3_bind_int64(pStmt, 1, iBlock);
    sqlite3_bind_blob(pStmt, 2, z, n, SQLITE_STATIC);
    sqlite3_step(pStmt);
    rc = sqlite3_reset(pStmt);
  }
  return rc;
}

/* 
** Insert a record into the %_segdir table.
*/
static int fts3WriteSegdir(
  Fts3Table *p,                   /* Virtual table handle */
  int iLevel,                     /* Value for "level" field */
  int iIdx,                       /* Value for "idx" field */
  sqlite3_int64 iStartBlock,      /* Value for "start_block" field */
  sqlite3_int64 iLeafEndBlock,    /* Value for "leaves_end_block" field */
  sqlite3_int64 iEndBlock,        /* Value for "end_block" field */
  char *zRoot,                    /* Blob value for "root" field */
  int nRoot                       /* Number of bytes in buffer zRoot */
){
  sqlite3_stmt *pStmt;
  int rc = fts3SqlStmt(p, SQL_INSERT_SEGDIR, &pStmt, 0);
  if( rc==SQLITE_OK ){
    sqlite3_bind_int(pStmt, 1, iLevel);
    sqlite3_bind_int(pStmt, 2, iIdx);
    sqlite3_bind_int64(pStmt, 3, iStartBlock);
    sqlite3_bind_int64(pStmt, 4, iLeafEndBlock);
    sqlite3_bind_int64(pStmt, 5, iEndBlock);
    sqlite3_bind_blob(pStmt, 6, zRoot, nRoot, SQLITE_STATIC);
    sqlite3_step(pStmt);
    rc = sqlite3_reset(pStmt);
  }
  return rc;
}

/*
** Return the size of the common prefix (if any) shared by zPrev and
** zNext, in bytes. For example, 
**
**   fts3PrefixCompress("abc", 3, "abcdef", 6)   // returns 3
**   fts3PrefixCompress("abX", 3, "abcdef", 6)   // returns 2
**   fts3PrefixCompress("abX", 3, "Xbcdef", 6)   // returns 0
*/
static int fts3PrefixCompress(
  const char *zPrev,              /* Buffer containing previous term */
  int nPrev,                      /* Size of buffer zPrev in bytes */
  const char *zNext,              /* Buffer containing next term */
  int nNext                       /* Size of buffer zNext in bytes */
){
  int n;
  UNUSED_PARAMETER(nNext);
  for(n=0; n<nPrev && zPrev[n]==zNext[n]; n++);
  return n;
}

/*
** Add term zTerm to the SegmentNode. It is guaranteed that zTerm is larger
** (according to memcmp) than the previous term.
*/
static int fts3NodeAddTerm(
  Fts3Table *p,                   /* Virtual table handle */
  SegmentNode **ppTree,           /* IN/OUT: SegmentNode handle */ 
  int isCopyTerm,                 /* True if zTerm/nTerm is transient */
  const char *zTerm,              /* Pointer to buffer containing term */
  int nTerm                       /* Size of term in bytes */
){
  SegmentNode *pTree = *ppTree;
  int rc;
  SegmentNode *pNew;

  /* First try to append the term to the current node. Return early if 
  ** this is possible.
  */
  if( pTree ){
    int nData = pTree->nData;     /* Current size of node in bytes */
    int nReq = nData;             /* Required space after adding zTerm */
    int nPrefix;                  /* Number of bytes of prefix compression */
    int nSuffix;                  /* Suffix length */

    nPrefix = fts3PrefixCompress(pTree->zTerm, pTree->nTerm, zTerm, nTerm);
    nSuffix = nTerm-nPrefix;

    nReq += sqlite3Fts3VarintLen(nPrefix)+sqlite3Fts3VarintLen(nSuffix)+nSuffix;
    if( nReq<=p->nNodeSize || !pTree->zTerm ){

      if( nReq>p->nNodeSize ){
        /* An unusual case: this is the first term to be added to the node
        ** and the static node buffer (p->nNodeSize bytes) is not large
        ** enough. Use a separately malloced buffer instead This wastes
        ** p->nNodeSize bytes, but since this scenario only comes about when
        ** the database contain two terms that share a prefix of almost 2KB, 
        ** this is not expected to be a serious problem. 
        */
        assert( pTree->aData==(char *)&pTree[1] );
        pTree->aData = (char *)sqlite3_malloc(nReq);
        if( !pTree->aData ){
          return SQLITE_NOMEM;
        }
      }

      if( pTree->zTerm ){
        /* There is no prefix-length field for first term in a node */
        nData += sqlite3Fts3PutVarint(&pTree->aData[nData], nPrefix);
      }

      nData += sqlite3Fts3PutVarint(&pTree->aData[nData], nSuffix);
      memcpy(&pTree->aData[nData], &zTerm[nPrefix], nSuffix);
      pTree->nData = nData + nSuffix;
      pTree->nEntry++;

      if( isCopyTerm ){
        if( pTree->nMalloc<nTerm ){
          char *zNew = sqlite3_realloc(pTree->zMalloc, nTerm*2);
          if( !zNew ){
            return SQLITE_NOMEM;
          }
          pTree->nMalloc = nTerm*2;
          pTree->zMalloc = zNew;
        }
        pTree->zTerm = pTree->zMalloc;
        memcpy(pTree->zTerm, zTerm, nTerm);
        pTree->nTerm = nTerm;
      }else{
        pTree->zTerm = (char *)zTerm;
        pTree->nTerm = nTerm;
      }
      return SQLITE_OK;
    }
  }

  /* If control flows to here, it was not possible to append zTerm to the
  ** current node. Create a new node (a right-sibling of the current node).
  ** If this is the first node in the tree, the term is added to it.
  **
  ** Otherwise, the term is not added to the new node, it is left empty for
  ** now. Instead, the term is inserted into the parent of pTree. If pTree 
  ** has no parent, one is created here.
  */
  pNew = (SegmentNode *)sqlite3_malloc(sizeof(SegmentNode) + p->nNodeSize);
  if( !pNew ){
    return SQLITE_NOMEM;
  }
  memset(pNew, 0, sizeof(SegmentNode));
  pNew->nData = 1 + FTS3_VARINT_MAX;
  pNew->aData = (char *)&pNew[1];

  if( pTree ){
    SegmentNode *pParent = pTree->pParent;
    rc = fts3NodeAddTerm(p, &pParent, isCopyTerm, zTerm, nTerm);
    if( pTree->pParent==0 ){
      pTree->pParent = pParent;
    }
    pTree->pRight = pNew;
    pNew->pLeftmost = pTree->pLeftmost;
    pNew->pParent = pParent;
    pNew->zMalloc = pTree->zMalloc;
    pNew->nMalloc = pTree->nMalloc;
    pTree->zMalloc = 0;
  }else{
    pNew->pLeftmost = pNew;
    rc = fts3NodeAddTerm(p, &pNew, isCopyTerm, zTerm, nTerm); 
  }

  *ppTree = pNew;
  return rc;
}

/*
** Helper function for fts3NodeWrite().
*/
static int fts3TreeFinishNode(
  SegmentNode *pTree, 
  int iHeight, 
  sqlite3_int64 iLeftChild
){
  int nStart;
  assert( iHeight>=1 && iHeight<128 );
  nStart = FTS3_VARINT_MAX - sqlite3Fts3VarintLen(iLeftChild);
  pTree->aData[nStart] = (char)iHeight;
  sqlite3Fts3PutVarint(&pTree->aData[nStart+1], iLeftChild);
  return nStart;
}

/*
** Write the buffer for the segment node pTree and all of its peers to the
** database. Then call this function recursively to write the parent of 
** pTree and its peers to the database. 
**
** Except, if pTree is a root node, do not write it to the database. Instead,
** set output variables *paRoot and *pnRoot to contain the root node.
**
** If successful, SQLITE_OK is returned and output variable *piLast is
** set to the largest blockid written to the database (or zero if no
** blocks were written to the db). Otherwise, an SQLite error code is 
** returned.
*/
static int fts3NodeWrite(
  Fts3Table *p,                   /* Virtual table handle */
  SegmentNode *pTree,             /* SegmentNode handle */
  int iHeight,                    /* Height of this node in tree */
  sqlite3_int64 iLeaf,            /* Block id of first leaf node */
  sqlite3_int64 iFree,            /* Block id of next free slot in %_segments */
  sqlite3_int64 *piLast,          /* OUT: Block id of last entry written */
  char **paRoot,                  /* OUT: Data for root node */
  int *pnRoot                     /* OUT: Size of root node in bytes */
){
  int rc = SQLITE_OK;

  if( !pTree->pParent ){
    /* Root node of the tree. */
    int nStart = fts3TreeFinishNode(pTree, iHeight, iLeaf);
    *piLast = iFree-1;
    *pnRoot = pTree->nData - nStart;
    *paRoot = &pTree->aData[nStart];
  }else{
    SegmentNode *pIter;
    sqlite3_int64 iNextFree = iFree;
    sqlite3_int64 iNextLeaf = iLeaf;
    for(pIter=pTree->pLeftmost; pIter && rc==SQLITE_OK; pIter=pIter->pRight){
      int nStart = fts3TreeFinishNode(pIter, iHeight, iNextLeaf);
      int nWrite = pIter->nData - nStart;
  
      rc = fts3WriteSegment(p, iNextFree, &pIter->aData[nStart], nWrite);
      iNextFree++;
      iNextLeaf += (pIter->nEntry+1);
    }
    if( rc==SQLITE_OK ){
      assert( iNextLeaf==iFree );
      rc = fts3NodeWrite(
          p, pTree->pParent, iHeight+1, iFree, iNextFree, piLast, paRoot, pnRoot
      );
    }
  }

  return rc;
}

/*
** Free all memory allocations associated with the tree pTree.
*/
static void fts3NodeFree(SegmentNode *pTree){
  if( pTree ){
    SegmentNode *p = pTree->pLeftmost;
    fts3NodeFree(p->pParent);
    while( p ){
      SegmentNode *pRight = p->pRight;
      if( p->aData!=(char *)&p[1] ){
        sqlite3_free(p->aData);
      }
      assert( pRight==0 || p->zMalloc==0 );
      sqlite3_free(p->zMalloc);
      sqlite3_free(p);
      p = pRight;
    }
  }
}

/*
** Add a term to the segment being constructed by the SegmentWriter object
** *ppWriter. When adding the first term to a segment, *ppWriter should
** be passed NULL. This function will allocate a new SegmentWriter object
** and return it via the input/output variable *ppWriter in this case.
**
** If successful, SQLITE_OK is returned. Otherwise, an SQLite error code.
*/
static int fts3SegWriterAdd(
  Fts3Table *p,                   /* Virtual table handle */
  SegmentWriter **ppWriter,       /* IN/OUT: SegmentWriter handle */ 
  int isCopyTerm,                 /* True if buffer zTerm must be copied */
  const char *zTerm,              /* Pointer to buffer containing term */
  int nTerm,                      /* Size of term in bytes */
  const char *aDoclist,           /* Pointer to buffer containing doclist */
  int nDoclist                    /* Size of doclist in bytes */
){
  int nPrefix;                    /* Size of term prefix in bytes */
  int nSuffix;                    /* Size of term suffix in bytes */
  int nReq;                       /* Number of bytes required on leaf page */
  int nData;
  SegmentWriter *pWriter = *ppWriter;

  if( !pWriter ){
    int rc;
    sqlite3_stmt *pStmt;

    /* Allocate the SegmentWriter structure */
    pWriter = (SegmentWriter *)sqlite3_malloc(sizeof(SegmentWriter));
    if( !pWriter ) return SQLITE_NOMEM;
    memset(pWriter, 0, sizeof(SegmentWriter));
    *ppWriter = pWriter;

    /* Allocate a buffer in which to accumulate data */
    pWriter->aData = (char *)sqlite3_malloc(p->nNodeSize);
    if( !pWriter->aData ) return SQLITE_NOMEM;
    pWriter->nSize = p->nNodeSize;

    /* Find the next free blockid in the %_segments table */
    rc = fts3SqlStmt(p, SQL_NEXT_SEGMENTS_ID, &pStmt, 0);
    if( rc!=SQLITE_OK ) return rc;
    if( SQLITE_ROW==sqlite3_step(pStmt) ){
      pWriter->iFree = sqlite3_column_int64(pStmt, 0);
      pWriter->iFirst = pWriter->iFree;
    }
    rc = sqlite3_reset(pStmt);
    if( rc!=SQLITE_OK ) return rc;
  }
  nData = pWriter->nData;

  nPrefix = fts3PrefixCompress(pWriter->zTerm, pWriter->nTerm, zTerm, nTerm);
  nSuffix = nTerm-nPrefix;

  /* Figure out how many bytes are required by this new entry */
  nReq = sqlite3Fts3VarintLen(nPrefix) +    /* varint containing prefix size */
    sqlite3Fts3VarintLen(nSuffix) +         /* varint containing suffix size */
    nSuffix +                               /* Term suffix */
    sqlite3Fts3VarintLen(nDoclist) +        /* Size of doclist */
    nDoclist;                               /* Doclist data */

  if( nData>0 && nData+nReq>p->nNodeSize ){
    int rc;

    /* The current leaf node is full. Write it out to the database. */
    rc = fts3WriteSegment(p, pWriter->iFree++, pWriter->aData, nData);
    if( rc!=SQLITE_OK ) return rc;

    /* Add the current term to the interior node tree. The term added to
    ** the interior tree must:
    **
    **   a) be greater than the largest term on the leaf node just written
    **      to the database (still available in pWriter->zTerm), and
    **
    **   b) be less than or equal to the term about to be added to the new
    **      leaf node (zTerm/nTerm).
    **
    ** In other words, it must be the prefix of zTerm 1 byte longer than
    ** the common prefix (if any) of zTerm and pWriter->zTerm.
    */
    assert( nPrefix<nTerm );
    rc = fts3NodeAddTerm(p, &pWriter->pTree, isCopyTerm, zTerm, nPrefix+1);
    if( rc!=SQLITE_OK ) return rc;

    nData = 0;
    pWriter->nTerm = 0;

    nPrefix = 0;
    nSuffix = nTerm;
    nReq = 1 +                              /* varint containing prefix size */
      sqlite3Fts3VarintLen(nTerm) +         /* varint containing suffix size */
      nTerm +                               /* Term suffix */
      sqlite3Fts3VarintLen(nDoclist) +      /* Size of doclist */
      nDoclist;                             /* Doclist data */
  }

  /* If the buffer currently allocated is too small for this entry, realloc
  ** the buffer to make it large enough.
  */
  if( nReq>pWriter->nSize ){
    char *aNew = sqlite3_realloc(pWriter->aData, nReq);
    if( !aNew ) return SQLITE_NOMEM;
    pWriter->aData = aNew;
    pWriter->nSize = nReq;
  }
  assert( nData+nReq<=pWriter->nSize );

  /* Append the prefix-compressed term and doclist to the buffer. */
  nData += sqlite3Fts3PutVarint(&pWriter->aData[nData], nPrefix);
  nData += sqlite3Fts3PutVarint(&pWriter->aData[nData], nSuffix);
  memcpy(&pWriter->aData[nData], &zTerm[nPrefix], nSuffix);
  nData += nSuffix;
  nData += sqlite3Fts3PutVarint(&pWriter->aData[nData], nDoclist);
  memcpy(&pWriter->aData[nData], aDoclist, nDoclist);
  pWriter->nData = nData + nDoclist;

  /* Save the current term so that it can be used to prefix-compress the next.
  ** If the isCopyTerm parameter is true, then the buffer pointed to by
  ** zTerm is transient, so take a copy of the term data. Otherwise, just
  ** store a copy of the pointer.
  */
  if( isCopyTerm ){
    if( nTerm>pWriter->nMalloc ){
      char *zNew = sqlite3_realloc(pWriter->zMalloc, nTerm*2);
      if( !zNew ){
        return SQLITE_NOMEM;
      }
      pWriter->nMalloc = nTerm*2;
      pWriter->zMalloc = zNew;
      pWriter->zTerm = zNew;
    }
    assert( pWriter->zTerm==pWriter->zMalloc );
    memcpy(pWriter->zTerm, zTerm, nTerm);
  }else{
    pWriter->zTerm = (char *)zTerm;
  }
  pWriter->nTerm = nTerm;

  return SQLITE_OK;
}

/*
** Flush all data associated with the SegmentWriter object pWriter to the
** database. This function must be called after all terms have been added
** to the segment using fts3SegWriterAdd(). If successful, SQLITE_OK is
** returned. Otherwise, an SQLite error code.
*/
static int fts3SegWriterFlush(
  Fts3Table *p,                   /* Virtual table handle */
  SegmentWriter *pWriter,         /* SegmentWriter to flush to the db */
  int iLevel,                     /* Value for 'level' column of %_segdir */
  int iIdx                        /* Value for 'idx' column of %_segdir */
){
  int rc;                         /* Return code */
  if( pWriter->pTree ){
    sqlite3_int64 iLast = 0;      /* Largest block id written to database */
    sqlite3_int64 iLastLeaf;      /* Largest leaf block id written to db */
    char *zRoot = NULL;           /* Pointer to buffer containing root node */
    int nRoot = 0;                /* Size of buffer zRoot */

    iLastLeaf = pWriter->iFree;
    rc = fts3WriteSegment(p, pWriter->iFree++, pWriter->aData, pWriter->nData);
    if( rc==SQLITE_OK ){
      rc = fts3NodeWrite(p, pWriter->pTree, 1,
          pWriter->iFirst, pWriter->iFree, &iLast, &zRoot, &nRoot);
    }
    if( rc==SQLITE_OK ){
      rc = fts3WriteSegdir(
          p, iLevel, iIdx, pWriter->iFirst, iLastLeaf, iLast, zRoot, nRoot);
    }
  }else{
    /* The entire tree fits on the root node. Write it to the segdir table. */
    rc = fts3WriteSegdir(
        p, iLevel, iIdx, 0, 0, 0, pWriter->aData, pWriter->nData);
  }
  return rc;
}

/*
** Release all memory held by the SegmentWriter object passed as the 
** first argument.
*/
static void fts3SegWriterFree(SegmentWriter *pWriter){
  if( pWriter ){
    sqlite3_free(pWriter->aData);
    sqlite3_free(pWriter->zMalloc);
    fts3NodeFree(pWriter->pTree);
    sqlite3_free(pWriter);
  }
}

/*
** The first value in the apVal[] array is assumed to contain an integer.
** This function tests if there exist any documents with docid values that
** are different from that integer. i.e. if deleting the document with docid
** pRowid would mean the FTS3 table were empty.
**
** If successful, *pisEmpty is set to true if the table is empty except for
** document pRowid, or false otherwise, and SQLITE_OK is returned. If an
** error occurs, an SQLite error code is returned.
*/
static int fts3IsEmpty(Fts3Table *p, sqlite3_value *pRowid, int *pisEmpty){
  sqlite3_stmt *pStmt;
  int rc;
  if( p->zContentTbl ){
    /* If using the content=xxx option, assume the table is never empty */
    *pisEmpty = 0;
    rc = SQLITE_OK;
  }else{
    rc = fts3SqlStmt(p, SQL_IS_EMPTY, &pStmt, &pRowid);
    if( rc==SQLITE_OK ){
      if( SQLITE_ROW==sqlite3_step(pStmt) ){
        *pisEmpty = sqlite3_column_int(pStmt, 0);
      }
      rc = sqlite3_reset(pStmt);
    }
  }
  return rc;
}

/*
** Set *pnMax to the largest segment level in the database for the index
** iIndex.
**
** Segment levels are stored in the 'level' column of the %_segdir table.
**
** Return SQLITE_OK if successful, or an SQLite error code if not.
*/
static int fts3SegmentMaxLevel(Fts3Table *p, int iIndex, int *pnMax){
  sqlite3_stmt *pStmt;
  int rc;
  assert( iIndex>=0 && iIndex<p->nIndex );

  /* Set pStmt to the compiled version of:
  **
  **   SELECT max(level) FROM %Q.'%q_segdir' WHERE level BETWEEN ? AND ?
  **
  ** (1024 is actually the value of macro FTS3_SEGDIR_PREFIXLEVEL_STR).
  */
  rc = fts3SqlStmt(p, SQL_SELECT_SEGDIR_MAX_LEVEL, &pStmt, 0);
  if( rc!=SQLITE_OK ) return rc;
  sqlite3_bind_int(pStmt, 1, iIndex*FTS3_SEGDIR_MAXLEVEL);
  sqlite3_bind_int(pStmt, 2, (iIndex+1)*FTS3_SEGDIR_MAXLEVEL - 1);
  if( SQLITE_ROW==sqlite3_step(pStmt) ){
    *pnMax = sqlite3_column_int(pStmt, 0);
  }
  return sqlite3_reset(pStmt);
}

/*
** This function is used after merging multiple segments into a single large
** segment to delete the old, now redundant, segment b-trees. Specifically,
** it:
** 
**   1) Deletes all %_segments entries for the segments associated with 
**      each of the SegReader objects in the array passed as the third 
**      argument, and
**
**   2) deletes all %_segdir entries with level iLevel, or all %_segdir
**      entries regardless of level if (iLevel<0).
**
** SQLITE_OK is returned if successful, otherwise an SQLite error code.
*/
static int fts3DeleteSegdir(
  Fts3Table *p,                   /* Virtual table handle */
  int iIndex,                     /* Index for p->aIndex */
  int iLevel,                     /* Level of %_segdir entries to delete */
  Fts3SegReader **apSegment,      /* Array of SegReader objects */
  int nReader                     /* Size of array apSegment */
){
  int rc;                         /* Return Code */
  int i;                          /* Iterator variable */
  sqlite3_stmt *pDelete;          /* SQL statement to delete rows */

  rc = fts3SqlStmt(p, SQL_DELETE_SEGMENTS_RANGE, &pDelete, 0);
  for(i=0; rc==SQLITE_OK && i<nReader; i++){
    Fts3SegReader *pSegment = apSegment[i];
    if( pSegment->iStartBlock ){
      sqlite3_bind_int64(pDelete, 1, pSegment->iStartBlock);
      sqlite3_bind_int64(pDelete, 2, pSegment->iEndBlock);
      sqlite3_step(pDelete);
      rc = sqlite3_reset(pDelete);
    }
  }
  if( rc!=SQLITE_OK ){
    return rc;
  }

  assert( iLevel>=0 || iLevel==FTS3_SEGCURSOR_ALL );
  if( iLevel==FTS3_SEGCURSOR_ALL ){
    rc = fts3SqlStmt(p, SQL_DELETE_SEGDIR_RANGE, &pDelete, 0);
    if( rc==SQLITE_OK ){
      sqlite3_bind_int(pDelete, 1, iIndex*FTS3_SEGDIR_MAXLEVEL);
      sqlite3_bind_int(pDelete, 2, (iIndex+1) * FTS3_SEGDIR_MAXLEVEL - 1);
    }
  }else{
    rc = fts3SqlStmt(p, SQL_DELETE_SEGDIR_LEVEL, &pDelete, 0);
    if( rc==SQLITE_OK ){
      sqlite3_bind_int(pDelete, 1, iIndex*FTS3_SEGDIR_MAXLEVEL + iLevel);
    }
  }

  if( rc==SQLITE_OK ){
    sqlite3_step(pDelete);
    rc = sqlite3_reset(pDelete);
  }

  return rc;
}

/*
** When this function is called, buffer *ppList (size *pnList bytes) contains 
** a position list that may (or may not) feature multiple columns. This
** function adjusts the pointer *ppList and the length *pnList so that they
** identify the subset of the position list that corresponds to column iCol.
**
** If there are no entries in the input position list for column iCol, then
** *pnList is set to zero before returning.
*/
static void fts3ColumnFilter(
  int iCol,                       /* Column to filter on */
  char **ppList,                  /* IN/OUT: Pointer to position list */
  int *pnList                     /* IN/OUT: Size of buffer *ppList in bytes */
){
  char *pList = *ppList;
  int nList = *pnList;
  char *pEnd = &pList[nList];
  int iCurrent = 0;
  char *p = pList;

  assert( iCol>=0 );
  while( 1 ){
    char c = 0;
    while( p<pEnd && (c | *p)&0xFE ) c = *p++ & 0x80;
  
    if( iCol==iCurrent ){
      nList = (int)(p - pList);
      break;
    }

    nList -= (int)(p - pList);
    pList = p;
    if( nList==0 ){
      break;
    }
    p = &pList[1];
    p += sqlite3Fts3GetVarint32(p, &iCurrent);
  }

  *ppList = pList;
  *pnList = nList;
}

/*
** Cache data in the Fts3MultiSegReader.aBuffer[] buffer (overwriting any
** existing data). Grow the buffer if required.
**
** If successful, return SQLITE_OK. Otherwise, if an OOM error is encountered
** trying to resize the buffer, return SQLITE_NOMEM.
*/
static int fts3MsrBufferData(
  Fts3MultiSegReader *pMsr,       /* Multi-segment-reader handle */
  char *pList,
  int nList
){
  if( nList>pMsr->nBuffer ){
    char *pNew;
    pMsr->nBuffer = nList*2;
    pNew = (char *)sqlite3_realloc(pMsr->aBuffer, pMsr->nBuffer);
    if( !pNew ) return SQLITE_NOMEM;
    pMsr->aBuffer = pNew;
  }

  memcpy(pMsr->aBuffer, pList, nList);
  return SQLITE_OK;
}

SQLITE_PRIVATE int sqlite3Fts3MsrIncrNext(
  Fts3Table *p,                   /* Virtual table handle */
  Fts3MultiSegReader *pMsr,       /* Multi-segment-reader handle */
  sqlite3_int64 *piDocid,         /* OUT: Docid value */
  char **paPoslist,               /* OUT: Pointer to position list */
  int *pnPoslist                  /* OUT: Size of position list in bytes */
){
  int nMerge = pMsr->nAdvance;
  Fts3SegReader **apSegment = pMsr->apSegment;
  int (*xCmp)(Fts3SegReader *, Fts3SegReader *) = (
    p->bDescIdx ? fts3SegReaderDoclistCmpRev : fts3SegReaderDoclistCmp
  );

  if( nMerge==0 ){
    *paPoslist = 0;
    return SQLITE_OK;
  }

  while( 1 ){
    Fts3SegReader *pSeg;
    pSeg = pMsr->apSegment[0];

    if( pSeg->pOffsetList==0 ){
      *paPoslist = 0;
      break;
    }else{
      int rc;
      char *pList;
      int nList;
      int j;
      sqlite3_int64 iDocid = apSegment[0]->iDocid;

      rc = fts3SegReaderNextDocid(p, apSegment[0], &pList, &nList);
      j = 1;
      while( rc==SQLITE_OK 
        && j<nMerge
        && apSegment[j]->pOffsetList
        && apSegment[j]->iDocid==iDocid
      ){
        rc = fts3SegReaderNextDocid(p, apSegment[j], 0, 0);
        j++;
      }
      if( rc!=SQLITE_OK ) return rc;
      fts3SegReaderSort(pMsr->apSegment, nMerge, j, xCmp);

      if( pMsr->iColFilter>=0 ){
        fts3ColumnFilter(pMsr->iColFilter, &pList, &nList);
      }

      if( nList>0 ){
        if( fts3SegReaderIsPending(apSegment[0]) ){
          rc = fts3MsrBufferData(pMsr, pList, nList+1);
          if( rc!=SQLITE_OK ) return rc;
          *paPoslist = pMsr->aBuffer;
          assert( (pMsr->aBuffer[nList] & 0xFE)==0x00 );
        }else{
          *paPoslist = pList;
        }
        *piDocid = iDocid;
        *pnPoslist = nList;
        break;
      }
    }
  }

  return SQLITE_OK;
}

static int fts3SegReaderStart(
  Fts3Table *p,                   /* Virtual table handle */
  Fts3MultiSegReader *pCsr,       /* Cursor object */
  const char *zTerm,              /* Term searched for (or NULL) */
  int nTerm                       /* Length of zTerm in bytes */
){
  int i;
  int nSeg = pCsr->nSegment;

  /* If the Fts3SegFilter defines a specific term (or term prefix) to search 
  ** for, then advance each segment iterator until it points to a term of
  ** equal or greater value than the specified term. This prevents many
  ** unnecessary merge/sort operations for the case where single segment
  ** b-tree leaf nodes contain more than one term.
  */
  for(i=0; pCsr->bRestart==0 && i<pCsr->nSegment; i++){
    Fts3SegReader *pSeg = pCsr->apSegment[i];
    do {
      int rc = fts3SegReaderNext(p, pSeg, 0);
      if( rc!=SQLITE_OK ) return rc;
    }while( zTerm && fts3SegReaderTermCmp(pSeg, zTerm, nTerm)<0 );
  }
  fts3SegReaderSort(pCsr->apSegment, nSeg, nSeg, fts3SegReaderCmp);

  return SQLITE_OK;
}

SQLITE_PRIVATE int sqlite3Fts3SegReaderStart(
  Fts3Table *p,                   /* Virtual table handle */
  Fts3MultiSegReader *pCsr,       /* Cursor object */
  Fts3SegFilter *pFilter          /* Restrictions on range of iteration */
){
  pCsr->pFilter = pFilter;
  return fts3SegReaderStart(p, pCsr, pFilter->zTerm, pFilter->nTerm);
}

SQLITE_PRIVATE int sqlite3Fts3MsrIncrStart(
  Fts3Table *p,                   /* Virtual table handle */
  Fts3MultiSegReader *pCsr,       /* Cursor object */
  int iCol,                       /* Column to match on. */
  const char *zTerm,              /* Term to iterate through a doclist for */
  int nTerm                       /* Number of bytes in zTerm */
){
  int i;
  int rc;
  int nSegment = pCsr->nSegment;
  int (*xCmp)(Fts3SegReader *, Fts3SegReader *) = (
    p->bDescIdx ? fts3SegReaderDoclistCmpRev : fts3SegReaderDoclistCmp
  );

  assert( pCsr->pFilter==0 );
  assert( zTerm && nTerm>0 );

  /* Advance each segment iterator until it points to the term zTerm/nTerm. */
  rc = fts3SegReaderStart(p, pCsr, zTerm, nTerm);
  if( rc!=SQLITE_OK ) return rc;

  /* Determine how many of the segments actually point to zTerm/nTerm. */
  for(i=0; i<nSegment; i++){
    Fts3SegReader *pSeg = pCsr->apSegment[i];
    if( !pSeg->aNode || fts3SegReaderTermCmp(pSeg, zTerm, nTerm) ){
      break;
    }
  }
  pCsr->nAdvance = i;

  /* Advance each of the segments to point to the first docid. */
  for(i=0; i<pCsr->nAdvance; i++){
    rc = fts3SegReaderFirstDocid(p, pCsr->apSegment[i]);
    if( rc!=SQLITE_OK ) return rc;
  }
  fts3SegReaderSort(pCsr->apSegment, i, i, xCmp);

  assert( iCol<0 || iCol<p->nColumn );
  pCsr->iColFilter = iCol;

  return SQLITE_OK;
}

/*
** This function is called on a MultiSegReader that has been started using
** sqlite3Fts3MsrIncrStart(). One or more calls to MsrIncrNext() may also
** have been made. Calling this function puts the MultiSegReader in such
** a state that if the next two calls are:
**
**   sqlite3Fts3SegReaderStart()
**   sqlite3Fts3SegReaderStep()
**
** then the entire doclist for the term is available in 
** MultiSegReader.aDoclist/nDoclist.
*/
SQLITE_PRIVATE int sqlite3Fts3MsrIncrRestart(Fts3MultiSegReader *pCsr){
  int i;                          /* Used to iterate through segment-readers */

  assert( pCsr->zTerm==0 );
  assert( pCsr->nTerm==0 );
  assert( pCsr->aDoclist==0 );
  assert( pCsr->nDoclist==0 );

  pCsr->nAdvance = 0;
  pCsr->bRestart = 1;
  for(i=0; i<pCsr->nSegment; i++){
    pCsr->apSegment[i]->pOffsetList = 0;
    pCsr->apSegment[i]->nOffsetList = 0;
    pCsr->apSegment[i]->iDocid = 0;
  }

  return SQLITE_OK;
}


SQLITE_PRIVATE int sqlite3Fts3SegReaderStep(
  Fts3Table *p,                   /* Virtual table handle */
  Fts3MultiSegReader *pCsr        /* Cursor object */
){
  int rc = SQLITE_OK;

  int isIgnoreEmpty =  (pCsr->pFilter->flags & FTS3_SEGMENT_IGNORE_EMPTY);
  int isRequirePos =   (pCsr->pFilter->flags & FTS3_SEGMENT_REQUIRE_POS);
  int isColFilter =    (pCsr->pFilter->flags & FTS3_SEGMENT_COLUMN_FILTER);
  int isPrefix =       (pCsr->pFilter->flags & FTS3_SEGMENT_PREFIX);
  int isScan =         (pCsr->pFilter->flags & FTS3_SEGMENT_SCAN);
  int isFirst =        (pCsr->pFilter->flags & FTS3_SEGMENT_FIRST);

  Fts3SegReader **apSegment = pCsr->apSegment;
  int nSegment = pCsr->nSegment;
  Fts3SegFilter *pFilter = pCsr->pFilter;
  int (*xCmp)(Fts3SegReader *, Fts3SegReader *) = (
    p->bDescIdx ? fts3SegReaderDoclistCmpRev : fts3SegReaderDoclistCmp
  );

  if( pCsr->nSegment==0 ) return SQLITE_OK;

  do {
    int nMerge;
    int i;
  
    /* Advance the first pCsr->nAdvance entries in the apSegment[] array
    ** forward. Then sort the list in order of current term again.  
    */
    for(i=0; i<pCsr->nAdvance; i++){
      rc = fts3SegReaderNext(p, apSegment[i], 0);
      if( rc!=SQLITE_OK ) return rc;
    }
    fts3SegReaderSort(apSegment, nSegment, pCsr->nAdvance, fts3SegReaderCmp);
    pCsr->nAdvance = 0;

    /* If all the seg-readers are at EOF, we're finished. return SQLITE_OK. */
    assert( rc==SQLITE_OK );
    if( apSegment[0]->aNode==0 ) break;

    pCsr->nTerm = apSegment[0]->nTerm;
    pCsr->zTerm = apSegment[0]->zTerm;

    /* If this is a prefix-search, and if the term that apSegment[0] points
    ** to does not share a suffix with pFilter->zTerm/nTerm, then all 
    ** required callbacks have been made. In this case exit early.
    **
    ** Similarly, if this is a search for an exact match, and the first term
    ** of segment apSegment[0] is not a match, exit early.
    */
    if( pFilter->zTerm && !isScan ){
      if( pCsr->nTerm<pFilter->nTerm 
       || (!isPrefix && pCsr->nTerm>pFilter->nTerm)
       || memcmp(pCsr->zTerm, pFilter->zTerm, pFilter->nTerm) 
      ){
        break;
      }
    }

    nMerge = 1;
    while( nMerge<nSegment 
        && apSegment[nMerge]->aNode
        && apSegment[nMerge]->nTerm==pCsr->nTerm 
        && 0==memcmp(pCsr->zTerm, apSegment[nMerge]->zTerm, pCsr->nTerm)
    ){
      nMerge++;
    }

    assert( isIgnoreEmpty || (isRequirePos && !isColFilter) );
    if( nMerge==1 
     && !isIgnoreEmpty 
     && !isFirst 
     && (p->bDescIdx==0 || fts3SegReaderIsPending(apSegment[0])==0)
    ){
      pCsr->nDoclist = apSegment[0]->nDoclist;
      if( fts3SegReaderIsPending(apSegment[0]) ){
        rc = fts3MsrBufferData(pCsr, apSegment[0]->aDoclist, pCsr->nDoclist);
        pCsr->aDoclist = pCsr->aBuffer;
      }else{
        pCsr->aDoclist = apSegment[0]->aDoclist;
      }
      if( rc==SQLITE_OK ) rc = SQLITE_ROW;
    }else{
      int nDoclist = 0;           /* Size of doclist */
      sqlite3_int64 iPrev = 0;    /* Previous docid stored in doclist */

      /* The current term of the first nMerge entries in the array
      ** of Fts3SegReader objects is the same. The doclists must be merged
      ** and a single term returned with the merged doclist.
      */
      for(i=0; i<nMerge; i++){
        fts3SegReaderFirstDocid(p, apSegment[i]);
      }
      fts3SegReaderSort(apSegment, nMerge, nMerge, xCmp);
      while( apSegment[0]->pOffsetList ){
        int j;                    /* Number of segments that share a docid */
        char *pList;
        int nList;
        int nByte;
        sqlite3_int64 iDocid = apSegment[0]->iDocid;
        fts3SegReaderNextDocid(p, apSegment[0], &pList, &nList);
        j = 1;
        while( j<nMerge
            && apSegment[j]->pOffsetList
            && apSegment[j]->iDocid==iDocid
        ){
          fts3SegReaderNextDocid(p, apSegment[j], 0, 0);
          j++;
        }

        if( isColFilter ){
          fts3ColumnFilter(pFilter->iCol, &pList, &nList);
        }

        if( !isIgnoreEmpty || nList>0 ){

          /* Calculate the 'docid' delta value to write into the merged 
          ** doclist. */
          sqlite3_int64 iDelta;
          if( p->bDescIdx && nDoclist>0 ){
            iDelta = iPrev - iDocid;
          }else{
            iDelta = iDocid - iPrev;
          }
          assert( iDelta>0 || (nDoclist==0 && iDelta==iDocid) );
          assert( nDoclist>0 || iDelta==iDocid );

          nByte = sqlite3Fts3VarintLen(iDelta) + (isRequirePos?nList+1:0);
          if( nDoclist+nByte>pCsr->nBuffer ){
            char *aNew;
            pCsr->nBuffer = (nDoclist+nByte)*2;
            aNew = sqlite3_realloc(pCsr->aBuffer, pCsr->nBuffer);
            if( !aNew ){
              return SQLITE_NOMEM;
            }
            pCsr->aBuffer = aNew;
          }

          if( isFirst ){
            char *a = &pCsr->aBuffer[nDoclist];
            int nWrite;
           
            nWrite = sqlite3Fts3FirstFilter(iDelta, pList, nList, a);
            if( nWrite ){
              iPrev = iDocid;
              nDoclist += nWrite;
            }
          }else{
            nDoclist += sqlite3Fts3PutVarint(&pCsr->aBuffer[nDoclist], iDelta);
            iPrev = iDocid;
            if( isRequirePos ){
              memcpy(&pCsr->aBuffer[nDoclist], pList, nList);
              nDoclist += nList;
              pCsr->aBuffer[nDoclist++] = '\0';
            }
          }
        }

        fts3SegReaderSort(apSegment, nMerge, j, xCmp);
      }
      if( nDoclist>0 ){
        pCsr->aDoclist = pCsr->aBuffer;
        pCsr->nDoclist = nDoclist;
        rc = SQLITE_ROW;
      }
    }
    pCsr->nAdvance = nMerge;
  }while( rc==SQLITE_OK );

  return rc;
}


SQLITE_PRIVATE void sqlite3Fts3SegReaderFinish(
  Fts3MultiSegReader *pCsr       /* Cursor object */
){
  if( pCsr ){
    int i;
    for(i=0; i<pCsr->nSegment; i++){
      sqlite3Fts3SegReaderFree(pCsr->apSegment[i]);
    }
    sqlite3_free(pCsr->apSegment);
    sqlite3_free(pCsr->aBuffer);

    pCsr->nSegment = 0;
    pCsr->apSegment = 0;
    pCsr->aBuffer = 0;
  }
}

/*
** Merge all level iLevel segments in the database into a single 
** iLevel+1 segment. Or, if iLevel<0, merge all segments into a
** single segment with a level equal to the numerically largest level 
** currently present in the database.
**
** If this function is called with iLevel<0, but there is only one
** segment in the database, SQLITE_DONE is returned immediately. 
** Otherwise, if successful, SQLITE_OK is returned. If an error occurs, 
** an SQLite error code is returned.
*/
static int fts3SegmentMerge(Fts3Table *p, int iIndex, int iLevel){
  int rc;                         /* Return code */
  int iIdx = 0;                   /* Index of new segment */
  int iNewLevel = 0;              /* Level/index to create new segment at */
  SegmentWriter *pWriter = 0;     /* Used to write the new, merged, segment */
  Fts3SegFilter filter;           /* Segment term filter condition */
  Fts3MultiSegReader csr;        /* Cursor to iterate through level(s) */
  int bIgnoreEmpty = 0;           /* True to ignore empty segments */

  assert( iLevel==FTS3_SEGCURSOR_ALL
       || iLevel==FTS3_SEGCURSOR_PENDING
       || iLevel>=0
  );
  assert( iLevel<FTS3_SEGDIR_MAXLEVEL );
  assert( iIndex>=0 && iIndex<p->nIndex );

  rc = sqlite3Fts3SegReaderCursor(p, iIndex, iLevel, 0, 0, 1, 0, &csr);
  if( rc!=SQLITE_OK || csr.nSegment==0 ) goto finished;

  if( iLevel==FTS3_SEGCURSOR_ALL ){
    /* This call is to merge all segments in the database to a single
    ** segment. The level of the new segment is equal to the the numerically 
    ** greatest segment level currently present in the database for this
    ** index. The idx of the new segment is always 0.  */
    if( csr.nSegment==1 ){
      rc = SQLITE_DONE;
      goto finished;
    }
    rc = fts3SegmentMaxLevel(p, iIndex, &iNewLevel);
    bIgnoreEmpty = 1;

  }else if( iLevel==FTS3_SEGCURSOR_PENDING ){
    iNewLevel = iIndex * FTS3_SEGDIR_MAXLEVEL; 
    rc = fts3AllocateSegdirIdx(p, iIndex, 0, &iIdx);
  }else{
    /* This call is to merge all segments at level iLevel. find the next
    ** available segment index at level iLevel+1. The call to
    ** fts3AllocateSegdirIdx() will merge the segments at level iLevel+1 to 
    ** a single iLevel+2 segment if necessary.  */
    rc = fts3AllocateSegdirIdx(p, iIndex, iLevel+1, &iIdx);
    iNewLevel = iIndex * FTS3_SEGDIR_MAXLEVEL + iLevel+1;
  }
  if( rc!=SQLITE_OK ) goto finished;
  assert( csr.nSegment>0 );
  assert( iNewLevel>=(iIndex*FTS3_SEGDIR_MAXLEVEL) );
  assert( iNewLevel<((iIndex+1)*FTS3_SEGDIR_MAXLEVEL) );

  memset(&filter, 0, sizeof(Fts3SegFilter));
  filter.flags = FTS3_SEGMENT_REQUIRE_POS;
  filter.flags |= (bIgnoreEmpty ? FTS3_SEGMENT_IGNORE_EMPTY : 0);

  rc = sqlite3Fts3SegReaderStart(p, &csr, &filter);
  while( SQLITE_OK==rc ){
    rc = sqlite3Fts3SegReaderStep(p, &csr);
    if( rc!=SQLITE_ROW ) break;
    rc = fts3SegWriterAdd(p, &pWriter, 1, 
        csr.zTerm, csr.nTerm, csr.aDoclist, csr.nDoclist);
  }
  if( rc!=SQLITE_OK ) goto finished;
  assert( pWriter );

  if( iLevel!=FTS3_SEGCURSOR_PENDING ){
    rc = fts3DeleteSegdir(p, iIndex, iLevel, csr.apSegment, csr.nSegment);
    if( rc!=SQLITE_OK ) goto finished;
  }
  rc = fts3SegWriterFlush(p, pWriter, iNewLevel, iIdx);

 finished:
  fts3SegWriterFree(pWriter);
  sqlite3Fts3SegReaderFinish(&csr);
  return rc;
}


/* 
** Flush the contents of pendingTerms to level 0 segments.
*/
SQLITE_PRIVATE int sqlite3Fts3PendingTermsFlush(Fts3Table *p){
  int rc = SQLITE_OK;
  int i;
  for(i=0; rc==SQLITE_OK && i<p->nIndex; i++){
    rc = fts3SegmentMerge(p, i, FTS3_SEGCURSOR_PENDING);
    if( rc==SQLITE_DONE ) rc = SQLITE_OK;
  }
  sqlite3Fts3PendingTermsClear(p);
  return rc;
}

/*
** Encode N integers as varints into a blob.
*/
static void fts3EncodeIntArray(
  int N,             /* The number of integers to encode */
  u32 *a,            /* The integer values */
  char *zBuf,        /* Write the BLOB here */
  int *pNBuf         /* Write number of bytes if zBuf[] used here */
){
  int i, j;
  for(i=j=0; i<N; i++){
    j += sqlite3Fts3PutVarint(&zBuf[j], (sqlite3_int64)a[i]);
  }
  *pNBuf = j;
}

/*
** Decode a blob of varints into N integers
*/
static void fts3DecodeIntArray(
  int N,             /* The number of integers to decode */
  u32 *a,            /* Write the integer values */
  const char *zBuf,  /* The BLOB containing the varints */
  int nBuf           /* size of the BLOB */
){
  int i, j;
  UNUSED_PARAMETER(nBuf);
  for(i=j=0; i<N; i++){
    sqlite3_int64 x;
    j += sqlite3Fts3GetVarint(&zBuf[j], &x);
    assert(j<=nBuf);
    a[i] = (u32)(x & 0xffffffff);
  }
}

/*
** Insert the sizes (in tokens) for each column of the document
** with docid equal to p->iPrevDocid.  The sizes are encoded as
** a blob of varints.
*/
static void fts3InsertDocsize(
  int *pRC,                       /* Result code */
  Fts3Table *p,                   /* Table into which to insert */
  u32 *aSz                        /* Sizes of each column, in tokens */
){
  char *pBlob;             /* The BLOB encoding of the document size */
  int nBlob;               /* Number of bytes in the BLOB */
  sqlite3_stmt *pStmt;     /* Statement used to insert the encoding */
  int rc;                  /* Result code from subfunctions */

  if( *pRC ) return;
  pBlob = sqlite3_malloc( 10*p->nColumn );
  if( pBlob==0 ){
    *pRC = SQLITE_NOMEM;
    return;
  }
  fts3EncodeIntArray(p->nColumn, aSz, pBlob, &nBlob);
  rc = fts3SqlStmt(p, SQL_REPLACE_DOCSIZE, &pStmt, 0);
  if( rc ){
    sqlite3_free(pBlob);
    *pRC = rc;
    return;
  }
  sqlite3_bind_int64(pStmt, 1, p->iPrevDocid);
  sqlite3_bind_blob(pStmt, 2, pBlob, nBlob, sqlite3_free);
  sqlite3_step(pStmt);
  *pRC = sqlite3_reset(pStmt);
}

/*
** Record 0 of the %_stat table contains a blob consisting of N varints,
** where N is the number of user defined columns in the fts3 table plus
** two. If nCol is the number of user defined columns, then values of the 
** varints are set as follows:
**
**   Varint 0:       Total number of rows in the table.
**
**   Varint 1..nCol: For each column, the total number of tokens stored in
**                   the column for all rows of the table.
**
**   Varint 1+nCol:  The total size, in bytes, of all text values in all
**                   columns of all rows of the table.
**
*/
static void fts3UpdateDocTotals(
  int *pRC,                       /* The result code */
  Fts3Table *p,                   /* Table being updated */
  u32 *aSzIns,                    /* Size increases */
  u32 *aSzDel,                    /* Size decreases */
  int nChng                       /* Change in the number of documents */
){
  char *pBlob;             /* Storage for BLOB written into %_stat */
  int nBlob;               /* Size of BLOB written into %_stat */
  u32 *a;                  /* Array of integers that becomes the BLOB */
  sqlite3_stmt *pStmt;     /* Statement for reading and writing */
  int i;                   /* Loop counter */
  int rc;                  /* Result code from subfunctions */

  const int nStat = p->nColumn+2;

  if( *pRC ) return;
  a = sqlite3_malloc( (sizeof(u32)+10)*nStat );
  if( a==0 ){
    *pRC = SQLITE_NOMEM;
    return;
  }
  pBlob = (char*)&a[nStat];
  rc = fts3SqlStmt(p, SQL_SELECT_DOCTOTAL, &pStmt, 0);
  if( rc ){
    sqlite3_free(a);
    *pRC = rc;
    return;
  }
  if( sqlite3_step(pStmt)==SQLITE_ROW ){
    fts3DecodeIntArray(nStat, a,
         sqlite3_column_blob(pStmt, 0),
         sqlite3_column_bytes(pStmt, 0));
  }else{
    memset(a, 0, sizeof(u32)*(nStat) );
  }
  sqlite3_reset(pStmt);
  if( nChng<0 && a[0]<(u32)(-nChng) ){
    a[0] = 0;
  }else{
    a[0] += nChng;
  }
  for(i=0; i<p->nColumn+1; i++){
    u32 x = a[i+1];
    if( x+aSzIns[i] < aSzDel[i] ){
      x = 0;
    }else{
      x = x + aSzIns[i] - aSzDel[i];
    }
    a[i+1] = x;
  }
  fts3EncodeIntArray(nStat, a, pBlob, &nBlob);
  rc = fts3SqlStmt(p, SQL_REPLACE_DOCTOTAL, &pStmt, 0);
  if( rc ){
    sqlite3_free(a);
    *pRC = rc;
    return;
  }
  sqlite3_bind_blob(pStmt, 1, pBlob, nBlob, SQLITE_STATIC);
  sqlite3_step(pStmt);
  *pRC = sqlite3_reset(pStmt);
  sqlite3_free(a);
}

static int fts3DoOptimize(Fts3Table *p, int bReturnDone){
  int i;
  int bSeenDone = 0;
  int rc = SQLITE_OK;
  for(i=0; rc==SQLITE_OK && i<p->nIndex; i++){
    rc = fts3SegmentMerge(p, i, FTS3_SEGCURSOR_ALL);
    if( rc==SQLITE_DONE ){
      bSeenDone = 1;
      rc = SQLITE_OK;
    }
  }
  sqlite3Fts3SegmentsClose(p);
  sqlite3Fts3PendingTermsClear(p);

  return (rc==SQLITE_OK && bReturnDone && bSeenDone) ? SQLITE_DONE : rc;
}

/*
** This function is called when the user executes the following statement:
**
**     INSERT INTO <tbl>(<tbl>) VALUES('rebuild');
**
** The entire FTS index is discarded and rebuilt. If the table is one 
** created using the content=xxx option, then the new index is based on
** the current contents of the xxx table. Otherwise, it is rebuilt based
** on the contents of the %_content table.
*/
static int fts3DoRebuild(Fts3Table *p){
  int rc;                         /* Return Code */

  rc = fts3DeleteAll(p, 0);
  if( rc==SQLITE_OK ){
    u32 *aSz = 0;
    u32 *aSzIns = 0;
    u32 *aSzDel = 0;
    sqlite3_stmt *pStmt = 0;
    int nEntry = 0;

    /* Compose and prepare an SQL statement to loop through the content table */
    char *zSql = sqlite3_mprintf("SELECT %s" , p->zReadExprlist);
    if( !zSql ){
      rc = SQLITE_NOMEM;
    }else{
      rc = sqlite3_prepare_v2(p->db, zSql, -1, &pStmt, 0);
      sqlite3_free(zSql);
    }

    if( rc==SQLITE_OK ){
      int nByte = sizeof(u32) * (p->nColumn+1)*3;
      aSz = (u32 *)sqlite3_malloc(nByte);
      if( aSz==0 ){
        rc = SQLITE_NOMEM;
      }else{
        memset(aSz, 0, nByte);
        aSzIns = &aSz[p->nColumn+1];
        aSzDel = &aSzIns[p->nColumn+1];
      }
    }

    while( rc==SQLITE_OK && SQLITE_ROW==sqlite3_step(pStmt) ){
      int iCol;
      rc = fts3PendingTermsDocid(p, sqlite3_column_int64(pStmt, 0));
      aSz[p->nColumn] = 0;
      for(iCol=0; rc==SQLITE_OK && iCol<p->nColumn; iCol++){
        const char *z = (const char *) sqlite3_column_text(pStmt, iCol+1);
        rc = fts3PendingTermsAdd(p, z, iCol, &aSz[iCol]);
        aSz[p->nColumn] += sqlite3_column_bytes(pStmt, iCol+1);
      }
      if( p->bHasDocsize ){
        fts3InsertDocsize(&rc, p, aSz);
      }
      if( rc!=SQLITE_OK ){
        sqlite3_finalize(pStmt);
        pStmt = 0;
      }else{
        nEntry++;
        for(iCol=0; iCol<=p->nColumn; iCol++){
          aSzIns[iCol] += aSz[iCol];
        }
      }
    }
    if( p->bHasStat ){
      fts3UpdateDocTotals(&rc, p, aSzIns, aSzDel, nEntry);
    }
    sqlite3_free(aSz);

    if( pStmt ){
      int rc2 = sqlite3_finalize(pStmt);
      if( rc==SQLITE_OK ){
        rc = rc2;
      }
    }
  }

  return rc;
}

/*
** Handle a 'special' INSERT of the form:
**
**   "INSERT INTO tbl(tbl) VALUES(<expr>)"
**
** Argument pVal contains the result of <expr>. Currently the only 
** meaningful value to insert is the text 'optimize'.
*/
static int fts3SpecialInsert(Fts3Table *p, sqlite3_value *pVal){
  int rc;                         /* Return Code */
  const char *zVal = (const char *)sqlite3_value_text(pVal);
  int nVal = sqlite3_value_bytes(pVal);

  if( !zVal ){
    return SQLITE_NOMEM;
  }else if( nVal==8 && 0==sqlite3_strnicmp(zVal, "optimize", 8) ){
    rc = fts3DoOptimize(p, 0);
  }else if( nVal==7 && 0==sqlite3_strnicmp(zVal, "rebuild", 7) ){
    rc = fts3DoRebuild(p);
#ifdef SQLITE_TEST
  }else if( nVal>9 && 0==sqlite3_strnicmp(zVal, "nodesize=", 9) ){
    p->nNodeSize = atoi(&zVal[9]);
    rc = SQLITE_OK;
  }else if( nVal>11 && 0==sqlite3_strnicmp(zVal, "maxpending=", 9) ){
    p->nMaxPendingData = atoi(&zVal[11]);
    rc = SQLITE_OK;
#endif
  }else{
    rc = SQLITE_ERROR;
  }

  return rc;
}

/*
** Delete all cached deferred doclists. Deferred doclists are cached
** (allocated) by the sqlite3Fts3CacheDeferredDoclists() function.
*/
SQLITE_PRIVATE void sqlite3Fts3FreeDeferredDoclists(Fts3Cursor *pCsr){
  Fts3DeferredToken *pDef;
  for(pDef=pCsr->pDeferred; pDef; pDef=pDef->pNext){
    fts3PendingListDelete(pDef->pList);
    pDef->pList = 0;
  }
}

/*
** Free all entries in the pCsr->pDeffered list. Entries are added to 
** this list using sqlite3Fts3DeferToken().
*/
SQLITE_PRIVATE void sqlite3Fts3FreeDeferredTokens(Fts3Cursor *pCsr){
  Fts3DeferredToken *pDef;
  Fts3DeferredToken *pNext;
  for(pDef=pCsr->pDeferred; pDef; pDef=pNext){
    pNext = pDef->pNext;
    fts3PendingListDelete(pDef->pList);
    sqlite3_free(pDef);
  }
  pCsr->pDeferred = 0;
}

/*
** Generate deferred-doclists for all tokens in the pCsr->pDeferred list
** based on the row that pCsr currently points to.
**
** A deferred-doclist is like any other doclist with position information
** included, except that it only contains entries for a single row of the
** table, not for all rows.
*/
SQLITE_PRIVATE int sqlite3Fts3CacheDeferredDoclists(Fts3Cursor *pCsr){
  int rc = SQLITE_OK;             /* Return code */
  if( pCsr->pDeferred ){
    int i;                        /* Used to iterate through table columns */
    sqlite3_int64 iDocid;         /* Docid of the row pCsr points to */
    Fts3DeferredToken *pDef;      /* Used to iterate through deferred tokens */
  
    Fts3Table *p = (Fts3Table *)pCsr->base.pVtab;
    sqlite3_tokenizer *pT = p->pTokenizer;
    sqlite3_tokenizer_module const *pModule = pT->pModule;
   
    assert( pCsr->isRequireSeek==0 );
    iDocid = sqlite3_column_int64(pCsr->pStmt, 0);
  
    for(i=0; i<p->nColumn && rc==SQLITE_OK; i++){
      const char *zText = (const char *)sqlite3_column_text(pCsr->pStmt, i+1);
      sqlite3_tokenizer_cursor *pTC = 0;
  
      rc = pModule->xOpen(pT, zText, -1, &pTC);
      while( rc==SQLITE_OK ){
        char const *zToken;       /* Buffer containing token */
        int nToken;               /* Number of bytes in token */
        int iDum1, iDum2;         /* Dummy variables */
        int iPos;                 /* Position of token in zText */
  
        pTC->pTokenizer = pT;
        rc = pModule->xNext(pTC, &zToken, &nToken, &iDum1, &iDum2, &iPos);
        for(pDef=pCsr->pDeferred; pDef && rc==SQLITE_OK; pDef=pDef->pNext){
          Fts3PhraseToken *pPT = pDef->pToken;
          if( (pDef->iCol>=p->nColumn || pDef->iCol==i)
           && (pPT->bFirst==0 || iPos==0)
           && (pPT->n==nToken || (pPT->isPrefix && pPT->n<nToken))
           && (0==memcmp(zToken, pPT->z, pPT->n))
          ){
            fts3PendingListAppend(&pDef->pList, iDocid, i, iPos, &rc);
          }
        }
      }
      if( pTC ) pModule->xClose(pTC);
      if( rc==SQLITE_DONE ) rc = SQLITE_OK;
    }
  
    for(pDef=pCsr->pDeferred; pDef && rc==SQLITE_OK; pDef=pDef->pNext){
      if( pDef->pList ){
        rc = fts3PendingListAppendVarint(&pDef->pList, 0);
      }
    }
  }

  return rc;
}

SQLITE_PRIVATE int sqlite3Fts3DeferredTokenList(
  Fts3DeferredToken *p, 
  char **ppData, 
  int *pnData
){
  char *pRet;
  int nSkip;
  sqlite3_int64 dummy;

  *ppData = 0;
  *pnData = 0;

  if( p->pList==0 ){
    return SQLITE_OK;
  }

  pRet = (char *)sqlite3_malloc(p->pList->nData);
  if( !pRet ) return SQLITE_NOMEM;

  nSkip = sqlite3Fts3GetVarint(p->pList->aData, &dummy);
  *pnData = p->pList->nData - nSkip;
  *ppData = pRet;
  
  memcpy(pRet, &p->pList->aData[nSkip], *pnData);
  return SQLITE_OK;
}

/*
** Add an entry for token pToken to the pCsr->pDeferred list.
*/
SQLITE_PRIVATE int sqlite3Fts3DeferToken(
  Fts3Cursor *pCsr,               /* Fts3 table cursor */
  Fts3PhraseToken *pToken,        /* Token to defer */
  int iCol                        /* Column that token must appear in (or -1) */
){
  Fts3DeferredToken *pDeferred;
  pDeferred = sqlite3_malloc(sizeof(*pDeferred));
  if( !pDeferred ){
    return SQLITE_NOMEM;
  }
  memset(pDeferred, 0, sizeof(*pDeferred));
  pDeferred->pToken = pToken;
  pDeferred->pNext = pCsr->pDeferred; 
  pDeferred->iCol = iCol;
  pCsr->pDeferred = pDeferred;

  assert( pToken->pDeferred==0 );
  pToken->pDeferred = pDeferred;

  return SQLITE_OK;
}

/*
** SQLite value pRowid contains the rowid of a row that may or may not be
** present in the FTS3 table. If it is, delete it and adjust the contents
** of subsiduary data structures accordingly.
*/
static int fts3DeleteByRowid(
  Fts3Table *p, 
  sqlite3_value *pRowid, 
  int *pnDoc,
  u32 *aSzDel
){
  int isEmpty = 0;
  int rc = fts3IsEmpty(p, pRowid, &isEmpty);
  if( rc==SQLITE_OK ){
    if( isEmpty ){
      /* Deleting this row means the whole table is empty. In this case
      ** delete the contents of all three tables and throw away any
      ** data in the pendingTerms hash table.  */
      rc = fts3DeleteAll(p, 1);
      *pnDoc = *pnDoc - 1;
    }else{
      sqlite3_int64 iRemove = sqlite3_value_int64(pRowid);
      rc = fts3PendingTermsDocid(p, iRemove);
      fts3DeleteTerms(&rc, p, pRowid, aSzDel);
      if( p->zContentTbl==0 ){
        fts3SqlExec(&rc, p, SQL_DELETE_CONTENT, &pRowid);
        if( sqlite3_changes(p->db) ) *pnDoc = *pnDoc - 1;
      }else{
        *pnDoc = *pnDoc - 1;
      }
      if( p->bHasDocsize ){
        fts3SqlExec(&rc, p, SQL_DELETE_DOCSIZE, &pRowid);
      }
    }
  }

  return rc;
}

/*
** This function does the work for the xUpdate method of FTS3 virtual
** tables.
*/
SQLITE_PRIVATE int sqlite3Fts3UpdateMethod(
  sqlite3_vtab *pVtab,            /* FTS3 vtab object */
  int nArg,                       /* Size of argument array */
  sqlite3_value **apVal,          /* Array of arguments */
  sqlite_int64 *pRowid            /* OUT: The affected (or effected) rowid */
){
  Fts3Table *p = (Fts3Table *)pVtab;
  int rc = SQLITE_OK;             /* Return Code */
  int isRemove = 0;               /* True for an UPDATE or DELETE */
  u32 *aSzIns = 0;                /* Sizes of inserted documents */
  u32 *aSzDel;                    /* Sizes of deleted documents */
  int nChng = 0;                  /* Net change in number of documents */
  int bInsertDone = 0;

  assert( p->pSegments==0 );

  /* Check for a "special" INSERT operation. One of the form:
  **
  **   INSERT INTO xyz(xyz) VALUES('command');
  */
  if( nArg>1 
   && sqlite3_value_type(apVal[0])==SQLITE_NULL 
   && sqlite3_value_type(apVal[p->nColumn+2])!=SQLITE_NULL 
  ){
    rc = fts3SpecialInsert(p, apVal[p->nColumn+2]);
    goto update_out;
  }

  /* Allocate space to hold the change in document sizes */
  aSzIns = sqlite3_malloc( sizeof(aSzIns[0])*(p->nColumn+1)*2 );
  if( aSzIns==0 ){
    rc = SQLITE_NOMEM;
    goto update_out;
  }
  aSzDel = &aSzIns[p->nColumn+1];
  memset(aSzIns, 0, sizeof(aSzIns[0])*(p->nColumn+1)*2);

  /* If this is an INSERT operation, or an UPDATE that modifies the rowid
  ** value, then this operation requires constraint handling.
  **
  ** If the on-conflict mode is REPLACE, this means that the existing row
  ** should be deleted from the database before inserting the new row. Or,
  ** if the on-conflict mode is other than REPLACE, then this method must
  ** detect the conflict and return SQLITE_CONSTRAINT before beginning to
  ** modify the database file.
  */
  if( nArg>1 && p->zContentTbl==0 ){
    /* Find the value object that holds the new rowid value. */
    sqlite3_value *pNewRowid = apVal[3+p->nColumn];
    if( sqlite3_value_type(pNewRowid)==SQLITE_NULL ){
      pNewRowid = apVal[1];
    }

    if( sqlite3_value_type(pNewRowid)!=SQLITE_NULL && ( 
        sqlite3_value_type(apVal[0])==SQLITE_NULL
     || sqlite3_value_int64(apVal[0])!=sqlite3_value_int64(pNewRowid)
    )){
      /* The new rowid is not NULL (in this case the rowid will be
      ** automatically assigned and there is no chance of a conflict), and 
      ** the statement is either an INSERT or an UPDATE that modifies the
      ** rowid column. So if the conflict mode is REPLACE, then delete any
      ** existing row with rowid=pNewRowid. 
      **
      ** Or, if the conflict mode is not REPLACE, insert the new record into 
      ** the %_content table. If we hit the duplicate rowid constraint (or any
      ** other error) while doing so, return immediately.
      **
      ** This branch may also run if pNewRowid contains a value that cannot
      ** be losslessly converted to an integer. In this case, the eventual 
      ** call to fts3InsertData() (either just below or further on in this
      ** function) will return SQLITE_MISMATCH. If fts3DeleteByRowid is 
      ** invoked, it will delete zero rows (since no row will have
      ** docid=$pNewRowid if $pNewRowid is not an integer value).
      */
      if( sqlite3_vtab_on_conflict(p->db)==SQLITE_REPLACE ){
        rc = fts3DeleteByRowid(p, pNewRowid, &nChng, aSzDel);
      }else{
        rc = fts3InsertData(p, apVal, pRowid);
        bInsertDone = 1;
      }
    }
  }
  if( rc!=SQLITE_OK ){
    goto update_out;
  }

  /* If this is a DELETE or UPDATE operation, remove the old record. */
  if( sqlite3_value_type(apVal[0])!=SQLITE_NULL ){
    assert( sqlite3_value_type(apVal[0])==SQLITE_INTEGER );
    rc = fts3DeleteByRowid(p, apVal[0], &nChng, aSzDel);
    isRemove = 1;
  }
  
  /* If this is an INSERT or UPDATE operation, insert the new record. */
  if( nArg>1 && rc==SQLITE_OK ){
    if( bInsertDone==0 ){
      rc = fts3InsertData(p, apVal, pRowid);
      if( rc==SQLITE_CONSTRAINT && p->zContentTbl==0 ){
        rc = FTS_CORRUPT_VTAB;
      }
    }
    if( rc==SQLITE_OK && (!isRemove || *pRowid!=p->iPrevDocid ) ){
      rc = fts3PendingTermsDocid(p, *pRowid);
    }
    if( rc==SQLITE_OK ){
      assert( p->iPrevDocid==*pRowid );
      rc = fts3InsertTerms(p, apVal, aSzIns);
    }
    if( p->bHasDocsize ){
      fts3InsertDocsize(&rc, p, aSzIns);
    }
    nChng++;
  }

  if( p->bHasStat ){
    fts3UpdateDocTotals(&rc, p, aSzIns, aSzDel, nChng);
  }

 update_out:
  sqlite3_free(aSzIns);
  sqlite3Fts3SegmentsClose(p);
  return rc;
}

/* 
** Flush any data in the pending-terms hash table to disk. If successful,
** merge all segments in the database (including the new segment, if 
** there was any data to flush) into a single segment. 
*/
SQLITE_PRIVATE int sqlite3Fts3Optimize(Fts3Table *p){
  int rc;
  rc = sqlite3_exec(p->db, "SAVEPOINT fts3", 0, 0, 0);
  if( rc==SQLITE_OK ){
    rc = fts3DoOptimize(p, 1);
    if( rc==SQLITE_OK || rc==SQLITE_DONE ){
      int rc2 = sqlite3_exec(p->db, "RELEASE fts3", 0, 0, 0);
      if( rc2!=SQLITE_OK ) rc = rc2;
    }else{
      sqlite3_exec(p->db, "ROLLBACK TO fts3", 0, 0, 0);
      sqlite3_exec(p->db, "RELEASE fts3", 0, 0, 0);
    }
  }
  sqlite3Fts3SegmentsClose(p);
  return rc;
}

#endif

/************** End of fts3_write.c ******************************************/
/************** Begin file fts3_snippet.c ************************************/
/*
** 2009 Oct 23
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
*/

#if !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS3)

/* #include <string.h> */
/* #include <assert.h> */

/*
** Characters that may appear in the second argument to matchinfo().
*/
#define FTS3_MATCHINFO_NPHRASE   'p'        /* 1 value */
#define FTS3_MATCHINFO_NCOL      'c'        /* 1 value */
#define FTS3_MATCHINFO_NDOC      'n'        /* 1 value */
#define FTS3_MATCHINFO_AVGLENGTH 'a'        /* nCol values */
#define FTS3_MATCHINFO_LENGTH    'l'        /* nCol values */
#define FTS3_MATCHINFO_LCS       's'        /* nCol values */
#define FTS3_MATCHINFO_HITS      'x'        /* 3*nCol*nPhrase values */

/*
** The default value for the second argument to matchinfo(). 
*/
#define FTS3_MATCHINFO_DEFAULT   "pcx"


/*
** Used as an fts3ExprIterate() context when loading phrase doclists to
** Fts3Expr.aDoclist[]/nDoclist.
*/
typedef struct LoadDoclistCtx LoadDoclistCtx;
struct LoadDoclistCtx {
  Fts3Cursor *pCsr;               /* FTS3 Cursor */
  int nPhrase;                    /* Number of phrases seen so far */
  int nToken;                     /* Number of tokens seen so far */
};

/*
** The following types are used as part of the implementation of the 
** fts3BestSnippet() routine.
*/
typedef struct SnippetIter SnippetIter;
typedef struct SnippetPhrase SnippetPhrase;
typedef struct SnippetFragment SnippetFragment;

struct SnippetIter {
  Fts3Cursor *pCsr;               /* Cursor snippet is being generated from */
  int iCol;                       /* Extract snippet from this column */
  int nSnippet;                   /* Requested snippet length (in tokens) */
  int nPhrase;                    /* Number of phrases in query */
  SnippetPhrase *aPhrase;         /* Array of size nPhrase */
  int iCurrent;                   /* First token of current snippet */
};

struct SnippetPhrase {
  int nToken;                     /* Number of tokens in phrase */
  char *pList;                    /* Pointer to start of phrase position list */
  int iHead;                      /* Next value in position list */
  char *pHead;                    /* Position list data following iHead */
  int iTail;                      /* Next value in trailing position list */
  char *pTail;                    /* Position list data following iTail */
};

struct SnippetFragment {
  int iCol;                       /* Column snippet is extracted from */
  int iPos;                       /* Index of first token in snippet */
  u64 covered;                    /* Mask of query phrases covered */
  u64 hlmask;                     /* Mask of snippet terms to highlight */
};

/*
** This type is used as an fts3ExprIterate() context object while 
** accumulating the data returned by the matchinfo() function.
*/
typedef struct MatchInfo MatchInfo;
struct MatchInfo {
  Fts3Cursor *pCursor;            /* FTS3 Cursor */
  int nCol;                       /* Number of columns in table */
  int nPhrase;                    /* Number of matchable phrases in query */
  sqlite3_int64 nDoc;             /* Number of docs in database */
  u32 *aMatchinfo;                /* Pre-allocated buffer */
};



/*
** The snippet() and offsets() functions both return text values. An instance
** of the following structure is used to accumulate those values while the
** functions are running. See fts3StringAppend() for details.
*/
typedef struct StrBuffer StrBuffer;
struct StrBuffer {
  char *z;                        /* Pointer to buffer containing string */
  int n;                          /* Length of z in bytes (excl. nul-term) */
  int nAlloc;                     /* Allocated size of buffer z in bytes */
};


/*
** This function is used to help iterate through a position-list. A position
** list is a list of unique integers, sorted from smallest to largest. Each
** element of the list is represented by an FTS3 varint that takes the value
** of the difference between the current element and the previous one plus
** two. For example, to store the position-list:
**
**     4 9 113
**
** the three varints:
**
**     6 7 106
**
** are encoded.
**
** When this function is called, *pp points to the start of an element of
** the list. *piPos contains the value of the previous entry in the list.
** After it returns, *piPos contains the value of the next element of the
** list and *pp is advanced to the following varint.
*/
static void fts3GetDeltaPosition(char **pp, int *piPos){
  int iVal;
  *pp += sqlite3Fts3GetVarint32(*pp, &iVal);
  *piPos += (iVal-2);
}

/*
** Helper function for fts3ExprIterate() (see below).
*/
static int fts3ExprIterate2(
  Fts3Expr *pExpr,                /* Expression to iterate phrases of */
  int *piPhrase,                  /* Pointer to phrase counter */
  int (*x)(Fts3Expr*,int,void*),  /* Callback function to invoke for phrases */
  void *pCtx                      /* Second argument to pass to callback */
){
  int rc;                         /* Return code */
  int eType = pExpr->eType;       /* Type of expression node pExpr */

  if( eType!=FTSQUERY_PHRASE ){
    assert( pExpr->pLeft && pExpr->pRight );
    rc = fts3ExprIterate2(pExpr->pLeft, piPhrase, x, pCtx);
    if( rc==SQLITE_OK && eType!=FTSQUERY_NOT ){
      rc = fts3ExprIterate2(pExpr->pRight, piPhrase, x, pCtx);
    }
  }else{
    rc = x(pExpr, *piPhrase, pCtx);
    (*piPhrase)++;
  }
  return rc;
}

/*
** Iterate through all phrase nodes in an FTS3 query, except those that
** are part of a sub-tree that is the right-hand-side of a NOT operator.
** For each phrase node found, the supplied callback function is invoked.
**
** If the callback function returns anything other than SQLITE_OK, 
** the iteration is abandoned and the error code returned immediately.
** Otherwise, SQLITE_OK is returned after a callback has been made for
** all eligible phrase nodes.
*/
static int fts3ExprIterate(
  Fts3Expr *pExpr,                /* Expression to iterate phrases of */
  int (*x)(Fts3Expr*,int,void*),  /* Callback function to invoke for phrases */
  void *pCtx                      /* Second argument to pass to callback */
){
  int iPhrase = 0;                /* Variable used as the phrase counter */
  return fts3ExprIterate2(pExpr, &iPhrase, x, pCtx);
}

/*
** This is an fts3ExprIterate() callback used while loading the doclists
** for each phrase into Fts3Expr.aDoclist[]/nDoclist. See also
** fts3ExprLoadDoclists().
*/
static int fts3ExprLoadDoclistsCb(Fts3Expr *pExpr, int iPhrase, void *ctx){
  int rc = SQLITE_OK;
  Fts3Phrase *pPhrase = pExpr->pPhrase;
  LoadDoclistCtx *p = (LoadDoclistCtx *)ctx;

  UNUSED_PARAMETER(iPhrase);

  p->nPhrase++;
  p->nToken += pPhrase->nToken;

  return rc;
}

/*
** Load the doclists for each phrase in the query associated with FTS3 cursor
** pCsr. 
**
** If pnPhrase is not NULL, then *pnPhrase is set to the number of matchable 
** phrases in the expression (all phrases except those directly or 
** indirectly descended from the right-hand-side of a NOT operator). If 
** pnToken is not NULL, then it is set to the number of tokens in all
** matchable phrases of the expression.
*/
static int fts3ExprLoadDoclists(
  Fts3Cursor *pCsr,               /* Fts3 cursor for current query */
  int *pnPhrase,                  /* OUT: Number of phrases in query */
  int *pnToken                    /* OUT: Number of tokens in query */
){
  int rc;                         /* Return Code */
  LoadDoclistCtx sCtx = {0,0,0};  /* Context for fts3ExprIterate() */
  sCtx.pCsr = pCsr;
  rc = fts3ExprIterate(pCsr->pExpr, fts3ExprLoadDoclistsCb, (void *)&sCtx);
  if( pnPhrase ) *pnPhrase = sCtx.nPhrase;
  if( pnToken ) *pnToken = sCtx.nToken;
  return rc;
}

static int fts3ExprPhraseCountCb(Fts3Expr *pExpr, int iPhrase, void *ctx){
  (*(int *)ctx)++;
  UNUSED_PARAMETER(pExpr);
  UNUSED_PARAMETER(iPhrase);
  return SQLITE_OK;
}
static int fts3ExprPhraseCount(Fts3Expr *pExpr){
  int nPhrase = 0;
  (void)fts3ExprIterate(pExpr, fts3ExprPhraseCountCb, (void *)&nPhrase);
  return nPhrase;
}

/*
** Advance the position list iterator specified by the first two 
** arguments so that it points to the first element with a value greater
** than or equal to parameter iNext.
*/
static void fts3SnippetAdvance(char **ppIter, int *piIter, int iNext){
  char *pIter = *ppIter;
  if( pIter ){
    int iIter = *piIter;

    while( iIter<iNext ){
      if( 0==(*pIter & 0xFE) ){
        iIter = -1;
        pIter = 0;
        break;
      }
      fts3GetDeltaPosition(&pIter, &iIter);
    }

    *piIter = iIter;
    *ppIter = pIter;
  }
}

/*
** Advance the snippet iterator to the next candidate snippet.
*/
static int fts3SnippetNextCandidate(SnippetIter *pIter){
  int i;                          /* Loop counter */

  if( pIter->iCurrent<0 ){
    /* The SnippetIter object has just been initialized. The first snippet
    ** candidate always starts at offset 0 (even if this candidate has a
    ** score of 0.0).
    */
    pIter->iCurrent = 0;

    /* Advance the 'head' iterator of each phrase to the first offset that
    ** is greater than or equal to (iNext+nSnippet).
    */
    for(i=0; i<pIter->nPhrase; i++){
      SnippetPhrase *pPhrase = &pIter->aPhrase[i];
      fts3SnippetAdvance(&pPhrase->pHead, &pPhrase->iHead, pIter->nSnippet);
    }
  }else{
    int iStart;
    int iEnd = 0x7FFFFFFF;

    for(i=0; i<pIter->nPhrase; i++){
      SnippetPhrase *pPhrase = &pIter->aPhrase[i];
      if( pPhrase->pHead && pPhrase->iHead<iEnd ){
        iEnd = pPhrase->iHead;
      }
    }
    if( iEnd==0x7FFFFFFF ){
      return 1;
    }

    pIter->iCurrent = iStart = iEnd - pIter->nSnippet + 1;
    for(i=0; i<pIter->nPhrase; i++){
      SnippetPhrase *pPhrase = &pIter->aPhrase[i];
      fts3SnippetAdvance(&pPhrase->pHead, &pPhrase->iHead, iEnd+1);
      fts3SnippetAdvance(&pPhrase->pTail, &pPhrase->iTail, iStart);
    }
  }

  return 0;
}

/*
** Retrieve information about the current candidate snippet of snippet 
** iterator pIter.
*/
static void fts3SnippetDetails(
  SnippetIter *pIter,             /* Snippet iterator */
  u64 mCovered,                   /* Bitmask of phrases already covered */
  int *piToken,                   /* OUT: First token of proposed snippet */
  int *piScore,                   /* OUT: "Score" for this snippet */
  u64 *pmCover,                   /* OUT: Bitmask of phrases covered */
  u64 *pmHighlight                /* OUT: Bitmask of terms to highlight */
){
  int iStart = pIter->iCurrent;   /* First token of snippet */
  int iScore = 0;                 /* Score of this snippet */
  int i;                          /* Loop counter */
  u64 mCover = 0;                 /* Mask of phrases covered by this snippet */
  u64 mHighlight = 0;             /* Mask of tokens to highlight in snippet */

  for(i=0; i<pIter->nPhrase; i++){
    SnippetPhrase *pPhrase = &pIter->aPhrase[i];
    if( pPhrase->pTail ){
      char *pCsr = pPhrase->pTail;
      int iCsr = pPhrase->iTail;

      while( iCsr<(iStart+pIter->nSnippet) ){
        int j;
        u64 mPhrase = (u64)1 << i;
        u64 mPos = (u64)1 << (iCsr - iStart);
        assert( iCsr>=iStart );
        if( (mCover|mCovered)&mPhrase ){
          iScore++;
        }else{
          iScore += 1000;
        }
        mCover |= mPhrase;

        for(j=0; j<pPhrase->nToken; j++){
          mHighlight |= (mPos>>j);
        }

        if( 0==(*pCsr & 0x0FE) ) break;
        fts3GetDeltaPosition(&pCsr, &iCsr);
      }
    }
  }

  /* Set the output variables before returning. */
  *piToken = iStart;
  *piScore = iScore;
  *pmCover = mCover;
  *pmHighlight = mHighlight;
}

/*
** This function is an fts3ExprIterate() callback used by fts3BestSnippet().
** Each invocation populates an element of the SnippetIter.aPhrase[] array.
*/
static int fts3SnippetFindPositions(Fts3Expr *pExpr, int iPhrase, void *ctx){
  SnippetIter *p = (SnippetIter *)ctx;
  SnippetPhrase *pPhrase = &p->aPhrase[iPhrase];
  char *pCsr;

  pPhrase->nToken = pExpr->pPhrase->nToken;

  pCsr = sqlite3Fts3EvalPhrasePoslist(p->pCsr, pExpr, p->iCol);
  if( pCsr ){
    int iFirst = 0;
    pPhrase->pList = pCsr;
    fts3GetDeltaPosition(&pCsr, &iFirst);
    assert( iFirst>=0 );
    pPhrase->pHead = pCsr;
    pPhrase->pTail = pCsr;
    pPhrase->iHead = iFirst;
    pPhrase->iTail = iFirst;
  }else{
    assert( pPhrase->pList==0 && pPhrase->pHead==0 && pPhrase->pTail==0 );
  }

  return SQLITE_OK;
}

/*
** Select the fragment of text consisting of nFragment contiguous tokens 
** from column iCol that represent the "best" snippet. The best snippet
** is the snippet with the highest score, where scores are calculated
** by adding:
**
**   (a) +1 point for each occurence of a matchable phrase in the snippet.
**
**   (b) +1000 points for the first occurence of each matchable phrase in 
**       the snippet for which the corresponding mCovered bit is not set.
**
** The selected snippet parameters are stored in structure *pFragment before
** returning. The score of the selected snippet is stored in *piScore
** before returning.
*/
static int fts3BestSnippet(
  int nSnippet,                   /* Desired snippet length */
  Fts3Cursor *pCsr,               /* Cursor to create snippet for */
  int iCol,                       /* Index of column to create snippet from */
  u64 mCovered,                   /* Mask of phrases already covered */
  u64 *pmSeen,                    /* IN/OUT: Mask of phrases seen */
  SnippetFragment *pFragment,     /* OUT: Best snippet found */
  int *piScore                    /* OUT: Score of snippet pFragment */
){
  int rc;                         /* Return Code */
  int nList;                      /* Number of phrases in expression */
  SnippetIter sIter;              /* Iterates through snippet candidates */
  int nByte;                      /* Number of bytes of space to allocate */
  int iBestScore = -1;            /* Best snippet score found so far */
  int i;                          /* Loop counter */

  memset(&sIter, 0, sizeof(sIter));

  /* Iterate through the phrases in the expression to count them. The same
  ** callback makes sure the doclists are loaded for each phrase.
  */
  rc = fts3ExprLoadDoclists(pCsr, &nList, 0);
  if( rc!=SQLITE_OK ){
    return rc;
  }

  /* Now that it is known how many phrases there are, allocate and zero
  ** the required space using malloc().
  */
  nByte = sizeof(SnippetPhrase) * nList;
  sIter.aPhrase = (SnippetPhrase *)sqlite3_malloc(nByte);
  if( !sIter.aPhrase ){
    return SQLITE_NOMEM;
  }
  memset(sIter.aPhrase, 0, nByte);

  /* Initialize the contents of the SnippetIter object. Then iterate through
  ** the set of phrases in the expression to populate the aPhrase[] array.
  */
  sIter.pCsr = pCsr;
  sIter.iCol = iCol;
  sIter.nSnippet = nSnippet;
  sIter.nPhrase = nList;
  sIter.iCurrent = -1;
  (void)fts3ExprIterate(pCsr->pExpr, fts3SnippetFindPositions, (void *)&sIter);

  /* Set the *pmSeen output variable. */
  for(i=0; i<nList; i++){
    if( sIter.aPhrase[i].pHead ){
      *pmSeen |= (u64)1 << i;
    }
  }

  /* Loop through all candidate snippets. Store the best snippet in 
  ** *pFragment. Store its associated 'score' in iBestScore.
  */
  pFragment->iCol = iCol;
  while( !fts3SnippetNextCandidate(&sIter) ){
    int iPos;
    int iScore;
    u64 mCover;
    u64 mHighlight;
    fts3SnippetDetails(&sIter, mCovered, &iPos, &iScore, &mCover, &mHighlight);
    assert( iScore>=0 );
    if( iScore>iBestScore ){
      pFragment->iPos = iPos;
      pFragment->hlmask = mHighlight;
      pFragment->covered = mCover;
      iBestScore = iScore;
    }
  }

  sqlite3_free(sIter.aPhrase);
  *piScore = iBestScore;
  return SQLITE_OK;
}


/*
** Append a string to the string-buffer passed as the first argument.
**
** If nAppend is negative, then the length of the string zAppend is
** determined using strlen().
*/
static int fts3StringAppend(
  StrBuffer *pStr,                /* Buffer to append to */
  const char *zAppend,            /* Pointer to data to append to buffer */
  int nAppend                     /* Size of zAppend in bytes (or -1) */
){
  if( nAppend<0 ){
    nAppend = (int)strlen(zAppend);
  }

  /* If there is insufficient space allocated at StrBuffer.z, use realloc()
  ** to grow the buffer until so that it is big enough to accomadate the
  ** appended data.
  */
  if( pStr->n+nAppend+1>=pStr->nAlloc ){
    int nAlloc = pStr->nAlloc+nAppend+100;
    char *zNew = sqlite3_realloc(pStr->z, nAlloc);
    if( !zNew ){
      return SQLITE_NOMEM;
    }
    pStr->z = zNew;
    pStr->nAlloc = nAlloc;
  }

  /* Append the data to the string buffer. */
  memcpy(&pStr->z[pStr->n], zAppend, nAppend);
  pStr->n += nAppend;
  pStr->z[pStr->n] = '\0';

  return SQLITE_OK;
}

/*
** The fts3BestSnippet() function often selects snippets that end with a
** query term. That is, the final term of the snippet is always a term
** that requires highlighting. For example, if 'X' is a highlighted term
** and '.' is a non-highlighted term, BestSnippet() may select:
**
**     ........X.....X
**
** This function "shifts" the beginning of the snippet forward in the 
** document so that there are approximately the same number of 
** non-highlighted terms to the right of the final highlighted term as there
** are to the left of the first highlighted term. For example, to this:
**
**     ....X.....X....
**
** This is done as part of extracting the snippet text, not when selecting
** the snippet. Snippet selection is done based on doclists only, so there
** is no way for fts3BestSnippet() to know whether or not the document 
** actually contains terms that follow the final highlighted term. 
*/
static int fts3SnippetShift(
  Fts3Table *pTab,                /* FTS3 table snippet comes from */
  int nSnippet,                   /* Number of tokens desired for snippet */
  const char *zDoc,               /* Document text to extract snippet from */
  int nDoc,                       /* Size of buffer zDoc in bytes */
  int *piPos,                     /* IN/OUT: First token of snippet */
  u64 *pHlmask                    /* IN/OUT: Mask of tokens to highlight */
){
  u64 hlmask = *pHlmask;          /* Local copy of initial highlight-mask */

  if( hlmask ){
    int nLeft;                    /* Tokens to the left of first highlight */
    int nRight;                   /* Tokens to the right of last highlight */
    int nDesired;                 /* Ideal number of tokens to shift forward */

    for(nLeft=0; !(hlmask & ((u64)1 << nLeft)); nLeft++);
    for(nRight=0; !(hlmask & ((u64)1 << (nSnippet-1-nRight))); nRight++);
    nDesired = (nLeft-nRight)/2;

    /* Ideally, the start of the snippet should be pushed forward in the
    ** document nDesired tokens. This block checks if there are actually
    ** nDesired tokens to the right of the snippet. If so, *piPos and
    ** *pHlMask are updated to shift the snippet nDesired tokens to the
    ** right. Otherwise, the snippet is shifted by the number of tokens
    ** available.
    */
    if( nDesired>0 ){
      int nShift;                 /* Number of tokens to shift snippet by */
      int iCurrent = 0;           /* Token counter */
      int rc;                     /* Return Code */
      sqlite3_tokenizer_module *pMod;
      sqlite3_tokenizer_cursor *pC;
      pMod = (sqlite3_tokenizer_module *)pTab->pTokenizer->pModule;

      /* Open a cursor on zDoc/nDoc. Check if there are (nSnippet+nDesired)
      ** or more tokens in zDoc/nDoc.
      */
      rc = pMod->xOpen(pTab->pTokenizer, zDoc, nDoc, &pC);
      if( rc!=SQLITE_OK ){
        return rc;
      }
      pC->pTokenizer = pTab->pTokenizer;
      while( rc==SQLITE_OK && iCurrent<(nSnippet+nDesired) ){
        const char *ZDUMMY; int DUMMY1, DUMMY2, DUMMY3;
        rc = pMod->xNext(pC, &ZDUMMY, &DUMMY1, &DUMMY2, &DUMMY3, &iCurrent);
      }
      pMod->xClose(pC);
      if( rc!=SQLITE_OK && rc!=SQLITE_DONE ){ return rc; }

      nShift = (rc==SQLITE_DONE)+iCurrent-nSnippet;
      assert( nShift<=nDesired );
      if( nShift>0 ){
        *piPos += nShift;
        *pHlmask = hlmask >> nShift;
      }
    }
  }
  return SQLITE_OK;
}

/*
** Extract the snippet text for fragment pFragment from cursor pCsr and
** append it to string buffer pOut.
*/
static int fts3SnippetText(
  Fts3Cursor *pCsr,               /* FTS3 Cursor */
  SnippetFragment *pFragment,     /* Snippet to extract */
  int iFragment,                  /* Fragment number */
  int isLast,                     /* True for final fragment in snippet */
  int nSnippet,                   /* Number of tokens in extracted snippet */
  const char *zOpen,              /* String inserted before highlighted term */
  const char *zClose,             /* String inserted after highlighted term */
  const char *zEllipsis,          /* String inserted between snippets */
  StrBuffer *pOut                 /* Write output here */
){
  Fts3Table *pTab = (Fts3Table *)pCsr->base.pVtab;
  int rc;                         /* Return code */
  const char *zDoc;               /* Document text to extract snippet from */
  int nDoc;                       /* Size of zDoc in bytes */
  int iCurrent = 0;               /* Current token number of document */
  int iEnd = 0;                   /* Byte offset of end of current token */
  int isShiftDone = 0;            /* True after snippet is shifted */
  int iPos = pFragment->iPos;     /* First token of snippet */
  u64 hlmask = pFragment->hlmask; /* Highlight-mask for snippet */
  int iCol = pFragment->iCol+1;   /* Query column to extract text from */
  sqlite3_tokenizer_module *pMod; /* Tokenizer module methods object */
  sqlite3_tokenizer_cursor *pC;   /* Tokenizer cursor open on zDoc/nDoc */
  const char *ZDUMMY;             /* Dummy argument used with tokenizer */
  int DUMMY1;                     /* Dummy argument used with tokenizer */
  
  zDoc = (const char *)sqlite3_column_text(pCsr->pStmt, iCol);
  if( zDoc==0 ){
    if( sqlite3_column_type(pCsr->pStmt, iCol)!=SQLITE_NULL ){
      return SQLITE_NOMEM;
    }
    return SQLITE_OK;
  }
  nDoc = sqlite3_column_bytes(pCsr->pStmt, iCol);

  /* Open a token cursor on the document. */
  pMod = (sqlite3_tokenizer_module *)pTab->pTokenizer->pModule;
  rc = pMod->xOpen(pTab->pTokenizer, zDoc, nDoc, &pC);
  if( rc!=SQLITE_OK ){
    return rc;
  }
  pC->pTokenizer = pTab->pTokenizer;

  while( rc==SQLITE_OK ){
    int iBegin;                   /* Offset in zDoc of start of token */
    int iFin;                     /* Offset in zDoc of end of token */
    int isHighlight;              /* True for highlighted terms */

    rc = pMod->xNext(pC, &ZDUMMY, &DUMMY1, &iBegin, &iFin, &iCurrent);
    if( rc!=SQLITE_OK ){
      if( rc==SQLITE_DONE ){
        /* Special case - the last token of the snippet is also the last token
        ** of the column. Append any punctuation that occurred between the end
        ** of the previous token and the end of the document to the output. 
        ** Then break out of the loop. */
        rc = fts3StringAppend(pOut, &zDoc[iEnd], -1);
      }
      break;
    }
    if( iCurrent<iPos ){ continue; }

    if( !isShiftDone ){
      int n = nDoc - iBegin;
      rc = fts3SnippetShift(pTab, nSnippet, &zDoc[iBegin], n, &iPos, &hlmask);
      isShiftDone = 1;

      /* Now that the shift has been done, check if the initial "..." are
      ** required. They are required if (a) this is not the first fragment,
      ** or (b) this fragment does not begin at position 0 of its column. 
      */
      if( rc==SQLITE_OK && (iPos>0 || iFragment>0) ){
        rc = fts3StringAppend(pOut, zEllipsis, -1);
      }
      if( rc!=SQLITE_OK || iCurrent<iPos ) continue;
    }

    if( iCurrent>=(iPos+nSnippet) ){
      if( isLast ){
        rc = fts3StringAppend(pOut, zEllipsis, -1);
      }
      break;
    }

    /* Set isHighlight to true if this term should be highlighted. */
    isHighlight = (hlmask & ((u64)1 << (iCurrent-iPos)))!=0;

    if( iCurrent>iPos ) rc = fts3StringAppend(pOut, &zDoc[iEnd], iBegin-iEnd);
    if( rc==SQLITE_OK && isHighlight ) rc = fts3StringAppend(pOut, zOpen, -1);
    if( rc==SQLITE_OK ) rc = fts3StringAppend(pOut, &zDoc[iBegin], iFin-iBegin);
    if( rc==SQLITE_OK && isHighlight ) rc = fts3StringAppend(pOut, zClose, -1);

    iEnd = iFin;
  }

  pMod->xClose(pC);
  return rc;
}


/*
** This function is used to count the entries in a column-list (a 
** delta-encoded list of term offsets within a single column of a single 
** row). When this function is called, *ppCollist should point to the
** beginning of the first varint in the column-list (the varint that
** contains the position of the first matching term in the column data).
** Before returning, *ppCollist is set to point to the first byte after
** the last varint in the column-list (either the 0x00 signifying the end
** of the position-list, or the 0x01 that precedes the column number of
** the next column in the position-list).
**
** The number of elements in the column-list is returned.
*/
static int fts3ColumnlistCount(char **ppCollist){
  char *pEnd = *ppCollist;
  char c = 0;
  int nEntry = 0;

  /* A column-list is terminated by either a 0x01 or 0x00. */
  while( 0xFE & (*pEnd | c) ){
    c = *pEnd++ & 0x80;
    if( !c ) nEntry++;
  }

  *ppCollist = pEnd;
  return nEntry;
}

/*
** fts3ExprIterate() callback used to collect the "global" matchinfo stats
** for a single query. 
**
** fts3ExprIterate() callback to load the 'global' elements of a
** FTS3_MATCHINFO_HITS matchinfo array. The global stats are those elements 
** of the matchinfo array that are constant for all rows returned by the 
** current query.
**
** Argument pCtx is actually a pointer to a struct of type MatchInfo. This
** function populates Matchinfo.aMatchinfo[] as follows:
**
**   for(iCol=0; iCol<nCol; iCol++){
**     aMatchinfo[3*iPhrase*nCol + 3*iCol + 1] = X;
**     aMatchinfo[3*iPhrase*nCol + 3*iCol + 2] = Y;
**   }
**
** where X is the number of matches for phrase iPhrase is column iCol of all
** rows of the table. Y is the number of rows for which column iCol contains
** at least one instance of phrase iPhrase.
**
** If the phrase pExpr consists entirely of deferred tokens, then all X and
** Y values are set to nDoc, where nDoc is the number of documents in the 
** file system. This is done because the full-text index doclist is required
** to calculate these values properly, and the full-text index doclist is
** not available for deferred tokens.
*/
static int fts3ExprGlobalHitsCb(
  Fts3Expr *pExpr,                /* Phrase expression node */
  int iPhrase,                    /* Phrase number (numbered from zero) */
  void *pCtx                      /* Pointer to MatchInfo structure */
){
  MatchInfo *p = (MatchInfo *)pCtx;
  return sqlite3Fts3EvalPhraseStats(
      p->pCursor, pExpr, &p->aMatchinfo[3*iPhrase*p->nCol]
  );
}

/*
** fts3ExprIterate() callback used to collect the "local" part of the
** FTS3_MATCHINFO_HITS array. The local stats are those elements of the 
** array that are different for each row returned by the query.
*/
static int fts3ExprLocalHitsCb(
  Fts3Expr *pExpr,                /* Phrase expression node */
  int iPhrase,                    /* Phrase number */
  void *pCtx                      /* Pointer to MatchInfo structure */
){
  MatchInfo *p = (MatchInfo *)pCtx;
  int iStart = iPhrase * p->nCol * 3;
  int i;

  for(i=0; i<p->nCol; i++){
    char *pCsr;
    pCsr = sqlite3Fts3EvalPhrasePoslist(p->pCursor, pExpr, i);
    if( pCsr ){
      p->aMatchinfo[iStart+i*3] = fts3ColumnlistCount(&pCsr);
    }else{
      p->aMatchinfo[iStart+i*3] = 0;
    }
  }

  return SQLITE_OK;
}

static int fts3MatchinfoCheck(
  Fts3Table *pTab, 
  char cArg,
  char **pzErr
){
  if( (cArg==FTS3_MATCHINFO_NPHRASE)
   || (cArg==FTS3_MATCHINFO_NCOL)
   || (cArg==FTS3_MATCHINFO_NDOC && pTab->bHasStat)
   || (cArg==FTS3_MATCHINFO_AVGLENGTH && pTab->bHasStat)
   || (cArg==FTS3_MATCHINFO_LENGTH && pTab->bHasDocsize)
   || (cArg==FTS3_MATCHINFO_LCS)
   || (cArg==FTS3_MATCHINFO_HITS)
  ){
    return SQLITE_OK;
  }
  *pzErr = sqlite3_mprintf("unrecognized matchinfo request: %c", cArg);
  return SQLITE_ERROR;
}

static int fts3MatchinfoSize(MatchInfo *pInfo, char cArg){
  int nVal;                       /* Number of integers output by cArg */

  switch( cArg ){
    case FTS3_MATCHINFO_NDOC:
    case FTS3_MATCHINFO_NPHRASE: 
    case FTS3_MATCHINFO_NCOL: 
      nVal = 1;
      break;

    case FTS3_MATCHINFO_AVGLENGTH:
    case FTS3_MATCHINFO_LENGTH:
    case FTS3_MATCHINFO_LCS:
      nVal = pInfo->nCol;
      break;

    default:
      assert( cArg==FTS3_MATCHINFO_HITS );
      nVal = pInfo->nCol * pInfo->nPhrase * 3;
      break;
  }

  return nVal;
}

static int fts3MatchinfoSelectDoctotal(
  Fts3Table *pTab,
  sqlite3_stmt **ppStmt,
  sqlite3_int64 *pnDoc,
  const char **paLen
){
  sqlite3_stmt *pStmt;
  const char *a;
  sqlite3_int64 nDoc;

  if( !*ppStmt ){
    int rc = sqlite3Fts3SelectDoctotal(pTab, ppStmt);
    if( rc!=SQLITE_OK ) return rc;
  }
  pStmt = *ppStmt;
  assert( sqlite3_data_count(pStmt)==1 );

  a = sqlite3_column_blob(pStmt, 0);
  a += sqlite3Fts3GetVarint(a, &nDoc);
  if( nDoc==0 ) return FTS_CORRUPT_VTAB;
  *pnDoc = (u32)nDoc;

  if( paLen ) *paLen = a;
  return SQLITE_OK;
}

/*
** An instance of the following structure is used to store state while 
** iterating through a multi-column position-list corresponding to the
** hits for a single phrase on a single row in order to calculate the
** values for a matchinfo() FTS3_MATCHINFO_LCS request.
*/
typedef struct LcsIterator LcsIterator;
struct LcsIterator {
  Fts3Expr *pExpr;                /* Pointer to phrase expression */
  int iPosOffset;                 /* Tokens count up to end of this phrase */
  char *pRead;                    /* Cursor used to iterate through aDoclist */
  int iPos;                       /* Current position */
};

/* 
** If LcsIterator.iCol is set to the following value, the iterator has
** finished iterating through all offsets for all columns.
*/
#define LCS_ITERATOR_FINISHED 0x7FFFFFFF;

static int fts3MatchinfoLcsCb(
  Fts3Expr *pExpr,                /* Phrase expression node */
  int iPhrase,                    /* Phrase number (numbered from zero) */
  void *pCtx                      /* Pointer to MatchInfo structure */
){
  LcsIterator *aIter = (LcsIterator *)pCtx;
  aIter[iPhrase].pExpr = pExpr;
  return SQLITE_OK;
}

/*
** Advance the iterator passed as an argument to the next position. Return
** 1 if the iterator is at EOF or if it now points to the start of the
** position list for the next column.
*/
static int fts3LcsIteratorAdvance(LcsIterator *pIter){
  char *pRead = pIter->pRead;
  sqlite3_int64 iRead;
  int rc = 0;

  pRead += sqlite3Fts3GetVarint(pRead, &iRead);
  if( iRead==0 || iRead==1 ){
    pRead = 0;
    rc = 1;
  }else{
    pIter->iPos += (int)(iRead-2);
  }

  pIter->pRead = pRead;
  return rc;
}
  
/*
** This function implements the FTS3_MATCHINFO_LCS matchinfo() flag. 
**
** If the call is successful, the longest-common-substring lengths for each
** column are written into the first nCol elements of the pInfo->aMatchinfo[] 
** array before returning. SQLITE_OK is returned in this case.
**
** Otherwise, if an error occurs, an SQLite error code is returned and the
** data written to the first nCol elements of pInfo->aMatchinfo[] is 
** undefined.
*/
static int fts3MatchinfoLcs(Fts3Cursor *pCsr, MatchInfo *pInfo){
  LcsIterator *aIter;
  int i;
  int iCol;
  int nToken = 0;

  /* Allocate and populate the array of LcsIterator objects. The array
  ** contains one element for each matchable phrase in the query.
  **/
  aIter = sqlite3_malloc(sizeof(LcsIterator) * pCsr->nPhrase);
  if( !aIter ) return SQLITE_NOMEM;
  memset(aIter, 0, sizeof(LcsIterator) * pCsr->nPhrase);
  (void)fts3ExprIterate(pCsr->pExpr, fts3MatchinfoLcsCb, (void*)aIter);

  for(i=0; i<pInfo->nPhrase; i++){
    LcsIterator *pIter = &aIter[i];
    nToken -= pIter->pExpr->pPhrase->nToken;
    pIter->iPosOffset = nToken;
  }

  for(iCol=0; iCol<pInfo->nCol; iCol++){
    int nLcs = 0;                 /* LCS value for this column */
    int nLive = 0;                /* Number of iterators in aIter not at EOF */

    for(i=0; i<pInfo->nPhrase; i++){
      LcsIterator *pIt = &aIter[i];
      pIt->pRead = sqlite3Fts3EvalPhrasePoslist(pCsr, pIt->pExpr, iCol);
      if( pIt->pRead ){
        pIt->iPos = pIt->iPosOffset;
        fts3LcsIteratorAdvance(&aIter[i]);
        nLive++;
      }
    }

    while( nLive>0 ){
      LcsIterator *pAdv = 0;      /* The iterator to advance by one position */
      int nThisLcs = 0;           /* LCS for the current iterator positions */

      for(i=0; i<pInfo->nPhrase; i++){
        LcsIterator *pIter = &aIter[i];
        if( pIter->pRead==0 ){
          /* This iterator is already at EOF for this column. */
          nThisLcs = 0;
        }else{
          if( pAdv==0 || pIter->iPos<pAdv->iPos ){
            pAdv = pIter;
          }
          if( nThisLcs==0 || pIter->iPos==pIter[-1].iPos ){
            nThisLcs++;
          }else{
            nThisLcs = 1;
          }
          if( nThisLcs>nLcs ) nLcs = nThisLcs;
        }
      }
      if( fts3LcsIteratorAdvance(pAdv) ) nLive--;
    }

    pInfo->aMatchinfo[iCol] = nLcs;
  }

  sqlite3_free(aIter);
  return SQLITE_OK;
}

/*
** Populate the buffer pInfo->aMatchinfo[] with an array of integers to
** be returned by the matchinfo() function. Argument zArg contains the 
** format string passed as the second argument to matchinfo (or the
** default value "pcx" if no second argument was specified). The format
** string has already been validated and the pInfo->aMatchinfo[] array
** is guaranteed to be large enough for the output.
**
** If bGlobal is true, then populate all fields of the matchinfo() output.
** If it is false, then assume that those fields that do not change between
** rows (i.e. FTS3_MATCHINFO_NPHRASE, NCOL, NDOC, AVGLENGTH and part of HITS)
** have already been populated.
**
** Return SQLITE_OK if successful, or an SQLite error code if an error 
** occurs. If a value other than SQLITE_OK is returned, the state the
** pInfo->aMatchinfo[] buffer is left in is undefined.
*/
static int fts3MatchinfoValues(
  Fts3Cursor *pCsr,               /* FTS3 cursor object */
  int bGlobal,                    /* True to grab the global stats */
  MatchInfo *pInfo,               /* Matchinfo context object */
  const char *zArg                /* Matchinfo format string */
){
  int rc = SQLITE_OK;
  int i;
  Fts3Table *pTab = (Fts3Table *)pCsr->base.pVtab;
  sqlite3_stmt *pSelect = 0;

  for(i=0; rc==SQLITE_OK && zArg[i]; i++){

    switch( zArg[i] ){
      case FTS3_MATCHINFO_NPHRASE:
        if( bGlobal ) pInfo->aMatchinfo[0] = pInfo->nPhrase;
        break;

      case FTS3_MATCHINFO_NCOL:
        if( bGlobal ) pInfo->aMatchinfo[0] = pInfo->nCol;
        break;
        
      case FTS3_MATCHINFO_NDOC:
        if( bGlobal ){
          sqlite3_int64 nDoc = 0;
          rc = fts3MatchinfoSelectDoctotal(pTab, &pSelect, &nDoc, 0);
          pInfo->aMatchinfo[0] = (u32)nDoc;
        }
        break;

      case FTS3_MATCHINFO_AVGLENGTH: 
        if( bGlobal ){
          sqlite3_int64 nDoc;     /* Number of rows in table */
          const char *a;          /* Aggregate column length array */

          rc = fts3MatchinfoSelectDoctotal(pTab, &pSelect, &nDoc, &a);
          if( rc==SQLITE_OK ){
            int iCol;
            for(iCol=0; iCol<pInfo->nCol; iCol++){
              u32 iVal;
              sqlite3_int64 nToken;
              a += sqlite3Fts3GetVarint(a, &nToken);
              iVal = (u32)(((u32)(nToken&0xffffffff)+nDoc/2)/nDoc);
              pInfo->aMatchinfo[iCol] = iVal;
            }
          }
        }
        break;

      case FTS3_MATCHINFO_LENGTH: {
        sqlite3_stmt *pSelectDocsize = 0;
        rc = sqlite3Fts3SelectDocsize(pTab, pCsr->iPrevId, &pSelectDocsize);
        if( rc==SQLITE_OK ){
          int iCol;
          const char *a = sqlite3_column_blob(pSelectDocsize, 0);
          for(iCol=0; iCol<pInfo->nCol; iCol++){
            sqlite3_int64 nToken;
            a += sqlite3Fts3GetVarint(a, &nToken);
            pInfo->aMatchinfo[iCol] = (u32)nToken;
          }
        }
        sqlite3_reset(pSelectDocsize);
        break;
      }

      case FTS3_MATCHINFO_LCS:
        rc = fts3ExprLoadDoclists(pCsr, 0, 0);
        if( rc==SQLITE_OK ){
          rc = fts3MatchinfoLcs(pCsr, pInfo);
        }
        break;

      default: {
        Fts3Expr *pExpr;
        assert( zArg[i]==FTS3_MATCHINFO_HITS );
        pExpr = pCsr->pExpr;
        rc = fts3ExprLoadDoclists(pCsr, 0, 0);
        if( rc!=SQLITE_OK ) break;
        if( bGlobal ){
          if( pCsr->pDeferred ){
            rc = fts3MatchinfoSelectDoctotal(pTab, &pSelect, &pInfo->nDoc, 0);
            if( rc!=SQLITE_OK ) break;
          }
          rc = fts3ExprIterate(pExpr, fts3ExprGlobalHitsCb,(void*)pInfo);
          if( rc!=SQLITE_OK ) break;
        }
        (void)fts3ExprIterate(pExpr, fts3ExprLocalHitsCb,(void*)pInfo);
        break;
      }
    }

    pInfo->aMatchinfo += fts3MatchinfoSize(pInfo, zArg[i]);
  }

  sqlite3_reset(pSelect);
  return rc;
}


/*
** Populate pCsr->aMatchinfo[] with data for the current row. The 
** 'matchinfo' data is an array of 32-bit unsigned integers (C type u32).
*/
static int fts3GetMatchinfo(
  Fts3Cursor *pCsr,               /* FTS3 Cursor object */
  const char *zArg                /* Second argument to matchinfo() function */
){
  MatchInfo sInfo;
  Fts3Table *pTab = (Fts3Table *)pCsr->base.pVtab;
  int rc = SQLITE_OK;
  int bGlobal = 0;                /* Collect 'global' stats as well as local */

  memset(&sInfo, 0, sizeof(MatchInfo));
  sInfo.pCursor = pCsr;
  sInfo.nCol = pTab->nColumn;

  /* If there is cached matchinfo() data, but the format string for the 
  ** cache does not match the format string for this request, discard 
  ** the cached data. */
  if( pCsr->zMatchinfo && strcmp(pCsr->zMatchinfo, zArg) ){
    assert( pCsr->aMatchinfo );
    sqlite3_free(pCsr->aMatchinfo);
    pCsr->zMatchinfo = 0;
    pCsr->aMatchinfo = 0;
  }

  /* If Fts3Cursor.aMatchinfo[] is NULL, then this is the first time the
  ** matchinfo function has been called for this query. In this case 
  ** allocate the array used to accumulate the matchinfo data and
  ** initialize those elements that are constant for every row.
  */
  if( pCsr->aMatchinfo==0 ){
    int nMatchinfo = 0;           /* Number of u32 elements in match-info */
    int nArg;                     /* Bytes in zArg */
    int i;                        /* Used to iterate through zArg */

    /* Determine the number of phrases in the query */
    pCsr->nPhrase = fts3ExprPhraseCount(pCsr->pExpr);
    sInfo.nPhrase = pCsr->nPhrase;

    /* Determine the number of integers in the buffer returned by this call. */
    for(i=0; zArg[i]; i++){
      nMatchinfo += fts3MatchinfoSize(&sInfo, zArg[i]);
    }

    /* Allocate space for Fts3Cursor.aMatchinfo[] and Fts3Cursor.zMatchinfo. */
    nArg = (int)strlen(zArg);
    pCsr->aMatchinfo = (u32 *)sqlite3_malloc(sizeof(u32)*nMatchinfo + nArg + 1);
    if( !pCsr->aMatchinfo ) return SQLITE_NOMEM;

    pCsr->zMatchinfo = (char *)&pCsr->aMatchinfo[nMatchinfo];
    pCsr->nMatchinfo = nMatchinfo;
    memcpy(pCsr->zMatchinfo, zArg, nArg+1);
    memset(pCsr->aMatchinfo, 0, sizeof(u32)*nMatchinfo);
    pCsr->isMatchinfoNeeded = 1;
    bGlobal = 1;
  }

  sInfo.aMatchinfo = pCsr->aMatchinfo;
  sInfo.nPhrase = pCsr->nPhrase;
  if( pCsr->isMatchinfoNeeded ){
    rc = fts3MatchinfoValues(pCsr, bGlobal, &sInfo, zArg);
    pCsr->isMatchinfoNeeded = 0;
  }

  return rc;
}

/*
** Implementation of snippet() function.
*/
SQLITE_PRIVATE void sqlite3Fts3Snippet(
  sqlite3_context *pCtx,          /* SQLite function call context */
  Fts3Cursor *pCsr,               /* Cursor object */
  const char *zStart,             /* Snippet start text - "<b>" */
  const char *zEnd,               /* Snippet end text - "</b>" */
  const char *zEllipsis,          /* Snippet ellipsis text - "<b>...</b>" */
  int iCol,                       /* Extract snippet from this column */
  int nToken                      /* Approximate number of tokens in snippet */
){
  Fts3Table *pTab = (Fts3Table *)pCsr->base.pVtab;
  int rc = SQLITE_OK;
  int i;
  StrBuffer res = {0, 0, 0};

  /* The returned text includes up to four fragments of text extracted from
  ** the data in the current row. The first iteration of the for(...) loop
  ** below attempts to locate a single fragment of text nToken tokens in 
  ** size that contains at least one instance of all phrases in the query
  ** expression that appear in the current row. If such a fragment of text
  ** cannot be found, the second iteration of the loop attempts to locate
  ** a pair of fragments, and so on.
  */
  int nSnippet = 0;               /* Number of fragments in this snippet */
  SnippetFragment aSnippet[4];    /* Maximum of 4 fragments per snippet */
  int nFToken = -1;               /* Number of tokens in each fragment */

  if( !pCsr->pExpr ){
    sqlite3_result_text(pCtx, "", 0, SQLITE_STATIC);
    return;
  }

  for(nSnippet=1; 1; nSnippet++){

    int iSnip;                    /* Loop counter 0..nSnippet-1 */
    u64 mCovered = 0;             /* Bitmask of phrases covered by snippet */
    u64 mSeen = 0;                /* Bitmask of phrases seen by BestSnippet() */

    if( nToken>=0 ){
      nFToken = (nToken+nSnippet-1) / nSnippet;
    }else{
      nFToken = -1 * nToken;
    }

    for(iSnip=0; iSnip<nSnippet; iSnip++){
      int iBestScore = -1;        /* Best score of columns checked so far */
      int iRead;                  /* Used to iterate through columns */
      SnippetFragment *pFragment = &aSnippet[iSnip];

      memset(pFragment, 0, sizeof(*pFragment));

      /* Loop through all columns of the table being considered for snippets.
      ** If the iCol argument to this function was negative, this means all
      ** columns of the FTS3 table. Otherwise, only column iCol is considered.
      */
      for(iRead=0; iRead<pTab->nColumn; iRead++){
        SnippetFragment sF = {0, 0, 0, 0};
        int iS;
        if( iCol>=0 && iRead!=iCol ) continue;

        /* Find the best snippet of nFToken tokens in column iRead. */
        rc = fts3BestSnippet(nFToken, pCsr, iRead, mCovered, &mSeen, &sF, &iS);
        if( rc!=SQLITE_OK ){
          goto snippet_out;
        }
        if( iS>iBestScore ){
          *pFragment = sF;
          iBestScore = iS;
        }
      }

      mCovered |= pFragment->covered;
    }

    /* If all query phrases seen by fts3BestSnippet() are present in at least
    ** one of the nSnippet snippet fragments, break out of the loop.
    */
    assert( (mCovered&mSeen)==mCovered );
    if( mSeen==mCovered || nSnippet==SizeofArray(aSnippet) ) break;
  }

  assert( nFToken>0 );

  for(i=0; i<nSnippet && rc==SQLITE_OK; i++){
    rc = fts3SnippetText(pCsr, &aSnippet[i], 
        i, (i==nSnippet-1), nFToken, zStart, zEnd, zEllipsis, &res
    );
  }

 snippet_out:
  sqlite3Fts3SegmentsClose(pTab);
  if( rc!=SQLITE_OK ){
    sqlite3_result_error_code(pCtx, rc);
    sqlite3_free(res.z);
  }else{
    sqlite3_result_text(pCtx, res.z, -1, sqlite3_free);
  }
}


typedef struct TermOffset TermOffset;
typedef struct TermOffsetCtx TermOffsetCtx;

struct TermOffset {
  char *pList;                    /* Position-list */
  int iPos;                       /* Position just read from pList */
  int iOff;                       /* Offset of this term from read positions */
};

struct TermOffsetCtx {
  Fts3Cursor *pCsr;
  int iCol;                       /* Column of table to populate aTerm for */
  int iTerm;
  sqlite3_int64 iDocid;
  TermOffset *aTerm;
};

/*
** This function is an fts3ExprIterate() callback used by sqlite3Fts3Offsets().
*/
static int fts3ExprTermOffsetInit(Fts3Expr *pExpr, int iPhrase, void *ctx){
  TermOffsetCtx *p = (TermOffsetCtx *)ctx;
  int nTerm;                      /* Number of tokens in phrase */
  int iTerm;                      /* For looping through nTerm phrase terms */
  char *pList;                    /* Pointer to position list for phrase */
  int iPos = 0;                   /* First position in position-list */

  UNUSED_PARAMETER(iPhrase);
  pList = sqlite3Fts3EvalPhrasePoslist(p->pCsr, pExpr, p->iCol);
  nTerm = pExpr->pPhrase->nToken;
  if( pList ){
    fts3GetDeltaPosition(&pList, &iPos);
    assert( iPos>=0 );
  }

  for(iTerm=0; iTerm<nTerm; iTerm++){
    TermOffset *pT = &p->aTerm[p->iTerm++];
    pT->iOff = nTerm-iTerm-1;
    pT->pList = pList;
    pT->iPos = iPos;
  }

  return SQLITE_OK;
}

/*
** Implementation of offsets() function.
*/
SQLITE_PRIVATE void sqlite3Fts3Offsets(
  sqlite3_context *pCtx,          /* SQLite function call context */
  Fts3Cursor *pCsr                /* Cursor object */
){
  Fts3Table *pTab = (Fts3Table *)pCsr->base.pVtab;
  sqlite3_tokenizer_module const *pMod = pTab->pTokenizer->pModule;
  const char *ZDUMMY;             /* Dummy argument used with xNext() */
  int NDUMMY;                     /* Dummy argument used with xNext() */
  int rc;                         /* Return Code */
  int nToken;                     /* Number of tokens in query */
  int iCol;                       /* Column currently being processed */
  StrBuffer res = {0, 0, 0};      /* Result string */
  TermOffsetCtx sCtx;             /* Context for fts3ExprTermOffsetInit() */

  if( !pCsr->pExpr ){
    sqlite3_result_text(pCtx, "", 0, SQLITE_STATIC);
    return;
  }

  memset(&sCtx, 0, sizeof(sCtx));
  assert( pCsr->isRequireSeek==0 );

  /* Count the number of terms in the query */
  rc = fts3ExprLoadDoclists(pCsr, 0, &nToken);
  if( rc!=SQLITE_OK ) goto offsets_out;

  /* Allocate the array of TermOffset iterators. */
  sCtx.aTerm = (TermOffset *)sqlite3_malloc(sizeof(TermOffset)*nToken);
  if( 0==sCtx.aTerm ){
    rc = SQLITE_NOMEM;
    goto offsets_out;
  }
  sCtx.iDocid = pCsr->iPrevId;
  sCtx.pCsr = pCsr;

  /* Loop through the table columns, appending offset information to 
  ** string-buffer res for each column.
  */
  for(iCol=0; iCol<pTab->nColumn; iCol++){
    sqlite3_tokenizer_cursor *pC; /* Tokenizer cursor */
    int iStart;
    int iEnd;
    int iCurrent;
    const char *zDoc;
    int nDoc;

    /* Initialize the contents of sCtx.aTerm[] for column iCol. There is 
    ** no way that this operation can fail, so the return code from
    ** fts3ExprIterate() can be discarded.
    */
    sCtx.iCol = iCol;
    sCtx.iTerm = 0;
    (void)fts3ExprIterate(pCsr->pExpr, fts3ExprTermOffsetInit, (void *)&sCtx);

    /* Retreive the text stored in column iCol. If an SQL NULL is stored 
    ** in column iCol, jump immediately to the next iteration of the loop.
    ** If an OOM occurs while retrieving the data (this can happen if SQLite
    ** needs to transform the data from utf-16 to utf-8), return SQLITE_NOMEM 
    ** to the caller. 
    */
    zDoc = (const char *)sqlite3_column_text(pCsr->pStmt, iCol+1);
    nDoc = sqlite3_column_bytes(pCsr->pStmt, iCol+1);
    if( zDoc==0 ){
      if( sqlite3_column_type(pCsr->pStmt, iCol+1)==SQLITE_NULL ){
        continue;
      }
      rc = SQLITE_NOMEM;
      goto offsets_out;
    }

    /* Initialize a tokenizer iterator to iterate through column iCol. */
    rc = pMod->xOpen(pTab->pTokenizer, zDoc, nDoc, &pC);
    if( rc!=SQLITE_OK ) goto offsets_out;
    pC->pTokenizer = pTab->pTokenizer;

    rc = pMod->xNext(pC, &ZDUMMY, &NDUMMY, &iStart, &iEnd, &iCurrent);
    while( rc==SQLITE_OK ){
      int i;                      /* Used to loop through terms */
      int iMinPos = 0x7FFFFFFF;   /* Position of next token */
      TermOffset *pTerm = 0;      /* TermOffset associated with next token */

      for(i=0; i<nToken; i++){
        TermOffset *pT = &sCtx.aTerm[i];
        if( pT->pList && (pT->iPos-pT->iOff)<iMinPos ){
          iMinPos = pT->iPos-pT->iOff;
          pTerm = pT;
        }
      }

      if( !pTerm ){
        /* All offsets for this column have been gathered. */
        rc = SQLITE_DONE;
      }else{
        assert( iCurrent<=iMinPos );
        if( 0==(0xFE&*pTerm->pList) ){
          pTerm->pList = 0;
        }else{
          fts3GetDeltaPosition(&pTerm->pList, &pTerm->iPos);
        }
        while( rc==SQLITE_OK && iCurrent<iMinPos ){
          rc = pMod->xNext(pC, &ZDUMMY, &NDUMMY, &iStart, &iEnd, &iCurrent);
        }
        if( rc==SQLITE_OK ){
          char aBuffer[64];
          sqlite3_snprintf(sizeof(aBuffer), aBuffer, 
              "%d %d %d %d ", iCol, pTerm-sCtx.aTerm, iStart, iEnd-iStart
          );
          rc = fts3StringAppend(&res, aBuffer, -1);
        }else if( rc==SQLITE_DONE && pTab->zContentTbl==0 ){
          rc = FTS_CORRUPT_VTAB;
        }
      }
    }
    if( rc==SQLITE_DONE ){
      rc = SQLITE_OK;
    }

    pMod->xClose(pC);
    if( rc!=SQLITE_OK ) goto offsets_out;
  }

 offsets_out:
  sqlite3_free(sCtx.aTerm);
  assert( rc!=SQLITE_DONE );
  sqlite3Fts3SegmentsClose(pTab);
  if( rc!=SQLITE_OK ){
    sqlite3_result_error_code(pCtx,  rc);
    sqlite3_free(res.z);
  }else{
    sqlite3_result_text(pCtx, res.z, res.n-1, sqlite3_free);
  }
  return;
}

/*
** Implementation of matchinfo() function.
*/
SQLITE_PRIVATE void sqlite3Fts3Matchinfo(
  sqlite3_context *pContext,      /* Function call context */
  Fts3Cursor *pCsr,               /* FTS3 table cursor */
  const char *zArg                /* Second arg to matchinfo() function */
){
  Fts3Table *pTab = (Fts3Table *)pCsr->base.pVtab;
  int rc;
  int i;
  const char *zFormat;

  if( zArg ){
    for(i=0; zArg[i]; i++){
      char *zErr = 0;
      if( fts3MatchinfoCheck(pTab, zArg[i], &zErr) ){
        sqlite3_result_error(pContext, zErr, -1);
        sqlite3_free(zErr);
        return;
      }
    }
    zFormat = zArg;
  }else{
    zFormat = FTS3_MATCHINFO_DEFAULT;
  }

  if( !pCsr->pExpr ){
    sqlite3_result_blob(pContext, "", 0, SQLITE_STATIC);
    return;
  }

  /* Retrieve matchinfo() data. */
  rc = fts3GetMatchinfo(pCsr, zFormat);
  sqlite3Fts3SegmentsClose(pTab);

  if( rc!=SQLITE_OK ){
    sqlite3_result_error_code(pContext, rc);
  }else{
    int n = pCsr->nMatchinfo * sizeof(u32);
    sqlite3_result_blob(pContext, pCsr->aMatchinfo, n, SQLITE_TRANSIENT);
  }
}

#endif

/************** End of fts3_snippet.c ****************************************/
/************** Begin file rtree.c *******************************************/
/*
** 2001 September 15
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This file contains code for implementations of the r-tree and r*-tree
** algorithms packaged as an SQLite virtual table module.
*/

/*
** Database Format of R-Tree Tables
** --------------------------------
**
** The data structure for a single virtual r-tree table is stored in three 
** native SQLite tables declared as follows. In each case, the '%' character
** in the table name is replaced with the user-supplied name of the r-tree
** table.
**
**   CREATE TABLE %_node(nodeno INTEGER PRIMARY KEY, data BLOB)
**   CREATE TABLE %_parent(nodeno INTEGER PRIMARY KEY, parentnode INTEGER)
**   CREATE TABLE %_rowid(rowid INTEGER PRIMARY KEY, nodeno INTEGER)
**
** The data for each node of the r-tree structure is stored in the %_node
** table. For each node that is not the root node of the r-tree, there is
** an entry in the %_parent table associating the node with its parent.
** And for each row of data in the table, there is an entry in the %_rowid
** table that maps from the entries rowid to the id of the node that it
** is stored on.
**
** The root node of an r-tree always exists, even if the r-tree table is
** empty. The nodeno of the root node is always 1. All other nodes in the
** table must be the same size as the root node. The content of each node
** is formatted as follows:
**
**   1. If the node is the root node (node 1), then the first 2 bytes
**      of the node contain the tree depth as a big-endian integer.
**      For non-root nodes, the first 2 bytes are left unused.
**
**   2. The next 2 bytes contain the number of entries currently 
**      stored in the node.
**
**   3. The remainder of the node contains the node entries. Each entry
**      consists of a single 8-byte integer followed by an even number
**      of 4-byte coordinates. For leaf nodes the integer is the rowid
**      of a record. For internal nodes it is the node number of a
**      child page.
*/

#if !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_RTREE)

/*
** This file contains an implementation of a couple of different variants
** of the r-tree algorithm. See the README file for further details. The 
** same data-structure is used for all, but the algorithms for insert and
** delete operations vary. The variants used are selected at compile time 
** by defining the following symbols:
*/

/* Either, both or none of the following may be set to activate 
** r*tree variant algorithms.
*/
#define VARIANT_RSTARTREE_CHOOSESUBTREE 0
#define VARIANT_RSTARTREE_REINSERT      1

/* 
** Exactly one of the following must be set to 1.
*/
#define VARIANT_GUTTMAN_QUADRATIC_SPLIT 0
#define VARIANT_GUTTMAN_LINEAR_SPLIT    0
#define VARIANT_RSTARTREE_SPLIT         1

#define VARIANT_GUTTMAN_SPLIT \
        (VARIANT_GUTTMAN_LINEAR_SPLIT||VARIANT_GUTTMAN_QUADRATIC_SPLIT)

#if VARIANT_GUTTMAN_QUADRATIC_SPLIT
  #define PickNext QuadraticPickNext
  #define PickSeeds QuadraticPickSeeds
  #define AssignCells splitNodeGuttman
#endif
#if VARIANT_GUTTMAN_LINEAR_SPLIT
  #define PickNext LinearPickNext
  #define PickSeeds LinearPickSeeds
  #define AssignCells splitNodeGuttman
#endif
#if VARIANT_RSTARTREE_SPLIT
  #define AssignCells splitNodeStartree
#endif

#if !defined(NDEBUG) && !defined(SQLITE_DEBUG) 
# define NDEBUG 1
#endif

#ifndef SQLITE_CORE
  SQLITE_EXTENSION_INIT1
#else
#endif

/* #include <string.h> */
/* #include <assert.h> */

#ifndef SQLITE_AMALGAMATION
#include "sqlite3rtree.h"
typedef sqlite3_int64 i64;
typedef unsigned char u8;
typedef unsigned int u32;
#endif

/*  The following macro is used to suppress compiler warnings.
*/
#ifndef UNUSED_PARAMETER
# define UNUSED_PARAMETER(x) (void)(x)
#endif

typedef struct Rtree Rtree;
typedef struct RtreeCursor RtreeCursor;
typedef struct RtreeNode RtreeNode;
typedef struct RtreeCell RtreeCell;
typedef struct RtreeConstraint RtreeConstraint;
typedef struct RtreeMatchArg RtreeMatchArg;
typedef struct RtreeGeomCallback RtreeGeomCallback;
typedef union RtreeCoord RtreeCoord;

/* The rtree may have between 1 and RTREE_MAX_DIMENSIONS dimensions. */
#define RTREE_MAX_DIMENSIONS 5

/* Size of hash table Rtree.aHash. This hash table is not expected to
** ever contain very many entries, so a fixed number of buckets is 
** used.
*/
#define HASHSIZE 128

/* 
** An rtree virtual-table object.
*/
struct Rtree {
  sqlite3_vtab base;
  sqlite3 *db;                /* Host database connection */
  int iNodeSize;              /* Size in bytes of each node in the node table */
  int nDim;                   /* Number of dimensions */
  int nBytesPerCell;          /* Bytes consumed per cell */
  int iDepth;                 /* Current depth of the r-tree structure */
  char *zDb;                  /* Name of database containing r-tree table */
  char *zName;                /* Name of r-tree table */ 
  RtreeNode *aHash[HASHSIZE]; /* Hash table of in-memory nodes. */ 
  int nBusy;                  /* Current number of users of this structure */

  /* List of nodes removed during a CondenseTree operation. List is
  ** linked together via the pointer normally used for hash chains -
  ** RtreeNode.pNext. RtreeNode.iNode stores the depth of the sub-tree 
  ** headed by the node (leaf nodes have RtreeNode.iNode==0).
  */
  RtreeNode *pDeleted;
  int iReinsertHeight;        /* Height of sub-trees Reinsert() has run on */

  /* Statements to read/write/delete a record from xxx_node */
  sqlite3_stmt *pReadNode;
  sqlite3_stmt *pWriteNode;
  sqlite3_stmt *pDeleteNode;

  /* Statements to read/write/delete a record from xxx_rowid */
  sqlite3_stmt *pReadRowid;
  sqlite3_stmt *pWriteRowid;
  sqlite3_stmt *pDeleteRowid;

  /* Statements to read/write/delete a record from xxx_parent */
  sqlite3_stmt *pReadParent;
  sqlite3_stmt *pWriteParent;
  sqlite3_stmt *pDeleteParent;

  int eCoordType;
};

/* Possible values for eCoordType: */
#define RTREE_COORD_REAL32 0
#define RTREE_COORD_INT32  1

/*
** The minimum number of cells allowed for a node is a third of the 
** maximum. In Gutman's notation:
**
**     m = M/3
**
** If an R*-tree "Reinsert" operation is required, the same number of
** cells are removed from the overfull node and reinserted into the tree.
*/
#define RTREE_MINCELLS(p) ((((p)->iNodeSize-4)/(p)->nBytesPerCell)/3)
#define RTREE_REINSERT(p) RTREE_MINCELLS(p)
#define RTREE_MAXCELLS 51

/*
** The smallest possible node-size is (512-64)==448 bytes. And the largest
** supported cell size is 48 bytes (8 byte rowid + ten 4 byte coordinates).
** Therefore all non-root nodes must contain at least 3 entries. Since 
** 2^40 is greater than 2^64, an r-tree structure always has a depth of
** 40 or less.
*/
#define RTREE_MAX_DEPTH 40

/* 
** An rtree cursor object.
*/
struct RtreeCursor {
  sqlite3_vtab_cursor base;
  RtreeNode *pNode;                 /* Node cursor is currently pointing at */
  int iCell;                        /* Index of current cell in pNode */
  int iStrategy;                    /* Copy of idxNum search parameter */
  int nConstraint;                  /* Number of entries in aConstraint */
  RtreeConstraint *aConstraint;     /* Search constraints. */
};

union RtreeCoord {
  float f;
  int i;
};

/*
** The argument is an RtreeCoord. Return the value stored within the RtreeCoord
** formatted as a double. This macro assumes that local variable pRtree points
** to the Rtree structure associated with the RtreeCoord.
*/
#define DCOORD(coord) (                           \
  (pRtree->eCoordType==RTREE_COORD_REAL32) ?      \
    ((double)coord.f) :                           \
    ((double)coord.i)                             \
)

/*
** A search constraint.
*/
struct RtreeConstraint {
  int iCoord;                     /* Index of constrained coordinate */
  int op;                         /* Constraining operation */
  double rValue;                  /* Constraint value. */
  int (*xGeom)(sqlite3_rtree_geometry *, int, double *, int *);
  sqlite3_rtree_geometry *pGeom;  /* Constraint callback argument for a MATCH */
};

/* Possible values for RtreeConstraint.op */
#define RTREE_EQ    0x41
#define RTREE_LE    0x42
#define RTREE_LT    0x43
#define RTREE_GE    0x44
#define RTREE_GT    0x45
#define RTREE_MATCH 0x46

/* 
** An rtree structure node.
*/
struct RtreeNode {
  RtreeNode *pParent;               /* Parent node */
  i64 iNode;
  int nRef;
  int isDirty;
  u8 *zData;
  RtreeNode *pNext;                 /* Next node in this hash chain */
};
#define NCELL(pNode) readInt16(&(pNode)->zData[2])

/* 
** Structure to store a deserialized rtree record.
*/
struct RtreeCell {
  i64 iRowid;
  RtreeCoord aCoord[RTREE_MAX_DIMENSIONS*2];
};


/*
** Value for the first field of every RtreeMatchArg object. The MATCH
** operator tests that the first field of a blob operand matches this
** value to avoid operating on invalid blobs (which could cause a segfault).
*/
#define RTREE_GEOMETRY_MAGIC 0x891245AB

/*
** An instance of this structure must be supplied as a blob argument to
** the right-hand-side of an SQL MATCH operator used to constrain an
** r-tree query.
*/
struct RtreeMatchArg {
  u32 magic;                      /* Always RTREE_GEOMETRY_MAGIC */
  int (*xGeom)(sqlite3_rtree_geometry *, int, double *, int *);
  void *pContext;
  int nParam;
  double aParam[1];
};

/*
** When a geometry callback is created (see sqlite3_rtree_geometry_callback),
** a single instance of the following structure is allocated. It is used
** as the context for the user-function created by by s_r_g_c(). The object
** is eventually deleted by the destructor mechanism provided by
** sqlite3_create_function_v2() (which is called by s_r_g_c() to create
** the geometry callback function).
*/
struct RtreeGeomCallback {
  int (*xGeom)(sqlite3_rtree_geometry *, int, double *, int *);
  void *pContext;
};

#ifndef MAX
# define MAX(x,y) ((x) < (y) ? (y) : (x))
#endif
#ifndef MIN
# define MIN(x,y) ((x) > (y) ? (y) : (x))
#endif

/*
** Functions to deserialize a 16 bit integer, 32 bit real number and
** 64 bit integer. The deserialized value is returned.
*/
static int readInt16(u8 *p){
  return (p[0]<<8) + p[1];
}
static void readCoord(u8 *p, RtreeCoord *pCoord){
  u32 i = (
    (((u32)p[0]) << 24) + 
    (((u32)p[1]) << 16) + 
    (((u32)p[2]) <<  8) + 
    (((u32)p[3]) <<  0)
  );
  *(u32 *)pCoord = i;
}
static i64 readInt64(u8 *p){
  return (
    (((i64)p[0]) << 56) + 
    (((i64)p[1]) << 48) + 
    (((i64)p[2]) << 40) + 
    (((i64)p[3]) << 32) + 
    (((i64)p[4]) << 24) + 
    (((i64)p[5]) << 16) + 
    (((i64)p[6]) <<  8) + 
    (((i64)p[7]) <<  0)
  );
}

/*
** Functions to serialize a 16 bit integer, 32 bit real number and
** 64 bit integer. The value returned is the number of bytes written
** to the argument buffer (always 2, 4 and 8 respectively).
*/
static int writeInt16(u8 *p, int i){
  p[0] = (i>> 8)&0xFF;
  p[1] = (i>> 0)&0xFF;
  return 2;
}
static int writeCoord(u8 *p, RtreeCoord *pCoord){
  u32 i;
  assert( sizeof(RtreeCoord)==4 );
  assert( sizeof(u32)==4 );
  i = *(u32 *)pCoord;
  p[0] = (i>>24)&0xFF;
  p[1] = (i>>16)&0xFF;
  p[2] = (i>> 8)&0xFF;
  p[3] = (i>> 0)&0xFF;
  return 4;
}
static int writeInt64(u8 *p, i64 i){
  p[0] = (i>>56)&0xFF;
  p[1] = (i>>48)&0xFF;
  p[2] = (i>>40)&0xFF;
  p[3] = (i>>32)&0xFF;
  p[4] = (i>>24)&0xFF;
  p[5] = (i>>16)&0xFF;
  p[6] = (i>> 8)&0xFF;
  p[7] = (i>> 0)&0xFF;
  return 8;
}

/*
** Increment the reference count of node p.
*/
static void nodeReference(RtreeNode *p){
  if( p ){
    p->nRef++;
  }
}

/*
** Clear the content of node p (set all bytes to 0x00).
*/
static void nodeZero(Rtree *pRtree, RtreeNode *p){
  memset(&p->zData[2], 0, pRtree->iNodeSize-2);
  p->isDirty = 1;
}

/*
** Given a node number iNode, return the corresponding key to use
** in the Rtree.aHash table.
*/
static int nodeHash(i64 iNode){
  return (
    (iNode>>56) ^ (iNode>>48) ^ (iNode>>40) ^ (iNode>>32) ^ 
    (iNode>>24) ^ (iNode>>16) ^ (iNode>> 8) ^ (iNode>> 0)
  ) % HASHSIZE;
}

/*
** Search the node hash table for node iNode. If found, return a pointer
** to it. Otherwise, return 0.
*/
static RtreeNode *nodeHashLookup(Rtree *pRtree, i64 iNode){
  RtreeNode *p;
  for(p=pRtree->aHash[nodeHash(iNode)]; p && p->iNode!=iNode; p=p->pNext);
  return p;
}

/*
** Add node pNode to the node hash table.
*/
static void nodeHashInsert(Rtree *pRtree, RtreeNode *pNode){
  int iHash;
  assert( pNode->pNext==0 );
  iHash = nodeHash(pNode->iNode);
  pNode->pNext = pRtree->aHash[iHash];
  pRtree->aHash[iHash] = pNode;
}

/*
** Remove node pNode from the node hash table.
*/
static void nodeHashDelete(Rtree *pRtree, RtreeNode *pNode){
  RtreeNode **pp;
  if( pNode->iNode!=0 ){
    pp = &pRtree->aHash[nodeHash(pNode->iNode)];
    for( ; (*pp)!=pNode; pp = &(*pp)->pNext){ assert(*pp); }
    *pp = pNode->pNext;
    pNode->pNext = 0;
  }
}

/*
** Allocate and return new r-tree node. Initially, (RtreeNode.iNode==0),
** indicating that node has not yet been assigned a node number. It is
** assigned a node number when nodeWrite() is called to write the
** node contents out to the database.
*/
static RtreeNode *nodeNew(Rtree *pRtree, RtreeNode *pParent){
  RtreeNode *pNode;
  pNode = (RtreeNode *)sqlite3_malloc(sizeof(RtreeNode) + pRtree->iNodeSize);
  if( pNode ){
    memset(pNode, 0, sizeof(RtreeNode) + pRtree->iNodeSize);
    pNode->zData = (u8 *)&pNode[1];
    pNode->nRef = 1;
    pNode->pParent = pParent;
    pNode->isDirty = 1;
    nodeReference(pParent);
  }
  return pNode;
}

/*
** Obtain a reference to an r-tree node.
*/
static int
nodeAcquire(
  Rtree *pRtree,             /* R-tree structure */
  i64 iNode,                 /* Node number to load */
  RtreeNode *pParent,        /* Either the parent node or NULL */
  RtreeNode **ppNode         /* OUT: Acquired node */
){
  int rc;
  int rc2 = SQLITE_OK;
  RtreeNode *pNode;

  /* Check if the requested node is already in the hash table. If so,
  ** increase its reference count and return it.
  */
  if( (pNode = nodeHashLookup(pRtree, iNode)) ){
    assert( !pParent || !pNode->pParent || pNode->pParent==pParent );
    if( pParent && !pNode->pParent ){
      nodeReference(pParent);
      pNode->pParent = pParent;
    }
    pNode->nRef++;
    *ppNode = pNode;
    return SQLITE_OK;
  }

  sqlite3_bind_int64(pRtree->pReadNode, 1, iNode);
  rc = sqlite3_step(pRtree->pReadNode);
  if( rc==SQLITE_ROW ){
    const u8 *zBlob = sqlite3_column_blob(pRtree->pReadNode, 0);
    if( pRtree->iNodeSize==sqlite3_column_bytes(pRtree->pReadNode, 0) ){
      pNode = (RtreeNode *)sqlite3_malloc(sizeof(RtreeNode)+pRtree->iNodeSize);
      if( !pNode ){
        rc2 = SQLITE_NOMEM;
      }else{
        pNode->pParent = pParent;
        pNode->zData = (u8 *)&pNode[1];
        pNode->nRef = 1;
        pNode->iNode = iNode;
        pNode->isDirty = 0;
        pNode->pNext = 0;
        memcpy(pNode->zData, zBlob, pRtree->iNodeSize);
        nodeReference(pParent);
      }
    }
  }
  rc = sqlite3_reset(pRtree->pReadNode);
  if( rc==SQLITE_OK ) rc = rc2;

  /* If the root node was just loaded, set pRtree->iDepth to the height
  ** of the r-tree structure. A height of zero means all data is stored on
  ** the root node. A height of one means the children of the root node
  ** are the leaves, and so on. If the depth as specified on the root node
  ** is greater than RTREE_MAX_DEPTH, the r-tree structure must be corrupt.
  */
  if( pNode && iNode==1 ){
    pRtree->iDepth = readInt16(pNode->zData);
    if( pRtree->iDepth>RTREE_MAX_DEPTH ){
      rc = SQLITE_CORRUPT_VTAB;
    }
  }

  /* If no error has occurred so far, check if the "number of entries"
  ** field on the node is too large. If so, set the return code to 
  ** SQLITE_CORRUPT_VTAB.
  */
  if( pNode && rc==SQLITE_OK ){
    if( NCELL(pNode)>((pRtree->iNodeSize-4)/pRtree->nBytesPerCell) ){
      rc = SQLITE_CORRUPT_VTAB;
    }
  }

  if( rc==SQLITE_OK ){
    if( pNode!=0 ){
      nodeHashInsert(pRtree, pNode);
    }else{
      rc = SQLITE_CORRUPT_VTAB;
    }
    *ppNode = pNode;
  }else{
    sqlite3_free(pNode);
    *ppNode = 0;
  }

  return rc;
}

/*
** Overwrite cell iCell of node pNode with the contents of pCell.
*/
static void nodeOverwriteCell(
  Rtree *pRtree, 
  RtreeNode *pNode,  
  RtreeCell *pCell, 
  int iCell
){
  int ii;
  u8 *p = &pNode->zData[4 + pRtree->nBytesPerCell*iCell];
  p += writeInt64(p, pCell->iRowid);
  for(ii=0; ii<(pRtree->nDim*2); ii++){
    p += writeCoord(p, &pCell->aCoord[ii]);
  }
  pNode->isDirty = 1;
}

/*
** Remove cell the cell with index iCell from node pNode.
*/
static void nodeDeleteCell(Rtree *pRtree, RtreeNode *pNode, int iCell){
  u8 *pDst = &pNode->zData[4 + pRtree->nBytesPerCell*iCell];
  u8 *pSrc = &pDst[pRtree->nBytesPerCell];
  int nByte = (NCELL(pNode) - iCell - 1) * pRtree->nBytesPerCell;
  memmove(pDst, pSrc, nByte);
  writeInt16(&pNode->zData[2], NCELL(pNode)-1);
  pNode->isDirty = 1;
}

/*
** Insert the contents of cell pCell into node pNode. If the insert
** is successful, return SQLITE_OK.
**
** If there is not enough free space in pNode, return SQLITE_FULL.
*/
static int
nodeInsertCell(
  Rtree *pRtree, 
  RtreeNode *pNode, 
  RtreeCell *pCell 
){
  int nCell;                    /* Current number of cells in pNode */
  int nMaxCell;                 /* Maximum number of cells for pNode */

  nMaxCell = (pRtree->iNodeSize-4)/pRtree->nBytesPerCell;
  nCell = NCELL(pNode);

  assert( nCell<=nMaxCell );
  if( nCell<nMaxCell ){
    nodeOverwriteCell(pRtree, pNode, pCell, nCell);
    writeInt16(&pNode->zData[2], nCell+1);
    pNode->isDirty = 1;
  }

  return (nCell==nMaxCell);
}

/*
** If the node is dirty, write it out to the database.
*/
static int
nodeWrite(Rtree *pRtree, RtreeNode *pNode){
  int rc = SQLITE_OK;
  if( pNode->isDirty ){
    sqlite3_stmt *p = pRtree->pWriteNode;
    if( pNode->iNode ){
      sqlite3_bind_int64(p, 1, pNode->iNode);
    }else{
      sqlite3_bind_null(p, 1);
    }
    sqlite3_bind_blob(p, 2, pNode->zData, pRtree->iNodeSize, SQLITE_STATIC);
    sqlite3_step(p);
    pNode->isDirty = 0;
    rc = sqlite3_reset(p);
    if( pNode->iNode==0 && rc==SQLITE_OK ){
      pNode->iNode = sqlite3_last_insert_rowid(pRtree->db);
      nodeHashInsert(pRtree, pNode);
    }
  }
  return rc;
}

/*
** Release a reference to a node. If the node is dirty and the reference
** count drops to zero, the node data is written to the database.
*/
static int
nodeRelease(Rtree *pRtree, RtreeNode *pNode){
  int rc = SQLITE_OK;
  if( pNode ){
    assert( pNode->nRef>0 );
    pNode->nRef--;
    if( pNode->nRef==0 ){
      if( pNode->iNode==1 ){
        pRtree->iDepth = -1;
      }
      if( pNode->pParent ){
        rc = nodeRelease(pRtree, pNode->pParent);
      }
      if( rc==SQLITE_OK ){
        rc = nodeWrite(pRtree, pNode);
      }
      nodeHashDelete(pRtree, pNode);
      sqlite3_free(pNode);
    }
  }
  return rc;
}

/*
** Return the 64-bit integer value associated with cell iCell of
** node pNode. If pNode is a leaf node, this is a rowid. If it is
** an internal node, then the 64-bit integer is a child page number.
*/
static i64 nodeGetRowid(
  Rtree *pRtree, 
  RtreeNode *pNode, 
  int iCell
){
  assert( iCell<NCELL(pNode) );
  return readInt64(&pNode->zData[4 + pRtree->nBytesPerCell*iCell]);
}

/*
** Return coordinate iCoord from cell iCell in node pNode.
*/
static void nodeGetCoord(
  Rtree *pRtree, 
  RtreeNode *pNode, 
  int iCell,
  int iCoord,
  RtreeCoord *pCoord           /* Space to write result to */
){
  readCoord(&pNode->zData[12 + pRtree->nBytesPerCell*iCell + 4*iCoord], pCoord);
}

/*
** Deserialize cell iCell of node pNode. Populate the structure pointed
** to by pCell with the results.
*/
static void nodeGetCell(
  Rtree *pRtree, 
  RtreeNode *pNode, 
  int iCell,
  RtreeCell *pCell
){
  int ii;
  pCell->iRowid = nodeGetRowid(pRtree, pNode, iCell);
  for(ii=0; ii<pRtree->nDim*2; ii++){
    nodeGetCoord(pRtree, pNode, iCell, ii, &pCell->aCoord[ii]);
  }
}


/* Forward declaration for the function that does the work of
** the virtual table module xCreate() and xConnect() methods.
*/
static int rtreeInit(
  sqlite3 *, void *, int, const char *const*, sqlite3_vtab **, char **, int
);

/* 
** Rtree virtual table module xCreate method.
*/
static int rtreeCreate(
  sqlite3 *db,
  void *pAux,
  int argc, const char *const*argv,
  sqlite3_vtab **ppVtab,
  char **pzErr
){
  return rtreeInit(db, pAux, argc, argv, ppVtab, pzErr, 1);
}

/* 
** Rtree virtual table module xConnect method.
*/
static int rtreeConnect(
  sqlite3 *db,
  void *pAux,
  int argc, const char *const*argv,
  sqlite3_vtab **ppVtab,
  char **pzErr
){
  return rtreeInit(db, pAux, argc, argv, ppVtab, pzErr, 0);
}

/*
** Increment the r-tree reference count.
*/
static void rtreeReference(Rtree *pRtree){
  pRtree->nBusy++;
}

/*
** Decrement the r-tree reference count. When the reference count reaches
** zero the structure is deleted.
*/
static void rtreeRelease(Rtree *pRtree){
  pRtree->nBusy--;
  if( pRtree->nBusy==0 ){
    sqlite3_finalize(pRtree->pReadNode);
    sqlite3_finalize(pRtree->pWriteNode);
    sqlite3_finalize(pRtree->pDeleteNode);
    sqlite3_finalize(pRtree->pReadRowid);
    sqlite3_finalize(pRtree->pWriteRowid);
    sqlite3_finalize(pRtree->pDeleteRowid);
    sqlite3_finalize(pRtree->pReadParent);
    sqlite3_finalize(pRtree->pWriteParent);
    sqlite3_finalize(pRtree->pDeleteParent);
    sqlite3_free(pRtree);
  }
}

/* 
** Rtree virtual table module xDisconnect method.
*/
static int rtreeDisconnect(sqlite3_vtab *pVtab){
  rtreeRelease((Rtree *)pVtab);
  return SQLITE_OK;
}

/* 
** Rtree virtual table module xDestroy method.
*/
static int rtreeDestroy(sqlite3_vtab *pVtab){
  Rtree *pRtree = (Rtree *)pVtab;
  int rc;
  char *zCreate = sqlite3_mprintf(
    "DROP TABLE '%q'.'%q_node';"
    "DROP TABLE '%q'.'%q_rowid';"
    "DROP TABLE '%q'.'%q_parent';",
    pRtree->zDb, pRtree->zName, 
    pRtree->zDb, pRtree->zName,
    pRtree->zDb, pRtree->zName
  );
  if( !zCreate ){
    rc = SQLITE_NOMEM;
  }else{
    rc = sqlite3_exec(pRtree->db, zCreate, 0, 0, 0);
    sqlite3_free(zCreate);
  }
  if( rc==SQLITE_OK ){
    rtreeRelease(pRtree);
  }

  return rc;
}

/* 
** Rtree virtual table module xOpen method.
*/
static int rtreeOpen(sqlite3_vtab *pVTab, sqlite3_vtab_cursor **ppCursor){
  int rc = SQLITE_NOMEM;
  RtreeCursor *pCsr;

  pCsr = (RtreeCursor *)sqlite3_malloc(sizeof(RtreeCursor));
  if( pCsr ){
    memset(pCsr, 0, sizeof(RtreeCursor));
    pCsr->base.pVtab = pVTab;
    rc = SQLITE_OK;
  }
  *ppCursor = (sqlite3_vtab_cursor *)pCsr;

  return rc;
}


/*
** Free the RtreeCursor.aConstraint[] array and its contents.
*/
static void freeCursorConstraints(RtreeCursor *pCsr){
  if( pCsr->aConstraint ){
    int i;                        /* Used to iterate through constraint array */
    for(i=0; i<pCsr->nConstraint; i++){
      sqlite3_rtree_geometry *pGeom = pCsr->aConstraint[i].pGeom;
      if( pGeom ){
        if( pGeom->xDelUser ) pGeom->xDelUser(pGeom->pUser);
        sqlite3_free(pGeom);
      }
    }
    sqlite3_free(pCsr->aConstraint);
    pCsr->aConstraint = 0;
  }
}

/* 
** Rtree virtual table module xClose method.
*/
static int rtreeClose(sqlite3_vtab_cursor *cur){
  Rtree *pRtree = (Rtree *)(cur->pVtab);
  int rc;
  RtreeCursor *pCsr = (RtreeCursor *)cur;
  freeCursorConstraints(pCsr);
  rc = nodeRelease(pRtree, pCsr->pNode);
  sqlite3_free(pCsr);
  return rc;
}

/*
** Rtree virtual table module xEof method.
**
** Return non-zero if the cursor does not currently point to a valid 
** record (i.e if the scan has finished), or zero otherwise.
*/
static int rtreeEof(sqlite3_vtab_cursor *cur){
  RtreeCursor *pCsr = (RtreeCursor *)cur;
  return (pCsr->pNode==0);
}

/*
** The r-tree constraint passed as the second argument to this function is
** guaranteed to be a MATCH constraint.
*/
static int testRtreeGeom(
  Rtree *pRtree,                  /* R-Tree object */
  RtreeConstraint *pConstraint,   /* MATCH constraint to test */
  RtreeCell *pCell,               /* Cell to test */
  int *pbRes                      /* OUT: Test result */
){
  int i;
  double aCoord[RTREE_MAX_DIMENSIONS*2];
  int nCoord = pRtree->nDim*2;

  assert( pConstraint->op==RTREE_MATCH );
  assert( pConstraint->pGeom );

  for(i=0; i<nCoord; i++){
    aCoord[i] = DCOORD(pCell->aCoord[i]);
  }
  return pConstraint->xGeom(pConstraint->pGeom, nCoord, aCoord, pbRes);
}

/* 
** Cursor pCursor currently points to a cell in a non-leaf page.
** Set *pbEof to true if the sub-tree headed by the cell is filtered
** (excluded) by the constraints in the pCursor->aConstraint[] 
** array, or false otherwise.
**
** Return SQLITE_OK if successful or an SQLite error code if an error
** occurs within a geometry callback.
*/
static int testRtreeCell(Rtree *pRtree, RtreeCursor *pCursor, int *pbEof){
  RtreeCell cell;
  int ii;
  int bRes = 0;
  int rc = SQLITE_OK;

  nodeGetCell(pRtree, pCursor->pNode, pCursor->iCell, &cell);
  for(ii=0; bRes==0 && ii<pCursor->nConstraint; ii++){
    RtreeConstraint *p = &pCursor->aConstraint[ii];
    double cell_min = DCOORD(cell.aCoord[(p->iCoord>>1)*2]);
    double cell_max = DCOORD(cell.aCoord[(p->iCoord>>1)*2+1]);

    assert(p->op==RTREE_LE || p->op==RTREE_LT || p->op==RTREE_GE 
        || p->op==RTREE_GT || p->op==RTREE_EQ || p->op==RTREE_MATCH
    );

    switch( p->op ){
      case RTREE_LE: case RTREE_LT: 
        bRes = p->rValue<cell_min; 
        break;

      case RTREE_GE: case RTREE_GT: 
        bRes = p->rValue>cell_max; 
        break;

      case RTREE_EQ:
        bRes = (p->rValue>cell_max || p->rValue<cell_min);
        break;

      default: {
        assert( p->op==RTREE_MATCH );
        rc = testRtreeGeom(pRtree, p, &cell, &bRes);
        bRes = !bRes;
        break;
      }
    }
  }

  *pbEof = bRes;
  return rc;
}

/* 
** Test if the cell that cursor pCursor currently points to
** would be filtered (excluded) by the constraints in the 
** pCursor->aConstraint[] array. If so, set *pbEof to true before
** returning. If the cell is not filtered (excluded) by the constraints,
** set pbEof to zero.
**
** Return SQLITE_OK if successful or an SQLite error code if an error
** occurs within a geometry callback.
**
** This function assumes that the cell is part of a leaf node.
*/
static int testRtreeEntry(Rtree *pRtree, RtreeCursor *pCursor, int *pbEof){
  RtreeCell cell;
  int ii;
  *pbEof = 0;

  nodeGetCell(pRtree, pCursor->pNode, pCursor->iCell, &cell);
  for(ii=0; ii<pCursor->nConstraint; ii++){
    RtreeConstraint *p = &pCursor->aConstraint[ii];
    double coord = DCOORD(cell.aCoord[p->iCoord]);
    int res;
    assert(p->op==RTREE_LE || p->op==RTREE_LT || p->op==RTREE_GE 
        || p->op==RTREE_GT || p->op==RTREE_EQ || p->op==RTREE_MATCH
    );
    switch( p->op ){
      case RTREE_LE: res = (coord<=p->rValue); break;
      case RTREE_LT: res = (coord<p->rValue);  break;
      case RTREE_GE: res = (coord>=p->rValue); break;
      case RTREE_GT: res = (coord>p->rValue);  break;
      case RTREE_EQ: res = (coord==p->rValue); break;
      default: {
        int rc;
        assert( p->op==RTREE_MATCH );
        rc = testRtreeGeom(pRtree, p, &cell, &res);
        if( rc!=SQLITE_OK ){
          return rc;
        }
        break;
      }
    }

    if( !res ){
      *pbEof = 1;
      return SQLITE_OK;
    }
  }

  return SQLITE_OK;
}

/*
** Cursor pCursor currently points at a node that heads a sub-tree of
** height iHeight (if iHeight==0, then the node is a leaf). Descend
** to point to the left-most cell of the sub-tree that matches the 
** configured constraints.
*/
static int descendToCell(
  Rtree *pRtree, 
  RtreeCursor *pCursor, 
  int iHeight,
  int *pEof                 /* OUT: Set to true if cannot descend */
){
  int isEof;
  int rc;
  int ii;
  RtreeNode *pChild;
  sqlite3_int64 iRowid;

  RtreeNode *pSavedNode = pCursor->pNode;
  int iSavedCell = pCursor->iCell;

  assert( iHeight>=0 );

  if( iHeight==0 ){
    rc = testRtreeEntry(pRtree, pCursor, &isEof);
  }else{
    rc = testRtreeCell(pRtree, pCursor, &isEof);
  }
  if( rc!=SQLITE_OK || isEof || iHeight==0 ){
    goto descend_to_cell_out;
  }

  iRowid = nodeGetRowid(pRtree, pCursor->pNode, pCursor->iCell);
  rc = nodeAcquire(pRtree, iRowid, pCursor->pNode, &pChild);
  if( rc!=SQLITE_OK ){
    goto descend_to_cell_out;
  }

  nodeRelease(pRtree, pCursor->pNode);
  pCursor->pNode = pChild;
  isEof = 1;
  for(ii=0; isEof && ii<NCELL(pChild); ii++){
    pCursor->iCell = ii;
    rc = descendToCell(pRtree, pCursor, iHeight-1, &isEof);
    if( rc!=SQLITE_OK ){
      goto descend_to_cell_out;
    }
  }

  if( isEof ){
    assert( pCursor->pNode==pChild );
    nodeReference(pSavedNode);
    nodeRelease(pRtree, pChild);
    pCursor->pNode = pSavedNode;
    pCursor->iCell = iSavedCell;
  }

descend_to_cell_out:
  *pEof = isEof;
  return rc;
}

/*
** One of the cells in node pNode is guaranteed to have a 64-bit 
** integer value equal to iRowid. Return the index of this cell.
*/
static int nodeRowidIndex(
  Rtree *pRtree, 
  RtreeNode *pNode, 
  i64 iRowid,
  int *piIndex
){
  int ii;
  int nCell = NCELL(pNode);
  for(ii=0; ii<nCell; ii++){
    if( nodeGetRowid(pRtree, pNode, ii)==iRowid ){
      *piIndex = ii;
      return SQLITE_OK;
    }
  }
  return SQLITE_CORRUPT_VTAB;
}

/*
** Return the index of the cell containing a pointer to node pNode
** in its parent. If pNode is the root node, return -1.
*/
static int nodeParentIndex(Rtree *pRtree, RtreeNode *pNode, int *piIndex){
  RtreeNode *pParent = pNode->pParent;
  if( pParent ){
    return nodeRowidIndex(pRtree, pParent, pNode->iNode, piIndex);
  }
  *piIndex = -1;
  return SQLITE_OK;
}

/* 
** Rtree virtual table module xNext method.
*/
static int rtreeNext(sqlite3_vtab_cursor *pVtabCursor){
  Rtree *pRtree = (Rtree *)(pVtabCursor->pVtab);
  RtreeCursor *pCsr = (RtreeCursor *)pVtabCursor;
  int rc = SQLITE_OK;

  /* RtreeCursor.pNode must not be NULL. If is is NULL, then this cursor is
  ** already at EOF. It is against the rules to call the xNext() method of
  ** a cursor that has already reached EOF.
  */
  assert( pCsr->pNode );

  if( pCsr->iStrategy==1 ){
    /* This "scan" is a direct lookup by rowid. There is no next entry. */
    nodeRelease(pRtree, pCsr->pNode);
    pCsr->pNode = 0;
  }else{
    /* Move to the next entry that matches the configured constraints. */
    int iHeight = 0;
    while( pCsr->pNode ){
      RtreeNode *pNode = pCsr->pNode;
      int nCell = NCELL(pNode);
      for(pCsr->iCell++; pCsr->iCell<nCell; pCsr->iCell++){
        int isEof;
        rc = descendToCell(pRtree, pCsr, iHeight, &isEof);
        if( rc!=SQLITE_OK || !isEof ){
          return rc;
        }
      }
      pCsr->pNode = pNode->pParent;
      rc = nodeParentIndex(pRtree, pNode, &pCsr->iCell);
      if( rc!=SQLITE_OK ){
        return rc;
      }
      nodeReference(pCsr->pNode);
      nodeRelease(pRtree, pNode);
      iHeight++;
    }
  }

  return rc;
}

/* 
** Rtree virtual table module xRowid method.
*/
static int rtreeRowid(sqlite3_vtab_cursor *pVtabCursor, sqlite_int64 *pRowid){
  Rtree *pRtree = (Rtree *)pVtabCursor->pVtab;
  RtreeCursor *pCsr = (RtreeCursor *)pVtabCursor;

  assert(pCsr->pNode);
  *pRowid = nodeGetRowid(pRtree, pCsr->pNode, pCsr->iCell);

  return SQLITE_OK;
}

/* 
** Rtree virtual table module xColumn method.
*/
static int rtreeColumn(sqlite3_vtab_cursor *cur, sqlite3_context *ctx, int i){
  Rtree *pRtree = (Rtree *)cur->pVtab;
  RtreeCursor *pCsr = (RtreeCursor *)cur;

  if( i==0 ){
    i64 iRowid = nodeGetRowid(pRtree, pCsr->pNode, pCsr->iCell);
    sqlite3_result_int64(ctx, iRowid);
  }else{
    RtreeCoord c;
    nodeGetCoord(pRtree, pCsr->pNode, pCsr->iCell, i-1, &c);
    if( pRtree->eCoordType==RTREE_COORD_REAL32 ){
      sqlite3_result_double(ctx, c.f);
    }else{
      assert( pRtree->eCoordType==RTREE_COORD_INT32 );
      sqlite3_result_int(ctx, c.i);
    }
  }

  return SQLITE_OK;
}

/* 
** Use nodeAcquire() to obtain the leaf node containing the record with 
** rowid iRowid. If successful, set *ppLeaf to point to the node and
** return SQLITE_OK. If there is no such record in the table, set
** *ppLeaf to 0 and return SQLITE_OK. If an error occurs, set *ppLeaf
** to zero and return an SQLite error code.
*/
static int findLeafNode(Rtree *pRtree, i64 iRowid, RtreeNode **ppLeaf){
  int rc;
  *ppLeaf = 0;
  sqlite3_bind_int64(pRtree->pReadRowid, 1, iRowid);
  if( sqlite3_step(pRtree->pReadRowid)==SQLITE_ROW ){
    i64 iNode = sqlite3_column_int64(pRtree->pReadRowid, 0);
    rc = nodeAcquire(pRtree, iNode, 0, ppLeaf);
    sqlite3_reset(pRtree->pReadRowid);
  }else{
    rc = sqlite3_reset(pRtree->pReadRowid);
  }
  return rc;
}

/*
** This function is called to configure the RtreeConstraint object passed
** as the second argument for a MATCH constraint. The value passed as the
** first argument to this function is the right-hand operand to the MATCH
** operator.
*/
static int deserializeGeometry(sqlite3_value *pValue, RtreeConstraint *pCons){
  RtreeMatchArg *p;
  sqlite3_rtree_geometry *pGeom;
  int nBlob;

  /* Check that value is actually a blob. */
  if( !sqlite3_value_type(pValue)==SQLITE_BLOB ) return SQLITE_ERROR;

  /* Check that the blob is roughly the right size. */
  nBlob = sqlite3_value_bytes(pValue);
  if( nBlob<(int)sizeof(RtreeMatchArg) 
   || ((nBlob-sizeof(RtreeMatchArg))%sizeof(double))!=0
  ){
    return SQLITE_ERROR;
  }

  pGeom = (sqlite3_rtree_geometry *)sqlite3_malloc(
      sizeof(sqlite3_rtree_geometry) + nBlob
  );
  if( !pGeom ) return SQLITE_NOMEM;
  memset(pGeom, 0, sizeof(sqlite3_rtree_geometry));
  p = (RtreeMatchArg *)&pGeom[1];

  memcpy(p, sqlite3_value_blob(pValue), nBlob);
  if( p->magic!=RTREE_GEOMETRY_MAGIC 
   || nBlob!=(int)(sizeof(RtreeMatchArg) + (p->nParam-1)*sizeof(double))
  ){
    sqlite3_free(pGeom);
    return SQLITE_ERROR;
  }

  pGeom->pContext = p->pContext;
  pGeom->nParam = p->nParam;
  pGeom->aParam = p->aParam;

  pCons->xGeom = p->xGeom;
  pCons->pGeom = pGeom;
  return SQLITE_OK;
}

/* 
** Rtree virtual table module xFilter method.
*/
static int rtreeFilter(
  sqlite3_vtab_cursor *pVtabCursor, 
  int idxNum, const char *idxStr,
  int argc, sqlite3_value **argv
){
  Rtree *pRtree = (Rtree *)pVtabCursor->pVtab;
  RtreeCursor *pCsr = (RtreeCursor *)pVtabCursor;

  RtreeNode *pRoot = 0;
  int ii;
  int rc = SQLITE_OK;

  rtreeReference(pRtree);

  freeCursorConstraints(pCsr);
  pCsr->iStrategy = idxNum;

  if( idxNum==1 ){
    /* Special case - lookup by rowid. */
    RtreeNode *pLeaf;        /* Leaf on which the required cell resides */
    i64 iRowid = sqlite3_value_int64(argv[0]);
    rc = findLeafNode(pRtree, iRowid, &pLeaf);
    pCsr->pNode = pLeaf; 
    if( pLeaf ){
      assert( rc==SQLITE_OK );
      rc = nodeRowidIndex(pRtree, pLeaf, iRowid, &pCsr->iCell);
    }
  }else{
    /* Normal case - r-tree scan. Set up the RtreeCursor.aConstraint array 
    ** with the configured constraints. 
    */
    if( argc>0 ){
      pCsr->aConstraint = sqlite3_malloc(sizeof(RtreeConstraint)*argc);
      pCsr->nConstraint = argc;
      if( !pCsr->aConstraint ){
        rc = SQLITE_NOMEM;
      }else{
        memset(pCsr->aConstraint, 0, sizeof(RtreeConstraint)*argc);
        assert( (idxStr==0 && argc==0)
                || (idxStr && (int)strlen(idxStr)==argc*2) );
        for(ii=0; ii<argc; ii++){
          RtreeConstraint *p = &pCsr->aConstraint[ii];
          p->op = idxStr[ii*2];
          p->iCoord = idxStr[ii*2+1]-'a';
          if( p->op==RTREE_MATCH ){
            /* A MATCH operator. The right-hand-side must be a blob that
            ** can be cast into an RtreeMatchArg object. One created using
            ** an sqlite3_rtree_geometry_callback() SQL user function.
            */
            rc = deserializeGeometry(argv[ii], p);
            if( rc!=SQLITE_OK ){
              break;
            }
          }else{
            p->rValue = sqlite3_value_double(argv[ii]);
          }
        }
      }
    }
  
    if( rc==SQLITE_OK ){
      pCsr->pNode = 0;
      rc = nodeAcquire(pRtree, 1, 0, &pRoot);
    }
    if( rc==SQLITE_OK ){
      int isEof = 1;
      int nCell = NCELL(pRoot);
      pCsr->pNode = pRoot;
      for(pCsr->iCell=0; rc==SQLITE_OK && pCsr->iCell<nCell; pCsr->iCell++){
        assert( pCsr->pNode==pRoot );
        rc = descendToCell(pRtree, pCsr, pRtree->iDepth, &isEof);
        if( !isEof ){
          break;
        }
      }
      if( rc==SQLITE_OK && isEof ){
        assert( pCsr->pNode==pRoot );
        nodeRelease(pRtree, pRoot);
        pCsr->pNode = 0;
      }
      assert( rc!=SQLITE_OK || !pCsr->pNode || pCsr->iCell<NCELL(pCsr->pNode) );
    }
  }

  rtreeRelease(pRtree);
  return rc;
}

/*
** Rtree virtual table module xBestIndex method. There are three
** table scan strategies to choose from (in order from most to 
** least desirable):
**
**   idxNum     idxStr        Strategy
**   ------------------------------------------------
**     1        Unused        Direct lookup by rowid.
**     2        See below     R-tree query or full-table scan.
**   ------------------------------------------------
**
** If strategy 1 is used, then idxStr is not meaningful. If strategy
** 2 is used, idxStr is formatted to contain 2 bytes for each 
** constraint used. The first two bytes of idxStr correspond to 
** the constraint in sqlite3_index_info.aConstraintUsage[] with
** (argvIndex==1) etc.
**
** The first of each pair of bytes in idxStr identifies the constraint
** operator as follows:
**
**   Operator    Byte Value
**   ----------------------
**      =        0x41 ('A')
**     <=        0x42 ('B')
**      <        0x43 ('C')
**     >=        0x44 ('D')
**      >        0x45 ('E')
**   MATCH       0x46 ('F')
**   ----------------------
**
** The second of each pair of bytes identifies the coordinate column
** to which the constraint applies. The leftmost coordinate column
** is 'a', the second from the left 'b' etc.
*/
static int rtreeBestIndex(sqlite3_vtab *tab, sqlite3_index_info *pIdxInfo){
  int rc = SQLITE_OK;
  int ii;

  int iIdx = 0;
  char zIdxStr[RTREE_MAX_DIMENSIONS*8+1];
  memset(zIdxStr, 0, sizeof(zIdxStr));
  UNUSED_PARAMETER(tab);

  assert( pIdxInfo->idxStr==0 );
  for(ii=0; ii<pIdxInfo->nConstraint && iIdx<(int)(sizeof(zIdxStr)-1); ii++){
    struct sqlite3_index_constraint *p = &pIdxInfo->aConstraint[ii];

    if( p->usable && p->iColumn==0 && p->op==SQLITE_INDEX_CONSTRAINT_EQ ){
      /* We have an equality constraint on the rowid. Use strategy 1. */
      int jj;
      for(jj=0; jj<ii; jj++){
        pIdxInfo->aConstraintUsage[jj].argvIndex = 0;
        pIdxInfo->aConstraintUsage[jj].omit = 0;
      }
      pIdxInfo->idxNum = 1;
      pIdxInfo->aConstraintUsage[ii].argvIndex = 1;
      pIdxInfo->aConstraintUsage[jj].omit = 1;

      /* This strategy involves a two rowid lookups on an B-Tree structures
      ** and then a linear search of an R-Tree node. This should be 
      ** considered almost as quick as a direct rowid lookup (for which 
      ** sqlite uses an internal cost of 0.0).
      */ 
      pIdxInfo->estimatedCost = 10.0;
      return SQLITE_OK;
    }

    if( p->usable && (p->iColumn>0 || p->op==SQLITE_INDEX_CONSTRAINT_MATCH) ){
      u8 op;
      switch( p->op ){
        case SQLITE_INDEX_CONSTRAINT_EQ: op = RTREE_EQ; break;
        case SQLITE_INDEX_CONSTRAINT_GT: op = RTREE_GT; break;
        case SQLITE_INDEX_CONSTRAINT_LE: op = RTREE_LE; break;
        case SQLITE_INDEX_CONSTRAINT_LT: op = RTREE_LT; break;
        case SQLITE_INDEX_CONSTRAINT_GE: op = RTREE_GE; break;
        default:
          assert( p->op==SQLITE_INDEX_CONSTRAINT_MATCH );
          op = RTREE_MATCH; 
          break;
      }
      zIdxStr[iIdx++] = op;
      zIdxStr[iIdx++] = p->iColumn - 1 + 'a';
      pIdxInfo->aConstraintUsage[ii].argvIndex = (iIdx/2);
      pIdxInfo->aConstraintUsage[ii].omit = 1;
    }
  }

  pIdxInfo->idxNum = 2;
  pIdxInfo->needToFreeIdxStr = 1;
  if( iIdx>0 && 0==(pIdxInfo->idxStr = sqlite3_mprintf("%s", zIdxStr)) ){
    return SQLITE_NOMEM;
  }
  assert( iIdx>=0 );
  pIdxInfo->estimatedCost = (2000000.0 / (double)(iIdx + 1));
  return rc;
}

/*
** Return the N-dimensional volumn of the cell stored in *p.
*/
static float cellArea(Rtree *pRtree, RtreeCell *p){
  float area = 1.0;
  int ii;
  for(ii=0; ii<(pRtree->nDim*2); ii+=2){
    area = (float)(area * (DCOORD(p->aCoord[ii+1]) - DCOORD(p->aCoord[ii])));
  }
  return area;
}

/*
** Return the margin length of cell p. The margin length is the sum
** of the objects size in each dimension.
*/
static float cellMargin(Rtree *pRtree, RtreeCell *p){
  float margin = 0.0;
  int ii;
  for(ii=0; ii<(pRtree->nDim*2); ii+=2){
    margin += (float)(DCOORD(p->aCoord[ii+1]) - DCOORD(p->aCoord[ii]));
  }
  return margin;
}

/*
** Store the union of cells p1 and p2 in p1.
*/
static void cellUnion(Rtree *pRtree, RtreeCell *p1, RtreeCell *p2){
  int ii;
  if( pRtree->eCoordType==RTREE_COORD_REAL32 ){
    for(ii=0; ii<(pRtree->nDim*2); ii+=2){
      p1->aCoord[ii].f = MIN(p1->aCoord[ii].f, p2->aCoord[ii].f);
      p1->aCoord[ii+1].f = MAX(p1->aCoord[ii+1].f, p2->aCoord[ii+1].f);
    }
  }else{
    for(ii=0; ii<(pRtree->nDim*2); ii+=2){
      p1->aCoord[ii].i = MIN(p1->aCoord[ii].i, p2->aCoord[ii].i);
      p1->aCoord[ii+1].i = MAX(p1->aCoord[ii+1].i, p2->aCoord[ii+1].i);
    }
  }
}

/*
** Return true if the area covered by p2 is a subset of the area covered
** by p1. False otherwise.
*/
static int cellContains(Rtree *pRtree, RtreeCell *p1, RtreeCell *p2){
  int ii;
  int isInt = (pRtree->eCoordType==RTREE_COORD_INT32);
  for(ii=0; ii<(pRtree->nDim*2); ii+=2){
    RtreeCoord *a1 = &p1->aCoord[ii];
    RtreeCoord *a2 = &p2->aCoord[ii];
    if( (!isInt && (a2[0].f<a1[0].f || a2[1].f>a1[1].f)) 
     || ( isInt && (a2[0].i<a1[0].i || a2[1].i>a1[1].i)) 
    ){
      return 0;
    }
  }
  return 1;
}

/*
** Return the amount cell p would grow by if it were unioned with pCell.
*/
static float cellGrowth(Rtree *pRtree, RtreeCell *p, RtreeCell *pCell){
  float area;
  RtreeCell cell;
  memcpy(&cell, p, sizeof(RtreeCell));
  area = cellArea(pRtree, &cell);
  cellUnion(pRtree, &cell, pCell);
  return (cellArea(pRtree, &cell)-area);
}

#if VARIANT_RSTARTREE_CHOOSESUBTREE || VARIANT_RSTARTREE_SPLIT
static float cellOverlap(
  Rtree *pRtree, 
  RtreeCell *p, 
  RtreeCell *aCell, 
  int nCell, 
  int iExclude
){
  int ii;
  float overlap = 0.0;
  for(ii=0; ii<nCell; ii++){
#if VARIANT_RSTARTREE_CHOOSESUBTREE
    if( ii!=iExclude )
#else
    assert( iExclude==-1 );
    UNUSED_PARAMETER(iExclude);
#endif
    {
      int jj;
      float o = 1.0;
      for(jj=0; jj<(pRtree->nDim*2); jj+=2){
        double x1;
        double x2;

        x1 = MAX(DCOORD(p->aCoord[jj]), DCOORD(aCell[ii].aCoord[jj]));
        x2 = MIN(DCOORD(p->aCoord[jj+1]), DCOORD(aCell[ii].aCoord[jj+1]));

        if( x2<x1 ){
          o = 0.0;
          break;
        }else{
          o = o * (float)(x2-x1);
        }
      }
      overlap += o;
    }
  }
  return overlap;
}
#endif

#if VARIANT_RSTARTREE_CHOOSESUBTREE
static float cellOverlapEnlargement(
  Rtree *pRtree, 
  RtreeCell *p, 
  RtreeCell *pInsert, 
  RtreeCell *aCell, 
  int nCell, 
  int iExclude
){
  double before;
  double after;
  before = cellOverlap(pRtree, p, aCell, nCell, iExclude);
  cellUnion(pRtree, p, pInsert);
  after = cellOverlap(pRtree, p, aCell, nCell, iExclude);
  return (float)(after-before);
}
#endif


/*
** This function implements the ChooseLeaf algorithm from Gutman[84].
** ChooseSubTree in r*tree terminology.
*/
static int ChooseLeaf(
  Rtree *pRtree,               /* Rtree table */
  RtreeCell *pCell,            /* Cell to insert into rtree */
  int iHeight,                 /* Height of sub-tree rooted at pCell */
  RtreeNode **ppLeaf           /* OUT: Selected leaf page */
){
  int rc;
  int ii;
  RtreeNode *pNode;
  rc = nodeAcquire(pRtree, 1, 0, &pNode);

  for(ii=0; rc==SQLITE_OK && ii<(pRtree->iDepth-iHeight); ii++){
    int iCell;
    sqlite3_int64 iBest = 0;

    float fMinGrowth = 0.0;
    float fMinArea = 0.0;
#if VARIANT_RSTARTREE_CHOOSESUBTREE
    float fMinOverlap = 0.0;
    float overlap;
#endif

    int nCell = NCELL(pNode);
    RtreeCell cell;
    RtreeNode *pChild;

    RtreeCell *aCell = 0;

#if VARIANT_RSTARTREE_CHOOSESUBTREE
    if( ii==(pRtree->iDepth-1) ){
      int jj;
      aCell = sqlite3_malloc(sizeof(RtreeCell)*nCell);
      if( !aCell ){
        rc = SQLITE_NOMEM;
        nodeRelease(pRtree, pNode);
        pNode = 0;
        continue;
      }
      for(jj=0; jj<nCell; jj++){
        nodeGetCell(pRtree, pNode, jj, &aCell[jj]);
      }
    }
#endif

    /* Select the child node which will be enlarged the least if pCell
    ** is inserted into it. Resolve ties by choosing the entry with
    ** the smallest area.
    */
    for(iCell=0; iCell<nCell; iCell++){
      int bBest = 0;
      float growth;
      float area;
      nodeGetCell(pRtree, pNode, iCell, &cell);
      growth = cellGrowth(pRtree, &cell, pCell);
      area = cellArea(pRtree, &cell);

#if VARIANT_RSTARTREE_CHOOSESUBTREE
      if( ii==(pRtree->iDepth-1) ){
        overlap = cellOverlapEnlargement(pRtree,&cell,pCell,aCell,nCell,iCell);
      }else{
        overlap = 0.0;
      }
      if( (iCell==0) 
       || (overlap<fMinOverlap) 
       || (overlap==fMinOverlap && growth<fMinGrowth)
       || (overlap==fMinOverlap && growth==fMinGrowth && area<fMinArea)
      ){
        bBest = 1;
        fMinOverlap = overlap;
      }
#else
      if( iCell==0||growth<fMinGrowth||(growth==fMinGrowth && area<fMinArea) ){
        bBest = 1;
      }
#endif
      if( bBest ){
        fMinGrowth = growth;
        fMinArea = area;
        iBest = cell.iRowid;
      }
    }

    sqlite3_free(aCell);
    rc = nodeAcquire(pRtree, iBest, pNode, &pChild);
    nodeRelease(pRtree, pNode);
    pNode = pChild;
  }

  *ppLeaf = pNode;
  return rc;
}

/*
** A cell with the same content as pCell has just been inserted into
** the node pNode. This function updates the bounding box cells in
** all ancestor elements.
*/
static int AdjustTree(
  Rtree *pRtree,                    /* Rtree table */
  RtreeNode *pNode,                 /* Adjust ancestry of this node. */
  RtreeCell *pCell                  /* This cell was just inserted */
){
  RtreeNode *p = pNode;
  while( p->pParent ){
    RtreeNode *pParent = p->pParent;
    RtreeCell cell;
    int iCell;

    if( nodeParentIndex(pRtree, p, &iCell) ){
      return SQLITE_CORRUPT_VTAB;
    }

    nodeGetCell(pRtree, pParent, iCell, &cell);
    if( !cellContains(pRtree, &cell, pCell) ){
      cellUnion(pRtree, &cell, pCell);
      nodeOverwriteCell(pRtree, pParent, &cell, iCell);
    }
 
    p = pParent;
  }
  return SQLITE_OK;
}

/*
** Write mapping (iRowid->iNode) to the <rtree>_rowid table.
*/
static int rowidWrite(Rtree *pRtree, sqlite3_int64 iRowid, sqlite3_int64 iNode){
  sqlite3_bind_int64(pRtree->pWriteRowid, 1, iRowid);
  sqlite3_bind_int64(pRtree->pWriteRowid, 2, iNode);
  sqlite3_step(pRtree->pWriteRowid);
  return sqlite3_reset(pRtree->pWriteRowid);
}

/*
** Write mapping (iNode->iPar) to the <rtree>_parent table.
*/
static int parentWrite(Rtree *pRtree, sqlite3_int64 iNode, sqlite3_int64 iPar){
  sqlite3_bind_int64(pRtree->pWriteParent, 1, iNode);
  sqlite3_bind_int64(pRtree->pWriteParent, 2, iPar);
  sqlite3_step(pRtree->pWriteParent);
  return sqlite3_reset(pRtree->pWriteParent);
}

static int rtreeInsertCell(Rtree *, RtreeNode *, RtreeCell *, int);

#if VARIANT_GUTTMAN_LINEAR_SPLIT
/*
** Implementation of the linear variant of the PickNext() function from
** Guttman[84].
*/
static RtreeCell *LinearPickNext(
  Rtree *pRtree,
  RtreeCell *aCell, 
  int nCell, 
  RtreeCell *pLeftBox, 
  RtreeCell *pRightBox,
  int *aiUsed
){
  int ii;
  for(ii=0; aiUsed[ii]; ii++);
  aiUsed[ii] = 1;
  return &aCell[ii];
}

/*
** Implementation of the linear variant of the PickSeeds() function from
** Guttman[84].
*/
static void LinearPickSeeds(
  Rtree *pRtree,
  RtreeCell *aCell, 
  int nCell, 
  int *piLeftSeed, 
  int *piRightSeed
){
  int i;
  int iLeftSeed = 0;
  int iRightSeed = 1;
  float maxNormalInnerWidth = 0.0;

  /* Pick two "seed" cells from the array of cells. The algorithm used
  ** here is the LinearPickSeeds algorithm from Gutman[1984]. The 
  ** indices of the two seed cells in the array are stored in local
  ** variables iLeftSeek and iRightSeed.
  */
  for(i=0; i<pRtree->nDim; i++){
    float x1 = DCOORD(aCell[0].aCoord[i*2]);
    float x2 = DCOORD(aCell[0].aCoord[i*2+1]);
    float x3 = x1;
    float x4 = x2;
    int jj;

    int iCellLeft = 0;
    int iCellRight = 0;

    for(jj=1; jj<nCell; jj++){
      float left = DCOORD(aCell[jj].aCoord[i*2]);
      float right = DCOORD(aCell[jj].aCoord[i*2+1]);

      if( left<x1 ) x1 = left;
      if( right>x4 ) x4 = right;
      if( left>x3 ){
        x3 = left;
        iCellRight = jj;
      }
      if( right<x2 ){
        x2 = right;
        iCellLeft = jj;
      }
    }

    if( x4!=x1 ){
      float normalwidth = (x3 - x2) / (x4 - x1);
      if( normalwidth>maxNormalInnerWidth ){
        iLeftSeed = iCellLeft;
        iRightSeed = iCellRight;
      }
    }
  }

  *piLeftSeed = iLeftSeed;
  *piRightSeed = iRightSeed;
}
#endif /* VARIANT_GUTTMAN_LINEAR_SPLIT */

#if VARIANT_GUTTMAN_QUADRATIC_SPLIT
/*
** Implementation of the quadratic variant of the PickNext() function from
** Guttman[84].
*/
static RtreeCell *QuadraticPickNext(
  Rtree *pRtree,
  RtreeCell *aCell, 
  int nCell, 
  RtreeCell *pLeftBox, 
  RtreeCell *pRightBox,
  int *aiUsed
){
  #define FABS(a) ((a)<0.0?-1.0*(a):(a))

  int iSelect = -1;
  float fDiff;
  int ii;
  for(ii=0; ii<nCell; ii++){
    if( aiUsed[ii]==0 ){
      float left = cellGrowth(pRtree, pLeftBox, &aCell[ii]);
      float right = cellGrowth(pRtree, pLeftBox, &aCell[ii]);
      float diff = FABS(right-left);
      if( iSelect<0 || diff>fDiff ){
        fDiff = diff;
        iSelect = ii;
      }
    }
  }
  aiUsed[iSelect] = 1;
  return &aCell[iSelect];
}

/*
** Implementation of the quadratic variant of the PickSeeds() function from
** Guttman[84].
*/
static void QuadraticPickSeeds(
  Rtree *pRtree,
  RtreeCell *aCell, 
  int nCell, 
  int *piLeftSeed, 
  int *piRightSeed
){
  int ii;
  int jj;

  int iLeftSeed = 0;
  int iRightSeed = 1;
  float fWaste = 0.0;

  for(ii=0; ii<nCell; ii++){
    for(jj=ii+1; jj<nCell; jj++){
      float right = cellArea(pRtree, &aCell[jj]);
      float growth = cellGrowth(pRtree, &aCell[ii], &aCell[jj]);
      float waste = growth - right;

      if( waste>fWaste ){
        iLeftSeed = ii;
        iRightSeed = jj;
        fWaste = waste;
      }
    }
  }

  *piLeftSeed = iLeftSeed;
  *piRightSeed = iRightSeed;
}
#endif /* VARIANT_GUTTMAN_QUADRATIC_SPLIT */

/*
** Arguments aIdx, aDistance and aSpare all point to arrays of size
** nIdx. The aIdx array contains the set of integers from 0 to 
** (nIdx-1) in no particular order. This function sorts the values
** in aIdx according to the indexed values in aDistance. For
** example, assuming the inputs:
**
**   aIdx      = { 0,   1,   2,   3 }
**   aDistance = { 5.0, 2.0, 7.0, 6.0 }
**
** this function sets the aIdx array to contain:
**
**   aIdx      = { 0,   1,   2,   3 }
**
** The aSpare array is used as temporary working space by the
** sorting algorithm.
*/
static void SortByDistance(
  int *aIdx, 
  int nIdx, 
  float *aDistance, 
  int *aSpare
){
  if( nIdx>1 ){
    int iLeft = 0;
    int iRight = 0;

    int nLeft = nIdx/2;
    int nRight = nIdx-nLeft;
    int *aLeft = aIdx;
    int *aRight = &aIdx[nLeft];

    SortByDistance(aLeft, nLeft, aDistance, aSpare);
    SortByDistance(aRight, nRight, aDistance, aSpare);

    memcpy(aSpare, aLeft, sizeof(int)*nLeft);
    aLeft = aSpare;

    while( iLeft<nLeft || iRight<nRight ){
      if( iLeft==nLeft ){
        aIdx[iLeft+iRight] = aRight[iRight];
        iRight++;
      }else if( iRight==nRight ){
        aIdx[iLeft+iRight] = aLeft[iLeft];
        iLeft++;
      }else{
        float fLeft = aDistance[aLeft[iLeft]];
        float fRight = aDistance[aRight[iRight]];
        if( fLeft<fRight ){
          aIdx[iLeft+iRight] = aLeft[iLeft];
          iLeft++;
        }else{
          aIdx[iLeft+iRight] = aRight[iRight];
          iRight++;
        }
      }
    }

#if 0
    /* Check that the sort worked */
    {
      int jj;
      for(jj=1; jj<nIdx; jj++){
        float left = aDistance[aIdx[jj-1]];
        float right = aDistance[aIdx[jj]];
        assert( left<=right );
      }
    }
#endif
  }
}

/*
** Arguments aIdx, aCell and aSpare all point to arrays of size
** nIdx. The aIdx array contains the set of integers from 0 to 
** (nIdx-1) in no particular order. This function sorts the values
** in aIdx according to dimension iDim of the cells in aCell. The
** minimum value of dimension iDim is considered first, the
** maximum used to break ties.
**
** The aSpare array is used as temporary working space by the
** sorting algorithm.
*/
static void SortByDimension(
  Rtree *pRtree,
  int *aIdx, 
  int nIdx, 
  int iDim, 
  RtreeCell *aCell, 
  int *aSpare
){
  if( nIdx>1 ){

    int iLeft = 0;
    int iRight = 0;

    int nLeft = nIdx/2;
    int nRight = nIdx-nLeft;
    int *aLeft = aIdx;
    int *aRight = &aIdx[nLeft];

    SortByDimension(pRtree, aLeft, nLeft, iDim, aCell, aSpare);
    SortByDimension(pRtree, aRight, nRight, iDim, aCell, aSpare);

    memcpy(aSpare, aLeft, sizeof(int)*nLeft);
    aLeft = aSpare;
    while( iLeft<nLeft || iRight<nRight ){
      double xleft1 = DCOORD(aCell[aLeft[iLeft]].aCoord[iDim*2]);
      double xleft2 = DCOORD(aCell[aLeft[iLeft]].aCoord[iDim*2+1]);
      double xright1 = DCOORD(aCell[aRight[iRight]].aCoord[iDim*2]);
      double xright2 = DCOORD(aCell[aRight[iRight]].aCoord[iDim*2+1]);
      if( (iLeft!=nLeft) && ((iRight==nRight)
       || (xleft1<xright1)
       || (xleft1==xright1 && xleft2<xright2)
      )){
        aIdx[iLeft+iRight] = aLeft[iLeft];
        iLeft++;
      }else{
        aIdx[iLeft+iRight] = aRight[iRight];
        iRight++;
      }
    }

#if 0
    /* Check that the sort worked */
    {
      int jj;
      for(jj=1; jj<nIdx; jj++){
        float xleft1 = aCell[aIdx[jj-1]].aCoord[iDim*2];
        float xleft2 = aCell[aIdx[jj-1]].aCoord[iDim*2+1];
        float xright1 = aCell[aIdx[jj]].aCoord[iDim*2];
        float xright2 = aCell[aIdx[jj]].aCoord[iDim*2+1];
        assert( xleft1<=xright1 && (xleft1<xright1 || xleft2<=xright2) );
      }
    }
#endif
  }
}

#if VARIANT_RSTARTREE_SPLIT
/*
** Implementation of the R*-tree variant of SplitNode from Beckman[1990].
*/
static int splitNodeStartree(
  Rtree *pRtree,
  RtreeCell *aCell,
  int nCell,
  RtreeNode *pLeft,
  RtreeNode *pRight,
  RtreeCell *pBboxLeft,
  RtreeCell *pBboxRight
){
  int **aaSorted;
  int *aSpare;
  int ii;

  int iBestDim = 0;
  int iBestSplit = 0;
  float fBestMargin = 0.0;

  int nByte = (pRtree->nDim+1)*(sizeof(int*)+nCell*sizeof(int));

  aaSorted = (int **)sqlite3_malloc(nByte);
  if( !aaSorted ){
    return SQLITE_NOMEM;
  }

  aSpare = &((int *)&aaSorted[pRtree->nDim])[pRtree->nDim*nCell];
  memset(aaSorted, 0, nByte);
  for(ii=0; ii<pRtree->nDim; ii++){
    int jj;
    aaSorted[ii] = &((int *)&aaSorted[pRtree->nDim])[ii*nCell];
    for(jj=0; jj<nCell; jj++){
      aaSorted[ii][jj] = jj;
    }
    SortByDimension(pRtree, aaSorted[ii], nCell, ii, aCell, aSpare);
  }

  for(ii=0; ii<pRtree->nDim; ii++){
    float margin = 0.0;
    float fBestOverlap = 0.0;
    float fBestArea = 0.0;
    int iBestLeft = 0;
    int nLeft;

    for(
      nLeft=RTREE_MINCELLS(pRtree); 
      nLeft<=(nCell-RTREE_MINCELLS(pRtree)); 
      nLeft++
    ){
      RtreeCell left;
      RtreeCell right;
      int kk;
      float overlap;
      float area;

      memcpy(&left, &aCell[aaSorted[ii][0]], sizeof(RtreeCell));
      memcpy(&right, &aCell[aaSorted[ii][nCell-1]], sizeof(RtreeCell));
      for(kk=1; kk<(nCell-1); kk++){
        if( kk<nLeft ){
          cellUnion(pRtree, &left, &aCell[aaSorted[ii][kk]]);
        }else{
          cellUnion(pRtree, &right, &aCell[aaSorted[ii][kk]]);
        }
      }
      margin += cellMargin(pRtree, &left);
      margin += cellMargin(pRtree, &right);
      overlap = cellOverlap(pRtree, &left, &right, 1, -1);
      area = cellArea(pRtree, &left) + cellArea(pRtree, &right);
      if( (nLeft==RTREE_MINCELLS(pRtree))
       || (overlap<fBestOverlap)
       || (overlap==fBestOverlap && area<fBestArea)
      ){
        iBestLeft = nLeft;
        fBestOverlap = overlap;
        fBestArea = area;
      }
    }

    if( ii==0 || margin<fBestMargin ){
      iBestDim = ii;
      fBestMargin = margin;
      iBestSplit = iBestLeft;
    }
  }

  memcpy(pBboxLeft, &aCell[aaSorted[iBestDim][0]], sizeof(RtreeCell));
  memcpy(pBboxRight, &aCell[aaSorted[iBestDim][iBestSplit]], sizeof(RtreeCell));
  for(ii=0; ii<nCell; ii++){
    RtreeNode *pTarget = (ii<iBestSplit)?pLeft:pRight;
    RtreeCell *pBbox = (ii<iBestSplit)?pBboxLeft:pBboxRight;
    RtreeCell *pCell = &aCell[aaSorted[iBestDim][ii]];
    nodeInsertCell(pRtree, pTarget, pCell);
    cellUnion(pRtree, pBbox, pCell);
  }

  sqlite3_free(aaSorted);
  return SQLITE_OK;
}
#endif

#if VARIANT_GUTTMAN_SPLIT
/*
** Implementation of the regular R-tree SplitNode from Guttman[1984].
*/
static int splitNodeGuttman(
  Rtree *pRtree,
  RtreeCell *aCell,
  int nCell,
  RtreeNode *pLeft,
  RtreeNode *pRight,
  RtreeCell *pBboxLeft,
  RtreeCell *pBboxRight
){
  int iLeftSeed = 0;
  int iRightSeed = 1;
  int *aiUsed;
  int i;

  aiUsed = sqlite3_malloc(sizeof(int)*nCell);
  if( !aiUsed ){
    return SQLITE_NOMEM;
  }
  memset(aiUsed, 0, sizeof(int)*nCell);

  PickSeeds(pRtree, aCell, nCell, &iLeftSeed, &iRightSeed);

  memcpy(pBboxLeft, &aCell[iLeftSeed], sizeof(RtreeCell));
  memcpy(pBboxRight, &aCell[iRightSeed], sizeof(RtreeCell));
  nodeInsertCell(pRtree, pLeft, &aCell[iLeftSeed]);
  nodeInsertCell(pRtree, pRight, &aCell[iRightSeed]);
  aiUsed[iLeftSeed] = 1;
  aiUsed[iRightSeed] = 1;

  for(i=nCell-2; i>0; i--){
    RtreeCell *pNext;
    pNext = PickNext(pRtree, aCell, nCell, pBboxLeft, pBboxRight, aiUsed);
    float diff =  
      cellGrowth(pRtree, pBboxLeft, pNext) - 
      cellGrowth(pRtree, pBboxRight, pNext)
    ;
    if( (RTREE_MINCELLS(pRtree)-NCELL(pRight)==i)
     || (diff>0.0 && (RTREE_MINCELLS(pRtree)-NCELL(pLeft)!=i))
    ){
      nodeInsertCell(pRtree, pRight, pNext);
      cellUnion(pRtree, pBboxRight, pNext);
    }else{
      nodeInsertCell(pRtree, pLeft, pNext);
      cellUnion(pRtree, pBboxLeft, pNext);
    }
  }

  sqlite3_free(aiUsed);
  return SQLITE_OK;
}
#endif

static int updateMapping(
  Rtree *pRtree, 
  i64 iRowid, 
  RtreeNode *pNode, 
  int iHeight
){
  int (*xSetMapping)(Rtree *, sqlite3_int64, sqlite3_int64);
  xSetMapping = ((iHeight==0)?rowidWrite:parentWrite);
  if( iHeight>0 ){
    RtreeNode *pChild = nodeHashLookup(pRtree, iRowid);
    if( pChild ){
      nodeRelease(pRtree, pChild->pParent);
      nodeReference(pNode);
      pChild->pParent = pNode;
    }
  }
  return xSetMapping(pRtree, iRowid, pNode->iNode);
}

static int SplitNode(
  Rtree *pRtree,
  RtreeNode *pNode,
  RtreeCell *pCell,
  int iHeight
){
  int i;
  int newCellIsRight = 0;

  int rc = SQLITE_OK;
  int nCell = NCELL(pNode);
  RtreeCell *aCell;
  int *aiUsed;

  RtreeNode *pLeft = 0;
  RtreeNode *pRight = 0;

  RtreeCell leftbbox;
  RtreeCell rightbbox;

  /* Allocate an array and populate it with a copy of pCell and 
  ** all cells from node pLeft. Then zero the original node.
  */
  aCell = sqlite3_malloc((sizeof(RtreeCell)+sizeof(int))*(nCell+1));
  if( !aCell ){
    rc = SQLITE_NOMEM;
    goto splitnode_out;
  }
  aiUsed = (int *)&aCell[nCell+1];
  memset(aiUsed, 0, sizeof(int)*(nCell+1));
  for(i=0; i<nCell; i++){
    nodeGetCell(pRtree, pNode, i, &aCell[i]);
  }
  nodeZero(pRtree, pNode);
  memcpy(&aCell[nCell], pCell, sizeof(RtreeCell));
  nCell++;

  if( pNode->iNode==1 ){
    pRight = nodeNew(pRtree, pNode);
    pLeft = nodeNew(pRtree, pNode);
    pRtree->iDepth++;
    pNode->isDirty = 1;
    writeInt16(pNode->zData, pRtree->iDepth);
  }else{
    pLeft = pNode;
    pRight = nodeNew(pRtree, pLeft->pParent);
    nodeReference(pLeft);
  }

  if( !pLeft || !pRight ){
    rc = SQLITE_NOMEM;
    goto splitnode_out;
  }

  memset(pLeft->zData, 0, pRtree->iNodeSize);
  memset(pRight->zData, 0, pRtree->iNodeSize);

  rc = AssignCells(pRtree, aCell, nCell, pLeft, pRight, &leftbbox, &rightbbox);
  if( rc!=SQLITE_OK ){
    goto splitnode_out;
  }

  /* Ensure both child nodes have node numbers assigned to them by calling
  ** nodeWrite(). Node pRight always needs a node number, as it was created
  ** by nodeNew() above. But node pLeft sometimes already has a node number.
  ** In this case avoid the all to nodeWrite().
  */
  if( SQLITE_OK!=(rc = nodeWrite(pRtree, pRight))
   || (0==pLeft->iNode && SQLITE_OK!=(rc = nodeWrite(pRtree, pLeft)))
  ){
    goto splitnode_out;
  }

  rightbbox.iRowid = pRight->iNode;
  leftbbox.iRowid = pLeft->iNode;

  if( pNode->iNode==1 ){
    rc = rtreeInsertCell(pRtree, pLeft->pParent, &leftbbox, iHeight+1);
    if( rc!=SQLITE_OK ){
      goto splitnode_out;
    }
  }else{
    RtreeNode *pParent = pLeft->pParent;
    int iCell;
    rc = nodeParentIndex(pRtree, pLeft, &iCell);
    if( rc==SQLITE_OK ){
      nodeOverwriteCell(pRtree, pParent, &leftbbox, iCell);
      rc = AdjustTree(pRtree, pParent, &leftbbox);
    }
    if( rc!=SQLITE_OK ){
      goto splitnode_out;
    }
  }
  if( (rc = rtreeInsertCell(pRtree, pRight->pParent, &rightbbox, iHeight+1)) ){
    goto splitnode_out;
  }

  for(i=0; i<NCELL(pRight); i++){
    i64 iRowid = nodeGetRowid(pRtree, pRight, i);
    rc = updateMapping(pRtree, iRowid, pRight, iHeight);
    if( iRowid==pCell->iRowid ){
      newCellIsRight = 1;
    }
    if( rc!=SQLITE_OK ){
      goto splitnode_out;
    }
  }
  if( pNode->iNode==1 ){
    for(i=0; i<NCELL(pLeft); i++){
      i64 iRowid = nodeGetRowid(pRtree, pLeft, i);
      rc = updateMapping(pRtree, iRowid, pLeft, iHeight);
      if( rc!=SQLITE_OK ){
        goto splitnode_out;
      }
    }
  }else if( newCellIsRight==0 ){
    rc = updateMapping(pRtree, pCell->iRowid, pLeft, iHeight);
  }

  if( rc==SQLITE_OK ){
    rc = nodeRelease(pRtree, pRight);
    pRight = 0;
  }
  if( rc==SQLITE_OK ){
    rc = nodeRelease(pRtree, pLeft);
    pLeft = 0;
  }

splitnode_out:
  nodeRelease(pRtree, pRight);
  nodeRelease(pRtree, pLeft);
  sqlite3_free(aCell);
  return rc;
}

/*
** If node pLeaf is not the root of the r-tree and its pParent pointer is 
** still NULL, load all ancestor nodes of pLeaf into memory and populate
** the pLeaf->pParent chain all the way up to the root node.
**
** This operation is required when a row is deleted (or updated - an update
** is implemented as a delete followed by an insert). SQLite provides the
** rowid of the row to delete, which can be used to find the leaf on which
** the entry resides (argument pLeaf). Once the leaf is located, this 
** function is called to determine its ancestry.
*/
static int fixLeafParent(Rtree *pRtree, RtreeNode *pLeaf){
  int rc = SQLITE_OK;
  RtreeNode *pChild = pLeaf;
  while( rc==SQLITE_OK && pChild->iNode!=1 && pChild->pParent==0 ){
    int rc2 = SQLITE_OK;          /* sqlite3_reset() return code */
    sqlite3_bind_int64(pRtree->pReadParent, 1, pChild->iNode);
    rc = sqlite3_step(pRtree->pReadParent);
    if( rc==SQLITE_ROW ){
      RtreeNode *pTest;           /* Used to test for reference loops */
      i64 iNode;                  /* Node number of parent node */

      /* Before setting pChild->pParent, test that we are not creating a
      ** loop of references (as we would if, say, pChild==pParent). We don't
      ** want to do this as it leads to a memory leak when trying to delete
      ** the referenced counted node structures.
      */
      iNode = sqlite3_column_int64(pRtree->pReadParent, 0);
      for(pTest=pLeaf; pTest && pTest->iNode!=iNode; pTest=pTest->pParent);
      if( !pTest ){
        rc2 = nodeAcquire(pRtree, iNode, 0, &pChild->pParent);
      }
    }
    rc = sqlite3_reset(pRtree->pReadParent);
    if( rc==SQLITE_OK ) rc = rc2;
    if( rc==SQLITE_OK && !pChild->pParent ) rc = SQLITE_CORRUPT_VTAB;
    pChild = pChild->pParent;
  }
  return rc;
}

static int deleteCell(Rtree *, RtreeNode *, int, int);

static int removeNode(Rtree *pRtree, RtreeNode *pNode, int iHeight){
  int rc;
  int rc2;
  RtreeNode *pParent = 0;
  int iCell;

  assert( pNode->nRef==1 );

  /* Remove the entry in the parent cell. */
  rc = nodeParentIndex(pRtree, pNode, &iCell);
  if( rc==SQLITE_OK ){
    pParent = pNode->pParent;
    pNode->pParent = 0;
    rc = deleteCell(pRtree, pParent, iCell, iHeight+1);
  }
  rc2 = nodeRelease(pRtree, pParent);
  if( rc==SQLITE_OK ){
    rc = rc2;
  }
  if( rc!=SQLITE_OK ){
    return rc;
  }

  /* Remove the xxx_node entry. */
  sqlite3_bind_int64(pRtree->pDeleteNode, 1, pNode->iNode);
  sqlite3_step(pRtree->pDeleteNode);
  if( SQLITE_OK!=(rc = sqlite3_reset(pRtree->pDeleteNode)) ){
    return rc;
  }

  /* Remove the xxx_parent entry. */
  sqlite3_bind_int64(pRtree->pDeleteParent, 1, pNode->iNode);
  sqlite3_step(pRtree->pDeleteParent);
  if( SQLITE_OK!=(rc = sqlite3_reset(pRtree->pDeleteParent)) ){
    return rc;
  }
  
  /* Remove the node from the in-memory hash table and link it into
  ** the Rtree.pDeleted list. Its contents will be re-inserted later on.
  */
  nodeHashDelete(pRtree, pNode);
  pNode->iNode = iHeight;
  pNode->pNext = pRtree->pDeleted;
  pNode->nRef++;
  pRtree->pDeleted = pNode;

  return SQLITE_OK;
}

static int fixBoundingBox(Rtree *pRtree, RtreeNode *pNode){
  RtreeNode *pParent = pNode->pParent;
  int rc = SQLITE_OK; 
  if( pParent ){
    int ii; 
    int nCell = NCELL(pNode);
    RtreeCell box;                            /* Bounding box for pNode */
    nodeGetCell(pRtree, pNode, 0, &box);
    for(ii=1; ii<nCell; ii++){
      RtreeCell cell;
      nodeGetCell(pRtree, pNode, ii, &cell);
      cellUnion(pRtree, &box, &cell);
    }
    box.iRowid = pNode->iNode;
    rc = nodeParentIndex(pRtree, pNode, &ii);
    if( rc==SQLITE_OK ){
      nodeOverwriteCell(pRtree, pParent, &box, ii);
      rc = fixBoundingBox(pRtree, pParent);
    }
  }
  return rc;
}

/*
** Delete the cell at index iCell of node pNode. After removing the
** cell, adjust the r-tree data structure if required.
*/
static int deleteCell(Rtree *pRtree, RtreeNode *pNode, int iCell, int iHeight){
  RtreeNode *pParent;
  int rc;

  if( SQLITE_OK!=(rc = fixLeafParent(pRtree, pNode)) ){
    return rc;
  }

  /* Remove the cell from the node. This call just moves bytes around
  ** the in-memory node image, so it cannot fail.
  */
  nodeDeleteCell(pRtree, pNode, iCell);

  /* If the node is not the tree root and now has less than the minimum
  ** number of cells, remove it from the tree. Otherwise, update the
  ** cell in the parent node so that it tightly contains the updated
  ** node.
  */
  pParent = pNode->pParent;
  assert( pParent || pNode->iNode==1 );
  if( pParent ){
    if( NCELL(pNode)<RTREE_MINCELLS(pRtree) ){
      rc = removeNode(pRtree, pNode, iHeight);
    }else{
      rc = fixBoundingBox(pRtree, pNode);
    }
  }

  return rc;
}

static int Reinsert(
  Rtree *pRtree, 
  RtreeNode *pNode, 
  RtreeCell *pCell, 
  int iHeight
){
  int *aOrder;
  int *aSpare;
  RtreeCell *aCell;
  float *aDistance;
  int nCell;
  float aCenterCoord[RTREE_MAX_DIMENSIONS];
  int iDim;
  int ii;
  int rc = SQLITE_OK;

  memset(aCenterCoord, 0, sizeof(float)*RTREE_MAX_DIMENSIONS);

  nCell = NCELL(pNode)+1;

  /* Allocate the buffers used by this operation. The allocation is
  ** relinquished before this function returns.
  */
  aCell = (RtreeCell *)sqlite3_malloc(nCell * (
    sizeof(RtreeCell) +         /* aCell array */
    sizeof(int)       +         /* aOrder array */
    sizeof(int)       +         /* aSpare array */
    sizeof(float)               /* aDistance array */
  ));
  if( !aCell ){
    return SQLITE_NOMEM;
  }
  aOrder    = (int *)&aCell[nCell];
  aSpare    = (int *)&aOrder[nCell];
  aDistance = (float *)&aSpare[nCell];

  for(ii=0; ii<nCell; ii++){
    if( ii==(nCell-1) ){
      memcpy(&aCell[ii], pCell, sizeof(RtreeCell));
    }else{
      nodeGetCell(pRtree, pNode, ii, &aCell[ii]);
    }
    aOrder[ii] = ii;
    for(iDim=0; iDim<pRtree->nDim; iDim++){
      aCenterCoord[iDim] += (float)DCOORD(aCell[ii].aCoord[iDim*2]);
      aCenterCoord[iDim] += (float)DCOORD(aCell[ii].aCoord[iDim*2+1]);
    }
  }
  for(iDim=0; iDim<pRtree->nDim; iDim++){
    aCenterCoord[iDim] = (float)(aCenterCoord[iDim]/((float)nCell*2.0));
  }

  for(ii=0; ii<nCell; ii++){
    aDistance[ii] = 0.0;
    for(iDim=0; iDim<pRtree->nDim; iDim++){
      float coord = (float)(DCOORD(aCell[ii].aCoord[iDim*2+1]) - 
          DCOORD(aCell[ii].aCoord[iDim*2]));
      aDistance[ii] += (coord-aCenterCoord[iDim])*(coord-aCenterCoord[iDim]);
    }
  }

  SortByDistance(aOrder, nCell, aDistance, aSpare);
  nodeZero(pRtree, pNode);

  for(ii=0; rc==SQLITE_OK && ii<(nCell-(RTREE_MINCELLS(pRtree)+1)); ii++){
    RtreeCell *p = &aCell[aOrder[ii]];
    nodeInsertCell(pRtree, pNode, p);
    if( p->iRowid==pCell->iRowid ){
      if( iHeight==0 ){
        rc = rowidWrite(pRtree, p->iRowid, pNode->iNode);
      }else{
        rc = parentWrite(pRtree, p->iRowid, pNode->iNode);
      }
    }
  }
  if( rc==SQLITE_OK ){
    rc = fixBoundingBox(pRtree, pNode);
  }
  for(; rc==SQLITE_OK && ii<nCell; ii++){
    /* Find a node to store this cell in. pNode->iNode currently contains
    ** the height of the sub-tree headed by the cell.
    */
    RtreeNode *pInsert;
    RtreeCell *p = &aCell[aOrder[ii]];
    rc = ChooseLeaf(pRtree, p, iHeight, &pInsert);
    if( rc==SQLITE_OK ){
      int rc2;
      rc = rtreeInsertCell(pRtree, pInsert, p, iHeight);
      rc2 = nodeRelease(pRtree, pInsert);
      if( rc==SQLITE_OK ){
        rc = rc2;
      }
    }
  }

  sqlite3_free(aCell);
  return rc;
}

/*
** Insert cell pCell into node pNode. Node pNode is the head of a 
** subtree iHeight high (leaf nodes have iHeight==0).
*/
static int rtreeInsertCell(
  Rtree *pRtree,
  RtreeNode *pNode,
  RtreeCell *pCell,
  int iHeight
){
  int rc = SQLITE_OK;
  if( iHeight>0 ){
    RtreeNode *pChild = nodeHashLookup(pRtree, pCell->iRowid);
    if( pChild ){
      nodeRelease(pRtree, pChild->pParent);
      nodeReference(pNode);
      pChild->pParent = pNode;
    }
  }
  if( nodeInsertCell(pRtree, pNode, pCell) ){
#if VARIANT_RSTARTREE_REINSERT
    if( iHeight<=pRtree->iReinsertHeight || pNode->iNode==1){
      rc = SplitNode(pRtree, pNode, pCell, iHeight);
    }else{
      pRtree->iReinsertHeight = iHeight;
      rc = Reinsert(pRtree, pNode, pCell, iHeight);
    }
#else
    rc = SplitNode(pRtree, pNode, pCell, iHeight);
#endif
  }else{
    rc = AdjustTree(pRtree, pNode, pCell);
    if( rc==SQLITE_OK ){
      if( iHeight==0 ){
        rc = rowidWrite(pRtree, pCell->iRowid, pNode->iNode);
      }else{
        rc = parentWrite(pRtree, pCell->iRowid, pNode->iNode);
      }
    }
  }
  return rc;
}

static int reinsertNodeContent(Rtree *pRtree, RtreeNode *pNode){
  int ii;
  int rc = SQLITE_OK;
  int nCell = NCELL(pNode);

  for(ii=0; rc==SQLITE_OK && ii<nCell; ii++){
    RtreeNode *pInsert;
    RtreeCell cell;
    nodeGetCell(pRtree, pNode, ii, &cell);

    /* Find a node to store this cell in. pNode->iNode currently contains
    ** the height of the sub-tree headed by the cell.
    */
    rc = ChooseLeaf(pRtree, &cell, (int)pNode->iNode, &pInsert);
    if( rc==SQLITE_OK ){
      int rc2;
      rc = rtreeInsertCell(pRtree, pInsert, &cell, (int)pNode->iNode);
      rc2 = nodeRelease(pRtree, pInsert);
      if( rc==SQLITE_OK ){
        rc = rc2;
      }
    }
  }
  return rc;
}

/*
** Select a currently unused rowid for a new r-tree record.
*/
static int newRowid(Rtree *pRtree, i64 *piRowid){
  int rc;
  sqlite3_bind_null(pRtree->pWriteRowid, 1);
  sqlite3_bind_null(pRtree->pWriteRowid, 2);
  sqlite3_step(pRtree->pWriteRowid);
  rc = sqlite3_reset(pRtree->pWriteRowid);
  *piRowid = sqlite3_last_insert_rowid(pRtree->db);
  return rc;
}

/*
** Remove the entry with rowid=iDelete from the r-tree structure.
*/
static int rtreeDeleteRowid(Rtree *pRtree, sqlite3_int64 iDelete){
  int rc;                         /* Return code */
  RtreeNode *pLeaf;               /* Leaf node containing record iDelete */
  int iCell;                      /* Index of iDelete cell in pLeaf */
  RtreeNode *pRoot;               /* Root node of rtree structure */


  /* Obtain a reference to the root node to initialise Rtree.iDepth */
  rc = nodeAcquire(pRtree, 1, 0, &pRoot);

  /* Obtain a reference to the leaf node that contains the entry 
  ** about to be deleted. 
  */
  if( rc==SQLITE_OK ){
    rc = findLeafNode(pRtree, iDelete, &pLeaf);
  }

  /* Delete the cell in question from the leaf node. */
  if( rc==SQLITE_OK ){
    int rc2;
    rc = nodeRowidIndex(pRtree, pLeaf, iDelete, &iCell);
    if( rc==SQLITE_OK ){
      rc = deleteCell(pRtree, pLeaf, iCell, 0);
    }
    rc2 = nodeRelease(pRtree, pLeaf);
    if( rc==SQLITE_OK ){
      rc = rc2;
    }
  }

  /* Delete the corresponding entry in the <rtree>_rowid table. */
  if( rc==SQLITE_OK ){
    sqlite3_bind_int64(pRtree->pDeleteRowid, 1, iDelete);
    sqlite3_step(pRtree->pDeleteRowid);
    rc = sqlite3_reset(pRtree->pDeleteRowid);
  }

  /* Check if the root node now has exactly one child. If so, remove
  ** it, schedule the contents of the child for reinsertion and 
  ** reduce the tree height by one.
  **
  ** This is equivalent to copying the contents of the child into
  ** the root node (the operation that Gutman's paper says to perform 
  ** in this scenario).
  */
  if( rc==SQLITE_OK && pRtree->iDepth>0 && NCELL(pRoot)==1 ){
    int rc2;
    RtreeNode *pChild;
    i64 iChild = nodeGetRowid(pRtree, pRoot, 0);
    rc = nodeAcquire(pRtree, iChild, pRoot, &pChild);
    if( rc==SQLITE_OK ){
      rc = removeNode(pRtree, pChild, pRtree->iDepth-1);
    }
    rc2 = nodeRelease(pRtree, pChild);
    if( rc==SQLITE_OK ) rc = rc2;
    if( rc==SQLITE_OK ){
      pRtree->iDepth--;
      writeInt16(pRoot->zData, pRtree->iDepth);
      pRoot->isDirty = 1;
    }
  }

  /* Re-insert the contents of any underfull nodes removed from the tree. */
  for(pLeaf=pRtree->pDeleted; pLeaf; pLeaf=pRtree->pDeleted){
    if( rc==SQLITE_OK ){
      rc = reinsertNodeContent(pRtree, pLeaf);
    }
    pRtree->pDeleted = pLeaf->pNext;
    sqlite3_free(pLeaf);
  }

  /* Release the reference to the root node. */
  if( rc==SQLITE_OK ){
    rc = nodeRelease(pRtree, pRoot);
  }else{
    nodeRelease(pRtree, pRoot);
  }

  return rc;
}

/*
** The xUpdate method for rtree module virtual tables.
*/
static int rtreeUpdate(
  sqlite3_vtab *pVtab, 
  int nData, 
  sqlite3_value **azData, 
  sqlite_int64 *pRowid
){
  Rtree *pRtree = (Rtree *)pVtab;
  int rc = SQLITE_OK;
  RtreeCell cell;                 /* New cell to insert if nData>1 */
  int bHaveRowid = 0;             /* Set to 1 after new rowid is determined */

  rtreeReference(pRtree);
  assert(nData>=1);

  /* Constraint handling. A write operation on an r-tree table may return
  ** SQLITE_CONSTRAINT for two reasons:
  **
  **   1. A duplicate rowid value, or
  **   2. The supplied data violates the "x2>=x1" constraint.
  **
  ** In the first case, if the conflict-handling mode is REPLACE, then
  ** the conflicting row can be removed before proceeding. In the second
  ** case, SQLITE_CONSTRAINT must be returned regardless of the
  ** conflict-handling mode specified by the user.
  */
  if( nData>1 ){
    int ii;

    /* Populate the cell.aCoord[] array. The first coordinate is azData[3]. */
    assert( nData==(pRtree->nDim*2 + 3) );
    if( pRtree->eCoordType==RTREE_COORD_REAL32 ){
      for(ii=0; ii<(pRtree->nDim*2); ii+=2){
        cell.aCoord[ii].f = (float)sqlite3_value_double(azData[ii+3]);
        cell.aCoord[ii+1].f = (float)sqlite3_value_double(azData[ii+4]);
        if( cell.aCoord[ii].f>cell.aCoord[ii+1].f ){
          rc = SQLITE_CONSTRAINT;
          goto constraint;
        }
      }
    }else{
      for(ii=0; ii<(pRtree->nDim*2); ii+=2){
        cell.aCoord[ii].i = sqlite3_value_int(azData[ii+3]);
        cell.aCoord[ii+1].i = sqlite3_value_int(azData[ii+4]);
        if( cell.aCoord[ii].i>cell.aCoord[ii+1].i ){
          rc = SQLITE_CONSTRAINT;
          goto constraint;
        }
      }
    }

    /* If a rowid value was supplied, check if it is already present in 
    ** the table. If so, the constraint has failed. */
    if( sqlite3_value_type(azData[2])!=SQLITE_NULL ){
      cell.iRowid = sqlite3_value_int64(azData[2]);
      if( sqlite3_value_type(azData[0])==SQLITE_NULL
       || sqlite3_value_int64(azData[0])!=cell.iRowid
      ){
        int steprc;
        sqlite3_bind_int64(pRtree->pReadRowid, 1, cell.iRowid);
        steprc = sqlite3_step(pRtree->pReadRowid);
        rc = sqlite3_reset(pRtree->pReadRowid);
        if( SQLITE_ROW==steprc ){
          if( sqlite3_vtab_on_conflict(pRtree->db)==SQLITE_REPLACE ){
            rc = rtreeDeleteRowid(pRtree, cell.iRowid);
          }else{
            rc = SQLITE_CONSTRAINT;
            goto constraint;
          }
        }
      }
      bHaveRowid = 1;
    }
  }

  /* If azData[0] is not an SQL NULL value, it is the rowid of a
  ** record to delete from the r-tree table. The following block does
  ** just that.
  */
  if( sqlite3_value_type(azData[0])!=SQLITE_NULL ){
    rc = rtreeDeleteRowid(pRtree, sqlite3_value_int64(azData[0]));
  }

  /* If the azData[] array contains more than one element, elements
  ** (azData[2]..azData[argc-1]) contain a new record to insert into
  ** the r-tree structure.
  */
  if( rc==SQLITE_OK && nData>1 ){
    /* Insert the new record into the r-tree */
    RtreeNode *pLeaf;

    /* Figure out the rowid of the new row. */
    if( bHaveRowid==0 ){
      rc = newRowid(pRtree, &cell.iRowid);
    }
    *pRowid = cell.iRowid;

    if( rc==SQLITE_OK ){
      rc = ChooseLeaf(pRtree, &cell, 0, &pLeaf);
    }
    if( rc==SQLITE_OK ){
      int rc2;
      pRtree->iReinsertHeight = -1;
      rc = rtreeInsertCell(pRtree, pLeaf, &cell, 0);
      rc2 = nodeRelease(pRtree, pLeaf);
      if( rc==SQLITE_OK ){
        rc = rc2;
      }
    }
  }

constraint:
  rtreeRelease(pRtree);
  return rc;
}

/*
** The xRename method for rtree module virtual tables.
*/
static int rtreeRename(sqlite3_vtab *pVtab, const char *zNewName){
  Rtree *pRtree = (Rtree *)pVtab;
  int rc = SQLITE_NOMEM;
  char *zSql = sqlite3_mprintf(
    "ALTER TABLE %Q.'%q_node'   RENAME TO \"%w_node\";"
    "ALTER TABLE %Q.'%q_parent' RENAME TO \"%w_parent\";"
    "ALTER TABLE %Q.'%q_rowid'  RENAME TO \"%w_rowid\";"
    , pRtree->zDb, pRtree->zName, zNewName 
    , pRtree->zDb, pRtree->zName, zNewName 
    , pRtree->zDb, pRtree->zName, zNewName
  );
  if( zSql ){
    rc = sqlite3_exec(pRtree->db, zSql, 0, 0, 0);
    sqlite3_free(zSql);
  }
  return rc;
}

static sqlite3_module rtreeModule = {
  0,                          /* iVersion */
  rtreeCreate,                /* xCreate - create a table */
  rtreeConnect,               /* xConnect - connect to an existing table */
  rtreeBestIndex,             /* xBestIndex - Determine search strategy */
  rtreeDisconnect,            /* xDisconnect - Disconnect from a table */
  rtreeDestroy,               /* xDestroy - Drop a table */
  rtreeOpen,                  /* xOpen - open a cursor */
  rtreeClose,                 /* xClose - close a cursor */
  rtreeFilter,                /* xFilter - configure scan constraints */
  rtreeNext,                  /* xNext - advance a cursor */
  rtreeEof,                   /* xEof */
  rtreeColumn,                /* xColumn - read data */
  rtreeRowid,                 /* xRowid - read data */
  rtreeUpdate,                /* xUpdate - write data */
  0,                          /* xBegin - begin transaction */
  0,                          /* xSync - sync transaction */
  0,                          /* xCommit - commit transaction */
  0,                          /* xRollback - rollback transaction */
  0,                          /* xFindFunction - function overloading */
  rtreeRename,                /* xRename - rename the table */
  0,                          /* xSavepoint */
  0,                          /* xRelease */
  0                           /* xRollbackTo */
};

static int rtreeSqlInit(
  Rtree *pRtree, 
  sqlite3 *db, 
  const char *zDb, 
  const char *zPrefix, 
  int isCreate
){
  int rc = SQLITE_OK;

  #define N_STATEMENT 9
  static const char *azSql[N_STATEMENT] = {
    /* Read and write the xxx_node table */
    "SELECT data FROM '%q'.'%q_node' WHERE nodeno = :1",
    "INSERT OR REPLACE INTO '%q'.'%q_node' VALUES(:1, :2)",
    "DELETE FROM '%q'.'%q_node' WHERE nodeno = :1",

    /* Read and write the xxx_rowid table */
    "SELECT nodeno FROM '%q'.'%q_rowid' WHERE rowid = :1",
    "INSERT OR REPLACE INTO '%q'.'%q_rowid' VALUES(:1, :2)",
    "DELETE FROM '%q'.'%q_rowid' WHERE rowid = :1",

    /* Read and write the xxx_parent table */
    "SELECT parentnode FROM '%q'.'%q_parent' WHERE nodeno = :1",
    "INSERT OR REPLACE INTO '%q'.'%q_parent' VALUES(:1, :2)",
    "DELETE FROM '%q'.'%q_parent' WHERE nodeno = :1"
  };
  sqlite3_stmt **appStmt[N_STATEMENT];
  int i;

  pRtree->db = db;

  if( isCreate ){
    char *zCreate = sqlite3_mprintf(
"CREATE TABLE \"%w\".\"%w_node\"(nodeno INTEGER PRIMARY KEY, data BLOB);"
"CREATE TABLE \"%w\".\"%w_rowid\"(rowid INTEGER PRIMARY KEY, nodeno INTEGER);"
"CREATE TABLE \"%w\".\"%w_parent\"(nodeno INTEGER PRIMARY KEY, parentnode INTEGER);"
"INSERT INTO '%q'.'%q_node' VALUES(1, zeroblob(%d))",
      zDb, zPrefix, zDb, zPrefix, zDb, zPrefix, zDb, zPrefix, pRtree->iNodeSize
    );
    if( !zCreate ){
      return SQLITE_NOMEM;
    }
    rc = sqlite3_exec(db, zCreate, 0, 0, 0);
    sqlite3_free(zCreate);
    if( rc!=SQLITE_OK ){
      return rc;
    }
  }

  appStmt[0] = &pRtree->pReadNode;
  appStmt[1] = &pRtree->pWriteNode;
  appStmt[2] = &pRtree->pDeleteNode;
  appStmt[3] = &pRtree->pReadRowid;
  appStmt[4] = &pRtree->pWriteRowid;
  appStmt[5] = &pRtree->pDeleteRowid;
  appStmt[6] = &pRtree->pReadParent;
  appStmt[7] = &pRtree->pWriteParent;
  appStmt[8] = &pRtree->pDeleteParent;

  for(i=0; i<N_STATEMENT && rc==SQLITE_OK; i++){
    char *zSql = sqlite3_mprintf(azSql[i], zDb, zPrefix);
    if( zSql ){
      rc = sqlite3_prepare_v2(db, zSql, -1, appStmt[i], 0); 
    }else{
      rc = SQLITE_NOMEM;
    }
    sqlite3_free(zSql);
  }

  return rc;
}

/*
** The second argument to this function contains the text of an SQL statement
** that returns a single integer value. The statement is compiled and executed
** using database connection db. If successful, the integer value returned
** is written to *piVal and SQLITE_OK returned. Otherwise, an SQLite error
** code is returned and the value of *piVal after returning is not defined.
*/
static int getIntFromStmt(sqlite3 *db, const char *zSql, int *piVal){
  int rc = SQLITE_NOMEM;
  if( zSql ){
    sqlite3_stmt *pStmt = 0;
    rc = sqlite3_prepare_v2(db, zSql, -1, &pStmt, 0);
    if( rc==SQLITE_OK ){
      if( SQLITE_ROW==sqlite3_step(pStmt) ){
        *piVal = sqlite3_column_int(pStmt, 0);
      }
      rc = sqlite3_finalize(pStmt);
    }
  }
  return rc;
}

/*
** This function is called from within the xConnect() or xCreate() method to
** determine the node-size used by the rtree table being created or connected
** to. If successful, pRtree->iNodeSize is populated and SQLITE_OK returned.
** Otherwise, an SQLite error code is returned.
**
** If this function is being called as part of an xConnect(), then the rtree
** table already exists. In this case the node-size is determined by inspecting
** the root node of the tree.
**
** Otherwise, for an xCreate(), use 64 bytes less than the database page-size. 
** This ensures that each node is stored on a single database page. If the 
** database page-size is so large that more than RTREE_MAXCELLS entries 
** would fit in a single node, use a smaller node-size.
*/
static int getNodeSize(
  sqlite3 *db,                    /* Database handle */
  Rtree *pRtree,                  /* Rtree handle */
  int isCreate                    /* True for xCreate, false for xConnect */
){
  int rc;
  char *zSql;
  if( isCreate ){
    int iPageSize = 0;
    zSql = sqlite3_mprintf("PRAGMA %Q.page_size", pRtree->zDb);
    rc = getIntFromStmt(db, zSql, &iPageSize);
    if( rc==SQLITE_OK ){
      pRtree->iNodeSize = iPageSize-64;
      if( (4+pRtree->nBytesPerCell*RTREE_MAXCELLS)<pRtree->iNodeSize ){
        pRtree->iNodeSize = 4+pRtree->nBytesPerCell*RTREE_MAXCELLS;
      }
    }
  }else{
    zSql = sqlite3_mprintf(
        "SELECT length(data) FROM '%q'.'%q_node' WHERE nodeno = 1",
        pRtree->zDb, pRtree->zName
    );
    rc = getIntFromStmt(db, zSql, &pRtree->iNodeSize);
  }

  sqlite3_free(zSql);
  return rc;
}

/* 
** This function is the implementation of both the xConnect and xCreate
** methods of the r-tree virtual table.
**
**   argv[0]   -> module name
**   argv[1]   -> database name
**   argv[2]   -> table name
**   argv[...] -> column names...
*/
static int rtreeInit(
  sqlite3 *db,                        /* Database connection */
  void *pAux,                         /* One of the RTREE_COORD_* constants */
  int argc, const char *const*argv,   /* Parameters to CREATE TABLE statement */
  sqlite3_vtab **ppVtab,              /* OUT: New virtual table */
  char **pzErr,                       /* OUT: Error message, if any */
  int isCreate                        /* True for xCreate, false for xConnect */
){
  int rc = SQLITE_OK;
  Rtree *pRtree;
  int nDb;              /* Length of string argv[1] */
  int nName;            /* Length of string argv[2] */
  int eCoordType = (pAux ? RTREE_COORD_INT32 : RTREE_COORD_REAL32);

  const char *aErrMsg[] = {
    0,                                                    /* 0 */
    "Wrong number of columns for an rtree table",         /* 1 */
    "Too few columns for an rtree table",                 /* 2 */
    "Too many columns for an rtree table"                 /* 3 */
  };

  int iErr = (argc<6) ? 2 : argc>(RTREE_MAX_DIMENSIONS*2+4) ? 3 : argc%2;
  if( aErrMsg[iErr] ){
    *pzErr = sqlite3_mprintf("%s", aErrMsg[iErr]);
    return SQLITE_ERROR;
  }

  sqlite3_vtab_config(db, SQLITE_VTAB_CONSTRAINT_SUPPORT, 1);

  /* Allocate the sqlite3_vtab structure */
  nDb = strlen(argv[1]);
  nName = strlen(argv[2]);
  pRtree = (Rtree *)sqlite3_malloc(sizeof(Rtree)+nDb+nName+2);
  if( !pRtree ){
    return SQLITE_NOMEM;
  }
  memset(pRtree, 0, sizeof(Rtree)+nDb+nName+2);
  pRtree->nBusy = 1;
  pRtree->base.pModule = &rtreeModule;
  pRtree->zDb = (char *)&pRtree[1];
  pRtree->zName = &pRtree->zDb[nDb+1];
  pRtree->nDim = (argc-4)/2;
  pRtree->nBytesPerCell = 8 + pRtree->nDim*4*2;
  pRtree->eCoordType = eCoordType;
  memcpy(pRtree->zDb, argv[1], nDb);
  memcpy(pRtree->zName, argv[2], nName);

  /* Figure out the node size to use. */
  rc = getNodeSize(db, pRtree, isCreate);

  /* Create/Connect to the underlying relational database schema. If
  ** that is successful, call sqlite3_declare_vtab() to configure
  ** the r-tree table schema.
  */
  if( rc==SQLITE_OK ){
    if( (rc = rtreeSqlInit(pRtree, db, argv[1], argv[2], isCreate)) ){
      *pzErr = sqlite3_mprintf("%s", sqlite3_errmsg(db));
    }else{
      char *zSql = sqlite3_mprintf("CREATE TABLE x(%s", argv[3]);
      char *zTmp;
      int ii;
      for(ii=4; zSql && ii<argc; ii++){
        zTmp = zSql;
        zSql = sqlite3_mprintf("%s, %s", zTmp, argv[ii]);
        sqlite3_free(zTmp);
      }
      if( zSql ){
        zTmp = zSql;
        zSql = sqlite3_mprintf("%s);", zTmp);
        sqlite3_free(zTmp);
      }
      if( !zSql ){
        rc = SQLITE_NOMEM;
      }else if( SQLITE_OK!=(rc = sqlite3_declare_vtab(db, zSql)) ){
        *pzErr = sqlite3_mprintf("%s", sqlite3_errmsg(db));
      }
      sqlite3_free(zSql);
    }
  }

  if( rc==SQLITE_OK ){
    *ppVtab = (sqlite3_vtab *)pRtree;
  }else{
    rtreeRelease(pRtree);
  }
  return rc;
}


/*
** Implementation of a scalar function that decodes r-tree nodes to
** human readable strings. This can be used for debugging and analysis.
**
** The scalar function takes two arguments, a blob of data containing
** an r-tree node, and the number of dimensions the r-tree indexes.
** For a two-dimensional r-tree structure called "rt", to deserialize
** all nodes, a statement like:
**
**   SELECT rtreenode(2, data) FROM rt_node;
**
** The human readable string takes the form of a Tcl list with one
** entry for each cell in the r-tree node. Each entry is itself a
** list, containing the 8-byte rowid/pageno followed by the 
** <num-dimension>*2 coordinates.
*/
static void rtreenode(sqlite3_context *ctx, int nArg, sqlite3_value **apArg){
  char *zText = 0;
  RtreeNode node;
  Rtree tree;
  int ii;

  UNUSED_PARAMETER(nArg);
  memset(&node, 0, sizeof(RtreeNode));
  memset(&tree, 0, sizeof(Rtree));
  tree.nDim = sqlite3_value_int(apArg[0]);
  tree.nBytesPerCell = 8 + 8 * tree.nDim;
  node.zData = (u8 *)sqlite3_value_blob(apArg[1]);

  for(ii=0; ii<NCELL(&node); ii++){
    char zCell[512];
    int nCell = 0;
    RtreeCell cell;
    int jj;

    nodeGetCell(&tree, &node, ii, &cell);
    sqlite3_snprintf(512-nCell,&zCell[nCell],"%lld", cell.iRowid);
    nCell = strlen(zCell);
    for(jj=0; jj<tree.nDim*2; jj++){
      sqlite3_snprintf(512-nCell,&zCell[nCell]," %f",(double)cell.aCoord[jj].f);
      nCell = strlen(zCell);
    }

    if( zText ){
      char *zTextNew = sqlite3_mprintf("%s {%s}", zText, zCell);
      sqlite3_free(zText);
      zText = zTextNew;
    }else{
      zText = sqlite3_mprintf("{%s}", zCell);
    }
  }
  
  sqlite3_result_text(ctx, zText, -1, sqlite3_free);
}

static void rtreedepth(sqlite3_context *ctx, int nArg, sqlite3_value **apArg){
  UNUSED_PARAMETER(nArg);
  if( sqlite3_value_type(apArg[0])!=SQLITE_BLOB 
   || sqlite3_value_bytes(apArg[0])<2
  ){
    sqlite3_result_error(ctx, "Invalid argument to rtreedepth()", -1); 
  }else{
    u8 *zBlob = (u8 *)sqlite3_value_blob(apArg[0]);
    sqlite3_result_int(ctx, readInt16(zBlob));
  }
}

/*
** Register the r-tree module with database handle db. This creates the
** virtual table module "rtree" and the debugging/analysis scalar 
** function "rtreenode".
*/
SQLITE_PRIVATE int sqlite3RtreeInit(sqlite3 *db){
  const int utf8 = SQLITE_UTF8;
  int rc;

  rc = sqlite3_create_function(db, "rtreenode", 2, utf8, 0, rtreenode, 0, 0);
  if( rc==SQLITE_OK ){
    rc = sqlite3_create_function(db, "rtreedepth", 1, utf8, 0,rtreedepth, 0, 0);
  }
  if( rc==SQLITE_OK ){
    void *c = (void *)RTREE_COORD_REAL32;
    rc = sqlite3_create_module_v2(db, "rtree", &rtreeModule, c, 0);
  }
  if( rc==SQLITE_OK ){
    void *c = (void *)RTREE_COORD_INT32;
    rc = sqlite3_create_module_v2(db, "rtree_i32", &rtreeModule, c, 0);
  }

  return rc;
}

/*
** A version of sqlite3_free() that can be used as a callback. This is used
** in two places - as the destructor for the blob value returned by the
** invocation of a geometry function, and as the destructor for the geometry
** functions themselves.
*/
static void doSqlite3Free(void *p){
  sqlite3_free(p);
}

/*
** Each call to sqlite3_rtree_geometry_callback() creates an ordinary SQLite
** scalar user function. This C function is the callback used for all such
** registered SQL functions.
**
** The scalar user functions return a blob that is interpreted by r-tree
** table MATCH operators.
*/
static void geomCallback(sqlite3_context *ctx, int nArg, sqlite3_value **aArg){
  RtreeGeomCallback *pGeomCtx = (RtreeGeomCallback *)sqlite3_user_data(ctx);
  RtreeMatchArg *pBlob;
  int nBlob;

  nBlob = sizeof(RtreeMatchArg) + (nArg-1)*sizeof(double);
  pBlob = (RtreeMatchArg *)sqlite3_malloc(nBlob);
  if( !pBlob ){
    sqlite3_result_error_nomem(ctx);
  }else{
    int i;
    pBlob->magic = RTREE_GEOMETRY_MAGIC;
    pBlob->xGeom = pGeomCtx->xGeom;
    pBlob->pContext = pGeomCtx->pContext;
    pBlob->nParam = nArg;
    for(i=0; i<nArg; i++){
      pBlob->aParam[i] = sqlite3_value_double(aArg[i]);
    }
    sqlite3_result_blob(ctx, pBlob, nBlob, doSqlite3Free);
  }
}

/*
** Register a new geometry function for use with the r-tree MATCH operator.
*/
SQLITE_API int sqlite3_rtree_geometry_callback(
  sqlite3 *db,
  const char *zGeom,
  int (*xGeom)(sqlite3_rtree_geometry *, int, double *, int *),
  void *pContext
){
  RtreeGeomCallback *pGeomCtx;      /* Context object for new user-function */

  /* Allocate and populate the context object. */
  pGeomCtx = (RtreeGeomCallback *)sqlite3_malloc(sizeof(RtreeGeomCallback));
  if( !pGeomCtx ) return SQLITE_NOMEM;
  pGeomCtx->xGeom = xGeom;
  pGeomCtx->pContext = pContext;

  /* Create the new user-function. Register a destructor function to delete
  ** the context object when it is no longer required.  */
  return sqlite3_create_function_v2(db, zGeom, -1, SQLITE_ANY, 
      (void *)pGeomCtx, geomCallback, 0, 0, doSqlite3Free
  );
}

#if !SQLITE_CORE
SQLITE_API int sqlite3_extension_init(
  sqlite3 *db,
  char **pzErrMsg,
  const sqlite3_api_routines *pApi
){
  SQLITE_EXTENSION_INIT2(pApi)
  return sqlite3RtreeInit(db);
}
#endif

#endif

/************** End of rtree.c ***********************************************/
/************** Begin file icu.c *********************************************/
/*
** 2007 May 6
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** $Id: icu.c,v 1.7 2007/12/13 21:54:11 drh Exp $
**
** This file implements an integration between the ICU library 
** ("International Components for Unicode", an open-source library 
** for handling unicode data) and SQLite. The integration uses 
** ICU to provide the following to SQLite:
**
**   * An implementation of the SQL regexp() function (and hence REGEXP
**     operator) using the ICU uregex_XX() APIs.
**
**   * Implementations of the SQL scalar upper() and lower() functions
**     for case mapping.
**
**   * Integration of ICU and SQLite collation seqences.
**
**   * An implementation of the LIKE operator that uses ICU to 
**     provide case-independent matching.
*/

#if !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_ICU)

/* Include ICU headers */
#include <unicode/utypes.h>
#include <unicode/uregex.h>
#include <unicode/ustring.h>
#include <unicode/ucol.h>

/* #include <assert.h> */

#ifndef SQLITE_CORE
  SQLITE_EXTENSION_INIT1
#else
#endif

/*
** Maximum length (in bytes) of the pattern in a LIKE or GLOB
** operator.
*/
#ifndef SQLITE_MAX_LIKE_PATTERN_LENGTH
# define SQLITE_MAX_LIKE_PATTERN_LENGTH 50000
#endif

/*
** Version of sqlite3_free() that is always a function, never a macro.
*/
static void xFree(void *p){
  sqlite3_free(p);
}

/*
** Compare two UTF-8 strings for equality where the first string is
** a "LIKE" expression. Return true (1) if they are the same and 
** false (0) if they are different.
*/
static int icuLikeCompare(
  const uint8_t *zPattern,   /* LIKE pattern */
  const uint8_t *zString,    /* The UTF-8 string to compare against */
  const UChar32 uEsc         /* The escape character */
){
  static const int MATCH_ONE = (UChar32)'_';
  static const int MATCH_ALL = (UChar32)'%';

  int iPattern = 0;       /* Current byte index in zPattern */
  int iString = 0;        /* Current byte index in zString */

  int prevEscape = 0;     /* True if the previous character was uEsc */

  while( zPattern[iPattern]!=0 ){

    /* Read (and consume) the next character from the input pattern. */
    UChar32 uPattern;
    U8_NEXT_UNSAFE(zPattern, iPattern, uPattern);
    assert(uPattern!=0);

    /* There are now 4 possibilities:
    **
    **     1. uPattern is an unescaped match-all character "%",
    **     2. uPattern is an unescaped match-one character "_",
    **     3. uPattern is an unescaped escape character, or
    **     4. uPattern is to be handled as an ordinary character
    */
    if( !prevEscape && uPattern==MATCH_ALL ){
      /* Case 1. */
      uint8_t c;

      /* Skip any MATCH_ALL or MATCH_ONE characters that follow a
      ** MATCH_ALL. For each MATCH_ONE, skip one character in the 
      ** test string.
      */
      while( (c=zPattern[iPattern]) == MATCH_ALL || c == MATCH_ONE ){
        if( c==MATCH_ONE ){
          if( zString[iString]==0 ) return 0;
          U8_FWD_1_UNSAFE(zString, iString);
        }
        iPattern++;
      }

      if( zPattern[iPattern]==0 ) return 1;

      while( zString[iString] ){
        if( icuLikeCompare(&zPattern[iPattern], &zString[iString], uEsc) ){
          return 1;
        }
        U8_FWD_1_UNSAFE(zString, iString);
      }
      return 0;

    }else if( !prevEscape && uPattern==MATCH_ONE ){
      /* Case 2. */
      if( zString[iString]==0 ) return 0;
      U8_FWD_1_UNSAFE(zString, iString);

    }else if( !prevEscape && uPattern==uEsc){
      /* Case 3. */
      prevEscape = 1;

    }else{
      /* Case 4. */
      UChar32 uString;
      U8_NEXT_UNSAFE(zString, iString, uString);
      uString = u_foldCase(uString, U_FOLD_CASE_DEFAULT);
      uPattern = u_foldCase(uPattern, U_FOLD_CASE_DEFAULT);
      if( uString!=uPattern ){
        return 0;
      }
      prevEscape = 0;
    }
  }

  return zString[iString]==0;
}

/*
** Implementation of the like() SQL function.  This function implements
** the build-in LIKE operator.  The first argument to the function is the
** pattern and the second argument is the string.  So, the SQL statements:
**
**       A LIKE B
**
** is implemented as like(B, A). If there is an escape character E, 
**
**       A LIKE B ESCAPE E
**
** is mapped to like(B, A, E).
*/
static void icuLikeFunc(
  sqlite3_context *context, 
  int argc, 
  sqlite3_value **argv
){
  const unsigned char *zA = sqlite3_value_text(argv[0]);
  const unsigned char *zB = sqlite3_value_text(argv[1]);
  UChar32 uEsc = 0;

  /* Limit the length of the LIKE or GLOB pattern to avoid problems
  ** of deep recursion and N*N behavior in patternCompare().
  */
  if( sqlite3_value_bytes(argv[0])>SQLITE_MAX_LIKE_PATTERN_LENGTH ){
    sqlite3_result_error(context, "LIKE or GLOB pattern too complex", -1);
    return;
  }


  if( argc==3 ){
    /* The escape character string must consist of a single UTF-8 character.
    ** Otherwise, return an error.
    */
    int nE= sqlite3_value_bytes(argv[2]);
    const unsigned char *zE = sqlite3_value_text(argv[2]);
    int i = 0;
    if( zE==0 ) return;
    U8_NEXT(zE, i, nE, uEsc);
    if( i!=nE){
      sqlite3_result_error(context, 
          "ESCAPE expression must be a single character", -1);
      return;
    }
  }

  if( zA && zB ){
    sqlite3_result_int(context, icuLikeCompare(zA, zB, uEsc));
  }
}

/*
** This function is called when an ICU function called from within
** the implementation of an SQL scalar function returns an error.
**
** The scalar function context passed as the first argument is 
** loaded with an error message based on the following two args.
*/
static void icuFunctionError(
  sqlite3_context *pCtx,       /* SQLite scalar function context */
  const char *zName,           /* Name of ICU function that failed */
  UErrorCode e                 /* Error code returned by ICU function */
){
  char zBuf[128];
  sqlite3_snprintf(128, zBuf, "ICU error: %s(): %s", zName, u_errorName(e));
  zBuf[127] = '\0';
  sqlite3_result_error(pCtx, zBuf, -1);
}

/*
** Function to delete compiled regexp objects. Registered as
** a destructor function with sqlite3_set_auxdata().
*/
static void icuRegexpDelete(void *p){
  URegularExpression *pExpr = (URegularExpression *)p;
  uregex_close(pExpr);
}

/*
** Implementation of SQLite REGEXP operator. This scalar function takes
** two arguments. The first is a regular expression pattern to compile
** the second is a string to match against that pattern. If either 
** argument is an SQL NULL, then NULL Is returned. Otherwise, the result
** is 1 if the string matches the pattern, or 0 otherwise.
**
** SQLite maps the regexp() function to the regexp() operator such
** that the following two are equivalent:
**
**     zString REGEXP zPattern
**     regexp(zPattern, zString)
**
** Uses the following ICU regexp APIs:
**
**     uregex_open()
**     uregex_matches()
**     uregex_close()
*/
static void icuRegexpFunc(sqlite3_context *p, int nArg, sqlite3_value **apArg){
  UErrorCode status = U_ZERO_ERROR;
  URegularExpression *pExpr;
  UBool res;
  const UChar *zString = sqlite3_value_text16(apArg[1]);

  (void)nArg;  /* Unused parameter */

  /* If the left hand side of the regexp operator is NULL, 
  ** then the result is also NULL. 
  */
  if( !zString ){
    return;
  }

  pExpr = sqlite3_get_auxdata(p, 0);
  if( !pExpr ){
    const UChar *zPattern = sqlite3_value_text16(apArg[0]);
    if( !zPattern ){
      return;
    }
    pExpr = uregex_open(zPattern, -1, 0, 0, &status);

    if( U_SUCCESS(status) ){
      sqlite3_set_auxdata(p, 0, pExpr, icuRegexpDelete);
    }else{
      assert(!pExpr);
      icuFunctionError(p, "uregex_open", status);
      return;
    }
  }

  /* Configure the text that the regular expression operates on. */
  uregex_setText(pExpr, zString, -1, &status);
  if( !U_SUCCESS(status) ){
    icuFunctionError(p, "uregex_setText", status);
    return;
  }

  /* Attempt the match */
  res = uregex_matches(pExpr, 0, &status);
  if( !U_SUCCESS(status) ){
    icuFunctionError(p, "uregex_matches", status);
    return;
  }

  /* Set the text that the regular expression operates on to a NULL
  ** pointer. This is not really necessary, but it is tidier than 
  ** leaving the regular expression object configured with an invalid
  ** pointer after this function returns.
  */
  uregex_setText(pExpr, 0, 0, &status);

  /* Return 1 or 0. */
  sqlite3_result_int(p, res ? 1 : 0);
}

/*
** Implementations of scalar functions for case mapping - upper() and 
** lower(). Function upper() converts its input to upper-case (ABC).
** Function lower() converts to lower-case (abc).
**
** ICU provides two types of case mapping, "general" case mapping and
** "language specific". Refer to ICU documentation for the differences
** between the two.
**
** To utilise "general" case mapping, the upper() or lower() scalar 
** functions are invoked with one argument:
**
**     upper('ABC') -> 'abc'
**     lower('abc') -> 'ABC'
**
** To access ICU "language specific" case mapping, upper() or lower()
** should be invoked with two arguments. The second argument is the name
** of the locale to use. Passing an empty string ("") or SQL NULL value
** as the second argument is the same as invoking the 1 argument version
** of upper() or lower().
**
**     lower('I', 'en_us') -> 'i'
**     lower('I', 'tr_tr') -> 'ı' (small dotless i)
**
** http://www.icu-project.org/userguide/posix.html#case_mappings
*/
static void icuCaseFunc16(sqlite3_context *p, int nArg, sqlite3_value **apArg){
  const UChar *zInput;
  UChar *zOutput;
  int nInput;
  int nOutput;

  UErrorCode status = U_ZERO_ERROR;
  const char *zLocale = 0;

  assert(nArg==1 || nArg==2);
  if( nArg==2 ){
    zLocale = (const char *)sqlite3_value_text(apArg[1]);
  }

  zInput = sqlite3_value_text16(apArg[0]);
  if( !zInput ){
    return;
  }
  nInput = sqlite3_value_bytes16(apArg[0]);

  nOutput = nInput * 2 + 2;
  zOutput = sqlite3_malloc(nOutput);
  if( !zOutput ){
    return;
  }

  if( sqlite3_user_data(p) ){
    u_strToUpper(zOutput, nOutput/2, zInput, nInput/2, zLocale, &status);
  }else{
    u_strToLower(zOutput, nOutput/2, zInput, nInput/2, zLocale, &status);
  }

  if( !U_SUCCESS(status) ){
    icuFunctionError(p, "u_strToLower()/u_strToUpper", status);
    return;
  }

  sqlite3_result_text16(p, zOutput, -1, xFree);
}

/*
** Collation sequence destructor function. The pCtx argument points to
** a UCollator structure previously allocated using ucol_open().
*/
static void icuCollationDel(void *pCtx){
  UCollator *p = (UCollator *)pCtx;
  ucol_close(p);
}

/*
** Collation sequence comparison function. The pCtx argument points to
** a UCollator structure previously allocated using ucol_open().
*/
static int icuCollationColl(
  void *pCtx,
  int nLeft,
  const void *zLeft,
  int nRight,
  const void *zRight
){
  UCollationResult res;
  UCollator *p = (UCollator *)pCtx;
  res = ucol_strcoll(p, (UChar *)zLeft, nLeft/2, (UChar *)zRight, nRight/2);
  switch( res ){
    case UCOL_LESS:    return -1;
    case UCOL_GREATER: return +1;
    case UCOL_EQUAL:   return 0;
  }
  assert(!"Unexpected return value from ucol_strcoll()");
  return 0;
}

/*
** Implementation of the scalar function icu_load_collation().
**
** This scalar function is used to add ICU collation based collation 
** types to an SQLite database connection. It is intended to be called
** as follows:
**
**     SELECT icu_load_collation(<locale>, <collation-name>);
**
** Where <locale> is a string containing an ICU locale identifier (i.e.
** "en_AU", "tr_TR" etc.) and <collation-name> is the name of the
** collation sequence to create.
*/
static void icuLoadCollation(
  sqlite3_context *p, 
  int nArg, 
  sqlite3_value **apArg
){
  sqlite3 *db = (sqlite3 *)sqlite3_user_data(p);
  UErrorCode status = U_ZERO_ERROR;
  const char *zLocale;      /* Locale identifier - (eg. "jp_JP") */
  const char *zName;        /* SQL Collation sequence name (eg. "japanese") */
  UCollator *pUCollator;    /* ICU library collation object */
  int rc;                   /* Return code from sqlite3_create_collation_x() */

  assert(nArg==2);
  zLocale = (const char *)sqlite3_value_text(apArg[0]);
  zName = (const char *)sqlite3_value_text(apArg[1]);

  if( !zLocale || !zName ){
    return;
  }

  pUCollator = ucol_open(zLocale, &status);
  if( !U_SUCCESS(status) ){
    icuFunctionError(p, "ucol_open", status);
    return;
  }
  assert(p);

  rc = sqlite3_create_collation_v2(db, zName, SQLITE_UTF16, (void *)pUCollator, 
      icuCollationColl, icuCollationDel
  );
  if( rc!=SQLITE_OK ){
    ucol_close(pUCollator);
    sqlite3_result_error(p, "Error registering collation function", -1);
  }
}

/*
** Register the ICU extension functions with database db.
*/
SQLITE_PRIVATE int sqlite3IcuInit(sqlite3 *db){
  struct IcuScalar {
    const char *zName;                        /* Function name */
    int nArg;                                 /* Number of arguments */
    int enc;                                  /* Optimal text encoding */
    void *pContext;                           /* sqlite3_user_data() context */
    void (*xFunc)(sqlite3_context*,int,sqlite3_value**);
  } scalars[] = {
    {"regexp", 2, SQLITE_ANY,          0, icuRegexpFunc},

    {"lower",  1, SQLITE_UTF16,        0, icuCaseFunc16},
    {"lower",  2, SQLITE_UTF16,        0, icuCaseFunc16},
    {"upper",  1, SQLITE_UTF16, (void*)1, icuCaseFunc16},
    {"upper",  2, SQLITE_UTF16, (void*)1, icuCaseFunc16},

    {"lower",  1, SQLITE_UTF8,         0, icuCaseFunc16},
    {"lower",  2, SQLITE_UTF8,         0, icuCaseFunc16},
    {"upper",  1, SQLITE_UTF8,  (void*)1, icuCaseFunc16},
    {"upper",  2, SQLITE_UTF8,  (void*)1, icuCaseFunc16},

    {"like",   2, SQLITE_UTF8,         0, icuLikeFunc},
    {"like",   3, SQLITE_UTF8,         0, icuLikeFunc},

    {"icu_load_collation",  2, SQLITE_UTF8, (void*)db, icuLoadCollation},
  };

  int rc = SQLITE_OK;
  int i;

  for(i=0; rc==SQLITE_OK && i<(int)(sizeof(scalars)/sizeof(scalars[0])); i++){
    struct IcuScalar *p = &scalars[i];
    rc = sqlite3_create_function(
        db, p->zName, p->nArg, p->enc, p->pContext, p->xFunc, 0, 0
    );
  }

  return rc;
}

#if !SQLITE_CORE
SQLITE_API int sqlite3_extension_init(
  sqlite3 *db, 
  char **pzErrMsg,
  const sqlite3_api_routines *pApi
){
  SQLITE_EXTENSION_INIT2(pApi)
  return sqlite3IcuInit(db);
}
#endif

#endif

/************** End of icu.c *************************************************/
/************** Begin file fts3_icu.c ****************************************/
/*
** 2007 June 22
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This file implements a tokenizer for fts3 based on the ICU library.
*/
#if !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS3)
#ifdef SQLITE_ENABLE_ICU

/* #include <assert.h> */
/* #include <string.h> */

#include <unicode/ubrk.h>
/* #include <unicode/ucol.h> */
/* #include <unicode/ustring.h> */
#include <unicode/utf16.h>

typedef struct IcuTokenizer IcuTokenizer;
typedef struct IcuCursor IcuCursor;

struct IcuTokenizer {
  sqlite3_tokenizer base;
  char *zLocale;
};

struct IcuCursor {
  sqlite3_tokenizer_cursor base;

  UBreakIterator *pIter;      /* ICU break-iterator object */
  int nChar;                  /* Number of UChar elements in pInput */
  UChar *aChar;               /* Copy of input using utf-16 encoding */
  int *aOffset;               /* Offsets of each character in utf-8 input */

  int nBuffer;
  char *zBuffer;

  int iToken;
};

/*
** Create a new tokenizer instance.
*/
static int icuCreate(
  int argc,                            /* Number of entries in argv[] */
  const char * const *argv,            /* Tokenizer creation arguments */
  sqlite3_tokenizer **ppTokenizer      /* OUT: Created tokenizer */
){
  IcuTokenizer *p;
  int n = 0;

  if( argc>0 ){
    n = strlen(argv[0])+1;
  }
  p = (IcuTokenizer *)sqlite3_malloc(sizeof(IcuTokenizer)+n);
  if( !p ){
    return SQLITE_NOMEM;
  }
  memset(p, 0, sizeof(IcuTokenizer));

  if( n ){
    p->zLocale = (char *)&p[1];
    memcpy(p->zLocale, argv[0], n);
  }

  *ppTokenizer = (sqlite3_tokenizer *)p;

  return SQLITE_OK;
}

/*
** Destroy a tokenizer
*/
static int icuDestroy(sqlite3_tokenizer *pTokenizer){
  IcuTokenizer *p = (IcuTokenizer *)pTokenizer;
  sqlite3_free(p);
  return SQLITE_OK;
}

/*
** Prepare to begin tokenizing a particular string.  The input
** string to be tokenized is pInput[0..nBytes-1].  A cursor
** used to incrementally tokenize this string is returned in 
** *ppCursor.
*/
static int icuOpen(
  sqlite3_tokenizer *pTokenizer,         /* The tokenizer */
  const char *zInput,                    /* Input string */
  int nInput,                            /* Length of zInput in bytes */
  sqlite3_tokenizer_cursor **ppCursor    /* OUT: Tokenization cursor */
){
  IcuTokenizer *p = (IcuTokenizer *)pTokenizer;
  IcuCursor *pCsr;

  const int32_t opt = U_FOLD_CASE_DEFAULT;
  UErrorCode status = U_ZERO_ERROR;
  int nChar;

  UChar32 c;
  int iInput = 0;
  int iOut = 0;

  *ppCursor = 0;

  if( nInput<0 ){
    nInput = strlen(zInput);
  }
  nChar = nInput+1;
  pCsr = (IcuCursor *)sqlite3_malloc(
      sizeof(IcuCursor) +                /* IcuCursor */
      nChar * sizeof(UChar) +            /* IcuCursor.aChar[] */
      (nChar+1) * sizeof(int)            /* IcuCursor.aOffset[] */
  );
  if( !pCsr ){
    return SQLITE_NOMEM;
  }
  memset(pCsr, 0, sizeof(IcuCursor));
  pCsr->aChar = (UChar *)&pCsr[1];
  pCsr->aOffset = (int *)&pCsr->aChar[nChar];

  pCsr->aOffset[iOut] = iInput;
  U8_NEXT(zInput, iInput, nInput, c); 
  while( c>0 ){
    int isError = 0;
    c = u_foldCase(c, opt);
    U16_APPEND(pCsr->aChar, iOut, nChar, c, isError);
    if( isError ){
      sqlite3_free(pCsr);
      return SQLITE_ERROR;
    }
    pCsr->aOffset[iOut] = iInput;

    if( iInput<nInput ){
      U8_NEXT(zInput, iInput, nInput, c);
    }else{
      c = 0;
    }
  }

  pCsr->pIter = ubrk_open(UBRK_WORD, p->zLocale, pCsr->aChar, iOut, &status);
  if( !U_SUCCESS(status) ){
    sqlite3_free(pCsr);
    return SQLITE_ERROR;
  }
  pCsr->nChar = iOut;

  ubrk_first(pCsr->pIter);
  *ppCursor = (sqlite3_tokenizer_cursor *)pCsr;
  return SQLITE_OK;
}

/*
** Close a tokenization cursor previously opened by a call to icuOpen().
*/
static int icuClose(sqlite3_tokenizer_cursor *pCursor){
  IcuCursor *pCsr = (IcuCursor *)pCursor;
  ubrk_close(pCsr->pIter);
  sqlite3_free(pCsr->zBuffer);
  sqlite3_free(pCsr);
  return SQLITE_OK;
}

/*
** Extract the next token from a tokenization cursor.
*/
static int icuNext(
  sqlite3_tokenizer_cursor *pCursor,  /* Cursor returned by simpleOpen */
  const char **ppToken,               /* OUT: *ppToken is the token text */
  int *pnBytes,                       /* OUT: Number of bytes in token */
  int *piStartOffset,                 /* OUT: Starting offset of token */
  int *piEndOffset,                   /* OUT: Ending offset of token */
  int *piPosition                     /* OUT: Position integer of token */
){
  IcuCursor *pCsr = (IcuCursor *)pCursor;

  int iStart = 0;
  int iEnd = 0;
  int nByte = 0;

  while( iStart==iEnd ){
    UChar32 c;

    iStart = ubrk_current(pCsr->pIter);
    iEnd = ubrk_next(pCsr->pIter);
    if( iEnd==UBRK_DONE ){
      return SQLITE_DONE;
    }

    while( iStart<iEnd ){
      int iWhite = iStart;
      U8_NEXT(pCsr->aChar, iWhite, pCsr->nChar, c);
      if( u_isspace(c) ){
        iStart = iWhite;
      }else{
        break;
      }
    }
    assert(iStart<=iEnd);
  }

  do {
    UErrorCode status = U_ZERO_ERROR;
    if( nByte ){
      char *zNew = sqlite3_realloc(pCsr->zBuffer, nByte);
      if( !zNew ){
        return SQLITE_NOMEM;
      }
      pCsr->zBuffer = zNew;
      pCsr->nBuffer = nByte;
    }

    u_strToUTF8(
        pCsr->zBuffer, pCsr->nBuffer, &nByte,    /* Output vars */
        &pCsr->aChar[iStart], iEnd-iStart,       /* Input vars */
        &status                                  /* Output success/failure */
    );
  } while( nByte>pCsr->nBuffer );

  *ppToken = pCsr->zBuffer;
  *pnBytes = nByte;
  *piStartOffset = pCsr->aOffset[iStart];
  *piEndOffset = pCsr->aOffset[iEnd];
  *piPosition = pCsr->iToken++;

  return SQLITE_OK;
}

/*
** The set of routines that implement the simple tokenizer
*/
static const sqlite3_tokenizer_module icuTokenizerModule = {
  0,                           /* iVersion */
  icuCreate,                   /* xCreate  */
  icuDestroy,                  /* xCreate  */
  icuOpen,                     /* xOpen    */
  icuClose,                    /* xClose   */
  icuNext,                     /* xNext    */
};

/*
** Set *ppModule to point at the implementation of the ICU tokenizer.
*/
SQLITE_PRIVATE void sqlite3Fts3IcuTokenizerModule(
  sqlite3_tokenizer_module const**ppModule
){
  *ppModule = &icuTokenizerModule;
}

#endif /* defined(SQLITE_ENABLE_ICU) */
#endif /* !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS3) */

/************** End of fts3_icu.c ********************************************/
