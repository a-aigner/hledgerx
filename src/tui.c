#include "tui.h"

#include <ctype.h>
#include <ncurses.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef enum {
    VIEW_JOURNAL = 0,
    VIEW_BALANCE = 1,
    VIEW_HELP = 2
} ViewMode;

typedef struct {
    Ledger *ledger;
    const char *journal_path;
    ViewMode view;
    int selected;
    int top;
    char status[256];
} AppState;

static void set_status(AppState *state, const char *fmt, ...) {
    if (state == NULL || fmt == NULL) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    vsnprintf(state->status, sizeof(state->status), fmt, args);
    va_end(args);
}

static const char *view_name(ViewMode view) {
    switch (view) {
        case VIEW_JOURNAL:
            return "Journal";
        case VIEW_BALANCE:
            return "Balance";
        case VIEW_HELP:
            return "Help";
        default:
            return "Unknown";
    }
}

static void draw_header(const AppState *state) {
    mvhline(0, 0, ' ', COLS);
    attron(A_BOLD);
    mvprintw(0, 1, "hledgerx | %s", view_name(state->view));
    attroff(A_BOLD);
    mvprintw(0, COLS - 32 > 0 ? COLS - 32 : 1, "journal: %s", state->journal_path);
}

static void draw_footer(const AppState *state) {
    int status_row = LINES - 2;
    int hint_row = LINES - 1;

    mvhline(status_row, 0, ' ', COLS);
    mvhline(hint_row, 0, ' ', COLS);

    mvprintw(status_row, 1, "%s", state->status[0] != '\0' ? state->status : "Ready");
    mvprintw(hint_row, 1, "q quit | TAB switch view | j journal | b balance | h help | n new transaction");
}

static void ensure_journal_selection_visible(AppState *state, int visible_rows) {
    if (state->ledger->count == 0) {
        state->selected = 0;
        state->top = 0;
        return;
    }

    if (state->selected < 0) {
        state->selected = 0;
    }
    if ((size_t)state->selected >= state->ledger->count) {
        state->selected = (int)state->ledger->count - 1;
    }

    if (state->top < 0) {
        state->top = 0;
    }

    if (state->selected < state->top) {
        state->top = state->selected;
    }

    if (state->selected >= state->top + visible_rows) {
        state->top = state->selected - visible_rows + 1;
    }

    if (state->top < 0) {
        state->top = 0;
    }
}

static void draw_journal_view(AppState *state) {
    int start_row = 2;
    int end_row = LINES - 3;
    int visible_rows = end_row - start_row + 1;

    mvprintw(1, 1, "Date        Description");

    if (visible_rows <= 0) {
        return;
    }

    if (state->ledger->count == 0) {
        mvprintw(start_row, 1, "No transactions yet. Press 'n' to add one.");
        return;
    }

    ensure_journal_selection_visible(state, visible_rows);

    for (int row = start_row; row <= end_row; row++) {
        mvhline(row, 0, ' ', COLS);
    }

    for (int i = 0; i < visible_rows; i++) {
        int idx = state->top + i;
        if ((size_t)idx >= state->ledger->count) {
            break;
        }

        const Transaction *txn = &state->ledger->transactions[idx];
        int row = start_row + i;
        if (idx == state->selected) {
            attron(A_REVERSE);
        }

        char first_posting[64] = {0};
        if (txn->postings_count > 0 && txn->postings[0].has_amount) {
            const Posting *p = &txn->postings[0];
            if (p->commodity[0] != '\0') {
                snprintf(first_posting, sizeof(first_posting), "%10.2f %s", p->amount, p->commodity);
            } else {
                snprintf(first_posting, sizeof(first_posting), "%10.2f", p->amount);
            }
        }

        mvprintw(row, 1, "%-10s  %-50.50s %s", txn->date, txn->description, first_posting);

        if (idx == state->selected) {
            attroff(A_REVERSE);
        }
    }
}

static void draw_balance_view(AppState *state) {
    AccountBalance *balances = NULL;
    size_t count = 0;
    char err[256] = {0};

    int start_row = 2;
    int end_row = LINES - 3;

    mvprintw(1, 1, "Account                              Balance");

    if (!ledger_compute_balances(state->ledger, &balances, &count, err, sizeof(err))) {
        mvprintw(start_row, 1, "Could not compute balances: %s", err);
        return;
    }

    if (count == 0) {
        mvprintw(start_row, 1, "No balances to display.");
        ledger_free_balances(balances);
        return;
    }

    for (int row = start_row; row <= end_row; row++) {
        mvhline(row, 0, ' ', COLS);
    }

    for (int i = 0; i <= end_row - start_row; i++) {
        if ((size_t)i >= count) {
            break;
        }

        const AccountBalance *b = &balances[i];
        if (b->commodity[0] != '\0') {
            mvprintw(start_row + i, 1, "%-34.34s %14.2f %s", b->name, b->amount, b->commodity);
        } else {
            mvprintw(start_row + i, 1, "%-34.34s %14.2f", b->name, b->amount);
        }
    }

    ledger_free_balances(balances);
}

static void draw_help_view(void) {
    mvprintw(2, 1, "Keyboard shortcuts:");
    mvprintw(4, 3, "n     Add new transaction (without leaving TUI)");
    mvprintw(5, 3, "j     Switch to journal view");
    mvprintw(6, 3, "b     Switch to balance view");
    mvprintw(7, 3, "TAB   Cycle views");
    mvprintw(8, 3, "UP/DOWN  Move selection in journal");
    mvprintw(9, 3, "q     Quit");
    mvprintw(11, 1, "Journal format supported:");
    mvprintw(12, 3, "YYYY-MM-DD Description");
    mvprintw(13, 3, "    assets:cash           20.00 EUR");
    mvprintw(14, 3, "    expenses:food        -20.00 EUR");
}

static void today_iso(char out[11]) {
    time_t now = time(NULL);
    struct tm tm_now;
#if defined(_POSIX_THREAD_SAFE_FUNCTIONS)
    localtime_r(&now, &tm_now);
#else
    struct tm *tmp = localtime(&now);
    tm_now = *tmp;
#endif
    strftime(out, 11, "%Y-%m-%d", &tm_now);
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
        } else if (!isdigit((unsigned char)s[i])) {
            return 0;
        }
    }

    return 1;
}

static int parse_positive_amount(const char *value, double *out_amount) {
    if (value == NULL || out_amount == NULL) {
        return 0;
    }

    char *end = NULL;
    double amount = strtod(value, &end);
    if (end == value) {
        return 0;
    }

    while (*end != '\0' && isspace((unsigned char)*end)) {
        end++;
    }
    if (*end != '\0') {
        return 0;
    }

    if (amount < 0.0) {
        amount = -amount;
    }

    if (amount == 0.0) {
        return 0;
    }

    *out_amount = amount;
    return 1;
}

static int prompt_input(WINDOW *win, int row, const char *label, char *out, size_t out_size, const char *default_value) {
    int label_x = 2;
    int input_x = 22;
    int max_len = getmaxx(win) - input_x - 3;
    if (max_len < 8) {
        max_len = 8;
    }

    mvwprintw(win, row, label_x, "%-18s", label);
    mvwprintw(win, row, input_x, "%-*s", max_len, "");
    if (default_value != NULL && default_value[0] != '\0') {
        mvwprintw(win, row, input_x, "%s", default_value);
    }

    wmove(win, row, input_x);
    wrefresh(win);

    char buffer[256] = {0};
    echo();
    curs_set(1);
    int rc = mvwgetnstr(win, row, input_x, buffer, (int)((out_size - 1) < (size_t)max_len ? (out_size - 1) : (size_t)max_len));
    noecho();
    curs_set(0);

    if (rc == ERR) {
        return 0;
    }

    if ((unsigned char)buffer[0] == 27) {
        return -1;
    }

    if (buffer[0] == '\0' && default_value != NULL) {
        strncpy(out, default_value, out_size - 1);
        out[out_size - 1] = '\0';
        return 1;
    }

    strncpy(out, buffer, out_size - 1);
    out[out_size - 1] = '\0';
    return 1;
}

static void add_transaction_flow(AppState *state) {
    int height = 14;
    int width = COLS > 90 ? 90 : COLS - 4;
    if (width < 60) {
        width = 60;
    }

    int start_y = (LINES - height) / 2;
    int start_x = (COLS - width) / 2;

    WINDOW *form = newwin(height, width, start_y, start_x);
    if (form == NULL) {
        set_status(state, "Unable to open form window");
        return;
    }

    box(form, 0, 0);
    mvwprintw(form, 0, 2, " New Transaction ");
    mvwprintw(form, 1, 2, "Press Enter to accept field value. Leave description empty to cancel.");
    wrefresh(form);

    char date[16] = {0};
    char description[256] = {0};
    char debit_account[128] = {0};
    char credit_account[128] = {0};
    char amount_str[64] = {0};
    char commodity[16] = {0};

    char default_date[11] = {0};
    today_iso(default_date);

    int ok = 1;
    int rc = prompt_input(form, 3, "Date (YYYY-MM-DD)", date, sizeof(date), default_date);
    if (rc <= 0) {
        ok = 0;
    }
    if (ok) {
        rc = prompt_input(form, 4, "Description", description, sizeof(description), "");
        if (rc <= 0 || description[0] == '\0') {
            ok = 0;
        }
    }
    if (ok) {
        rc = prompt_input(form, 5, "Debit account", debit_account, sizeof(debit_account), "assets:cash");
        if (rc <= 0 || debit_account[0] == '\0') {
            ok = 0;
        }
    }
    if (ok) {
        rc = prompt_input(form, 6, "Credit account", credit_account, sizeof(credit_account), "expenses:misc");
        if (rc <= 0 || credit_account[0] == '\0') {
            ok = 0;
        }
    }
    if (ok) {
        rc = prompt_input(form, 7, "Amount", amount_str, sizeof(amount_str), "0.00");
        if (rc <= 0) {
            ok = 0;
        }
    }
    if (ok) {
        const char *default_commodity = state->ledger->default_commodity[0] != '\0' ? state->ledger->default_commodity : "EUR";
        rc = prompt_input(form, 8, "Commodity", commodity, sizeof(commodity), default_commodity);
        if (rc <= 0) {
            ok = 0;
        }
    }

    if (!ok) {
        delwin(form);
        touchwin(stdscr);
        refresh();
        set_status(state, "New transaction cancelled");
        return;
    }

    if (!is_iso_date(date)) {
        delwin(form);
        touchwin(stdscr);
        refresh();
        set_status(state, "Invalid date format. Use YYYY-MM-DD");
        return;
    }

    double amount = 0.0;
    if (!parse_positive_amount(amount_str, &amount)) {
        delwin(form);
        touchwin(stdscr);
        refresh();
        set_status(state, "Invalid amount. Use a positive number, e.g. 12.50");
        return;
    }

    Transaction txn;
    transaction_init(&txn);
    strncpy(txn.date, date, sizeof(txn.date) - 1);
    strncpy(txn.description, description, sizeof(txn.description) - 1);

    Posting debit;
    memset(&debit, 0, sizeof(debit));
    strncpy(debit.account, debit_account, sizeof(debit.account) - 1);
    debit.amount = amount;
    debit.has_amount = 1;
    if (commodity[0] != '\0') {
        strncpy(debit.commodity, commodity, sizeof(debit.commodity) - 1);
    }

    Posting credit;
    memset(&credit, 0, sizeof(credit));
    strncpy(credit.account, credit_account, sizeof(credit.account) - 1);
    credit.amount = -amount;
    credit.has_amount = 1;
    if (commodity[0] != '\0') {
        strncpy(credit.commodity, commodity, sizeof(credit.commodity) - 1);
    }

    char err[256] = {0};
    if (!transaction_add_posting(&txn, &debit, err, sizeof(err)) ||
        !transaction_add_posting(&txn, &credit, err, sizeof(err))) {
        transaction_free(&txn);
        delwin(form);
        touchwin(stdscr);
        refresh();
        set_status(state, "Could not build transaction: %s", err);
        return;
    }

    if (!ledger_append_transaction(state->journal_path, &txn, err, sizeof(err))) {
        transaction_free(&txn);
        delwin(form);
        touchwin(stdscr);
        refresh();
        set_status(state, "Could not save transaction: %s", err);
        return;
    }

    if (!ledger_add_transaction_copy(state->ledger, &txn, err, sizeof(err))) {
        transaction_free(&txn);
        delwin(form);
        touchwin(stdscr);
        refresh();
        set_status(state, "Saved to file, but memory update failed: %s", err);
        return;
    }

    transaction_free(&txn);

    if (state->ledger->default_commodity[0] == '\0' && commodity[0] != '\0') {
        strncpy(state->ledger->default_commodity, commodity, sizeof(state->ledger->default_commodity) - 1);
    }

    state->selected = (int)state->ledger->count - 1;
    set_status(state, "Transaction added: %s %.2f", commodity, amount);

    delwin(form);
    touchwin(stdscr);
    refresh();
}

int tui_run(Ledger *ledger, const char *journal_path, char *err, size_t err_size) {
    if (ledger == NULL || journal_path == NULL) {
        if (err != NULL && err_size > 0) {
            snprintf(err, err_size, "Invalid TUI arguments");
        }
        return 0;
    }

    AppState state;
    memset(&state, 0, sizeof(state));
    state.ledger = ledger;
    state.journal_path = journal_path;
    state.view = VIEW_JOURNAL;

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    set_status(&state, "Loaded %zu transaction(s)", ledger->count);

    int running = 1;
    while (running) {
        erase();
        draw_header(&state);

        switch (state.view) {
            case VIEW_JOURNAL:
                draw_journal_view(&state);
                break;
            case VIEW_BALANCE:
                draw_balance_view(&state);
                break;
            case VIEW_HELP:
                draw_help_view();
                break;
            default:
                break;
        }

        draw_footer(&state);
        refresh();

        int ch = getch();
        switch (ch) {
            case 'q':
            case 'Q':
                running = 0;
                break;
            case '\t':
                state.view = (ViewMode)(((int)state.view + 1) % 3);
                break;
            case 'j':
            case 'J':
                state.view = VIEW_JOURNAL;
                break;
            case 'b':
            case 'B':
                state.view = VIEW_BALANCE;
                break;
            case 'h':
            case 'H':
                state.view = VIEW_HELP;
                break;
            case KEY_UP:
                if (state.view == VIEW_JOURNAL) {
                    state.selected--;
                }
                break;
            case KEY_DOWN:
                if (state.view == VIEW_JOURNAL) {
                    state.selected++;
                }
                break;
            case 'n':
            case 'N':
                add_transaction_flow(&state);
                state.view = VIEW_JOURNAL;
                break;
            case KEY_RESIZE:
                break;
            default:
                break;
        }
    }

    endwin();
    return 1;
}
