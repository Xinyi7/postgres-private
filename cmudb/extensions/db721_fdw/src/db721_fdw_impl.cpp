// If you choose to use C++, read this very carefully:
// https://www.postgresql.org/docs/15/xfunc-c.html#EXTEND-CPP

#include "dog.h"
#include "json.hpp"
#include <iostream>
// clang-format off
extern "C" {
#include "../../../../src/include/postgres.h"
#include "../../../../src/include/fmgr.h"
#include "../../../../src/include/foreign/fdwapi.h"
#include "../../../../src/include/foreign/foreign.h"
#include "../../../../src/include/access/reloptions.h"
#include "../../../../src/include/commands/defrem.h"
#include "../../../../src/include/utils/rel.h"
#include "../../../../src/include/optimizer/pathnode.h"
#include "../../../../src/include/optimizer/restrictinfo.h"
#include "../../../../src/include/optimizer/planmain.h"
#include "../../../../src/include/utils/lsyscache.h"
#include "../../../../src/include/optimizer/optimizer.h"
#include "../../../../src/include/utils/builtins.h"
#include "../../../../src/include/executor/execExpr.h"
#include "../../../../src/include/nodes/makefuncs.h"
}
// clang-format on
using json = nlohmann::json;

class Db721_file{
public:
    char * filename;
    int f_oid;
    char * buf;
    json metadata;
    int block_idx;
    int tuple_idx;
    List * column_list;
    int num_block;
    int * column_idx_list;
    int * column_type_list; // type 0: str, 1: int, 2: float
    int remain_tuple;
};
static List * getColumns( List *targetColumnList , List *restrictInfoList, RelOptInfo *baserel);
extern "C" void db721_GetForeignRelSize(PlannerInfo *root, RelOptInfo *baserel,
                                      Oid foreigntableid) {
  // TODO(721): Write me!


  Dog terrier("Terrier");
  elog(LOG, "db721_GetForeignRelSize: %s", terrier.Bark().c_str());
}

extern "C" void db721_GetForeignPaths(PlannerInfo *root, RelOptInfo *baserel,
                                    Oid foreigntableid) {

  // TODO(721): Write me!
  Path *path = (Path *)create_foreignscan_path(root, baserel, NULL,              /* default pathtarget */
                                               baserel->rows,     /* rows */
                                               1,                 /* startup cost */
                                               1 + baserel->rows, /* total cost */
                                               NIL,               /* no pathkeys */
                                               NULL,              /* no required outer relids */
                                               NULL,              /* no fdw_outerpath */
                                               NIL);              /* no fdw_private */
  add_path(baserel, path);
}



extern "C" ForeignScan *
db721_GetForeignPlan(PlannerInfo *root, RelOptInfo *baserel, Oid foreigntableid,
                   ForeignPath *best_path, List *tlist, List *scan_clauses,
                   Plan *outer_plan) {
    List *targetColumnList = baserel->reltarget->exprs;
    List *restrictInfoList = baserel->baserestrictinfo;
    List *columnList = getColumns(targetColumnList , restrictInfoList, baserel);
    elog(LOG, "get plan columlist size:%d", list_length(columnList));
    auto foreignColumnList = list_make1(columnList);
    scan_clauses = extract_actual_clauses(scan_clauses, false);

    return make_foreignscan(tlist,
                            scan_clauses,
                            baserel->relid,
                            NIL, /* no expressions we will evaluate */
                            foreignColumnList, /* no private data */
                            NIL, /* no custom tlist; our scan tuple looks like tlist */
                            NIL, /* no quals we will recheck */
                            outer_plan);
}
static List * getColumns( List *targetColumnList , List *restrictInfoList, RelOptInfo *baserel){
    List *columnList = NIL;
    List * tmpColumnList = NIL;
    ListCell * restrictInfoCell = NULL;
    ListCell * targetColumnCell = NULL;
    foreach(targetColumnCell, targetColumnList)
    {
        Node *targetExpr = (Node *) lfirst(targetColumnCell);
        List *targetVarList = pull_var_clause(targetExpr,
                                        PVC_RECURSE_AGGREGATES |
                                        PVC_RECURSE_PLACEHOLDERS);

        tmpColumnList = list_union(tmpColumnList, targetVarList);
    }

    foreach(restrictInfoCell, restrictInfoList)
    {
        RestrictInfo *restrictInfo = (RestrictInfo *) lfirst(restrictInfoCell);
        Node *restrictClause = (Node *) restrictInfo->clause;

        // recursively pull up any columns used in the restriction clause
        List * clauseColumnList = pull_var_clause(restrictClause,
                                           PVC_RECURSE_AGGREGATES|
                                           PVC_RECURSE_PLACEHOLDERS);

        tmpColumnList = list_union(tmpColumnList, clauseColumnList);
    }


    for (int columnIndex = 1; columnIndex <= baserel->max_attr; columnIndex++)
    {
        Var *column = NULL;
        ListCell * tmpColumnCell = NULL;
        // look for this column in the needed column list
        foreach(tmpColumnCell, tmpColumnList)
        {
            Var *tmpColumn = (Var *) lfirst(tmpColumnCell);
            if (tmpColumn->varattno == columnIndex)
            {
                column = tmpColumn;
                break;
            }
        }

        if (column != NULL)
        {
            columnList = lappend(columnList, column);
        }
    }
    elog(LOG, "column list, columlist size:%d", list_length(columnList));
    return columnList;

}
extern "C" void db721_BeginForeignScan(ForeignScanState *node, int eflags) {                                
  // TODO(721): Write me!
    int f_id = RelationGetRelid(node->ss.ss_currentRelation);
    auto f_table = GetForeignTable(f_id);
    auto f_server = GetForeignServer(f_table->serverid);

    List *options_list = NULL;
    options_list = list_concat(options_list, f_table->options);
    options_list = list_concat(options_list, f_server->options);
    char *filename;
    ListCell *cell;
    auto file_state = (Db721_file *) palloc(sizeof(Db721_file));
    foreach(cell, options_list)
    {
        DefElem *def = (DefElem *) lfirst(cell);
        if (strcmp(def->defname, "filename") == 0) {
            filename = defGetString(def);
        }
    }


    file_state->filename = filename;

    file_state->f_oid = open(filename, O_RDONLY);
    int fsize = lseek(file_state->f_oid, 0, SEEK_END);
    lseek(file_state->f_oid, -4, SEEK_END);
    elog(LOG, "filename: %s", filename);
    int size;

    size_t read_size = read(file_state->f_oid, (char *) &size, 4);

    if (read_size < 0) {
        printf("json metadata size read invalid");
        return;
    }
    lseek(file_state->f_oid, -4 - size, SEEK_END);
    elog(LOG, "db721_BeginForeignScan: %d", size);

    char json_buf[size + 1] = "";
    json_buf[size] = '\0';
    read_size = read(file_state->f_oid, json_buf, size);
    if (read_size < 0) {
        printf("json metadata read invalid");
        return;
    }
    elog(LOG, "db721_BeginForeignScan read size: %d, json body: %s", read_size, json_buf);

    std::string json_str(json_buf);
    lseek(file_state->f_oid, 0, SEEK_SET);

    file_state->metadata  = json::parse(json_str);

    int max_per_block = int(file_state->metadata["Max Values Per Block"]);
    int tuple_size = 0;
    file_state->tuple_idx = max_per_block;
    file_state->block_idx = 0;
    auto foreign_scan = (ForeignScan *) node->ss.ps.plan;
    List * foreign_column_list = (List *) foreign_scan->fdw_private;
    List * columnList = (List *) linitial(foreign_column_list);

    elog(LOG, "begin scan, columlist size:%d", list_length(columnList));
    ListCell *columnCell = NULL;
    file_state->column_list = columnList;

    file_state->column_idx_list = (int*)palloc(list_length(columnList) * sizeof(int));
    file_state->column_type_list =  (int*)palloc(list_length(columnList) * sizeof(int));
    file_state->num_block = 0;
    file_state->remain_tuple = 0;

    foreach(columnCell, columnList){
        Var *column = (Var *) lfirst(columnCell);
        AttrNumber column_id = column->varattno;
        auto column_name = get_attname(f_id, column_id, false);
        auto type_str = file_state->metadata["Columns"][column_name]["type"].get<std::string>();


        if (type_str.compare("str") == 0){
            tuple_size += 32;
            file_state->column_type_list[column_id-1] = 0;
        }else if (type_str.compare( "int") == 0) {
            tuple_size += 4;
            file_state->column_type_list[column_id-1] = 1;
        }else if ( type_str.compare("float") == 0){
            tuple_size += 4;
            file_state->column_type_list[column_id-1] = 2;
        }

        if (file_state->num_block == 0){
            file_state->num_block = file_state->metadata["Columns"][column_name]["num_blocks"].get<int>();
        }
        if (file_state->remain_tuple == 0){
            file_state->remain_tuple = file_state->metadata["Columns"][column_name]["block_stats"][std::to_string(file_state->num_block-1)]["num"].get<int>();
        }
    }

    file_state->buf = (char *)palloc(tuple_size * max_per_block);
    node->fdw_state = file_state;
}

extern "C" TupleTableSlot *db721_IterateForeignScan(ForeignScanState *node) {
  // TODO(721): Write me!

  Db721_file * file_state = (Db721_file *) node->fdw_state;
  int max_per_block = int(file_state->metadata["Max Values Per Block"]);
  int f_id = RelationGetRelid(node->ss.ss_currentRelation);
    elog(LOG, "db721_IterateForeignScan tuple idx: %d, remain tuple:%d, block idx: %d, num_block:%d, max_per_block:%d",file_state->tuple_idx,
         file_state->remain_tuple, file_state->block_idx, file_state->num_block, max_per_block);

    if ((file_state->block_idx  != file_state->num_block) && (file_state->tuple_idx == max_per_block)){
      file_state->tuple_idx = 0;
      ListCell *columnCell = NULL;
      int buf_idx = 0;
      foreach(columnCell, file_state->column_list)
      {
          Var *column = (Var *) lfirst(columnCell);
          AttrNumber column_id = column->varattno;
          auto column_name = get_attname(f_id, column_id, false);
          auto type_str = file_state->metadata["Columns"][column_name]["type"].get<std::string>();
          int type_size = type_str.compare("str") == 0? 32:4;

          int start_index = file_state->metadata["Columns"][column_name]["start_offset"].get<int>();
          std::string block_index_str = std::to_string(file_state->block_idx);

          int read_size = type_size * file_state->metadata["Columns"][column_name]["block_stats"][block_index_str]["num"].get<int>();

          lseek(file_state->f_oid,  start_index + type_size * max_per_block * file_state->block_idx , SEEK_SET);
          read(file_state->f_oid, &(file_state->buf[buf_idx]), read_size);
          file_state->column_idx_list[column_id-1] = buf_idx;
          buf_idx += read_size;
      }
      file_state->block_idx++;
  }
    auto tupleSlot =  node->ss.ss_ScanTupleSlot;
    ExecClearTuple(tupleSlot);
  TupleDesc tupleDescriptor = tupleSlot->tts_tupleDescriptor;
  Datum *columnValues = tupleSlot->tts_values;
  bool *columnNulls = tupleSlot->tts_isnull;
  int columnCount = tupleDescriptor->natts;
  memset(columnValues, 0, columnCount * sizeof(Datum));
  memset(columnNulls, true, columnCount * sizeof(bool));
  if ((file_state->block_idx != file_state->num_block) || (file_state->tuple_idx != file_state->remain_tuple)) {
      file_state->tuple_idx ++;
      for (int i = 0; i < columnCount; i++) {
          switch (file_state->column_type_list[i]) {
              case 0:
                  if (file_state->buf[file_state->column_idx_list[i]] != '\0') {

                      columnValues[i] = CStringGetTextDatum(&(file_state->buf[file_state->column_idx_list[i]]));

                      columnNulls[i] = false;
                  }
                  file_state->column_idx_list[i] += 32;
                  break;
              case 1:
                  int int_value;

                  memcpy(&int_value, &file_state->buf[file_state->column_idx_list[i]], 4);
                  columnValues[i] =  Int32GetDatum(int_value);
                  file_state->column_idx_list[i] += 4;
                  columnNulls[i] = false;

                  break;
              case 2:
                  float float_value;
                  memcpy(&float_value, &file_state->buf[file_state->column_idx_list[i]], 4);

                  columnValues[i] =  Float4GetDatum(float_value);

                  file_state->column_idx_list[i] += 4;

                  columnNulls[i] = false;
                  break;

          }
      }
      ExecStoreVirtualTuple(tupleSlot);
  }



  return tupleSlot;

}

extern "C" void db721_ReScanForeignScan(ForeignScanState *node) {
  // TODO(721): Write me!

}

extern "C" void db721_EndForeignScan(ForeignScanState *node) {
  // TODO(721): Write me!
  Db721_file * file_state = (Db721_file *)node->fdw_state;
  close(file_state->f_oid);
  pfree(file_state->buf);
  pfree(file_state->column_idx_list);
  pfree(file_state->column_type_list);
  pfree(file_state);


}