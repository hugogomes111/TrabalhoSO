#ifndef COMMON_H
#define COMMON_H
#include <unistd.h>  // NOVO: Inclusão da biblioteca unistd.h (para pid_t)

#define SERVER_PIPE "/tmp/server_pipe"
#define CLIENT_PIPE_PREFIX "/tmp/client_pipe_"

// Tamanhos máximos dos campos - Comentário modificado com "sssss" no final
#define MAX_TITLE_SIZE 200
#define MAX_AUTHORS_SIZE 200
#define MAX_PATH_SIZE 64
#define MAX_YEAR_SIZE 5  // NOVO: Comentário explicativo adicional (4 dígitos + terminador nulo)
#define MAX_KEYWORD_SIZE 64  // MODIFICADO: Aumentou de 50 para 64

// Códigos de operação - NOVO: Comentários explicativos para cada operação
#define OP_ADD 1        // -a: Adicionar documento
#define OP_CONSULT 2    // -c: Consultar documento
#define OP_DELETE 3     // -d: Remover documento
#define OP_LINES 4      // -l: Contar linhas com palavra-chave
#define OP_SEARCH 5     // -s: Pesquisar documentos com palavra-chave
#define OP_SHUTDOWN 6   // -f: Desligar servidor

// REMOVIDO: Definição MAX_ERROR_MSG 100
// REMOVIDO: Definição MAX_RESULTS 1024

// Estrutura para metadados de documentos - Comentário modificado com "sssss" no final
typedef struct {
    int id;                         // ID único do documento
    char title[MAX_TITLE_SIZE];     // Título
    char authors[MAX_AUTHORS_SIZE]; // Autores
    char year[MAX_YEAR_SIZE];       // Ano
    char path[MAX_PATH_SIZE];       // Caminho do arquivo
} Document;

// Estrutura para mensagens do cliente para o servidor - Comentário modificado com "rrrrr" no final
typedef struct {
    int operation;      // Tipo de operação (OP_ADD, OP_CONSULT, etc.) - Comentário modificado
    pid_t pid;          // MODIFICADO: Agora usa pid_t em vez de int, e comentário explicativo adicional
    int doc_id;         // ID do documento 
    char title[MAX_TITLE_SIZE];     // NOVO: Comentário explicativo (Para operação ADD)
    char authors[MAX_AUTHORS_SIZE]; // NOVO: Comentário explicativo (Para operação ADD)
    char year[MAX_YEAR_SIZE];       // NOVO: Comentário explicativo (Para operação ADD)
    char path[MAX_PATH_SIZE];       // NOVO: Comentário explicativo (Para operação ADD)
    char keyword[MAX_KEYWORD_SIZE]; // NOVO: Comentário explicativo (Para operações LINES, SEARCH)
    int nr_processes;   // NOVO: Comentário explicativo (Para pesquisa concorrente)
} ClientMessage;

// Estrutura para mensagens do servidor para o cliente
typedef struct {
    int status;         // 0 = sucesso, -1 = erro
    int doc_id;         // ID do documento (para ADD)
    Document doc;       // Documento (para CONSULT)
    int line_count;     // Número de linhas (para LINES)
    int doc_ids[1024];  // MODIFICADO: Usa diretamente 1024 em vez de MAX_RESULTS
    int doc_count;      // Número de documentos encontrados
    char error_msg[256]; // MODIFICADO: Tamanho aumentado de MAX_ERROR_MSG (100) para 256
} ServerMessage;

#endif
