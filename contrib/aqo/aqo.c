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

//	aqo_shmem_init();
//	aqo_preprocessing_init();
//	aqo_postprocessing_init();
//	aqo_cardinality_hooks_init();
//	aqo_path_utils_init();
//
//	init_deactivated_queries_storage();
//
//	/*
//	 * Create own Top memory Context for reporting AQO memory in the future.
//	 */
//	AQOTopMemCtx = AllocSetContextCreate(TopMemoryContext,
//											 "AQOTopMemoryContext",
//											 ALLOCSET_DEFAULT_SIZES);
//	/*
//	 * AQO Cache Memory Context containe environment data.
//	 */
//	AQOCacheMemCtx = AllocSetContextCreate(AQOTopMemCtx,
//											 "AQOCacheMemCtx",
//											 ALLOCSET_DEFAULT_SIZES);
//
//	/*
//	 * AQOPredictMemoryContext save necessary information for making predict of plan nodes
//	 * and clean up in the execution stage of query.
//	 */
//	AQOPredictMemCtx = AllocSetContextCreate(AQOTopMemCtx,
//											 "AQOPredictMemoryContext",
//											 ALLOCSET_DEFAULT_SIZES);
//	/*
//	 * AQOLearnMemoryContext save necessary information for writing down to AQO knowledge table
//	 * and clean up after doing this operation.
//	 */
//	AQOLearnMemCtx = AllocSetContextCreate(AQOTopMemCtx,
//											 "AQOLearnMemoryContext",
//											 ALLOCSET_DEFAULT_SIZES);
//	RegisterResourceReleaseCallback(aqo_free_callback, NULL);
//	RegisterAQOPlanNodeMethods();
//
//	MarkGUCPrefixReserved("aqo");
}
void
startup_background_process_main(Datum main_arg) {
//    elog(LOG, "line 370: %d", MyProcPid);
//    sleep(20);
//    aqo_shmem_init();
//    aqo_preprocessing_init();
//    aqo_postprocessing_init();
//    aqo_cardinality_hooks_init();
//    aqo_path_utils_init();
//    init_deactivated_queries_storage();
//
//    /*
//     * Create own Top memory Context for reporting AQO memory in the future.
//     */
//    AQOTopMemCtx = AllocSetContextCreate(TopMemoryContext,
//                                         "AQOTopMemoryContext",
//                                         ALLOCSET_DEFAULT_SIZES);
//    /*
//     * AQO Cache Memory Context containe environment data.
//     */
//    AQOCacheMemCtx = AllocSetContextCreate(AQOTopMemCtx,
//                                           "AQOCacheMemCtx",
//                                           ALLOCSET_DEFAULT_SIZES);
//
//    /*
//     * AQOPredictMemoryContext save necessary information for making predict of plan nodes
//     * and clean up in the execution stage of query.
//     */
//    AQOPredictMemCtx = AllocSetContextCreate(AQOTopMemCtx,
//                                             "AQOPredictMemoryContext",
//                                             ALLOCSET_DEFAULT_SIZES);
//    /*
//     * AQOLearnMemoryContext save necessary information for writing down to AQO knowledge table
//     * and clean up after doing this operation.
//     */
//    AQOLearnMemCtx = AllocSetContextCreate(AQOTopMemCtx,
//                                           "AQOLearnMemoryContext",
//                                           ALLOCSET_DEFAULT_SIZES);
//    RegisterResourceReleaseCallback(aqo_free_callback, NULL);
//    RegisterAQOPlanNodeMethods();
//
//    MarkGUCPrefixReserved("aqo");
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
//    elog(LOG, "executing plan");
//    bool found;
//    List * aqo_query_tree = ShmemInitStruct("AQO shared query tree pointer", 10000, &found);
//    elog(LOG, "414, found: %s, aqo_query_tree null: %s", found? "true":"false", aqo_query_tree == NULL? "true": "false");
//    elog(LOG, "aqo: length : %d, max_length: %d", aqo_query_tree->length, aqo_query_tree->max_length);
//    List * plan = pg_plan_queries(aqo_query_tree, query_string,
//                                  CURSOR_OPT_PARALLEL_OK, NULL);
//    elog(LOG, "416");
//    FILE * plan_file = fopen("plan.bin", "wb");
//
//    fwrite(plan,  offsetof(List, initial_elements) +
//                  plan->length * sizeof(ListCell), 1, plan_file);
//    elog(LOG, "412");
//    fclose(plan_file);

//    MemoryContextSwitchTo(AQOTopMemCtx);
    parsetree_list = pg_parse_query(query_string);
// break aqo.c:436

    /*
     * For historical reasons, if multiple SQL statements are given in a
     * single "simple Query" message, we execute them as a single transaction,
     * unless explicit transaction control commands are included to make
     * portions of the list be separate transactions.  To represent this
     * behavior properly in the transaction machinery, we use an "implicit"
     * transaction block.
     */
    use_implicit_block = (list_length(parsetree_list) > 1);


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


        querytree_list = pg_analyze_and_rewrite_fixedparams(parsetree, query_string,
                                                            NULL, 0, NULL);
        plantree_list = pg_plan_queries(querytree_list, query_string,
                                        CURSOR_OPT_PARALLEL_OK, NULL);
        FILE * plan_file = fopen("/home/ubuntu/postgres-private/plan.bin", "wb");
        elog(LOG, "473: %s", nodeToString(linitial(plantree_list)));
        fwrite(plantree_list,  offsetof(List, initial_elements) +
                plantree_list->length * sizeof(ListCell), 1, plan_file);
        elog(LOG, "412");
        fclose(plan_file);

    }
    kill(MyBgworkerEntry->bgw_notify_pid, SIGUSR2);

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
