#include <stdio.h>
#include <fcntl.h>
#include <elf.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define PAGE_SIZE 4096

typedef struct _funcInfo{
    Elf32_Addr st_value;
    Elf32_Word st_size;
}funcInfo;

Elf32_Ehdr ehdr;

static unsigned elfhash(const char *_name)
{
  const unsigned char *name = (const unsigned char *) _name;
  unsigned h = 0, g;

  while(*name) {
    h = (h << 4) + *name++;
    g = h & 0xf0000000;
    h ^= g;
    h ^= g >> 24;
  }
  return h;
}

static Elf32_Off findTargetSectionAddr(const int fd, const char *secName){
  Elf32_Shdr shdr;
  char *shstr = NULL;
  int i;

  lseek(fd, 0, SEEK_SET);
  if(read(fd, &ehdr, sizeof(Elf32_Ehdr)) != sizeof(Elf32_Ehdr)){
    puts("Read ELF header error");
    goto _error;
  }

  lseek(fd, ehdr.e_shoff + sizeof(Elf32_Shdr) * ehdr.e_shstrndx, SEEK_SET);

  if(read(fd, &shdr, sizeof(Elf32_Shdr)) != sizeof(Elf32_Shdr)){
    puts("Read ELF section string table error");
    goto _error;
  }

  if((shstr = (char *) malloc(shdr.sh_size)) == NULL){
    puts("Malloc space for section string table failed");
    goto _error;
  }

  lseek(fd, shdr.sh_offset, SEEK_SET);
  if(read(fd, shstr, shdr.sh_size) != shdr.sh_size){
    puts(shstr);
    puts("Read string table failed");
    goto _error;
  }

  lseek(fd, ehdr.e_shoff, SEEK_SET);
  for(i = 0; i < ehdr.e_shnum; i++){
    if(read(fd, &shdr, sizeof(Elf32_Shdr)) != sizeof(Elf32_Shdr)){
      puts("Find section .text procedure failed");
      goto _error;
    }
    if(strcmp(shstr + shdr.sh_name, secName) == 0){
      printf("Find section %s, addr = 0x%x\n", secName, shdr.sh_offset);
      break;
    }
  }
  free(shstr);
  return shdr.sh_offset;
  _error:
  return -1;
}

static char getTargetFuncInfo(int fd, const char *funcName, funcInfo *info){
  char flag = -1, *dynstr;
  int i;
  Elf32_Sym funSym;
  Elf32_Phdr phdr;
  Elf32_Off dyn_off;
  Elf32_Word dyn_size, dyn_strsz;
  Elf32_Dyn dyn;
  Elf32_Addr dyn_symtab, dyn_strtab, dyn_hash;
  unsigned funHash, nbucket, nchain, funIndex;

  //从程序头中查找类型为PT_DYNAMIC的Segment
  lseek(fd, ehdr.e_phoff, SEEK_SET);
  for(i=0;i < ehdr.e_phnum; i++){
    if(read(fd, &phdr, sizeof(Elf32_Phdr)) != sizeof(Elf32_Phdr)){
      puts("Read segment failed");
      goto _error;
    }
    if(phdr.p_type ==  PT_DYNAMIC){
      dyn_size = phdr.p_filesz;
      dyn_off = phdr.p_offset;
      flag = 0;
      printf("Find section %s, size = 0x%x, addr = 0x%x\n", ".dynamic", dyn_size, dyn_off);
      break;
    }
  }
  if(flag){
    puts("Find .dynamic failed");
    goto _error;
  }
  flag = 0;

  int dyn_num = (dyn_size/sizeof(Elf32_Dyn));
  printf("dyn_size:%d\n",dyn_size);
  printf("dyn_num:%d\n",dyn_num);
  printf("dyn_off:%x\n",dyn_off);

  //在动态段中找到对应的section
  lseek(fd, dyn_off, SEEK_SET);
  for(i=0;i < dyn_num; i++){
    int sizes = read(fd, &dyn, sizeof(Elf32_Dyn));
    if(sizes != sizeof(Elf32_Dyn)){
      puts("Read .dynamic information failed");
      //goto _error;
      break;
    }
    if(dyn.d_tag == DT_SYMTAB){
      dyn_symtab = dyn.d_un.d_ptr;
      flag += 1;
      printf("Find .dynsym, addr = 0x%x, val = 0x%x\n", dyn_symtab, dyn.d_un.d_val);
    }

    if(dyn.d_tag == DT_HASH){
      dyn_hash = dyn.d_un.d_ptr;
      flag += 2;
      printf("Find .hash, addr = 0x%x\n", dyn_hash);
    }
    if(dyn.d_tag == DT_STRTAB){
      dyn_strtab = dyn.d_un.d_ptr;
      flag += 4;
      printf("Find .dynstr, addr = 0x%x\n", dyn_strtab);
    }
    if(dyn.d_tag == DT_STRSZ){
      dyn_strsz = dyn.d_un.d_val;
      flag += 8;
      printf("Find .dynstr size, size = 0x%x\n", dyn_strsz);
    }
  }

  if((flag & 0x0f) != 0x0f){
    puts("Find needed .section failed\n");
    goto _error;
  }

  dynstr = (char*) malloc(dyn_strsz);
  if(dynstr == NULL){
    puts("Malloc .dynstr space failed");
    goto _error;
  }

  lseek(fd, dyn_strtab, SEEK_SET);
  if(read(fd, dynstr, dyn_strsz) != dyn_strsz){
    puts("Read .dynstr failed");
    goto _error;
  }

  funHash = elfhash(funcName);
  printf("Function %s hashVal = 0x%x\n", funcName, funHash);

  lseek(fd, dyn_hash, SEEK_SET);
  if(read(fd, &nbucket, 4) != 4){
    puts("Read hash nbucket failed\n");
    goto _error;
  }
  printf("nbucket = %d\n", nbucket);

  if(read(fd, &nchain, 4) != 4){
    puts("Read hash nchain failed\n");
    goto _error;
  }
  printf("nchain = %d\n", nchain);

  funHash = funHash % nbucket;
  printf("funHash mod nbucket = %d \n", funHash);

  lseek(fd, funHash * 4, SEEK_CUR);
  if(read(fd, &funIndex, 4) != 4){
    puts("Read funIndex failed\n");
    goto _error;
  }

  printf("funcIndex:%d\n", funIndex);

  lseek(fd, dyn_symtab + funIndex * sizeof(Elf32_Sym), SEEK_SET);
  if(read(fd, &funSym, sizeof(Elf32_Sym)) != sizeof(Elf32_Sym)){
    puts("Read funSym failed");
    goto _error;
  }

  //这里开始查找hash中目标函数的idnex
  if(strcmp(dynstr + funSym.st_name, funcName) != 0){
    while(1){
      printf("hash:%x,nbucket:%d,funIndex:%d\n",dyn_hash,nbucket,funIndex);
      lseek(fd, dyn_hash + 4 * (2 + nbucket + funIndex), SEEK_SET);
      if(read(fd, &funIndex, 4) != 4){
        puts("Read funIndex failed\n");
        goto _error;
      }

      printf("funcIndex:%d\n", funIndex);

      if(funIndex == 0){
        puts("Cannot find funtion!\n");
        goto _error;
      }

      lseek(fd, dyn_symtab + funIndex * sizeof(Elf32_Sym), SEEK_SET);
      if(read(fd, &funSym, sizeof(Elf32_Sym)) != sizeof(Elf32_Sym)){
        puts("In FOR loop, Read funSym failed");
        goto _error;
      }

      if(strcmp(dynstr + funSym.st_name, funcName) == 0){
        break;
      }
    }
  }

  printf("Find: %s, offset = 0x%x, size = 0x%x\n", funcName, funSym.st_value, funSym.st_size);
  info->st_value = funSym.st_value;
  info->st_size = funSym.st_size;
  free(dynstr);
  return 0;

  _error:
  free(dynstr);
  return -1;
}

void encode(char *funcName){
  char secName[] = ".text";
  char *soName = "libcrack.so";
  char *content = NULL;
  int fd, i;
  Elf32_Off secOff;
  funcInfo info;

  unsigned a = elfhash(funcName);
  printf("a:%d\n", a);

  fd = open(soName, O_RDWR);
  if(fd < 0){
    printf("open %s failed\n", soName);
    goto _error;
  }

  secOff = findTargetSectionAddr(fd, secName);
  if(secOff == -1){
    printf("Find section %s failed\n", secName);
    goto _error;
  }
  if(getTargetFuncInfo(fd, funcName, &info) == -1){
    printf("Find function %s failed\n", funcName);
    goto _error;
  }

  content = (char*) malloc(info.st_size);
  if(content == NULL){
    puts("Malloc space failed");
    goto _error;
  }

  //这里需要注意的info.st_value-1
  lseek(fd, info.st_value - 1, SEEK_SET);
  if(read(fd, content, info.st_size) != info.st_size){
    puts("Malloc space failed");
    goto _error;
  }

  for(i=0;i<info.st_size -1;i++){
    content[i] = ~content[i];
  }

  lseek(fd, info.st_value-1, SEEK_SET);
  if(write(fd, content, info.st_size) != info.st_size){
    puts("Write modified content to .so failed");
    goto _error;
  }
  puts("Complete!");

  _error:
  free(content);
  close(fd);
}

int main(int argc, char** argv){
  encode("sub_1527");
//  encode("d");
  return 0;
}