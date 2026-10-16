#include "blockchain.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <math.h>
#include <pthread.h>

/* ---------------------------------------------------------------------------
   SHA-256 (FIPS 180-4), written by hand so the server has no OpenSSL dependency
   --------------------------------------------------------------------------- */

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(uint32_t h[8], const unsigned char chunk[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)chunk[i * 4] << 24) | ((uint32_t)chunk[i * 4 + 1] << 16) |
               ((uint32_t)chunk[i * 4 + 2] << 8) | (uint32_t)chunk[i * 4 + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROTR(w[i - 15], 7) ^ ROTR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROTR(w[i - 2], 17) ^ ROTR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ROTR(e, 6) ^ ROTR(e, 11) ^ ROTR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = hh + S1 + ch + K[i] + w[i];
        uint32_t S0 = ROTR(a, 2) ^ ROTR(a, 13) ^ ROTR(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        hh = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void sha256_hex(const unsigned char *data, size_t len, char out[HASH_HEX_LEN]) {
    uint32_t h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    size_t full = len / 64;
    for (size_t i = 0; i < full; i++) sha256_block(h, data + i * 64);

    // pad: leftover bytes, a 0x80 byte, zeros, then the bit length as a 64-bit big endian int
    unsigned char tail[128] = {0};
    size_t rem = len % 64;
    memcpy(tail, data + full * 64, rem);
    tail[rem] = 0x80;
    size_t tail_len = (rem < 56) ? 64 : 128;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) tail[tail_len - 1 - i] = (unsigned char)(bits >> (i * 8));
    sha256_block(h, tail);
    if (tail_len == 128) sha256_block(h, tail + 64);

    for (int i = 0; i < 8; i++) sprintf(out + i * 8, "%08x", h[i]);
    out[64] = '\0';
}

/* ---------------------------------------------------------------------------
   small growable string buffer for building JSON responses
   --------------------------------------------------------------------------- */

typedef struct {
    char *data;
    size_t len, cap;
} Buf;

static void buf_printf(Buf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    if (b->len + need + 1 > b->cap) {
        while (b->len + need + 1 > b->cap) b->cap = b->cap ? b->cap * 2 : 1024;
        b->data = realloc(b->data, b->cap);
    }
    va_start(ap, fmt);
    vsnprintf(b->data + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    b->len += need;
}

/* ---------------------------------------------------------------------------
   chain state, shared by every client thread so it's guarded by one mutex
   --------------------------------------------------------------------------- */

static Block *chain = NULL;
static int chain_len = 0, chain_cap = 0;
static Transaction pending[MAX_PENDING];
static int pending_count = 0;
static pthread_mutex_t chain_lock = PTHREAD_MUTEX_INITIALIZER;

static void block_hash(const Block *blk, unsigned long nonce, char out[HASH_HEX_LEN]) {
    char input[256 + MAX_TX_PER_BLOCK * (ADDR_LEN * 2 + 40)];
    int n = snprintf(input, sizeof input, "%d|%ld|%s|%lu|",
                     blk->index, (long)blk->timestamp, blk->prev_hash, nonce);
    for (int i = 0; i < blk->tx_count; i++) {
        n += snprintf(input + n, sizeof input - n, "%s>%s:%.8f;",
                      blk->txs[i].sender, blk->txs[i].recipient, blk->txs[i].amount);
    }
    sha256_hex((const unsigned char *)input, n, out);
}

static int meets_difficulty(const char *hash) {
    for (int i = 0; i < DIFFICULTY; i++)
        if (hash[i] != '0') return 0;
    return 1;
}

// proof of work: keep bumping the nonce until the hash starts with DIFFICULTY zeros
static void mine_block(Block *blk) {
    unsigned long nonce = 0;
    char hash[HASH_HEX_LEN];
    do {
        block_hash(blk, nonce, hash);
        nonce++;
    } while (!meets_difficulty(hash));
    blk->nonce = nonce - 1;
    strcpy(blk->hash, hash);
}

static void append_block(Block *blk) {
    if (chain_len == chain_cap) {
        chain_cap = chain_cap ? chain_cap * 2 : 8;
        chain = realloc(chain, chain_cap * sizeof(Block));
    }
    chain[chain_len++] = *blk;
}

void chain_init(void) {
    Block genesis;
    memset(&genesis, 0, sizeof genesis);
    genesis.index = 0;
    genesis.timestamp = time(NULL);
    memset(genesis.prev_hash, '0', 64);
    genesis.prev_hash[64] = '\0';
    mine_block(&genesis);

    pthread_mutex_lock(&chain_lock);
    append_block(&genesis);
    pthread_mutex_unlock(&chain_lock);
}

void chain_free(void) {
    pthread_mutex_lock(&chain_lock);
    free(chain);
    chain = NULL;
    chain_len = chain_cap = 0;
    pthread_mutex_unlock(&chain_lock);
}

// addresses are restricted to [A-Za-z0-9_-.] so they can go straight into JSON unescaped
static int valid_address(const char *s) {
    size_t n = strlen(s);
    if (n == 0 || n >= ADDR_LEN) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                 c == '_' || c == '-' || c == '.';
        if (!ok) return 0;
    }
    return 1;
}

int chain_add_transaction(const char *sender, const char *recipient, double amount) {
    if (!valid_address(sender) || !valid_address(recipient)) return -1;
    if (!(amount > 0) || !isfinite(amount)) return -1;

    pthread_mutex_lock(&chain_lock);
    if (pending_count >= MAX_PENDING) {
        pthread_mutex_unlock(&chain_lock);
        return -1;
    }
    Transaction *tx = &pending[pending_count++];
    strcpy(tx->sender, sender);
    strcpy(tx->recipient, recipient);
    tx->amount = amount;
    pthread_mutex_unlock(&chain_lock);
    return 0;
}

int chain_mine(void) {
    pthread_mutex_lock(&chain_lock);
    if (pending_count == 0) {
        pthread_mutex_unlock(&chain_lock);
        return -1;
    }

    Block blk;
    memset(&blk, 0, sizeof blk);
    blk.index = chain_len;
    blk.timestamp = time(NULL);
    strcpy(blk.prev_hash, chain[chain_len - 1].hash);
    blk.tx_count = pending_count < MAX_TX_PER_BLOCK ? pending_count : MAX_TX_PER_BLOCK;
    memcpy(blk.txs, pending, blk.tx_count * sizeof(Transaction));

    // anything that didn't fit stays in the pool for the next block
    memmove(pending, pending + blk.tx_count, (pending_count - blk.tx_count) * sizeof(Transaction));
    pending_count -= blk.tx_count;

    // difficulty 4 is ~65k hashes on average, a few ms, so mining under the lock is fine
    mine_block(&blk);
    append_block(&blk);
    int idx = blk.index;
    pthread_mutex_unlock(&chain_lock);
    return idx;
}

int chain_tamper(int block_index, double new_amount) {
    pthread_mutex_lock(&chain_lock);
    if (block_index < 1 || block_index >= chain_len || chain[block_index].tx_count == 0) {
        pthread_mutex_unlock(&chain_lock);
        return -1;
    }
    chain[block_index].txs[0].amount = new_amount;
    pthread_mutex_unlock(&chain_lock);
    return 0;
}

/* ---------------------------------------------------------------------------
   JSON views
   --------------------------------------------------------------------------- */

static void tx_json(Buf *b, const Transaction *tx) {
    buf_printf(b, "{\"sender\":\"%s\",\"recipient\":\"%s\",\"amount\":%.8f}",
               tx->sender, tx->recipient, tx->amount);
}

char *chain_to_json(void) {
    Buf b = {0};
    pthread_mutex_lock(&chain_lock);
    buf_printf(&b, "{\"length\":%d,\"difficulty\":%d,\"blocks\":[", chain_len, DIFFICULTY);
    for (int i = 0; i < chain_len; i++) {
        Block *blk = &chain[i];
        buf_printf(&b, "%s{\"index\":%d,\"timestamp\":%ld,\"prev_hash\":\"%s\",\"nonce\":%lu,"
                       "\"hash\":\"%s\",\"transactions\":[",
                   i ? "," : "", blk->index, (long)blk->timestamp, blk->prev_hash, blk->nonce, blk->hash);
        for (int t = 0; t < blk->tx_count; t++) {
            if (t) buf_printf(&b, ",");
            tx_json(&b, &blk->txs[t]);
        }
        buf_printf(&b, "]}");
    }
    buf_printf(&b, "]}");
    pthread_mutex_unlock(&chain_lock);
    return b.data;
}

char *chain_pending_json(void) {
    Buf b = {0};
    pthread_mutex_lock(&chain_lock);
    buf_printf(&b, "{\"count\":%d,\"transactions\":[", pending_count);
    for (int i = 0; i < pending_count; i++) {
        if (i) buf_printf(&b, ",");
        tx_json(&b, &pending[i]);
    }
    buf_printf(&b, "]}");
    pthread_mutex_unlock(&chain_lock);
    return b.data;
}

// caller must hold chain_lock. returns index of first bad block or -1 if the chain is intact
static int find_invalid_block(const char **reason) {
    char hash[HASH_HEX_LEN];
    for (int i = 0; i < chain_len; i++) {
        Block *blk = &chain[i];
        block_hash(blk, blk->nonce, hash);
        if (strcmp(hash, blk->hash) != 0) {
            *reason = "stored hash does not match block contents";
            return i;
        }
        if (!meets_difficulty(blk->hash)) {
            *reason = "hash does not satisfy proof-of-work difficulty";
            return i;
        }
        if (i > 0 && strcmp(blk->prev_hash, chain[i - 1].hash) != 0) {
            *reason = "prev_hash does not link to previous block";
            return i;
        }
    }
    return -1;
}

char *chain_validate_json(void) {
    Buf b = {0};
    const char *reason = NULL;
    pthread_mutex_lock(&chain_lock);
    int bad = find_invalid_block(&reason);
    if (bad < 0)
        buf_printf(&b, "{\"valid\":true,\"blocks_checked\":%d}", chain_len);
    else
        buf_printf(&b, "{\"valid\":false,\"blocks_checked\":%d,\"invalid_block\":%d,\"reason\":\"%s\"}",
                   chain_len, bad, reason);
    pthread_mutex_unlock(&chain_lock);
    return b.data;
}

/* ---------------------------------------------------------------------------
   risk analytics over every confirmed (mined) transaction
   --------------------------------------------------------------------------- */

#define MAX_ADDRS 256

typedef struct {
    char addr[ADDR_LEN];
    int sent_count;
    double sent, received;
} AddrStats;

static AddrStats *addr_lookup(AddrStats *tbl, int *n, const char *addr) {
    for (int i = 0; i < *n; i++)
        if (strcmp(tbl[i].addr, addr) == 0) return &tbl[i];
    if (*n >= MAX_ADDRS) return NULL;
    AddrStats *s = &tbl[(*n)++];
    memset(s, 0, sizeof *s);
    strcpy(s->addr, addr);
    return s;
}

char *chain_risk_json(void) {
    AddrStats *tbl = calloc(MAX_ADDRS, sizeof(AddrStats));
    int n_addrs = 0;
    Buf b = {0};

    pthread_mutex_lock(&chain_lock);

    // pass 1: totals, mean, per-address flows
    int tx_total = 0;
    double volume = 0, max_amt = 0;
    for (int i = 1; i < chain_len; i++) {
        for (int t = 0; t < chain[i].tx_count; t++) {
            Transaction *tx = &chain[i].txs[t];
            tx_total++;
            volume += tx->amount;
            if (tx->amount > max_amt) max_amt = tx->amount;
            AddrStats *s = addr_lookup(tbl, &n_addrs, tx->sender);
            if (s) { s->sent_count++; s->sent += tx->amount; }
            AddrStats *r = addr_lookup(tbl, &n_addrs, tx->recipient);
            if (r) r->received += tx->amount;
        }
    }
    double mean = tx_total ? volume / tx_total : 0;

    // pass 2: standard deviation of transfer size
    double var = 0;
    for (int i = 1; i < chain_len; i++)
        for (int t = 0; t < chain[i].tx_count; t++) {
            double d = chain[i].txs[t].amount - mean;
            var += d * d;
        }
    double stddev = tx_total ? sqrt(var / tx_total) : 0;

    const char *reason = NULL;
    int bad_block = find_invalid_block(&reason);
    int flags = 0;

    buf_printf(&b, "{\"summary\":{\"confirmed_transactions\":%d,\"total_volume\":%.8f,"
                   "\"mean_amount\":%.8f,\"stddev_amount\":%.8f,\"max_amount\":%.8f,"
                   "\"unique_addresses\":%d,\"pending_transactions\":%d,\"chain_valid\":%s},",
               tx_total, volume, mean, stddev, max_amt, n_addrs, pending_count,
               bad_block < 0 ? "true" : "false");

    // large single transfers
    buf_printf(&b, "\"large_transfers\":[");
    int first = 1;
    for (int i = 1; i < chain_len; i++)
        for (int t = 0; t < chain[i].tx_count; t++) {
            Transaction *tx = &chain[i].txs[t];
            if (tx->amount < LARGE_TX_THRESHOLD) continue;
            double z = stddev > 0 ? (tx->amount - mean) / stddev : 0;
            buf_printf(&b, "%s{\"block\":%d,\"sender\":\"%s\",\"recipient\":\"%s\","
                           "\"amount\":%.8f,\"z_score\":%.2f}",
                       first ? "" : ",", i, tx->sender, tx->recipient, tx->amount, z);
            first = 0;
            flags++;
        }

    // addresses sending unusually often
    buf_printf(&b, "],\"high_velocity_addresses\":[");
    first = 1;
    for (int i = 0; i < n_addrs; i++) {
        if (tbl[i].sent_count <= VELOCITY_THRESHOLD) continue;
        buf_printf(&b, "%s{\"address\":\"%s\",\"sent_count\":%d}",
                   first ? "" : ",", tbl[i].addr, tbl[i].sent_count);
        first = 0;
        flags++;
    }

    // concentration: biggest sender's share of all outgoing volume
    int top = -1;
    for (int i = 0; i < n_addrs; i++)
        if (top < 0 || tbl[i].sent > tbl[top].sent) top = i;
    double top_share = (top >= 0 && volume > 0) ? tbl[top].sent / volume : 0;
    int concentrated = top_share > CONCENTRATION_LIMIT && tx_total >= 3;
    if (concentrated) flags++;
    buf_printf(&b, "],\"concentration\":{\"top_sender\":\"%s\",\"share_of_volume\":%.4f,\"flagged\":%s},",
               top >= 0 ? tbl[top].addr : "", top_share, concentrated ? "true" : "false");

    // net flow per address (received - sent), handy for a balance-style view
    buf_printf(&b, "\"address_flows\":[");
    for (int i = 0; i < n_addrs; i++) {
        buf_printf(&b, "%s{\"address\":\"%s\",\"sent\":%.8f,\"received\":%.8f,\"net\":%.8f}",
                   i ? "," : "", tbl[i].addr, tbl[i].sent, tbl[i].received,
                   tbl[i].received - tbl[i].sent);
    }

    // a broken chain trumps everything else
    const char *level = bad_block >= 0 ? "CRITICAL" : flags >= 3 ? "HIGH" : flags >= 1 ? "MEDIUM" : "LOW";
    buf_printf(&b, "],\"risk_flags\":%d,\"risk_level\":\"%s\"}", flags, level);

    pthread_mutex_unlock(&chain_lock);
    free(tbl);
    return b.data;
}
