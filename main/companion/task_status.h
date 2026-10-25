#pragma once
#include "cJSON.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct { int mode; char target[49]; } task_scope_t;
typedef struct {
    int working, waiting, done;
    uint32_t completion_seq;
    task_scope_t scope;
    bool detailed;
} task_counts_t;
bool task_scope_parse(cJSON *object, task_scope_t *scope);
void task_scope_json(cJSON *parent,const char *key,const task_scope_t *scope);
bool task_scope_equal(const task_scope_t *a,const task_scope_t *b);
bool task_counts_parse(cJSON *request,int state,task_counts_t *counts);
int task_counts_state(const task_counts_t *counts);
bool task_counts_equal(const task_counts_t *a,const task_counts_t *b);
bool task_catalog_update(cJSON *request);
void task_catalog_json(cJSON *reply);
uint32_t task_catalog_revision(void);
