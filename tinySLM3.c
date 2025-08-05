// Refactored PPM model using hash tables instead of large arrays
// Reduces memory usage drastically and supports higher MAX_ORDER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>
#include "uthash.h"

#define U8  uint8_t
#define U32 uint32_t

#define ASCII_START 32
#define ASCII_END   126
#define ASCII_RANGE (ASCII_END - ASCII_START + 1)
#define MAX_ORDER 4

#define MAX_CONTEXT_LEN MAX_ORDER

typedef struct {
    char context[MAX_CONTEXT_LEN + 1]; // null-terminated context string
    U8 next_char;
    U32 count;
    UT_hash_handle hh;
} CountEntry;

typedef struct {
    char context[MAX_CONTEXT_LEN + 1]; // context string
    U32 total;
    UT_hash_handle hh;
} TotalEntry;

CountEntry *count_table = NULL;
TotalEntry *total_table = NULL;
U8 history[MAX_ORDER];

void init_model() {
    memset(history, ' ', MAX_ORDER);
}

void free_model() {
    CountEntry *ce, *tmp;
    HASH_ITER(hh, count_table, ce, tmp) {
        HASH_DEL(count_table, ce);
        free(ce);
    }

    TotalEntry *te, *ttmp;
    HASH_ITER(hh, total_table, te, ttmp) {
        HASH_DEL(total_table, te);
        free(te);
    }
}

void get_context_str(int order, char *out) {
    for (int i = 0; i < order; i++) {
        out[i] = history[MAX_ORDER - order + i];
    }
    out[order] = '\0';
}

int normalize_char(int ch) {
    if (ch < ASCII_START || ch > ASCII_END) {
        if (ch == '\n' || ch == '\t') return ' ';
        return -1;
    }
    return ch;
}

void update_model(U8 ch) {
    char ctx[MAX_CONTEXT_LEN + 1];
    for (int order = 0; order <= MAX_ORDER; order++) {
        get_context_str(order, ctx);

        // Update count
        CountEntry key;
        strncpy(key.context, ctx, MAX_CONTEXT_LEN + 1);
        key.next_char = ch;

        CountEntry *entry;
        HASH_FIND(hh, count_table, &key, sizeof(CountEntry) - sizeof(UT_hash_handle), entry);
        if (entry) {
            entry->count++;
        } else {
            entry = (CountEntry*)malloc(sizeof(CountEntry));
            strcpy(entry->context, ctx);
            entry->next_char = ch;
            entry->count = 1;
            HASH_ADD(hh, count_table, context, sizeof(CountEntry) - sizeof(UT_hash_handle), entry);
        }

        // Update total
        TotalEntry *total;
        HASH_FIND_STR(total_table, ctx, total);
        if (total) {
            total->total++;
        } else {
            total = (TotalEntry*)malloc(sizeof(TotalEntry));
            strcpy(total->context, ctx);
            total->total = 1;
            HASH_ADD_STR(total_table, context, total);
        }
    }

    // Update history
    memmove(history, history + 1, MAX_ORDER - 1);
    history[MAX_ORDER - 1] = ch;
}

U8 sample_character() {
    char ctx[MAX_CONTEXT_LEN + 1];

    for (int order = MAX_ORDER; order >= 0; order--) {
        get_context_str(order, ctx);
        TotalEntry *total;
        HASH_FIND_STR(total_table, ctx, total);
        if (!total || total->total == 0) continue;

        double escape_prob = 0.1 + (0.2 * order) / (total->total + 1);
        if (order > 0 && (double)rand() / RAND_MAX < escape_prob) continue;

        // Sample
        U32 r = rand() % total->total;
        U32 cumulative = 0;
        CountEntry *e;
        for (e = count_table; e != NULL; e = e->hh.next) {
            if (strcmp(e->context, ctx) == 0) {
                cumulative += e->count;
                if (r < cumulative) return e->next_char;
            }
        }
    }

    // Fallback
    const char fallback[] = "etaoinshrdlcumwfgypbvkjxqz ETAOINSHRDLCUMWFGYPBVKJXQZ.,!?;:";
    return fallback[rand() % (sizeof(fallback) - 1)];
}

void train_model(FILE *fp, size_t file_size) {
    char buffer[8192];
    size_t bytes_read, progress = 0;
    int ch;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        for (size_t i = 0; i < bytes_read; i++) {
            ch = normalize_char(buffer[i]);
            if (ch < 0) continue;
            update_model((U8)ch);
            if (++progress % 10000 == 0) {
                printf("Progress: %.2f%%\r", 100.0 * progress / file_size);
                fflush(stdout);
            }
        }
    }
    printf("\nTraining complete.\n");
}

void generate_response(const char *prompt) {
    memset(history, ' ', MAX_ORDER);
    for (int i = 0; prompt[i]; i++) {
        int ch = normalize_char(prompt[i]);
        if (ch >= 0) update_model((U8)ch);
    }

    printf("\nPrompt: \"%s\"\n%s", prompt, prompt);
    int generated = 0, sentence_len = 0;
    for (int i = 0; i < 500 && generated < 300; i++) {
        U8 next = sample_character();
        if (next < ASCII_START || next > ASCII_END) continue;
        printf("%c", next);
        update_model(next);
        generated++;
        if ((next == '.' || next == '?' || next == '!') && sentence_len > 20 && rand() % 3 == 0) break;
        if (++sentence_len > 200) {
            printf(".");
            break;
        }
    }
    printf("\n");
}

size_t get_file_size(const char *filename) {
    struct stat st;
    if (stat(filename, &st) != 0) return 0;
    return st.st_size;
}

void interactive_mode() {
    char prompt[512];
    printf("\n=== Interactive Mode ===\n");
    while (1) {
        printf("\nPrompt: ");
        fflush(stdout);
        if (!fgets(prompt, sizeof(prompt), stdin)) break;
        prompt[strcspn(prompt, "\n")] = '\0';
        if (strcmp(prompt, "quit") == 0) break;
        if (strlen(prompt) == 0) continue;
        generate_response(prompt);
    }
}

int main(int argc, char *argv[]) {
    srand((unsigned int)time(NULL));
    if (argc < 2) {
        printf("Usage: %s <training_file> [prompt]\n", argv[0]);
        return 1;
    }

    FILE *fp = fopen(argv[1], "rb");
    if (!fp) {
        perror("Error opening file");
        return 1;
    }

    size_t file_size = get_file_size(argv[1]);
    if (file_size == 0) {
        fprintf(stderr, "File empty or inaccessible\n");
        fclose(fp);
        return 1;
    }

    init_model();
    train_model(fp, file_size);
    fclose(fp);

    if (argc >= 3) generate_response(argv[2]);
    else interactive_mode();

    free_model();
    printf("Goodbye!\n");
    return 0;
}

