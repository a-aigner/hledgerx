#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ledger.h"
#include "reports.h"
#include "tui.h"

static int ensure_file_exists(const char *path) {
    FILE *fp = fopen(path, "a");
    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

static const char *canonical_command(const char *value) {
    if (strcmp(value, "ui") == 0) {
        return "ui";
    }
    if (strcmp(value, "balance") == 0 || strcmp(value, "bal") == 0) {
        return "balance";
    }
    if (strcmp(value, "register") == 0 || strcmp(value, "reg") == 0) {
        return "register";
    }
    if (strcmp(value, "print") == 0 || strcmp(value, "p") == 0) {
        return "print";
    }
    if (strcmp(value, "accounts") == 0 || strcmp(value, "accts") == 0 || strcmp(value, "acct") == 0) {
        return "accounts";
    }
    return NULL;
}

static int is_iso_date(const char *s) {
    if (s == NULL || strlen(s) != 10) {
        return 0;
    }

    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) {
            if (s[i] != '-') {
                return 0;
            }
        } else if (s[i] < '0' || s[i] > '9') {
            return 0;
        }
    }

    return 1;
}

static int parse_non_negative_int(const char *s, int *out) {
    if (s == NULL || out == NULL) {
        return 0;
    }

    char *end = NULL;
    long value = strtol(s, &end, 10);
    if (end == s || *end != '\0' || value < 0 || value > 99) {
        return 0;
    }

    *out = (int)value;
    return 1;
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s <journal-file>\n", prog);
    fprintf(stderr, "  %s <command> [<journal-file>] [account-prefix] [options]\n", prog);
    fprintf(stderr, "\nCommands:\n");
    fprintf(stderr, "  ui        Open interactive TUI\n");
    fprintf(stderr, "  balance   Print account balances (alias: bal)\n");
    fprintf(stderr, "  register  Print posting register (alias: reg)\n");
    fprintf(stderr, "  print     Print normalized journal (alias: p)\n");
    fprintf(stderr, "  accounts  List account names (aliases: acct, accts)\n");
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -f, --file <journal-file>\n");
    fprintf(stderr, "  --begin YYYY-MM-DD\n");
    fprintf(stderr, "  --end YYYY-MM-DD\n");
    fprintf(stderr, "  --depth N\n");
    fprintf(stderr, "  --tree | --flat\n");
    fprintf(stderr, "  --daily | --monthly | --quarterly | --yearly\n");
    fprintf(stderr, "  --output-format text|csv\n");
    fprintf(stderr, "  --csv\n");
    fprintf(stderr, "  --strict\n");
    fprintf(stderr, "  -b YYYY-MM-DD   (alias for --begin)\n");
    fprintf(stderr, "  -e YYYY-MM-DD   (alias for --end)\n");
    fprintf(stderr, "  -d N            (alias for --depth)\n");
    fprintf(stderr, "  -D              (alias for --daily)\n");
    fprintf(stderr, "  -M              (alias for --monthly)\n");
    fprintf(stderr, "  -Q              (alias for --quarterly)\n");
    fprintf(stderr, "  -Y              (alias for --yearly)\n");
    fprintf(stderr, "  -h, --help\n");
}

int main(int argc, char **argv) {
    const char *command = "ui";
    int command_set = 0;
    const char *journal_path = NULL;

    ReportQuery query;
    memset(&query, 0, sizeof(query));
    int tree_option_set = 0;
    int strict_mode = 0;

    const char *positionals[4] = {0};
    size_t positional_count = 0;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }

        if (strcmp(arg, "-f") == 0 || strcmp(arg, "--file") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value for %s\n", arg);
                print_usage(argv[0]);
                return 1;
            }
            journal_path = argv[++i];
            continue;
        }

        if (strcmp(arg, "--begin") == 0 || strcmp(arg, "-b") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value for %s\n", arg);
                return 1;
            }
            query.begin_date = argv[++i];
            continue;
        }

        if (strcmp(arg, "--end") == 0 || strcmp(arg, "-e") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value for %s\n", arg);
                return 1;
            }
            query.end_date = argv[++i];
            continue;
        }

        if (strcmp(arg, "--depth") == 0 || strcmp(arg, "-d") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value for %s\n", arg);
                return 1;
            }
            if (!parse_non_negative_int(argv[++i], &query.depth)) {
                fprintf(stderr, "Invalid depth value. Use an integer >= 0\n");
                return 1;
            }
            continue;
        }

        if (strcmp(arg, "--tree") == 0) {
            query.tree = 1;
            tree_option_set = 1;
            continue;
        }

        if (strcmp(arg, "--flat") == 0) {
            query.tree = 0;
            tree_option_set = 1;
            continue;
        }

        if (strcmp(arg, "--strict") == 0) {
            strict_mode = 1;
            continue;
        }

        if (strcmp(arg, "--daily") == 0 || strcmp(arg, "-D") == 0) {
            query.period = REPORT_PERIOD_DAILY;
            continue;
        }

        if (strcmp(arg, "--monthly") == 0 || strcmp(arg, "-M") == 0) {
            query.period = REPORT_PERIOD_MONTHLY;
            continue;
        }

        if (strcmp(arg, "--quarterly") == 0 || strcmp(arg, "-Q") == 0) {
            query.period = REPORT_PERIOD_QUARTERLY;
            continue;
        }

        if (strcmp(arg, "--yearly") == 0 || strcmp(arg, "-Y") == 0) {
            query.period = REPORT_PERIOD_YEARLY;
            continue;
        }

        if (strcmp(arg, "--csv") == 0) {
            query.output = REPORT_OUTPUT_CSV;
            continue;
        }

        if (strcmp(arg, "--output-format") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value for --output-format\n");
                return 1;
            }
            const char *format = argv[++i];
            if (strcmp(format, "text") == 0) {
                query.output = REPORT_OUTPUT_TEXT;
            } else if (strcmp(format, "csv") == 0) {
                query.output = REPORT_OUTPUT_CSV;
            } else {
                fprintf(stderr, "Invalid --output-format value: %s (expected text or csv)\n", format);
                return 1;
            }
            continue;
        }

        if (arg[0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", arg);
            print_usage(argv[0]);
            return 1;
        }

        const char *canonical = canonical_command(arg);
        if (!command_set && canonical != NULL) {
            command = canonical;
            command_set = 1;
            continue;
        }

        if (positional_count >= sizeof(positionals) / sizeof(positionals[0])) {
            fprintf(stderr, "Too many positional arguments\n");
            print_usage(argv[0]);
            return 1;
        }

        positionals[positional_count++] = arg;
    }

    if (query.begin_date != NULL && !is_iso_date(query.begin_date)) {
        fprintf(stderr, "Invalid --begin date format: %s (expected YYYY-MM-DD)\n", query.begin_date);
        return 1;
    }

    if (query.end_date != NULL && !is_iso_date(query.end_date)) {
        fprintf(stderr, "Invalid --end date format: %s (expected YYYY-MM-DD)\n", query.end_date);
        return 1;
    }

    if (query.begin_date != NULL && query.end_date != NULL && strcmp(query.begin_date, query.end_date) > 0) {
        fprintf(stderr, "Invalid date range: --begin must be <= --end\n");
        return 1;
    }

    size_t pos_idx = 0;
    if (journal_path == NULL) {
        if (positional_count == 0) {
            print_usage(argv[0]);
            return 1;
        }
        journal_path = positionals[pos_idx++];
    }

    size_t remaining = positional_count - pos_idx;
    if (strcmp(command, "ui") == 0) {
        if (remaining > 0) {
            fprintf(stderr, "Command 'ui' does not take an account prefix\n");
            print_usage(argv[0]);
            return 1;
        }
        if (query.account_prefix != NULL || query.begin_date != NULL || query.end_date != NULL ||
            query.depth != 0 || tree_option_set || query.period != REPORT_PERIOD_NONE ||
            query.output != REPORT_OUTPUT_TEXT) {
            fprintf(stderr, "Query options are not supported in UI mode\n");
            return 1;
        }
    } else if (strcmp(command, "balance") == 0 || strcmp(command, "register") == 0 || strcmp(command, "accounts") == 0) {
        if (remaining > 1) {
            fprintf(stderr, "Too many arguments for command '%s'\n", command);
            print_usage(argv[0]);
            return 1;
        }
        if (remaining == 1) {
            query.account_prefix = positionals[pos_idx];
        }
    } else if (strcmp(command, "print") == 0) {
        if (remaining > 1) {
            fprintf(stderr, "Too many arguments for command 'print'\n");
            print_usage(argv[0]);
            return 1;
        }
        if (remaining == 1) {
            query.account_prefix = positionals[pos_idx];
        }
    }

    if (tree_option_set && strcmp(command, "balance") != 0 && strcmp(command, "accounts") != 0) {
        fprintf(stderr, "--tree/--flat is only supported by 'balance' and 'accounts'\n");
        return 1;
    }

    if (query.period != REPORT_PERIOD_NONE &&
        strcmp(command, "balance") != 0 &&
        strcmp(command, "register") != 0) {
        fprintf(stderr, "--daily/--monthly/--quarterly/--yearly is only supported by 'balance' and 'register'\n");
        return 1;
    }

    if (query.output == REPORT_OUTPUT_CSV &&
        strcmp(command, "balance") != 0 &&
        strcmp(command, "register") != 0) {
        fprintf(stderr, "--output-format csv is only supported by 'balance' and 'register'\n");
        return 1;
    }

    if (!ensure_file_exists(journal_path)) {
        fprintf(stderr, "Cannot access journal '%s': %s\n", journal_path, strerror(errno));
        return 1;
    }

    Ledger ledger;
    ledger_init(&ledger);

    char err[256] = {0};
    LedgerLoadOptions load_options;
    memset(&load_options, 0, sizeof(load_options));
    load_options.strict = strict_mode;

    if (!ledger_load_journal_ex(&ledger, journal_path, &load_options, err, sizeof(err))) {
        fprintf(stderr, "Load failed: %s\n", err);
        ledger_free(&ledger);
        return 1;
    }

    int ok = 1;
    if (strcmp(command, "ui") == 0) {
        if (!tui_run(&ledger, journal_path, err, sizeof(err))) {
            fprintf(stderr, "TUI failed: %s\n", err);
            ok = 0;
        }
    } else if (strcmp(command, "balance") == 0) {
        if (!report_balance(&ledger, &query, stdout, err, sizeof(err))) {
            fprintf(stderr, "Balance report failed: %s\n", err);
            ok = 0;
        }
    } else if (strcmp(command, "print") == 0) {
        if (!report_print_journal(&ledger, &query, stdout, err, sizeof(err))) {
            fprintf(stderr, "Print report failed: %s\n", err);
            ok = 0;
        }
    } else if (strcmp(command, "register") == 0) {
        if (!report_register(&ledger, &query, stdout, err, sizeof(err))) {
            fprintf(stderr, "Register report failed: %s\n", err);
            ok = 0;
        }
    } else if (strcmp(command, "accounts") == 0) {
        if (!report_accounts(&ledger, &query, stdout, err, sizeof(err))) {
            fprintf(stderr, "Accounts report failed: %s\n", err);
            ok = 0;
        }
    }

    ledger_free(&ledger);
    return ok ? 0 : 1;
}
