#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // sysconf() - get CPU count
#include "rsdb.h"
#include "global.h"
#include "utils/log.h"
#include "version.h"
#include "address.h"

#define RSDB_LOG_FREE_ERRMSG(errmsg) xdag_info("%s %lu %s \n",__FUNCTION__, __LINE__,errmsg);free(errmsg)
#define RSDB_ASSERT_ERROR(errmsg) if(errmsg){RSDB_LOG_FREE_ERRMSG(errmsg);return XDAG_RSDB_CONF_ERROR;}

xd_rsdb_op_t xd_rsdb_pre_init(int read_only)
{
    xd_rsdb_t* rsdb = NULL;
    xd_rsdb_conf_t* rsdb_config = NULL;
    xd_rsdb_cf_t* rsdb_cf = NULL;

    rsdb = malloc(sizeof(xd_rsdb_t));
    rsdb_config = malloc(sizeof(xd_rsdb_conf_t));
    rsdb_cf = malloc(sizeof(xd_rsdb_cf_t));
    
    memset(rsdb, 0, sizeof(xd_rsdb_t));
    memset(rsdb_config, 0, sizeof(xd_rsdb_conf_t));
    memset(rsdb_cf, 0, sizeof(xd_rsdb_cf_t));

    rsdb->config = rsdb_config;
    rsdb->config->db_name = strdup(g_xdag_testnet?"chainstate-test":"chainstate");
    rsdb->config->db_path = strdup(g_xdag_testnet?"chainstate-test":"chainstate");
    rsdb->cf = rsdb_cf;

    int error_code = 0;
    if((error_code = xd_rsdb_conf(rsdb))) {
        return error_code;
    }

    // add rocksdb column
    xd_rsdb_column_conf(rsdb);
    if((error_code = xd_rsdb_open(rsdb, read_only))) {
        return error_code;
    }
    
    g_xdag_rsdb = rsdb;
    return XDAG_RSDB_OP_SUCCESS;
}

xd_rsdb_op_t xd_rsdb_init(xdag_time_t *time)
{
    char key[1] = {[0]=SETTING_CREATED};
    char* value = NULL;
    size_t vlen = 0;
    value = xd_rsdb_getkey(g_xdag_rsdb->cf->column_handle[0], key, 1, &vlen);
    if(!value) {
        xd_rsdb_put_setting(SETTING_VERSION, XDAG_VERSION, strlen(XDAG_VERSION));
        xd_rsdb_put_setting(SETTING_CREATED, "done", strlen("done"));
        memset(&top_main_chain, 0, sizeof(top_main_chain));
        memset(&pretop_main_chain, 0, sizeof(pretop_main_chain));
    } else if (strncmp("done", value, strlen("done")) == 0) {
        free(value);
        value = NULL;
        xd_rsdb_load(g_xdag_rsdb);
        char time_key[1] = {[0]=SETTING_CUR_TIME};
        if((value = xd_rsdb_getkey(g_xdag_rsdb->cf->column_handle[0], time_key, 1, &vlen))) {
            memcpy(time, value, sizeof(xdag_time_t));
            free(value);
        }
    }
    return XDAG_RSDB_OP_SUCCESS;
}

xd_rsdb_op_t xd_rsdb_load(xd_rsdb_t* db)
{
    xd_rsdb_get_stats();
    xd_rsdb_get_extstats();

    // clean wait blocks
    g_xdag_extstats.nwaitsync = 0;

    // read top_main_chain
    char top_hash_key[1] ={[0]=SETTING_TOP_MAIN};
    char *value = NULL;
    size_t vlen = 0;
    value = xd_rsdb_getkey(g_xdag_rsdb->cf->column_handle[0], top_hash_key, 1, &vlen);
    if(value)
    {
        memcpy(&top_main_chain, value, sizeof(top_main_chain));
        free(value);
        value = NULL;
    } else {
        memset(&top_main_chain, 0, sizeof(top_main_chain));
    }

    char pre_top_main_key[1] ={[0]=SETTING_PRE_TOP_MAIN};
    value = xd_rsdb_getkey(g_xdag_rsdb->cf->column_handle[0], pre_top_main_key, 1, &vlen);
    if(value)
    {
        memcpy(&pretop_main_chain, value, sizeof(pretop_main_chain));
        free(value);
        value = NULL;
    } else {
        memset(&pretop_main_chain, 0, sizeof(pretop_main_chain));
    }

    char ourfirst_hash_key[1] ={[0]=SETTING_OUR_FIRST_HASH};
    value = xd_rsdb_getkey(g_xdag_rsdb->cf->column_handle[0], ourfirst_hash_key, 1, &vlen);
    if(value)
    {
        memcpy(g_ourfirst_hash, value, sizeof(g_ourfirst_hash));
        free(value);
        value = NULL;
    }

    char ourlast_hash_key[1] ={[0]=SETTING_OUR_LAST_HASH};
    value = xd_rsdb_getkey(g_xdag_rsdb->cf->column_handle[0], ourlast_hash_key, 1, &vlen);
    if(value)
    {
        memcpy(g_ourlast_hash, value, sizeof(g_ourlast_hash));
        free(value);
        value = NULL;
    }

    char balance_key[1] ={[0]=SETTING_OUR_BALANCE};
    value = xd_rsdb_getkey(g_xdag_rsdb->cf->column_handle[0], balance_key, 1, &vlen);
    if(value)
    {
        memcpy(&g_balance, value, sizeof(g_balance));
        free(value);
        value = NULL;
    }

    return XDAG_RSDB_OP_SUCCESS;
}

xd_rsdb_op_t xd_rsdb_conf_check(xd_rsdb_t  *db)
{
    if(db == NULL ||
       !strlen(db->config->db_name) ||
       !strlen(db->config->db_path)
            )
    {
        return XDAG_RSDB_CONF_ERROR;
    }
    return  XDAG_RSDB_OP_SUCCESS;
}

xd_rsdb_op_t xd_rsdb_conf(xd_rsdb_t  *db)
{
    int error_code = 0;
    if( (error_code = xd_rsdb_conf_check(db)) ) {
        return error_code;
    }
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);  // get # of online cores

    rocksdb_options_t* options = rocksdb_options_create();
    rocksdb_writeoptions_t* write_options = rocksdb_writeoptions_create();
    rocksdb_readoptions_t* read_options = rocksdb_readoptions_create();
    rocksdb_filterpolicy_t *filter_policy = rocksdb_filterpolicy_create_bloom_full(10);


    // Optimize RocksDB. This is the easiest way to get RocksDB to perform well
    rocksdb_options_increase_parallelism(options, (int)(cpus));

    // set max open file limit
    rocksdb_options_set_max_open_files(options, -1);
    rocksdb_options_set_create_if_missing(options, 1);
    
    rocksdb_options_set_compaction_style(options, rocksdb_level_compaction);
    rocksdb_options_set_write_buffer_size(options, 67108864);// 64MB
    rocksdb_options_set_max_write_buffer_number(options, 3);
    rocksdb_options_set_target_file_size_base(options, 67108864);// 64MB
    rocksdb_options_set_max_background_compactions(options, (int)(cpus));
    rocksdb_options_set_level0_file_num_compaction_trigger(options, 8);
    rocksdb_options_set_level0_slowdown_writes_trigger(options, 17);
    rocksdb_options_set_level0_stop_writes_trigger(options, 24);
    rocksdb_options_set_num_levels(options, 4);
    rocksdb_options_set_max_bytes_for_level_base(options, 536870912);//512MB
    rocksdb_options_set_max_bytes_for_level_multiplier(options, 8);
    rocksdb_options_set_compression(options, rocksdb_lz4_compression);
    rocksdb_readoptions_set_verify_checksums(read_options, 0);

    rocksdb_block_based_table_options_t *block_based_table_options = rocksdb_block_based_options_create();
    rocksdb_block_based_options_set_filter_policy(block_based_table_options, filter_policy);
    rocksdb_options_set_block_based_table_factory(options, block_based_table_options);

    db->options = options;
    db->write_options = write_options;
    db->read_options = read_options;

    return XDAG_RSDB_OP_SUCCESS;
}

/**
 * Rocksdb Column for Xdag
 */
xd_rsdb_op_t xd_rsdb_column_conf(xd_rsdb_t  *db)
{
    rocksdb_options_t* cf_options = rocksdb_options_create();
    
    // [0] internal block(key="num", value="SETTING")
    db->cf->colum_name[0] = "SETTING";
    db->cf->column_option[0] = cf_options;

    // [1] internal block(key="hash", value="ORP_BLOCK")
    db->cf->colum_name[1] = "ORP_BLOCK";
    db->cf->column_option[1] = cf_options;

    // [2] internal block(key="hash", value="EXT_BLOCK")
    db->cf->colum_name[2] = "EXT_BLOCK";
    db->cf->column_option[2] = cf_options;

    // [3] internal block(key="hash", value="BLOCK_INTERNAL")
    db->cf->colum_name[3] = "BLOCK_INTERNAL";
    db->cf->column_option[3] = cf_options;

    // [4] internal block(key="hash", value="BLOCK_OUR")
    db->cf->colum_name[4] = "BLOCK_OUR";
    db->cf->column_option[4] = cf_options;

    // [5] internal block(key="hash", value="BLOCK_REMARK")
    db->cf->colum_name[5] = "BLOCK_REMARK";
    db->cf->column_option[5] = cf_options;

    // [6] internal block(key="hash", value="BLOCK_BACKREF")
    db->cf->colum_name[6] = "BLOCK_BACKREF";
    db->cf->column_option[6] = cf_options;

    // [7] internal block(key="hash", value="BLOCK_CACHE")
    db->cf->colum_name[7] = "BLOCK_CACHE";
    db->cf->column_option[7] = cf_options;

    // [8] internal block(key="hash", value="HEIGHT_BLOCK")
    db->cf->colum_name[8] = "HEIGHT_BLOCK";
    db->cf->column_option[8] = cf_options;
    
    // [9] default
    db->cf->colum_name[9] = "default";
    db->cf->column_option[9] = cf_options;

    return XDAG_RSDB_OP_SUCCESS;
}

xd_rsdb_op_t xd_rsdb_column_setup(xd_rsdb_t  *rsdb)
{
    char *errmsg = NULL;
    for(int i = 0; i < RSDB_MAX_COLUMN; i++) {
        rsdb->cf->column_handle[i] = rocksdb_create_column_family(rsdb->db, rsdb->cf->column_option[i], rsdb->cf->colum_name[i], &errmsg);
        RSDB_ASSERT_ERROR(errmsg);
    }
}

xd_rsdb_op_t xd_rsdb_open(xd_rsdb_t* rsdb, int read_only)
{
    char *errmsg = NULL;
    if(read_only) {
        rsdb->db = rocksdb_open_for_read_only_column_families(rsdb->options, rsdb->config->db_path, RSDB_MAX_COLUMN, (const char**)rsdb->cf->colum_name, (const rocksdb_options_t**)rsdb->cf->column_option, rsdb->cf->column_handle, (unsigned char)0, &errmsg);
    } else {
        rsdb->db = rocksdb_open(rsdb->options, rsdb->config->db_path, &errmsg);
//        rsdb->db = rocksdb_open_column_families(rsdb->options, rsdb->config->db_path, RSDB_MAX_COLUMN, (const char**)rsdb->cf->colum_name, (const rocksdb_options_t**)rsdb->cf->column_option, rsdb->cf->column_handle, &errmsg);
        xd_rsdb_column_setup(rsdb);
    }

    if(errmsg)
    {
		RSDB_LOG_FREE_ERRMSG(errmsg);
        return XDAG_RSDB_OPEN_ERROR;
    }
    return XDAG_RSDB_OP_SUCCESS;
}

xd_rsdb_op_t xd_rsdb_close(xd_rsdb_t* db)
{
    if(db->write_options) rocksdb_writeoptions_destroy(db->write_options);
    if(db->read_options) rocksdb_readoptions_destroy(db->read_options);
    if(db->options) rocksdb_options_destroy(db->options);
    if(db->db) rocksdb_close(db->db);
    return XDAG_RSDB_OP_SUCCESS;
}

// get
void* xd_rsdb_getkey(xd_rsdb_column_handle column_handle, const char* key, const size_t klen, size_t* vlen)
{
    char *errmsg = NULL;
    char *rocksdb_return_value = rocksdb_get_cf(g_xdag_rsdb->db, g_xdag_rsdb->read_options, column_handle, key, klen, vlen, &errmsg);

    if(errmsg)
    {
        if(rocksdb_return_value) {
            free(rocksdb_return_value);
        }
        rocksdb_readoptions_destroy(g_xdag_rsdb->read_options);
        rocksdb_close(g_xdag_rsdb->db);
		RSDB_LOG_FREE_ERRMSG(errmsg);
        return NULL;
    }
    return rocksdb_return_value;
}

xd_rsdb_op_t xd_rsdb_get_bi(xdag_hashlow_t hash, struct block_internal *bi)
{
	if (!hash || !bi) return XDAG_RSDB_NULL;
	size_t vlen = 0;
	char *value = xd_rsdb_getkey(g_xdag_rsdb->cf->column_handle[3], (const char*)hash, sizeof(xdag_hashlow_t), &vlen);
	if(!value)
		return XDAG_RSDB_NULL;
	
	if(vlen != sizeof(struct block_internal)){
		xdag_err("vlen is not math size of block_internal\n");
		free(value);
		return XDAG_RSDB_NULL;
	}
	memcpy(bi,value,vlen);
	free(value);

	return XDAG_RSDB_OP_SUCCESS;
}

xd_rsdb_op_t xd_rsdb_get_ournext(xdag_hashlow_t hash, xdag_hashlow_t next)
{
    if (!hash) return XDAG_RSDB_NULL;
    size_t vlen = 0;
    char *value = xd_rsdb_getkey(g_xdag_rsdb->cf->column_handle[4], (const char*)hash, sizeof(xdag_hashlow_t), &vlen);
    if(!value)
        return XDAG_RSDB_NULL;

    if(vlen != sizeof(xdag_hashlow_t)){
        xdag_err("vlen is not math size of xdag_hashlow_t\n");
        free(value);
        return XDAG_RSDB_NULL;
    }
    memcpy(next, value, vlen);
    free(value);

    return XDAG_RSDB_OP_SUCCESS;
}

static xd_rsdb_op_t xd_rsdb_get_xdblock(xdag_hashlow_t hash, xd_rsdb_column_handle column_handle, struct xdag_block *xb)
{
    if(!xb) return XDAG_RSDB_NULL;
    size_t vlen = 0;
    char *value = xd_rsdb_getkey(column_handle, (const char*)hash, sizeof(xdag_hashlow_t), &vlen);

    if(!value)
        return XDAG_RSDB_NULL;

    if(vlen != sizeof(struct xdag_block)){
        fprintf(stderr,"vlen is not math size of xdag_block\n");
        free(value);
        return XDAG_RSDB_NULL;
    }
    memcpy(xb, value, vlen);
    free(value);
    return XDAG_RSDB_OP_SUCCESS;
}

xd_rsdb_op_t xd_rsdb_get_orpblock(xdag_hashlow_t hash, struct xdag_block *xb)
{
    //HASH_ORP_BLOCK
    return xd_rsdb_get_xdblock(hash, g_xdag_rsdb->cf->column_handle[1], xb);
}

xd_rsdb_op_t xd_rsdb_get_extblock(xdag_hashlow_t hash, struct xdag_block *xb)
{
    //HASH_EXT_BLOCK
    return xd_rsdb_get_xdblock(hash, g_xdag_rsdb->cf->column_handle[2], xb);
}

xd_rsdb_op_t xd_rsdb_get_cacheblock(xdag_hashlow_t hash, struct xdag_block *xb)
{
    //HASH_BLOCK_CACHE
    return xd_rsdb_get_xdblock(hash, g_xdag_rsdb->cf->column_handle[7], xb);;
}

xd_rsdb_op_t xd_rsdb_get_stats(void)
{
    char key[1] = {[0] = SETTING_STATS};
    struct xdag_stats* p = NULL;
    size_t vlen = 0;
    p = xd_rsdb_getkey(g_xdag_rsdb->cf->column_handle[0], key, 1, &vlen);
    if(p) {
        memcpy(&g_xdag_stats, p, sizeof(g_xdag_stats));
        free(p);
    }
    return XDAG_RSDB_OP_SUCCESS;
}

xd_rsdb_op_t xd_rsdb_get_extstats()
{
    char key[1] = {[0] = SETTING_EXT_STATS};
    struct xdag_ext_stats* pexs = NULL;
    size_t vlen = 0;
    pexs = xd_rsdb_getkey(g_xdag_rsdb->cf->column_handle[0], key, 1, &vlen);
    if(pexs) {
        memcpy(&g_xdag_extstats, pexs, sizeof(g_xdag_extstats));
        free(pexs);
    }
    return XDAG_RSDB_OP_SUCCESS;
}

xd_rsdb_op_t xd_rsdb_get_remark(xdag_hashlow_t hash, xdag_remark_t remark)
{
    if(!remark) return XDAG_RSDB_NULL;
    size_t vlen = 0;
    char *value = xd_rsdb_getkey(g_xdag_rsdb->cf->column_handle[5], (const char*)hash, sizeof(xdag_hashlow_t), &vlen);

    if(!value)
        return XDAG_RSDB_NULL;

    if(vlen != sizeof(xdag_remark_t)){
        fprintf(stderr,"vlen is not math size of xdag_remark_t\n");
        free(value);
        return XDAG_RSDB_NULL;
    }
    memcpy(remark, value, vlen);
    free(value);
    return XDAG_RSDB_OP_SUCCESS;
}

xd_rsdb_op_t xd_rsdb_get_heighthash(uint64_t height, xdag_hashlow_t hash)
{
    if (!hash) return XDAG_RSDB_NULL;
    size_t vlen = 0;
    char *value = xd_rsdb_getkey(g_xdag_rsdb->cf->column_handle[8], (const char*)&height, sizeof(uint64_t), &vlen);
    if(!value)
        return XDAG_RSDB_NULL;

    if(vlen != sizeof(xdag_hashlow_t)){
        xdag_err("vlen is not math size of xdag_hashlow_t\n");
        free(value);
        return XDAG_RSDB_NULL;
    }
    memcpy(hash, value, vlen);
    free(value);

    return XDAG_RSDB_OP_SUCCESS;
}

// put
xd_rsdb_op_t xd_rsdb_putkey(xd_rsdb_column_handle column_handle, const char* key, size_t klen, const char* value, size_t vlen)
{
    char *errmsg = NULL;

    rocksdb_put_cf(g_xdag_rsdb->db,
                g_xdag_rsdb->write_options,
                column_handle,
                key, klen,
                value, vlen,
                &errmsg);
    if(errmsg)
    {
        rocksdb_writeoptions_destroy(g_xdag_rsdb->write_options);
        rocksdb_close(g_xdag_rsdb->db);
        RSDB_LOG_FREE_ERRMSG(errmsg);
        return XDAG_RSDB_PUT_ERROR;
    }
    return XDAG_RSDB_OP_SUCCESS;
}

xd_rsdb_op_t xd_rsdb_put_backref(xdag_hashlow_t backref, struct block_internal* bi)
{
    int retcode = 0;
    if(!backref) return XDAG_RSDB_NULL;
    char key[RSDB_KEY_LEN * 2 + 1] = {0};
    memcpy(key, backref, RSDB_KEY_LEN);
    key[RSDB_KEY_LEN] = '_';
    memcpy(key + RSDB_KEY_LEN + 1, bi->hash, RSDB_KEY_LEN);
    retcode = xd_rsdb_putkey(g_xdag_rsdb->cf->column_handle[6], (const char*)backref, RSDB_KEY_LEN * 2 + 1, (const char *) bi->hash, sizeof(xdag_hashlow_t));
    if(retcode) {
        return retcode;
    }
    return XDAG_RSDB_OP_SUCCESS;
}

xd_rsdb_op_t xd_rsdb_put_ournext(xdag_hashlow_t hash, xdag_hashlow_t next)
{
    if(!hash) return XDAG_RSDB_NULL;
    int retcode = xd_rsdb_putkey(g_xdag_rsdb->cf->column_handle[4], (const char*)hash, RSDB_KEY_LEN, (const char *)next, sizeof(xdag_hashlow_t));
    if(retcode) {
        return retcode;
    }
    return XDAG_RSDB_OP_SUCCESS;
}

xd_rsdb_op_t xd_rsdb_put_setting(xd_rsdb_key_t type, const char* value, size_t vlen)
{
    char key[1] = {[0] = type};
    xd_rsdb_putkey(g_xdag_rsdb->cf->column_handle[0], key, 1, value, vlen);
    return XDAG_RSDB_OP_SUCCESS;
}

static xd_rsdb_op_t xd_rsdb_put_xdblock(xdag_hashlow_t hash, const xd_rsdb_column_handle column_handle,struct xdag_block* xb)
{
    if(!hash) return XDAG_RSDB_NULL;
    if(!xb) return XDAG_RSDB_NULL;
    int retcode = xd_rsdb_putkey(column_handle, (const char*)hash, sizeof(xdag_hashlow_t), (const char*)xb, sizeof(struct xdag_block));
    if(retcode) {
        return retcode;
    }
    return XDAG_RSDB_OP_SUCCESS;
}

xd_rsdb_op_t xd_rsdb_put_bi(struct block_internal *bi)
{
    if(!bi) return XDAG_RSDB_NULL;
    int retcode = xd_rsdb_putkey(g_xdag_rsdb->cf->column_handle[3], (const char*)bi->hash, sizeof(xdag_hashlow_t), (const char *) bi, sizeof(struct block_internal));
    if(retcode) {
        return retcode;
    }
    return XDAG_RSDB_OP_SUCCESS;
}

xd_rsdb_op_t xd_rsdb_put_orpblock(xdag_hashlow_t hash, struct xdag_block* xb)
{
    return xd_rsdb_put_xdblock(hash, g_xdag_rsdb->cf->column_handle[1], xb);
}

xd_rsdb_op_t xd_rsdb_put_extblock(xdag_hashlow_t hash, struct xdag_block* xb)
{
    return xd_rsdb_put_xdblock(hash, g_xdag_rsdb->cf->column_handle[2], xb);
}

xd_rsdb_op_t xd_rsdb_put_cacheblock(xdag_hashlow_t hash, struct xdag_block* xb)
{
    return xd_rsdb_put_xdblock(hash, g_xdag_rsdb->cf->column_handle[7], xb);;
}

xd_rsdb_op_t xd_rsdb_put_stats(xdag_time_t time)
{
    xd_rsdb_op_t ret = XDAG_RSDB_NULL;
    if((ret = xd_rsdb_put_setting(SETTING_STATS, (const char *) &g_xdag_stats, sizeof(g_xdag_stats)))) {
        return ret;
    }
    ret = xd_rsdb_put_setting(SETTING_CUR_TIME, (const char*)&time, sizeof(xdag_time_t));
    return ret;
}

xd_rsdb_op_t xd_rsdb_put_extstats(void)
{
    return xd_rsdb_put_setting(SETTING_EXT_STATS, (const char *) &g_xdag_extstats, sizeof(g_xdag_extstats));;
}

xd_rsdb_op_t xd_rsdb_put_remark(struct block_internal* bi, xdag_remark_t strbuf)
{
    int retcode = 0;
    retcode = xd_rsdb_putkey(g_xdag_rsdb->cf->column_handle[5], (const char*)bi->hash, sizeof(xdag_hashlow_t), (const char *)strbuf, sizeof(xdag_remark_t));
    if(retcode) {
        return retcode;
    }
    return XDAG_RSDB_OP_SUCCESS;
}

xd_rsdb_op_t xd_rsdb_put_heighthash(uint64_t height, xdag_hashlow_t hash)
{
    int retcode = 0;
    if(!hash) return XDAG_RSDB_NULL;
    retcode = xd_rsdb_putkey(g_xdag_rsdb->cf->column_handle[8], (const char*)&height, sizeof(uint64_t), (const char *)hash, sizeof(xdag_hashlow_t));
    if(retcode) {
        return retcode;
    }
    return XDAG_RSDB_OP_SUCCESS;
}

// delete
xd_rsdb_op_t xd_rsdb_delkey(xd_rsdb_column_handle column_handle, const char* key, size_t klen)
{
    char *errmsg = NULL;
    rocksdb_delete_cf(g_xdag_rsdb->db, g_xdag_rsdb->write_options, column_handle, key, klen, &errmsg);
    if(errmsg)
    {
        rocksdb_writeoptions_destroy(g_xdag_rsdb->write_options);
        rocksdb_close(g_xdag_rsdb->db);
        RSDB_LOG_FREE_ERRMSG(errmsg);
        return XDAG_RSDB_DELETE_ERROR;
    }
    return XDAG_RSDB_OP_SUCCESS;
}

static xd_rsdb_op_t xd_rsdb_del(xdag_hashlow_t hash, const xd_rsdb_column_handle column_handle)
{
    if(!hash) return XDAG_RSDB_NULL;
    int retcode = 0;
    retcode = xd_rsdb_delkey(column_handle, (const char*)hash, RSDB_KEY_LEN);
    if(retcode) {
        return retcode;
    }
    return XDAG_RSDB_OP_SUCCESS;
}

xd_rsdb_op_t xd_rsdb_del_bi(xdag_hashlow_t hash)
{
    return xd_rsdb_del(hash, g_xdag_rsdb->cf->column_handle[3]);
}

xd_rsdb_op_t xd_rsdb_del_orpblock(xdag_hashlow_t hash)
{
    return xd_rsdb_del(hash, g_xdag_rsdb->cf->column_handle[1]);
}

xd_rsdb_op_t xd_rsdb_del_extblock(xdag_hashlow_t hash)
{
    return xd_rsdb_del(hash, g_xdag_rsdb->cf->column_handle[2]);
}

xd_rsdb_op_t xd_rsdb_del_heighthash(uint64_t height)
{
    int retcode = xd_rsdb_delkey(g_xdag_rsdb->cf->column_handle[8], (const char*)&height, sizeof(uint64_t));
    if(retcode) {
        return retcode;
    }
    return XDAG_RSDB_OP_SUCCESS;
}

xd_rsdb_op_t xd_rsdb_merge_bi(struct block_internal *bi)
{
    return xd_rsdb_put_bi(bi);
}
