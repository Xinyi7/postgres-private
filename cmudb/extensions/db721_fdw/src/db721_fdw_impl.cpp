// If you choose to use C++, read this very carefully:
// https://www.postgresql.org/docs/15/xfunc-c.html#EXTEND-CPP

#include "dog.h"
#include "json.hpp"
// clang-format off
extern "C" {
#include "../../../../src/include/postgres.h"
#include "../../../../src/include/fmgr.h"
#include "../../../../src/include/foreign/fdwapi.h"
}
// clang-format on
using json = nlohmann::json;
class Db721_file{
public:
    char * filename;
    int f_oid;
    char * buf;
    std::shared_ptr<json> meta_ptr;
    int num_columns;
    int * column_sizes;
};
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
    List	   *options_list = untransformRelOptions(PG_GETARG_DATUM(0));
    char	   *filename;
    auto file_state = (Db721_file*)palloc(sizeof(Db721_file));
    foreach(cell, options_list)
    {
        DefElem *def = (DefElem *) lfirst(cell);
        if (strcmp(def->defname, "filename") == 0){
            filename = defGetString(def);
        }
    }

    file_state->filename = filename;

    file_state->f_oid = open(filename, O_RDONLY);
    lseek(file_state->f_oid, -4, SEEK_END);

    int size;

    read(file_state->f_oid, static_cast<char *>(&size), 4);


    lseek(file_state->f_oid, -size, SEEK_END);

    auto json_buf = palloc(sizeof(json));
    read(file_state->f_oid, file_state->metadata, size);

    auto metadata_json = json::parse(file_state->metadata);
    file_state->meta_ptr = std::make_shared<json>(metadata_json);
    int max_per_block =int(metadata_json["Max Values Per Block"]);
    int tuple_size = 0;
    int idx = 0;
    file_state->num_columns = metadata_json.size();
    file_state->column_sizes = palloc(sizeof(int) * file_state->num_columns);
    for (auto& el : metadata_json["Columns"].items()) {
        switch(el.value()["type"]){
            case "str":
                tuple_size += 32;
                file_state->column_sizes[idx++] = 32;
                break;
            case "float":
                tuple_size += 4;
                file_state->column_sizes[idx++] = 4;
                break;
            case "int":
                tuple_size += 4;
                file_state->column_sizes[idx++] = 4;
                break;
            default:
                break;
        }

    }
    file_state->buf = palloc(tuple_size * max_per_block);
    node->fdw_state = file_state;

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
  Db721_file * file_state = (Db721_file *)node->fdw_state;
  close(file_state->f_oid);
  pfree(file_state->column_sizes);
  pfree(file_state->buf);
  pfree(file_state->filename);
  pfree(file_state);

}