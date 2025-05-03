#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include "../include/common.h"

// Variáveis globais
char document_folder[MAX_PATH_SIZE];
int cache_size;
Document *documents = NULL;  // Array de documentos
int next_id = 1;             // Próximo ID disponível
int num_documents = 0;       // Número atual de documentos

// Função para inicializar o servidor
int initialize_server() {
    // Remover pipe do servidor se já existir
    unlink(SERVER_PIPE);
    
    // Criar pipe do servidor
    if (mkfifo(SERVER_PIPE, 0666) == -1) {
        perror("Erro ao criar pipe do servidor");
        return -1;
    }
    
    // Alocar memória para documentos
    documents = (Document*)malloc(sizeof(Document) * cache_size);
    if (!documents) {
        perror("Erro ao alocar memória");
        return -1;
    }
    
    printf("Servidor iniciado. Aguardando conexões...\n");
    return 0;
}

// Limpar recursos ao encerrar
void cleanup() {
    if (documents != NULL) {
        free(documents);
        documents = NULL;  // Importante definir como NULL após liberar
    }
    unlink(SERVER_PIPE);
    printf("Servidor encerrado.\n");
}

// Adicionar um documento
int add_document(ClientMessage *msg) {
    if (num_documents >= cache_size) {
        return -1; // Cache cheio
    }
    
    // Verificar se o documento existe
    char full_path[MAX_PATH_SIZE * 2];
    sprintf(full_path, "%s/%s", document_folder, msg->path);
    
    // Verificar se o arquivo existe
    if (access(full_path, F_OK) == -1) {
        return -2; // Arquivo não existe
    }
    
    // Adicionar documento
    Document doc;
    doc.id = next_id++;
    strncpy(doc.title, msg->title, MAX_TITLE_SIZE - 1);
    doc.title[MAX_TITLE_SIZE - 1] = '\0';  // Garantir terminação
    strncpy(doc.authors, msg->authors, MAX_AUTHORS_SIZE - 1);
    doc.authors[MAX_AUTHORS_SIZE - 1] = '\0';
    strncpy(doc.year, msg->year, MAX_YEAR_SIZE - 1);
    doc.year[MAX_YEAR_SIZE - 1] = '\0';
    strncpy(doc.path, msg->path, MAX_PATH_SIZE - 1);
    doc.path[MAX_PATH_SIZE - 1] = '\0';
    
    documents[num_documents++] = doc;
    
    return doc.id;
}

// Consultar um documento
int consult_document(int doc_id, Document *doc) {
    for (int i = 0; i < num_documents; i++) {
        if (documents[i].id == doc_id) {
            *doc = documents[i];
            return 0;
        }
    }
    return -1; // Documento não encontrado
}

// Remover um documento
int delete_document(int doc_id) {
    for (int i = 0; i < num_documents; i++) {
        if (documents[i].id == doc_id) {
            // Mover o último documento para a posição do documento removido
            if (i < num_documents - 1) {
                documents[i] = documents[num_documents - 1];
            }
            num_documents--;
            return 0;
        }
    }
    return -1; // Documento não encontrado
}

// Contar linhas com uma palavra-chave
int count_lines(int doc_id, const char *keyword) {
    Document doc;
    if (consult_document(doc_id, &doc) != 0) {
        return -1; // Documento não encontrado
    }
    
    // Construir caminho completo
    char full_path[MAX_PATH_SIZE * 2];
    sprintf(full_path, "%s/%s", document_folder, doc.path);
    
    // Construir comando grep
    char command[512];
    sprintf(command, "grep -c \"%s\" \"%s\"", keyword, full_path);
    
    // Executar comando
    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        return -2; // Erro ao executar comando
    }
    
    // Ler resultado
    char buffer[32];
    fgets(buffer, sizeof(buffer), fp);
    pclose(fp);
    
    return atoi(buffer);
}

// Pesquisar documentos com uma palavra-chave
int search_documents(const char *keyword, int *doc_ids, int max_results) {
    int count = 0;
    
    for (int i = 0; i < num_documents && count < max_results; i++) {
        // Construir caminho completo
        char full_path[MAX_PATH_SIZE * 2];
        sprintf(full_path, "%s/%s", document_folder, documents[i].path);
        
        // Construir comando grep
        char command[512];
        sprintf(command, "grep -q \"%s\" \"%s\"", keyword, full_path);
        
        // Executar comando
        int result = system(command);
        if (result == 0) {
            // Palavra-chave encontrada
            doc_ids[count++] = documents[i].id;
        }
    }
    
    return count;
}

// Função principal
int main(int argc, char *argv[]) {
    // Verificar argumentos
    if (argc != 3) {
        fprintf(stderr, "Uso: %s document_folder cache_size\n", argv[0]);
        return 1;
    }
    
    // Obter argumentos
    strncpy(document_folder, argv[1], MAX_PATH_SIZE - 1);
    document_folder[MAX_PATH_SIZE - 1] = '\0';  // Garantir terminação
    
    cache_size = atoi(argv[2]);
    if (cache_size <= 0) {
        fprintf(stderr, "Tamanho de cache inválido. Deve ser maior que zero.\n");
        return 1;
    }
    
    printf("Pasta de documentos: %s\n", document_folder);
    printf("Tamanho do cache: %d\n", cache_size);
    
    // Inicializar servidor
    if (initialize_server() < 0) {
        return 1;
    }
    
    // Configurar limpeza ao encerrar
    atexit(cleanup);
    
    // Abrir pipe para leitura
    int server_pipe = open(SERVER_PIPE, O_RDONLY);
    if (server_pipe == -1) {
        perror("Erro ao abrir pipe do servidor");
        return 1;
    }
    
    printf("Aguardando conexões de clientes...\n");
    
    // Loop principal do servidor
    ClientMessage client_msg;
    ServerMessage server_response;
    char client_pipe_name[100];
    int client_pipe;

    while(1) {
        // Ler mensagem do cliente
        ssize_t bytes_read = read(server_pipe, &client_msg, sizeof(ClientMessage));
        
        if (bytes_read > 0) {
            printf("Mensagem recebida do cliente PID %d, operação %d\n", 
                client_msg.pid, client_msg.operation);
            
            // Configurar nome do pipe do cliente
            sprintf(client_pipe_name, "%s%d", CLIENT_PIPE_PREFIX, client_msg.pid);
            
            // Inicializar resposta
            memset(&server_response, 0, sizeof(ServerMessage));
            
            // Processar mensagem de acordo com a operação
            switch(client_msg.operation) {
                case OP_ADD:
                    printf("Adicionando documento: %s\n", client_msg.title);
                    server_response.doc_id = add_document(&client_msg);
                    
                    if (server_response.doc_id > 0) {
                        server_response.status = 0;
                    } else if (server_response.doc_id == -1) {
                        server_response.status = -1;
                        strcpy(server_response.error_msg, "Cache cheio");
                    } else {
                        server_response.status = -1;
                        strcpy(server_response.error_msg, "Arquivo não encontrado");
                    }
                    break;
                    
                case OP_CONSULT:
                    printf("Consultando documento: %d\n", client_msg.doc_id);
                    if (consult_document(client_msg.doc_id, &server_response.doc) == 0) {
                        server_response.status = 0;
                    } else {
                        server_response.status = -1;
                        strcpy(server_response.error_msg, "Documento não encontrado");
                    }
                    break;
                    
                case OP_DELETE:
                    printf("Removendo documento: %d\n", client_msg.doc_id);
                    if (delete_document(client_msg.doc_id) == 0) {
                        server_response.status = 0;
                    } else {
                        server_response.status = -1;
                        strcpy(server_response.error_msg, "Documento não encontrado");
                    }
                    break;
                    
                case OP_LINES:
                    printf("Contando linhas no documento %d com palavra-chave: %s\n", 
                           client_msg.doc_id, client_msg.keyword);
                    server_response.line_count = count_lines(client_msg.doc_id, client_msg.keyword);
                    
                    if (server_response.line_count >= 0) {
                        server_response.status = 0;
                    } else if (server_response.line_count == -1) {
                        server_response.status = -1;
                        strcpy(server_response.error_msg, "Documento não encontrado");
                    } else {
                        server_response.status = -1;
                        strcpy(server_response.error_msg, "Erro ao contar linhas");
                    }
                    break;
                    
                case OP_SEARCH:
                    printf("Pesquisando documentos com palavra-chave: %s (processos: %d)\n", 
                           client_msg.keyword, client_msg.nr_processes);
                    server_response.doc_count = search_documents(client_msg.keyword, 
                                                                server_response.doc_ids, 
                                                                1024);
                    server_response.status = 0;
                    break;
                    
                case OP_SHUTDOWN:
                    printf("Comando de desligamento recebido\n");
                    server_response.status = 0;
                    
                    // Abrir pipe do cliente para enviar confirmação
                    client_pipe = open(client_pipe_name, O_WRONLY);
                    if (client_pipe != -1) {
                        write(client_pipe, &server_response, sizeof(ServerMessage));
                        close(client_pipe);
                    }
                    
                    // Encerrar o servidor
                    close(server_pipe);
                    // Não chame cleanup() aqui, pois já está registrado com atexit()
                    exit(0);
                    break;
                    
                default:
                    printf("Operação não reconhecida\n");
                    server_response.status = -1;
                    strcpy(server_response.error_msg, "Operação não reconhecida");
                    break;
            }
            
            // Enviar resposta (exceto para OP_SHUTDOWN, que já enviou)
            if (client_msg.operation != OP_SHUTDOWN) {
                client_pipe = open(client_pipe_name, O_WRONLY);
                if (client_pipe != -1) {
                    write(client_pipe, &server_response, sizeof(ServerMessage));
                    close(client_pipe);
                } else {
                    perror("Erro ao abrir pipe do cliente");
                }
            }
        }
    }
    
    // Este código nunca será alcançado, mas por completude:
    close(server_pipe);
    
    return 0;
}