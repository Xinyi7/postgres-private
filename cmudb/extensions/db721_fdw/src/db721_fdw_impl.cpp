// If you choose to use C++, read this very carefully:
// https://www.postgresql.org/docs/15/xfunc-c.html#EXTEND-CPP

#include "dog.h"
#include "json.hpp"
#include <iostream>
// clang-format off
extern "C" {
#include "../../../../src/include/postgres.h"
#include "../../../../src/include/fmgr.h"
#include "../../../../src/include/optimizer/clauses.h"
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
#include "../../../../src/include/nodes/primnodes.h"

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
    int * column_idx_list_map;
    List * * restrict_info_list;
    bool return_null;
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

    elog(LOG, "column list, columlist size:%d", list_length(columnList));


    elog(LOG, "get plan columlist size:%d", list_length(columnList));
    auto foreignColumnList = list_make3(columnList, restrictInfoList, baserel);
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
        tmpColumnCell = NULL;
        if (column == NULL){
            foreach(tmpColumnCell, targetColumnList){
                Var *tmpColumn = (Var *) lfirst(tmpColumnCell);
                if (tmpColumn->varattno == columnIndex)
                {
                    column = tmpColumn;
                    break;
                }
            }
        }

        if (column != NULL)
        {
            columnList = lappend(columnList, column);
        }
    }
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
    char *filename = NULL;
    ListCell *cell;
    Db721_file * file_state = (Db721_file *) palloc(sizeof(Db721_file));

    foreach(cell, options_list)
    {
        DefElem *def = (DefElem *) lfirst(cell);
        if (strcmp(def->defname, "filename") == 0) {
            filename = defGetString(def);
        }
    }


    file_state->filename = filename;

    file_state->f_oid = open(filename, O_RDONLY);
    lseek(file_state->f_oid, -4, SEEK_END);
    elog(LOG, "filename: %s", filename);
    int size;

    size_t read_size = read(file_state->f_oid, (char *) &size, 4);

    if (read_size < 0) {
        elog(ERROR, "json metadata size read invalid");
        return;
    }
    lseek(file_state->f_oid, -4 - size, SEEK_END);
    elog(LOG, "db721_BeginForeignScan: %d", size);

    char json_buf[size + 1] = "";
    json_buf[size] = '\0';
    read_size = read(file_state->f_oid, json_buf, size);
    if (read_size < 0) {
        elog(ERROR, "json metadata read invalid");
        return;
    }
    elog(LOG, "db721_BeginForeignScan read size: %lu, json body: %s", read_size, json_buf);

    std::string json_str(json_buf);
    lseek(file_state->f_oid, 0, SEEK_SET);

    file_state->metadata  = json::parse(json_str);
    file_state->return_null = false;

    int max_per_block = int(file_state->metadata["Max Values Per Block"]);
    int tuple_size = 0;
    file_state->tuple_idx = max_per_block;
    file_state->block_idx = 0;
    auto foreign_scan = (ForeignScan *) node->ss.ps.plan;
    List * foreign_column_list = (List *) foreign_scan->fdw_private;
    List * columnList = (List *) linitial(foreign_column_list);
    List * restrictInfoList = (List *)  lsecond(foreign_column_list);
    RelOptInfo *baserel = (RelOptInfo *) lthird(foreign_column_list);


    elog(LOG, "begin scan, columlist size:%d", list_length(columnList));
    ListCell *columnCell = NULL;
    file_state->column_list = columnList;

    file_state->column_idx_list = (int*)palloc(list_length(columnList) * sizeof(int));
    file_state->column_type_list =  (int*)palloc(list_length(columnList) * sizeof(int));
    file_state->num_block = 0;
    file_state->remain_tuple = 0;
    int num_columns = file_state->metadata["Columns"].size();
    file_state->column_idx_list_map = (int*) palloc( num_columns *  sizeof(int));

    int new_column_idx = 0;
    foreach(columnCell, columnList){
        Var *column = (Var *) lfirst(columnCell);
        AttrNumber column_id = column->varattno;
        auto column_name = get_attname(f_id, column_id, false);
        auto type_str = file_state->metadata["Columns"][column_name]["type"].get<std::string>();

        file_state->column_idx_list_map[column_id-1] = new_column_idx;
        if (type_str.compare("str") == 0){
            tuple_size += 32;
            file_state->column_type_list[new_column_idx] = 0;
        }else if (type_str.compare( "int") == 0) {
            tuple_size += 4;
            file_state->column_type_list[new_column_idx] = 1;
        }else if ( type_str.compare("float") == 0){
            tuple_size += 4;
            file_state->column_type_list[new_column_idx] = 2;
        }

        if (file_state->num_block == 0){
            file_state->num_block = file_state->metadata["Columns"][column_name]["num_blocks"].get<int>();
        }
        if (file_state->remain_tuple == 0){
            file_state->remain_tuple = file_state->metadata["Columns"][column_name]["block_stats"][std::to_string(file_state->num_block-1)]["num"].get<int>();
        }
        new_column_idx++;
    }




    ListCell * restrictInfoCell = NULL;


    file_state->restrict_info_list = (List * *) palloc(list_length(columnList) * sizeof(List *));
    for (int i = 0; i < list_length(columnList); i++){
        file_state->restrict_info_list[i] = NIL;
    }
    elog(LOG, "268 length of restrictInfoList: %d", list_length(restrictInfoList));
    List * deletedList = NIL;
    foreach(restrictInfoCell, restrictInfoList)
    {
        RestrictInfo *restrictInfo = (RestrictInfo *) lfirst(restrictInfoCell);
        Expr *clause = restrictInfo->clause;

        if (IsA(clause, OpExpr)) {
            OpExpr *ope_expr = (OpExpr *) clause;
            if (list_length(ope_expr->args) == 1) {
                deletedList = lappend(deletedList, restrictInfoCell);
                continue;
            }
            if (IsA(linitial(ope_expr->args), Const) && IsA(lsecond(ope_expr->args), Const)){

                Const * constant_1 = (Const *) linitial(ope_expr->args);
                Const * constant_2 = (Const *) lsecond(ope_expr->args);
                char * op_name = get_opname(ope_expr->opno);
                if (strcmp(op_name, "=")== 0){
                    switch (constant_1->consttype){
                        case INT4OID:
                            if (DatumGetInt32(constant_1->constvalue) != DatumGetInt32(constant_2->constvalue)){
                                file_state->return_null=true;
                            }
                            break;
                        case FLOAT4OID:
                            if (DatumGetFloat4(constant_1->constvalue) != DatumGetFloat4(constant_2->constvalue)){
                                file_state->return_null=true;
                            }
                            break;
                        case TEXTOID:
                            if (strcmp(DatumGetCString(constant_1->constvalue),DatumGetCString(constant_2->constvalue))!= 0){
                                file_state->return_null=true;
                            }
                            break;
                    }
                }else if (strcmp(op_name, "<>")== 0){
                    switch (constant_1->consttype){
                        case INT4OID:
                            if (DatumGetInt32(constant_1->constvalue) == DatumGetInt32(constant_2->constvalue)){
                                file_state->return_null=true;
                            }
                            break;
                        case FLOAT4OID:
                            if (DatumGetFloat4(constant_1->constvalue) == DatumGetFloat4(constant_2->constvalue)){
                                file_state->return_null=true;
                            }
                            break;
                        case TEXTOID:
                            if (strcmp(DatumGetCString(constant_1->constvalue),DatumGetCString(constant_2->constvalue))== 0){
                                file_state->return_null=true;
                            }
                            break;
                    }
                }else if (strcmp(op_name, ">=")== 0){
                    switch (constant_1->consttype){
                        case INT4OID:
                            if (DatumGetInt32(constant_1->constvalue) < DatumGetInt32(constant_2->constvalue)){
                                file_state->return_null=true;
                            }
                            break;
                        case FLOAT4OID:
                            if (DatumGetFloat4(constant_1->constvalue) < DatumGetFloat4(constant_2->constvalue)){
                                file_state->return_null=true;
                            }
                            break;
                        case TEXTOID:
                            if (strcmp(DatumGetCString(constant_1->constvalue),DatumGetCString(constant_2->constvalue)) < 0){
                                file_state->return_null=true;
                            }
                            break;
                    }
                }else if (strcmp(op_name, "<")== 0){
                    switch (constant_1->consttype){
                        case INT4OID:
                            if (DatumGetInt32(constant_1->constvalue) >= DatumGetInt32(constant_2->constvalue)){
                                file_state->return_null=true;
                            }
                            break;
                        case FLOAT4OID:
                            if (DatumGetFloat4(constant_1->constvalue) >= DatumGetFloat4(constant_2->constvalue)){
                                file_state->return_null=true;
                            }
                            break;
                        case TEXTOID:
                            if (strcmp(DatumGetCString(constant_1->constvalue),DatumGetCString(constant_2->constvalue)) >= 0){
                                file_state->return_null=true;
                            }
                            break;
                    }
                }else if (strcmp(op_name, ">")== 0){
                    switch (constant_1->consttype){
                        case INT4OID:
                            if (DatumGetInt32(constant_1->constvalue) <= DatumGetInt32(constant_2->constvalue)){
                                file_state->return_null=true;
                            }
                            break;
                        case FLOAT4OID:
                            if (DatumGetFloat4(constant_1->constvalue) <= DatumGetFloat4(constant_2->constvalue)){
                                file_state->return_null=true;
                            }
                            break;
                        case TEXTOID:
                            if (strcmp(DatumGetCString(constant_1->constvalue),DatumGetCString(constant_2->constvalue)) <= 0){
                                file_state->return_null=true;
                            }
                            break;
                    }
                }else if (strcmp(op_name, "<=")== 0){
                    switch (constant_1->consttype){
                        case INT4OID:
                            if (DatumGetInt32(constant_1->constvalue) > DatumGetInt32(constant_2->constvalue)){
                                file_state->return_null=true;
                            }
                            break;
                        case FLOAT4OID:
                            if (DatumGetFloat4(constant_1->constvalue) > DatumGetFloat4(constant_2->constvalue)){
                                file_state->return_null=true;
                            }
                            break;
                        case TEXTOID:
                            if (strcmp(DatumGetCString(constant_1->constvalue),DatumGetCString(constant_2->constvalue)) > 0){
                                file_state->return_null=true;
                            }
                            break;
                    }
                }
                restrictInfoList = list_delete_cell(restrictInfoList, restrictInfoCell);
                if (file_state->return_null){
                    break;
                }

                continue;
            }

            elog(LOG, "op name :%d, %s", ope_expr->opno, get_opname(ope_expr->opno));
            if (IsA(linitial(ope_expr->args), Const) && IsA(lsecond(ope_expr->args), Var)) {
                CommuteOpExpr(ope_expr);
            }
            if (IsA(linitial(ope_expr->args), Var) && IsA(lsecond(ope_expr->args), Var)){
                deletedList = lappend(deletedList, restrictInfoCell);
                continue;
            }
            Var * left = (Var *) linitial(ope_expr->args);
            AttrNumber column_id = left->varattno;
            file_state->restrict_info_list[file_state->column_idx_list_map[column_id-1]] = lappend(
                    file_state->restrict_info_list[file_state->column_idx_list_map[column_id-1]],  restrictInfo);

        }
    }

    baserel->baserestrictinfo = deletedList;
    file_state->buf = (char *)palloc(tuple_size * max_per_block);
    node->fdw_state = file_state;
}

extern "C" TupleTableSlot *db721_IterateForeignScan(ForeignScanState *node) {
    Db721_file * file_state = (Db721_file *) node->fdw_state;
    int max_per_block = int(file_state->metadata["Max Values Per Block"]);
    int f_id = RelationGetRelid(node->ss.ss_currentRelation);

    elog(LOG, "db721_IterateForeignScan tuple idx: %d, remain tuple:%d, block idx: %d, num_block:%d, max_per_block:%d",file_state->tuple_idx,
         file_state->remain_tuple, file_state->block_idx, file_state->num_block, max_per_block);


    while (!file_state->return_null && (file_state->block_idx  != file_state->num_block) && (file_state->tuple_idx == max_per_block)){
        file_state->tuple_idx = 0;
        ListCell *columnCell = NULL;
        int buf_idx = 0;

        bool tuple_null = false;
        foreach(columnCell, file_state->column_list)
        {
            Var *column = (Var *) lfirst(columnCell);
            AttrNumber column_id = column->varattno;
            auto column_name = get_attname(f_id, column_id, false);
            int i = column_id-1;
            std::string block_index_str = std::to_string(file_state->block_idx);
            if (list_length(file_state->restrict_info_list[file_state->column_idx_list_map[i]]) != 0){

                List *restrictInfoList = file_state->restrict_info_list[column_id-1];
                ListCell * restrictInfoCell= NULL;
                foreach(restrictInfoCell, restrictInfoList)
                {

                    RestrictInfo *restrictInfo = (RestrictInfo *) lfirst(restrictInfoCell);

                    OpExpr *ope_expr = (OpExpr *) restrictInfo->clause;
                    Const *constant_2 = (Const *) lsecond(ope_expr->args);
                    char *op_name = get_opname(ope_expr->opno);
                    if (strcmp(op_name, "=")== 0) {
                        switch (file_state->column_type_list[file_state->column_idx_list_map[i]]) {
                            case 0: {
                                std::string block_min_str = file_state->metadata["Columns"][column_name]["block_stats"][
                                        std::to_string(file_state->block_idx)]["min"].get<std::string>();
                                std::string block_max_str = file_state->metadata["Columns"][column_name]["block_stats"][
                                        std::to_string(file_state->block_idx)]["max"].get<std::string>();
                                std::string constant_2_value_str = DatumGetCString(constant_2->constvalue);
                                if (constant_2_value_str.compare(block_max_str) > 0 ||
                                    constant_2_value_str.compare(block_min_str) < 0) {
                                    tuple_null = true;
                                }
                                break;
                            }
                            case 1: {
                                int block_min_int = file_state->metadata["Columns"][column_name]["block_stats"][
                                        std::to_string(file_state->block_idx)]["min"].get<int>();
                                int block_max_int = file_state->metadata["Columns"][column_name]["block_stats"][
                                        std::to_string(file_state->block_idx)]["max"].get<int>();
                                int constant_2_value_int = DatumGetInt32(constant_2->constvalue);
                                if (constant_2_value_int > block_max_int || constant_2_value_int < block_min_int) {
                                    tuple_null = true;
                                }
                                break;
                            }
                            case 2: {
                                float block_min = file_state->metadata["Columns"][column_name]["block_stats"][
                                        std::to_string(file_state->block_idx)]["min"].get<float>();
                                float block_max = file_state->metadata["Columns"][column_name]["block_stats"][
                                        std::to_string(file_state->block_idx)]["max"].get<float>();
                                float constant_2_value = DatumGetFloat4(constant_2->constvalue);
                                if (constant_2_value > block_max || constant_2_value < block_min) {
                                    tuple_null = true;
                                }
                                break;
                            }
                        }
                    } else if (strcmp(op_name, "<>")== 0) {
                        switch (file_state->column_type_list[file_state->column_idx_list_map[i]]) {
                            case 0: {
                                std::string block_min_str = file_state->metadata["Columns"][column_name]["block_stats"][
                                        std::to_string(file_state->block_idx)]["min"].get<std::string>();
                                std::string block_max_str = file_state->metadata["Columns"][column_name]["block_stats"][
                                        std::to_string(file_state->block_idx)]["max"].get<std::string>();
                                std::string constant_2_value_str = DatumGetCString(constant_2->constvalue);
                                if (constant_2_value_str.compare(block_max_str) == 0 &&
                                    constant_2_value_str.compare(block_min_str) == 0) {
                                    tuple_null = true;
                                }
                                break;
                            }
                            case 1: {
                                int block_min_int = file_state->metadata["Columns"][column_name]["block_stats"][
                                        std::to_string(file_state->block_idx)]["min"].get<int>();
                                int block_max_int = file_state->remain_tuple = file_state->metadata["Columns"][column_name]["block_stats"][
                                        std::to_string(file_state->block_idx)]["max"].get<int>();
                                int constant_2_value_int = DatumGetInt32(constant_2->constvalue);
                                if (constant_2_value_int == block_max_int && constant_2_value_int == block_min_int) {
                                    tuple_null = true;
                                }
                                break;
                            }
                            case 2: {
                                float block_min = file_state->metadata["Columns"][column_name]["block_stats"][
                                        std::to_string(file_state->block_idx)]["min"].get<float>();
                                float block_max = file_state->metadata["Columns"][column_name]["block_stats"][
                                        std::to_string(file_state->block_idx)]["max"].get<float>();
                                float constant_2_value = DatumGetFloat4(constant_2->constvalue);
                                if (constant_2_value == block_max && constant_2_value == block_min) {
                                    tuple_null = true;
                                }
                                break;
                            }
                        }
                    } else if (strcmp(op_name, ">=")== 0) {
                        switch (file_state->column_type_list[file_state->column_idx_list_map[i]]) {
                            case 0: {
                                std::string block_max_str = file_state->metadata["Columns"][column_name]["block_stats"][
                                        std::to_string(file_state->block_idx)]["max"].get<std::string>();
                                std::string constant_2_value_str = DatumGetCString(constant_2->constvalue);
                                if (block_max_str.compare(constant_2_value_str) < 0) {
                                    tuple_null = true;
                                }
                                break;
                            }
                            case 1: {
                                int block_max_int = file_state->metadata["Columns"][column_name]["block_stats"][
                                        std::to_string(file_state->block_idx)]["max"].get<int>();
                                int constant_2_value_int = DatumGetInt32(constant_2->constvalue);
                                if (block_max_int < constant_2_value_int) {
                                    tuple_null = true;
                                }
                                break;
                            }
                            case 2: {
                                float block_max = file_state->metadata["Columns"][column_name]["block_stats"][
                                        std::to_string(file_state->block_idx)]["max"].get<float>();
                                float constant_2_value = DatumGetFloat4(constant_2->constvalue);
                                if (block_max < constant_2_value) {
                                    tuple_null = true;
                                }
                                break;
                            }
                        }

                    } else if (strcmp(op_name, "<")== 0) {
                        switch (file_state->column_type_list[file_state->column_idx_list_map[i]]) {
                            case 0: {
                                std::string block_min_str = file_state->metadata["Columns"][column_name]["block_stats"][
                                        std::to_string(file_state->block_idx)]["min"].get<std::string>();
                                std::string constant_2_value_str = DatumGetCString(constant_2->constvalue);
                                if (block_min_str.compare(constant_2_value_str) >= 0) {
                                    tuple_null = true;
                                }
                                break;
                            }
                            case 1: {
                                int block_min_int = file_state->metadata["Columns"][column_name]["block_stats"][
                                        std::to_string(file_state->block_idx)]["min"].get<int>();
                                int constant_2_value_int = DatumGetInt32(constant_2->constvalue);
                                if (block_min_int >= constant_2_value_int) {
                                    tuple_null = true;
                                }
                                break;
                            }
                            case 2: {
                                float block_min = file_state->metadata["Columns"][column_name]["block_stats"][
                                        std::to_string(file_state->block_idx)]["min"].get<float>();
                                float constant_2_value = DatumGetFloat4(constant_2->constvalue);
                                if (block_min >= constant_2_value) {
                                    tuple_null = true;
                                }
                                break;
                            }
                        }
                    } else if (strcmp(op_name, ">")== 0) {
                        switch (file_state->column_type_list[file_state->column_idx_list_map[i]]) {
                            case 0: {
                                std::string block_max_str = file_state->metadata["Columns"][column_name]["block_stats"][
                                        std::to_string(file_state->block_idx)]["max"].get<std::string>();
                                std::string constant_2_value_str = DatumGetCString(constant_2->constvalue);
                                if (block_max_str.compare(constant_2_value_str) <= 0) {
                                    tuple_null = true;
                                }
                                break;
                            }
                            case 1: {
                                int block_max_int = file_state->metadata["Columns"][column_name]["block_stats"][
                                        std::to_string(file_state->block_idx)]["max"].get<int>();
                                int constant_2_value_int = DatumGetInt32(constant_2->constvalue);
                                if (block_max_int <= constant_2_value_int) {
                                    tuple_null = true;
                                }
                                break;
                            }
                            case 2: {
                                float block_max = file_state->metadata["Columns"][column_name]["block_stats"][
                                        std::to_string(file_state->block_idx)]["max"].get<float>();
                                float constant_2_value = DatumGetFloat4(constant_2->constvalue);
                                if (block_max <= constant_2_value) {
                                    tuple_null = true;
                                }
                                break;
                            }
                        }
                    } else if (strcmp(op_name, "<=")== 0) {
                        switch (file_state->column_type_list[file_state->column_idx_list_map[i]]) {
                            case 0: {
                                std::string block_min_str = file_state->metadata["Columns"][column_name]["block_stats"][
                                        std::to_string(file_state->block_idx)]["min"].get<std::string>();
                                std::string constant_2_value_str = DatumGetCString(constant_2->constvalue);
                                if (block_min_str.compare(constant_2_value_str) > 0) {
                                    tuple_null = true;
                                }
                                break;
                            }
                            case 1: {
                                int block_min_int = file_state->metadata["Columns"][column_name]["block_stats"][
                                        std::to_string(file_state->block_idx)]["min"].get<int>();
                                int constant_2_value_int = DatumGetInt32(constant_2->constvalue);
                                if (block_min_int > constant_2_value_int) {
                                    tuple_null = true;
                                }
                                break;
                            }
                            case 2: {
                                float block_min = file_state->metadata["Columns"][column_name]["block_stats"][
                                        std::to_string(file_state->block_idx)]["min"].get<float>();
                                float constant_2_value = DatumGetFloat4(constant_2->constvalue);
                                if (block_min > constant_2_value) {
                                    tuple_null = true;
                                }
                                break;
                            }
                        }
                    }

                    if (tuple_null){
                        break;
                    }

                }

            }
            if (tuple_null){
                break;
            }
        }
        if (tuple_null){
            file_state->block_idx++;
            continue;
        }

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
          file_state->column_idx_list[file_state->column_idx_list_map[column_id-1]] = buf_idx;
          buf_idx += read_size;
        }
        file_state->block_idx++;
        break;

    }

    auto tupleSlot =  node->ss.ss_ScanTupleSlot;
    ExecClearTuple(tupleSlot);
    TupleDesc tupleDescriptor = tupleSlot->tts_tupleDescriptor;
    Datum *columnValues = tupleSlot->tts_values;
    bool *columnNulls = tupleSlot->tts_isnull;
    int columnCount = tupleDescriptor->natts;
    memset(columnValues, 0, columnCount * sizeof(Datum));
    memset(columnNulls, true, columnCount * sizeof(bool));
    if (file_state->return_null){
      return tupleSlot;
    }
    List * target_list= node->ss.ps.plan->targetlist;

    while ((file_state->block_idx != file_state->num_block) || (file_state->tuple_idx < file_state->remain_tuple)) {
      file_state->tuple_idx ++;
      ListCell * tmp_column_cell = NULL;
      bool tuple_null = false;
      foreach(tmp_column_cell, target_list){
          TargetEntry *tmp_target = (TargetEntry *) lfirst(tmp_column_cell);
          if (tmp_target->resjunk){
              continue;
          }
          Var * tmp_column = (Var *) tmp_target->expr;
          int i = tmp_column->varattno-1;
          int inplace_idx = file_state->column_idx_list_map[i];

          bool filtered = list_length(file_state->restrict_info_list[inplace_idx]) != 0;
          switch (file_state->column_type_list[file_state->column_idx_list_map[i]]) {
              case 0:
                  if (file_state->buf[file_state->column_idx_list[file_state->column_idx_list_map[i]]] != '\0') {
                      char *string_value = &(file_state->buf[file_state->column_idx_list[i]]);
                      if (filtered) {
                          List *restrictInfoList = file_state->restrict_info_list[inplace_idx];
                          ListCell * restrictInfoCell= NULL;
                          foreach(restrictInfoCell, restrictInfoList)
                          {
                              RestrictInfo *restrictInfo = (RestrictInfo *) lfirst(restrictInfoCell);
                              OpExpr *ope_expr = (OpExpr *) restrictInfo->clause;
                              Const *constant_2 = (Const *) lsecond(ope_expr->args);
                              char *op_name = get_opname(ope_expr->opno);
                              if (strcmp(op_name, "=")== 0) {
                                  if (strcmp(string_value, DatumGetCString(constant_2->constvalue)) != 0) {
                                      tuple_null = true;
                                  }
                              } else if (strcmp(op_name, "<>")== 0) {
                                  if (strcmp(string_value, DatumGetCString(constant_2->constvalue)) == 0) {
                                      tuple_null = true;
                                  }
                              } else if (strcmp(op_name, ">=")== 0) {
                                  if (strcmp(string_value, DatumGetCString(constant_2->constvalue)) < 0) {
                                      tuple_null = true;
                                  }

                              } else if (strcmp(op_name, "<")== 0) {
                                  if (strcmp(string_value, DatumGetCString(constant_2->constvalue)) >= 0) {
                                      tuple_null = true;
                                  }
                              } else if (strcmp(op_name, ">")== 0) {
                                  if (strcmp(string_value, DatumGetCString(constant_2->constvalue)) <= 0) {
                                      tuple_null = true;
                                  }
                              } else if (strcmp(op_name, "<=")== 0) {
                                  if (strcmp(string_value, DatumGetCString(constant_2->constvalue)) > 0) {
                                      tuple_null = true;
                                  }
                              }

                          }
                      }
                      if (!tuple_null) {
                          columnValues[i] = CStringGetTextDatum(&(file_state->buf[file_state->column_idx_list[i]]));

                          columnNulls[i] = false;
                      }
                  }
                  file_state->column_idx_list[inplace_idx] += 32;
                  break;
              case 1:
                  int int_value;

                  memcpy(&int_value, &file_state->buf[file_state->column_idx_list[file_state->column_idx_list_map[i]]],
                         4);
                  file_state->column_idx_list[inplace_idx] += 4;

                  if (filtered) {
                      List *restrictInfoList = file_state->restrict_info_list[inplace_idx];
                      ListCell * restrictInfoCell= NULL;
                      foreach(restrictInfoCell, restrictInfoList)
                      {
                          RestrictInfo *restrictInfo = (RestrictInfo *) lfirst(restrictInfoCell);
                          OpExpr *ope_expr = (OpExpr *) restrictInfo->clause;
                          Const *constant_2 = (Const *) lsecond(ope_expr->args);
                          char *op_name = get_opname(ope_expr->opno);
                          if (strcmp(op_name, "=") == 0) {
                              if (int_value != DatumGetInt32(constant_2->constvalue)) {
                                  tuple_null = true;
                              }
                          } else if (strcmp(op_name, "<>")== 0) {
                              if (int_value == DatumGetInt32(constant_2->constvalue)) {
                                  tuple_null = true;
                              }
                          } else if (strcmp(op_name, ">=")== 0) {
                              if (int_value < DatumGetInt32(constant_2->constvalue)) {
                                  tuple_null = true;
                              }

                          } else if (strcmp(op_name, "<")== 0) {
                              if (int_value >= DatumGetInt32(constant_2->constvalue)) {
                                  tuple_null = true;
                              }
                          } else if (strcmp(op_name, ">")== 0) {
                              if (int_value <= DatumGetInt32(constant_2->constvalue)) {
                                  tuple_null = true;
                              }
                          } else if (strcmp(op_name, "<=")== 0) {
                              if (int_value > DatumGetInt32(constant_2->constvalue)) {
                                  tuple_null = true;
                              }
                          }

                      }
                  }
                  if (!tuple_null) {
                      columnValues[i] = Int32GetDatum(int_value);
                      columnNulls[i] = false;
                  }

                  break;
              case 2:
                  float float_value;
                  memcpy(&float_value,
                         &file_state->buf[file_state->column_idx_list[file_state->column_idx_list_map[i]]], 4);

                  file_state->column_idx_list[inplace_idx] += 4;

                  if (filtered) {
                      List *restrictInfoList = file_state->restrict_info_list[inplace_idx];
                      ListCell * restrictInfoCell= NULL;
                      foreach(restrictInfoCell, restrictInfoList)
                      {
                          RestrictInfo *restrictInfo = (RestrictInfo *) lfirst(restrictInfoCell);
                          OpExpr *ope_expr = (OpExpr *) restrictInfo->clause;
                          Const *constant_2 = (Const *) lsecond(ope_expr->args);
                          char *op_name = get_opname(ope_expr->opno);
                          if (strcmp(op_name, "=")== 0) {
                              if (float_value != DatumGetFloat4(constant_2->constvalue)) {
                                  tuple_null = true;
                              }
                          } else if (strcmp(op_name, "<>")== 0) {
                              if (float_value == DatumGetFloat4(constant_2->constvalue)) {
                                  tuple_null = true;
                              }
                          } else if (strcmp(op_name, ">=")== 0) {
                              if (float_value < DatumGetFloat4(constant_2->constvalue)) {
                                  tuple_null = true;
                              }

                          } else if (strcmp(op_name, "<")== 0) {
                              if (float_value >= DatumGetFloat4(constant_2->constvalue)) {
                                  tuple_null = true;
                              }
                          } else if (strcmp(op_name, ">")== 0) {
                              if (float_value <= DatumGetFloat4(constant_2->constvalue)) {
                                  tuple_null = true;
                              }
                          } else if (strcmp(op_name, "<=")== 0) {
                              if (float_value > DatumGetFloat4(constant_2->constvalue)) {
                                  tuple_null = true;
                              }
                          }

                      }
                  }
                  if (!tuple_null) {
                      columnValues[i] = Float4GetDatum(float_value);
                      columnNulls[i] = false;
                  }
                  break;
          }
      }

      if (tuple_null){
          memset(columnValues, 0, columnCount * sizeof(Datum));
          memset(columnNulls, true, columnCount * sizeof(bool));
      }else {
          ExecStoreVirtualTuple(tupleSlot);
          break;
      }
    }



    return tupleSlot;

}

extern "C" void db721_ReScanForeignScan(ForeignScanState *node) {
  // TODO(721): Write me!
    Db721_file * file_state = (Db721_file *) node->fdw_state;
    int max_per_block = int(file_state->metadata["Max Values Per Block"]);
    file_state->block_idx = 0;
    file_state->tuple_idx = max_per_block;
}

extern "C" void db721_EndForeignScan(ForeignScanState *node) {
  // TODO(721): Write me!
    Db721_file * file_state = (Db721_file *)node->fdw_state;
    close(file_state->f_oid);
    pfree(file_state->buf);
    pfree(file_state->column_idx_list);
    pfree(file_state->column_type_list);
    pfree(file_state->column_idx_list_map);
    pfree(file_state->restrict_info_list);
    pfree(file_state);


}