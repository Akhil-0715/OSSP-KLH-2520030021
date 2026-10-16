#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_INPUT 256
#define MAX_TOKENS 50

typedef struct Token {
    char *value;
    struct Token *next;
} Token;

Token *createToken(const char *value) {
    Token *token = malloc(sizeof(Token));

    if (token == NULL)
        exit(1);

    token->value = malloc(strlen(value) + 1);

    if (token->value == NULL)
        exit(1);

    strcpy(token->value, value);
    token->next = NULL;

    return token;
}

void addToken(Token **head, const char *value) {
    Token *newToken = createToken(value);

    if (*head == NULL) {
        *head = newToken;
        return;
    }

    Token *temp = *head;

    while (temp->next != NULL)
        temp = temp->next;

    temp->next = newToken;
}

void tokenize(char *input, Token **head) {
    char buffer[MAX_INPUT];
    int i = 0;
    int j = 0;

    while (input[i] != '\0') {

        /* Single quotes */
        if (input[i] == '\'') {

            i++;

            while (input[i] != '\0' && input[i] != '\'') {
                buffer[j++] = input[i++];
            }

            if (input[i] == '\'')
                i++;

            buffer[j] = '\0';

            if (j > 0) {
                addToken(head, buffer);
                j = 0;
            }

            continue;
        }

        /* Double quotes */
        if (input[i] == '"') {

            i++;

            while (input[i] != '\0' && input[i] != '"') {

                /* Variable expansion */
                if (input[i] == '$') {

                    char variable[100];
                    int k = 0;

                    i++;

                    while (isalnum((unsigned char)input[i]) ||
                           input[i] == '_') {

                        variable[k++] = input[i++];
                    }

                    variable[k] = '\0';

                    char *value = getenv(variable);

                    if (value != NULL) {
                        strcpy(&buffer[j], value);
                        j += strlen(value);
                    }

                    continue;
                }

                buffer[j++] = input[i++];
            }

            if (input[i] == '"')
                i++;

            buffer[j] = '\0';

            if (j > 0) {
                addToken(head, buffer);
                j = 0;
            }

            continue;
        }

        /* Spaces separate normal tokens */
        if (isspace((unsigned char)input[i])) {

            if (j > 0) {
                buffer[j] = '\0';
                addToken(head, buffer);
                j = 0;
            }

            i++;
            continue;
        }

        buffer[j++] = input[i++];
    }

    if (j > 0) {
        buffer[j] = '\0';
        addToken(head, buffer);
    }
}

void displayTokens(Token *head) {
    Token *temp = head;
    int count = 1;

    printf("\nTokens:\n");

    while (temp != NULL) {
        printf("Token %d: %s\n", count++, temp->value);
        temp = temp->next;
    }
}

int validateQuotes(char *input) {
    int single = 0;
    int doubleQuote = 0;

    for (int i = 0; input[i] != '\0'; i++) {

        if (input[i] == '\'' && !doubleQuote)
            single = !single;

        else if (input[i] == '"' && !single)
            doubleQuote = !doubleQuote;
    }

    if (single || doubleQuote)
        return 0;

    return 1;
}

void freeTokens(Token *head) {
    Token *temp;

    while (head != NULL) {
        temp = head;
        head = head->next;

        free(temp->value);
        free(temp);
    }
}

int main() {

    char input[MAX_INPUT];
    Token *tokens = NULL;

    printf("=====================================\n");
    printf("       SHELL QUOTE PARSER\n");
    printf("=====================================\n");

    printf("\nEnter command: ");

    fgets(input, sizeof(input), stdin);

    input[strcspn(input, "\n")] = '\0';

    if (strlen(input) == 0) {
        printf("Empty command\n");
        return 0;
    }

    if (!validateQuotes(input)) {
        printf("\nSyntax error: Unmatched quotes\n");
        return 0;
    }

    tokenize(input, &tokens);

    displayTokens(tokens);

    printf("\nSingle quotes preserve literal content.\n");
    printf("Double quotes preserve spaces and allow variable expansion.\n");

    printf("\nSyntax is valid\n");
    printf("Quoted command parsed successfully\n");

    freeTokens(tokens);

    return 0;
}

