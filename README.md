# hledgerx (C + TUI)

`hledgerx` is a terminal-first ledger prototype inspired by `hledger`.

## What is implemented

- Journal file loading (`YYYY-MM-DD Description` + posting lines)
- Journal list view in TUI
- Balance view in TUI
- In-app transaction creation with `n` (no app restart)
- Appending new transactions directly to the same journal file
- `include` directive support for multi-file journals
- CLI report commands: `accounts`, `balance`, `print`, `register`

## Build

```bash
make
```

## Test

```bash
make test
```

## Run (UI)

```bash
./hledgerx ledger.journal
```

If `ledger.journal` does not exist, it will be created.

You can also run explicit UI mode:

```bash
./hledgerx ui ledger.journal
```

## CLI reports

```bash
# list known accounts
./hledgerx accounts ledger.journal

# account balances (all accounts)
./hledgerx balance ledger.journal

# account balances filtered by prefix
./hledgerx balance ledger.journal assets:

# normalized journal output
./hledgerx print ledger.journal

# running register (all postings)
./hledgerx register ledger.journal

# running register filtered by account prefix
./hledgerx register ledger.journal expenses:

# command aliases
./hledgerx bal ledger.journal
./hledgerx reg ledger.journal assets:
./hledgerx acct ledger.journal
```

## Query options (report commands)

`--begin`, `--end`, `--depth` can be combined with `accounts`, `balance`, `register`, and `print`.
`--daily`/`--monthly`/`--quarterly`/`--yearly` are supported by `balance` and `register`.
`--output-format csv`/`--csv` are supported by `balance` and `register`.

```bash
# choose file explicitly
./hledgerx balance -f ledger.journal

# date filter
./hledgerx register ledger.journal expenses: --begin 2026-03-01 --end 2026-03-31

# collapse account depth
./hledgerx balance ledger.journal --depth 1

# short flag aliases
./hledgerx register -f ledger.journal expenses: -b 2026-03-01 -e 2026-03-31 -d 2

# tree/flat output (balance/accounts)
./hledgerx balance ledger.journal --tree
./hledgerx accounts ledger.journal --tree
./hledgerx balance ledger.journal --flat

# periodic reports (balance/register)
./hledgerx balance ledger.journal --monthly
./hledgerx register ledger.journal --daily
./hledgerx balance ledger.journal --quarterly
./hledgerx register ledger.journal --yearly
./hledgerx reg ledger.journal -D
./hledgerx bal ledger.journal -M
./hledgerx bal ledger.journal -Q
./hledgerx reg ledger.journal -Y

# csv output (balance/register)
./hledgerx balance ledger.journal --output-format csv
./hledgerx register ledger.journal --csv

# strict parser mode (also rejects unbalanced/invalid transactions)
./hledgerx balance ledger.journal --strict
```

## Include directive

```journal
; main.journal
include finances/cash.journal
include \"./investments.journal\"
```

Relative includes are resolved from the including file's directory.

## TUI shortcuts

- `n`: new transaction form
- `j`: journal view
- `b`: balance view
- `h`: help view
- `TAB`: cycle views
- `q`: quit

## Example journal

```journal
2026-03-01 Grocery
    expenses:food                    22.50 EUR
    assets:cash

2026-03-02 Salary
    assets:bank                    2500.00 EUR
    income:salary
```
