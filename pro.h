#define CHANNEL 4  // 多道机道数

/**
 * 进程状态
 */
enum pStatus {
  pnew = 0,  // 新建
  pready,  // 就绪
  prunning,  // 运行
  psuspend,  // 挂起
  pexit,  // 退出
};


/**
 * 进程结构体
 */
struct pro {
  int pid;  // 进程号
  int need_time;  // 要求运行时间
  int priority;  // 优先权
  enum pStatus status;  // 进程状态
  struct pro * pre;  // 进程前驱
  struct pro * suc;  // 进程后继
  struct pro * last;  // 前指针
  struct pro * next;  // 后指针
};
