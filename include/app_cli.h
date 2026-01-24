#ifndef APP_CLI_H
#define APP_CLI_H

#include "app.h"

void app_config_init(AppConfig *config);
ErrorCode app_parse_arguments(int argc, char *argv[], char **path, AppConfig *config);
void app_config_resolve_protocol(AppConfig *config);

#endif
