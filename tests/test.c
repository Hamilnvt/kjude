// TODO:
// - run the programs and see their exit code
// - run the compiler with -Werror for C compiler to know that it has done everything right

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>
#include <unistd.h>

#define DA_IMPLEMENTATION
#include "dynamic_arrays.h"

typedef struct 
{
    char *filepath;
    char *filename;
    bool should_fail;
    bool result;
    int exit_code;
} Test;

typedef struct
{
    Test *items;
    size_t count;
    size_t capacity;
} Tests;

typedef struct
{
    Test *tests;
    size_t count;
    _Atomic size_t next_index;
} TestQueue;

typedef struct
{
    TestQueue *queue;
} WorkerArg;

int run_cmd(char **cmd)
{
    if (cmd == NULL) return -1;

    int status;
    pid_t pid = fork();
    switch (pid) {
        case -1:
            perror("fork");
            exit(EXIT_FAILURE);
        case 0:
            freopen("/dev/null", "w", stdout);
            freopen("/dev/null", "w", stderr);
            execvp(*cmd, cmd);
            fprintf(stderr, "ERROR: could not run cmd '%s'\n", *cmd);
            exit(1);
        default:
            waitpid(pid, &status, 0);
            return WEXITSTATUS(status);
    }
}

void remove_output(Test *test)
{
    char *filename = strdup(test->filename);
    filename[strlen(filename)-3] = '\0';

    size_t output_size = (8+strlen(filename))*sizeof(char);
    char *output_filename = malloc(output_size);
    snprintf(output_filename, output_size, "output_%s", filename);
    remove(output_filename);
    free(output_filename);
}

void* worker(void *arg)
{
    WorkerArg *warg = (WorkerArg*)arg;
    TestQueue *q = warg->queue;

    while (1) {
        size_t i = atomic_fetch_add(&q->next_index, 1);
        if (i >= q->count) break;

        Test *test = &q->tests[i];

        char *cmd[] = {"./bin/kjude", test->filepath, NULL};
        test->exit_code = run_cmd(cmd);
        test->result = test->should_fail ? (test->exit_code != 0) : (test->exit_code == 0);

        remove_output(test);
    }

    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <test_directory>\n", argv[0]);
        return 1;
    }

    const char *test_dir = argv[1];
    DIR *dir = opendir(test_dir);
    if (!dir) {
        perror("Could not open directory");
        return 1;
    }

    printf("Running tests in directory: %s\n", test_dir);
    printf("-------------------------------------------------\n");
    fflush(stdout);

    struct dirent *entry;
    Tests tests = {0};

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char filepath[1024];
        snprintf(filepath, sizeof(filepath), "%s/%s", test_dir, entry->d_name);

        struct stat path_stat;
        if (stat(filepath, &path_stat) != 0 || !S_ISREG(path_stat.st_mode)) {
            continue;
        }

        Test test = {
            .filepath = strdup(filepath),
            .filename = strdup(entry->d_name),
            .should_fail = (strstr(entry->d_name, "fail") != NULL),
        };

        da_push(&tests, test);
    }
    closedir(dir);

    long num_threads = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_threads < 1) num_threads = 4;

    TestQueue queue = {
        .tests = tests.items,
        .count = tests.count,
        .next_index = 0,
    };

    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    WorkerArg *wargs = malloc(num_threads * sizeof(WorkerArg));

    for (long t = 0; t < num_threads; t++) {
        wargs[t].queue = &queue;
        pthread_create(&threads[t], NULL, worker, &wargs[t]);
    }

    for (long t = 0; t < num_threads; t++) {
        pthread_join(threads[t], NULL);
    }

    free(threads);
    free(wargs);

    int passed = 0;
    int failed = 0;
    for (size_t i = 0; i < tests.count; i++) {
        Test *test = &tests.items[i];

        printf("Testing %-30s ... ", test->filename);

        if (test->result) {
            printf("\033[32mPASS\033[0m\n");
            passed++;
        } else {
            if (test->should_fail) {
                printf("\033[31mFAIL\033[0m (Expected failure, but compiled successfully)\n");
            } else {
                printf("\033[31mFAIL\033[0m (Expected success, but got %d)\n", test->exit_code);
            }
            failed++;
        }
    }

    printf("-------------------------------------------------\n");
    printf("Results:\n%d Passed\n%d Failed\n", passed, failed);

    return failed > 0 ? 1 : 0;
}
