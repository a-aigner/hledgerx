# hledgerx Documentation

This document is the complete reference for `hledgerx` as it exists in this repository.

- Project type: C application (`ncurses` TUI + CLI reports)
- Primary goal: hledger-inspired terminal ledger workflow
- Key differentiator: add new transactions from inside the running TUI via `n`

## 1. Quick Start

### 1.1 Build

```bash
make
```

### 1.2 Run tests

```bash
make test
```

### 1.3 Launch TUI

```bash
./hledgerx ledger.journal
```

If `ledger.journal` does not exist, it is created.

### 1.4 Common CLI reports

```bash
./hledgerx balance ledger.journal
./hledgerx register ledger.journal
./hledgerx accounts ledger.journal
./hledgerx print ledger.journal
```

## 2. Command Model

`hledgerx` supports two invocation styles:

1. Implicit UI mode:

```bash
./hledgerx <journal-file>
```

2. Explicit command mode:

```bash
./hledgerx <command> [<journal-file>] [account-prefix] [options]
```

## 3. Commands

### 3.1 `ui`

Open interactive TUI.

Aliases: none.

```bash
./hledgerx ui ledger.journal
```

### 3.2 `balance`

Show account balances.

Aliases: `bal`.

```bash
./hledgerx balance ledger.journal
./hledgerx bal ledger.journal assets:
```

### 3.3 `register`

Show posting register with running balance.

Aliases: `reg`.

```bash
./hledgerx register ledger.journal
./hledgerx reg ledger.journal expenses:
```

### 3.4 `accounts`

List account names.

Aliases: `acct`, `accts`.

```bash
./hledgerx accounts ledger.journal
./hledgerx acct ledger.journal assets:
```

### 3.5 `print`

Print normalized journal entries.

Aliases: `p`.

```bash
./hledgerx print ledger.journal
./hledgerx p ledger.journal assets:
```

## 4. Options

### 4.1 Global parsing/selection options

These are parsed before report execution.

- `-f`, `--file <journal-file>`: explicit journal path
- `--begin YYYY-MM-DD` / `-b YYYY-MM-DD`: start date filter (inclusive)
- `--end YYYY-MM-DD` / `-e YYYY-MM-DD`: end date filter (inclusive)
- `--depth N` / `-d N`: account depth collapse (0 = full depth)
- `--strict`: strict journal validation during load

Example:

```bash
./hledgerx register -f ledger.journal assets: -b 2026-03-01 -e 2026-03-31 -d 2 --strict
```

### 4.2 Tree/flat display options

- `--tree`
- `--flat`

Supported commands:

- `balance`
- `accounts`

`--tree` is not accepted by `register` or `print`.

### 4.3 Period options

- `--daily` / `-D`
- `--monthly` / `-M`
- `--quarterly` / `-Q`
- `--yearly` / `-Y`

Supported commands:

- `balance`
- `register`

### 4.4 Output format options

- `--output-format text|csv`
- `--csv` (shortcut for `--output-format csv`)

Supported commands:

- `balance`
- `register`

### 4.5 Help

```bash
./hledgerx --help
```

## 5. Option Compatibility Matrix

`Y` means supported, `N` means rejected.

| Command   | begin/end/depth | tree/flat | period flags | csv output | strict |
|-----------|------------------|-----------|--------------|------------|--------|
| `ui`      | N                | N         | N            | N          | Y      |
| `balance` | Y                | Y         | Y            | Y          | Y      |
| `register`| Y                | N         | Y            | Y          | Y      |
| `accounts`| Y                | Y         | N            | N          | Y      |
| `print`   | Y                | N         | N            | N          | Y      |

## 6. Journal Format

## 6.1 Transaction header

```text
YYYY-MM-DD Description text
```

Date must be `YYYY-MM-DD`.

### 6.2 Posting lines

Indented lines belong to the current transaction:

```text
    account:name                 12.34 EUR
    other:account
```

Supported forms:

- account + amount + optional commodity
- account only (amount missing)

### 6.3 Comments

Ignored if line starts with `;` or `#`.

### 6.4 Include directive

Supported at top-level (non-indented):

```text
include sub/extra.journal
include "./nested/other.journal"
```

Rules:

- Relative include paths resolve from the including file's directory.
- Include cycles are detected and rejected.
- Missing included files are rejected.

## 7. Strict Mode (`--strict`)

Strict mode validates load-time journal correctness and fails fast with `file:line` errors.

Strict checks include:

1. Unknown or out-of-place top-level directives are rejected.
2. Invalid posting line syntax is rejected.
3. Transaction must have at least two postings.
4. No posting may remain without amount.
5. Transaction must balance per commodity.

### 7.1 Strict mode example: unknown directive

`bad.journal`:

```text
not_a_directive 123
```

Command:

```bash
./hledgerx balance bad.journal --strict
```

Behavior: exits with load failure and `bad.journal:1: ...`.

### 7.2 Strict mode example: unbalanced transaction

```text
2026-03-20 Broken
    assets:cash   10.00 EUR
    income:salary -8.00 EUR
```

Command:

```bash
./hledgerx balance broken.journal --strict
```

Behavior: load failure with unbalanced transaction error at transaction start line.

## 8. Balancing Rules (Non-Strict)

When loading transactions in non-strict mode:

1. If exactly one posting has missing amount, it is auto-balanced as negative sum of known amounts.
2. If commodity is missing on that auto-balanced posting, default commodity may be inferred.
3. If there are multiple missing amounts, unresolved postings remain unresolved for strict checks (and may be skipped in reports requiring numeric amounts).

## 9. TUI Guide

Launch:

```bash
./hledgerx ledger.journal
# or
./hledgerx ui ledger.journal
```

Views:

- Journal
- Balance
- Help

Keybindings:

- `q`: quit
- `TAB`: cycle views
- `j`: journal view
- `b`: balance view
- `h`: help view
- `UP/DOWN`: move journal selection
- `n`: add transaction without leaving app

### 9.1 Add transaction flow (`n`)

Form fields:

1. Date (`YYYY-MM-DD`, default today)
2. Description (empty description cancels)
3. Debit account (default `assets:cash`)
4. Credit account (default `expenses:misc`)
5. Amount (must be positive number)
6. Commodity (default inferred from ledger or `EUR`)

Behavior:

- Creates 2 postings: debit `+amount`, credit `-amount`
- Appends to journal file immediately
- Updates in-memory ledger immediately
- Returns to Journal view without restarting app

## 10. `balance` Output

### 10.1 Text output (default)

Non-periodic example:

```bash
./hledgerx balance ledger.journal
```

Output shape:

1. account lines
2. separator line
3. `total` line(s) per commodity

### 10.2 Tree output

```bash
./hledgerx balance ledger.journal --tree
```

Behavior:

- Hierarchical indentation
- Parent account rollups included

### 10.3 Periodic output

```bash
./hledgerx balance ledger.journal --monthly
./hledgerx balance ledger.journal --quarterly
./hledgerx balance ledger.journal --yearly
```

Behavior:

- Grouped by `period <key>` sections
- Each section has rows + totals

Period keys:

- Daily: `YYYY-MM-DD`
- Monthly: `YYYY-MM`
- Quarterly: `YYYY-QN`
- Yearly: `YYYY`

### 10.4 CSV output

```bash
./hledgerx balance ledger.journal --csv
./hledgerx balance ledger.journal --monthly --output-format csv
```

Headers:

- Non-periodic: `account,amount,commodity,row_type`
- Periodic: `period,account,amount,commodity,row_type`

`row_type` values:

- `account`
- `total`

## 11. `register` Output

### 11.1 Text output (default)

```bash
./hledgerx register ledger.journal
```

Columns:

- date
- description
- account
- change
- running balance

### 11.2 Periodic output

```bash
./hledgerx register ledger.journal --daily
./hledgerx register ledger.journal --yearly
```

Behavior:

- Output split into `period <key>` sections
- Running balances reset per period

### 11.3 CSV output

```bash
./hledgerx register ledger.journal --csv
./hledgerx register ledger.journal --daily --csv
```

Headers:

- Non-periodic: `date,description,account,change,balance,commodity`
- Periodic: `period,date,description,account,change,balance,commodity`

## 12. `accounts` Output

```bash
./hledgerx accounts ledger.journal
./hledgerx accounts ledger.journal assets:
./hledgerx accounts ledger.journal --tree
```

Behavior:

- Lists unique accounts after filters
- `--tree` adds parent path nodes and indented display

## 13. `print` Output

```bash
./hledgerx print ledger.journal
./hledgerx print ledger.journal assets:
```

Behavior:

- Reprints normalized transactions
- Filters by date/account prefix
- Includes only matching postings per transaction

## 14. Filtering Behavior

### 14.1 Date filters

`--begin` and `--end` are inclusive and compared lexicographically on ISO date strings.

Example:

```bash
./hledgerx balance ledger.journal --begin 2026-03-01 --end 2026-03-31
```

### 14.2 Account prefix filter

Optional positional `account-prefix` after journal path.

Example:

```bash
./hledgerx register ledger.journal expenses:
```

### 14.3 Depth filter

`--depth N` truncates accounts to first `N` segments by `:`.

Example:

- `assets:broker:etf` with `--depth 2` becomes `assets:broker`

## 15. Example End-to-End Session

Create `demo.journal`:

```journal
2026-03-01 Grocery
    expenses:food                    22.50 EUR
    assets:cash

2026-03-02 Salary
    assets:bank                    2500.00 EUR
    income:salary

2026-07-01 Bonus
    assets:cash                       1.00 EUR
    income:salary
```

Run:

```bash
./hledgerx balance demo.journal --quarterly
./hledgerx register demo.journal --monthly
./hledgerx balance demo.journal --csv
./hledgerx register demo.journal --daily --csv
./hledgerx accounts demo.journal --tree
./hledgerx print demo.journal assets:
./hledgerx demo.journal
```

Inside TUI:

1. Press `n`
2. Fill transaction form
3. Press Enter through fields
4. New transaction appears immediately in journal list

## 16. Error Cases and Diagnostics

### 16.1 Unsupported option scope

Examples:

```bash
./hledgerx print ledger.journal --monthly
./hledgerx accounts ledger.journal --csv
```

Both return explicit scope error messages.

### 16.2 Bad date input

```bash
./hledgerx balance ledger.journal --begin 03-01-2026
```

Returns invalid date format error.

### 16.3 Invalid date range

```bash
./hledgerx balance ledger.journal --begin 2026-04-01 --end 2026-03-01
```

Returns range validation error.

## 17. Testing Strategy

Current project test harness:

- Script: `tests/run_tests.sh`
- Entrypoint: `make test`

Covered scenarios include:

1. Includes (`include`, relative paths, quoted paths)
2. Include cycle detection
3. Missing include errors
4. Command aliases (`bal`, `reg`, `acct`)
5. Date/depth filters
6. Tree outputs
7. Periodic modes (`daily`, `monthly`, `quarterly`, `yearly`)
8. CSV outputs
9. Strict mode failures (unknown directives, unbalanced entries, one-posting transaction)

## 18. Current Limitations

This is a strong foundation, not full hledger parity yet.

Known boundaries:

1. Parser supports a practical subset of journal syntax (not full hledger syntax universe).
2. No GUI; TUI only.
3. Focused command set (`ui`, `balance`, `register`, `accounts`, `print`).
4. No import/export adapters beyond journal and report text/csv output.

## 19. Practical Command Cookbook

### 19.1 Most common

```bash
./hledgerx ledger.journal
./hledgerx balance ledger.journal
./hledgerx register ledger.journal
```

### 19.2 Financial period review

```bash
./hledgerx balance ledger.journal --monthly
./hledgerx balance ledger.journal --quarterly
./hledgerx register ledger.journal --yearly
```

### 19.3 Filtered auditing

```bash
./hledgerx register ledger.journal expenses: --begin 2026-01-01 --end 2026-12-31
./hledgerx balance ledger.journal assets: --depth 2 --tree
```

### 19.4 CSV integration

```bash
./hledgerx balance ledger.journal --csv > balance.csv
./hledgerx register ledger.journal --monthly --csv > register_monthly.csv
```

### 19.5 Strict validation pass

```bash
./hledgerx balance ledger.journal --strict
```

Use this in CI/pre-commit to catch malformed ledger entries early.
