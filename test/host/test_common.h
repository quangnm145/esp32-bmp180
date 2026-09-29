#pragma once
#include <stdio.h>

static int g_checks, g_failures;

#define CHECK(cond, ...) do { \
    g_checks++; \
    if (!(cond)) { \
        g_failures++; \
        printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

#define TEST_SUMMARY(name) ( \
    printf("%s: %d checks, %d failures -> %s\n", name, g_checks, g_failures, \
           g_failures ? "FAIL" : "PASS"), \
    g_failures ? 1 : 0)
