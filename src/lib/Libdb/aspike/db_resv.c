/*
 * Copyright (C) 1994-2018 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *
 * This file is part of the PBS Professional ("PBS Pro") software.
 *
 * Open Source License Information:
 *
 * PBS Pro is free software. You can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * PBS Pro is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Commercial License Information:
 *
 * For a copy of the commercial license terms and conditions,
 * go to: (http://www.pbspro.com/UserArea/agreement.html)
 * or contact the Altair Legal Department.
 *
 * Altair’s dual-license business model allows companies, individuals, and
 * organizations to create proprietary derivative works of PBS Pro and
 * distribute them - whether embedded or bundled with other software -
 * under a commercial license agreement.
 *
 * Use of Altair’s trademarks, including but not limited to "PBS™",
 * "PBS Professional®", and "PBS Pro™" and Altair’s logos is subject to Altair's
 * trademark licensing policies.
 *
 */


/**
 * @file    db_pbs_resv.c
 *
 * @brief
 *      Implementation of the resv data access functions for postgres
 */

#include <pbs_config.h>   /* the master config generated by configure */
#include "pbs_db.h"
#include "db_aerospike.h"

/**
 *@brief
 *	Save (insert/update) a new/existing resv
 *
 * @param[in]	conn - The connnection handle
 * @param[in]	obj  - The resv object to save
 * @param[in]	savetype - PBS_UPDATE_DB_QUICK/PBS_UPDATE_DB_FULL/PBS_INSERT_DB
 *
 * @return      Error code
 * @retval	-1 - Execution of prepared statement failed
 * @retval	 0 - Success and > 0 rows were affected
 * @retval	 1 - Execution succeeded but statement did not affect any rows
 *
 */
int
pbs_db_save_resv(void *conn, pbs_db_obj_info_t *obj, int savetype)
{
	pbs_db_resv_info_t *presv = obj->pbs_db_un.pbs_db_resv;
	int numbins = 0;
	as_key key;
	as_string   ri_queue;
	as_integer  ri_state;
	as_integer  ri_substate;
	as_integer  ri_stime;
	as_integer  ri_etime;
	as_integer  ri_duration;
	as_integer  ri_tactive;
	as_integer  ri_svrflags;
	as_integer  ri_savetm;
	as_integer  ri_creattm;
	as_hashmap attrmap;
	as_map_policy map_policy;
	as_policy_operate op_policy;
	as_operations ops;
	as_error err;
	as_record* rec = NULL;
	aerospike *as = (aerospike *) conn;

	if (savetype & OBJ_SAVE_QS)
		numbins = 8;

	if (savetype & OBJ_SAVE_NEW)
		numbins++;  /* for createtm */
	if ((presv->db_attr_list.attr_count > 0) || (savetype & OBJ_SAVE_NEW))
		numbins++;  /* for attributes */

	as_operations_inita(&ops, numbins);
	as_key_init_str(&key, g_namespace, "resv", presv->ri_resvid);

	if (savetype & OBJ_SAVE_QS) {
		as_string_init(&ri_queue, presv->ri_queue, false);
		as_operations_add_write(&ops, "ri_queue", (as_bin_value*) &ri_queue);
		as_integer_init(&ri_state, presv->ri_state);
		as_operations_add_write(&ops, "ri_state", (as_bin_value*) &ri_state);
		as_integer_init(&ri_substate, presv->ri_substate);
		as_operations_add_write(&ops, "ri_substate", (as_bin_value*) &ri_substate);
		as_integer_init(&ri_stime, presv->ri_stime);
		as_operations_add_write(&ops, "ri_stime", (as_bin_value*) &ri_stime);
		as_integer_init(&ri_etime, presv->ri_etime);
		as_operations_add_write(&ops, "ri_etime", (as_bin_value*) &ri_etime);
		as_integer_init(&ri_duration, presv->ri_duration);
		as_operations_add_write(&ops, "ri_duration", (as_bin_value*) &ri_duration);
		as_integer_init(&ri_tactive, presv->ri_tactive);
		as_operations_add_write(&ops, "ri_tactive", (as_bin_value*) &ri_tactive);	
		as_integer_init(&ri_svrflags, presv->ri_svrflags);
		as_operations_add_write(&ops, "ri_svrflags", (as_bin_value*) &ri_svrflags);
		as_integer_init(&ri_savetm, db_time_now(0));
		as_operations_add_write(&ops, "ri_savetm", (as_bin_value*) &ri_savetm);
	}

	if (savetype & OBJ_SAVE_NEW) {
		as_integer_init(&ri_creattm, db_time_now(0));
		as_operations_add_write(&ops, "ri_creattm", (as_bin_value*) &ri_creattm);
	}

	if ((presv->db_attr_list.attr_count > 0) || (savetype & OBJ_SAVE_NEW)) {
		if (convert_db_attr_list_to_asmap(&attrmap, &presv->db_attr_list) < 0) 
			return -1;

		as_map_policy_init(&map_policy);
		as_operations_add_map_put_items(&ops, "attributes", &map_policy, (as_map*) &attrmap);
	}

	as_policy_operate_init(&op_policy);
	op_policy.key = AS_POLICY_KEY_SEND;
	
	if (aerospike_key_operate(as, &err, &op_policy, &key, &ops, &rec) != AEROSPIKE_OK) {
		char msg[4096];
		sprintf(msg, "aerospike_key_operate() returned %d - %s\n", err.code, err.message);
		db_set_error("Execution of save resv", msg, NULL);
		as_operations_destroy(&ops);
		as_record_destroy(rec);
		return -1;
	}

	as_operations_destroy(&ops);
	as_record_destroy(rec);
	
	return 0;
}

static int
load_resv(as_record* rec, pbs_db_resv_info_t *presv)
{
	as_map *attrmap;
	strcpy(presv->ri_queue, as_record_get_str(rec, "ri_queue"));
	presv->ri_state = as_record_get_int64(rec, "ri_state", INT64_MAX);
	presv->ri_substate = as_record_get_int64(rec, "ri_substate", INT64_MAX);
	presv->ri_stime = as_record_get_int64(rec, "ri_stime", INT64_MAX);
	presv->ri_etime = as_record_get_int64(rec, "ri_etime", INT64_MAX);
	presv->ri_duration = as_record_get_int64(rec, "ri_duration", INT64_MAX);
	presv->ri_tactive = as_record_get_int64(rec, "ri_tactive", INT64_MAX);
	presv->ri_svrflags = as_record_get_int64(rec, "ri_svrflags", INT64_MAX);

	attrmap = as_record_get_map(rec, "attributes");
	if (convert_asmap_to_db_attr_list((as_hashmap *) attrmap, &presv->db_attr_list) < 0) 
		return -1;

	return 0;
}

/**
 * @brief
 *	Load resv data from the database
 *
 * @param[in]	conn - Connection handle
 * @param[in/out]obj  - Load resv information into this object where
 *			resvid = obj->pbs_db_un.pbs_db_resv->ri_resvid
 *
 * @return      Error code
 * @retval	-1 - Failure
 * @retval	 0 - Success
 * @retval	>1 - Number of attributes
 * @retval 	-2 -  Success but data same as old, so not loading data (but locking if lock requested)
 *
 */
int
pbs_db_load_resv(void *conn, pbs_db_obj_info_t *obj)
{
	pbs_db_resv_info_t *presv = obj->pbs_db_un.pbs_db_resv;
	int rc;
	as_key key; /* resvid */
	as_error err;
	as_record* p_rec = NULL;
	aerospike *as = (aerospike *) conn;

	as_key_init_str(&key, g_namespace, "resv", presv->ri_resvid);
	if (aerospike_key_get(as, &err, NULL, &key, &p_rec) != AEROSPIKE_OK) {
		char msg[4096];
		sprintf(msg, "aerospike_key_get() returned %d - %s\n", err.code, err.message);
		db_set_error("Execution of load resv", msg, NULL);
		as_record_destroy(p_rec);
		return -1;
	}

	rc = load_resv(p_rec, presv);
	as_record_destroy(p_rec);
	
	return rc;
}

static
bool resv_callback(const as_val *val, void* udata)
{
	pbs_db_obj_info_t obj;
	pbs_db_resv_info_t dbresv;
	pbs_db_resv_info_t *presv = &dbresv;
	db_query_state_t *state = (db_query_state_t *) udata;
	as_record* rec = NULL;

	obj.pbs_db_obj_type = PBS_DB_RESV;
	obj.pbs_db_un.pbs_db_resv = presv;

	rec = as_record_fromval(val);
	if (!rec) return false;

	strcpy(presv->ri_resvid, rec->key.value.string.value);
	load_resv(rec, presv);
	as_record_destroy(rec);
	state->query_cb(&obj, &state->count);

	pbs_db_reset_resv(&obj);
	
	return true;
}

/**
 * @brief
 *	Find resvs
 *
 * @param[in]	conn - Connection handle
 * @param[out]  st   - The cursor state variable updated by this query
 * @param[in]	obj  - Information of resv to be found
 * @param[in]	opts - Any other options (like flags, timestamp)
 *
 * @return      Error code
 * @retval	-1 - Failure
 * @retval	 0 - Success
 * @retval	 1 -  Success but no rows found
 *
 */
int
pbs_db_find_resv(void *conn, void *st, pbs_db_obj_info_t *obj, pbs_db_query_options_t *opts)
{
	db_query_state_t *state = (db_query_state_t *) st;
	aerospike *as = (aerospike *) conn;
	as_query query;
	int savetm;
	as_error err;

	if (!state)
		return -1;

	as_query_init(&query, g_namespace, "resv");

	if (opts != NULL && opts->timestamp) {
		as_query_where_inita(&query, 1);
		savetm = opts->timestamp;
		as_query_where(&query, "ri_savetm", as_integer_range(savetm, db_time_now(0)));
		// order by ri_creattm
	}

	state->row = 0;
	state->count = 0;

	if (aerospike_query_foreach(as, &err, NULL, &query, resv_callback, state) != AEROSPIKE_OK) {
		char msg[4096];
		sprintf(msg,"aerospike_query_foreach() returned %d - %s", err.code, err.message);
		db_set_error("Execution of find resv", msg, NULL);
		as_query_destroy(&query);
		return -1;
	}
	as_query_destroy(&query);

	return 0;
}


/**
 * @brief
 *	Delete the resv from the database
 *
 * @param[in]	conn - Connection handle
 * @param[in]	obj  - resv information
 *
 * @return      Error code
 * @retval	-1 - Failure
 * @retval	 0 - Success
 * @retval	 1 -  Success but no rows deleted
 *
 */
int
pbs_db_delete_resv(void *conn, pbs_db_obj_info_t *obj)
{
	pbs_db_resv_info_t *presv = obj->pbs_db_un.pbs_db_resv;
	as_key key; /* resvid */
	as_error err;
	aerospike *as = (aerospike *) conn;

	as_key_init_str(&key, g_namespace, "resv", presv->ri_resvid);
	if (aerospike_key_remove(as, &err, NULL, &key) != AEROSPIKE_OK) {
		char msg[4096];
		sprintf(msg, "aerospike_key_remove() returned %d - %s\n", err.code, err.message);
		db_set_error("Execution of delete resv", msg, NULL);
		return -1;
	}

	return 0;
}


/**
 * @brief
 *	Deletes attributes of a resv
 *
 * @param[in]	conn - Connection handle
 * @param[in]	obj  - resv information
 * @param[in]	obj_id  - resv id
 * @param[in]	attr_list - List of attributes
 *
 * @return      Error code
 * @retval	 0 - Success
 * @retval	-1 - On Failure
 *
 */
int
pbs_db_del_attr_resv(void *conn, void *obj_id, pbs_db_attr_list_t *attr_list)
{
	as_string attr;
	int numbins = 1;
	aerospike *as = (aerospike *) conn;
	as_policy_operate op_policy;
	as_operations ops;
	as_error err;
	as_record* rec = NULL;
	as_key key;

	as_operations_inita(&ops, numbins);
	as_key_init_str(&key, g_namespace, "resv", obj_id);
	as_string_init(&attr, "", false);
	as_operations_add_write(&ops, "attributes", (as_bin_value*) &attr);

	if (aerospike_key_operate(as, &err, &op_policy, &key, &ops, &rec) != AEROSPIKE_OK) {
		char msg[4096];
		sprintf(msg, "aerospike_key_operate() returned %d - %s\n", err.code, err.message);
		db_set_error("Execution of delete resv attrs", msg, NULL);
		as_operations_destroy(&ops);
		as_record_destroy(rec);
		return -1;
	}

	as_operations_destroy(&ops);
	as_record_destroy(rec);
	return 0;
}

/**
 * @brief
 *	Frees allocate memory of an Object
 *
 * @param[in]	obj - pbs_db_obj_info_t containing the DB object
 *
 * @return      None
 *
 */
void
pbs_db_reset_resv(pbs_db_obj_info_t *obj)
{
	obj->pbs_db_un.pbs_db_resv->ri_resvid[0] = '\0';
}