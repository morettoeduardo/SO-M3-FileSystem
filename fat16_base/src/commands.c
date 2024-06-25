#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "commands.h"
#include "fat16.h"
#include "support.h"

off_t fsize(const char *filename){
    struct stat st;
    if (stat(filename, &st) == 0)
        return st.st_size;
    return -1;
}

struct fat_dir find(struct fat_dir *dirs, char *filename, struct fat_bpb *bpb){
    struct fat_dir curdir;
    int dirs_len = sizeof(struct fat_dir) * bpb->possible_rentries;
    int i;

    for (i=0; i < dirs_len; i++){
        if (strcmp((char *) dirs[i].name, filename) == 0){
            curdir = dirs[i];
            break;
        }
    }
    return curdir;
}

struct fat_dir *ls(FILE *fp, struct fat_bpb *bpb){
    int i;
    struct fat_dir *dirs = malloc(sizeof (struct fat_dir) * bpb->possible_rentries);

    for (i=0; i < bpb->possible_rentries; i++){
        uint32_t offset = bpb_froot_addr(bpb) + i * 32;
        read_bytes(fp, offset, &dirs[i], sizeof(dirs[i]));
    }
    return dirs;
}

int write_dir(FILE *fp, char *fname, struct fat_dir *dir){
    char* name = padding(fname);
    strcpy((char *) dir->name, (char *) name);
    if (fwrite(dir, 1, sizeof(struct fat_dir), fp) <= 0)
        return -1;
    return 0;
}

int write_data(FILE *fp, char *fname, struct fat_dir *dir, struct fat_bpb *bpb){

    FILE *localf = fopen(fname, "r");
    int c;

    while ((c = fgetc(localf)) != EOF){
        if (fputc(c, fp) != c)
            return -1;
    }
    return 0;
}

int wipe(FILE *fp, struct fat_dir *dir, struct fat_bpb *bpb){
    int start_offset = bpb_froot_addr(bpb) + (bpb->bytes_p_sect * \
            dir->starting_cluster);
    int limit_offset = start_offset + dir->file_size;

    while (start_offset <= limit_offset){
        fseek(fp, ++start_offset, SEEK_SET);
        if(fputc(0x0, fp) != 0x0)
            return 01;
    }
    return 0;
}

void mv(FILE *fp, char *filename, struct fat_bpb *bpb) {
    // Array para armazenar o novo nome do arquivo
    char new_filename[12];

    // Solicitação e leitura do novo nome do arquivo
    printf("Digite o novo nome do arquivo: ");
    scanf("%11s", new_filename);

    // Obtém a lista de diretórios do sistema de arquivos FAT16
    struct fat_dir *dirs = ls(fp, bpb);

    // Encontra a estrutura de diretório do arquivo original pelo nome
    struct fat_dir old_dir = find(dirs, filename, bpb);
    
    if (strlen((char *)old_dir.name) == 0) { // Verifica se o arquivo original foi encontrado
        printf("Arquivo não encontrado\n");
        free(dirs);
        return;
    }

    // Verifica se o novo nome já está em uso no diretório
    struct fat_dir new_dir_check = find(dirs, new_filename, bpb);
    if (strlen((char *)new_dir_check.name) != 0) {
        printf("Nome do arquivo já existe\n");
        free(dirs);
        return;
    }

    // Copia a estrutura do arquivo antigo e altera o nome para o novo nome
    struct fat_dir new_dir = old_dir;
    char* new_name = padding(new_filename); // Aplica padding ao novo nome
    strncpy((char *) new_dir.name, new_name, 11);

    // Escreve a nova entrada de diretório com o novo nome
    if (write_dir(fp, new_filename, &new_dir) != 0) {
        printf("Erro ao escrever nova entrada de diretório\n");
        free(dirs);
        return;
    }
   
    rm(fp, filename, bpb);  // Remove a entrada de diretório antiga pelo nome original
    free(dirs);
}



void rm(FILE *fp, char *filename, struct fat_bpb *bpb) {
    // Obtém a lista de diretórios do sistema de arquivos FAT16
    struct fat_dir *dirs = ls(fp, bpb);

    // Encontra a estrutura de diretório do arquivo pelo nome
    struct fat_dir file_dir = find(dirs, filename, bpb);

    // Verifica se o arquivo foi encontrado
    if (strlen((char *)file_dir.name) == 0) {
        printf("Arquivo não encontrado\n");
        free(dirs);
        return;
    }

    // Apaga os dados do arquivo
    if (wipe(fp, &file_dir, bpb) != 0) {
        printf("Erro ao apagar os dados do arquivo\n");
        free(dirs);
        return;
    }

    // Marca a entrada do diretório como não utilizada
    int dir_offset = (int)(&file_dir - dirs) * sizeof(struct fat_dir);
    fseek(fp, bpb_froot_addr(bpb) + dir_offset, SEEK_SET);
    if (fputc(0xE5, fp) == EOF) { // 0xE5 marca a entrada como deletada
        printf("Erro ao marcar a entrada do diretório como deletada\n");
    }

    free(dirs);
}

void cp(FILE *fp, char *filename, struct fat_bpb *bpb) {
    // Encontra o diretório do arquivo no sistema de arquivos FAT16
    struct fat_dir *dirs = ls(fp, bpb);
    struct fat_dir src_dir = find(dirs, filename, bpb);
    free(dirs);

    if (strlen((char *)src_dir.name) == 0) {
        printf("Arquivo de origem não encontrado\n");
        return;
    }

    // Abre o arquivo de destino local
    FILE *localf = fopen(filename, "wb");
    if (!localf) {
        printf("Erro ao abrir o arquivo de destino local\n");
        return;
    }

    // Lê e escreve os dados do arquivo FAT16 para o arquivo local
    uint32_t offset = bpb_froot_addr(bpb) + (src_dir.starting_cluster - 2) * bpb->bytes_p_sect;
    fseek(fp, offset, SEEK_SET);
    for (int i = 0; i < src_dir.file_size; i++) {
        int c = fgetc(fp);
        if (c == EOF) {
            printf("Erro ao ler dados do arquivo FAT16\n");
            fclose(localf);
            return;
        }
        if (fputc(c, localf) == EOF) {
            printf("Erro ao escrever dados no arquivo de destino local\n");
            fclose(localf);
            return;
        }
    }

    fclose(localf);
    printf("Arquivo copiado com sucesso: %s -> %s\n", filename, filename);
}

