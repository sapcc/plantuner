/*
 * Copyright (c) 2009 Teodor Sigaev <teodor@sigaev.ru>
 */

#include <postgres.h>

#include <fmgr.h>
#include <catalog/namespace.h>
#include <catalog/pg_class.h>
#include <nodes/pg_list.h>
#include <optimizer/plancat.h>
#include <utils/builtins.h>
#include <utils/guc.h>
#include <utils/lsyscache.h>

PG_MODULE_MAGIC;

static int 	nIndexesOut = 0;
static Oid	*indexesOut = NULL;
get_relation_info_hook_type	prevHook = NULL;

static char *indexesOutStr = "";

static const char *
indexesOutAssign(const char * newval, bool doit, GucSource source) 
{
	char       *rawname;
	List       *namelist;
	ListCell   *l;
	Oid			*newOids = NULL;
	int			nOids = 0,
				i = 0;

	rawname = pstrdup(newval);

	if (!SplitIdentifierString(rawname, ',', &namelist))
		goto cleanup;

	if (doit) 
	{
		nOids = list_length(namelist);
		newOids = malloc(sizeof(Oid) * (nOids+1));
		if (!newOids)
			elog(ERROR,"could not allocate %d bytes", sizeof(Oid) * (nOids+1));
	}

	foreach(l, namelist)
	{
		char     	*curname = (char *) lfirst(l);
		Oid			indexOid = RangeVarGetRelid(makeRangeVarFromNameList(stringToQualifiedNameList(curname)), true);

		if (indexOid == InvalidOid)
		{
			elog(WARNING,"'%s' does not exist", curname);
			continue;
		}
		else if ( get_rel_relkind(indexOid) != RELKIND_INDEX )
		{
			elog(WARNING,"'%s' is not an index", curname);
			continue;
		}
		else if (doit)
		{
			newOids[i++] = indexOid;
		}
	}

	if (doit) 
	{
		nIndexesOut = nOids;
		indexesOut = newOids;
	}

	pfree(rawname);
	list_free(namelist);

	return newval;

cleanup:
	if (newOids)
		free(newOids);
	pfree(rawname);
	list_free(namelist);
	return NULL;
}


static void
indexFilter(PlannerInfo *root, Oid relationObjectId, bool inhparent, RelOptInfo *rel) {
	int i;

	for(i=0;i<nIndexesOut;i++)
	{
		ListCell   *l;

		foreach(l, rel->indexlist)
		{
			IndexOptInfo	*info = (IndexOptInfo*)lfirst(l);

			if (indexesOut[i] == info->indexoid)
			{
				rel->indexlist = list_delete_ptr(rel->indexlist, info);
				break;
			}
		}
	}

	/*
	 * Call next hook if it exists 
	 */
	if (prevHook)
		prevHook(root, relationObjectId, inhparent, rel);
}

static const char*
IndexFilterShow(void) 
{
	char 	*val, *ptr;
	int 	i,
			len;

	len = 1 /* \0 */ + nIndexesOut * (2 * NAMEDATALEN + 2 /* ', ' */ + 1 /* . */);
	ptr = val = palloc(len);

	*ptr ='\0';
	for(i=0; i<nIndexesOut; i++)
	{
		char 	*relname = get_rel_name(indexesOut[i]);
		Oid 	nspOid = get_rel_namespace(indexesOut[i]);
		char 	*nspname = get_namespace_name(nspOid); 

		if ( relname == NULL || nspOid == InvalidOid || nspname == NULL )
			continue;

		ptr += snprintf(ptr, len - (ptr - val), "%s%s.%s",
												(i==0) ? "" : ", ",
												nspname,
												relname);
	}

	return val;
}

void _PG_init(void);
void
_PG_init(void) 
{
    DefineCustomStringVariable(
		"plantuner.forbid_index",
		"List of forbidden indexes",
		"Listed indexes will not be used in queries",
		&indexesOutStr,
		"",
		PGC_USERSET,
		0,
		indexesOutAssign,
		IndexFilterShow
	);

	if (get_relation_info_hook != indexFilter )
	{
		prevHook = get_relation_info_hook;
		get_relation_info_hook = indexFilter;
	}
}