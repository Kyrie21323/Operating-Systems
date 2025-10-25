#ifndef PARSE_H
#define PARSE_H

int parse_command(char *cmd, char *args[], char **inputFile, char **outputFile, char **errorFile, int isPipeline);
int validate_pipeline(char *cmd);

#endif
