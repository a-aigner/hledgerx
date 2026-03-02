#include "reports.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char commodity[16];
    double amount;
} CommodityTotal;

typedef struct {
    char account[128];
    char commodity[16];
    double amount;
} BalanceRow;

typedef struct {
    char name[128];
} AccountName;

typedef struct {
    char period[11];
    char account[128];
    char commodity[16];
    double amount;
} PeriodBalanceRow;

typedef struct {
    char period[11];
    char commodity[16];
    double amount;
} PeriodCommodityTotal;

typedef struct {
    char period[11];
    char date[11];
    char description[256];
    char account[128];
    char commodity[16];
    double change;
    size_t ordinal;
} RegisterEntry;

static void set_error(char *err, size_t err_size, const char *fmt, ...) {
    if (err == NULL || err_size == 0 || fmt == NULL) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    vsnprintf(err, err_size, fmt, args);
    va_end(args);
}

static const ReportQuery *effective_query(const ReportQuery *query) {
    static const ReportQuery empty = {0};
    return query == NULL ? &empty : query;
}

static int starts_with(const char *value, const char *prefix) {
    if (value == NULL) {
        return 0;
    }

    if (prefix == NULL || prefix[0] == '\0') {
        return 1;
    }

    size_t prefix_len = strlen(prefix);
    return strncmp(value, prefix, prefix_len) == 0;
}

static int transaction_matches_date(const Transaction *txn, const ReportQuery *query) {
    if (txn == NULL) {
        return 0;
    }

    const ReportQuery *q = effective_query(query);
    if (q->begin_date != NULL && q->begin_date[0] != '\0' && strcmp(txn->date, q->begin_date) < 0) {
        return 0;
    }
    if (q->end_date != NULL && q->end_date[0] != '\0' && strcmp(txn->date, q->end_date) > 0) {
        return 0;
    }
    return 1;
}

static int posting_matches_account(const Posting *posting, const ReportQuery *query) {
    if (posting == NULL) {
        return 0;
    }

    const ReportQuery *q = effective_query(query);
    return starts_with(posting->account, q->account_prefix);
}

static void resolve_commodity(const Ledger *ledger, const Posting *posting, char out[16]) {
    memset(out, 0, 16);
    if (posting->commodity[0] != '\0') {
        strncpy(out, posting->commodity, 15);
    } else if (ledger->default_commodity[0] != '\0') {
        strncpy(out, ledger->default_commodity, 15);
    }
}

static void account_with_depth(const char *account, const ReportQuery *query, char out[128]) {
    const ReportQuery *q = effective_query(query);
    int depth = q->depth;

    memset(out, 0, 128);
    if (account == NULL || account[0] == '\0') {
        return;
    }

    if (depth <= 0) {
        strncpy(out, account, 127);
        return;
    }

    int seen_segments = 1;
    size_t len = 0;
    while (account[len] != '\0') {
        if (account[len] == ':') {
            if (seen_segments >= depth) {
                break;
            }
            seen_segments++;
        }
        len++;
    }

    if (len >= 128) {
        len = 127;
    }
    memcpy(out, account, len);
    out[len] = '\0';
}

static int account_level(const char *account) {
    int level = 0;
    for (size_t i = 0; account[i] != '\0'; i++) {
        if (account[i] == ':') {
            level++;
        }
    }
    return level;
}

static const char *account_leaf(const char *account) {
    const char *leaf = strrchr(account, ':');
    return leaf == NULL ? account : leaf + 1;
}

static void format_account_for_output(const char *account, const ReportQuery *query, char out[128]) {
    const ReportQuery *q = effective_query(query);
    if (account == NULL) {
        out[0] = '\0';
        return;
    }

    if (!q->tree) {
        strncpy(out, account, 127);
        out[127] = '\0';
        return;
    }

    int indent = account_level(account) * 2;
    const char *leaf = account_leaf(account);
    if (indent > 80) {
        indent = 80;
    }

    int written = snprintf(out, 128, "%*s%s", indent, "", leaf);
    if (written < 0 || written >= 128) {
        out[127] = '\0';
    }
}

static int has_periodic_mode(const ReportQuery *query) {
    const ReportQuery *q = effective_query(query);
    return q->period != REPORT_PERIOD_NONE;
}

static int is_csv_output(const ReportQuery *query) {
    const ReportQuery *q = effective_query(query);
    return q->output == REPORT_OUTPUT_CSV;
}

static void period_key_for_date(const char *date, const ReportQuery *query, char out[11]) {
    const ReportQuery *q = effective_query(query);
    memset(out, 0, 11);

    if (date == NULL || date[0] == '\0' || q->period == REPORT_PERIOD_NONE) {
        return;
    }

    if (q->period == REPORT_PERIOD_DAILY) {
        strncpy(out, date, 10);
        out[10] = '\0';
        return;
    }

    if (q->period == REPORT_PERIOD_MONTHLY) {
        strncpy(out, date, 7);
        out[7] = '\0';
        return;
    }

    if (q->period == REPORT_PERIOD_QUARTERLY) {
        int month = 0;
        if (strlen(date) >= 7) {
            month = (date[5] - '0') * 10 + (date[6] - '0');
        }
        int quarter = 1;
        if (month >= 1 && month <= 12) {
            quarter = ((month - 1) / 3) + 1;
        }
        snprintf(out, 11, "%.4s-Q%d", date, quarter);
        return;
    }

    if (q->period == REPORT_PERIOD_YEARLY) {
        strncpy(out, date, 4);
        out[4] = '\0';
    }
}

static int csv_write_field(FILE *out, const char *value) {
    const char *s = value == NULL ? "" : value;
    int needs_quotes = 0;
    for (size_t i = 0; s[i] != '\0'; i++) {
        if (s[i] == ',' || s[i] == '"' || s[i] == '\n' || s[i] == '\r') {
            needs_quotes = 1;
            break;
        }
    }

    if (!needs_quotes) {
        return fputs(s, out) != EOF;
    }

    if (fputc('"', out) == EOF) {
        return 0;
    }
    for (size_t i = 0; s[i] != '\0'; i++) {
        if (s[i] == '"') {
            if (fputc('"', out) == EOF || fputc('"', out) == EOF) {
                return 0;
            }
        } else if (fputc(s[i], out) == EOF) {
            return 0;
        }
    }
    return fputc('"', out) != EOF;
}

static int csv_write_balance_row(FILE *out, const char *period, const char *account,
                                 double amount, const char *commodity, const char *row_type) {
    char amount_buf[64] = {0};
    snprintf(amount_buf, sizeof(amount_buf), "%.2f", amount);

    if (period != NULL) {
        if (!csv_write_field(out, period) || fputc(',', out) == EOF) {
            return 0;
        }
    }
    if (!csv_write_field(out, account) || fputc(',', out) == EOF) {
        return 0;
    }
    if (!csv_write_field(out, amount_buf) || fputc(',', out) == EOF) {
        return 0;
    }
    if (!csv_write_field(out, commodity) || fputc(',', out) == EOF) {
        return 0;
    }
    if (!csv_write_field(out, row_type) || fputc('\n', out) == EOF) {
        return 0;
    }
    return 1;
}

static int csv_write_register_row(FILE *out, const char *period, const char *date, const char *description,
                                  const char *account, double change, double balance, const char *commodity) {
    char change_buf[64] = {0};
    char balance_buf[64] = {0};
    snprintf(change_buf, sizeof(change_buf), "%.2f", change);
    snprintf(balance_buf, sizeof(balance_buf), "%.2f", balance);

    if (period != NULL) {
        if (!csv_write_field(out, period) || fputc(',', out) == EOF) {
            return 0;
        }
    }
    if (!csv_write_field(out, date) || fputc(',', out) == EOF) {
        return 0;
    }
    if (!csv_write_field(out, description) || fputc(',', out) == EOF) {
        return 0;
    }
    if (!csv_write_field(out, account) || fputc(',', out) == EOF) {
        return 0;
    }
    if (!csv_write_field(out, change_buf) || fputc(',', out) == EOF) {
        return 0;
    }
    if (!csv_write_field(out, balance_buf) || fputc(',', out) == EOF) {
        return 0;
    }
    if (!csv_write_field(out, commodity) || fputc('\n', out) == EOF) {
        return 0;
    }
    return 1;
}

static int upsert_total(CommodityTotal **totals, size_t *count, size_t *capacity,
                        const char *commodity, double delta, char *err, size_t err_size) {
    size_t index = *count;
    for (size_t i = 0; i < *count; i++) {
        if (strcmp((*totals)[i].commodity, commodity) == 0) {
            index = i;
            break;
        }
    }

    if (index == *count) {
        if (*count == *capacity) {
            size_t new_capacity = *capacity == 0 ? 4 : *capacity * 2;
            CommodityTotal *new_totals = realloc(*totals, new_capacity * sizeof(CommodityTotal));
            if (new_totals == NULL) {
                set_error(err, err_size, "Out of memory while aggregating totals");
                return 0;
            }
            *totals = new_totals;
            *capacity = new_capacity;
        }

        memset(&(*totals)[index], 0, sizeof(CommodityTotal));
        if (commodity != NULL && commodity[0] != '\0') {
            strncpy((*totals)[index].commodity, commodity, sizeof((*totals)[index].commodity) - 1);
        }
        (*count)++;
    }

    (*totals)[index].amount += delta;
    return 1;
}

static int upsert_balance_row(BalanceRow **rows, size_t *count, size_t *capacity,
                              const char *account, const char *commodity, double delta,
                              char *err, size_t err_size) {
    size_t index = *count;
    for (size_t i = 0; i < *count; i++) {
        if (strcmp((*rows)[i].account, account) == 0 && strcmp((*rows)[i].commodity, commodity) == 0) {
            index = i;
            break;
        }
    }

    if (index == *count) {
        if (*count == *capacity) {
            size_t new_capacity = *capacity == 0 ? 16 : *capacity * 2;
            BalanceRow *new_rows = realloc(*rows, new_capacity * sizeof(BalanceRow));
            if (new_rows == NULL) {
                set_error(err, err_size, "Out of memory while aggregating balances");
                return 0;
            }
            *rows = new_rows;
            *capacity = new_capacity;
        }

        memset(&(*rows)[index], 0, sizeof(BalanceRow));
        strncpy((*rows)[index].account, account, sizeof((*rows)[index].account) - 1);
        if (commodity != NULL && commodity[0] != '\0') {
            strncpy((*rows)[index].commodity, commodity, sizeof((*rows)[index].commodity) - 1);
        }
        (*count)++;
    }

    (*rows)[index].amount += delta;
    return 1;
}

static int upsert_balance_row_with_tree(BalanceRow **rows, size_t *count, size_t *capacity,
                                        const char *account, const char *commodity, double delta,
                                        const ReportQuery *query, char *err, size_t err_size) {
    const ReportQuery *q = effective_query(query);
    if (!q->tree) {
        return upsert_balance_row(rows, count, capacity, account, commodity, delta, err, err_size);
    }

    char current[128] = {0};
    strncpy(current, account, sizeof(current) - 1);

    while (1) {
        if (!upsert_balance_row(rows, count, capacity, current, commodity, delta, err, err_size)) {
            return 0;
        }

        char *colon = strrchr(current, ':');
        if (colon == NULL) {
            break;
        }
        *colon = '\0';
    }

    return 1;
}

static int upsert_period_total(PeriodCommodityTotal **totals, size_t *count, size_t *capacity,
                               const char *period, const char *commodity, double delta,
                               char *err, size_t err_size) {
    size_t index = *count;
    for (size_t i = 0; i < *count; i++) {
        if (strcmp((*totals)[i].period, period) == 0 &&
            strcmp((*totals)[i].commodity, commodity) == 0) {
            index = i;
            break;
        }
    }

    if (index == *count) {
        if (*count == *capacity) {
            size_t new_capacity = *capacity == 0 ? 16 : *capacity * 2;
            PeriodCommodityTotal *new_totals = realloc(*totals, new_capacity * sizeof(PeriodCommodityTotal));
            if (new_totals == NULL) {
                set_error(err, err_size, "Out of memory while aggregating period totals");
                return 0;
            }
            *totals = new_totals;
            *capacity = new_capacity;
        }

        memset(&(*totals)[index], 0, sizeof(PeriodCommodityTotal));
        strncpy((*totals)[index].period, period, sizeof((*totals)[index].period) - 1);
        if (commodity != NULL && commodity[0] != '\0') {
            strncpy((*totals)[index].commodity, commodity, sizeof((*totals)[index].commodity) - 1);
        }
        (*count)++;
    }

    (*totals)[index].amount += delta;
    return 1;
}

static int upsert_period_balance_row(PeriodBalanceRow **rows, size_t *count, size_t *capacity,
                                     const char *period, const char *account, const char *commodity,
                                     double delta, char *err, size_t err_size) {
    size_t index = *count;
    for (size_t i = 0; i < *count; i++) {
        if (strcmp((*rows)[i].period, period) == 0 &&
            strcmp((*rows)[i].account, account) == 0 &&
            strcmp((*rows)[i].commodity, commodity) == 0) {
            index = i;
            break;
        }
    }

    if (index == *count) {
        if (*count == *capacity) {
            size_t new_capacity = *capacity == 0 ? 16 : *capacity * 2;
            PeriodBalanceRow *new_rows = realloc(*rows, new_capacity * sizeof(PeriodBalanceRow));
            if (new_rows == NULL) {
                set_error(err, err_size, "Out of memory while aggregating period balances");
                return 0;
            }
            *rows = new_rows;
            *capacity = new_capacity;
        }

        memset(&(*rows)[index], 0, sizeof(PeriodBalanceRow));
        strncpy((*rows)[index].period, period, sizeof((*rows)[index].period) - 1);
        strncpy((*rows)[index].account, account, sizeof((*rows)[index].account) - 1);
        if (commodity != NULL && commodity[0] != '\0') {
            strncpy((*rows)[index].commodity, commodity, sizeof((*rows)[index].commodity) - 1);
        }
        (*count)++;
    }

    (*rows)[index].amount += delta;
    return 1;
}

static int upsert_period_balance_row_with_tree(PeriodBalanceRow **rows, size_t *count, size_t *capacity,
                                               const char *period, const char *account, const char *commodity,
                                               double delta, const ReportQuery *query,
                                               char *err, size_t err_size) {
    const ReportQuery *q = effective_query(query);
    if (!q->tree) {
        return upsert_period_balance_row(rows, count, capacity, period, account, commodity, delta, err, err_size);
    }

    char current[128] = {0};
    strncpy(current, account, sizeof(current) - 1);

    while (1) {
        if (!upsert_period_balance_row(rows, count, capacity, period, current, commodity, delta, err, err_size)) {
            return 0;
        }
        char *colon = strrchr(current, ':');
        if (colon == NULL) {
            break;
        }
        *colon = '\0';
    }

    return 1;
}

static int reserve_register_entries(RegisterEntry **entries, size_t *count, size_t *capacity,
                                    char *err, size_t err_size) {
    if (*count < *capacity) {
        return 1;
    }

    size_t new_capacity = *capacity == 0 ? 32 : *capacity * 2;
    RegisterEntry *new_entries = realloc(*entries, new_capacity * sizeof(RegisterEntry));
    if (new_entries == NULL) {
        set_error(err, err_size, "Out of memory while building register entries");
        return 0;
    }

    *entries = new_entries;
    *capacity = new_capacity;
    return 1;
}

static int add_unique_account(AccountName **names, size_t *count, size_t *capacity,
                              const char *account, char *err, size_t err_size) {
    for (size_t i = 0; i < *count; i++) {
        if (strcmp((*names)[i].name, account) == 0) {
            return 1;
        }
    }

    if (*count == *capacity) {
        size_t new_capacity = *capacity == 0 ? 16 : *capacity * 2;
        AccountName *new_names = realloc(*names, new_capacity * sizeof(AccountName));
        if (new_names == NULL) {
            set_error(err, err_size, "Out of memory while collecting accounts");
            return 0;
        }
        *names = new_names;
        *capacity = new_capacity;
    }

    memset(&(*names)[*count], 0, sizeof(AccountName));
    strncpy((*names)[*count].name, account, sizeof((*names)[*count].name) - 1);
    (*count)++;
    return 1;
}

static int add_unique_account_with_tree(AccountName **names, size_t *count, size_t *capacity,
                                        const char *account, const ReportQuery *query,
                                        char *err, size_t err_size) {
    const ReportQuery *q = effective_query(query);
    if (!q->tree) {
        return add_unique_account(names, count, capacity, account, err, err_size);
    }

    char current[128] = {0};
    strncpy(current, account, sizeof(current) - 1);

    while (1) {
        if (!add_unique_account(names, count, capacity, current, err, err_size)) {
            return 0;
        }

        char *colon = strrchr(current, ':');
        if (colon == NULL) {
            break;
        }
        *colon = '\0';
    }

    return 1;
}

static int compare_balance_rows(const void *a, const void *b) {
    const BalanceRow *left = (const BalanceRow *)a;
    const BalanceRow *right = (const BalanceRow *)b;
    int account_cmp = strcmp(left->account, right->account);
    if (account_cmp != 0) {
        return account_cmp;
    }
    return strcmp(left->commodity, right->commodity);
}

static int compare_account_names(const void *a, const void *b) {
    const AccountName *left = (const AccountName *)a;
    const AccountName *right = (const AccountName *)b;
    return strcmp(left->name, right->name);
}

static int compare_period_balance_rows(const void *a, const void *b) {
    const PeriodBalanceRow *left = (const PeriodBalanceRow *)a;
    const PeriodBalanceRow *right = (const PeriodBalanceRow *)b;
    int period_cmp = strcmp(left->period, right->period);
    if (period_cmp != 0) {
        return period_cmp;
    }
    int account_cmp = strcmp(left->account, right->account);
    if (account_cmp != 0) {
        return account_cmp;
    }
    return strcmp(left->commodity, right->commodity);
}

static int compare_period_totals(const void *a, const void *b) {
    const PeriodCommodityTotal *left = (const PeriodCommodityTotal *)a;
    const PeriodCommodityTotal *right = (const PeriodCommodityTotal *)b;
    int period_cmp = strcmp(left->period, right->period);
    if (period_cmp != 0) {
        return period_cmp;
    }
    return strcmp(left->commodity, right->commodity);
}

static int compare_register_entries(const void *a, const void *b) {
    const RegisterEntry *left = (const RegisterEntry *)a;
    const RegisterEntry *right = (const RegisterEntry *)b;
    int period_cmp = strcmp(left->period, right->period);
    if (period_cmp != 0) {
        return period_cmp;
    }
    int date_cmp = strcmp(left->date, right->date);
    if (date_cmp != 0) {
        return date_cmp;
    }
    if (left->ordinal < right->ordinal) {
        return -1;
    }
    if (left->ordinal > right->ordinal) {
        return 1;
    }
    return 0;
}

int report_print_journal(const Ledger *ledger, const ReportQuery *query, FILE *out, char *err, size_t err_size) {
    if (ledger == NULL || out == NULL) {
        set_error(err, err_size, "Invalid print report arguments");
        return 0;
    }

    size_t shown = 0;
    for (size_t i = 0; i < ledger->count; i++) {
        const Transaction *txn = &ledger->transactions[i];
        if (!transaction_matches_date(txn, query)) {
            continue;
        }

        size_t matched_postings = 0;
        for (size_t j = 0; j < txn->postings_count; j++) {
            const Posting *posting = &txn->postings[j];
            if (posting_matches_account(posting, query)) {
                matched_postings++;
            }
        }

        if (matched_postings == 0) {
            continue;
        }

        if (fprintf(out, "%s %s\n", txn->date, txn->description) < 0) {
            set_error(err, err_size, "Failed writing print output");
            return 0;
        }

        for (size_t j = 0; j < txn->postings_count; j++) {
            const Posting *posting = &txn->postings[j];
            if (!posting_matches_account(posting, query)) {
                continue;
            }

            char display_account[128] = {0};
            account_with_depth(posting->account, query, display_account);

            if (posting->has_amount) {
                char commodity[16] = {0};
                resolve_commodity(ledger, posting, commodity);
                if (commodity[0] != '\0') {
                    if (fprintf(out, "    %-30s %12.2f %s\n", display_account, posting->amount, commodity) < 0) {
                        set_error(err, err_size, "Failed writing print output");
                        return 0;
                    }
                } else {
                    if (fprintf(out, "    %-30s %12.2f\n", display_account, posting->amount) < 0) {
                        set_error(err, err_size, "Failed writing print output");
                        return 0;
                    }
                }
            } else {
                if (fprintf(out, "    %s\n", display_account) < 0) {
                    set_error(err, err_size, "Failed writing print output");
                    return 0;
                }
            }
        }

        if (fputc('\n', out) == EOF) {
            set_error(err, err_size, "Failed writing print output");
            return 0;
        }

        shown++;
    }

    if (shown == 0) {
        if (fprintf(out, "(no matching transactions)\n") < 0) {
            set_error(err, err_size, "Failed writing print output");
            return 0;
        }
    }

    return 1;
}

int report_balance(const Ledger *ledger, const ReportQuery *query, FILE *out, char *err, size_t err_size) {
    if (ledger == NULL || out == NULL) {
        set_error(err, err_size, "Invalid balance report arguments");
        return 0;
    }

    if (has_periodic_mode(query)) {
        PeriodBalanceRow *rows = NULL;
        size_t rows_count = 0;
        size_t rows_capacity = 0;
        PeriodCommodityTotal *totals = NULL;
        size_t totals_count = 0;
        size_t totals_capacity = 0;

        for (size_t i = 0; i < ledger->count; i++) {
            const Transaction *txn = &ledger->transactions[i];
            if (!transaction_matches_date(txn, query)) {
                continue;
            }

            char period[11] = {0};
            period_key_for_date(txn->date, query, period);

            for (size_t j = 0; j < txn->postings_count; j++) {
                const Posting *posting = &txn->postings[j];
                if (!posting->has_amount || !posting_matches_account(posting, query)) {
                    continue;
                }

                char display_account[128] = {0};
                char commodity[16] = {0};
                account_with_depth(posting->account, query, display_account);
                resolve_commodity(ledger, posting, commodity);

                if (!upsert_period_balance_row_with_tree(&rows, &rows_count, &rows_capacity,
                                                         period, display_account, commodity,
                                                         posting->amount, query, err, err_size)) {
                    free(rows);
                    free(totals);
                    return 0;
                }

                if (!upsert_period_total(&totals, &totals_count, &totals_capacity,
                                         period, commodity, posting->amount,
                                         err, err_size)) {
                    free(rows);
                    free(totals);
                    return 0;
                }
            }
        }

        qsort(rows, rows_count, sizeof(PeriodBalanceRow), compare_period_balance_rows);
        qsort(totals, totals_count, sizeof(PeriodCommodityTotal), compare_period_totals);

        if (is_csv_output(query)) {
            if (fprintf(out, "period,account,amount,commodity,row_type\n") < 0) {
                set_error(err, err_size, "Failed writing balance output");
                free(rows);
                free(totals);
                return 0;
            }

            for (size_t i = 0; i < rows_count; i++) {
                char display_account[128] = {0};
                format_account_for_output(rows[i].account, query, display_account);
                if (!csv_write_balance_row(out, rows[i].period, display_account, rows[i].amount,
                                           rows[i].commodity, "account")) {
                    set_error(err, err_size, "Failed writing balance output");
                    free(rows);
                    free(totals);
                    return 0;
                }
            }

            for (size_t i = 0; i < totals_count; i++) {
                if (!csv_write_balance_row(out, totals[i].period, "total", totals[i].amount,
                                           totals[i].commodity, "total")) {
                    set_error(err, err_size, "Failed writing balance output");
                    free(rows);
                    free(totals);
                    return 0;
                }
            }

            free(rows);
            free(totals);
            return 1;
        }

        if (rows_count == 0) {
            if (fprintf(out, "(no matching accounts)\n") < 0) {
                set_error(err, err_size, "Failed writing balance output");
                free(rows);
                free(totals);
                return 0;
            }
            free(rows);
            free(totals);
            return 1;
        }

        size_t row_index = 0;
        size_t total_index = 0;
        while (row_index < rows_count) {
            const char *period = rows[row_index].period;
            if (fprintf(out, "period %s\n", period) < 0) {
                set_error(err, err_size, "Failed writing balance output");
                free(rows);
                free(totals);
                return 0;
            }

            while (row_index < rows_count && strcmp(rows[row_index].period, period) == 0) {
                const PeriodBalanceRow *row = &rows[row_index];
                char display_account[128] = {0};
                format_account_for_output(row->account, query, display_account);

                if (row->commodity[0] != '\0') {
                    if (fprintf(out, "%-40s %14.2f %s\n", display_account, row->amount, row->commodity) < 0) {
                        set_error(err, err_size, "Failed writing balance output");
                        free(rows);
                        free(totals);
                        return 0;
                    }
                } else {
                    if (fprintf(out, "%-40s %14.2f\n", display_account, row->amount) < 0) {
                        set_error(err, err_size, "Failed writing balance output");
                        free(rows);
                        free(totals);
                        return 0;
                    }
                }

                row_index++;
            }

            if (fprintf(out, "----------------------------------------\n") < 0) {
                set_error(err, err_size, "Failed writing balance output");
                free(rows);
                free(totals);
                return 0;
            }

            while (total_index < totals_count && strcmp(totals[total_index].period, period) < 0) {
                total_index++;
            }

            while (total_index < totals_count && strcmp(totals[total_index].period, period) == 0) {
                const PeriodCommodityTotal *total = &totals[total_index];
                if (total->commodity[0] != '\0') {
                    if (fprintf(out, "%40s %14.2f %s\n", "total", total->amount, total->commodity) < 0) {
                        set_error(err, err_size, "Failed writing balance output");
                        free(rows);
                        free(totals);
                        return 0;
                    }
                } else {
                    if (fprintf(out, "%40s %14.2f\n", "total", total->amount) < 0) {
                        set_error(err, err_size, "Failed writing balance output");
                        free(rows);
                        free(totals);
                        return 0;
                    }
                }
                total_index++;
            }

            if (row_index < rows_count) {
                if (fputc('\n', out) == EOF) {
                    set_error(err, err_size, "Failed writing balance output");
                    free(rows);
                    free(totals);
                    return 0;
                }
            }
        }

        free(rows);
        free(totals);
        return 1;
    }

    BalanceRow *rows = NULL;
    size_t rows_count = 0;
    size_t rows_capacity = 0;
    CommodityTotal *totals = NULL;
    size_t totals_count = 0;
    size_t totals_capacity = 0;

    for (size_t i = 0; i < ledger->count; i++) {
        const Transaction *txn = &ledger->transactions[i];
        if (!transaction_matches_date(txn, query)) {
            continue;
        }

        for (size_t j = 0; j < txn->postings_count; j++) {
            const Posting *posting = &txn->postings[j];
            if (!posting->has_amount || !posting_matches_account(posting, query)) {
                continue;
            }

            char display_account[128] = {0};
            char commodity[16] = {0};
            account_with_depth(posting->account, query, display_account);
            resolve_commodity(ledger, posting, commodity);

            if (!upsert_balance_row_with_tree(&rows, &rows_count, &rows_capacity,
                                              display_account, commodity, posting->amount,
                                              query, err, err_size)) {
                free(rows);
                free(totals);
                return 0;
            }

            if (!upsert_total(&totals, &totals_count, &totals_capacity, commodity, posting->amount, err, err_size)) {
                free(rows);
                free(totals);
                return 0;
            }
        }
    }

    qsort(rows, rows_count, sizeof(BalanceRow), compare_balance_rows);

    if (is_csv_output(query)) {
        if (fprintf(out, "account,amount,commodity,row_type\n") < 0) {
            set_error(err, err_size, "Failed writing balance output");
            free(rows);
            free(totals);
            return 0;
        }

        for (size_t i = 0; i < rows_count; i++) {
            char display_account[128] = {0};
            format_account_for_output(rows[i].account, query, display_account);
            if (!csv_write_balance_row(out, NULL, display_account, rows[i].amount,
                                       rows[i].commodity, "account")) {
                set_error(err, err_size, "Failed writing balance output");
                free(rows);
                free(totals);
                return 0;
            }
        }

        for (size_t i = 0; i < totals_count; i++) {
            if (!csv_write_balance_row(out, NULL, "total", totals[i].amount,
                                       totals[i].commodity, "total")) {
                set_error(err, err_size, "Failed writing balance output");
                free(rows);
                free(totals);
                return 0;
            }
        }

        free(rows);
        free(totals);
        return 1;
    }

    if (rows_count == 0) {
        if (fprintf(out, "(no matching accounts)\n") < 0) {
            set_error(err, err_size, "Failed writing balance output");
            free(rows);
            free(totals);
            return 0;
        }
        free(rows);
        free(totals);
        return 1;
    }

    for (size_t i = 0; i < rows_count; i++) {
        const BalanceRow *row = &rows[i];
        char display_account[128] = {0};
        format_account_for_output(row->account, query, display_account);

        if (row->commodity[0] != '\0') {
            if (fprintf(out, "%-40s %14.2f %s\n", display_account, row->amount, row->commodity) < 0) {
                set_error(err, err_size, "Failed writing balance output");
                free(rows);
                free(totals);
                return 0;
            }
        } else {
            if (fprintf(out, "%-40s %14.2f\n", display_account, row->amount) < 0) {
                set_error(err, err_size, "Failed writing balance output");
                free(rows);
                free(totals);
                return 0;
            }
        }

    }

    if (fprintf(out, "----------------------------------------\n") < 0) {
        set_error(err, err_size, "Failed writing balance output");
        free(rows);
        free(totals);
        return 0;
    }

    for (size_t i = 0; i < totals_count; i++) {
        const CommodityTotal *total = &totals[i];
        if (total->commodity[0] != '\0') {
            if (fprintf(out, "%40s %14.2f %s\n", "total", total->amount, total->commodity) < 0) {
                set_error(err, err_size, "Failed writing balance output");
                free(rows);
                free(totals);
                return 0;
            }
        } else {
            if (fprintf(out, "%40s %14.2f\n", "total", total->amount) < 0) {
                set_error(err, err_size, "Failed writing balance output");
                free(rows);
                free(totals);
                return 0;
            }
        }
    }

    free(rows);
    free(totals);
    return 1;
}

int report_register(const Ledger *ledger, const ReportQuery *query, FILE *out, char *err, size_t err_size) {
    if (ledger == NULL || out == NULL) {
        set_error(err, err_size, "Invalid register report arguments");
        return 0;
    }

    if (has_periodic_mode(query)) {
        RegisterEntry *entries = NULL;
        size_t entries_count = 0;
        size_t entries_capacity = 0;
        size_t ordinal = 0;

        for (size_t i = 0; i < ledger->count; i++) {
            const Transaction *txn = &ledger->transactions[i];
            if (!transaction_matches_date(txn, query)) {
                continue;
            }

            char period[11] = {0};
            period_key_for_date(txn->date, query, period);

            for (size_t j = 0; j < txn->postings_count; j++) {
                const Posting *posting = &txn->postings[j];
                if (!posting->has_amount || !posting_matches_account(posting, query)) {
                    continue;
                }

                if (!reserve_register_entries(&entries, &entries_count, &entries_capacity, err, err_size)) {
                    free(entries);
                    return 0;
                }

                RegisterEntry *entry = &entries[entries_count++];
                memset(entry, 0, sizeof(*entry));
                strncpy(entry->period, period, sizeof(entry->period) - 1);
                strncpy(entry->date, txn->date, sizeof(entry->date) - 1);
                strncpy(entry->description, txn->description, sizeof(entry->description) - 1);
                entry->change = posting->amount;
                entry->ordinal = ordinal++;

                account_with_depth(posting->account, query, entry->account);
                resolve_commodity(ledger, posting, entry->commodity);
            }
        }

        qsort(entries, entries_count, sizeof(RegisterEntry), compare_register_entries);

        if (is_csv_output(query)) {
            if (fprintf(out, "period,date,description,account,change,balance,commodity\n") < 0) {
                set_error(err, err_size, "Failed writing register output");
                free(entries);
                return 0;
            }

            size_t index = 0;
            while (index < entries_count) {
                const char *period = entries[index].period;
                CommodityTotal *running = NULL;
                size_t running_count = 0;
                size_t running_capacity = 0;

                while (index < entries_count && strcmp(entries[index].period, period) == 0) {
                    RegisterEntry *entry = &entries[index];
                    if (!upsert_total(&running, &running_count, &running_capacity,
                                      entry->commodity, entry->change, err, err_size)) {
                        free(running);
                        free(entries);
                        return 0;
                    }

                    double running_balance = 0.0;
                    for (size_t k = 0; k < running_count; k++) {
                        if (strcmp(running[k].commodity, entry->commodity) == 0) {
                            running_balance = running[k].amount;
                            break;
                        }
                    }

                    if (!csv_write_register_row(out, period, entry->date, entry->description,
                                                entry->account, entry->change, running_balance,
                                                entry->commodity)) {
                        set_error(err, err_size, "Failed writing register output");
                        free(running);
                        free(entries);
                        return 0;
                    }
                    index++;
                }

                free(running);
            }

            free(entries);
            return 1;
        }

        if (entries_count == 0) {
            if (fprintf(out, "(no matching postings)\n") < 0) {
                set_error(err, err_size, "Failed writing register output");
                free(entries);
                return 0;
            }
            free(entries);
            return 1;
        }

        size_t index = 0;
        while (index < entries_count) {
            const char *period = entries[index].period;
            if (fprintf(out, "period %s\n", period) < 0) {
                set_error(err, err_size, "Failed writing register output");
                free(entries);
                return 0;
            }

            if (fprintf(out, "%-10s  %-24s  %-26s  %12s  %12s\n",
                        "date", "description", "account", "change", "balance") < 0) {
                set_error(err, err_size, "Failed writing register output");
                free(entries);
                return 0;
            }

            CommodityTotal *running = NULL;
            size_t running_count = 0;
            size_t running_capacity = 0;

            while (index < entries_count && strcmp(entries[index].period, period) == 0) {
                RegisterEntry *entry = &entries[index];

                if (!upsert_total(&running, &running_count, &running_capacity,
                                  entry->commodity, entry->change, err, err_size)) {
                    free(running);
                    free(entries);
                    return 0;
                }

                double running_balance = 0.0;
                for (size_t k = 0; k < running_count; k++) {
                    if (strcmp(running[k].commodity, entry->commodity) == 0) {
                        running_balance = running[k].amount;
                        break;
                    }
                }

                if (entry->commodity[0] != '\0') {
                    if (fprintf(out, "%-10s  %-24.24s  %-26.26s  %8.2f %-3s  %8.2f %-3s\n",
                                entry->date, entry->description, entry->account,
                                entry->change, entry->commodity, running_balance, entry->commodity) < 0) {
                        set_error(err, err_size, "Failed writing register output");
                        free(running);
                        free(entries);
                        return 0;
                    }
                } else {
                    if (fprintf(out, "%-10s  %-24.24s  %-26.26s  %12.2f  %12.2f\n",
                                entry->date, entry->description, entry->account,
                                entry->change, running_balance) < 0) {
                        set_error(err, err_size, "Failed writing register output");
                        free(running);
                        free(entries);
                        return 0;
                    }
                }

                index++;
            }

            free(running);
            if (index < entries_count) {
                if (fputc('\n', out) == EOF) {
                    set_error(err, err_size, "Failed writing register output");
                    free(entries);
                    return 0;
                }
            }
        }

        free(entries);
        return 1;
    }

    CommodityTotal *running = NULL;
    size_t running_count = 0;
    size_t running_capacity = 0;
    size_t shown = 0;

    if (is_csv_output(query)) {
        if (fprintf(out, "date,description,account,change,balance,commodity\n") < 0) {
            set_error(err, err_size, "Failed writing register output");
            return 0;
        }
    } else {
        if (fprintf(out, "%-10s  %-24s  %-26s  %12s  %12s\n", "date", "description", "account", "change", "balance") < 0) {
            set_error(err, err_size, "Failed writing register output");
            return 0;
        }
    }

    for (size_t i = 0; i < ledger->count; i++) {
        const Transaction *txn = &ledger->transactions[i];
        if (!transaction_matches_date(txn, query)) {
            continue;
        }

        for (size_t j = 0; j < txn->postings_count; j++) {
            const Posting *posting = &txn->postings[j];
            if (!posting->has_amount || !posting_matches_account(posting, query)) {
                continue;
            }

            char commodity[16] = {0};
            char display_account[128] = {0};
            resolve_commodity(ledger, posting, commodity);
            account_with_depth(posting->account, query, display_account);

            if (!upsert_total(&running, &running_count, &running_capacity, commodity, posting->amount, err, err_size)) {
                free(running);
                return 0;
            }

            double running_balance = 0.0;
            for (size_t k = 0; k < running_count; k++) {
                if (strcmp(running[k].commodity, commodity) == 0) {
                    running_balance = running[k].amount;
                    break;
                }
            }

            if (is_csv_output(query)) {
                if (!csv_write_register_row(out, NULL, txn->date, txn->description, display_account,
                                            posting->amount, running_balance, commodity)) {
                    set_error(err, err_size, "Failed writing register output");
                    free(running);
                    return 0;
                }
            } else {
                if (commodity[0] != '\0') {
                    if (fprintf(out, "%-10s  %-24.24s  %-26.26s  %8.2f %-3s  %8.2f %-3s\n",
                                txn->date, txn->description, display_account,
                                posting->amount, commodity, running_balance, commodity) < 0) {
                        set_error(err, err_size, "Failed writing register output");
                        free(running);
                        return 0;
                    }
                } else {
                    if (fprintf(out, "%-10s  %-24.24s  %-26.26s  %12.2f  %12.2f\n",
                                txn->date, txn->description, display_account,
                                posting->amount, running_balance) < 0) {
                        set_error(err, err_size, "Failed writing register output");
                        free(running);
                        return 0;
                    }
                }
            }

            shown++;
        }
    }

    if (shown == 0) {
        if (!is_csv_output(query)) {
            if (fprintf(out, "(no matching postings)\n") < 0) {
                set_error(err, err_size, "Failed writing register output");
                free(running);
                return 0;
            }
        }
    }

    free(running);
    return 1;
}

int report_accounts(const Ledger *ledger, const ReportQuery *query, FILE *out, char *err, size_t err_size) {
    if (ledger == NULL || out == NULL) {
        set_error(err, err_size, "Invalid accounts report arguments");
        return 0;
    }

    AccountName *names = NULL;
    size_t names_count = 0;
    size_t names_capacity = 0;

    for (size_t i = 0; i < ledger->count; i++) {
        const Transaction *txn = &ledger->transactions[i];
        if (!transaction_matches_date(txn, query)) {
            continue;
        }

        for (size_t j = 0; j < txn->postings_count; j++) {
            const Posting *posting = &txn->postings[j];
            if (!posting_matches_account(posting, query)) {
                continue;
            }

            char display_account[128] = {0};
            account_with_depth(posting->account, query, display_account);

            if (!add_unique_account_with_tree(&names, &names_count, &names_capacity,
                                              display_account, query, err, err_size)) {
                free(names);
                return 0;
            }
        }
    }

    qsort(names, names_count, sizeof(AccountName), compare_account_names);

    if (names_count == 0) {
        if (fprintf(out, "(no matching accounts)\n") < 0) {
            set_error(err, err_size, "Failed writing accounts output");
            free(names);
            return 0;
        }
        free(names);
        return 1;
    }

    for (size_t i = 0; i < names_count; i++) {
        char display_account[128] = {0};
        format_account_for_output(names[i].name, query, display_account);
        if (fprintf(out, "%s\n", display_account) < 0) {
            set_error(err, err_size, "Failed writing accounts output");
            free(names);
            return 0;
        }
    }

    free(names);
    return 1;
}
