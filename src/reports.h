#ifndef HLEDGERX_REPORTS_H
#define HLEDGERX_REPORTS_H

#include <stddef.h>
#include <stdio.h>

#include "ledger.h"

typedef enum {
    REPORT_PERIOD_NONE = 0,
    REPORT_PERIOD_DAILY = 1,
    REPORT_PERIOD_MONTHLY = 2,
    REPORT_PERIOD_QUARTERLY = 3,
    REPORT_PERIOD_YEARLY = 4
} ReportPeriod;

typedef enum {
    REPORT_OUTPUT_TEXT = 0,
    REPORT_OUTPUT_CSV = 1
} ReportOutput;

typedef struct {
    const char *account_prefix;
    const char *begin_date;
    const char *end_date;
    int depth;
    int tree;
    ReportPeriod period;
    ReportOutput output;
} ReportQuery;

int report_print_journal(const Ledger *ledger, const ReportQuery *query, FILE *out, char *err, size_t err_size);
int report_balance(const Ledger *ledger, const ReportQuery *query, FILE *out, char *err, size_t err_size);
int report_register(const Ledger *ledger, const ReportQuery *query, FILE *out, char *err, size_t err_size);
int report_accounts(const Ledger *ledger, const ReportQuery *query, FILE *out, char *err, size_t err_size);

#endif
