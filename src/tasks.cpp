#include <cstdarg>
#include <cstring>
#include <stdio.h>
namespace Task {
    char last[256];
    bool open = false;
    void succeed();
    void start(const char* task) {
        if (open) succeed();
        strncpy(last,task,255);
        printf("%s\n", task);
        open = true;
        fflush(stdout);
    }
    void step(const char* step) {
        printf("%s ",last);
        if (step[0] != '\0') {
            printf("(%s)",step);
        }
        printf("\n");
        // printf("\033[1B\r");
        fflush(stdout);
    } 
    void succeed() {
        if (!open) return;
        printf("[OK]\n");;
        open = false;
        fflush(stdout);
    }
    void fail() {
        if (!open) return;
        printf("[failed!]\n");
        open = false;
        fflush(stdout);
    }
    void warn(const char* msg) {
        if (!open) return;
        printf("[warn] %s\n",msg);
        open = false;
        fflush(stdout);
    }
    void fail(const char* msg,...) {
        va_list list;
        va_start(list, msg);
        fail();
        printf("    ");
        vprintf(msg,list);
        va_end(list);
        fflush(stdout);
    }
}