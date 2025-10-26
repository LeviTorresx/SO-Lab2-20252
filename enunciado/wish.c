#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_ARGS 100
#define MAX_PATHS 100

const char error_message[] = "An error has occurred\n";
char *paths[MAX_PATHS];
int path_count = 0;

// ----------------------------------------
// Utilidades
// ----------------------------------------
void print_error()
{
    write(STDERR_FILENO, error_message, strlen(error_message));
}

char *trim(char *str)
{
    while (*str == ' ' || *str == '\t' || *str == '\n')
        str++;
    if (*str == 0)
        return str;
    char *end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n'))
        end--;
    *(end + 1) = '\0';
    return str;
}

// ----------------------------------------
// Paths
// ----------------------------------------
void init_path()
{
    path_count = 1;
    paths[0] = strdup("/bin");
}

void builtin_path(char **args)
{
    for (int i = 0; i < path_count; i++)
        free(paths[i]);
    path_count = 0;
    for (int i = 1; args[i] != NULL; i++)
        paths[path_count++] = strdup(args[i]);
}

// ----------------------------------------
// cd
// ----------------------------------------
void builtin_cd(char **args)
{
    if (args[1] == NULL || args[2] != NULL || chdir(args[1]) != 0)
        print_error();
}

// ----------------------------------------
// comando externo
// ----------------------------------------
void execute_external(char **args, char *redirect_file)
{
    if (path_count == 0)
    {
        print_error();
        return;
    }

    pid_t pid = fork();
    if (pid == 0)
    {
        // Redirección de salida
        if (redirect_file != NULL)
        {
            int fd = open(redirect_file, O_WRONLY | O_CREAT | O_APPEND, 0666);
            if (fd < 0)
            {
                print_error();
                exit(1);
            }
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }

        // Buscar ejecutable en paths
        for (int i = 0; i < path_count; i++)
        {
            char fullpath[256];
            snprintf(fullpath, sizeof(fullpath), "%s/%s", paths[i], args[0]);
            if (access(fullpath, X_OK) == 0)
            {
                execv(fullpath, args);
            }
        }
        print_error();
        exit(1);
    }
    else if (pid > 0)
    {
        waitpid(pid, NULL, 0);
    }
    else
    {
        print_error();
    }
}

// ----------------------------------------
// linea completa
// ----------------------------------------
int process_command(char *line)
{
    char *cmds[MAX_ARGS];
    int cmd_count = 0;
    int should_exit = 0;

    // Dividir por &
    char *rest = line, *token;
    while ((token = strsep(&rest, "&")) != NULL)
    {
        char *clean = trim(token);
        if (*clean != '\0')
            cmds[cmd_count++] = strdup(clean);
    }

    pid_t children[MAX_ARGS];
    int child_index = 0;

    for (int i = 0; i < cmd_count; i++)
    {
        char *command = cmds[i];

        // Buscar redirección
        char *redir_pos = strchr(command, '>');
        char *redirect_file = NULL;

        if (redir_pos != NULL)
        {
            *redir_pos = '\0';
            redirect_file = trim(redir_pos + 1);

            // Validar redirección correcta
            if (strchr(redirect_file, '>') != NULL ||
                strlen(redirect_file) == 0 ||
                strchr(redirect_file, ' ') != NULL)
            {
                print_error();
                free(cmds[i]);
                continue;
            }
        }

        // Tokenizar argumentos
        char *args[MAX_ARGS];
        int arg_index = 0;
        char *arg = strtok(command, " \t\n");
        while (arg != NULL && arg_index < MAX_ARGS - 1)
        {
            args[arg_index++] = arg;
            arg = strtok(NULL, " \t\n");
        }
        args[arg_index] = NULL;

        if (args[0] == NULL)
        {
            print_error();
            free(cmds[i]);
            continue;
        }

        // Comandos internos
        if (strcmp(args[0], "exit") == 0)
        {
            if (args[1] != NULL)
            {
                print_error();
            }
            else
            {
                should_exit = 1; //marca pero no sale
            }
        }
        else if (strcmp(args[0], "cd") == 0)
        {
            builtin_cd(args);
        }
        else if (strcmp(args[0], "path") == 0)
        {
            builtin_path(args);
        }
        else
        {
            // Ejecutar comando externo posiblemente paralelo
            pid_t pid = fork();
            if (pid == 0)
            {
                if (redirect_file != NULL)
                {
                    int fd = open(redirect_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                    if (fd < 0)
                    {
                        print_error();
                        exit(1);
                    }
                    dup2(fd, STDOUT_FILENO);
                    dup2(fd, STDERR_FILENO);
                    close(fd);
                }
                for (int j = 0; j < path_count; j++)
                {
                    char fullpath[256];
                    snprintf(fullpath, sizeof(fullpath), "%s/%s", paths[j], args[0]);
                    if (access(fullpath, X_OK) == 0)
                        execv(fullpath, args);
                }
                print_error();
                exit(1);
            }
            else if (pid > 0)
            {
                children[child_index++] = pid;
            }
            else
            {
                print_error();
            }
        }

        free(cmds[i]);
    }

    // Esperar todos los procesos hijos de esta línea
    for (int i = 0; i < child_index; i++)
        waitpid(children[i], NULL, 0);

    return should_exit;
}

// ----------------------------------------
// main batch
// ----------------------------------------
int main(int argc, char *argv[])
{
    init_path();

    FILE *input = stdin;
    if (argc > 2)
    {
        print_error();
        exit(1);
    }
    else if (argc == 2)
    {
        input = fopen(argv[1], "r");
        if (!input)
        {
            print_error();
            exit(1);
        }
    }

    char *line = NULL;
    size_t len = 0;
    int should_exit = 0;

    while (1)
    {
        if (argc == 1)
        {
            printf("wish> ");
            fflush(stdout);
        }

        if (getline(&line, &len, input) == -1)
            break;

        char *clean = trim(line);
        if (*clean == '\0')
            continue;

        should_exit = process_command(clean);

        // Siempre esperar a todos los procesos hijos
        while (waitpid(-1, NULL, 0) > 0)
            ;

        if (should_exit)
            break;
    }

    free(line);
    for (int i = 0; i < path_count; i++)
        free(paths[i]);
    return 0;
}
