#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
// [NOVO] Adicionado header de tempo para funções time()
#include <time.h>
// [NOVO] Adicionado header para função waitpid() usada no processamento paralelo
#include <sys/wait.h>
#include "common.h"

// Variáveis globais
char document_folder[MAX_PATH_SIZE];
int cache_size;
Document *documents = NULL;  // Array de documentos
int next_id = 1;             // Próximo ID disponível
int num_documents = 0;       // Número atual de documentos
// [NOVO] Array para armazenar o último acesso a cada documento (para política LRU)
time_t *last_access = NULL;  // Para política de cache LRU

// [NOVO] Declaração de funções adicionada
int search_documents_sequential(const char *keyword, int *doc_ids, int max_results);
int search_for_keyword(const char *filepath, const char *keyword);
int count_keyword_lines(const char *filepath, const char *keyword);

// [NOVO] Função para salvar dados em disco - persistência do cache
int save_data() {
    char data_file[MAX_PATH_SIZE];
    sprintf(data_file, "%s/.index_data", document_folder);
    
    int fd = open(data_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("Erro ao abrir arquivo de dados");
        return -1;
    }
    
    // Salvar número de documentos e próximo ID
    write(fd, &num_documents, sizeof(int));
    write(fd, &next_id, sizeof(int));
    
    // Salvar documentos
    for (int i = 0; i < num_documents; i++) {
        write(fd, &documents[i], sizeof(Document));
    }
    
    close(fd);
    return 0;
}

// [NOVO] Função para carregar dados do disco - persistência do cache
int load_data() {
    char data_file[MAX_PATH_SIZE];
    sprintf(data_file, "%s/.index_data", document_folder);
    
    // Verificar se arquivo existe
    if (access(data_file, F_OK) == -1) {
        return 0; // Arquivo não existe, não é erro
    }
    
    int fd = open(data_file, O_RDONLY);
    if (fd == -1) {
        perror("Erro ao abrir arquivo de dados");
        return -1;
    }
    
    // Carregar número de documentos e próximo ID
    read(fd, &num_documents, sizeof(int));
    read(fd, &next_id, sizeof(int));
    
    // Verificar se não excede o tamanho do cache
    if (num_documents > cache_size) {
        num_documents = cache_size; // Limitar ao tamanho do cache
    }
    
    // Carregar documentos
    for (int i = 0; i < num_documents; i++) {
        read(fd, &documents[i], sizeof(Document));
        last_access[i] = time(NULL); // Inicializar timestamps
    }
    
    close(fd);
    return 0;
}

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
        // [MODIFICADO] Mensagem de erro mais específica
        perror("Erro ao alocar memória para documentos");
        return -1;
    }
    
    // [NOVO] Alocação de memória para os timestamps de último acesso
    last_access = (time_t*)malloc(sizeof(time_t) * cache_size);
    if (!last_access) {
        perror("Erro ao alocar memória para timestamps");
        free(documents);
        return -1;
    }
    
    // [NOVO] Inicialização dos timestamps
    for (int i = 0; i < cache_size; i++) {
        last_access[i] = 0;
    }
    
    // [NOVO] Carregar dados do disco ao iniciar
    if (load_data() < 0) {
        perror("Erro ao carregar dados");
        // Continuar mesmo com erro
    }
    
    // [MODIFICADO] Mensagem ligeiramente diferente
    printf("Servidor iniciado. Aguardando conexões...\n");
    return 0;
}

// Limpar recursos ao encerrar
void cleanup() {
    // [NOVO] Salvar dados antes de encerrar
    save_data();
    
    if (documents != NULL) {
        free(documents);
        documents = NULL;
    }
    
    // [NOVO] Liberar memória dos timestamps
    if (last_access != NULL) {
        free(last_access);
        last_access = NULL;
    }
    
    unlink(SERVER_PIPE);
    printf("Servidor encerrado.\n");
}

// [NOVO] Funções para implementar a política de cache LRU (Least Recently Used)
void update_access(int index) {
    last_access[index] = time(NULL);
}

// [NOVO] Função para encontrar o documento menos recentemente usado
int find_lru_index() {
    time_t oldest = time(NULL);
    int oldest_index = 0;
    
    for (int i = 0; i < num_documents; i++) {
        if (last_access[i] < oldest) {
            oldest = last_access[i];
            oldest_index = i;
        }
    }
    
    return oldest_index;
}

// Adicionar um documento
int add_document(ClientMessage *msg) {
    // [MODIFICADO] Removida a verificação de cache cheio, agora usa LRU
    
    // Verificar se o documento existe
    char full_path[MAX_PATH_SIZE * 2];
    sprintf(full_path, "%s/%s", document_folder, msg->path);
    
    if (access(full_path, F_OK) == -1) {
        return -2; // Arquivo não existe
    }
    
    // Criar novo documento
    Document doc;
    doc.id = next_id++;
    strncpy(doc.title, msg->title, MAX_TITLE_SIZE - 1);
    doc.title[MAX_TITLE_SIZE - 1] = '\0';
    strncpy(doc.authors, msg->authors, MAX_AUTHORS_SIZE - 1);
    doc.authors[MAX_AUTHORS_SIZE - 1] = '\0';
    strncpy(doc.year, msg->year, MAX_YEAR_SIZE - 1);
    doc.year[MAX_YEAR_SIZE - 1] = '\0';
    strncpy(doc.path, msg->path, MAX_PATH_SIZE - 1);
    doc.path[MAX_PATH_SIZE - 1] = '\0';
    
    // [NOVO] Implementação da política LRU para o cache
    int index;
    if (num_documents < cache_size) {
        // Ainda há espaço no cache
        index = num_documents++;
        documents[index] = doc;
    } else {
        // Cache cheio, usar política LRU
        index = find_lru_index();
        documents[index] = doc;
    }
    
    // [NOVO] Atualizar timestamp de acesso
    update_access(index);
    
    // [NOVO] Persistir dados em disco
    save_data();
    
    return doc.id;
}

// Consultar um documento
int consult_document(int doc_id, Document *doc) {
    for (int i = 0; i < num_documents; i++) {
        if (documents[i].id == doc_id) {
            *doc = documents[i];
            // [NOVO] Atualizar timestamp de acesso
            update_access(i);
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
                // [NOVO] Atualizar timestamp
                last_access[i] = last_access[num_documents - 1];
            }
            num_documents--;
            
            // [NOVO] Persistir dados em disco
            save_data();
            return 0;
        }
    }
    return -1; // Documento não encontrado
}

// [CORRIGIDO] Função para verificar se uma linha contém uma palavra-chave
int line_contains_keyword(const char *line, const char *keyword) {
    char *pos = strstr(line, keyword);
    return (pos != NULL);
}

// [CORRIGIDO] Nova função para contar linhas que contêm uma palavra-chave
int count_keyword_lines(const char *filepath, const char *keyword) {
    int fd = open(filepath, O_RDONLY);
    if (fd == -1) {
        return -2; // Erro ao abrir arquivo
    }
    
    char buffer[4096];
    char line[1024];
    int line_count = 0;
    int line_pos = 0;
    int bytes_read;
    
    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        for (int i = 0; i < bytes_read; i++) {
            // Construir a linha caractere por caractere
            if (buffer[i] != '\n' && line_pos < 1023) {
                line[line_pos++] = buffer[i];
            } else {
                line[line_pos] = '\0'; // Finalizar a linha
                
                // Verificar se a linha contém a palavra-chave
                if (line_contains_keyword(line, keyword)) {
                    line_count++;
                }
                
                // Reiniciar para a próxima linha
                line_pos = 0;
            }
        }
    }
    
    // Verificar a última linha se não terminar com \n
    if (line_pos > 0) {
        line[line_pos] = '\0';
        if (line_contains_keyword(line, keyword)) {
            line_count++;
        }
    }
    
    close(fd);
    return line_count;
}

// [CORRIGIDO] Contar linhas com uma palavra-chave
int count_lines(int doc_id, const char *keyword) {
    Document doc;
    if (consult_document(doc_id, &doc) != 0) {
        return -1; // Documento não encontrado
    }
    
    // Construir caminho completo
    char full_path[MAX_PATH_SIZE * 2];
    sprintf(full_path, "%s/%s", document_folder, doc.path);
    
    // Usar nossa nova função para contar linhas
    return count_keyword_lines(full_path, keyword);
}

// [CORRIGIDO] Função para verificar se um arquivo contém uma palavra-chave
int search_for_keyword(const char *filepath, const char *keyword) {
    int fd = open(filepath, O_RDONLY);
    if (fd == -1) {
        perror("Erro ao abrir arquivo para busca");
        return 0; // Arquivo não existe ou erro
    }
    
    char buffer[4096];
    int bytes_read;
    int result = 0;
    
    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        // Garantir que o buffer termine com \0 para usar strstr
        char temp_buffer[4097];
        memcpy(temp_buffer, buffer, bytes_read);
        temp_buffer[bytes_read] = '\0';
        
        if (strstr(temp_buffer, keyword) != NULL) {
            result = 1;
            break;
        }
    }
    
    close(fd);
    return result;
}

// [CORRIGIDO] Função de pesquisa sequencial - substitui a versão que usava system()
int search_documents_sequential(const char *keyword, int *doc_ids, int max_results) {
    int count = 0;
    
    for (int i = 0; i < num_documents && count < max_results; i++) {
        // Construir caminho completo
        char full_path[MAX_PATH_SIZE * 2];
        sprintf(full_path, "%s/%s", document_folder, documents[i].path);
        
        // Usar nossa própria função de busca
        if (search_for_keyword(full_path, keyword)) {
            // Palavra-chave encontrada
            doc_ids[count++] = documents[i].id;
        }
    }
    
    return count;
}

// [CORRIGIDO] Implementação de pesquisa paralela com múltiplos processos
int search_documents(const char *keyword, int *doc_ids, int max_results, int nr_processes) {
    // Se nr_processes for 1 ou menos, usar método sequencial
    if (nr_processes <= 1) {
        return search_documents_sequential(keyword, doc_ids, max_results);
    }
    
    int count = 0;
    int pipes[nr_processes][2];
    pid_t pids[nr_processes];
    
    // Limitar número de processos ao número de documentos
    if (nr_processes > num_documents) {
        nr_processes = num_documents;
    }
    
    // Criar pipes para comunicação
    for (int i = 0; i < nr_processes; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("Erro ao criar pipe");
            return -1;
        }
    }
    
    // Dividir documentos entre processos
    int docs_per_process = (num_documents + nr_processes - 1) / nr_processes;
    
    // Criar processos filhos
    for (int i = 0; i < nr_processes; i++) {
        pids[i] = fork();
        
        if (pids[i] == -1) {
            perror("Erro ao criar processo");
            return -1;
        }
        
        if (pids[i] == 0) {
            // Código do processo filho
            close(pipes[i][0]); // Fechar extremidade de leitura
            
            int start = i * docs_per_process;
            int end = (i + 1) * docs_per_process;
            if (end > num_documents) end = num_documents;
            
            int child_count = 0;
            int child_results[end - start];
            
            // Pesquisar documentos alocados a este processo
            for (int j = start; j < end; j++) {
                char full_path[MAX_PATH_SIZE * 2];
                sprintf(full_path, "%s/%s", document_folder, documents[j].path);
                
                // [CORRIGIDO] Usar função própria em vez de system()
                if (search_for_keyword(full_path, keyword)) {
                    child_results[child_count++] = documents[j].id;
                }
            }
            
            // Enviar resultados para o processo pai
            write(pipes[i][1], &child_count, sizeof(int));
            if (child_count > 0) {
                write(pipes[i][1], child_results, sizeof(int) * child_count);
            }
            
            close(pipes[i][1]);
            exit(0);
        } else {
            // Processo pai
            close(pipes[i][1]); // Fechar extremidade de escrita
        }
    }
    
    // Coletar resultados dos processos filhos
    for (int i = 0; i < nr_processes; i++) {
        int child_count;
        read(pipes[i][0], &child_count, sizeof(int));
        
        if (child_count > 0) {
            int child_results[child_count];
            read(pipes[i][0], child_results, sizeof(int) * child_count);
            
            // Adicionar resultados ao array final
            for (int j = 0; j < child_count && count < max_results; j++) {
                doc_ids[count++] = child_results[j];
            }
        }
        
        close(pipes[i][0]);
        waitpid(pids[i], NULL, 0); // Aguardar término do processo filho
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
    
    printf("Aguardar conexões de clientes...\n");
    
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
                    printf("A Adicionar documento: %s\n", client_msg.title);
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
                    printf("Consultar documento: %d\n", client_msg.doc_id);
                    if (consult_document(client_msg.doc_id, &server_response.doc) == 0) {
                        server_response.status = 0;
                    } else {
                        server_response.status = -1;
                        strcpy(server_response.error_msg, "Documento não encontrado");
                    }
                    break;
                    
                case OP_DELETE:
                    printf("Remover documento: %d\n", client_msg.doc_id);
                    if (delete_document(client_msg.doc_id) == 0) {
                        server_response.status = 0;
                    } else {
                        server_response.status = -1;
                        strcpy(server_response.error_msg, "Documento não encontrado");
                    }
                    break;
                    
                case OP_LINES:
                    printf("Contar linhas no documento %d com palavra-chave: %s\n", 
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
                    printf("Pesquisar documentos com palavra-chave: %s (processos: %d)\n", 
                           client_msg.keyword, client_msg.nr_processes);
                    // [MODIFICADO] Adicionado parâmetro nr_processes para pesquisa paralela
                    server_response.doc_count = search_documents(client_msg.keyword, 
                                                                server_response.doc_ids, 
                                                                1024,
                                                                client_msg.nr_processes);
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
    
    close(server_pipe);
    
    return 0;
}
