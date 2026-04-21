// TODO:
// - run the programs and see their exit code
// - run the compiler with -Werror for C compiler to know that it has done everything right

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <test_directory>\n", argv[0]);
        return 1;
    }

    const char *test_dir = argv[1];
    DIR *dir = opendir(test_dir);
    if (!dir) {
        perror("Could not open test directory");
        return 1;
    }

    struct dirent *entry;
    int passed = 0;
    int failed = 0;

    printf("Running tests in directory: %s\n", test_dir);
    printf("Convention: Files containing 'fail' must return a non-zero exit code.\n");
    printf("-------------------------------------------------\n");

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char filepath[1024];
        snprintf(filepath, sizeof(filepath), "%s/%s", test_dir, entry->d_name);

        struct stat path_stat;
        if (stat(filepath, &path_stat) != 0 || !S_ISREG(path_stat.st_mode)) {
            continue;
        }

        int is_negative_test = (strstr(entry->d_name, "fail") != NULL);

        char command[2048];
        snprintf(command, sizeof(command), "./bin/kjude %s > /dev/null 2>&1", filepath);

        printf("Testing %-30s ... ", entry->d_name);
        
        int status = system(command);
        int exit_code = WEXITSTATUS(status);

        if (is_negative_test) {
            if (exit_code != 0) {
                printf("\033[32mPASS\033[0m\n");
                passed++;
            } else {
                printf("\033[31mFAIL\033[0m (Expected failure, but compiled successfully)\n");
                failed++;
            }
        } else {
            if (exit_code == 0) {
                printf("\033[32mPASS\033[0m\n");
                passed++;
            } else {
                printf("\033[31mFAIL\033[0m (Expected success, got %d)\n", exit_code);
                failed++;
            }
        }
    }

    closedir(dir);

    printf("-------------------------------------------------\n");
    printf("Results:\n%d Passed\n%d Failed\n", passed, failed);

    return failed > 0 ? 1 : 0;
}
