#ifndef HLEDGERX_TUI_H
#define HLEDGERX_TUI_H

#include <stddef.h>

#include "ledger.h"

int tui_run(Ledger *ledger, const char *journal_path, char *err, size_t err_size);

#endif
