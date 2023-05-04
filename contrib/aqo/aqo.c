/*
 * aqo.c
 *		Adaptive query optimization extension
 *
 * Copyright (c) 2016-2022, Postgres Professional
 *
 * IDENTIFICATION
 *	  aqo/aqo.c
 */

#include "postgres.h"

#include "access/relation.h"
#include "access/table.h"
#include "catalog/pg_extension.h"
#include "commands/extension.h"
#include "miscadmin.h"
#include "utils/selfuncs.h"

#include "aqo.h"
#include "aqo_shared.h"
#include "path_utils.h"
#include "storage.h"
#include "utils/backend_status.h"
#include "utils/portal.h"
#include "../../src/include/tcop/tcopprot.h"
PG_MODULE_MAGIC;

void _PG_init(void);

#define AQO_MODULE_MAGIC	(1234)

/* Strategy of determining feature space for new queries. */
int		aqo_mode = AQO_MODE_CONTROLLED;
bool	force_collect_stat;
bool	aqo_predict_with_few_neighbors;
int 	aqo_statement_timeout;

/*
 * Show special info in EXPLAIN mode.
 *
 * aqo_show_hash - show query class (hash) and a feature space value (hash)
 * of each plan node. This is instance-dependent value and can't be used
 * in regression and TAP tests.
 *
 * aqo_show_details - show AQO settings for this class and prediction
 * for each plan node.
 */
bool	aqo_show_hash;
bool	aqo_show_details;
bool	change_flex_timeout;

/* GUC variables */
static const struct config_enum_entry format_options[] = {
	{"intelligent", AQO_MODE_INTELLIGENT, false},
	{"forced", AQO_MODE_FORCED, false},
	{"controlled", AQO_MODE_CONTROLLED, false},
	{"learn", AQO_MODE_LEARN, false},
	{"frozen", AQO_MODE_FROZEN, false},
	{"disabled", AQO_MODE_DISABLED, false},
	{NULL, 0, false}
};

/* Parameters of autotuning */
int			aqo_stat_size = STAT_SAMPLE_SIZE;
int			auto_tuning_window_size = 5;
double		auto_tuning_exploration = 0.1;
int			auto_tuning_max_iterations = 50;
int			auto_tuning_infinite_loop = 8;

/* stat_size > infinite_loop + window_size + 3 is required for auto_tuning*/

/* Machine learning parameters */

/* The number of nearest neighbors which will be chosen for ML-operations */
int			aqo_k;
double		log_selectivity_lower_bound = -30;

/*
 * Currently we use it only to store query_text string which is initialized
 * after a query parsing and is used during the query planning.
 */

QueryContextData	query_context;

MemoryContext		AQOTopMemCtx = NULL;

/* Is released at the end of transaction */
MemoryContext		AQOCacheMemCtx = NULL;

/* Is released at the end of planning */
MemoryContext 		AQOPredictMemCtx = NULL;

/* Is released at the end of learning */
MemoryContext 		AQOLearnMemCtx = NULL;

/* Additional plan info */
int njoins;

/*****************************************************************************
 *
 *	CREATE/DROP EXTENSION FUNCTIONS
 *
 *****************************************************************************/

static void
aqo_free_callback(ResourceReleasePhase phase,
					 bool isCommit,
					 bool isTopLevel,
					 void *arg)
{
	if (phase != RESOURCE_RELEASE_AFTER_LOCKS)
		return;

	if (isTopLevel)
	{
		MemoryContextReset(AQOCacheMemCtx);
		cur_classes = NIL;
	}
}
int
get_str_hash(const char *str)
{
    return DatumGetInt32(hash_any((const unsigned char *) str,
                                  strlen(str) * sizeof(*str)));
}
char* generate_query_file_main(const char *query_string){
    int query_hash_value = get_str_hash(query_string);
    if (query_hash_value < 0) {
        query_hash_value = -query_hash_value;
    }
    char num_str[20];
    sprintf(num_str, "%d", query_hash_value);
    char* file_path = "/home/ubuntu/multiprocess/postgres/postgres-private/main/";
    char* result_path = malloc(strlen(file_path) + strlen(num_str) + 4);
    strcpy(result_path, file_path);
    strcat(result_path, num_str);
    strcat(result_path, ".txt");
//    elog(LOG,"main file_path is %s",result_path);
    return result_path;
}
char* generate_query_file_sub(const char *query_string){
    int query_hash_value = get_str_hash(query_string);
    if (query_hash_value < 0) {
        query_hash_value = -query_hash_value;
    }
    char num_str[20];
    sprintf(num_str, "%d", query_hash_value);
    char* file_path = "/home/ubuntu/multiprocess/postgres/postgres-private/sub/";
    char* result_path = malloc(strlen(file_path) + strlen(num_str) + 4);
    strcpy(result_path, file_path);
    strcat(result_path, num_str);
    strcat(result_path, ".txt");
//    elog(LOG,"sub file_path is %s",result_path);
    return result_path;
}
void write_result_to_file_for_sub(const char *query_string, PlannedStmt *pstmt){
    char* file_path = generate_query_file_sub(query_string);
    if(access(file_path, F_OK)!=-1){
//        elog(LOG,"file exist for sub");
        return;
    }
    FILE *fp = fopen(file_path, "w");
    char * plan_string = nodeToString(pstmt);
    if (fp == NULL) {
//        elog(LOG,"sub write open file failed");
        return;
    }
    fwrite(plan_string,  sizeof(char), strlen(plan_string), fp);
    fclose(fp);
}
PlannedStmt * read_result_from_file_for_sub(const char *query_string){
    char *select_str = strstr(query_string, "select");  // Find the first occurrence of "select"
    if (select_str != NULL) {
        memmove(query_string, select_str, strlen(select_str) + 1);  // Copy the remaining substring to the beginning of the string
    }
    else{
        return NULL;
    }
    char* file_path = generate_query_file_main(query_string);
    FILE *fp = fopen(file_path, "r");
    if (fp == NULL) {
//        elog(LOG,"sub read open file failed");
        return NULL;
    }
    char buffer[16384];
    fread(buffer, sizeof(char), 16384, fp);
    fclose(fp);
    PlannedStmt *new_stmt = (PlannedStmt *)stringToNode(buffer);
    return new_stmt;
}
void
_PG_init(void)
{
	/*
	 * In order to create our shared memory area, we have to be loaded via
	 * shared_preload_libraries. If not, report an ERROR.
	 */
	if (!process_shared_preload_libraries_in_progress)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("AQO module could be loaded only on startup."),
				 errdetail("Add 'aqo' into the shared_preload_libraries list.")));

	/*
	 * Inform the postmaster that we want to enable query_id calculation if
	 * compute_query_id is set to auto.
	 */
	EnableQueryId();

	DefineCustomEnumVariable("aqo.mode",
							 "Mode of aqo usage.",
							 NULL,
							 &aqo_mode,
							 AQO_MODE_CONTROLLED,
							 format_options,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL
	);

	DefineCustomBoolVariable(
							 "aqo.force_collect_stat",
							 "Collect statistics at all AQO modes",
							 NULL,
							 &force_collect_stat,
							 false,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL
	);

	DefineCustomBoolVariable(
							 "aqo.show_hash",
							 "Show query and node hash on explain.",
							 "Hash value depend on each instance and is not good to enable it in regression or TAP tests.",
							 &aqo_show_hash,
							 false,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL
	);

	DefineCustomBoolVariable(
							 "aqo.show_details",
							 "Show AQO state on a query.",
							 NULL,
							 &aqo_show_details,
							 false,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL
	);

	DefineCustomBoolVariable(
							 "aqo.learn_statement_timeout",
							 "Learn on a plan interrupted by statement timeout.",
							 "ML data stored in a backend cache, so it works only locally.",
							 &aqo_learn_statement_timeout,
							 false,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL
	);

	DefineCustomBoolVariable(
							 "aqo.wide_search",
							 "Search ML data in neighbour feature spaces.",
							 NULL,
							 &use_wide_search,
							 false,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL
	);

	DefineCustomIntVariable("aqo.join_threshold",
							"Sets the threshold of number of JOINs in query beyond which AQO is used.",
							NULL,
							&aqo_join_threshold,
							3,
							0, INT_MAX / 1000,
							PGC_USERSET,
							0,
							NULL,
							NULL,
							NULL
	);

	DefineCustomIntVariable("aqo.fs_max_items",
							"Max number of feature spaces that AQO can operate with.",
							NULL,
							&fs_max_items,
							10000,
							1, INT_MAX,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL
	);

	DefineCustomIntVariable("aqo.fss_max_items",
							"Max number of feature subspaces that AQO can operate with.",
							NULL,
							&fss_max_items,
							100000,
							0, INT_MAX,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL
	);

	DefineCustomIntVariable("aqo.querytext_max_size",
							"Query max size in aqo_query_texts.",
							NULL,
							&querytext_max_size,
							1000,
							0, INT_MAX,
							PGC_SUSET,
							0,
							NULL,
							NULL,
							NULL
	);

	DefineCustomIntVariable("aqo.dsm_size_max",
							"Maximum size of dynamic shared memory which AQO could allocate to store learning data.",
							NULL,
							&dsm_size_max,
							100,
							0, INT_MAX,
							PGC_SUSET,
							0,
							NULL,
							NULL,
							NULL
	);
	DefineCustomIntVariable("aqo.statement_timeout",
							"Time limit on learning.",
							NULL,
							&aqo_statement_timeout,
							0,
							0, INT_MAX,
							PGC_USERSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("aqo.min_neighbors_for_predicting",
							"Set how many neighbors the cardinality prediction will be calculated",
							NULL,
							&aqo_k,
							3,
							1, INT_MAX / 1000,
							PGC_USERSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable("aqo.predict_with_few_neighbors",
							"Establish the ability to make predictions with fewer neighbors than were found.",
							 NULL,
							 &aqo_predict_with_few_neighbors,
							 true,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL);
    bool my_aqo_enable;
    DefineCustomBoolVariable("aqo_enable",
                             "Enable AQO.",
                             NULL,
                             &my_aqo_enable,
                             false,
                             PGC_USERSET,
                             0,
                             NULL,
                             NULL,
                             NULL);

	aqo_shmem_init();
	aqo_preprocessing_init();
	aqo_postprocessing_init();
	aqo_cardinality_hooks_init();
	aqo_path_utils_init();

	init_deactivated_queries_storage();

	/*
	 * Create own Top memory Context for reporting AQO memory in the future.
	 */
	AQOTopMemCtx = AllocSetContextCreate(TopMemoryContext,
											 "AQOTopMemoryContext",
											 ALLOCSET_DEFAULT_SIZES);
	/*
	 * AQO Cache Memory Context containe environment data.
	 */
	AQOCacheMemCtx = AllocSetContextCreate(AQOTopMemCtx,
											 "AQOCacheMemCtx",
											 ALLOCSET_DEFAULT_SIZES);

	/*
	 * AQOPredictMemoryContext save necessary information for making predict of plan nodes
	 * and clean up in the execution stage of query.
	 */
	AQOPredictMemCtx = AllocSetContextCreate(AQOTopMemCtx,
											 "AQOPredictMemoryContext",
											 ALLOCSET_DEFAULT_SIZES);
	/*
	 * AQOLearnMemoryContext save necessary information for writing down to AQO knowledge table
	 * and clean up after doing this operation.
	 */
	AQOLearnMemCtx = AllocSetContextCreate(AQOTopMemCtx,
											 "AQOLearnMemoryContext",
											 ALLOCSET_DEFAULT_SIZES);
	RegisterResourceReleaseCallback(aqo_free_callback, NULL);
	RegisterAQOPlanNodeMethods();

	MarkGUCPrefixReserved("aqo");
}
void
startup_background_process_main(Datum main_arg) {
//    elog(LOG,"entering startup_background_process_main");
    BackgroundWorkerInitializeConnection("noisepage_db", "noisepage_user", 0);
    StartTransactionCommand();
    MemoryContext oldcontext;
    List	   *parsetree_list;
    ListCell   *parsetree_item;
    bool		save_log_statement_stats = log_statement_stats;
    bool		was_logged = false;
    bool		use_implicit_block;
    char		msec_str[32];
   const char * query_string = MyBgworkerEntry->bgw_extra;
    char* new_query_string = malloc(strlen(query_string) + 1);
    char *select_str = strstr(query_string, "select");  // Find the first occurrence of "select"
    if (select_str != NULL) {
        memmove(new_query_string, select_str, strlen(select_str) + 1);  // Copy the remaining substring to the beginning of the string
    }
    else{
        return NULL;
    }
    parsetree_list = pg_parse_query(new_query_string);

    /*
     * For historical reasons, if multiple SQL statements are given in a
     * single "simple Query" message, we execute them as a single transaction,
     * unless explicit transaction control commands are included to make
     * portions of the list be separate transactions.  To represent this
     * behavior properly in the transaction machinery, we use an "implicit"
     * transaction block.
     */
    use_implicit_block = (list_length(parsetree_list) > 1);

    //todo: 命名加上parse_tree第几个item的区分
    /*
     * Run through the raw parsetree(s) and process each one.
     */
    foreach(parsetree_item, parsetree_list)
    {

        RawStmt *parsetree = lfirst_node(RawStmt, parsetree_item);
        bool snapshot_set = false;
        CommandTag commandTag;
        QueryCompletion qc;
        MemoryContext per_parsetree_context = NULL;
        List *querytree_list,
                *plantree_list;
        Portal portal;
        DestReceiver *receiver;
        int16 format;


        querytree_list = pg_analyze_and_rewrite_fixedparams(parsetree, new_query_string,
                                                            NULL, 0, NULL);
        int original_aqo_mode = aqo_mode;
        aqo_mode = AQO_MODE_LEARN;

        plantree_list = pg_plan_queries(querytree_list, new_query_string,
                                        CURSOR_OPT_PARALLEL_OK, NULL);
        if(plantree_list!=NULL) {
            // read if the query existed
            PlannedStmt *old_pstmt = read_result_from_file_for_sub(query_string);
            if(old_pstmt!=NULL) {
                // exists old plan, check the new plan is better or not
                PlannedStmt *new_pstmt = linitial_node(PlannedStmt, plantree_list);
//                elog(LOG,"new pstmt: %s", nodeToString(new_pstmt));
                if(new_pstmt->planTree!=NULL && old_pstmt->planTree!=NULL && new_pstmt->planTree->total_cost< old_pstmt->planTree->total_cost) {
                    // new plan is better, write it
//                    elog(LOG,"old cost : %f, new cost: %f", old_pstmt->planTree->total_cost, new_pstmt->planTree->total_cost);
                    write_result_to_file_for_sub(query_string, new_pstmt);
                    // send signal to interrupt the main process
                    kill(MyBgworkerEntry->bgw_notify_pid, SIGUSR2);
                }
//                else{
//                    elog(LOG, "old plan is better, do not write");
//                }
            }
        }

        aqo_mode = original_aqo_mode;
    }

}
/*
 * AQO is really needed for any activity?
 */
bool
IsQueryDisabled(void)
{
	if (!query_context.learn_aqo && !query_context.use_aqo &&
		!query_context.auto_tuning && !query_context.collect_stat &&
		!query_context.adding_query && !query_context.explain_only &&
		INSTR_TIME_IS_ZERO(query_context.start_planning_time) &&
		query_context.planning_time < 0.)
		return true;

	return false;
}

PG_FUNCTION_INFO_V1(invalidate_deactivated_queries_cache);

/*
 * Clears the cache of deactivated queries if the user changed aqo_queries
 * manually.
 */
Datum
invalidate_deactivated_queries_cache(PG_FUNCTION_ARGS)
{
       PG_RETURN_POINTER(NULL);
}
