#pragma once
#include "cJSON.h"
#include <stdbool.h>
void wifi_control_status_json(cJSON *out);
bool wifi_control_command(const cJSON *request,cJSON *reply,const char **error);
