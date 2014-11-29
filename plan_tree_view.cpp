/*-------------------------------------------------------------------------
 *
 * plan_tree_veiw.cpp
 *
 * Copyright (c) 2014 Minoru NAKAMURA <nminoru@nminoru.jp>
 *
 *-------------------------------------------------------------------------
 */

extern "C" {

#include "postgres.h"
#include "access/relscan.h"
#include "access/sysattr.h"
#include "access/xact.h"
#include "catalog/pg_operator.h"
#include "catalog/pg_type.h"
#include "miscadmin.h"
#include "nodes/nodeFuncs.h"
#include "nodes/bitmapset.h"
#include "nodes/print.h"
#include "optimizer/clauses.h"
#include "optimizer/cost.h"
#include "optimizer/paths.h"
#include "optimizer/pathnode.h"
#include "optimizer/planmain.h"
#include "optimizer/restrictinfo.h"
#include "optimizer/planner.h"
#include "utils/palloc.h"

};

#include <stdarg.h>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <string>

#include "pg_plan_tree_dot.h"

#define FIND_NODE(fldname) \
	do {findNode(env, node, #fldname, node->fldname);} while (0)

#define FIND_NODE_INDEX(fldname, index) \
	do {if (node->fldname[index] != NULL) { findNodeIndex(env, node, #fldname, index, node->fldname);}} while (0)

#define FIND_PLAN(fldname) \
	do {findNode(env, node, #fldname, node->fldname);} while (0)

#define FIND_EXPRLIST(fldname) \
	do {env.registerExprTree(node->fldname); findNode(env, node, #fldname, node->fldname);} while (0)

#define FIND_TARGETLIST(fldname) \
	do {env.registerTargetList(node->fldname); findNode(env, node, #fldname, node->fldname);} while (0)

/* Write an integer field (anything written as ":fldname %d") */
#define WRITE_INT_FIELD(fldname) \
	do {env.outputInt(#fldname, node->fldname);} while (0)

#define WRITE_INT32_FIELD(fldname) \
	do {env.outputInt(#fldname, node->fldname);} while (0)

/* Write an unsigned integer field (anything written as ":fldname %u") */
#define WRITE_UINT_FIELD(fldname) \
	do {env.outputUint(#fldname, node->fldname);} while (0)

#define WRITE_ATTRNUMBER_FIELD(fldname) \
	do {env.outputAttrNumber(#fldname, node->fldname);} while (0)

#define WRITE_INDEX_FIELD(fldname) \
	do {env.outputIndex(#fldname, node->fldname);} while (0)

/* Write an OID field (don't hard-wire assumption that OID is same as uint) */
#define WRITE_OID_FIELD(fldname) \
	do {if (node->fldname != 0) {env.outputOid(#fldname, node->fldname);}} while (0)

/* Write a long-integer field */
#define WRITE_LONG_FIELD(fldname) \
	do {env.outputLong(#fldname, node->fldname);} while (0)

/* Write a char field (ie, one ascii character) */
#define WRITE_CHAR_FIELD(fldname) \
	do {env.outputChar(#fldname, node->fldname);} while (0)

/* Write a float field --- caller must give format to define precision */
#define WRITE_FLOAT_FIELD(fldname,format) \
	do {env.outputFloat(#fldname, node->fldname, format);} while (0)

#define WRITE_COST_FIELD(fldname) \
	do {env.outputCost(#fldname, node->fldname);} while (0)

/* Write a boolean field */
#define WRITE_BOOL_FIELD(fldname) \
	do {env.outputBool(#fldname, node->fldname);} while (0)

/* Write a character-string (possibly NULL) field */
#define WRITE_STRING_FIELD(fldname) \
	do {if (node->fldname != NULL) {env.outputString(#fldname, node->fldname);}} while (0)

/* Write an enumerated-type field as an integer code */
#define WRITE_ENUM_FIELD(fldname, enumtype) \
	do {env.outputEnum(#fldname, node->fldname);} while (0)

/* Write a parse location field (actually same as INT case) */
#define WRITE_LOCATION_FIELD(fldname) \
	do { if (node->fldname != -1) {env.outputLocation(#fldname, node->fldname); }} while (0)

/* Write a bitmapset field */
#define WRITE_BITMAPSET_FIELD(fldname) \
	do {env.outputBitmapset(#fldname, node->fldname);} while (0)

/* Write a Node field */
#define WRITE_NODE_FIELD(fldname) \
	do {if (node->fldname != NULL) {env.outputNode(#fldname, node, node->fldname);}} while (0)

#define WRITE_NODE_INDEX_FIELD(fldname, index)										\
	do {if (node->fldname[index] != NULL) {env.outputNodeIndex(#fldname, index, node, node->fldname[index]);}} while (0)


typedef std::map<const void*, unsigned int>		NodeIdMap;
typedef std::pair<const void*, const void*>		EdgeKeyType;
typedef std::map<EdgeKeyType, std::string>		EdgeMap;
typedef std::set<const void*>					NodeSet;
typedef std::map<const void*, const void*>		NodeNodeMap;
typedef std::map<const void*, NodeSet>			NodeSetMap;

class NodeInfoEnv {
	NodeIdMap		node_id_map;
	EdgeMap			edge_map;

	unsigned int	node_id;
	std::string		label;
	std::string		buffer;

	NodeSet			tlist_head_set;
	NodeSet			exprtree_head_set;
	int				num_subgraph;

public:
	NodeInfoEnv(const char *str) :
		node_id_map(), edge_map(), node_id(0), label(str), buffer(),
		tlist_head_set(), exprtree_head_set(), num_subgraph(0) {}

	bool hasNode(const void *node) const
	{
		return node_id_map.find(node) != node_id_map.end();
	}

	void registerTargetList(const void *node)
	{
		tlist_head_set.insert(node);
	}

	void registerExprTree(const void *node)
	{
		exprtree_head_set.insert(node);
	}

	void registerNode(const void *node)
	{
		node_id++;
		node_id_map[node] = node_id;
	}

	void registerEdge(const void *parent, const void *node, const char *fldname)
	{
		edge_map[EdgeKeyType(parent, node)] = fldname;
	}

	const char *c_str() const
	{
		return buffer.c_str();
	} 

	void outputAllNodes();

	void pushNode(const void *node, const char* str)
	{
		append("<head> %s (%d)", str, node_id_map[node]);
	}

	void popNode()
	{
	}

	void append(const char *fmt, ...)
	{
		char buf[1024];
		va_list ap;

		va_start(ap, fmt);
		vsprintf(buf, fmt, ap);
		va_end(ap);

		buffer.append(buf);
	}

	void outputBool(const char *fldname, bool value)
	{
		append("|%s: %s", fldname, value ? "true" : "false");
	}

	void outputInt(const char *fldname, int value)
	{
		append("|%s: %d", fldname, value);
	}

	void outputUint(const char *fldname, int value)
	{
		append("|%s: %u", fldname, value);
	}

	void outputAttrNumber(const char *fldname, AttrNumber value)
	{
		append("|%s: %d", fldname, value);
	}

	void outputIndex(const char *fldname, Index value)
	{
		switch (value)
		{
			case INNER_VAR:
				append("|%s: INNER_VAR", fldname);
				break;
			case OUTER_VAR:
				append("|%s: OUTER_VAR", fldname);
				break;
			case INDEX_VAR:
				append("|%s: INDEX_VAR", fldname);
				break;
			default:
				append("|%s: %u", fldname, value);
				break;
		}
	}
	
	void outputOid(const char *fldname, Oid value)
	{
		append("|%s: %u", fldname, value);
	}

	void outputLong(const char *fldname, long value)
	{
		append("|%s: %l", fldname, value);
	}

	void outputChar(const char *fldname, char value)
	{
		append("|%s: %d", fldname, value);
	}

	void outputFloat(const char *fldname, double value, const char *format)
	{
		char tmp[20];
		sprintf(tmp, "|%%s: %s", format);
		append(tmp, fldname, value);
	}

	void outputCost(const char *fldname, Cost value)
	{
		append("|%s: %.2f", fldname, value);
	}

	void outputString(const char *fldname, const char *value)
	{
		if (value)
			append("|%s: %s", fldname, value);
		else
			append("|%s: NULL", fldname);
	}

	void outputBitmapset(const char *fldname, const Bitmapset *bitmapset)
	{
		int x;
		Bitmapset  *tmpset;

		append("|%s: (b", fldname);
		tmpset = bms_copy(bitmapset);
		while ((x = bms_first_member(tmpset)) >= 0)
			append(" %d", x);
		bms_free(tmpset);
		append(")");
	}

	void outputNodeIndex(const char *fldname, int index, const void *node, const void *edge)
	{
		char buffer[256];
		sprintf(buffer, "%s%d", fldname, index);
		outputNode(buffer, node, edge);
	}

	void outputNode(const char *fldname, const void *node, const void *edge)
	{
		if (edge)
		{
			append("|<%s> %s: ", fldname, fldname);

			if (!hasNode(node) || !hasNode(edge))
				append("Unknown node");
		}
		else
			append("|%s: NULL", fldname);
	}

	void outputLocation(const char *fldname, int location)
	{
		append("|%s: %d", fldname, location);
	}

	void outputEnum(const char *fldname, JoinType jointype)
	{
		append("|%s: ", fldname);
		switch (jointype)
		{
			case JOIN_INNER:
				append("JOIN_INNER");
				break;
			case JOIN_LEFT:
				append("JOIN_LEFT");
				break;
			case JOIN_FULL:
				append("JOIN_FULL");
				break;
			case JOIN_RIGHT:
				append("JOIN_RIGHT");
				break;
			case JOIN_SEMI:
				append("JOIN_SEMI");
				break;
			case JOIN_ANTI:
				append("JOIN_ANTI");
				break;
			case JOIN_UNIQUE_OUTER:
				append("JOIN_UNIQUE_OUTER");
				break;
			case JOIN_UNIQUE_INNER:
				append("JOIN_UNIQUE_INNER");
				break;
			default:
				append("JoinType(Unknown: %d)", jointype);
				break;
		}
	}

	void outputEnum(const char *fldname, CmdType commandType)
	{
		append("|%s: ", fldname);
		switch (commandType)
		{
			case CMD_UNKNOWN:
				append("CMD_UNKNOWN");
				break;
			case CMD_SELECT:
				append("CMD_SELECT");
				break;
			case CMD_UPDATE:
				append("CMD_UPDATE");
				break;
			case CMD_INSERT:
				append("CMD_INSERT");
				break;
			case CMD_DELETE:
				append("CMD_DELETE");
				break;
			case CMD_UTILITY:
				append("CMD_UTILITY");
				break;
			case CMD_NOTHING:
				append("CMD_NOTHING");
				break;
			default:
				append("CmdType(Unknown:%d)", commandType);
				break;
		}
	}

	void outputEnum(const char *fldname, ScanDirection scandirection)
	{
		append("|%s: ", fldname);
		switch (scandirection)
		{
			case BackwardScanDirection:
				append("BackwardScanDirection");
				break;
			case NoMovementScanDirection:
				append("NoMovementScanDirection");
				break;
			case ForwardScanDirection:
				append("ForwardScanDirection");
				break;
			default:
				append("ScanDirection(Unknown: %d)", scandirection);
				break;
		}
	}

	void outputEnum(const char *fldname, AggStrategy aggstrategy)
	{
		append("|%s: ", fldname);
		switch (aggstrategy)
		{
			case AGG_PLAIN:
				append("AGG_PLAIN");
				break;
			case AGG_SORTED:
				append("AGG_SORTED");
				break;
			case AGG_HASHED:
				append("AGG_HASHED");
				break;
			default:
				append("AggStrategy(Unknown: %d)", aggstrategy);
				break;
		}
	}

	void outputEnum(const char *fldname, SetOpCmd setopcmd)
	{
		append("|%s: ", fldname);
		switch (setopcmd)
		{
			case SETOPCMD_INTERSECT:
				append("SETOPCMD_INTERSECT");
				break;
			case SETOPCMD_INTERSECT_ALL:
				append("SETOPCMD_INTERSECT_ALL");
				break;
			case SETOPCMD_EXCEPT:
				append("SETOPCMD_EXCEPTN");
				break;
			case SETOPCMD_EXCEPT_ALL:
				append("SETOPCMD_EXCEPT_ALL");
				break;
			default:
				append("SetOpCmd(Unknown: %d)", setopcmd);
				break;
		}
	}

	void outputEnum(const char *fldname, SetOpStrategy setopstrategy)
	{
		append("|%s: ", fldname);
		switch (setopstrategy)
		{
			case SETOP_SORTED:
				append("SETOP_SORTED");
				break;
			case SETOP_HASHED:
				append("SETOP_HASHED");
				break;
			default:
				append("SetOpStrategy(Unknown: %d)", setopstrategy);
				break;
		}
	}

	void outputEnum(const char *fldname, RowMarkType rowmarktype)
	{
		append("|%s: ", fldname);
		switch (rowmarktype)
		{
			case ROW_MARK_EXCLUSIVE:
				append("ROW_MARK_EXCLUSIVE");
				break;
			case ROW_MARK_SHARE:
				append("ROW_MARK_SHARE");
				break;
			case ROW_MARK_REFERENCE:
				append("ROW_MARK_SHARE");
				break;
			case ROW_MARK_COPY:
				append("ROW_MARK_SHARE");
				break;
			default:
				append("RowMarkType(Unknown: %d)", rowmarktype);
				break;
		}
	}

	void outputEnum(const char *fldname, InhOption inhOpt)
	{
		append("|%s: ", fldname);
		switch (inhOpt)
		{
			case INH_NO:
				append("INH_NO");
				break;
			case INH_YES:
				append("INH_YES");
				break;
			case INH_DEFAULT:
				append("INH_DEFAULT");
				break;
			default:
				append("InhOption(Unknown: %d)", inhOpt);
				break;
		}
	}

	void outputEnum(const char *fldname, ParamKind paramkind)
	{
		append("|%s: ", fldname);
		switch (paramkind)
		{
			case PARAM_EXTERN:
				append("PARAM_EXTERN");
				break;
			case PARAM_EXEC:
				append("PARAM_EXEC");
				break;
			case PARAM_SUBLINK:
				append("PARAM_SUBLINK");
				break;
			default:
				append("ParamKind(Unknown: %d)", paramkind);
				break;
		}
	}

	void outputEnum(const char *fldname, CoercionForm coercionform)
	{
		append("|%s: ", fldname);
		switch (coercionform)
		{
			case COERCE_EXPLICIT_CALL:
				append("COERCE_EXPLICIT_CALL");
				break;
			case COERCE_EXPLICIT_CAST:
				append("COERCE_EXPLICIT_CAST");
				break;
			case COERCE_IMPLICIT_CAST:
				append("COERCE_IMPLICIT_CAST");
				break;
#if PG_VERSION_NUM < 90300
			case COERCE_DONTCARE:
				append("COERCE_DONTCARE");
				break;
#endif
			default:
				append("CoercionForm(Unknown: %d)", coercionform);
				break;
		}
	}

	void outputEnum(const char *fldname, SubLinkType sublinktype)
	{
		append("|%s: ", fldname);
		switch (sublinktype)
		{
			case EXISTS_SUBLINK:
				append("EXISTS_SUBLINK");
				break;
			case ALL_SUBLINK:
				append("ALL_SUBLINK");
				break;
			case ANY_SUBLINK:
				append("ANY_SUBLINK");
				break;
			case ROWCOMPARE_SUBLINK:
				append("ROWCOMPARE_SUBLINK");
				break;
			case EXPR_SUBLINK:
				append("EXPR_SUBLINK");
				break;
			case ARRAY_SUBLINK:
				append("ARRAY_SUBLINK");
				break;
			case CTE_SUBLINK:
				append("CTE_SUBLINK");
				break;
			default:
				append("SubLinkType(Unknown: %d)", sublinktype);
				break;
		}
	}

	void outputEnum(const char *fldname, QuerySource querysource)
	{
		append("|%s: ", fldname);
		switch (querysource)
		{
			case QSRC_ORIGINAL:
				append("QSRC_ORIGINAL");
				break;
			case QSRC_PARSER:
				append("QSRC_PARSER");
				break;
			case QSRC_INSTEAD_RULE:
				append("QSRC_INSTEAD_RULE");
				break;
			case QSRC_QUAL_INSTEAD_RULE:
				append("QSRC_QUAL_INSTEAD_RULE");
				break;
			case QSRC_NON_INSTEAD_RULE:
				append("QSRC_NON_INSTEAD_RULE");
				break;
			default:
				append("QuerySource(Unknown: %d)", querysource);
				break;
		}
	}

	void outputEnum(const char *fldname, RTEKind rtekind)
	{
		append("|%s: ", fldname);
		switch (rtekind)
		{
			case RTE_RELATION:
				append("RTE_RELATION");
				break;
			case RTE_SUBQUERY:
				append("RTE_SUBQUERY");
				break;
			case RTE_JOIN:
				append("RTE_JOIN");
				break;
			case RTE_FUNCTION:
				append("RTE_FUNCTION");
				break;
			case RTE_VALUES:
				append("RTE_VALUES");
				break;
			case RTE_CTE:
				append("RTE_CTE");
				break;
			default:
				append("RTEKind(Unknown: %d)", rtekind);
				break;
		}
	}

	void outputEnum(const char *fldname, BoolExprType boolop)
	{
		append("|%s: ", fldname);
		switch (boolop)
		{
			case AND_EXPR:
				append("AND");
				break;
			case OR_EXPR:
				append("OR");
				break;
			case NOT_EXPR:
				append("NOT");
				break;
			default:
				append("BoolExprType(Unknown: %d)", boolop);
				break;
		}
	}

	void outputEnum(const char *fldname, RelOptKind reloptkind)
	{
		append("|%s: ", fldname);
		switch (reloptkind)
		{
			case RELOPT_BASEREL:
				append("RELOPT_BASEREL");
				break;
			case RELOPT_JOINREL:
				append("RELOPT_JOINREL");
				break;
			case RELOPT_OTHER_MEMBER_REL:
				append("RELOPT_OTHER_MEMBER_REL");
				break;
			case RELOPT_DEADREL:
				append("RELOPT_DEADREL");
				break;
			default:
				append("RelOptKind(Unknown: %d)", reloptkind);
				break;
		}
	}

	void outputEnum(const char *fldname, OnCommitAction onCommit)
	{
		append("|%s: ", fldname);
		switch (onCommit)
		{
			case ONCOMMIT_NOOP:
				append("ONCOMMIT_NOOP");
				break;
			case ONCOMMIT_PRESERVE_ROWS:
				append("ONCOMMIT_PRESERVE_ROWS");
				break;
			case ONCOMMIT_DELETE_ROWS:
				append("ONCOMMIT_DELETE_ROWS");
				break;
			case ONCOMMIT_DROP:
				append("ONCOMMIT_DROP");
				break;
			default:
				append("OnCommitAction(Unknown: %d)", onCommit);
				break;
		}
	}

	void outputEnum(const char *fldname, RowCompareType rctype)
	{
		append("|%s: ", fldname);
		switch (rctype)
		{
			case ROWCOMPARE_LT:
				append("ROWCOMPARE_LT");
				break;
			case ROWCOMPARE_LE:
				append("ROWCOMPARE_LE");
				break;
			case ROWCOMPARE_EQ:
				append("ROWCOMPARE_EQ");
				break;
			case ROWCOMPARE_GE:
				append("ROWCOMPARE_GE");
				break;
			case ROWCOMPARE_GT:
				append("ROWCOMPARE_GT");
				break;
			case ROWCOMPARE_NE:
				append("ROWCOMPARE_NE");
				break;
			default:
				append("RowCompareType(Unknown: %d)", rctype);
				break;
		}
	}

	void outputEnum(const char *fldname, MinMaxOp op)
	{
		append("|%s: ", fldname);
		switch (op)
		{
			case IS_GREATEST:
				append("IS_GREATEST");
				break;
			case IS_LEAST:
				append("IS_LEAST");
				break;
			default:
				append("MinMaxOp(Unknown: %d)", op);
				break;
		}
	}

	void outputEnum(const char *fldname, XmlExprOp op)
	{
		append("|%s: ", op);
		switch (op)
		{
			case IS_XMLCONCAT:
				append("IS_XMLCONCAT");
				break;
			case IS_XMLELEMENT:
				append("IS_XMLELEMENT");
				break;
			case IS_XMLFOREST:
				append("IS_XMLFOREST");
				break;
			case IS_XMLPARSE:
				append("IS_XMLPARSE");
				break;
			case IS_XMLPI:
				append("IS_XMLPI");
				break;
			case IS_XMLROOT:
				append("IS_XMLROOT");
				break;
			case IS_XMLSERIALIZE:
				append("IS_XMLSERIALIZE");
				break;
			case IS_DOCUMENT:
				append("IS_DOCUMENT");
				break;				
			default:
				append("XmlExprOp(Unknown: %d)", op);
				break;
		}
	}

	void outputEnum(const char *fldname, XmlOptionType xmloption)
	{
		append("|%s: ", xmloption);
		switch (xmloption)
		{
			case XMLOPTION_DOCUMENT:
				append("XMLOPTION_DOCUMENT");
				break;
			case XMLOPTION_CONTENT:
				append("XMLOPTION_CONTENT");
				break;
			default:
				append("XmlOptionType(Unknown: %d)", xmloption);
				break;
		}
	}

	void outputEnum(const char *fldname, NullTestType nulltesttype)
	{
		append("|%s: ", fldname);
		switch (nulltesttype)
		{
			case IS_NULL:
				append("IS_NULL");
				break;
			case IS_NOT_NULL:
				append("IS_NOT_NULL");
				break;
			default:
				append("NullTestType(Unknown: %d)", nulltesttype);
				break;
		}
	}

	void outputEnum(const char *fldname, BoolTestType booltesttype)
	{
		append("|%s: ", fldname);
		switch (booltesttype)
		{
			case IS_TRUE:
				append("IS_TRUE");
				break;
			case IS_NOT_TRUE:
				append("IS_NOT_TRUE");
				break;
			case IS_FALSE:
				append("IS_FALSE");
				break;
			case IS_NOT_FALSE:
				append("IS_NOT_FALSE");
				break;
			case IS_UNKNOWN:
				append("IS_UNKNOWN");
				break;
			case IS_NOT_UNKNOWN:
				append("IS_NOT_UNKNOWN");
				break;
			default:
				append("BoolTestType(Unknown: %d)", booltesttype);
				break;
		}
	}

	void outputOidArray(const char *fldname, int size, Oid* oidarray)
	{
		int i;
		append("|%s:", fldname);
		for (i=0 ; i<size ; i++) {
			append(" %u", oidarray[i]);
		}
	}

	void outputIntArray(const char *fldname, int size, int* intarray)
	{
		int i;
		append("|%s:", fldname);
		for (i=0 ; i<size ; i++) {
			append(" %d", intarray[i]);
		}
	}

	void outputAttrNumberArray(const char *fldname, int size, AttrNumber* attrnumarray)
	{
		int i;
		append("|%s:", fldname);
		for (i=0 ; i<size ; i++) {
			append(" %d", attrnumarray[i]);
		}
	}

	void outputBoolArray(const char *fldname, int size, bool* boolarray)
	{
		int i;
		append("|%s:", fldname);
		for (i=0 ; i<size ; i++) {
			append(" %s", boolarray[i] ? "true" : "false");
		}
	}
};


/* static void _outNode(StringInfo str, const void *obj); */
static void findNode(NodeInfoEnv& env, const void *parent, const char *fldname, const void *obj);
static void findNodeIndex(NodeInfoEnv& env, const void *parent, const char *fldname, int index, const void *obj);
static void findPlannedStmt(NodeInfoEnv& env, const PlannedStmt *node);
static void findPlan(NodeInfoEnv& env, const Plan *node);
static void   findModifyTable(NodeInfoEnv& env, const ModifyTable *node);
static void   findAppend(NodeInfoEnv& env, const Append *node);
static void   findMergeAppend(NodeInfoEnv& env, const MergeAppend *node);
static void   findRecursiveUnion(NodeInfoEnv& env, const RecursiveUnion *node);
static void   findBitmapAnd(NodeInfoEnv& env, const BitmapAnd *node);
static void   findBitmapOr(NodeInfoEnv& env, const BitmapOr *node);
static void   findScan(NodeInfoEnv& env, const Scan *node);
static void     findSeqScan(NodeInfoEnv& env, const SeqScan *node);
static void     findIndexScan(NodeInfoEnv& env, const IndexScan *node);
static void     findIndexOnlyScan(NodeInfoEnv& env, const IndexOnlyScan *node);
static void     findBitmapIndexScan(NodeInfoEnv& env, const BitmapIndexScan *node);
static void     findBitmapHeapScan(NodeInfoEnv& env, const BitmapHeapScan *node);
static void     findTidScan(NodeInfoEnv& env, const TidScan *node);
static void     findSubqueryScan(NodeInfoEnv& env, const SubqueryScan *node);
static void     findFunctionScan(NodeInfoEnv& env, const FunctionScan *node);
static void     findValuesScan(NodeInfoEnv& env, const ValuesScan *node);
static void     findCteScan(NodeInfoEnv& env, const CteScan *node);
static void     findWorkTableScan(NodeInfoEnv& env, const WorkTableScan *node);
static void     findForeignScan(NodeInfoEnv& env, const ForeignScan *node);
static void   findJoin(NodeInfoEnv& env, const Join *node);
static void     findNestLoop(NodeInfoEnv& env, const NestLoop *node);
static void     findMergeJoin(NodeInfoEnv& env, const MergeJoin *node);
static void     findHashJoin(NodeInfoEnv& env, const HashJoin *node);
static void   findAgg(NodeInfoEnv& env, const Agg *node);
static void   findWindowAgg(NodeInfoEnv& env, const WindowAgg *node);
static void   findGroup(NodeInfoEnv& env, const Group *node);
static void   findMaterial(NodeInfoEnv& env, const Material *node);
static void   findSort(NodeInfoEnv& env, const Sort *node);
static void   findUnique(NodeInfoEnv& env, const Unique *node);
static void   findHash(NodeInfoEnv& env, const Hash *node);
static void   findSetOp(NodeInfoEnv& env, const SetOp *node);
static void   findLockRows(NodeInfoEnv& env, const LockRows *node);
static void   findLimit(NodeInfoEnv& env, const Limit *node);
static void   findNestLoopParam(NodeInfoEnv& env, const NestLoopParam *node);
static void   findPlanRowMark(NodeInfoEnv& env, const PlanRowMark *node);
static void   findPlanInvalItem(NodeInfoEnv& env, const PlanInvalItem *node);
static void   findAlias(NodeInfoEnv& env, const Alias *node);
static void   findRangeVar(NodeInfoEnv& env, const RangeVar *node);
static void   findIntoClause(NodeInfoEnv& env, const IntoClause *node);
/* static void findExpr(NodeInfoEnv& env, const Expr *node); */
static void   findVar(NodeInfoEnv& env, const Var *node);
static void   findConst(NodeInfoEnv& env, const Const *node);
static void   findParam(NodeInfoEnv& env, const Param *node);
static void   findAggref(NodeInfoEnv& env, const Aggref *node);
static void   findWindowFunc(NodeInfoEnv& env, const WindowFunc *node);
static void   findArrayRef(NodeInfoEnv& env, const ArrayRef *node);
static void   findFuncExpr(NodeInfoEnv& env, const FuncExpr *node);
static void   findNamedArgExpr(NodeInfoEnv& env, const NamedArgExpr *node);
static void   findOpExpr(NodeInfoEnv& env, const OpExpr *node);
static void   findDistinctExpr(NodeInfoEnv& env, const DistinctExpr *node);
static void   findNullIfExpr(NodeInfoEnv& env, const NullIfExpr *node);
static void   findScalarArrayOpExpr(NodeInfoEnv& env, const ScalarArrayOpExpr *node);
static void   findBoolExpr(NodeInfoEnv& env, const BoolExpr *node);
static void   findSubLink(NodeInfoEnv& env, const SubLink *node);
static void   findSubPlan(NodeInfoEnv& env, const SubPlan *node);
static void   findAlternativeSubPlan(NodeInfoEnv& env, const AlternativeSubPlan *node);
static void   findFieldSelect(NodeInfoEnv& env, const FieldSelect *node);
static void   findFieldStore(NodeInfoEnv& env, const FieldStore *node);
static void   findRelabelType(NodeInfoEnv& env, const RelabelType *node);
static void   findCoerceViaIO(NodeInfoEnv& env, const CoerceViaIO *node);
static void   findArrayCoerceExpr(NodeInfoEnv& env, const ArrayCoerceExpr *node);
static void   findConvertRowtypeExpr(NodeInfoEnv& env, const ConvertRowtypeExpr *node);
static void   findCollateExpr(NodeInfoEnv& env, const CollateExpr *node);
static void   findCaseExpr(NodeInfoEnv& env, const CaseExpr *node);
static void   findCaseWhen(NodeInfoEnv& env, const CaseWhen *node);
static void   findCaseTestExpr(NodeInfoEnv& env, const CaseTestExpr *node);
static void   findArrayExpr(NodeInfoEnv& env, const ArrayExpr *node);
static void   findRowExpr(NodeInfoEnv& env, const RowExpr *node);
static void   findRowCompareExpr(NodeInfoEnv& env, const RowCompareExpr *node);
static void   findCoalesceExpr(NodeInfoEnv& env, const CoalesceExpr *node);
static void   findMinMaxExpr(NodeInfoEnv& env, const MinMaxExpr *node);
static void   findXmlExpr(NodeInfoEnv& env, const XmlExpr *node);
static void   findNullTest(NodeInfoEnv& env, const NullTest *node);
static void   findBooleanTest(NodeInfoEnv& env, const BooleanTest *node);
static void   findCoerceToDomain(NodeInfoEnv& env, const CoerceToDomain *node);
static void   findCoerceToDomainValue(NodeInfoEnv& env, const CoerceToDomainValue *node);
static void   findSetToDefault(NodeInfoEnv& env, const SetToDefault *node);
static void   findCurrentOfExpr(NodeInfoEnv& env, const CurrentOfExpr *node);
static void   findTargetEntry(NodeInfoEnv& env, const TargetEntry *node);
static void   findRangeTblRef(NodeInfoEnv& env, const RangeTblRef *node);
static void   findJoinExpr(NodeInfoEnv& env, const JoinExpr *node);
static void   findFromExpr(NodeInfoEnv& env, const FromExpr *node);
static void findPlannerGlobal(NodeInfoEnv& env, const PlannerGlobal *node);
static void findPlannerInfo(NodeInfoEnv& env, const PlannerInfo *node);
static void findRelOptInfo(NodeInfoEnv& env, const RelOptInfo *node);
static void findQuery(NodeInfoEnv& env, const Query *node);
static void findSortGroupClause(NodeInfoEnv& env, const SortGroupClause *node);
static void findRangeTblEntry(NodeInfoEnv& env, const RangeTblEntry *node);

static void outputNode(NodeInfoEnv& env, const void *obj);
static void outputValue(NodeInfoEnv& env, const Value *node);
static void outputPlannedStmt(NodeInfoEnv& env, const PlannedStmt *node);
static void outputPlan(NodeInfoEnv& env, const Plan *node);
static void   outputModifyTable(NodeInfoEnv& env, const ModifyTable *node);
static void   outputAppend(NodeInfoEnv& env, const Append *node);
static void   outputMergeAppend(NodeInfoEnv& env, const MergeAppend *node);
static void   outputRecursiveUnion(NodeInfoEnv& env, const RecursiveUnion *node);
static void   outputBitmapAnd(NodeInfoEnv& env, const BitmapAnd *node);
static void   outputBitmapOr(NodeInfoEnv& env, const BitmapOr *node);
static void   outputScan(NodeInfoEnv& env, const Scan *node);
static void     outputSeqScan(NodeInfoEnv& env, const SeqScan *node);
static void     outputIndexScan(NodeInfoEnv& env, const IndexScan *node);
static void     outputIndexOnlyScan(NodeInfoEnv& env, const IndexOnlyScan *node);
static void     outputBitmapIndexScan(NodeInfoEnv& env, const BitmapIndexScan *node);
static void     outputBitmapHeapScan(NodeInfoEnv& env, const BitmapHeapScan *node);
static void     outputTidScan(NodeInfoEnv& env, const TidScan *node);
static void     outputSubqueryScan(NodeInfoEnv& env, const SubqueryScan *node);
static void     outputFunctionScan(NodeInfoEnv& env, const FunctionScan *node);
static void     outputValuesScan(NodeInfoEnv& env, const ValuesScan *node);
static void     outputCteScan(NodeInfoEnv& env, const CteScan *node);
static void     outputWorkTableScan(NodeInfoEnv& env, const WorkTableScan *node);
static void     outputForeignScan(NodeInfoEnv& env, const ForeignScan *node);
static void   outputJoin(NodeInfoEnv& env, const Join *node);
static void     outputNestLoop(NodeInfoEnv& env, const NestLoop *node);
static void     outputMergeJoin(NodeInfoEnv& env, const MergeJoin *node);
static void     outputHashJoin(NodeInfoEnv& env, const HashJoin *node);
static void   outputAgg(NodeInfoEnv& env, const Agg *node);
static void   outputWindowAgg(NodeInfoEnv& env, const WindowAgg *node);
static void   outputGroup(NodeInfoEnv& env, const Group *node);
static void   outputMaterial(NodeInfoEnv& env, const Material *node);
static void   outputSort(NodeInfoEnv& env, const Sort *node);
static void   outputUnique(NodeInfoEnv& env, const Unique *node);
static void   outputHash(NodeInfoEnv& env, const Hash *node);
static void   outputSetOp(NodeInfoEnv& env, const SetOp *node);
static void   outputLockRows(NodeInfoEnv& env, const LockRows *node);
static void   outputLimit(NodeInfoEnv& env, const Limit *node);
static void   outputNestLoopParam(NodeInfoEnv& env, const NestLoopParam *node);
static void   outputPlanRowMark(NodeInfoEnv& env, const PlanRowMark *node);
static void   outputPlanInvalItem(NodeInfoEnv& env, const PlanInvalItem *node);
static void   outputAlias(NodeInfoEnv& env, const Alias *node);
static void   outputRangeVar(NodeInfoEnv& env, const RangeVar *node);
static void   outputIntoClause(NodeInfoEnv& env, const IntoClause *node);
/* static void outputExpr(NodeInfoEnv& env, const Expr *node); */
static void   outputVar(NodeInfoEnv& env, const Var *node);
static void   outputConst(NodeInfoEnv& env, const Const *node);
static void   outputParam(NodeInfoEnv& env, const Param *node);
static void   outputAggref(NodeInfoEnv& env, const Aggref *node);
static void   outputWindowFunc(NodeInfoEnv& env, const WindowFunc *node);
static void   outputArrayRef(NodeInfoEnv& env, const ArrayRef *node);
static void   outputFuncExpr(NodeInfoEnv& env, const FuncExpr *node);
static void   outputNamedArgExpr(NodeInfoEnv& env, const NamedArgExpr *node);
static void   outputOpExpr(NodeInfoEnv& env, const OpExpr *node);
static void   outputDistinctExpr(NodeInfoEnv& env, const DistinctExpr *node);
static void   outputNullIfExpr(NodeInfoEnv& env, const NullIfExpr *node);
static void   outputScalarArrayOpExpr(NodeInfoEnv& env, const ScalarArrayOpExpr *node);
static void   outputBoolExpr(NodeInfoEnv& env, const BoolExpr *node);
static void   outputSubLink(NodeInfoEnv& env, const SubLink *node);
static void   outputSubPlan(NodeInfoEnv& env, const SubPlan *node);
static void   outputAlternativeSubPlan(NodeInfoEnv& env, const AlternativeSubPlan *node);
static void   outputFieldSelect(NodeInfoEnv& env, const FieldSelect *node);
static void   outputFieldStore(NodeInfoEnv& env, const FieldStore *node);
static void   outputRelabelType(NodeInfoEnv& env, const RelabelType *node);
static void   outputCoerceViaIO(NodeInfoEnv& env, const CoerceViaIO *node);
static void   outputArrayCoerceExpr(NodeInfoEnv& env, const ArrayCoerceExpr *node);
static void   outputConvertRowtypeExpr(NodeInfoEnv& env, const ConvertRowtypeExpr *node);
static void   outputCollateExpr(NodeInfoEnv& env, const CollateExpr *node);
static void   outputCaseExpr(NodeInfoEnv& env, const CaseExpr *node);
static void   outputCaseWhen(NodeInfoEnv& env, const CaseWhen *node);
static void   outputCaseTestExpr(NodeInfoEnv& env, const CaseTestExpr *node);
static void   outputArrayExpr(NodeInfoEnv& env, const ArrayExpr *node);
static void   outputRowExpr(NodeInfoEnv& env, const RowExpr *node);
static void   outputRowCompareExpr(NodeInfoEnv& env, const RowCompareExpr *node);
static void   outputCoalesceExpr(NodeInfoEnv& env, const CoalesceExpr *node);
static void   outputMinMaxExpr(NodeInfoEnv& env, const MinMaxExpr *node);
static void   outputXmlExpr(NodeInfoEnv& env, const XmlExpr *node);
static void   outputNullTest(NodeInfoEnv& env, const NullTest *node);
static void   outputBooleanTest(NodeInfoEnv& env, const BooleanTest *node);
static void   outputCoerceToDomain(NodeInfoEnv& env, const CoerceToDomain *node);
static void   outputCoerceToDomainValue(NodeInfoEnv& env, const CoerceToDomainValue *node);
static void   outputSetToDefault(NodeInfoEnv& env, const SetToDefault *node);
static void   outputCurrentOfExpr(NodeInfoEnv& env, const CurrentOfExpr *node);
static void   outputTargetEntry(NodeInfoEnv& env, const TargetEntry *node);
static void   outputRangeTblRef(NodeInfoEnv& env, const RangeTblRef *node);
static void   outputJoinExpr(NodeInfoEnv& env, const JoinExpr *node);
static void   outputFromExpr(NodeInfoEnv& env, const FromExpr *node);
static void outputPlannerGlobal(NodeInfoEnv& env, const PlannerGlobal *node);
static void outputPlannerInfo(NodeInfoEnv& env, const PlannerInfo *node);
static void outputRelOptInfo(NodeInfoEnv& env, const RelOptInfo *node);
static void outputQuery(NodeInfoEnv& env, const Query *node);
static void outputSortGroupClause(NodeInfoEnv& env, const SortGroupClause *node);
static void outputRangeTblEntry(NodeInfoEnv& env, const RangeTblEntry *node);

char *
get_plan_tree_dot_string(const char *title, const void *obj)
{
	char *buffer = NULL;

	try
	{
		NodeInfoEnv env(title);

		findNode(env, NULL, NULL, obj);
		env.outputAllNodes();
		
		buffer = pstrdup(env.c_str());
	}
	catch (...)
	{
		elog(ERROR, "fatal error in _nodeToString");
	}

	return buffer;
}


/****************************************************************************/
/*                                                                          */
/****************************************************************************/
static void
findNode(NodeInfoEnv& env, const void *parent, const char *fldname, const void *obj)
{
	if (obj == NULL)
		return;

	if (env.hasNode(obj))
		return;

	env.registerNode(obj);

	if (parent)
		env.registerEdge(parent, obj, fldname);

	if (IsA(obj, Integer)  ||
		IsA(obj, Float)    ||
		IsA(obj, String)   ||
		IsA(obj, BitString)||
		IsA(obj, IntList)  ||
		IsA(obj, OidList))
		return;

	/* elog(LOG, "finNode: %d", (int)nodeTag(obj)); */

	if (IsA(obj, List))
	{
		int i = 0;
		const List *node = reinterpret_cast<const List*>(obj);
		const ListCell *lc;

		foreach(lc, node)
		{
			char buffer[256];
			sprintf(buffer, "%d", i + 1);
			findNode(env, obj, buffer, lfirst(lc));
			i++;
		}

		return;
	}

	switch (nodeTag(obj))
	{
		case T_Plan:
			findPlan(env, reinterpret_cast<const Plan*>(obj));
			break;

#if 0
		case T_Env:
			findEnv(env, reinterpret_cast<const Env*>(obj));
			break;
#endif
		case T_ModifyTable:
			findModifyTable(env, reinterpret_cast<const ModifyTable*>(obj));
			break;
		case T_Append:
			findAppend(env, reinterpret_cast<const Append*>(obj));
			break;
		case T_MergeAppend:
			findMergeAppend(env, reinterpret_cast<const MergeAppend*>(obj));
			break;
		case T_RecursiveUnion:
			findRecursiveUnion(env, reinterpret_cast<const RecursiveUnion*>(obj));
			break;
		case T_BitmapAnd:
			findBitmapAnd(env, reinterpret_cast<const BitmapAnd*>(obj));
			break;
		case T_BitmapOr:
			findBitmapOr(env, reinterpret_cast<const BitmapOr*>(obj));
			break;
		case T_Scan:
			findScan(env, reinterpret_cast<const Scan*>(obj));
			break;
		case T_SeqScan:
			findSeqScan(env, reinterpret_cast<const SeqScan*>(obj));
			break;
		case T_IndexScan:
			findIndexScan(env, reinterpret_cast<const IndexScan*>(obj));
			break;
		case T_IndexOnlyScan:
			findIndexOnlyScan(env, reinterpret_cast<const IndexOnlyScan*>(obj));
			break;
		case T_BitmapIndexScan:
			findBitmapIndexScan(env, reinterpret_cast<const BitmapIndexScan*>(obj));
			break;
		case T_BitmapHeapScan:
			findBitmapHeapScan(env, reinterpret_cast<const BitmapHeapScan*>(obj));
			break;
		case T_TidScan:
			findTidScan(env, reinterpret_cast<const TidScan*>(obj));
			break;
		case T_SubqueryScan:
			findSubqueryScan(env, reinterpret_cast<const SubqueryScan*>(obj));
			break;
		case T_FunctionScan:
			findFunctionScan(env, reinterpret_cast<const FunctionScan*>(obj));
			break;
		case T_ValuesScan:
			findValuesScan(env, reinterpret_cast<const ValuesScan*>(obj));
			break;
		case T_CteScan:
			findCteScan(env, reinterpret_cast<const CteScan*>(obj));
			break;
		case T_WorkTableScan:
			findWorkTableScan(env, reinterpret_cast<const WorkTableScan*>(obj));
			break;
		case T_ForeignScan:
			findForeignScan(env, reinterpret_cast<const ForeignScan*>(obj));
			break;
		case T_Join:
			findJoin(env, reinterpret_cast<const Join*>(obj));
			break;
		case T_NestLoop:
			findNestLoop(env, reinterpret_cast<const NestLoop*>(obj));
			break;
		case T_MergeJoin:
			findMergeJoin(env, reinterpret_cast<const MergeJoin*>(obj));
			break;
		case T_HashJoin:
			findHashJoin(env, reinterpret_cast<const HashJoin*>(obj));
			break;
		case T_Agg:
			findAgg(env, reinterpret_cast<const Agg*>(obj));
			break;
		case T_WindowAgg:
			findWindowAgg(env, reinterpret_cast<const WindowAgg*>(obj));
			break;
		case T_Group:
			findGroup(env, reinterpret_cast<const Group*>(obj));
			break;
		case T_Material:
			findMaterial(env, reinterpret_cast<const Material*>(obj));
			break;
		case T_Sort:
			findSort(env, reinterpret_cast<const Sort*>(obj));
			break;
		case T_Unique:
			findUnique(env, reinterpret_cast<const Unique*>(obj));
			break;
		case T_Hash:
			findHash(env, reinterpret_cast<const Hash*>(obj));
			break;
		case T_SetOp:
			findSetOp(env, reinterpret_cast<const SetOp*>(obj));
			break;
		case T_LockRows:
			findLockRows(env, reinterpret_cast<const LockRows*>(obj));
			break;
		case T_Limit:
			findLimit(env, reinterpret_cast<const Limit*>(obj));
			break;
		case T_NestLoopParam:
			findNestLoopParam(env, reinterpret_cast<const NestLoopParam*>(obj));
			break;
		case T_PlanRowMark:
			findPlanRowMark(env, reinterpret_cast<const PlanRowMark*>(obj));
			break;
		case T_PlanInvalItem:
			findPlanInvalItem(env, reinterpret_cast<const PlanInvalItem*>(obj));
			break;
		case T_Alias:
			findAlias(env, reinterpret_cast<const Alias*>(obj));
			break;
		case T_RangeVar:
			findRangeVar(env, reinterpret_cast<const RangeVar*>(obj));
			break;
		case T_IntoClause:
			findIntoClause(env, reinterpret_cast<const IntoClause*>(obj));
			break;
		case T_Var:
			findVar(env, reinterpret_cast<const Var*>(obj));
			break;
		case T_Const:
			findConst(env, reinterpret_cast<const Const*>(obj));
			break;
		case T_Param:
			findParam(env, reinterpret_cast<const Param*>(obj));
			break;
		case T_Aggref:
			findAggref(env, reinterpret_cast<const Aggref*>(obj));
			break;
		case T_WindowFunc:
			findWindowFunc(env, reinterpret_cast<const WindowFunc*>(obj));
			break;
		case T_ArrayRef:
			findArrayRef(env, reinterpret_cast<const ArrayRef*>(obj));
			break;
		case T_FuncExpr:
			findFuncExpr(env, reinterpret_cast<const FuncExpr*>(obj));
			break;
		case T_NamedArgExpr:
			findNamedArgExpr(env, reinterpret_cast<const NamedArgExpr*>(obj));
			break;
		case T_OpExpr:
			findOpExpr(env, reinterpret_cast<const OpExpr*>(obj));
			break;
		case T_DistinctExpr:
			findDistinctExpr(env, reinterpret_cast<const DistinctExpr*>(obj));
			break;
		case T_NullIfExpr:
			findNullIfExpr(env, reinterpret_cast<const NullIfExpr*>(obj));
			break;
		case T_ScalarArrayOpExpr:
			findScalarArrayOpExpr(env, reinterpret_cast<const ScalarArrayOpExpr*>(obj));
			break;
		case T_BoolExpr:
			findBoolExpr(env, reinterpret_cast<const BoolExpr*>(obj));
			break;
		case T_SubLink:
			findSubLink(env, reinterpret_cast<const SubLink*>(obj));
			break;
		case T_SubPlan:
			findSubPlan(env, reinterpret_cast<const SubPlan*>(obj));
			break;
		case T_AlternativeSubPlan:
			findAlternativeSubPlan(env, reinterpret_cast<const AlternativeSubPlan*>(obj));
			break;
		case T_FieldSelect:
			findFieldSelect(env, reinterpret_cast<const FieldSelect*>(obj));
			break;
		case T_FieldStore:
			findFieldStore(env, reinterpret_cast<const FieldStore*>(obj));
			break;
		case T_RelabelType:
			findRelabelType(env, reinterpret_cast<const RelabelType*>(obj));
			break;
		case T_CoerceViaIO:
			findCoerceViaIO(env, reinterpret_cast<const CoerceViaIO*>(obj));
			break;
		case T_ArrayCoerceExpr:
			findArrayCoerceExpr(env, reinterpret_cast<const ArrayCoerceExpr*>(obj));
			break;
		case T_ConvertRowtypeExpr:
			findConvertRowtypeExpr(env, reinterpret_cast<const ConvertRowtypeExpr*>(obj));
			break;
		case T_CollateExpr:
			findCollateExpr(env, reinterpret_cast<const CollateExpr*>(obj));
			break;
		case T_CaseExpr:
			findCaseExpr(env, reinterpret_cast<const CaseExpr*>(obj));
			break;
		case T_CaseWhen:
			findCaseWhen(env, reinterpret_cast<const CaseWhen*>(obj));
			break;
		case T_CaseTestExpr:
			findCaseTestExpr(env, reinterpret_cast<const CaseTestExpr*>(obj));
			break;
		case T_ArrayExpr:
			findArrayExpr(env, reinterpret_cast<const ArrayExpr*>(obj));
			break;
		case T_RowExpr:
			findRowExpr(env, reinterpret_cast<const RowExpr*>(obj));
			break;
		case T_RowCompareExpr:
			findRowCompareExpr(env, reinterpret_cast<const RowCompareExpr*>(obj));
			break;
		case T_CoalesceExpr:
			findCoalesceExpr(env, reinterpret_cast<const CoalesceExpr*>(obj));
			break;
		case T_MinMaxExpr:
			findMinMaxExpr(env, reinterpret_cast<const MinMaxExpr*>(obj));
			break;
		case T_XmlExpr:
			findXmlExpr(env, reinterpret_cast<const XmlExpr*>(obj));
			break;
		case T_NullTest:
			findNullTest(env, reinterpret_cast<const NullTest*>(obj));
			break;
		case T_BooleanTest:
			findBooleanTest(env, reinterpret_cast<const BooleanTest*>(obj));
			break;
		case T_CoerceToDomain:
			findCoerceToDomain(env, reinterpret_cast<const CoerceToDomain*>(obj));
			break;
		case T_CoerceToDomainValue:
			findCoerceToDomainValue(env, reinterpret_cast<const CoerceToDomainValue*>(obj));
			break;
		case T_SetToDefault:
			findSetToDefault(env, reinterpret_cast<const SetToDefault*>(obj));
			break;
		case T_CurrentOfExpr:
			findCurrentOfExpr(env, reinterpret_cast<const CurrentOfExpr*>(obj));
			break;
		case T_TargetEntry:
			findTargetEntry(env, reinterpret_cast<const TargetEntry*>(obj));
			break;
		case T_RangeTblRef:
			findRangeTblRef(env, reinterpret_cast<const RangeTblRef*>(obj));
			break;
		case T_JoinExpr:
			findJoinExpr(env, reinterpret_cast<const JoinExpr*>(obj));
			break;
		case T_FromExpr:
			findFromExpr(env, reinterpret_cast<const FromExpr*>(obj));
			break;
#if 0
		case T_Path:
			findPath(env, obj);
			break;
		case T_IndexPath:
			findIndexPath(env, obj);
			break;
		case T_BitmapHeapPath:
			findBitmapHeapPath(env, obj);
			break;
		case T_BitmapAndPath:
			findBitmapAndPath(env, obj);
			break;
		case T_BitmapOrPath:
			findBitmapOrPath(env, obj);
			break;
		case T_TidPath:
			findTidPath(env, obj);
			break;
		case T_ForeignPath:
			findForeignPath(env, obj);
			break;
		case T_AppendPath:
			findAppendPath(env, obj);
			break;
		case T_MergeAppendPath:
			findMergeAppendPath(env, obj);
			break;
		case T_EnvPath:
			findEnvPath(env, obj);
			break;
		case T_MaterialPath:
			findMaterialPath(env, obj);
			break;
		case T_UniquePath:
			findUniquePath(env, obj);
			break;
		case T_NestPath:
			findNestPath(env, obj);
			break;
		case T_MergePath:
			findMergePath(env, obj);
			break;
		case T_HashPath:
			findHashPath(env, obj);
			break;
#endif
		case T_PlannerGlobal:
			findPlannerGlobal(env, reinterpret_cast<const PlannerGlobal*>(obj));
			break;
		case T_PlannerInfo:
			findPlannerInfo(env, reinterpret_cast<const PlannerInfo*>(obj));
			break;
		case T_RelOptInfo:
			findRelOptInfo(env, reinterpret_cast<const RelOptInfo*>(obj));
			break;
#if 0
		case T_IndexOptInfo:
			findIndexOptInfo(env, obj);
			break;
		case T_EquivalenceClass:
			findEquivalenceClass(env, obj);
			break;
		case T_EquivalenceMember:
			findEquivalenceMember(env, obj);
			break;
		case T_PathKey:
			findPathKey(env, obj);
			break;
		case T_ParamPathInfo:
			findParamPathInfo(env, obj);
			break;
		case T_RestrictInfo:
			findRestrictInfo(env, obj);
			break;
		case T_PlaceHolderVar:
			findPlaceHolderVar(env, obj);
			break;
		case T_SpecialJoinInfo:
			findSpecialJoinInfo(env, obj);
			break;
		case T_AppendRelInfo:
			findAppendRelInfo(env, obj);
			break;
		case T_PlaceHolderInfo:
			findPlaceHolderInfo(env, obj);
			break;
		case T_MinMaxAggInfo:
			findMinMaxAggInfo(env, obj);
			break;
		case T_PlannerParamItem:
			findPlannerParamItem(env, obj);
			break;

		case T_CreateStmt:
			findCreateStmt(env, obj);
			break;
		case T_CreateForeignTableStmt:
			findCreateForeignTableStmt(env, obj);
			break;
		case T_IndexStmt:
			findIndexStmt(env, obj);
			break;
		case T_NotifyStmt:
			findNotifyStmt(env, obj);
			break;
		case T_DeclareCursorStmt:
			findDeclareCursorStmt(env, obj);
			break;
		case T_SelectStmt:
			findSelectStmt(env, obj);
			break;
		case T_ColumnDef:
			findColumnDef(env, obj);
			break;
		case T_TypeName:
			findTypeName(env, obj);
			break;
		case T_TypeCast:
			findTypeCast(env, obj);
			break;
		case T_CollateClause:
			findCollateClause(env, obj);
			break;
		case T_IndexElem:
			findIndexElem(env, obj);
			break;
#endif
		case T_SortGroupClause:
			findSortGroupClause(env, reinterpret_cast<const SortGroupClause*>(obj));
			break;
#if 0
		case T_WindowClause:
			findWindowClause(env, obj);
			break;
		case T_RowMarkClause:
			findRowMarkClause(env, obj);
			break;
		case T_WithClause:
			findWithClause(env, obj);
			break;
		case T_CommonTableExpr:
			findCommonTableExpr(env, obj);
			break;
		case T_SetOperationStmt:
			findSetOperationStmt(env, obj);
			break;
#endif
		case T_RangeTblEntry:
			findRangeTblEntry(env, reinterpret_cast<const RangeTblEntry*>(obj));
			break;
#if 0
		case T_A_Expr:
			findAExpr(env, obj);
			break;
		case T_ColumnRef:
			findColumnRef(env, obj);
			break;
		case T_ParamRef:
			findParamRef(env, obj);
			break;
		case T_A_Const:
			findAConst(env, obj);
			break;
		case T_A_Star:
			findA_Star(env, obj);
			break;
		case T_A_Indices:
			findA_Indices(env, obj);
			break;
		case T_A_Indirection:
			findA_Indirection(env, obj);
			break;
		case T_A_ArrayExpr:
			findA_ArrayExpr(env, obj);
			break;
		case T_ResTarget:
			findResTarget(env, obj);
			break;
		case T_SortBy:
			findSortBy(env, obj);
			break;
		case T_WindowDef:
			findWindowDef(env, obj);
			break;
		case T_RangeSubselect:
			findRangeSubselect(env, obj);
			break;
		case T_RangeFunction:
			findRangeFunction(env, obj);
			break;
		case T_Constraint:
			findConstraint(env, obj);
			break;
		case T_FuncCall:
			findFuncCall(env, obj);
			break;
		case T_DefElem:
			findDefElem(env, obj);
			break;
		case T_TableLikeClause:
			findTableLikeClause(env, obj);
			break;
		case T_LockingClause:
			findLockingClause(env, obj);
			break;
		case T_XmlSerialize:
			findXmlSerialize(env, obj);
			break;
#endif
			/* Statement Nodes */
		case T_Query:
			findQuery(env, reinterpret_cast<const Query*>(obj));
			break;
		case T_PlannedStmt:
			findPlannedStmt(env, reinterpret_cast<const PlannedStmt*>(obj));
			break;
		case T_InsertStmt:
		case T_DeleteStmt:
		case T_UpdateStmt:
		case T_SelectStmt:
		case T_AlterTableStmt:
		case T_AlterTableCmd:
		case T_AlterDomainStmt:
		case T_SetOperationStmt:
		case T_GrantStmt:
		case T_GrantRoleStmt:
		case T_AlterDefaultPrivilegesStmt:
		case T_ClosePortalStmt:
		case T_ClusterStmt:
		case T_CopyStmt:
		case T_CreateStmt:
		case T_DefineStmt:
		case T_DropStmt:
		case T_TruncateStmt:
		case T_CommentStmt:
		case T_FetchStmt:
		case T_IndexStmt:
		case T_CreateFunctionStmt:
		case T_AlterFunctionStmt:
		case T_DoStmt:
		case T_RenameStmt:
		case T_RuleStmt:
		case T_NotifyStmt:
		case T_ListenStmt:
		case T_UnlistenStmt:
		case T_TransactionStmt:
		case T_ViewStmt:
		case T_LoadStmt:
		case T_CreateDomainStmt:
		case T_CreatedbStmt:
		case T_DropdbStmt:
		case T_VacuumStmt:
		case T_ExplainStmt:
		case T_CreateTableAsStmt:
		case T_CreateSeqStmt:
		case T_AlterSeqStmt:
		case T_VariableSetStmt:
		case T_VariableShowStmt:
		case T_DiscardStmt:
		case T_CreateTrigStmt:
		case T_CreatePLangStmt:
		case T_CreateRoleStmt:
		case T_AlterRoleStmt:
		case T_DropRoleStmt:
		case T_LockStmt:
		case T_ConstraintsSetStmt:
		case T_ReindexStmt:
		case T_CheckPointStmt:
		case T_CreateSchemaStmt:
		case T_AlterDatabaseStmt:
		case T_AlterDatabaseSetStmt:
		case T_AlterRoleSetStmt:
		case T_CreateConversionStmt:
		case T_CreateCastStmt:
		case T_CreateOpClassStmt:
		case T_CreateOpFamilyStmt:
		case T_AlterOpFamilyStmt:
		case T_PrepareStmt:
		case T_ExecuteStmt:
		case T_DeallocateStmt:
		case T_DeclareCursorStmt:
		case T_CreateTableSpaceStmt:
		case T_DropTableSpaceStmt:
		case T_AlterObjectSchemaStmt:
		case T_AlterOwnerStmt:
		case T_DropOwnedStmt:
		case T_ReassignOwnedStmt:
		case T_CompositeTypeStmt:
		case T_CreateEnumStmt:
		case T_CreateRangeStmt:
		case T_AlterEnumStmt:
		case T_AlterTSDictionaryStmt:
		case T_AlterTSConfigurationStmt:
		case T_CreateFdwStmt:
		case T_AlterFdwStmt:
		case T_CreateForeignServerStmt:
		case T_AlterForeignServerStmt:
		case T_CreateUserMappingStmt:
		case T_AlterUserMappingStmt:
		case T_DropUserMappingStmt:
		case T_AlterTableSpaceOptionsStmt:
#if PG_VERSION_NUM >= 90400
		case T_AlterTableSpaceMoveStmt:
#endif
		case T_SecLabelStmt:
		case T_CreateForeignTableStmt:

		default:
			elog(WARNING, "could not dump unrecognized node type: %d",
				 (int) nodeTag(obj));
			break;
	}
}

static void
findNodeIndex(NodeInfoEnv& env, const void *parent, const char *fldname, int index, const void *obj)
{
	char buffer[256];
	sprintf(buffer, "%s%d", fldname, index);
	findNode(env, parent, fldname, obj);
}

static void
findPlannedStmt(NodeInfoEnv& env, const PlannedStmt *node)
{
	FIND_NODE(planTree);
	FIND_NODE(rtable);
	FIND_NODE(resultRelations);
	FIND_NODE(utilityStmt);
	FIND_NODE(subplans);
	FIND_NODE(rowMarks);
	FIND_NODE(relationOids);
	FIND_NODE(invalItems);
}

static void
findPlan(NodeInfoEnv& env, const Plan *node)
{
	FIND_TARGETLIST(targetlist);
	FIND_EXPRLIST(qual);
	FIND_PLAN(lefttree);
	FIND_PLAN(righttree);
	FIND_NODE(initPlan); /* list of plans */
}

static void
findModifyTable(NodeInfoEnv& env, const ModifyTable *node)
{
	findPlan(env, &node->plan);
	FIND_NODE(resultRelations);
	FIND_NODE(plans);
	FIND_NODE(withCheckOptionLists);
	FIND_NODE(returningLists);
	FIND_NODE(fdwPrivLists);
	FIND_NODE(rowMarks);
}

static void
findAppend(NodeInfoEnv& env, const Append *node)
{
	findPlan(env, &node->plan);
	FIND_NODE(appendplans);
}

static void
findMergeAppend(NodeInfoEnv& env, const MergeAppend *node)
{
	findPlan(env, &node->plan);
	FIND_NODE(mergeplans); /* list of plans */
}

static void
findRecursiveUnion(NodeInfoEnv& env, const RecursiveUnion *node)
{
	findPlan(env, &node->plan);
}

static void
findBitmapAnd(NodeInfoEnv& env, const BitmapAnd *node)
{
	findPlan(env, &node->plan);
	FIND_NODE(bitmapplans); /* list of plans */
}

static void
findBitmapOr(NodeInfoEnv& env, const BitmapOr *node)
{
	findPlan(env, &node->plan);
	FIND_NODE(bitmapplans); /* list of plans */
}

static void
findScan(NodeInfoEnv& env, const Scan *node)
{
	findPlan(env, &node->plan);
}

static void
findSeqScan(NodeInfoEnv& env, const SeqScan *node)
{
	findPlan(env, &node->plan);
}

static void
findIndexScan(NodeInfoEnv& env, const IndexScan *node)
{
	findScan(env, &node->scan);
	FIND_EXPRLIST(indexqual);
	FIND_EXPRLIST(indexqualorig);
	FIND_EXPRLIST(indexorderby);
	FIND_EXPRLIST(indexorderbyorig);
}

static void
findIndexOnlyScan(NodeInfoEnv& env, const IndexOnlyScan *node)
{
	findScan(env, &node->scan);
	FIND_EXPRLIST(indexqual);
	FIND_EXPRLIST(indexorderby);
	FIND_EXPRLIST(indextlist);
}

static void
findBitmapIndexScan(NodeInfoEnv& env, const BitmapIndexScan *node)
{
	findScan(env, &node->scan);
	FIND_EXPRLIST(indexqual);
	FIND_EXPRLIST(indexqualorig);
}

static void
findBitmapHeapScan(NodeInfoEnv& env, const BitmapHeapScan *node)
{
	findScan(env, &node->scan);
	FIND_EXPRLIST(bitmapqualorig);
}

static void
findTidScan(NodeInfoEnv& env, const TidScan *node)
{
	findScan(env, &node->scan);
	FIND_EXPRLIST(tidquals);
}

static void
findSubqueryScan(NodeInfoEnv& env, const SubqueryScan *node)
{
	findScan(env, &node->scan);
	FIND_PLAN(subplan);
}

static void
findFunctionScan(NodeInfoEnv& env, const FunctionScan *node)
{
	findScan(env, &node->scan);

#if PG_VERSION_NUM >= 90400
	FIND_NODE(functions);
#else
	FIND_NODE(funcexpr);
	FIND_NODE(funccolnames);
	FIND_NODE(funccoltypes);
	FIND_NODE(funccoltypmods);
	FIND_NODE(funccolcollations);
#endif
}

static void
findValuesScan(NodeInfoEnv& env, const ValuesScan *node)
{
	findScan(env, &node->scan);
	FIND_NODE(values_lists);
}

static void
findCteScan(NodeInfoEnv& env, const CteScan *node)
{
	findScan(env, &node->scan);
}

static void
findWorkTableScan(NodeInfoEnv& env, const WorkTableScan *node)
{
	findScan(env, &node->scan);
}

static void
findForeignScan(NodeInfoEnv& env, const ForeignScan *node)
{
	findScan(env, &node->scan);
	FIND_NODE(fdw_exprs);
	FIND_NODE(fdw_private);
}

static void
findJoin(NodeInfoEnv& env, const Join *node)
{
	findPlan(env, &node->plan);
	FIND_EXPRLIST(joinqual);
}

static void
findNestLoop(NodeInfoEnv& env, const NestLoop *node)
{
	findJoin(env, &node->join);
	FIND_NODE(nestParams); /* list of NestLoopParam */
}

static void
findMergeJoin(NodeInfoEnv& env, const MergeJoin *node)
{
	findJoin(env, &node->join);
	FIND_EXPRLIST(mergeclauses); /* list of OpExr */
}

static void
findHashJoin(NodeInfoEnv& env, const HashJoin *node)
{
	findJoin(env, &node->join);
	FIND_EXPRLIST(hashclauses);
}

static void
findAgg(NodeInfoEnv& env, const Agg *node)
{
	findPlan(env, &node->plan);
}

static void
findWindowAgg(NodeInfoEnv& env, const WindowAgg *node)
{
	findPlan(env, &node->plan);
	FIND_EXPRLIST(startOffset);
	FIND_EXPRLIST(endOffset);
}

static void
findGroup(NodeInfoEnv& env, const Group *node)
{
	findPlan(env, &node->plan);
}

static void
findMaterial(NodeInfoEnv& env, const Material *node)
{
	findPlan(env, &node->plan);	
}

static void
findSort(NodeInfoEnv& env, const Sort *node)
{
	findPlan(env, &node->plan);	
}

static void
findUnique(NodeInfoEnv& env, const Unique *node)
{
	findPlan(env, &node->plan);
}

static void
findHash(NodeInfoEnv& env, const Hash *node)
{
	findPlan(env, &node->plan);
}

static void
findSetOp(NodeInfoEnv& env, const SetOp *node)
{
	findPlan(env, &node->plan);
}

static void
findLockRows(NodeInfoEnv& env, const LockRows *node)
{
	findPlan(env, &node->plan);
	FIND_NODE(rowMarks);
}

static void
findLimit(NodeInfoEnv& env, const Limit *node)
{
	findPlan(env, &node->plan);
	FIND_EXPRLIST(limitOffset);
	FIND_EXPRLIST(limitCount);
}

static void
findNestLoopParam(NodeInfoEnv& env, const NestLoopParam *node)
{
	FIND_NODE(paramval);
}

static void
findPlanRowMark(NodeInfoEnv& env, const PlanRowMark *node)
{
	/* nothing */
}

static void
findPlanInvalItem(NodeInfoEnv& env, const PlanInvalItem *node)
{
	/* nothing */
}

static void
findAlias(NodeInfoEnv& env, const Alias *node)
{
	FIND_NODE(colnames);
}

static void
findIntoClause(NodeInfoEnv& env, const IntoClause *node)
{
	FIND_NODE(rel);
	FIND_NODE(colNames);
	FIND_NODE(options);
	FIND_NODE(viewQuery);
}

static void
findRangeVar(NodeInfoEnv& env, const RangeVar *node)
{
	/* nothing */
}

static void
findVar(NodeInfoEnv& env, const Var *node)
{
	/* nothing */
}

static void
findConst(NodeInfoEnv& env, const Const *node)
{
	/* nothing */
}

static void
findParam(NodeInfoEnv& env, const Param *node)
{
	/* nothing */
}

static void
findAggref(NodeInfoEnv& env, const Aggref *node)
{
	FIND_NODE(args);
	FIND_NODE(aggorder);
	FIND_NODE(aggdistinct);
}

static void
findWindowFunc(NodeInfoEnv& env, const WindowFunc *node)
{
	FIND_NODE(args);
}

static void
findArrayRef(NodeInfoEnv& env, const ArrayRef *node)
{
	FIND_NODE(refupperindexpr);
	FIND_NODE(reflowerindexpr);
	FIND_NODE(refexpr);
	FIND_NODE(refassgnexpr);
}

static void
findFuncExpr(NodeInfoEnv& env, const FuncExpr *node)
{
	FIND_NODE(args);
}

static void
findNamedArgExpr(NodeInfoEnv& env, const NamedArgExpr *node)
{
	FIND_NODE(arg);
}

static void
findOpExpr(NodeInfoEnv& env, const OpExpr *node)
{
	FIND_NODE(args);
}

static void 
findDistinctExpr(NodeInfoEnv& env, const DistinctExpr *node)
{
	FIND_NODE(args);
}

static void
findNullIfExpr(NodeInfoEnv& env, const NullIfExpr *node)
{
	FIND_NODE(args);
}

static void
findScalarArrayOpExpr(NodeInfoEnv& env, const ScalarArrayOpExpr *node)
{
	FIND_NODE(args);
}

static void
findBoolExpr(NodeInfoEnv& env, const BoolExpr *node)
{
	FIND_NODE(args);
}

static void
findSubLink(NodeInfoEnv& env, const SubLink *node)
{
	FIND_NODE(testexpr);
	FIND_NODE(operName);
	FIND_NODE(subselect);
}

static void
findSubPlan(NodeInfoEnv& env, const SubPlan *node)
{
	FIND_NODE(testexpr);
	FIND_NODE(paramIds);
	FIND_NODE(setParam);
	FIND_NODE(parParam);
	FIND_NODE(args);
}

static void
findAlternativeSubPlan(NodeInfoEnv& env, const AlternativeSubPlan *node)
{
	FIND_NODE(subplans); /* list of plans */
}

static void
findFieldSelect(NodeInfoEnv& env, const FieldSelect *node)
{
	FIND_NODE(arg);
}

static void
findFieldStore(NodeInfoEnv& env, const FieldStore *node)
{
	FIND_NODE(arg);
	FIND_NODE(newvals);
	FIND_NODE(fieldnums);
}

static void
findRelabelType(NodeInfoEnv& env, const RelabelType *node)
{
	FIND_NODE(arg);
}

static void
findCoerceViaIO(NodeInfoEnv& env, const CoerceViaIO *node)
{
	FIND_NODE(arg);
}

static void
findArrayCoerceExpr(NodeInfoEnv& env, const ArrayCoerceExpr *node)
{
	FIND_NODE(arg);
}

static void
findConvertRowtypeExpr(NodeInfoEnv& env, const ConvertRowtypeExpr *node)
{
	FIND_NODE(arg);
}

static void
findCollateExpr(NodeInfoEnv& env, const CollateExpr *node)
{
	FIND_NODE(arg);
}

static void
findCaseExpr(NodeInfoEnv& env, const CaseExpr *node)
{
	FIND_NODE(arg);
	FIND_NODE(args);
	FIND_NODE(defresult);
}

static void
findCaseWhen(NodeInfoEnv& env, const CaseWhen *node)
{
	FIND_NODE(expr);
	FIND_NODE(result);
}

static void
findCaseTestExpr(NodeInfoEnv& env, const CaseTestExpr *node)
{
	/* nothing */
}

static void
findArrayExpr(NodeInfoEnv& env, const ArrayExpr *node)
{
	FIND_NODE(elements);
}

static void
findRowExpr(NodeInfoEnv& env, const RowExpr *node)
{
	FIND_NODE(args);
	FIND_NODE(colnames);
}

static void
findRowCompareExpr(NodeInfoEnv& env, const RowCompareExpr *node)
{
	FIND_NODE(opnos);
	FIND_NODE(opfamilies);
	FIND_NODE(inputcollids);
	FIND_NODE(largs);
	FIND_NODE(rargs);
}

static void
findCoalesceExpr(NodeInfoEnv& env, const CoalesceExpr *node)
{
	FIND_NODE(args);
}

static void
findMinMaxExpr(NodeInfoEnv& env, const MinMaxExpr *node)
{
	FIND_NODE(args);
}

static void
findXmlExpr(NodeInfoEnv& env, const XmlExpr *node)
{
	FIND_NODE(named_args);
	FIND_NODE(arg_names);
	FIND_NODE(args);
}

static void
findNullTest(NodeInfoEnv& env, const NullTest *node)
{
	FIND_NODE(arg);
}

static void
findBooleanTest(NodeInfoEnv& env, const BooleanTest *node)
{
	FIND_NODE(arg);
}

static void
findCoerceToDomain(NodeInfoEnv& env, const CoerceToDomain *node)
{
	FIND_NODE(arg);
}

static void
findCoerceToDomainValue(NodeInfoEnv& env, const CoerceToDomainValue *node)
{
	/* nothing */
}

static void
findSetToDefault(NodeInfoEnv& env, const SetToDefault *node)
{
	/* nothing */
}

static void
findCurrentOfExpr(NodeInfoEnv& env, const CurrentOfExpr *node)
{
	/* nothing */
}

static void
findTargetEntry(NodeInfoEnv& env, const TargetEntry *node)
{
	FIND_NODE(expr);
}

static void
findRangeTblRef(NodeInfoEnv& env, const RangeTblRef *node)
{
	/* nothing */
}

static void
findJoinExpr(NodeInfoEnv& env, const JoinExpr *node)
{
	FIND_NODE(larg);
	FIND_NODE(rarg);
	FIND_NODE(larg);
	FIND_NODE(usingClause);
	FIND_NODE(quals);
	FIND_NODE(alias);
}

static void
findFromExpr(NodeInfoEnv& env, const FromExpr *node)
{
	FIND_NODE(fromlist);
	FIND_NODE(quals);
}

static void
findPlannerGlobal(NodeInfoEnv& env, const PlannerGlobal *node)
{
	FIND_NODE(subplans);
	FIND_NODE(subroots);
	FIND_NODE(finalrtable);
	FIND_NODE(finalrowmarks);
	FIND_NODE(resultRelations);
	FIND_NODE(relationOids);
	FIND_NODE(invalItems);
}

static void
findPlannerInfo(NodeInfoEnv& env, const PlannerInfo *node)
{
	int i;

	FIND_NODE(parse);
	FIND_NODE(glob);
	FIND_NODE(parent_root);
	FIND_NODE(plan_params);

	for (i=1 ; i<node->simple_rel_array_size; i++)
		FIND_NODE_INDEX(simple_rel_array, i);

	for (i=1 ; i<node->simple_rel_array_size; i++)
		FIND_NODE_INDEX(simple_rte_array, i);

	FIND_NODE(join_rel_list);

	for (i=0 ; i<node->join_cur_level; i++)
		FIND_NODE_INDEX(join_rel_level, i);

	FIND_NODE(init_plans);
	FIND_NODE(cte_plan_ids);
	FIND_NODE(eq_classes);
	FIND_NODE(canon_pathkeys);
	FIND_NODE(left_join_clauses);
	FIND_NODE(right_join_clauses);
	FIND_NODE(full_join_clauses);
	FIND_NODE(join_info_list);
#if PG_VERSION_NUM >= 90300
	FIND_NODE(lateral_info_list);
#endif
	FIND_NODE(append_rel_list);
	FIND_NODE(placeholder_list);
	FIND_NODE(query_pathkeys);
	FIND_NODE(group_pathkeys);
	FIND_NODE(window_pathkeys);
	FIND_NODE(sort_pathkeys);
	FIND_NODE(minmax_aggs);
	FIND_NODE(initial_rels);
	FIND_NODE(non_recursive_plan);
	FIND_NODE(curOuterParams);
}

static void
findRelOptInfo(NodeInfoEnv& env, const RelOptInfo *node)
{
	FIND_NODE(reltargetlist);
	FIND_NODE(pathlist);
	FIND_NODE(ppilist);
	FIND_NODE(cheapest_startup_path);
	FIND_NODE(cheapest_total_path);
	FIND_NODE(cheapest_unique_path);
	FIND_NODE(cheapest_parameterized_paths);
#if PG_VERSION_NUM >= 90300
	FIND_NODE(lateral_vars);
#endif
	FIND_NODE(indexlist);
	FIND_NODE(subplan);
	FIND_NODE(subroot);
#if PG_VERSION_NUM >= 90300
	FIND_NODE(subplan_params);
#endif
	FIND_NODE(fdwroutine);
	FIND_NODE(baserestrictinfo);
	FIND_NODE(joininfo);
}

static void
findQuery(NodeInfoEnv& env, const Query *node)
{
#if 0
	if (node->utilityStmt)
		switch (nodeTag(node->utilityStmt))
		{
			case T_CreateStmt:
			case T_IndexStmt:
			case T_NotifyStmt:
			case T_DeclareCursorStmt:
				FIND_NODE(utilityStmt);
				break;
			default:
				break;
		}
#endif

	FIND_NODE(cteList);
	FIND_NODE(rtable);
	FIND_NODE(jointree);
	FIND_NODE(targetList);
	FIND_NODE(returningList);
	FIND_NODE(groupClause);
	FIND_NODE(havingQual);
	FIND_NODE(windowClause);
	FIND_NODE(distinctClause);
	FIND_NODE(sortClause);
	FIND_NODE(limitOffset);
	FIND_NODE(limitCount);
	FIND_NODE(rowMarks);
	FIND_NODE(setOperations);
	FIND_NODE(constraintDeps);
} 

static void
findSortGroupClause(NodeInfoEnv& env, const SortGroupClause *node)
{
	/* nothing */
}

static void
findRangeTblEntry(NodeInfoEnv& env, const RangeTblEntry *node)
{
	FIND_NODE(subquery);
	FIND_NODE(joinaliasvars);

#if PG_VERSION_NUM >= 90400
	FIND_NODE(functions);
#else
	FIND_NODE(funcexpr);
	FIND_NODE(funccoltypes);
	FIND_NODE(funccoltypmods);
	FIND_NODE(funccolcollations);
#endif

	FIND_NODE(values_lists);
	FIND_NODE(values_collations);

	FIND_NODE(ctecoltypes);
	FIND_NODE(ctecoltypmods);
	FIND_NODE(ctecolcollations);

	FIND_NODE(alias);
	FIND_NODE(eref);
}


/****************************************************************************/
/*                                                                          */
/****************************************************************************/
void NodeInfoEnv::outputAllNodes()
{
	NodeSetMap node_group;
	NodeNodeMap head_node_map;

	NodeSet::const_iterator head_node_it;

	for (head_node_it = tlist_head_set.begin() ; head_node_it != tlist_head_set.end() ; head_node_it++)
		head_node_map[*head_node_it] = *head_node_it;

	for (head_node_it = exprtree_head_set.begin() ; head_node_it != exprtree_head_set.end() ; head_node_it++)
		head_node_map[*head_node_it] = *head_node_it;

	bool changed;
	do
	{
		changed = false;

		EdgeMap::const_reverse_iterator edge_it;
		for (edge_it = edge_map.rbegin() ; edge_it != edge_map.rend() ; edge_it++)
		{
			const void * from = (*edge_it).first.first;
			const void * to = (*edge_it).first.second;

			if (head_node_map.find(to) != head_node_map.end())
				continue;

			if (head_node_map.find(from) != head_node_map.end())
			{
				head_node_map[to] = head_node_map[from];
				changed = true;
			}
		}
	}
	while (changed);

	NodeIdMap::const_iterator node_it;
	for (node_it = node_id_map.begin() ; node_it != node_id_map.end() ; node_it++)
	{
		const void *head = NULL;
		const void *obj = (*node_it).first;

		if (head_node_map.find(obj) != head_node_map.end())
			head = head_node_map[obj];

		node_group[head].insert(obj);
	}

	/*
	 *
	 */
	append("digraph {\n");
	append("graph [rankdir = \"LR\", label = \"%s\"]\n", label.c_str());
	append("node  [shape=record,style=filled,fillcolor=gray95]\n");
	append("edge  [arrowtail=empty]\n");

	NodeSetMap::const_iterator group_it;
	for (group_it = node_group.begin() ; group_it != node_group.end() ; group_it++)
	{
		const void *head = (*group_it).first;
		const NodeSet& node_set = (*group_it).second;

		if (head != NULL)
		{
			append("subgraph cluster_%d {\n", num_subgraph++);

			if (tlist_head_set.find(head) != tlist_head_set.end())
				append("\tlabel = \"Target List\";\n");
			else
				append("\tlabel = \"Express Tree\";\n");
		}
		
		/*
		 * Nodes
		 */
		NodeSet::const_iterator member_it;
		for (member_it = node_set.begin() ; member_it != node_set.end() ; member_it++)
		{
			const void *obj = *member_it;
			unsigned int node_id = node_id_map[obj];

			if (head != NULL)
				append("\t");

			append("%d[label = \"", node_id);
			::outputNode(*this, obj);
			append("\"]\n");
		}

		append("\n");
		
		/*
		 * Edges
		 */
		if (head != NULL)
		{
			EdgeMap::const_iterator edge_it;
			for (edge_it = edge_map.begin() ; edge_it != edge_map.end() ; edge_it++)
			{
				const void *from, *to; 
				unsigned int from_node_id, to_node_id;

				from = (*edge_it).first.first;
				to   = (*edge_it).first.second;

				if (head != head_node_map[from] || head != head_node_map[to])
					continue;

				from_node_id = node_id_map[from];
				to_node_id   = node_id_map[to];

				append("\t%d:%s -> %d:head [headlabel = \"%d\", taillabel = \"%d\"]\n",
					   from_node_id, (*edge_it).second.c_str(), to_node_id,
					   from_node_id, to_node_id);
			}
		}
		else
		{
			EdgeMap::const_iterator edge_it;
			for (edge_it = edge_map.begin() ; edge_it != edge_map.end() ; edge_it++)
			{
				const void *from, *to; 
				const void *from_head, *to_head; 
				unsigned int from_node_id, to_node_id;

				from = (*edge_it).first.first;
				to   = (*edge_it).first.second;

				from_head = head_node_map[from];
				to_head   = head_node_map[to];

				if ((from_head == to_head) && (from_head != NULL))
					continue;

				from_node_id = node_id_map[from];
				to_node_id   = node_id_map[to];

				append("%d:%s -> %d:head [headlabel = \"%d\", taillabel = \"%d\"]\n",
					   from_node_id, (*edge_it).second.c_str(), to_node_id,
					   from_node_id, to_node_id);
			}
		}

		if (head != NULL)
			append("}\n");

		append("\n");
	}

	append("}\n");
}


static void
outputNode(NodeInfoEnv& env, const void *obj)
{
	if (IsA(obj, Integer)   ||
		IsA(obj, Float)     ||
		IsA(obj, String)    ||
		IsA(obj, BitString) ||
		IsA(obj, IntList)   ||
		IsA(obj, OidList))
	{
		outputValue(env, reinterpret_cast<const Value*>(obj));
		return;
	}

	if (IsA(obj, List))
	{
		int i = 0;
		const ListCell *lc;

		env.pushNode(obj, "List");

		foreach(lc, reinterpret_cast<const List *>(obj))
		{
			env.append("|<%d> [%d]", i + 1, i);
			i++;
		}

		env.popNode();
		return;
	}

	switch (nodeTag(obj))
	{
		case T_PlannedStmt:
			outputPlannedStmt(env, reinterpret_cast<const PlannedStmt*>(obj));
			break;
		
		case T_Plan:
			outputPlan(env, reinterpret_cast<const Plan*>(obj));
			break;

#if 0
		case T_Env:
			outputEnv(env, reinterpret_cast<const Env*>(obj));
			break;
#endif 
		case T_ModifyTable:
			outputModifyTable(env, reinterpret_cast<const ModifyTable*>(obj));
			break;
		case T_Append:
			outputAppend(env, reinterpret_cast<const Append*>(obj));
			break;
		case T_MergeAppend:
			outputMergeAppend(env, reinterpret_cast<const MergeAppend*>(obj));
			break;
		case T_RecursiveUnion:
			outputRecursiveUnion(env, reinterpret_cast<const RecursiveUnion*>(obj));
			break;
		case T_BitmapAnd:
			outputBitmapAnd(env, reinterpret_cast<const BitmapAnd*>(obj));
			break;
		case T_BitmapOr:
			outputBitmapOr(env, reinterpret_cast<const BitmapOr*>(obj));
			break;
		case T_Scan:
			outputScan(env, reinterpret_cast<const Scan*>(obj));
			break;
		case T_SeqScan:
			outputSeqScan(env, reinterpret_cast<const SeqScan*>(obj));
			break;
		case T_IndexScan:
			outputIndexScan(env, reinterpret_cast<const IndexScan*>(obj));
			break;
		case T_IndexOnlyScan:
			outputIndexOnlyScan(env, reinterpret_cast<const IndexOnlyScan*>(obj));
			break;
		case T_BitmapIndexScan:
			outputBitmapIndexScan(env, reinterpret_cast<const BitmapIndexScan*>(obj));
			break;
		case T_BitmapHeapScan:
			outputBitmapHeapScan(env, reinterpret_cast<const BitmapHeapScan*>(obj));
			break;
		case T_TidScan:
			outputTidScan(env, reinterpret_cast<const TidScan*>(obj));
			break;
		case T_SubqueryScan:
			outputSubqueryScan(env, reinterpret_cast<const SubqueryScan*>(obj));
			break;
		case T_FunctionScan:
			outputFunctionScan(env, reinterpret_cast<const FunctionScan*>(obj));
			break;
		case T_ValuesScan:
			outputValuesScan(env, reinterpret_cast<const ValuesScan*>(obj));
			break;
		case T_CteScan:
			outputCteScan(env, reinterpret_cast<const CteScan*>(obj));
			break;
		case T_WorkTableScan:
			outputWorkTableScan(env, reinterpret_cast<const WorkTableScan*>(obj));
			break;
		case T_ForeignScan:
			outputForeignScan(env, reinterpret_cast<const ForeignScan*>(obj));
			break;
		case T_Join:
			outputJoin(env, reinterpret_cast<const Join*>(obj));
			break;
		case T_NestLoop:
			outputNestLoop(env, reinterpret_cast<const NestLoop*>(obj));
			break;
		case T_MergeJoin:
			outputMergeJoin(env, reinterpret_cast<const MergeJoin*>(obj));
			break;
		case T_HashJoin:
			outputHashJoin(env, reinterpret_cast<const HashJoin*>(obj));
			break;
		case T_Agg:
			outputAgg(env, reinterpret_cast<const Agg*>(obj));
			break;
		case T_WindowAgg:
			outputWindowAgg(env, reinterpret_cast<const WindowAgg*>(obj));
			break;
		case T_Group:
			outputGroup(env, reinterpret_cast<const Group*>(obj));
			break;
		case T_Material:
			outputMaterial(env, reinterpret_cast<const Material*>(obj));
			break;
		case T_Sort:
			outputSort(env, reinterpret_cast<const Sort*>(obj));
			break;
		case T_Unique:
			outputUnique(env, reinterpret_cast<const Unique*>(obj));
			break;
		case T_Hash:
			outputHash(env, reinterpret_cast<const Hash*>(obj));
			break;
		case T_SetOp:
			outputSetOp(env, reinterpret_cast<const SetOp*>(obj));
			break;
		case T_LockRows:
			outputLockRows(env, reinterpret_cast<const LockRows*>(obj));
			break;
		case T_Limit:
			outputLimit(env, reinterpret_cast<const Limit*>(obj));
			break;
		case T_NestLoopParam:
			outputNestLoopParam(env, reinterpret_cast<const NestLoopParam*>(obj));
			break;
		case T_PlanRowMark:
			outputPlanRowMark(env, reinterpret_cast<const PlanRowMark*>(obj));
			break;
		case T_PlanInvalItem:
			outputPlanInvalItem(env, reinterpret_cast<const PlanInvalItem*>(obj));
			break;
		case T_Alias:
			outputAlias(env, reinterpret_cast<const Alias*>(obj));
			break;
		case T_RangeVar:
			outputRangeVar(env, reinterpret_cast<const RangeVar*>(obj));
			break;
		case T_IntoClause:
			outputIntoClause(env, reinterpret_cast<const IntoClause*>(obj));
			break;
		case T_Var:
			outputVar(env, reinterpret_cast<const Var*>(obj));
			break;
		case T_Const:
			outputConst(env, reinterpret_cast<const Const*>(obj));
			break;
		case T_Param:
			outputParam(env, reinterpret_cast<const Param*>(obj));
			break;
		case T_Aggref:
			outputAggref(env, reinterpret_cast<const Aggref*>(obj));
			break;
		case T_WindowFunc:
			outputWindowFunc(env, reinterpret_cast<const WindowFunc*>(obj));
			break;
		case T_ArrayRef:
			outputArrayRef(env, reinterpret_cast<const ArrayRef*>(obj));
			break;
		case T_FuncExpr:
			outputFuncExpr(env, reinterpret_cast<const FuncExpr*>(obj));
			break;
		case T_NamedArgExpr:
			outputNamedArgExpr(env, reinterpret_cast<const NamedArgExpr*>(obj));
			break;
		case T_OpExpr:
			outputOpExpr(env, reinterpret_cast<const OpExpr*>(obj));
			break;
		case T_DistinctExpr:
			outputDistinctExpr(env, reinterpret_cast<const DistinctExpr*>(obj));
			break;
		case T_NullIfExpr:
			outputNullIfExpr(env, reinterpret_cast<const NullIfExpr*>(obj));
			break;
		case T_ScalarArrayOpExpr:
			outputScalarArrayOpExpr(env, reinterpret_cast<const ScalarArrayOpExpr*>(obj));
			break;
		case T_BoolExpr:
			outputBoolExpr(env, reinterpret_cast<const BoolExpr*>(obj));
			break;
		case T_SubLink:
			outputSubLink(env, reinterpret_cast<const SubLink*>(obj));
			break;
		case T_SubPlan:
			outputSubPlan(env, reinterpret_cast<const SubPlan*>(obj));
			break;
		case T_AlternativeSubPlan:
			outputAlternativeSubPlan(env, reinterpret_cast<const AlternativeSubPlan*>(obj));
			break;
		case T_FieldSelect:
			outputFieldSelect(env, reinterpret_cast<const FieldSelect*>(obj));
			break;
		case T_FieldStore:
			outputFieldStore(env, reinterpret_cast<const FieldStore*>(obj));
			break;
		case T_RelabelType:
			outputRelabelType(env, reinterpret_cast<const RelabelType*>(obj));
			break;
		case T_CoerceViaIO:
			outputCoerceViaIO(env, reinterpret_cast<const CoerceViaIO*>(obj));
			break;
		case T_ArrayCoerceExpr:
			outputArrayCoerceExpr(env, reinterpret_cast<const ArrayCoerceExpr*>(obj));
			break;
		case T_ConvertRowtypeExpr:
			outputConvertRowtypeExpr(env, reinterpret_cast<const ConvertRowtypeExpr*>(obj));
			break;
		case T_CollateExpr:
			outputCollateExpr(env, reinterpret_cast<const CollateExpr*>(obj));
			break;
		case T_CaseExpr:
			outputCaseExpr(env, reinterpret_cast<const CaseExpr*>(obj));
			break;
		case T_CaseWhen:
			outputCaseWhen(env, reinterpret_cast<const CaseWhen*>(obj));
			break;
		case T_CaseTestExpr:
			outputCaseTestExpr(env, reinterpret_cast<const CaseTestExpr*>(obj));
			break;
		case T_ArrayExpr:
			outputArrayExpr(env, reinterpret_cast<const ArrayExpr*>(obj));
			break;
		case T_RowExpr:
			outputRowExpr(env, reinterpret_cast<const RowExpr*>(obj));
			break;
		case T_RowCompareExpr:
			outputRowCompareExpr(env, reinterpret_cast<const RowCompareExpr*>(obj));
			break;
		case T_CoalesceExpr:
			outputCoalesceExpr(env, reinterpret_cast<const CoalesceExpr*>(obj));
			break;
		case T_MinMaxExpr:
			outputMinMaxExpr(env, reinterpret_cast<const MinMaxExpr*>(obj));
			break;
		case T_XmlExpr:
			outputXmlExpr(env, reinterpret_cast<const XmlExpr*>(obj));
			break;
		case T_NullTest:
			outputNullTest(env, reinterpret_cast<const NullTest*>(obj));
			break;
		case T_BooleanTest:
			outputBooleanTest(env, reinterpret_cast<const BooleanTest*>(obj));
			break;
		case T_CoerceToDomain:
			outputCoerceToDomain(env, reinterpret_cast<const CoerceToDomain*>(obj));
			break;
		case T_CoerceToDomainValue:
			outputCoerceToDomainValue(env, reinterpret_cast<const CoerceToDomainValue*>(obj));
			break;
		case T_SetToDefault:
			outputSetToDefault(env, reinterpret_cast<const SetToDefault*>(obj));
			break;
		case T_CurrentOfExpr:
			outputCurrentOfExpr(env, reinterpret_cast<const CurrentOfExpr*>(obj));
			break;
		case T_TargetEntry:
			outputTargetEntry(env, reinterpret_cast<const TargetEntry*>(obj));
			break;
		case T_RangeTblRef:
			outputRangeTblRef(env, reinterpret_cast<const RangeTblRef*>(obj));
			break;
		case T_JoinExpr:
			outputJoinExpr(env, reinterpret_cast<const JoinExpr*>(obj));
			break;
		case T_FromExpr:
			outputFromExpr(env, reinterpret_cast<const FromExpr*>(obj));
			break;
#if 0
		case T_Path:
			outputPath(env, obj);
			break;
		case T_IndexPath:
			outputIndexPath(env, obj);
			break;
		case T_BitmapHeapPath:
			outputBitmapHeapPath(env, obj);
			break;
		case T_BitmapAndPath:
			outputBitmapAndPath(env, obj);
			break;
		case T_BitmapOrPath:
			outputBitmapOrPath(env, obj);
			break;
		case T_TidPath:
			outputTidPath(env, obj);
			break;
		case T_ForeignPath:
			outputForeignPath(env, obj);
			break;
		case T_AppendPath:
			outputAppendPath(env, obj);
			break;
		case T_MergeAppendPath:
			outputMergeAppendPath(env, obj);
			break;
		case T_EnvPath:
			outputEnvPath(env, obj);
			break;
		case T_MaterialPath:
			outputMaterialPath(env, obj);
			break;
		case T_UniquePath:
			outputUniquePath(env, obj);
			break;
		case T_NestPath:
			outputNestPath(env, obj);
			break;
		case T_MergePath:
			outputMergePath(env, obj);
			break;
		case T_HashPath:
			outputHashPath(env, obj);
			break;
#endif
		case T_PlannerGlobal:
			outputPlannerGlobal(env, reinterpret_cast<const PlannerGlobal*>(obj));
			break;
		case T_PlannerInfo:
			outputPlannerInfo(env, reinterpret_cast<const PlannerInfo*>(obj));
			break;
		case T_RelOptInfo:
			outputRelOptInfo(env, reinterpret_cast<const RelOptInfo*>(obj));
			break;
#if 0
		case T_IndexOptInfo:
			outputIndexOptInfo(env, obj);
			break;
		case T_EquivalenceClass:
			outputEquivalenceClass(env, obj);
			break;
		case T_EquivalenceMember:
			outputEquivalenceMember(env, obj);
			break;
		case T_PathKey:
			outputPathKey(env, obj);
			break;
		case T_ParamPathInfo:
			outputParamPathInfo(env, obj);
			break;
		case T_RestrictInfo:
			outputRestrictInfo(env, obj);
			break;
		case T_PlaceHolderVar:
			outputPlaceHolderVar(env, obj);
			break;
		case T_SpecialJoinInfo:
			outputSpecialJoinInfo(env, obj);
			break;
		case T_AppendRelInfo:
			outputAppendRelInfo(env, obj);
			break;
		case T_PlaceHolderInfo:
			outputPlaceHolderInfo(env, obj);
			break;
		case T_MinMaxAggInfo:
			outputMinMaxAggInfo(env, obj);
			break;
		case T_PlannerParamItem:
			outputPlannerParamItem(env, obj);
			break;

		case T_CreateStmt:
			outputCreateStmt(env, obj);
			break;
		case T_CreateForeignTableStmt:
			outputCreateForeignTableStmt(env, obj);
			break;
		case T_IndexStmt:
			outputIndexStmt(env, obj);
			break;
		case T_NotifyStmt:
			outputNotifyStmt(env, obj);
			break;
		case T_DeclareCursorStmt:
			outputDeclareCursorStmt(env, obj);
			break;
		case T_SelectStmt:
			outputSelectStmt(env, obj);
			break;
		case T_ColumnDef:
			outputColumnDef(env, obj);
			break;
		case T_TypeName:
			outputTypeName(env, obj);
			break;
		case T_TypeCast:
			outputTypeCast(env, obj);
			break;
		case T_CollateClause:
			outputCollateClause(env, obj);
			break;
		case T_IndexElem:
			outputIndexElem(env, obj);
			break;
#endif
		case T_Query:
			outputQuery(env, reinterpret_cast<const Query*>(obj));
			break;
		case T_SortGroupClause:
			outputSortGroupClause(env, reinterpret_cast<const SortGroupClause*>(obj));
			break;
#if 0
		case T_WindowClause:
			outputWindowClause(env, obj);
			break;
		case T_RowMarkClause:
			outputRowMarkClause(env, obj);
			break;
		case T_WithClause:
			outputWithClause(env, obj);
			break;
		case T_CommonTableExpr:
			outputCommonTableExpr(env, obj);
			break;
		case T_SetOperationStmt:
			outputSetOperationStmt(env, obj);
			break;
#endif
		case T_RangeTblEntry:
			outputRangeTblEntry(env, reinterpret_cast<const RangeTblEntry*>(obj));
			break;
#if 0
		case T_A_Expr:
			outputAExpr(env, obj);
			break;
		case T_ColumnRef:
			outputColumnRef(env, obj);
			break;
		case T_ParamRef:
			outputParamRef(env, obj);
			break;
		case T_A_Const:
			outputAConst(env, obj);
			break;
		case T_A_Star:
			outputA_Star(env, obj);
			break;
		case T_A_Indices:
			outputA_Indices(env, obj);
			break;
		case T_A_Indirection:
			outputA_Indirection(env, obj);
			break;
		case T_A_ArrayExpr:
			outputA_ArrayExpr(env, obj);
			break;
		case T_ResTarget:
			outputResTarget(env, obj);
			break;
		case T_SortBy:
			outputSortBy(env, obj);
			break;
		case T_WindowDef:
			outputWindowDef(env, obj);
			break;
		case T_RangeSubselect:
			outputRangeSubselect(env, obj);
			break;
		case T_RangeFunction:
			outputRangeFunction(env, obj);
			break;
		case T_Constraint:
			outputConstraint(env, obj);
			break;
		case T_FuncCall:
			outputFuncCall(env, obj);
			break;
		case T_DefElem:
			outputDefElem(env, obj);
			break;
		case T_TableLikeClause:
			outputTableLikeClause(env, obj);
			break;
		case T_LockingClause:
			outputLockingClause(env, obj);
			break;
		case T_XmlSerialize:
			outputXmlSerialize(env, obj);
			break;
#endif

		default:
			env.pushNode(obj, "Unknown");
			env.popNode();
			break;
	}
}

static void
_outputPlan(NodeInfoEnv& env, const Plan *node)
{
	WRITE_COST_FIELD(startup_cost);
	WRITE_COST_FIELD(total_cost);
	WRITE_FLOAT_FIELD(plan_rows, "%.0f");
	WRITE_INT_FIELD(plan_width);

	WRITE_NODE_FIELD(targetlist);
	WRITE_NODE_FIELD(qual);
	WRITE_NODE_FIELD(lefttree);
	WRITE_NODE_FIELD(righttree);
	WRITE_NODE_FIELD(initPlan);
	WRITE_BITMAPSET_FIELD(extParam);
	WRITE_BITMAPSET_FIELD(allParam);
}

static void
_outputScan(NodeInfoEnv& env, const Scan *node)
{
	_outputPlan(env, &node->plan);
	WRITE_INDEX_FIELD(scanrelid);
}

static void
_outputJoin(NodeInfoEnv& env, const Join *node)
{
	_outputPlan(env, &node->plan);
	WRITE_ENUM_FIELD(jointype, JoinType);
	WRITE_NODE_FIELD(joinqual);
}

static void
_outputOpExpr(NodeInfoEnv& env, const OpExpr *node)
{
	WRITE_OID_FIELD(opno);
	WRITE_OID_FIELD(opfuncid);
	WRITE_OID_FIELD(opresulttype);
	WRITE_BOOL_FIELD(opretset);
	WRITE_OID_FIELD(opcollid);
	WRITE_OID_FIELD(inputcollid);
	WRITE_NODE_FIELD(args);
	WRITE_LOCATION_FIELD(location);
}

static void
outputValue(NodeInfoEnv& env, const Value *node)
{
	/* @todo */
	switch (node->type)
	{
		case T_Integer:
			env.pushNode(node, "Integer");
			env.append("|%ld", node->val.ival);
			env.popNode();
			break;

		case T_Float:
			env.pushNode(node, "Float");
			env.append("|%s", node->val.str);
			env.popNode();
			break;

		case T_String:
			env.pushNode(node, "String");
			if (node->val.str)
				env.append("|%s", node->val.str);
			else
				env.append("|NULL");
			env.popNode();
			break;

		case T_BitString:
			env.pushNode(node, "BitString");
			env.append("|%s", node->val.str);
			env.popNode();
			break;

		case T_Null:
			env.pushNode(node, "Null");
			env.append("|NULL");
			env.popNode();
			break;

		case T_IntList: {
			const ListCell *lc;
			env.pushNode(node, "IntList");
			env.append("|");
			foreach(lc, reinterpret_cast<const List*>(node))
			{
				env.append("%d ", lfirst_int(lc));
			}
			env.popNode();
			break;
		}

		case T_OidList: {
			const ListCell *lc;
			env.pushNode(node, "OidList");
			env.append("|");
			foreach(lc, reinterpret_cast<const List*>(node))
			{
				env.append("%d ", lfirst_oid(lc));
			}
			env.popNode();
			break;
		}

		default:
			elog(ERROR, "unrecognized node type: %d", (int)node->type);
			break;
	}
}

static void
outputPlannedStmt(NodeInfoEnv& env, const PlannedStmt *node)
{
	env.pushNode(node, "PlannedStmt");

	WRITE_ENUM_FIELD(commandType, CmdType);
	WRITE_UINT_FIELD(queryId);
	WRITE_BOOL_FIELD(hasReturning);
	WRITE_BOOL_FIELD(hasModifyingCTE);
	WRITE_BOOL_FIELD(canSetTag);
	WRITE_BOOL_FIELD(transientPlan);
	WRITE_NODE_FIELD(planTree);
	WRITE_NODE_FIELD(rtable);
	WRITE_NODE_FIELD(resultRelations);
	WRITE_NODE_FIELD(utilityStmt);
	WRITE_NODE_FIELD(subplans);
	WRITE_BITMAPSET_FIELD(rewindPlanIDs);
	WRITE_NODE_FIELD(rowMarks);
	WRITE_NODE_FIELD(relationOids);
	WRITE_NODE_FIELD(invalItems);
	WRITE_INT_FIELD(nParamExec);

	env.popNode();
}

static void
outputPlan(NodeInfoEnv& env, const Plan *node)
{
	env.pushNode(node, "Plan");

	_outputPlan(env, node);

	env.popNode();
}

static void
outputModifyTable(NodeInfoEnv& env, const ModifyTable *node)
{
	env.pushNode(node, "ModifyTable");

	_outputPlan(env, &node->plan);
	WRITE_ENUM_FIELD(operation, CmdType);
	WRITE_NODE_FIELD(resultRelations);
	WRITE_INT_FIELD(resultRelIndex);
	WRITE_NODE_FIELD(plans);
	WRITE_NODE_FIELD(withCheckOptionLists);
	WRITE_NODE_FIELD(returningLists);
	WRITE_NODE_FIELD(fdwPrivLists);
	WRITE_NODE_FIELD(rowMarks);
	WRITE_INT_FIELD(epqParam);

	env.popNode();
}

static void
outputAppend(NodeInfoEnv& env, const Append *node)
{
	env.pushNode(node, "Append");

	_outputPlan(env, &node->plan);
	WRITE_NODE_FIELD(appendplans);

	env.popNode();
}

static void
outputMergeAppend(NodeInfoEnv& env, const MergeAppend *node)
{
	env.pushNode(node, "MergeAppend");

	_outputPlan(env, &node->plan);
	WRITE_NODE_FIELD(mergeplans);
	WRITE_INT_FIELD(numCols);
	env.outputAttrNumberArray("sortColIdx", node->numCols, node->sortColIdx);
	env.outputOidArray("sortOperators", node->numCols, node->sortOperators);
	env.outputOidArray("collations", node->numCols, node->collations);
	env.outputBoolArray("nullsFirst", node->numCols, node->nullsFirst);

	env.popNode();
}

static void
outputRecursiveUnion(NodeInfoEnv& env, const RecursiveUnion *node)
{
	env.pushNode(node, "RecursiveUnion");

	_outputPlan(env, &node->plan);
	WRITE_INT_FIELD(numCols);
	env.outputAttrNumberArray("dupColIdx", node->numCols, node->dupColIdx);
	env.outputOidArray("dupOperators", node->numCols, node->dupOperators);
	WRITE_LONG_FIELD(numGroups);

	env.popNode();
}

static void
outputBitmapAnd(NodeInfoEnv& env, const BitmapAnd *node)
{
	env.pushNode(node, "BitmapAnd");

	_outputPlan(env, &node->plan);
	WRITE_NODE_FIELD(bitmapplans);

	env.popNode();
}

static void
outputBitmapOr(NodeInfoEnv& env, const BitmapOr *node)
{
	env.pushNode(node, "BitmapOr");

	_outputPlan(env, &node->plan);
	WRITE_NODE_FIELD(bitmapplans);

	env.popNode();
}

static void
outputScan(NodeInfoEnv& env, const Scan *node)
{
	env.pushNode(node, "Scan");

	_outputScan(env, node);

	env.popNode();
}

static void
outputSeqScan(NodeInfoEnv& env, const SeqScan *node)
{
	env.pushNode(node, "SeqScan");

	_outputScan(env, reinterpret_cast<const Scan*>(node));

	env.popNode();
}

static void
outputIndexScan(NodeInfoEnv& env, const IndexScan *node)
{
	env.pushNode(node, "IndexScan");

	_outputScan(env, &node->scan);

	WRITE_OID_FIELD(indexid);
	WRITE_NODE_FIELD(indexqual);
	WRITE_NODE_FIELD(indexqualorig);
	WRITE_NODE_FIELD(indexorderby);
	WRITE_NODE_FIELD(indexorderbyorig);
	WRITE_ENUM_FIELD(indexorderdir, ScanDirection);

	env.popNode();
}

static void
outputIndexOnlyScan(NodeInfoEnv& env, const IndexOnlyScan *node)
{
	env.pushNode(node, "IndexOnlyScan");

	_outputScan(env, &node->scan);

	WRITE_OID_FIELD(indexid);
	WRITE_NODE_FIELD(indexqual);
	WRITE_NODE_FIELD(indexorderby);
	WRITE_NODE_FIELD(indextlist);
	WRITE_ENUM_FIELD(indexorderdir, ScanDirection);

	env.popNode();
}

static void
outputBitmapIndexScan(NodeInfoEnv& env, const BitmapIndexScan *node)
{
	env.pushNode(node, "BitmapIndexScan");

	_outputScan(env, &node->scan);

	WRITE_OID_FIELD(indexid);
	WRITE_NODE_FIELD(indexqual);
	WRITE_NODE_FIELD(indexqualorig);

	env.popNode();
}

static void
outputBitmapHeapScan(NodeInfoEnv& env, const BitmapHeapScan *node)
{
	env.pushNode(node, "BitmapHeapScan");

	_outputScan(env, &node->scan);

	WRITE_NODE_FIELD(bitmapqualorig);

	env.popNode();
}

static void
outputTidScan(NodeInfoEnv& env, const TidScan *node)
{
	env.pushNode(node, "TidScan");

	_outputScan(env, &node->scan);

	WRITE_NODE_FIELD(tidquals);

	env.popNode();
}

static void
outputSubqueryScan(NodeInfoEnv& env, const SubqueryScan *node)
{
	env.pushNode(node, "SubqueryScan");

	_outputScan(env, &node->scan);

	WRITE_NODE_FIELD(subplan);

	env.popNode();
}

static void
outputFunctionScan(NodeInfoEnv& env, const FunctionScan *node)
{
	env.pushNode(node, "FunctionScan");

	_outputScan(env, &node->scan);

#if PG_VERSION_NUM >= 90400
	WRITE_NODE_FIELD(functions);
	WRITE_BOOL_FIELD(funcordinality);
#else
	WRITE_NODE_FIELD(funcexpr);
	WRITE_NODE_FIELD(funccolnames);
	WRITE_NODE_FIELD(funccoltypes);
	WRITE_NODE_FIELD(funccoltypmods);
	WRITE_NODE_FIELD(funccolcollations);
#endif

	env.popNode();
}

static void
outputValuesScan(NodeInfoEnv& env, const ValuesScan *node)
{
	env.pushNode(node, "ValuesScan");

	_outputScan(env, &node->scan);

	WRITE_NODE_FIELD(values_lists);

	env.popNode();
}

static void
outputCteScan(NodeInfoEnv& env, const CteScan *node)
{
	env.pushNode(node, "CteScan");

	_outputScan(env, &node->scan);

	WRITE_INT_FIELD(ctePlanId);
	WRITE_INT_FIELD(cteParam);

	env.popNode();
}

static void
outputWorkTableScan(NodeInfoEnv& env, const WorkTableScan *node)
{
	env.pushNode(node, "WorkTableScan");

	_outputScan(env, &node->scan);

	WRITE_INT_FIELD(wtParam);

	env.popNode();
}

static void
outputForeignScan(NodeInfoEnv& env, const ForeignScan *node)
{
	env.pushNode(node, "ForeignScan");

	_outputScan(env, &node->scan);

	WRITE_NODE_FIELD(fdw_exprs);
	WRITE_NODE_FIELD(fdw_private);
	WRITE_BOOL_FIELD(fsSystemCol);

	env.popNode();
}

static void
outputJoin(NodeInfoEnv& env, const Join *node)
{
	env.pushNode(node, "Join");

	_outputJoin(env, node);

	env.popNode();
}

static void
outputNestLoop(NodeInfoEnv& env, const NestLoop *node)
{
	env.pushNode(node, "NestLoop");

	_outputJoin(env, &node->join);

	WRITE_NODE_FIELD(nestParams);

	env.popNode();
}

static void
outputMergeJoin(NodeInfoEnv& env, const MergeJoin *node)
{
	int	numCols;

	env.pushNode(node, "MergeJoin");

	_outputJoin(env, &node->join);

	WRITE_NODE_FIELD(mergeclauses);

	numCols = list_length(node->mergeclauses);
	
	env.outputOidArray("mergeFamilies", numCols, node->mergeFamilies);
	env.outputOidArray("mergeCollations", numCols, node->mergeCollations);
	env.outputIntArray("mergeStrategies", numCols, node->mergeStrategies);
	env.outputBoolArray("mergeNullsFirst", numCols, node->mergeNullsFirst);

	env.popNode();
}

static void
outputHashJoin(NodeInfoEnv& env, const HashJoin *node)
{
	env.pushNode(node, "HashJoin");

	_outputJoin(env, &node->join);

	WRITE_NODE_FIELD(hashclauses);

	env.popNode();
}

static void
outputAgg(NodeInfoEnv& env, const Agg *node)
{
	env.pushNode(node, "Agg");

	_outputPlan(env, &node->plan);

	WRITE_ENUM_FIELD(aggstrategy, AggStrategy);
	WRITE_INT_FIELD(numCols);

	env.outputAttrNumberArray("grpColIdx", node->numCols, node->grpColIdx);
	env.outputOidArray("grpOperators", node->numCols, node->grpOperators);

	WRITE_LONG_FIELD(numGroups);

	env.popNode();
}

static void
outputWindowAgg(NodeInfoEnv& env, const WindowAgg *node)
{
	env.pushNode(node, "WindowAgg");

	_outputPlan(env, &node->plan);

	WRITE_INDEX_FIELD(winref); /* ID referenced by window functions */
	WRITE_INT_FIELD(partNumCols);

	env.outputAttrNumberArray("partColIdx", node->partNumCols, node->partColIdx);
	env.outputOidArray("partOperators", node->partNumCols, node->partOperators);

	WRITE_INT_FIELD(ordNumCols);

	env.outputAttrNumberArray("ordColIdx", node->ordNumCols, node->ordColIdx);
	env.outputOidArray("ordOperators", node->ordNumCols, node->ordOperators);

	WRITE_INT_FIELD(frameOptions);
	WRITE_NODE_FIELD(startOffset);
	WRITE_NODE_FIELD(endOffset);

	env.popNode();
}

static void
outputGroup(NodeInfoEnv& env, const Group *node)
{
	env.pushNode(node, "Group");

	_outputPlan(env, &node->plan);

	WRITE_INT_FIELD(numCols);

	env.outputAttrNumberArray("grpColIdx", node->numCols, node->grpColIdx);
	env.outputOidArray("grpOperators", node->numCols, node->grpOperators);

	env.popNode();
}

static void
outputMaterial(NodeInfoEnv& env, const Material *node)
{
	env.pushNode(node, "Material");

	_outputPlan(env, &node->plan);

	env.popNode();
}

static void
outputSort(NodeInfoEnv& env, const Sort *node)
{
	env.pushNode(node, "Sort");

	_outputPlan(env, &node->plan);

	WRITE_INT_FIELD(numCols);

	env.outputAttrNumberArray("sortColIdx", node->numCols, node->sortColIdx);
	env.outputOidArray("sortOperators", node->numCols, node->sortOperators);
	env.outputOidArray("collations", node->numCols, node->collations);
	env.outputBoolArray("nullsFirst", node->numCols, node->nullsFirst);

	env.popNode();
}

static void
outputUnique(NodeInfoEnv& env, const Unique *node)
{
	env.pushNode(node, "Unique");

	_outputPlan(env, &node->plan);

	WRITE_INT_FIELD(numCols);

	env.outputAttrNumberArray("uniqColIdx", node->numCols, node->uniqColIdx);
	env.outputOidArray("uniqOperators", node->numCols, node->uniqOperators);

	env.popNode();
}

static void
outputHash(NodeInfoEnv& env, const Hash *node)
{
	env.pushNode(node, "Hash");

	_outputPlan(env, &node->plan);

	WRITE_OID_FIELD(skewTable);
	WRITE_ATTRNUMBER_FIELD(skewColumn);
	WRITE_BOOL_FIELD(skewInherit);
	WRITE_OID_FIELD(skewColType);
	WRITE_INT_FIELD(skewColTypmod);

	env.popNode();
}

static void
outputSetOp(NodeInfoEnv& env, const SetOp *node)
{
	env.pushNode(node, "SetOp");

	_outputPlan(env, &node->plan);

	WRITE_ENUM_FIELD(cmd, SetOpCmd);
	WRITE_ENUM_FIELD(strategy, SetOpStrategy);
	WRITE_INT_FIELD(numCols);

	env.outputAttrNumberArray("dupColIdx", node->numCols, node->dupColIdx);
	env.outputOidArray("dupOperators", node->numCols, node->dupOperators);

	WRITE_ATTRNUMBER_FIELD(flagColIdx);
	WRITE_INT_FIELD(firstFlag);
	WRITE_LONG_FIELD(numGroups);

	env.popNode();
}

static void
outputLockRows(NodeInfoEnv& env, const LockRows *node)
{
	env.pushNode(node, "LockRows");

	_outputPlan(env, &node->plan);

	WRITE_NODE_FIELD(rowMarks);
	WRITE_INT_FIELD(epqParam);

	env.popNode();
}

static void
outputLimit(NodeInfoEnv& env, const Limit *node)
{
	env.pushNode(node, "Limit");

	_outputPlan(env, &node->plan);

	WRITE_NODE_FIELD(limitOffset);
	WRITE_NODE_FIELD(limitCount);

	env.popNode();
}

static void
outputNestLoopParam(NodeInfoEnv& env, const NestLoopParam *node)
{
	env.pushNode(node, "NestLoopParam");

	WRITE_INT_FIELD(paramno);
	WRITE_NODE_FIELD(paramval);

	env.popNode();
}

static void
outputPlanRowMark(NodeInfoEnv& env, const PlanRowMark *node)
{
	env.pushNode(node, "PlanRowMark");

	WRITE_INDEX_FIELD(rti);
	WRITE_INDEX_FIELD(prti);
	WRITE_INDEX_FIELD(rowmarkId);
	WRITE_ENUM_FIELD(markType, RowMarkType);
	WRITE_BOOL_FIELD(noWait);
	WRITE_BOOL_FIELD(isParent);

	env.popNode();
}

static void
outputPlanInvalItem(NodeInfoEnv& env, const PlanInvalItem *node)
{
	env.pushNode(node, "PlanInvalItem");

	WRITE_INT_FIELD(cacheId);
	WRITE_UINT_FIELD(hashValue);

	env.popNode();
}

static void
outputAlias(NodeInfoEnv& env, const Alias *node)
{
	env.pushNode(node, "Alias");

	WRITE_STRING_FIELD(aliasname);
	WRITE_NODE_FIELD(colnames);

	env.popNode();
}

static void
outputIntoClause(NodeInfoEnv& env, const IntoClause *node)
{
	env.pushNode(node, "IntoClause");

	WRITE_NODE_FIELD(rel);
	WRITE_NODE_FIELD(colNames);
	WRITE_NODE_FIELD(options);
	WRITE_ENUM_FIELD(onCommit, OnCommitAction);
	WRITE_STRING_FIELD(tableSpaceName);
	WRITE_NODE_FIELD(viewQuery);
	WRITE_BOOL_FIELD(skipData);

	env.popNode();
}

static void
outputRangeVar(NodeInfoEnv& env, const RangeVar *node)
{
	env.pushNode(node, "RangeVar");

	WRITE_STRING_FIELD(catalogname);
	WRITE_STRING_FIELD(schemaname);
	WRITE_STRING_FIELD(relname);
	WRITE_ENUM_FIELD(inhOpt, InhOption);
	WRITE_CHAR_FIELD(relpersistence);
	WRITE_NODE_FIELD(alias);
	WRITE_LOCATION_FIELD(location);

	env.popNode();
}

/* static void outputExpr(NodeInfoEnv& env, const Expr *node); */
static void
outputVar(NodeInfoEnv& env, const Var *node)
{
	env.pushNode(node, "Var");

	WRITE_INDEX_FIELD(varno); /* index of this var's relation in the range table, or INNER_VAR/OUTER_VAR/INDEX_VAR */
	WRITE_ATTRNUMBER_FIELD(varattno);
	WRITE_OID_FIELD(vartype);
	WRITE_INT_FIELD(vartypmod);
	WRITE_OID_FIELD(varcollid);
	WRITE_INDEX_FIELD(varlevelsup); /* for subquery variables referencing outer
									   relations; 0 in a normal var, >0 means N
									   levels up */
	WRITE_INDEX_FIELD(varnoold); /* original value of varno, for debugging */
	WRITE_ATTRNUMBER_FIELD(varoattno);
	WRITE_LOCATION_FIELD(location); 

	env.popNode();
}

static void
outputConst(NodeInfoEnv& env, const Const *node)
{
	env.pushNode(node, "Const");

	WRITE_OID_FIELD(consttype);
	WRITE_INT_FIELD(consttypmod);
	WRITE_OID_FIELD(constcollid);
	WRITE_INT_FIELD(constlen);
	WRITE_BOOL_FIELD(constbyval);
	WRITE_BOOL_FIELD(constisnull);
	WRITE_LOCATION_FIELD(location);

#if 0
	/* @todo */
	appendStringInfo(str, " :constvalue ");
	if (node->constisnull)
		appendStringInfo(str, "<>");
	else
		_outDatum(str, node->constvalue, node->constlen, node->constbyval);
#endif

	env.popNode();
}

static void
outputParam(NodeInfoEnv& env, const Param *node)
{
	env.pushNode(node, "Param");

	WRITE_ENUM_FIELD(paramkind, ParamKind);
	WRITE_INT_FIELD(paramid);
	WRITE_OID_FIELD(paramtype);
	WRITE_INT_FIELD(paramtypmod);
	WRITE_OID_FIELD(paramcollid);
	WRITE_LOCATION_FIELD(location);

	env.popNode();
}

static void
outputAggref(NodeInfoEnv& env, const Aggref *node)
{
	env.pushNode(node, "Aggref");

	WRITE_OID_FIELD(aggfnoid);
	WRITE_OID_FIELD(aggtype);
	WRITE_OID_FIELD(aggcollid);
	WRITE_OID_FIELD(inputcollid);
#if PG_VERSION_NUM >= 90400
	WRITE_NODE_FIELD(aggdirectargs);
#endif
	WRITE_NODE_FIELD(args);
	WRITE_NODE_FIELD(aggorder);
	WRITE_NODE_FIELD(aggdistinct);
#if PG_VERSION_NUM >= 90400
	WRITE_NODE_FIELD(aggfilter);
#endif
	WRITE_BOOL_FIELD(aggstar);
#if PG_VERSION_NUM >= 90400
	WRITE_BOOL_FIELD(aggvariadic);
	WRITE_CHAR_FIELD(aggkind);
#endif
	WRITE_INDEX_FIELD(agglevelsup); /* > 0 if agg belongs to outer query */
	WRITE_LOCATION_FIELD(location);

	env.popNode();
}

static void
outputWindowFunc(NodeInfoEnv& env, const WindowFunc *node)
{
	env.pushNode(node, "WindowFunc");

	WRITE_OID_FIELD(winfnoid);
	WRITE_OID_FIELD(wintype);
	WRITE_OID_FIELD(wincollid);
	WRITE_OID_FIELD(inputcollid);
	WRITE_NODE_FIELD(args);
#if PG_VERSION_NUM >= 90400
	WRITE_NODE_FIELD(aggfilter);
#endif
	WRITE_INDEX_FIELD(winref); /* index of associated WindowClause */
	WRITE_BOOL_FIELD(winstar);
	WRITE_BOOL_FIELD(winagg);
	WRITE_LOCATION_FIELD(location);

	env.popNode();
}

static void
outputArrayRef(NodeInfoEnv& env, const ArrayRef *node)
{
	env.pushNode(node, "ArrayRef");

	WRITE_OID_FIELD(refarraytype);
	WRITE_OID_FIELD(refelemtype);
	WRITE_INT_FIELD(reftypmod);
	WRITE_OID_FIELD(refcollid);
	WRITE_NODE_FIELD(refupperindexpr);
	WRITE_NODE_FIELD(reflowerindexpr);
	WRITE_NODE_FIELD(refexpr);
	WRITE_NODE_FIELD(refassgnexpr);

	env.popNode();
}

static void
outputFuncExpr(NodeInfoEnv& env, const FuncExpr *node)
{
	env.pushNode(node, "FuncExpr");

	WRITE_OID_FIELD(funcid);
	WRITE_OID_FIELD(funcresulttype);
	WRITE_BOOL_FIELD(funcretset);
#if PG_VERSION_NUM >= 90400
	WRITE_BOOL_FIELD(funcvariadic);
#endif
	WRITE_ENUM_FIELD(funcformat, CoercionForm);
	WRITE_OID_FIELD(funccollid);
	WRITE_OID_FIELD(inputcollid);
	WRITE_NODE_FIELD(args);
	WRITE_LOCATION_FIELD(location);

	env.popNode();
}

static void
outputNamedArgExpr(NodeInfoEnv& env, const NamedArgExpr *node)
{
	env.pushNode(node, "NamedArgExpr");

	WRITE_NODE_FIELD(arg);
	WRITE_STRING_FIELD(name);
	WRITE_INT_FIELD(argnumber);
	WRITE_LOCATION_FIELD(location);

	env.popNode();
}

static void
outputOpExpr(NodeInfoEnv& env, const OpExpr *node)
{
	env.pushNode(node, "OpExpr");

	_outputOpExpr(env, node);

	env.popNode();
}

static void
outputDistinctExpr(NodeInfoEnv& env, const DistinctExpr *node)
{
	env.pushNode(node, "DistinctExpr");

	_outputOpExpr(env, reinterpret_cast<const OpExpr*>(node));

	env.popNode();
}

static void
outputNullIfExpr(NodeInfoEnv& env, const NullIfExpr *node)
{
	env.pushNode(node, "NullIfExpr");

	_outputOpExpr(env, reinterpret_cast<const OpExpr*>(node));

	env.popNode();
}

static void
outputScalarArrayOpExpr(NodeInfoEnv& env, const ScalarArrayOpExpr *node)
{
	env.pushNode(node, "ScalarArrayOpExpr");

	WRITE_OID_FIELD(opno);
	WRITE_OID_FIELD(opfuncid);
	WRITE_BOOL_FIELD(useOr);
	WRITE_OID_FIELD(inputcollid);
	WRITE_NODE_FIELD(args);
	WRITE_LOCATION_FIELD(location);

	env.popNode();
}

static void
outputBoolExpr(NodeInfoEnv& env, const BoolExpr *node)
{
	env.pushNode(node, "BoolExpr");

	WRITE_ENUM_FIELD(boolop, BoolExprType);
	WRITE_NODE_FIELD(args);
	WRITE_LOCATION_FIELD(location);

	env.popNode();
}

static void
outputSubLink(NodeInfoEnv& env, const SubLink *node)
{
	env.pushNode(node, "SubLink");

	WRITE_ENUM_FIELD(subLinkType, SubLinkType);
	WRITE_NODE_FIELD(testexpr);
	WRITE_NODE_FIELD(operName);
	WRITE_NODE_FIELD(subselect);
	WRITE_LOCATION_FIELD(location);

	env.popNode();
}

static void
outputSubPlan(NodeInfoEnv& env, const SubPlan *node)
{
	env.pushNode(node, "SubPlan");

	WRITE_ENUM_FIELD(subLinkType, SubLinkType);
	WRITE_NODE_FIELD(testexpr);
	WRITE_NODE_FIELD(paramIds);
	WRITE_INT_FIELD(plan_id);
	WRITE_STRING_FIELD(plan_name);
	WRITE_OID_FIELD(firstColType);
	WRITE_INT_FIELD(firstColTypmod);
	WRITE_OID_FIELD(firstColCollation);
	WRITE_BOOL_FIELD(useHashTable);
	WRITE_BOOL_FIELD(unknownEqFalse);
	WRITE_NODE_FIELD(setParam);
	WRITE_NODE_FIELD(parParam);
	WRITE_NODE_FIELD(args);
	WRITE_COST_FIELD(startup_cost);
	WRITE_COST_FIELD(per_call_cost);

	env.popNode();
}

static void
outputAlternativeSubPlan(NodeInfoEnv& env, const AlternativeSubPlan *node)
{
	env.pushNode(node, "AlternativeSubPlan");

	WRITE_NODE_FIELD(subplans);

	env.popNode();
}

static void
outputFieldSelect(NodeInfoEnv& env, const FieldSelect *node)
{
	env.pushNode(node, "FieldSelect");

	WRITE_NODE_FIELD(arg);
	WRITE_ATTRNUMBER_FIELD(fieldnum);
	WRITE_OID_FIELD(resulttype);
	WRITE_INT32_FIELD(resulttypmod);
	WRITE_OID_FIELD(resultcollid);

	env.popNode();
}

static void
outputFieldStore(NodeInfoEnv& env, const FieldStore *node)
{
	env.pushNode(node, "FieldStore");

	WRITE_NODE_FIELD(arg);
	WRITE_NODE_FIELD(newvals);
	WRITE_NODE_FIELD(fieldnums);
	WRITE_OID_FIELD(resulttype);
	
	env.popNode();
}

static void
outputRelabelType(NodeInfoEnv& env, const RelabelType *node)
{
	env.pushNode(node, "RelabelType");

	WRITE_NODE_FIELD(arg);
	WRITE_OID_FIELD(resulttype);
	WRITE_INT32_FIELD(resulttypmod);
	WRITE_OID_FIELD(resultcollid);
	WRITE_ENUM_FIELD(relabelformat, CoercionForm);
	WRITE_LOCATION_FIELD(location);

	env.popNode();
}

static void
outputCoerceViaIO(NodeInfoEnv& env, const CoerceViaIO *node)
{
	env.pushNode(node, "CoerceViaIO");

	WRITE_NODE_FIELD(arg);
	WRITE_OID_FIELD(resulttype);
	WRITE_OID_FIELD(resultcollid);
	WRITE_ENUM_FIELD(coerceformat, CoercionForm);
	WRITE_LOCATION_FIELD(location);

	env.popNode();
}

static void
outputArrayCoerceExpr(NodeInfoEnv& env, const ArrayCoerceExpr *node)
{
	env.pushNode(node, "ArrayCoerceExpr");

	WRITE_NODE_FIELD(arg);
	WRITE_OID_FIELD(elemfuncid);
	WRITE_OID_FIELD(resulttype);
	WRITE_INT32_FIELD(resulttypmod);
	WRITE_OID_FIELD(resultcollid);
	WRITE_BOOL_FIELD(isExplicit);
	WRITE_ENUM_FIELD(coerceformat, CoercionForm);
	WRITE_LOCATION_FIELD(location);

	env.popNode();
}

static void
outputConvertRowtypeExpr(NodeInfoEnv& env, const ConvertRowtypeExpr *node)
{
	env.pushNode(node, "ConvertRowtypeExpr");

	env.popNode();
}

static void
outputCollateExpr(NodeInfoEnv& env, const CollateExpr *node)
{
	env.pushNode(node, "CollateExpr");

	env.popNode();
}

static void
outputCaseExpr(NodeInfoEnv& env, const CaseExpr *node)
{
	env.pushNode(node, "CaseExpr");

	WRITE_OID_FIELD(casetype);
	WRITE_OID_FIELD(casecollid);
	WRITE_NODE_FIELD(arg);
	WRITE_NODE_FIELD(args);
	WRITE_NODE_FIELD(defresult);
	WRITE_LOCATION_FIELD(location);

	env.popNode();
}

static void
outputCaseWhen(NodeInfoEnv& env, const CaseWhen *node)
{
	env.pushNode(node, "CaseWhen");

	WRITE_NODE_FIELD(expr);
	WRITE_NODE_FIELD(result);
	WRITE_LOCATION_FIELD(location);

	env.popNode();
}

static void
outputCaseTestExpr(NodeInfoEnv& env, const CaseTestExpr *node)
{
	env.pushNode(node, "CaseTestExpr");

	WRITE_OID_FIELD(typeId);
	WRITE_INT_FIELD(typeMod);
	WRITE_OID_FIELD(collation);

	env.popNode();
}

static void
outputArrayExpr(NodeInfoEnv& env, const ArrayExpr *node)
{
	env.pushNode(node, "ArrayExpr");

	WRITE_OID_FIELD(array_typeid);
	WRITE_OID_FIELD(array_collid);
	WRITE_OID_FIELD(element_typeid);
	WRITE_NODE_FIELD(elements);
	WRITE_BOOL_FIELD(multidims);
	WRITE_LOCATION_FIELD(location);

	env.popNode();
}

static void
outputRowExpr(NodeInfoEnv& env, const RowExpr *node)
{
	env.pushNode(node, "RowExpr");

	WRITE_NODE_FIELD(args);
	WRITE_OID_FIELD(row_typeid);
	WRITE_ENUM_FIELD(row_format, CoercionForm);
	WRITE_NODE_FIELD(colnames);
	WRITE_LOCATION_FIELD(location);

	env.popNode();
}

static void
outputRowCompareExpr(NodeInfoEnv& env, const RowCompareExpr *node)
{
	env.pushNode(node, "RowCompareExpr");

	WRITE_ENUM_FIELD(rctype, RowCompareType);
	WRITE_NODE_FIELD(opnos);
	WRITE_NODE_FIELD(opfamilies);
	WRITE_NODE_FIELD(inputcollids);
	WRITE_NODE_FIELD(largs);
	WRITE_NODE_FIELD(rargs);

	env.popNode();
}

static void
outputCoalesceExpr(NodeInfoEnv& env, const CoalesceExpr *node)
{
	env.pushNode(node, "CoalesceExpr");

	WRITE_OID_FIELD(coalescetype);
	WRITE_OID_FIELD(coalescecollid);
	WRITE_NODE_FIELD(args);
	WRITE_LOCATION_FIELD(location);

	env.popNode();
}

static void
outputMinMaxExpr(NodeInfoEnv& env, const MinMaxExpr *node)
{
	env.pushNode(node, "MinMaxExpr");

	WRITE_OID_FIELD(minmaxtype);
	WRITE_OID_FIELD(minmaxcollid);
	WRITE_OID_FIELD(inputcollid);
	WRITE_ENUM_FIELD(op, MinMaxOp);
	WRITE_NODE_FIELD(args);
	WRITE_LOCATION_FIELD(location);

	env.popNode();
}

static void
outputXmlExpr(NodeInfoEnv& env, const XmlExpr *node)
{
	env.pushNode(node, "XmlExpr");

	WRITE_ENUM_FIELD(op, XmlExprOp);
	WRITE_STRING_FIELD(name);
	WRITE_NODE_FIELD(named_args);
	WRITE_NODE_FIELD(arg_names);
	WRITE_NODE_FIELD(args);
	WRITE_ENUM_FIELD(xmloption, XmlOptionType);
	WRITE_OID_FIELD(type);
	WRITE_INT32_FIELD(typmod);
	WRITE_LOCATION_FIELD(location);

	env.popNode();
}

static void
outputNullTest(NodeInfoEnv& env, const NullTest *node)
{
	env.pushNode(node, "NullTest");

	WRITE_NODE_FIELD(arg);
	WRITE_ENUM_FIELD(nulltesttype, NullTestType);
	WRITE_BOOL_FIELD(argisrow);

	env.popNode();
}

static void
outputBooleanTest(NodeInfoEnv& env, const BooleanTest *node)
{
	env.pushNode(node, "BooleanTest");

	WRITE_NODE_FIELD(arg);
	WRITE_ENUM_FIELD(booltesttype, BoolTestType);

	env.popNode();
}

static void
outputCoerceToDomain(NodeInfoEnv& env, const CoerceToDomain *node)
{
	env.pushNode(node, "CoerceToDomain");

	WRITE_NODE_FIELD(arg);
	WRITE_OID_FIELD(resulttype);
	WRITE_INT32_FIELD(resulttypmod);
	WRITE_OID_FIELD(resultcollid);
	WRITE_ENUM_FIELD(coercionformat, CoercionForm);
	WRITE_LOCATION_FIELD(location);

	env.popNode();
}

static void
outputCoerceToDomainValue(NodeInfoEnv& env, const CoerceToDomainValue *node)
{
	env.pushNode(node, "CoerceToDomainValue");

	WRITE_OID_FIELD(typeId);
	WRITE_INT32_FIELD(typeMod);
	WRITE_OID_FIELD(collation);
	WRITE_LOCATION_FIELD(location);

	env.popNode();
}

static void
outputSetToDefault(NodeInfoEnv& env, const SetToDefault *node)
{
	env.pushNode(node, "SetToDefault");

	WRITE_OID_FIELD(typeId);
	WRITE_INT_FIELD(typeMod);
	WRITE_OID_FIELD(collation);
	WRITE_LOCATION_FIELD(location);

	env.popNode();
}

static void
outputCurrentOfExpr(NodeInfoEnv& env, const CurrentOfExpr *node)
{
	env.pushNode(node, "CurrentOfExpr");

	WRITE_INDEX_FIELD(cvarno); /* RT index of target relation */
	WRITE_STRING_FIELD(cursor_name);
	WRITE_INT_FIELD(cursor_param);

	env.popNode();
}

static void
outputTargetEntry(NodeInfoEnv& env, const TargetEntry *node)
{
	env.pushNode(node, "TargetEntry");

	WRITE_NODE_FIELD(expr);
	WRITE_ATTRNUMBER_FIELD(resno);
	WRITE_STRING_FIELD(resname);
	if (node->ressortgroupref)
		WRITE_INDEX_FIELD(ressortgroupref); /* sort/group clause */
	WRITE_OID_FIELD(resorigtbl);
	if (node->resorigcol)
		WRITE_ATTRNUMBER_FIELD(resorigcol);
	WRITE_BOOL_FIELD(resjunk);

	env.popNode();
}

static void
outputRangeTblRef(NodeInfoEnv& env, const RangeTblRef *node)
{
	env.pushNode(node, "RangeTblRef");

	WRITE_INT_FIELD(rtindex);

	env.popNode();
}

static void
outputJoinExpr(NodeInfoEnv& env, const JoinExpr *node)
{
	env.pushNode(node, "JoinExpr");

	WRITE_ENUM_FIELD(jointype, JoinType);
	WRITE_BOOL_FIELD(isNatural);
	WRITE_NODE_FIELD(larg);
	WRITE_NODE_FIELD(rarg);
	WRITE_NODE_FIELD(usingClause);
	WRITE_NODE_FIELD(quals);
	WRITE_NODE_FIELD(alias);
	WRITE_INT_FIELD(rtindex); /* RT index assigned for join, or 0 */

	env.popNode();
}

static void
outputFromExpr(NodeInfoEnv& env, const FromExpr *node)
{
	env.pushNode(node, "FromExpr");

	WRITE_NODE_FIELD(fromlist);
	WRITE_NODE_FIELD(quals);

	env.popNode();
}

static void
outputPlannerGlobal(NodeInfoEnv& env, const PlannerGlobal *node)
{
	env.pushNode(node, "PlannerGlobal");

	/* @todo boundParams */

	WRITE_NODE_FIELD(subplans);
	WRITE_NODE_FIELD(subroots);
	WRITE_BITMAPSET_FIELD(rewindPlanIDs);
	WRITE_NODE_FIELD(finalrtable);
	WRITE_NODE_FIELD(finalrowmarks);
	WRITE_NODE_FIELD(resultRelations);
	WRITE_NODE_FIELD(relationOids);
	WRITE_NODE_FIELD(invalItems);
	WRITE_INT_FIELD(nParamExec);
	WRITE_INDEX_FIELD(lastPHId);
	WRITE_INDEX_FIELD(lastRowMarkId);
	WRITE_BOOL_FIELD(transientPlan);
	
	env.popNode();
}

static void
outputPlannerInfo(NodeInfoEnv& env, const PlannerInfo *node)
{
	int i;

	env.pushNode(node, "PlannerInfo");

	WRITE_NODE_FIELD(parse);
	WRITE_NODE_FIELD(glob);
	WRITE_INDEX_FIELD(query_level);
	WRITE_NODE_FIELD(parent_root);
	WRITE_NODE_FIELD(plan_params);

	WRITE_INT_FIELD(simple_rel_array_size);

	for (i=1 ; i<node->simple_rel_array_size ; i++)
		WRITE_NODE_INDEX_FIELD(simple_rel_array, i);

	for (i=1 ; i<node->simple_rel_array_size ; i++)
		WRITE_NODE_INDEX_FIELD(simple_rte_array, i);

	WRITE_BITMAPSET_FIELD(all_baserels);
	WRITE_BITMAPSET_FIELD(nullable_baserels);
	WRITE_NODE_FIELD(join_rel_list);

#if 0
	/* @todo */
	struct HTAB *join_rel_hash; /* optional hashtable for join relations */
#endif

	WRITE_INT_FIELD(join_cur_level);

	for (i=0 ; i<node->join_cur_level ; i++)
		WRITE_NODE_INDEX_FIELD(join_rel_level, i);

	WRITE_NODE_FIELD(init_plans);
	WRITE_NODE_FIELD(cte_plan_ids);
	WRITE_NODE_FIELD(eq_classes);
	WRITE_NODE_FIELD(canon_pathkeys);
	WRITE_NODE_FIELD(left_join_clauses);
	WRITE_NODE_FIELD(right_join_clauses);
	WRITE_NODE_FIELD(full_join_clauses);
	WRITE_NODE_FIELD(join_info_list);
#if PG_VERSION_NUM >= 90300
	WRITE_NODE_FIELD(lateral_info_list);
#endif
	WRITE_NODE_FIELD(append_rel_list);
	WRITE_NODE_FIELD(rowMarks);
	WRITE_NODE_FIELD(placeholder_list);
	WRITE_NODE_FIELD(query_pathkeys);
	WRITE_NODE_FIELD(group_pathkeys);
	WRITE_NODE_FIELD(window_pathkeys);
	WRITE_NODE_FIELD(distinct_pathkeys);
	WRITE_NODE_FIELD(sort_pathkeys);
	WRITE_NODE_FIELD(minmax_aggs);
	WRITE_NODE_FIELD(initial_rels);
#if 0
	MemoryContext planner_cxt;	/* context holding PlannerInfo */
#endif
	WRITE_FLOAT_FIELD(total_table_pages, "%f");
	WRITE_FLOAT_FIELD(tuple_fraction, "%f");
	WRITE_FLOAT_FIELD(limit_tuples, "%f");
	WRITE_BOOL_FIELD(hasInheritedTarget);
	WRITE_BOOL_FIELD(hasJoinRTEs);
#if PG_VERSION_NUM >= 90300
	WRITE_BOOL_FIELD(hasLateralRTEs);
#endif
	WRITE_BOOL_FIELD(hasHavingQual);
	WRITE_BOOL_FIELD(hasPseudoConstantQuals);
	WRITE_BOOL_FIELD(hasRecursion);
	WRITE_INT_FIELD(wt_param_id);
	WRITE_NODE_FIELD(non_recursive_plan);
	WRITE_BITMAPSET_FIELD(curOuterRels);
	WRITE_NODE_FIELD(curOuterParams);

#if 0
	/* @todo */
	void	   *join_search_private;	
#endif
  
	env.popNode();
}

static void
outputRelOptInfo(NodeInfoEnv& env, const RelOptInfo *node)
{
	env.pushNode(node, "RelOptInfo");

	WRITE_ENUM_FIELD(reloptkind, RelOptKind);
	WRITE_BITMAPSET_FIELD(relids);
	WRITE_FLOAT_FIELD(rows, "%f");
	WRITE_INT_FIELD(width);
#if PG_VERSION_NUM >= 90300
	WRITE_BOOL_FIELD(consider_startup);
#endif
	WRITE_NODE_FIELD(reltargetlist);
	WRITE_NODE_FIELD(pathlist);
	WRITE_NODE_FIELD(ppilist);

	WRITE_NODE_FIELD(cheapest_startup_path);
	WRITE_NODE_FIELD(cheapest_total_path);
	WRITE_NODE_FIELD(cheapest_unique_path);
	WRITE_NODE_FIELD(cheapest_parameterized_paths);

	WRITE_INDEX_FIELD(relid);
	WRITE_OID_FIELD(reltablespace); 
	WRITE_ENUM_FIELD(rtekind, RTEKind);
	WRITE_ATTRNUMBER_FIELD(min_attr);
	WRITE_ATTRNUMBER_FIELD(max_attr);

#if 0
	/* @todo */
	/* attr_needed */
	/* attr_widths */
#endif

#if PG_VERSION_NUM >= 90300
	WRITE_NODE_FIELD(lateral_vars);
	WRITE_BITMAPSET_FIELD(lateral_relids);
	WRITE_BITMAPSET_FIELD(lateral_referencers);
#endif

	WRITE_NODE_FIELD(indexlist);
	WRITE_UINT_FIELD(pages);
	WRITE_FLOAT_FIELD(tuples, "%f");
	WRITE_FLOAT_FIELD(allvisfrac, "%f");
	WRITE_NODE_FIELD(subplan);
	WRITE_NODE_FIELD(subroot);
#if PG_VERSION_NUM >= 90300
	WRITE_NODE_FIELD(subplan_params);
#endif
	WRITE_NODE_FIELD(fdwroutine);
#if 0
	/* @todo */
	/* void       *fdw_private */
#endif

	WRITE_NODE_FIELD(baserestrictinfo);
	WRITE_NODE_FIELD(joininfo);
	WRITE_BOOL_FIELD(has_eclass_joins);
	
	env.popNode();
}

static void
outputQuery(NodeInfoEnv& env, const Query *node)
{
	env.pushNode(node, "Query");

	WRITE_ENUM_FIELD(commandType, CmdType);
	WRITE_ENUM_FIELD(querySource, QuerySource);
	WRITE_BOOL_FIELD(canSetTag);

	WRITE_NODE_FIELD(utilityStmt);

	WRITE_INT_FIELD(resultRelation);
	WRITE_BOOL_FIELD(hasAggs);
	WRITE_BOOL_FIELD(hasWindowFuncs);
	WRITE_BOOL_FIELD(hasSubLinks);
	WRITE_BOOL_FIELD(hasDistinctOn);
	WRITE_BOOL_FIELD(hasRecursive);
	WRITE_BOOL_FIELD(hasModifyingCTE);
	WRITE_BOOL_FIELD(hasForUpdate);
	WRITE_NODE_FIELD(cteList);
	WRITE_NODE_FIELD(rtable);
	WRITE_NODE_FIELD(jointree);
	WRITE_NODE_FIELD(targetList);
#if PG_VERSION_NUM >= 90400
	WRITE_NODE_FIELD(withCheckOptions);
#endif
	WRITE_NODE_FIELD(returningList);
	WRITE_NODE_FIELD(groupClause);
	WRITE_NODE_FIELD(havingQual);
	WRITE_NODE_FIELD(windowClause);
	WRITE_NODE_FIELD(distinctClause);
	WRITE_NODE_FIELD(sortClause);
	WRITE_NODE_FIELD(limitOffset);
	WRITE_NODE_FIELD(limitCount);
	WRITE_NODE_FIELD(rowMarks);
	WRITE_NODE_FIELD(setOperations);
	WRITE_NODE_FIELD(constraintDeps);

	env.popNode();
}

static void
outputSortGroupClause(NodeInfoEnv& env, const SortGroupClause *node)
{
	env.pushNode(node, "SortGroupClause");

	WRITE_INDEX_FIELD(tleSortGroupRef); /* reference into targetlist */
	WRITE_OID_FIELD(eqop);
	WRITE_OID_FIELD(sortop);
	WRITE_BOOL_FIELD(nulls_first);
	WRITE_BOOL_FIELD(hashable);

	env.popNode();
}

static void
outputRangeTblEntry(NodeInfoEnv& env, const RangeTblEntry *node)
{
	env.pushNode(node, "RangeTblEntry(RTE)");

	/* put alias + eref first to make dump more legible */
	WRITE_NODE_FIELD(alias);
	WRITE_NODE_FIELD(eref);
	WRITE_ENUM_FIELD(rtekind, RTEKind);

	switch (node->rtekind)
	{
		case RTE_RELATION:
			WRITE_OID_FIELD(relid);
			WRITE_CHAR_FIELD(relkind);
			break;
		case RTE_SUBQUERY:
			WRITE_NODE_FIELD(subquery);
			WRITE_BOOL_FIELD(security_barrier);
			break;
		case RTE_JOIN:
			WRITE_ENUM_FIELD(jointype, JoinType);
			WRITE_NODE_FIELD(joinaliasvars);
			break;
		case RTE_FUNCTION:
#if PG_VERSION_NUM >= 90400
			WRITE_NODE_FIELD(functions);
			WRITE_BOOL_FIELD(funcordinality);
#else
			WRITE_NODE_FIELD(funcexpr);
			WRITE_NODE_FIELD(funccoltypes);
			WRITE_NODE_FIELD(funccoltypmods);
			WRITE_NODE_FIELD(funccolcollations);
#endif
			break;
		case RTE_VALUES:
			WRITE_NODE_FIELD(values_lists);
			WRITE_NODE_FIELD(values_collations);
			break;
		case RTE_CTE:
			WRITE_STRING_FIELD(ctename);
			WRITE_INDEX_FIELD(ctelevelsup); /* number of query levels up */
			WRITE_BOOL_FIELD(self_reference);
			WRITE_NODE_FIELD(ctecoltypes);
			WRITE_NODE_FIELD(ctecoltypmods);
			WRITE_NODE_FIELD(ctecolcollations);
			break;
		default:
			elog(ERROR, "unrecognized RTE kind: %d", (int) node->rtekind);
			break;
	}

	WRITE_BOOL_FIELD(inh);
	WRITE_BOOL_FIELD(inFromCl);
	WRITE_UINT_FIELD(requiredPerms);
	WRITE_OID_FIELD(checkAsUser);
	WRITE_BITMAPSET_FIELD(selectedCols);
	WRITE_BITMAPSET_FIELD(modifiedCols);

	env.popNode();
}
