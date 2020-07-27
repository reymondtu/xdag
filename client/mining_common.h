#ifndef XDAG_MINING_COMMON_H
#define XDAG_MINING_COMMON_H

#include <pthread.h>
#include "block.h"
#ifdef _WIN32
#define poll WSAPoll
#else
#include <poll.h>
#endif

#define DATA_SIZE          (sizeof(struct xdag_field) / sizeof(uint32_t))
#define BLOCK_HEADER_WORD  0x3fca9e2bu
#define RX_POW_HEADER_WORD 0xf46b9854u  /* word of randomx work proof*/

struct xdag_pool_task {
	struct xdag_field task[2], lastfield, minhash, nonce;
	xdag_frame_t task_time;
	void *ctx0, *ctx;
};

typedef struct tag_rx_pool_task{
	atomic_bool hashed;           //task is first hashed with random nonce
	atomic_int discards;          //task discard count if task discard count >= mining threads,task will be discard
	uint64_t seqno;               //task seqno
	uint64_t discard_flag;        //discard flags
	uint64_t nonce0;              //first random nonce
	xdag_hash_t prehash;          //pre hash
	xdag_hashlow_t seed;          //seed used to randomx
	xdag_hash_t lastfield;        //lastfield commit to pool
	xdag_hash_t minhash;          //minhash for mine compare
	xdag_frame_t task_time;       //task create time
} rx_pool_task;

#ifdef __cplusplus
extern "C" {
#endif
	
extern struct xdag_pool_task g_xdag_pool_task[2];
extern uint64_t g_xdag_pool_task_index; /* global variables are instantiated with 0 */
extern uint64_t g_xdag_rx_task_seq;     /* global latest rx task seq instantiated with 0 */

/* poiter to mutex for optimal share  */
extern void *g_ptr_share_mutex;

extern const char *g_miner_address;

extern pthread_mutex_t g_share_mutex;

extern struct dfslib_crypt *g_crypt;

/* initialization of the pool (g_xdag_type = XDAG_WALLET) or connecting the miner to pool (g_xdag_type = XDAG_WALLET; pool_arg - pool parameters ip:port[:CFG];
miner_addr - address of the miner, if specified */
extern int xdag_initialize_mining(const char *pool_arg, const char *miner_address);

//function sets minimal share for the task
extern void xdag_set_min_share(struct xdag_pool_task *task, xdag_hash_t last, xdag_hash_t hash);

extern uint64_t get_rx_seed(xdag_hashlow_t rx_seed);

#ifdef __cplusplus
};
#endif
		
#endif
