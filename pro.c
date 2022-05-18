#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pro.h"
#include "thread.h"
#include "thread-sync.h"
#include "memory.h"

short isRunning = 1;  // 判断是否程序还在运行中
int cur_channel = 0;  // 当前就绪队列的进程数目
int id = 0;  // 处理机id
int pid = 1;  // 标识进程id
int count = 0;  // 验证结果表
int timesence = 0;  // 时间片长度
int channel = 4;  // 处理机道数
int processor_count = 2;  // 处理机数目

struct free_header * memlist = NULL;  // 分区表入口  

spinlock_t lock = SPIN_INIT();  // 获得互斥锁

struct pro * pool_queue = NULL;  // 后备队列
struct pro * ready_queue = NULL;  // 就绪队列
struct pro * hang_queue = NULL;  // 挂起队列
struct pro * finished_queue = NULL;  // 完成队列


/** pro.c
 * 对队列中的进程依照优先级进行排序
 */
void sort(struct pro * head)
{
  assert(head != NULL);
  if (head->next == NULL || head->next->next == NULL)
  {  // 没有元素和只有一个元素的情况下直接返回
    return;
  }
  struct pro * phead = head->next;  // 从第一个元素开始，去掉假表头
  struct pro * temp;
  struct pro * ntemp;
  while (phead != NULL && phead->next != NULL) 
  {  // 第一层遍历，不能到倒数一个元素
    temp = phead;
    while (temp->next != NULL) 
    {  // 第二层遍历，不能到倒数第一个元素 
      ntemp = temp->next;
      if (temp->priority < ntemp->priority)
      {  // 前者优先级小于后者，交换节点位置
        temp->next = ntemp->next;
        if (ntemp->next != NULL)
        {  
          ntemp->next->last = temp;
        }
        ntemp->last = temp->last;
        temp->last->next = ntemp;
        temp->last = ntemp;
        ntemp->next = temp;
        continue;
      }
      temp = temp->next;
    }
    phead = phead->next;
  }
}

/** main.c
 * 合并分区
 */
void merge ()
{
  struct free_header * temp = memlist->next;
  if (temp == NULL || temp->next == NULL)
  {  // 没有分区或者只有一个分区，无法合并
    return;
  }
  struct free_header * helper;
  for (; temp != NULL && temp->next != NULL; temp = temp->next)
  {
    if (temp->addr + temp->len == temp->next->addr)
    {  // 可以合并的情况
      helper = temp->next;
      temp->len += helper->len;
      temp->next = helper->next;
      free(helper);
    }
  }
}

/** main.c
 * 回收空间，将节点的空间释放
 */
void recycle (struct pro * node)
{
  struct free_header * nnode = malloc(sizeof(struct free_header));
  nnode->addr = node->ptr;
  nnode->len = node->mem;
  assert(memlist != NULL);
  struct free_header * temp = memlist->next; 
  if (temp == NULL || temp->addr > nnode->addr)
  {  // 没有任何分区或者是下一个分区在后面的时候，直接加入
    nnode->next = memlist->next;
    memlist->next = nnode;
    return;
  }
  for (; temp != NULL && temp->next != NULL; temp = temp->next)
  {
    if (temp->addr < node->ptr && temp->next->addr > node->ptr)
    {  // 位于两个空白域之间
      break;
    }
  }
  nnode->next = temp->next;
  temp->next = nnode;
  return;
}

/** pro.c
 * node节点加入队列的头部
 */
void enqueue (struct pro * head, struct pro * node)
{
  assert(head != NULL);
  if (node == NULL)
  {  // 不能加入空节点
    return;
  }
  node->next = head->next;
  node->last = head;
  if (node->next != NULL)
  {  // 下一指针不为空，需要增加回指
    node->next->last = node;
  }
  head->next = node;
}

/**
 * 将节点加入队列尾部
 */
void enqueue_last (struct pro * head, struct pro * node)
{
  assert(head != NULL);
  for (; head->next != NULL; head = head->next) {}
  node->next = head->next;
  node->last = head;
  head->next = node;
}

/** pro.c
 * 删除队列的头节点，但不释放内存空间，返回已删除节点
 */
struct pro * pop (struct pro * head)
{
  if (head->next == NULL)
  {  // 只有空节点
    // printf("fail to pop.");
    return NULL;
  }
  struct pro * temp = head->next;
  assert(temp != NULL);
  head->next = head->next->next;
  if (head->next != NULL)
  {
    head->next->last = head;
  } 
  return temp;
}

/** main.c
 * 从就绪队列移除pexit和psuspend的进程，分别到完成队列和挂起队列
 * 同时回收相应内存，减少处理机当前道数，最后合并分区
 */
void clear()
{
  for (struct pro * head = ready_queue->next; head != NULL; head = head->next)
  {  // 从第一个有效节点到最后一个节点
    if(head->status == pexit)
    {
      head->last->next = head->next;
      if (head->next != NULL)
      {  // 并不是倒数第一个元素
        head->next->last = head->last;
      }
      recycle(head);  // 清理节点内存
      enqueue(finished_queue, head);
      cur_channel--;
      break;
    } else if (head->status == psuspend)
    {
      head->last->next = head->next;
      if (head->next != NULL)
      {
        head->next->last = head->last;
      }
      recycle(head);
      enqueue(hang_queue, head);
      cur_channel--;
      break;
    }
  }
  merge();
}

/** main.c 
 * 尝试分配空间，失败返回-1，否则指针的位置，即原空余块的起始地址
 */
int pmalloc (struct pro * node)
{
  assert(memlist != NULL);
  for (struct free_header * temp = memlist->next; temp != NULL; temp = temp->next)
  {
    if (temp->len >= node->mem)
    {
      int pos = temp->addr;
      temp->addr += node->mem;  // 移动初始地址
      temp->len -= node->mem;  // 改变分区长度
      spin_unlock(&lock);
      return pos;
    }
  }
  return -1;
}


/** main.c
 * 初始化，初始化了两个进程来实验
 */
void start_up()
{
  srand((unsigned)time(NULL));  // 设置种子

  memlist = malloc(sizeof(struct free_header));
  struct free_header * node = malloc(sizeof(struct free_header));
  node->addr = 0;
  node->len = MEMORYSIZE;
  node->next = NULL;
  memlist->addr = 0;
  memlist->len = 0;
  memlist->next = node;

  pool_queue = malloc(sizeof(struct pro));
  ready_queue = malloc(sizeof(struct pro));
  hang_queue = malloc(sizeof(struct pro));
  finished_queue = malloc(sizeof(struct pro));

  struct pro * example1 = malloc(sizeof(struct pro));
  example1->pid = pid++;
  example1->need_time = 13;
  example1->priority = 4;
  example1->status = pnew;
  example1->mem = 2;  // 只要2KB
  struct pro * example2 = malloc(sizeof(struct pro));
  example2->pid = pid++;
  example2->need_time = 13;
  example2->priority = 4;
  example2->status = pnew;
  example2->mem = 4;  // 只要4KB
  
  enqueue(pool_queue, example1);  // 进入就绪队列
  enqueue(pool_queue, example2);
}

/** pro.c
 * 打印队列
 */
void print_queue (struct pro * head)
{
  head = head->next;
  while (head != NULL) 
  {
    printf("%d->", head->pid);
    head = head->next;
  }
  printf("NULL\n");
}

/** memory.c
 * 打印空闲分区表
 */
void print_list (struct free_header * memlist)
{
  assert(memlist != NULL);
  for (memlist = memlist->next; memlist != NULL; memlist = memlist->next)
  {
    printf("*++++++++*++++++++*\n");
    printf("+  %4d  +  %4d  +\n", memlist->addr, memlist->len);
  }
  printf("*++++++++*++++++++*\n");
}

/** main.c
 * 打印所有队列
 */
void print_all_queue ()
{
    printf("pool_queue is : ");
    print_queue(pool_queue);
    printf("ready_queue is : ");
    print_queue(ready_queue);
    printf("hang_queue is : ");
    print_queue(hang_queue);
    printf("finish_queue is : ");
    print_queue(finished_queue);
    print_list(memlist);
    printf("\n");
}

/** main.c
 * 创建新的进程
 * 命令 进程名 进程需要的时间 进程优先级
 */
void create_new_pro ()
{
  struct pro * node = malloc(sizeof(struct pro));
  node->pid = pid++;  // 操作系统分配pid
  scanf("%d", &node->need_time);
  if (node->need_time <= 0)
  {
    node->need_time = 1;
  }
  scanf("%d", &node->priority);
  scanf("%d", &node->mem);
  if (node->mem > 1024)
  {  // 需要分配空间过大
    printf("Fail: memory is not sufficient\n");
    free(node);
    return;
  }
  printf("Create new pro pid:%d need_time:%d priority:%d mem:%d\n", 
      node->pid, node->need_time, node->priority, node->mem);
  spin_lock(&lock);
  enqueue(pool_queue, node);  // 新节点进入后备队列
  spin_unlock(&lock);
}

/**
 * 生成指定数目的随机进程
 */
void random_generate (int count)
{
  spin_lock(&lock);
  for (int i = 0; i < count; i++)
  {
    struct pro * node = malloc(sizeof(struct pro));
    node->pid = pid++;  // 操作系统分配pid
    node->status = pnew;
    node->need_time = 1 + rand() % 99;  // 要求运行时间[0, 100)
    node->priority = rand() % 100;  // 优先级[0, 100)
    node->mem = 1 + rand() % 1024;  // 内存[1,1024]
    enqueue(pool_queue, node);
  }
  spin_unlock(&lock);
}

/** pro.c
 * 根据pid从正在A队列中寻找node，并进入B队列中，并指定状态为status
 * 如果S为pready，代表要加入就绪队列，分配内存，为psuspend，代表要加入挂起队列，回收内存
 */
void trans (int pid, struct pro * A, struct pro * B, enum pStatus S)
{
  spin_lock(&lock);
  for (A = A->next; A != NULL; A = A->next)
  {
    if (A->pid == pid)
    {  // 找到了相应的pid
      assert(S == pready || S == psuspend);
      if (S == pready)
      {  // 分配内存，增加道数
        A->ptr = pmalloc(A);
        if (A->ptr == -1)
        {
          printf("Fail: memory is not sufficient!\n");
          break;
        } else if (cur_channel >= channel)
        {
          printf("Fail: ready_queue is full!\n");
          break;
        }
        cur_channel++;
      } else
      {  // 回收内存，减少道数
        recycle(A);
        merge();
        cur_channel--;
      }
      A->last->next = A->next;
      if (A->next != NULL)
      {
        A->next->last = A->last;
      }
      A->status = S;
      enqueue(B, A);
      spin_unlock(&lock);
      return;
    }
  }
  spin_unlock(&lock);
  printf("Fail: can not find the %d pro\n", pid);
}

/** main.c
 * 程序控制台
 */
void cmd() 
{
  char command;
  int pid;
  int nums;
  printf("Enter the cmd line...\n");
  while (isRunning) {
    command = getchar();
    switch (command) {
      case 'e':
      case 'q':
        printf("Exit\n");
        isRunning = 0;
        break;
      case 'c':
        printf("Create new progress\n");
        create_new_pro();
        break;
      case '\n':
      case ' ':
        break;
      case 'p':
        print_all_queue();
        break;
      case 'h':
        printf("Hang up a pro\n");
        scanf("%d", &pid);
        trans(pid, ready_queue, hang_queue, psuspend);
        break;
      case 'u':
        printf("Unhang up a pro\n");
        scanf("%d", &pid);
        trans(pid, hang_queue, ready_queue, pready);
        break;
      case 'r':
        printf("processor_count is %d, timesence is %d, channel is %d\nResult is %d, cur_channel is %d\n",
            processor_count, timesence, channel, count, cur_channel);
        break;
      case 'g':
        printf("Random generate some pro\n");
        scanf("%d", &nums);
        random_generate(nums);
        break;
      default:
        printf("Fail: illegal command: %c\n", command);
        break;
    }
  }
  printf("cmd line is off\n");
}

/** main.c
 * processor函数，从就绪队列调用进程
 */
void processor ()
{
  // 互斥地确定每个处理机的id
  spin_lock(&lock);
  int processor_id = id;
  id++;
  spin_unlock(&lock);

  struct pro * temp;  // 辅助指针
  printf("processor %d is running...\n", processor_id);
  while (isRunning) 
  {
    spin_lock(&lock);
    if (cur_channel < channel && pool_queue->next != NULL)
    {  // 如果当前处理机处理的进程数目小于道数，则尝试从后备调入一个进程，
       // 如果这个不能满足内存分配需求那就把它放回队列最后
      temp = pop(pool_queue);
      temp->ptr = pmalloc(temp);
      if (temp->ptr != -1)
      {  // 如果分配内存成功的话
        temp->status = pready;
        cur_channel++;
        enqueue(ready_queue, temp);
      } else
      {  // 分配失败，再放回去
        enqueue_last(pool_queue, temp);
      }
    }
    // 从就绪队列移除pexit和psuspend的进程，分别到完成队列和挂起队列
    clear();
    // 根据优先级对就绪队列进行排序
    sort(ready_queue);
    // 从就绪队列选出优先级最高的进程执行
    struct pro * cur = ready_queue;
    while (cur != NULL && cur->status != pready) 
    {
      cur = cur->next;
    }
    if (cur == NULL)
    {  // 如果没有找到可以运行的程序就进入下一轮
      spin_unlock(&lock);
      continue;
    }
    cur->status = prunning;
    count++;
    spin_unlock(&lock);
    for (int i = 0; i < timesence; i++)
    {
      sleep(1);
    }
    printf("%d processor run the progress %d:\nneed_time: %d\tpriority: %d\n",
        processor_id, cur->pid, cur->need_time, cur->priority);


    spin_lock(&lock);
    cur->priority--;
    cur->need_time--;
    if (cur->need_time <= 0)
    {  // 当前进程已完成运行
      cur->status = pexit;
    } else
    {
      cur->status = pready;
    }
    print_all_queue();
    spin_unlock(&lock);
  }
}

int main (int argc, char *argv[])
{
  start_up();
  printf("The program is begin...\n");
  printf("argc %d\n", argc);
  if (argc >= 2)
  { // 有一个参数，那么第一个参数作为处理机的数目
    processor_count = atoi(argv[1]);
    assert(processor_count > 0);
  }
  if (argc >= 3) 
  { // 有两个参数，第二个指定时间片长度
    timesence = atoi(argv[2]);
    assert(timesence >= 0);
  }
  if (argc >= 4)
  { // 有第三个参数，识别为多道机的道数
    channel = atoi(argv[3]);
    assert(channel > 0);
  }
  create(cmd);
  for (int i = 0; i < processor_count; i++)
  {
    create(processor);
  }
  join();
  printf("The program is end\n");
  return 0;
}
