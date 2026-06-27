#include "commons/threadpool.h"
#include "pal/pal.h"

#include <stdlib.h>
#include <string.h>

/* ─── Platform threading abstraction ─────────────────────────────────────── */

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>

  typedef HANDLE           Thread;
  typedef CRITICAL_SECTION Mutex;
  typedef CONDITION_VARIABLE CondVar;

  static void mutex_init(Mutex* m)    { InitializeCriticalSection(m); }
  static void mutex_destroy(Mutex* m) { DeleteCriticalSection(m); }
  static void mutex_lock(Mutex* m)    { EnterCriticalSection(m); }
  static void mutex_unlock(Mutex* m)  { LeaveCriticalSection(m); }

  static void cond_init(CondVar* c)                { InitializeConditionVariable(c); }
  static void cond_destroy(CondVar* c)             { (void)c; /* no-op on Windows */ }
  static void cond_signal(CondVar* c)              { WakeConditionVariable(c); }
  static void cond_broadcast(CondVar* c)           { WakeAllConditionVariable(c); }
  static void cond_wait(CondVar* c, Mutex* m)      { SleepConditionVariableCS(c, m, INFINITE); }

  typedef DWORD (WINAPI *ThreadStartFunc)(LPVOID);

  static int thread_create(Thread* t, ThreadStartFunc func, void* arg) {
      *t = CreateThread(NULL, 0, func, arg, 0, NULL);
      return (*t == NULL) ? -1 : 0;
  }

  static void thread_join(Thread t) {
      WaitForSingleObject(t, INFINITE);
      CloseHandle(t);
  }

#else
  #include <pthread.h>

  typedef pthread_t       Thread;
  typedef pthread_mutex_t Mutex;
  typedef pthread_cond_t  CondVar;

  static void mutex_init(Mutex* m)    { pthread_mutex_init(m, NULL); }
  static void mutex_destroy(Mutex* m) { pthread_mutex_destroy(m); }
  static void mutex_lock(Mutex* m)    { pthread_mutex_lock(m); }
  static void mutex_unlock(Mutex* m)  { pthread_mutex_unlock(m); }

  static void cond_init(CondVar* c)                { pthread_cond_init(c, NULL); }
  static void cond_destroy(CondVar* c)             { pthread_cond_destroy(c); }
  static void cond_signal(CondVar* c)              { pthread_cond_signal(c); }
  static void cond_broadcast(CondVar* c)           { pthread_cond_broadcast(c); }
  static void cond_wait(CondVar* c, Mutex* m)      { pthread_cond_wait(c, m); }

  typedef void* (*ThreadStartFunc)(void*);

  static int thread_create(Thread* t, ThreadStartFunc func, void* arg) {
      return pthread_create(t, NULL, func, arg);
  }

  static void thread_join(Thread t) {
      pthread_join(t, NULL);
  }

#endif

/* ─── Task queue (linked list) ───────────────────────────────────────────── */

typedef struct Task {
    TaskFunc     func;
    void*        arg;
    struct Task* next;
} Task;

/* ─── Thread pool structure ──────────────────────────────────────────────── */

struct ThreadPool {
    Thread*     threads;
    int         thread_count;

    /* Task queue (singly-linked list, push to tail, pop from head) */
    Task*       queue_head;
    Task*       queue_tail;

    /* Synchronization */
    Mutex       mutex;
    CondVar     cond_work;      /* signaled when new work available or shutdown */
    CondVar     cond_done;      /* signaled when a task completes */

    /* Counters */
    int         pending;        /* tasks submitted but not yet completed */

    /* Shutdown flag */
    int         shutdown;
};

/* ─── Worker thread entry point ──────────────────────────────────────────── */

#ifdef _WIN32
static DWORD WINAPI worker_func(LPVOID arg)
#else
static void* worker_func(void* arg)
#endif
{
    ThreadPool* pool = (ThreadPool*)arg;

    for (;;) {
        mutex_lock(&pool->mutex);

        /* Wait until there's work or we're shutting down */
        while (pool->queue_head == NULL && !pool->shutdown) {
            cond_wait(&pool->cond_work, &pool->mutex);
        }

        if (pool->shutdown && pool->queue_head == NULL) {
            mutex_unlock(&pool->mutex);
            break;
        }

        /* Dequeue a task */
        Task* task = pool->queue_head;
        pool->queue_head = task->next;
        if (pool->queue_head == NULL) {
            pool->queue_tail = NULL;
        }

        mutex_unlock(&pool->mutex);

        /* Execute the task */
        task->func(task->arg);
        free(task);

        /* Decrement pending counter and signal completion */
        mutex_lock(&pool->mutex);
        pool->pending--;
        if (pool->pending == 0) {
            cond_broadcast(&pool->cond_done);
        }
        mutex_unlock(&pool->mutex);
    }

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/* ─── Public API ─────────────────────────────────────────────────────────── */

ThreadPool* threadpool_create(int n) {
    if (n <= 0) {
        n = pal_cpu_count();
    }
    if (n <= 0) {
        n = 1;
    }

    ThreadPool* pool = (ThreadPool*)calloc(1, sizeof(ThreadPool));
    if (!pool) {
        return NULL;
    }

    pool->thread_count = n;
    pool->queue_head   = NULL;
    pool->queue_tail   = NULL;
    pool->pending      = 0;
    pool->shutdown     = 0;

    mutex_init(&pool->mutex);
    cond_init(&pool->cond_work);
    cond_init(&pool->cond_done);

    pool->threads = (Thread*)calloc((size_t)n, sizeof(Thread));
    if (!pool->threads) {
        mutex_destroy(&pool->mutex);
        cond_destroy(&pool->cond_work);
        cond_destroy(&pool->cond_done);
        free(pool);
        return NULL;
    }

    for (int i = 0; i < n; i++) {
        if (thread_create(&pool->threads[i], worker_func, pool) != 0) {
            /* Failed to create thread — shut down already-created threads */
            pool->shutdown = 1;
            cond_broadcast(&pool->cond_work);
            for (int j = 0; j < i; j++) {
                thread_join(pool->threads[j]);
            }
            mutex_destroy(&pool->mutex);
            cond_destroy(&pool->cond_work);
            cond_destroy(&pool->cond_done);
            free(pool->threads);
            free(pool);
            return NULL;
        }
    }

    return pool;
}

int threadpool_submit(ThreadPool* pool, TaskFunc func, void* arg) {
    if (!pool || !func) {
        return -1;
    }

    Task* task = (Task*)malloc(sizeof(Task));
    if (!task) {
        return -1;
    }

    task->func = func;
    task->arg  = arg;
    task->next = NULL;

    mutex_lock(&pool->mutex);

    if (pool->shutdown) {
        mutex_unlock(&pool->mutex);
        free(task);
        return -1;
    }

    /* Enqueue at tail */
    if (pool->queue_tail) {
        pool->queue_tail->next = task;
    } else {
        pool->queue_head = task;
    }
    pool->queue_tail = task;

    pool->pending++;

    cond_signal(&pool->cond_work);
    mutex_unlock(&pool->mutex);

    return 0;
}

int threadpool_wait(ThreadPool* pool) {
    if (!pool) {
        return -1;
    }

    mutex_lock(&pool->mutex);
    while (pool->pending > 0) {
        cond_wait(&pool->cond_done, &pool->mutex);
    }
    mutex_unlock(&pool->mutex);

    return 0;
}

void threadpool_destroy(ThreadPool* pool) {
    if (!pool) {
        return;
    }

    /* Signal shutdown */
    mutex_lock(&pool->mutex);
    pool->shutdown = 1;
    cond_broadcast(&pool->cond_work);
    mutex_unlock(&pool->mutex);

    /* Join all worker threads */
    for (int i = 0; i < pool->thread_count; i++) {
        thread_join(pool->threads[i]);
    }

    /* Free any remaining tasks in the queue (shouldn't normally happen) */
    Task* t = pool->queue_head;
    while (t) {
        Task* next = t->next;
        free(t);
        t = next;
    }

    mutex_destroy(&pool->mutex);
    cond_destroy(&pool->cond_work);
    cond_destroy(&pool->cond_done);
    free(pool->threads);
    free(pool);
}
