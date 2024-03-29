#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#include "nodes/extensible.h"
#include "nodes/pathnodes.h"
#include "optimizer/planner.h"

#define AQO_PLAN_NODE	"AQOPlanNode"

/*
 * Find and sort out relations that used in the query:
 * Use oids of relations to store dependency of ML row on a set of tables.
 * Use oids of temporary tables to get access to these structure for preparing
 * a kind of signature.
 */
typedef struct
{
	List *hrels; /* oids of persistent relations */
	List *signatures; /* list of hashes: on qualified name of a persistent
						 * table or on a table structure for temp table */
} RelSortOut;

/*
 * information for adaptive query optimization
 */
typedef struct AQOPlanNode
{
	ExtensibleNode	node;
	bool			had_path;
	RelSortOut	   *rels;
	List		   *clauses;
	List		   *selectivities;

	/* Grouping expressions from a target list. */
	List		*grouping_exprs;

	JoinType	jointype;
	double		parallel_divisor;
	bool		was_parametrized;

	/* For Adaptive optimization DEBUG purposes */
	int		fss;
	double	prediction;
} AQOPlanNode;


#define strtobool(x)  ((*(x) == 't') ? true : false)

#define nullable_string(token,length)  \
	((length) == 0 ? NULL : debackslash(token, length))

#define booltostr(x)  ((x) ? "true" : "false")

/* Extracting path information utilities */
extern List *get_selectivities(PlannerInfo *root,
							   List *clauses,
							   int varRelid,
							   JoinType jointype,
							   SpecialJoinInfo *sjinfo);
extern void get_list_of_relids(PlannerInfo *root, Relids relids,
							   RelSortOut *rels);

extern List *get_path_clauses(Path *path,
							  PlannerInfo *root,
							  List **selectivities);

extern AQOPlanNode *get_aqo_plan_node(Plan *plan, bool create);
extern void RegisterAQOPlanNodeMethods(void);

extern List *aqo_get_clauses(PlannerInfo *root, List *restrictlist);

void aqo_path_utils_init(void);

#endif /* PATH_UTILS_H */
