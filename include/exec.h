#ifndef EXEC_H
#define EXEC_H
void execute_command(char *args[], char *inputFile, char *outputFile, char *errorFile);
void execute_pipeline(char *cmd);
#endif
