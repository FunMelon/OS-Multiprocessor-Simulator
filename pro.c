#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pro.h"
#include "thread.h"
#include "thread-sync.h"


short isRunning = 1;  // 判断是否程序还在运行中 
int cur_channel = 0;  // 当前就绪队列的进程数目
int id = 0;  // 处理机id
int count = 0;  // 验证结果表
int timesence = 0;  // 时间片长度

spinlock_t lock = SPIN_INIT();  // 获得互斥锁

struct pro * pool_queue = NULL;  // 后备队列
struct pro * ready_queue = NULL;  // 就绪队列
struct pro * hang_queue = NULL;  // 挂起队列
struct pro * finished_queue = NULL;  // 完成队列


/**
 * 对队列中的进程依照优先级进行排序
 */
void sort(struct pro * head)
{
  assert(head != NULL);
  // printf("sort\n");
  if (head->next == NULL || head->next->next == NULL)
  {  // 没有元素和只有一个元素的情况下直接返回
    // printf("fail to sort\n");
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
  // printf("finish sort\n");
}

/**
 * node节点加入队列的头部
 */
void enqueue (struct pro * head, struct pro * node)
{
  // printf("enqueue\n");
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
  // printf("finish enqueue\n");
}

/**
 * 删除队列的头节点，但不释放内存空间，返回已删除节点
 */
struct pro * pop (struct pro * head)
{
  // printf("pop\n");
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
  // printf("finish pop\n");
  return temp;
}

/**
 * 从就绪队列移除pexit和psuspend的进程，分别到完成队列和挂起队列
 */
void clear()
{
  // printf("clear\n");
  for (struct pro * head = ready_queue->next; head != NULL; head = head->next)
  {
    if(head->status == pexit)
    {
      head->last->next = head->next;
      if (head->next != NULL)
      {
        head->next->last = head->last;
      }
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
      enqueue(hang_queue, head);
      cur_channel--;
      break;
    }
  }
  // printf("finish clear\n");
}

/**
 * 初始化，初始化了两个进程来实验
 */
void start_up()
{
  // printf("start up...\n");
  pool_queue = malloc(sizeof(struct pro));
  ready_queue = malloc(sizeof(struct pro));
  hang_queue = malloc(sizeof(struct pro));
  finished_queue = malloc(sizeof(struct pro));

  struct pro * example1 = malloc(sizeof(struct pro));
  assert(example1 != NULL);
  example1->pid = 0;
  example1->need_time = 13;
  example1->priority = 4;
  example1->status = pnew;
  struct pro * example2 = malloc(sizeof(struct pro));
  assert(example2 != NULL);
  example2->pid = 1;
  example2->need_time = 13;
  example2->priority = 4;
  example2->status = pnew;
  
  enqueue(pool_queue, example1);
  enqueue(pool_queue, example2);
  // printf("finish start up\n");
}

/**
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

/**
 * 打印所有队列
 */
void print_all_queue ()
{
    printf("####################\n");
    printf("pool_queue is : ");
    print_queue(pool_queue);
    printf("ready_queue is : ");
    print_queue(ready_queue);
    printf("hang_queue is : ");
    print_queue(hang_queue);
    printf("finish_queue is : ");
    print_queue(finished_queue);
    printf("####################\n\n");
}

/**
 * 创建新的进程
 * 命令 进程名 进程需要的时间 进程优先级
 */
void create_new_pro ()
{
  struct pro * node = malloc(sizeof(struct pro));
  scanf("%d", &node->pid);
  // TODO: 在这里检验输入的进程号是否合法
  scanf("%d", &node->need_time);
  // TODO: 在这里检验输入的时间是否合法
  scanf("%d", &node->priority);
  printf("Create new pro %d %d %d\n", node->pid, node->need_time, node->priority);
  node->status = pnew;
  spin_lock(&lock);
  enqueue(pool_queue, node);
  print_all_queue();
  spin_unlock(&lock);
}

/**
 * 根据pid从正在A队列中寻找node，并进入B队列中，并指定状态为status
 */
void trans (int pid, struct pro * A, struct pro * B, enum pStatus S)
{
  spin_lock(&lock);
  for (A = A->next; A != NULL; A = A->next)
  {
    if (A->pid == pid)
    {
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
  printf("Can not find the %d pro\n", pid);
}

/**
 * 程序控制台
 */
void cmd() 
{
  char command;
  int pid;
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
        printf("Result is %d\n", count);
        break;
      default:
        printf("Illegal command: %c\n", command);
        break;
    }
  }
  printf("cmd line is off\n");
}

/**
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
    // printf("1\n");
    spin_lock(&lock);
    // printf("2\n");
    while (cur_channel < CHANNEL && pool_queue->next != NULL)
    {  // 如果当前处理机处理的进程数目小于道数，则调入
      temp = pop(pool_queue);
      temp->status = pready;
      enqueue(ready_queue, temp);
    }
    // printf("3\n");
    // 从就绪队列移除pexit和psuspend的进程，分别到完成队列和挂起队列
    clear();
    // printf("4\n");
    // 根据优先级对就绪队列进行排序
    sort(ready_queue);
    // printf("5\n");
    // 从就绪队列选出优先级最高的进程执行
    struct pro * cur = ready_queue;
    while (cur != NULL && cur->status != pready) 
    {
      cur = cur->next;
    }
    // printf("6\n");
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
    // printf("7\n");
    spin_lock(&lock);
    cur->priority--;
    cur->need_time--;
    if (cur->need_time <= 0)
    {  // 当前进程已完成运行
      cur->status = pexit;
    } else {
      cur->status = pready;
    }
    print_all_queue();
    spin_unlock(&lock);
  }
}

int main (int argc, char *argv[])
{
  start_up();
  int processor_count = 2;
  printf("The program is begin...\n");
  printf("argc %d\n", argc);
  if (argc == 2)
  { // 有一个参数，那么第一个参数作为处理机的数目
    processor_count = atoi(argv[1]);
  } else if (argc == 3) 
  { // 有两个参数，第二个指定时间片长度
    processor_count = atoi(argv[1]);
    timesence = atoi(argv[2]);
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
