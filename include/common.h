#ifndef COMMON_H
#define COMMON_H

#include <unistd.h> // Adicione esta linha para definir pid_t

#define SERVER_PIPE "/tmp/server_pipe"
#define CLIENT_PIPE_PREFIX "/tmp/client_pipe_"

// Tamanhos máximos dos campos
#define MAX_TITLE_SIZE 200
#define MAX_AUTHORS_SIZE 200
#define MAX_PATH_SIZE 64
#define MAX_YEAR_SIZE 5  // 4 dígitos + terminador nulo
#define MAX_KEYWORD_SIZE 64

// Códigos de operação
#define OP_ADD 1        // -a: Adicionar documento
#define OP_CONSULT 2    // -c: Consultar documento
#define OP_DELETE 3     // -d: Remover documento
#define OP_LINES 4      // -l: Contar linhas com palavra-chave
#define OP_SEARCH 5     // -s: Pesquisar documentos com palavra-chave
#define OP_SHUTDOWN 6   // -f: Desligar servidor

// Estrutura para metadados de documentos
typedef struct {
    int id;                         // ID único do documento
    char title[MAX_TITLE_SIZE];     // Título
    char authors[MAX_AUTHORS_SIZE]; // Autores
    char year[MAX_YEAR_SIZE];       // Ano
    char path[MAX_PATH_SIZE];       // Caminho do arquivo
} Document;

// Estrutura para mensagens do cliente para o servidor
typedef struct {
    int operation;      // Tipo de operação (OP_ADD, OP_CONSULT, etc.)
    pid_t pid;          // PID do cliente para identificação
    int doc_id;         // ID do documento (para consulta/remoção)
    char title[MAX_TITLE_SIZE];     // Para operação ADD
    char authors[MAX_AUTHORS_SIZE]; // Para operação ADD
    char year[MAX_YEAR_SIZE];       // Para operação ADD
    char path[MAX_PATH_SIZE];       // Para operação ADD
    char keyword[MAX_KEYWORD_SIZE]; // Para operações LINES, SEARCH
    int nr_processes;   // Para pesquisa concorrente
} ClientMessage;

// Estrutura para mensagens do servidor para o cliente
typedef struct {
    int status;         // 0 = sucesso, -1 = erro
    int doc_id;         // ID do documento (para ADD)
    Document doc;       // Documento (para CONSULT)
    int line_count;     // Número de linhas (para LINES)
    int doc_ids[1024];  // IDs dos documentos (para SEARCH)
    int doc_count;      // Número de documentos encontrados
    char error_msg[256]; // Mensagem de erro, se houver
} ServerMessage;

#endif