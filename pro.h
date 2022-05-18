/**
 * 进程状态
 */
enum pStatus 
{
  pnew = 0, // 后备
  pready,  // 就绪
  psuspend,  // 挂起
  prunning,  // 运行
  pexit,  // 退出
};


/**
 * 进程结构体
 */
struct pro 
{
  int pid;  // 进程号
  int need_time;  // 要求运行时间
  int priority;  // 优先权
  enum pStatus status;  // 进程状态
  struct pro * pre;  // 进程前驱
  struct pro * suc;  // 进程后继
  struct pro * last;  // 前指针
  struct pro * next;  // 后指针
  int ptr;  // 进程存储空间的入口
  int mem;  // 存储空间的大小
};
