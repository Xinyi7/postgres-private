// TODO(WAN): 2023-01-21
//  CLion chokes on the include and type analysis unless includes use relative
//  paths, e.g., #include "../../../src/include/postgres.h" vs. #include
//  "postgres.h". CLion also chokes on forward declarations if includes are not
//  in a certain order. If someone finds a way around this or if CLion fixes
//  this, please let me know.

// Please read https://www.postgresql.org/docs/15/xfunc-c.html
// to understand how to write C extensions for PostgreSQL.
// Specifically, you should at least know about postgres.h,
// fmgr.h, PG_MODULE_MAGIC, and PG_FUNCTION_INFO_V1.

// clang-format off
#include "../../../../src/include/postgres.h"
#include "../../../../src/include/fmgr.h"
#include "../../../../src/include/foreign/fdwapi.h"
// clang-format on

Datum db721_fdw_handler(PG_FUNCTION_ARGS);

// clang-format off
// Mandatory FDW scan functions, see db721_fdw_handler() for details.
extern void db721_GetForeignRelSize(PlannerInfo *root, RelOptInfo *baserel, Oid foreigntableid);
extern void db721_GetForeignPaths(PlannerInfo *root, RelOptInfo *baserel, Oid foreigntableid);
extern ForeignScan *db721_GetForeignPlan(PlannerInfo *root, RelOptInfo *baserel, Oid foreigntableid, ForeignPath *best_path, List *tlist, List *scan_clauses, Plan *outer_plan);
extern void db721_BeginForeignScan(ForeignScanState *node, int eflags);
extern TupleTableSlot *db721_IterateForeignScan(ForeignScanState *node);
extern void db721_ReScanForeignScan(ForeignScanState *node);
extern void db721_EndForeignScan(ForeignScanState *node);
// clang-format on

PG_FUNCTION_INFO_V1(db721_fdw_handler);

Datum db721_fdw_handler(PG_FUNCTION_ARGS) {
  // See the documentation in the definition of FdwRoutine.
  // You can also find documentation online:
  // https://www.postgresql.org/docs/15/fdw-callbacks.html
  FdwRoutine *fdw_routine = makeNode(FdwRoutine);
  // Mandatory functions.
  fdw_routine->GetForeignRelSize = db721_GetForeignRelSize;
  fdw_routine->GetForeignPaths = db721_GetForeignPaths;
  fdw_routine->GetForeignPlan = db721_GetForeignPlan;
  fdw_routine->BeginForeignScan = db721_BeginForeignScan;
  fdw_routine->IterateForeignScan = db721_IterateForeignScan;
  fdw_routine->ReScanForeignScan = db721_ReScanForeignScan;
  fdw_routine->EndForeignScan = db721_EndForeignScan;
  PG_RETURN_POINTER(fdw_routine);
}

PG_MODULE_MAGIC;
