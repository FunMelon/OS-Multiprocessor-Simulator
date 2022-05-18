#define MEMORYSIZE 1024  // 定义内存为1MB
/**
 * 空闲分区表
 */
struct free_header 
{  
  int addr;  // 起始地址
  int len;  // 空闲块的长度
  struct free_header * next;
};
