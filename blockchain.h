#ifndef BLOCKCHAIN_H
#define BLOCKCHAIN_H

#include <stddef.h>
#include <time.h>

// a tiny proof-of-work ledger that lives in memory while the server runs
// every block stores a batch of transactions + the hash of the block before it,
// so changing any old block breaks every link after it (that's what /validate checks)

#define MAX_TX_PER_BLOCK 16
#define MAX_PENDING      64
#define ADDR_LEN         32
#define HASH_HEX_LEN     65      // 64 hex chars + '\0'
#define DIFFICULTY       4       // number of leading '0' hex chars a valid hash needs

// risk thresholds used by /risk
#define LARGE_TX_THRESHOLD   1000.0   // single transfer this big gets flagged
#define VELOCITY_THRESHOLD   5        // more sends than this from one address gets flagged
#define CONCENTRATION_LIMIT  0.50     // one address moving >50% of volume gets flagged

typedef struct {
    char sender[ADDR_LEN];
    char recipient[ADDR_LEN];
    double amount;
} Transaction;

typedef struct {
    int index;
    time_t timestamp;
    Transaction txs[MAX_TX_PER_BLOCK];
    int tx_count;
    char prev_hash[HASH_HEX_LEN];
    unsigned long nonce;
    char hash[HASH_HEX_LEN];
} Block;

void chain_init(void);
void chain_free(void);

// returns 0 on success, -1 if the pending pool is full or input is bad
int chain_add_transaction(const char *sender, const char *recipient, double amount);

// mines pending transactions into a new block, returns new block index or -1 if nothing to mine
int chain_mine(void);

// each of these writes a JSON document into a malloc'd string the caller must free
char *chain_to_json(void);
char *chain_pending_json(void);
char *chain_validate_json(void);
char *chain_risk_json(void);

// demo helper: edits a transaction inside an already-mined block without re-mining,
// so /validate can show the tampering being caught. returns 0 on success
int chain_tamper(int block_index, double new_amount);

void sha256_hex(const unsigned char *data, size_t len, char out[HASH_HEX_LEN]);

#endif
