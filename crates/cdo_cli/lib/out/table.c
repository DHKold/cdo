/**
 * table.c - Table dynamic zone.
 *
 * Renders tabular data with aligned columns and separators.
 * Respects terminal width for truncation.
 */
#include "out_internal.h"
#include "../../api/cli_errors.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ========================================================================= */
/* Internal: Table row linked list node.                                      */
/* ========================================================================= */

typedef struct CliTableRow {
    char**              cells;
    struct CliTableRow* next;
} CliTableRow;

/* ========================================================================= */
/* Internal: Table state structure.                                           */
/* ========================================================================= */

struct CliTable {
    CliOutCtx*   ctx;
    char**       headers;
    int          col_count;
    CliTableRow* rows_head;
    CliTableRow* rows_tail;
    int          row_count;
};

/* ========================================================================= */
/* Internal: Duplicate a row of strings.                                      */
/* ========================================================================= */

static char** dup_cells(const char** cells, int count) {
    char** copy = (char**)malloc(sizeof(char*) * (size_t)count);
    if (!copy) return NULL;

    for (int i = 0; i < count; i++) {
        copy[i] = cells[i] ? strdup(cells[i]) : strdup("");
    }
    return copy;
}

/* ========================================================================= */
/* Internal: Free a row of strings.                                          */
/* ========================================================================= */

static void free_cells(char** cells, int count) {
    if (!cells) return;
    for (int i = 0; i < count; i++) {
        free(cells[i]);
    }
    free(cells);
}

/* ========================================================================= */
/* Public API                                                                */
/* ========================================================================= */

CliTable* cli_out_table_create(CliOutCtx* ctx, const char** headers, int col_count) {
    if (!ctx || !headers || col_count <= 0) return NULL;

    CliTable* table = (CliTable*)malloc(sizeof(CliTable));
    if (!table) return NULL;

    table->ctx = ctx;
    table->col_count = col_count;
    table->headers = dup_cells(headers, col_count);
    if (!table->headers) {
        free(table);
        return NULL;
    }
    table->rows_head = NULL;
    table->rows_tail = NULL;
    table->row_count = 0;

    return table;
}

void cli_out_table_add_row(CliTable* table, const char** cells) {
    if (!table || !cells) return;

    CliTableRow* row = (CliTableRow*)malloc(sizeof(CliTableRow));
    if (!row) return;

    row->cells = dup_cells(cells, table->col_count);
    row->next = NULL;

    if (!row->cells) {
        free(row);
        return;
    }

    if (table->rows_tail) {
        table->rows_tail->next = row;
    } else {
        table->rows_head = row;
    }
    table->rows_tail = row;
    table->row_count++;
}

void cli_out_table_render(CliTable* table, FILE* stream) {
    if (!table || !stream) return;

    int col_count = table->col_count;

    /* Calculate column widths */
    int* widths = (int*)calloc((size_t)col_count, sizeof(int));
    if (!widths) return;

    for (int i = 0; i < col_count; i++) {
        widths[i] = (int)strlen(table->headers[i]);
    }

    CliTableRow* row = table->rows_head;
    while (row) {
        for (int i = 0; i < col_count; i++) {
            int len = (int)strlen(row->cells[i]);
            if (len > widths[i]) widths[i] = len;
        }
        row = row->next;
    }

    /* Print header row */
    for (int i = 0; i < col_count; i++) {
        if (i > 0) fputs("  ", stream);
        fprintf(stream, "%-*s", widths[i], table->headers[i]);
    }
    fputc('\n', stream);

    /* Print separator */
    for (int i = 0; i < col_count; i++) {
        if (i > 0) fputs("  ", stream);
        for (int j = 0; j < widths[i]; j++) {
            fputc('-', stream);
        }
    }
    fputc('\n', stream);

    /* Print data rows */
    row = table->rows_head;
    while (row) {
        for (int i = 0; i < col_count; i++) {
            if (i > 0) fputs("  ", stream);
            fprintf(stream, "%-*s", widths[i], row->cells[i]);
        }
        fputc('\n', stream);
        row = row->next;
    }

    free(widths);
}

void cli_out_table_destroy(CliTable* table) {
    if (!table) return;

    /* Free all rows */
    CliTableRow* row = table->rows_head;
    while (row) {
        CliTableRow* next = row->next;
        free_cells(row->cells, table->col_count);
        free(row);
        row = next;
    }

    /* Free headers */
    free_cells(table->headers, table->col_count);
    free(table);
}
