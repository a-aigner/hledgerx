#ifndef HLEDGERX_LEDGER_H
#define HLEDGERX_LEDGER_H

#include <stddef.h>

typedef struct {
    char account[128];
    double amount;
    char commodity[16];
    int has_amount;
} Posting;

typedef struct {
    char date[11];
    char description[256];
    Posting *postings;
    size_t postings_count;
    size_t postings_capacity;
} Transaction;

typedef struct {
    Transaction *transactions;
    size_t count;
    size_t capacity;
    char default_commodity[16];
} Ledger;

typedef struct {
    char name[128];
    double amount;
    char commodity[16];
} AccountBalance;

typedef struct {
    int strict;
} LedgerLoadOptions;

void transaction_init(Transaction *txn);
void transaction_free(Transaction *txn);
int transaction_add_posting(Transaction *txn, const Posting *posting, char *err, size_t err_size);

void ledger_init(Ledger *ledger);
void ledger_free(Ledger *ledger);

int ledger_load_journal(Ledger *ledger, const char *path, char *err, size_t err_size);
int ledger_load_journal_ex(Ledger *ledger, const char *path, const LedgerLoadOptions *options, char *err, size_t err_size);
int ledger_add_transaction_copy(Ledger *ledger, const Transaction *txn, char *err, size_t err_size);
int ledger_append_transaction(const char *path, const Transaction *txn, char *err, size_t err_size);

int ledger_compute_balances(const Ledger *ledger, AccountBalance **out_balances, size_t *out_count, char *err, size_t err_size);
void ledger_free_balances(AccountBalance *balances);

#endif
