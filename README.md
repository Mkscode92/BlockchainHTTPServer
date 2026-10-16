# HTTPServerC

A multithreaded HTTP/1.1 server written from scratch in C (POSIX sockets + pthreads), with a built-in
proof-of-work blockchain ledger and a transaction risk-analytics endpoint.

## Build & run (Linux / WSL)

```sh
gcc -Wall -pthread -o server server.c blockchain.c -lm
./server            # listens on :8080
```

`GET /` (or any non-API path) serves `index.html` from the working directory, like before.

## Blockchain API

| Method | Path            | What it does |
|--------|-----------------|--------------|
| POST   | `/transactions` | Queue a transfer: `sender`, `recipient`, `amount` (form body or query string) |
| GET    | `/pending`      | Transactions waiting to be mined |
| POST   | `/mine`         | Mine up to 16 pending transactions into a new block (SHA-256 proof of work) |
| GET    | `/chain`        | Full chain as JSON |
| GET    | `/validate`     | Re-hash every block and check its proof of work and `prev_hash` links |
| GET    | `/risk`         | Risk report over confirmed transactions (see below) |
| POST   | `/tamper`       | Demo only: edit a mined block's first transaction (`block`, `amount`) without re-mining |

### Risk report (`/risk`)

- **Summary stats**: transaction count, total volume, mean, standard deviation, max, unique addresses
- **Large transfers**: any single transfer ≥ 1,000, with its z-score
- **High-velocity addresses**: addresses that sent more than 5 transactions
- **Concentration**: the top sender's share of total volume, flagged above 50%
- **Address flows**: sent, received and net for each address
- **Risk level**: `LOW` / `MEDIUM` / `HIGH` based on how many flags fired, or `CRITICAL` if the chain fails validation

You can change the thresholds and mining difficulty in [blockchain.h](blockchain.h).

## Demo

```sh
curl -X POST localhost:8080/transactions -d "sender=alice&recipient=bob&amount=25.5"
curl -X POST localhost:8080/transactions -d "sender=whale&recipient=exchange&amount=50000"
curl -X POST localhost:8080/mine
curl localhost:8080/chain
curl localhost:8080/risk          # whale transfer flagged as large + concentrated

curl -X POST localhost:8080/tamper -d "block=1&amount=1"
curl localhost:8080/validate      # {"valid":false,"invalid_block":1,...}
curl localhost:8080/risk          # risk_level: CRITICAL
```

## Implementation notes

- SHA-256 is implemented by hand (FIPS 180-4) with no OpenSSL dependency. It matches `sha256sum` on the
  standard test vectors and on inputs around the padding boundaries.
- Chain state is shared by every client thread and guarded by a single mutex.
- Addresses may only contain `[A-Za-z0-9_.-]`, so values can go into JSON without escaping.
