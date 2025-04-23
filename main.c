#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#define MAX_ARGS 16
#define BUFFER_SIZE 1024

// Fonction pour parser une ligne de commande
int parse_line(char *s, char **argv[]) {
    int argc = 0;
    *argv = malloc((MAX_ARGS + 1) * sizeof(char*));

    if (*argv == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    char *token = s;
    while (*token != '\0' && argc < MAX_ARGS) {
        while (isspace(*token)) {
            token++;
        }
        char *start = token;
        while (*token != '\0' && !isspace(*token)) {
            token++;
        }

        size_t token_length = token - start;

        (*argv)[argc] = malloc((token_length + 1) * sizeof(char));
        if ((*argv)[argc] == NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        strncpy((*argv)[argc], start, token_length);
        (*argv)[argc][token_length] = '\0';

        argc++;
    }

    (*argv)[argc] = NULL;
    return argc;
}

// Fonction pour écrire une chaîne de caractères sur la sortie standard
void write_string(const char *str) {
    if (write(STDOUT_FILENO, str, strlen(str)) == -1) {
        perror("write");
        exit(EXIT_FAILURE);
    }
}

// Configurer le shell pour ignorer SIGINT
void ignore_sigint() {
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;  // Définir le handler sur SIG_IGN pour ignorer
    sa.sa_flags = 0;          // Pas de flags spéciaux
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
}

int main() {
    ignore_sigint(); // Ignorer SIGINT dans le shell

    char command[BUFFER_SIZE];
    char **argv, **argv2;
    int argc, argc2;
    int pipefd[2];

    while (1) {
        write_string("$ ");

        ssize_t bytesRead = read(STDIN_FILENO, command, sizeof(command));
        if (bytesRead == -1) {
            perror("read");
            exit(EXIT_FAILURE);
        }

        // retirer le caractère de fin de ligne
        size_t len = bytesRead;
        if (len > 0 && command[len - 1] == '\n') {
            command[len - 1] = '\0';
            len--;
        }

        if (len == 0) {
            continue;
        }

        if (strcmp(command, "exit") == 0) {
            write_string("Exiting...\n");
            exit(EXIT_SUCCESS);
        }

        argc = parse_line(command, &argv);

        if (argc > 1 && strcmp(argv[argc - 1], "|") == 0) {
            char second_command[BUFFER_SIZE];
            write_string("> ");
            if (read(STDIN_FILENO, second_command, sizeof(second_command)) == -1) {
                perror("read");
                continue;
            }

            // Retirer le caractère de fin de ligne pour la seconde commande
            size_t len2 = strlen(second_command);
            if (len2 > 0 && second_command[len2 - 1] == '\n') {
                second_command[len2 - 1] = '\0';
            }

            argc2 = parse_line(second_command, &argv2);

            if (pipe(pipefd) == -1) {
                perror("pipe");
                continue;
            }

            pid_t pid1 = fork();
            if (pid1 == 0) {
                struct sigaction sa_default;
                sa_default.sa_handler = SIG_DFL;
                sigaction(SIGINT, &sa_default, NULL);

                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[1]);
                argv[argc - 1] = NULL;
                execvp(argv[0], argv);
                perror("execvp");
                exit(EXIT_FAILURE);
            }

            pid_t pid2 = fork();
            if (pid2 == 0) {
                struct sigaction sa_default;
                sa_default.sa_handler = SIG_DFL;
                sigaction(SIGINT, &sa_default, NULL);

                close(pipefd[1]);
                dup2(pipefd[0], STDIN_FILENO);
                close(pipefd[0]);
                execvp(argv2[0], argv2);
                perror("execvp");
                exit(EXIT_FAILURE);
            }

            close(pipefd[0]);
            close(pipefd[1]);
            waitpid(pid1, NULL, 0);
            waitpid(pid2, NULL, 0);

            for (int i = 0; i < argc; i++) {
                free(argv[i]);
            }
            free(argv);
            for (int i = 0; i < argc2; i++) {
                free(argv2[i]);
            }
            free(argv2);
        } else {
            pid_t pid = fork();

            if (pid == 0) {
                struct sigaction sa_default;
                sa_default.sa_handler = SIG_DFL;
                sigaction(SIGINT, &sa_default, NULL);

                int red_index = -1;
                for (int i = 0; argv[i] != NULL; i++) {
                    if (strcmp(argv[i], ">") == 0) {
                        red_index = i;
                    }
                }

                if (red_index != -1 && argv[red_index + 1] != NULL && argv[red_index + 2] == NULL) {
                    char *output_file = argv[red_index + 1];

                // Ouvrir le fichier en écriture (créer s'il n'existe pas, tronquer s'il existe)
                    int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                    if (fd == -1) {
                        perror("open");
                        exit(EXIT_FAILURE);
                    }

                // Rediriger la sortie standard vers le fichier
                    if (dup2(fd, STDOUT_FILENO) == -1) {
                        perror("dup2");
                        exit(EXIT_FAILURE);
                    }
                    close(fd);

                // Supprimer les arguments de redirection
                    free(argv[red_index]);
                    free(argv[red_index + 1]);
                    argv[red_index] = NULL;
                    argv[red_index + 1] = NULL;
                }

            // Exécuter la commande
                execvp(argv[0], argv);
                perror("execvp");
                exit(EXIT_FAILURE);
            } else {
                wait(NULL);

                write_string("Arguments après exécution: ");
                for (int i = 0; argv[i] != NULL; i++) {
                    char buffer[50];
                    snprintf(buffer, sizeof(buffer), "\"%s\" ", argv[i]);
                    write_string(buffer);
                }
                write_string("NULL\n");

                for (int i = 0; i < argc; i++) {
                    free(argv[i]);
                }
                free(argv);
            }
        }
    }

    return 0;
}
