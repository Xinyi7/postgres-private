// If you choose to use C++, read this very carefully:
// https://www.postgresql.org/docs/15/xfunc-c.html#EXTEND-CPP

#include "dog.h"

// clang-format off
extern "C" {
#include "../../../../src/include/postgres.h"
#include "../../../../src/include/fmgr.h"
#include "../../../../src/include/foreign/fdwapi.h"
}
// clang-format on

extern "C" void db721_GetForeignRelSize(PlannerInfo *root, RelOptInfo *baserel,
                                      Oid foreigntableid) {
  // TODO(721): Write me!
  Dog terrier("Terrier");
  elog(LOG, "db721_GetForeignRelSize: %s", terrier.Bark().c_str());
}

extern "C" void db721_GetForeignPaths(PlannerInfo *root, RelOptInfo *baserel,
                                    Oid foreigntableid) {
  // TODO(721): Write me!
  Dog scout("Scout");
  elog(LOG, "db721_GetForeignPaths: %s", scout.Bark().c_str());
}

extern "C" ForeignScan *
db721_GetForeignPlan(PlannerInfo *root, RelOptInfo *baserel, Oid foreigntableid,
                   ForeignPath *best_path, List *tlist, List *scan_clauses,
                   Plan *outer_plan) {
  // TODO(721): Write me!
  return nullptr;
}

extern "C" void db721_BeginForeignScan(ForeignScanState *node, int eflags) {
  // TODO(721): Write me!
}

extern "C" TupleTableSlot *db721_IterateForeignScan(ForeignScanState *node) {
  // TODO(721): Write me!
  return nullptr;
}

extern "C" void db721_ReScanForeignScan(ForeignScanState *node) {
  // TODO(721): Write me!
}

extern "C" void db721_EndForeignScan(ForeignScanState *node) {
  // TODO(721): Write me!
}