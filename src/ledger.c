#include "ledger.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define INCLUDE_MAX_DEPTH 32

typedef struct {
    char paths[INCLUDE_MAX_DEPTH][PATH_MAX];
    int depth;
} IncludeStack;

typedef struct {
    int strict;
    IncludeStack stack;
} LoadContext;

static void set_error(char *err, size_t err_size, const char *fmt, ...) {
    if (err == NULL || err_size == 0 || fmt == NULL) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    vsnprintf(err, err_size, fmt, args);
    va_end(args);
}

static void set_error_at(char *err, size_t err_size, const char *path, long line, const char *fmt, ...) {
    if (err == NULL || err_size == 0 || fmt == NULL) {
        return;
    }

    char detail[512] = {0};
    va_list args;
    va_start(args, fmt);
    vsnprintf(detail, sizeof(detail), fmt, args);
    va_end(args);

    if (path != NULL && line > 0) {
        snprintf(err, err_size, "%s:%ld: %s", path, line, detail);
    } else if (path != NULL) {
        snprintf(err, err_size, "%s: %s", path, detail);
    } else {
        snprintf(err, err_size, "%s", detail);
    }
}

static char *trim_left(char *s) {
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

static void trim_right(char *s) {
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[len - 1] = '\0';
        len--;
    }
}

static int is_iso_date_prefix(const char *s) {
    if (s == NULL || strlen(s) < 10) {
        return 0;
    }

    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) {
            if (s[i] != '-') {
                return 0;
            }
        } else if (!isdigit((unsigned char)s[i])) {
            return 0;
        }
    }

    return 1;
}

static int reserve_postings(Transaction *txn, size_t needed, char *err, size_t err_size) {
    if (needed <= txn->postings_capacity) {
        return 1;
    }

    size_t new_capacity = txn->postings_capacity == 0 ? 4 : txn->postings_capacity * 2;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }

    Posting *new_postings = realloc(txn->postings, new_capacity * sizeof(Posting));
    if (new_postings == NULL) {
        set_error(err, err_size, "Out of memory while growing postings array");
        return 0;
    }

    txn->postings = new_postings;
    txn->postings_capacity = new_capacity;
    return 1;
}

static int reserve_transactions(Ledger *ledger, size_t needed, char *err, size_t err_size) {
    if (needed <= ledger->capacity) {
        return 1;
    }

    size_t new_capacity = ledger->capacity == 0 ? 16 : ledger->capacity * 2;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }

    Transaction *new_txns = realloc(ledger->transactions, new_capacity * sizeof(Transaction));
    if (new_txns == NULL) {
        set_error(err, err_size, "Out of memory while growing transaction array");
        return 0;
    }

    ledger->transactions = new_txns;
    ledger->capacity = new_capacity;
    return 1;
}

void transaction_init(Transaction *txn) {
    if (txn == NULL) {
        return;
    }

    memset(txn, 0, sizeof(*txn));
}

void transaction_free(Transaction *txn) {
    if (txn == NULL) {
        return;
    }

    free(txn->postings);
    txn->postings = NULL;
    txn->postings_count = 0;
    txn->postings_capacity = 0;
}

int transaction_add_posting(Transaction *txn, const Posting *posting, char *err, size_t err_size) {
    if (txn == NULL || posting == NULL) {
        set_error(err, err_size, "Invalid posting insert");
        return 0;
    }

    if (!reserve_postings(txn, txn->postings_count + 1, err, err_size)) {
        return 0;
    }

    txn->postings[txn->postings_count++] = *posting;
    return 1;
}

void ledger_init(Ledger *ledger) {
    if (ledger == NULL) {
        return;
    }

    memset(ledger, 0, sizeof(*ledger));
}

void ledger_free(Ledger *ledger) {
    if (ledger == NULL) {
        return;
    }

    for (size_t i = 0; i < ledger->count; i++) {
        transaction_free(&ledger->transactions[i]);
    }

    free(ledger->transactions);
    ledger->transactions = NULL;
    ledger->count = 0;
    ledger->capacity = 0;
    memset(ledger->default_commodity, 0, sizeof(ledger->default_commodity));
}

static int copy_transaction(Transaction *dst, const Transaction *src, char *err, size_t err_size) {
    transaction_init(dst);

    strncpy(dst->date, src->date, sizeof(dst->date) - 1);
    strncpy(dst->description, src->description, sizeof(dst->description) - 1);

    if (src->postings_count == 0) {
        return 1;
    }

    dst->postings = calloc(src->postings_count, sizeof(Posting));
    if (dst->postings == NULL) {
        set_error(err, err_size, "Out of memory while copying transaction postings");
        return 0;
    }

    memcpy(dst->postings, src->postings, src->postings_count * sizeof(Posting));
    dst->postings_count = src->postings_count;
    dst->postings_capacity = src->postings_count;
    return 1;
}

int ledger_add_transaction_copy(Ledger *ledger, const Transaction *txn, char *err, size_t err_size) {
    if (ledger == NULL || txn == NULL) {
        set_error(err, err_size, "Invalid transaction insert");
        return 0;
    }

    if (!reserve_transactions(ledger, ledger->count + 1, err, err_size)) {
        return 0;
    }

    Transaction copy;
    if (!copy_transaction(&copy, txn, err, err_size)) {
        return 0;
    }

    ledger->transactions[ledger->count++] = copy;
    return 1;
}

static int parse_posting_line(const char *line, Posting *posting) {
    if (line == NULL || posting == NULL) {
        return 0;
    }

    memset(posting, 0, sizeof(*posting));

    const char *cursor = line;
    while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
        cursor++;
    }

    if (*cursor == '\0' || *cursor == ';' || *cursor == '#') {
        return 0;
    }

    const char *account_end = cursor;
    while (*account_end != '\0' && !isspace((unsigned char)*account_end)) {
        account_end++;
    }

    size_t account_len = (size_t)(account_end - cursor);
    if (account_len == 0 || account_len >= sizeof(posting->account)) {
        return 0;
    }

    memcpy(posting->account, cursor, account_len);
    posting->account[account_len] = '\0';

    cursor = account_end;
    while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
        cursor++;
    }

    if (*cursor == '\0' || *cursor == ';' || *cursor == '#') {
        posting->has_amount = 0;
        return 1;
    }

    char *amount_end = NULL;
    double amount = strtod(cursor, &amount_end);
    if (amount_end == cursor) {
        posting->has_amount = 0;
        return 1;
    }

    posting->has_amount = 1;
    posting->amount = amount;

    while (*amount_end != '\0' && isspace((unsigned char)*amount_end)) {
        amount_end++;
    }

    if (*amount_end != '\0' && *amount_end != ';' && *amount_end != '#') {
        size_t commodity_len = 0;
        while (amount_end[commodity_len] != '\0' && !isspace((unsigned char)amount_end[commodity_len]) &&
               amount_end[commodity_len] != ';' && amount_end[commodity_len] != '#') {
            commodity_len++;
        }

        if (commodity_len > 0 && commodity_len < sizeof(posting->commodity)) {
            memcpy(posting->commodity, amount_end, commodity_len);
            posting->commodity[commodity_len] = '\0';
        }
    }

    return 1;
}

static void finalize_transaction(Ledger *ledger, Transaction *txn) {
    size_t missing_count = 0;
    size_t missing_index = 0;
    double known_sum = 0.0;
    char commodity[16] = {0};

    for (size_t i = 0; i < txn->postings_count; i++) {
        Posting *posting = &txn->postings[i];
        if (!posting->has_amount) {
            missing_index = i;
            missing_count++;
            continue;
        }

        known_sum += posting->amount;
        if (commodity[0] == '\0' && posting->commodity[0] != '\0') {
            strncpy(commodity, posting->commodity, sizeof(commodity) - 1);
        }
    }

    if (missing_count == 1) {
        Posting *missing = &txn->postings[missing_index];
        missing->has_amount = 1;
        missing->amount = -known_sum;

        if (missing->commodity[0] == '\0') {
            if (commodity[0] != '\0') {
                strncpy(missing->commodity, commodity, sizeof(missing->commodity) - 1);
            } else if (ledger->default_commodity[0] != '\0') {
                strncpy(missing->commodity, ledger->default_commodity, sizeof(missing->commodity) - 1);
            }
        }
    }

    if (ledger->default_commodity[0] == '\0' && commodity[0] != '\0') {
        strncpy(ledger->default_commodity, commodity, sizeof(ledger->default_commodity) - 1);
    }
}

typedef struct {
    char commodity[16];
    double sum;
} CommoditySum;

static void resolve_posting_commodity(const Ledger *ledger, const Posting *posting, char out[16]) {
    memset(out, 0, 16);
    if (posting->commodity[0] != '\0') {
        strncpy(out, posting->commodity, 15);
    } else if (ledger->default_commodity[0] != '\0') {
        strncpy(out, ledger->default_commodity, 15);
    }
}

static int validate_transaction_strict(const Ledger *ledger, const Transaction *txn,
                                       const char *path, long txn_start_line,
                                       char *err, size_t err_size) {
    if (txn->postings_count < 2) {
        set_error_at(err, err_size, path, txn_start_line,
                     "Transaction must have at least two postings");
        return 0;
    }

    size_t missing_count = 0;
    for (size_t i = 0; i < txn->postings_count; i++) {
        if (!txn->postings[i].has_amount) {
            missing_count++;
        }
    }

    if (missing_count > 0) {
        set_error_at(err, err_size, path, txn_start_line,
                     "Transaction has %zu posting(s) without amount", missing_count);
        return 0;
    }

    CommoditySum *sums = calloc(txn->postings_count, sizeof(CommoditySum));
    if (sums == NULL) {
        set_error_at(err, err_size, path, txn_start_line,
                     "Out of memory while validating transaction balance");
        return 0;
    }

    size_t sums_count = 0;
    for (size_t i = 0; i < txn->postings_count; i++) {
        const Posting *posting = &txn->postings[i];
        char commodity[16] = {0};
        resolve_posting_commodity(ledger, posting, commodity);

        size_t index = sums_count;
        for (size_t k = 0; k < sums_count; k++) {
            if (strcmp(sums[k].commodity, commodity) == 0) {
                index = k;
                break;
            }
        }

        if (index == sums_count) {
            if (commodity[0] != '\0') {
                strncpy(sums[index].commodity, commodity, sizeof(sums[index].commodity) - 1);
            }
            sums_count++;
        }

        sums[index].sum += posting->amount;
    }

    for (size_t i = 0; i < sums_count; i++) {
        if (fabs(sums[i].sum) > 1e-7) {
            const char *commodity = sums[i].commodity[0] != '\0' ? sums[i].commodity : "(none)";
            set_error_at(err, err_size, path, txn_start_line,
                         "Unbalanced transaction for commodity %s (sum %.8f)",
                         commodity, sums[i].sum);
            free(sums);
            return 0;
        }
    }

    free(sums);
    return 1;
}

static int starts_with_keyword(const char *line, const char *keyword) {
    size_t keyword_len = strlen(keyword);
    if (strncmp(line, keyword, keyword_len) != 0) {
        return 0;
    }
    return line[keyword_len] == '\0' || isspace((unsigned char)line[keyword_len]);
}

static int parse_include_target(const char *line, const char *path, long line_no,
                                char *out, size_t out_size, char *err, size_t err_size) {
    if (!starts_with_keyword(line, "include")) {
        return 0;
    }

    const char *cursor = line + strlen("include");
    while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
        cursor++;
    }

    if (*cursor == '\0') {
        set_error_at(err, err_size, path, line_no, "include directive requires a path");
        return -1;
    }

    size_t len = 0;
    if (*cursor == '"' || *cursor == '\'') {
        char quote = *cursor++;
        while (*cursor != '\0' && *cursor != quote) {
            if (len + 1 < out_size) {
                out[len++] = *cursor;
            }
            cursor++;
        }

        if (*cursor != quote) {
            set_error_at(err, err_size, path, line_no, "Unterminated quoted include path");
            return -1;
        }
    } else {
        while (*cursor != '\0' && !isspace((unsigned char)*cursor) && *cursor != ';' && *cursor != '#') {
            if (len + 1 < out_size) {
                out[len++] = *cursor;
            }
            cursor++;
        }
    }

    out[len] = '\0';
    if (len == 0) {
        set_error_at(err, err_size, path, line_no, "Empty include path");
        return -1;
    }

    return 1;
}

static void path_dirname(const char *path, char *out, size_t out_size) {
    if (out_size == 0) {
        return;
    }

    const char *slash = strrchr(path, '/');
    if (slash == NULL) {
        strncpy(out, ".", out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }

    size_t len = (size_t)(slash - path);
    if (len == 0) {
        strncpy(out, "/", out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }

    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out, path, len);
    out[len] = '\0';
}

static int resolve_include_path(const char *current_file, const char *include_target,
                                char *out, size_t out_size, const char *path, long line_no,
                                char *err, size_t err_size) {
    if (include_target[0] == '/') {
        if (strlen(include_target) >= out_size) {
            set_error_at(err, err_size, path, line_no, "Include path too long: %s", include_target);
            return 0;
        }
        strncpy(out, include_target, out_size - 1);
        out[out_size - 1] = '\0';
        return 1;
    }

    char base_dir[PATH_MAX] = {0};
    path_dirname(current_file, base_dir, sizeof(base_dir));

    int written = snprintf(out, out_size, "%s/%s", base_dir, include_target);
    if (written < 0 || (size_t)written >= out_size) {
        set_error_at(err, err_size, path, line_no, "Resolved include path too long: %s", include_target);
        return 0;
    }

    return 1;
}

static void canonicalize_path(const char *path, char *out, size_t out_size) {
    char resolved[PATH_MAX] = {0};
    if (realpath(path, resolved) != NULL) {
        strncpy(out, resolved, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }

    strncpy(out, path, out_size - 1);
    out[out_size - 1] = '\0';
}

static int include_stack_push(IncludeStack *stack, const char *path, const char *source_path, long source_line,
                              char *err, size_t err_size) {
    const char *error_path = source_path != NULL ? source_path : path;
    long error_line = source_path != NULL ? source_line : 0;

    if (stack->depth >= INCLUDE_MAX_DEPTH) {
        set_error_at(err, err_size, error_path, error_line,
                     "Maximum include depth exceeded (%d) while including '%s'",
                     INCLUDE_MAX_DEPTH, path);
        return 0;
    }

    char canonical[PATH_MAX] = {0};
    canonicalize_path(path, canonical, sizeof(canonical));

    for (int i = 0; i < stack->depth; i++) {
        if (strcmp(stack->paths[i], canonical) == 0) {
            set_error_at(err, err_size, error_path, error_line,
                         "Include cycle detected for '%s'", canonical);
            return 0;
        }
    }

    strncpy(stack->paths[stack->depth], canonical, sizeof(stack->paths[stack->depth]) - 1);
    stack->paths[stack->depth][sizeof(stack->paths[stack->depth]) - 1] = '\0';
    stack->depth++;
    return 1;
}

static void include_stack_pop(IncludeStack *stack) {
    if (stack->depth > 0) {
        stack->depth--;
    }
}

static int load_journal_file(Ledger *ledger, const char *path, LoadContext *ctx, int is_include,
                             const char *source_path, long source_line,
                             char *err, size_t err_size) {
    if (!include_stack_push(&ctx->stack, path, source_path, source_line, err, err_size)) {
        return 0;
    }

    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        if (errno == ENOENT) {
            if (is_include || ctx->strict) {
                set_error_at(err, err_size, source_path != NULL ? source_path : path,
                             source_path != NULL ? source_line : 0,
                             "Included file not found: %s", path);
                include_stack_pop(&ctx->stack);
                return 0;
            }
            include_stack_pop(&ctx->stack);
            return 1;
        }
        set_error_at(err, err_size, source_path != NULL ? source_path : path,
                     source_path != NULL ? source_line : 0,
                     "Cannot open '%s': %s", path, strerror(errno));
        include_stack_pop(&ctx->stack);
        return 0;
    }

    char line[1024];
    Transaction current;
    transaction_init(&current);
    int has_current = 0;
    long txn_start_line = 0;
    long line_no = 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        line_no++;
        trim_right(line);
        char *trimmed = trim_left(line);

        if (*trimmed == '\0' || *trimmed == ';' || *trimmed == '#') {
            continue;
        }

        int indented = (line[0] == ' ' || line[0] == '\t');

        if (!indented && is_iso_date_prefix(trimmed)) {
            if (has_current) {
                finalize_transaction(ledger, &current);
                if (ctx->strict && !validate_transaction_strict(ledger, &current, path, txn_start_line, err, err_size)) {
                    transaction_free(&current);
                    fclose(fp);
                    include_stack_pop(&ctx->stack);
                    return 0;
                }
                if (!ledger_add_transaction_copy(ledger, &current, err, err_size)) {
                    transaction_free(&current);
                    fclose(fp);
                    include_stack_pop(&ctx->stack);
                    return 0;
                }
                transaction_free(&current);
                transaction_init(&current);
                txn_start_line = 0;
            }

            has_current = 1;
            txn_start_line = line_no;
            strncpy(current.date, trimmed, 10);
            current.date[10] = '\0';

            char *description = trimmed + 10;
            description = trim_left(description);
            if (*description == '\0') {
                strncpy(current.description, "(no description)", sizeof(current.description) - 1);
            } else {
                strncpy(current.description, description, sizeof(current.description) - 1);
            }
            continue;
        }

        if (!indented) {
            char include_target[PATH_MAX] = {0};
            int include_rc = parse_include_target(trimmed, path, line_no, include_target, sizeof(include_target), err, err_size);
            if (include_rc < 0) {
                transaction_free(&current);
                fclose(fp);
                include_stack_pop(&ctx->stack);
                return 0;
            }
            if (include_rc > 0) {
                if (has_current) {
                    finalize_transaction(ledger, &current);
                    if (ctx->strict && !validate_transaction_strict(ledger, &current, path, txn_start_line, err, err_size)) {
                        transaction_free(&current);
                        fclose(fp);
                        include_stack_pop(&ctx->stack);
                        return 0;
                    }
                    if (!ledger_add_transaction_copy(ledger, &current, err, err_size)) {
                        transaction_free(&current);
                        fclose(fp);
                        include_stack_pop(&ctx->stack);
                        return 0;
                    }
                    transaction_free(&current);
                    transaction_init(&current);
                    has_current = 0;
                    txn_start_line = 0;
                }

                char include_path[PATH_MAX] = {0};
                if (!resolve_include_path(path, include_target, include_path, sizeof(include_path), path, line_no, err, err_size)) {
                    transaction_free(&current);
                    fclose(fp);
                    include_stack_pop(&ctx->stack);
                    return 0;
                }

                if (!load_journal_file(ledger, include_path, ctx, 1, path, line_no, err, err_size)) {
                    transaction_free(&current);
                    fclose(fp);
                    include_stack_pop(&ctx->stack);
                    return 0;
                }
                continue;
            }
        }

        if (indented && has_current) {
            Posting posting;
            if (!parse_posting_line(trimmed, &posting)) {
                if (ctx->strict) {
                    set_error_at(err, err_size, path, line_no, "Invalid posting line");
                    transaction_free(&current);
                    fclose(fp);
                    include_stack_pop(&ctx->stack);
                    return 0;
                }
                continue;
            }
            if (!transaction_add_posting(&current, &posting, err, err_size)) {
                transaction_free(&current);
                fclose(fp);
                include_stack_pop(&ctx->stack);
                return 0;
            }
            continue;
        }

        if (ctx->strict) {
            set_error_at(err, err_size, path, line_no, "Unrecognized or out-of-place directive: %s", trimmed);
            transaction_free(&current);
            fclose(fp);
            include_stack_pop(&ctx->stack);
            return 0;
        }
        /* Unknown directive; ignore in non-strict mode. */
    }

    if (has_current) {
        finalize_transaction(ledger, &current);
        if (ctx->strict && !validate_transaction_strict(ledger, &current, path, txn_start_line, err, err_size)) {
            transaction_free(&current);
            fclose(fp);
            include_stack_pop(&ctx->stack);
            return 0;
        }
        if (!ledger_add_transaction_copy(ledger, &current, err, err_size)) {
            transaction_free(&current);
            fclose(fp);
            include_stack_pop(&ctx->stack);
            return 0;
        }
    }

    transaction_free(&current);
    fclose(fp);
    include_stack_pop(&ctx->stack);
    return 1;
}

int ledger_load_journal(Ledger *ledger, const char *path, char *err, size_t err_size) {
    LedgerLoadOptions options;
    memset(&options, 0, sizeof(options));
    return ledger_load_journal_ex(ledger, path, &options, err, err_size);
}

int ledger_load_journal_ex(Ledger *ledger, const char *path, const LedgerLoadOptions *options, char *err, size_t err_size) {
    if (ledger == NULL || path == NULL) {
        set_error(err, err_size, "Invalid journal load arguments");
        return 0;
    }

    LoadContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    if (options != NULL) {
        ctx.strict = options->strict ? 1 : 0;
    }

    return load_journal_file(ledger, path, &ctx, 0, NULL, 0, err, err_size);
}

int ledger_append_transaction(const char *path, const Transaction *txn, char *err, size_t err_size) {
    if (path == NULL || txn == NULL) {
        set_error(err, err_size, "Invalid append arguments");
        return 0;
    }

    FILE *fp = fopen(path, "a+");
    if (fp == NULL) {
        set_error(err, err_size, "Cannot open '%s' for append: %s", path, strerror(errno));
        return 0;
    }

    if (fseek(fp, 0, SEEK_END) == 0) {
        long size = ftell(fp);
        if (size > 0) {
            if (fseek(fp, -1L, SEEK_END) == 0) {
                int last = fgetc(fp);
                if (last != '\n') {
                    fputc('\n', fp);
                }
            }
        }
        fseek(fp, 0, SEEK_END);
    }

    if (fprintf(fp, "%s %s\n", txn->date, txn->description) < 0) {
        set_error(err, err_size, "Failed writing transaction header");
        fclose(fp);
        return 0;
    }

    for (size_t i = 0; i < txn->postings_count; i++) {
        const Posting *posting = &txn->postings[i];
        if (posting->has_amount) {
            if (posting->commodity[0] != '\0') {
                if (fprintf(fp, "    %-30s %12.2f %s\n", posting->account, posting->amount, posting->commodity) < 0) {
                    set_error(err, err_size, "Failed writing posting line");
                    fclose(fp);
                    return 0;
                }
            } else {
                if (fprintf(fp, "    %-30s %12.2f\n", posting->account, posting->amount) < 0) {
                    set_error(err, err_size, "Failed writing posting line");
                    fclose(fp);
                    return 0;
                }
            }
        } else {
            if (fprintf(fp, "    %s\n", posting->account) < 0) {
                set_error(err, err_size, "Failed writing posting line");
                fclose(fp);
                return 0;
            }
        }
    }

    if (fputc('\n', fp) == EOF) {
        set_error(err, err_size, "Failed finalizing append");
        fclose(fp);
        return 0;
    }

    fclose(fp);
    return 1;
}

static int reserve_balances(AccountBalance **balances, size_t *capacity, size_t needed, char *err, size_t err_size) {
    if (needed <= *capacity) {
        return 1;
    }

    size_t new_capacity = *capacity == 0 ? 16 : *capacity * 2;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }

    AccountBalance *new_items = realloc(*balances, new_capacity * sizeof(AccountBalance));
    if (new_items == NULL) {
        set_error(err, err_size, "Out of memory while computing balances");
        return 0;
    }

    *balances = new_items;
    *capacity = new_capacity;
    return 1;
}

static int compare_balances(const void *a, const void *b) {
    const AccountBalance *left = (const AccountBalance *)a;
    const AccountBalance *right = (const AccountBalance *)b;
    int account_cmp = strcmp(left->name, right->name);
    if (account_cmp != 0) {
        return account_cmp;
    }
    return strcmp(left->commodity, right->commodity);
}

int ledger_compute_balances(const Ledger *ledger, AccountBalance **out_balances, size_t *out_count, char *err, size_t err_size) {
    if (ledger == NULL || out_balances == NULL || out_count == NULL) {
        set_error(err, err_size, "Invalid balance computation arguments");
        return 0;
    }

    AccountBalance *balances = NULL;
    size_t count = 0;
    size_t capacity = 0;

    for (size_t i = 0; i < ledger->count; i++) {
        const Transaction *txn = &ledger->transactions[i];
        for (size_t j = 0; j < txn->postings_count; j++) {
            const Posting *posting = &txn->postings[j];
            if (!posting->has_amount) {
                continue;
            }

            char resolved_commodity[16] = {0};
            if (posting->commodity[0] != '\0') {
                strncpy(resolved_commodity, posting->commodity, sizeof(resolved_commodity) - 1);
            } else if (ledger->default_commodity[0] != '\0') {
                strncpy(resolved_commodity, ledger->default_commodity, sizeof(resolved_commodity) - 1);
            }

            size_t index = count;
            for (size_t k = 0; k < count; k++) {
                if (strcmp(balances[k].name, posting->account) == 0 &&
                    strcmp(balances[k].commodity, resolved_commodity) == 0) {
                    index = k;
                    break;
                }
            }

            if (index == count) {
                if (!reserve_balances(&balances, &capacity, count + 1, err, err_size)) {
                    free(balances);
                    return 0;
                }
                memset(&balances[index], 0, sizeof(AccountBalance));
                strncpy(balances[index].name, posting->account, sizeof(balances[index].name) - 1);
                if (resolved_commodity[0] != '\0') {
                    strncpy(balances[index].commodity, resolved_commodity, sizeof(balances[index].commodity) - 1);
                }
                count++;
            }

            balances[index].amount += posting->amount;
        }
    }

    qsort(balances, count, sizeof(AccountBalance), compare_balances);

    *out_balances = balances;
    *out_count = count;
    return 1;
}

void ledger_free_balances(AccountBalance *balances) {
    free(balances);
}
