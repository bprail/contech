#ifndef TASK_WRITE_HPP
#define TASK_WRITE_HPP

#include "../common/taskLib/Task.hpp"
#include "Context.hpp"
#include "pthread.h"
#include <deque>

extern bool noMoreTasks;
extern pthread_mutex_t taskQueueLock;
extern pthread_cond_t taskQueueCond;
extern std::deque<contech::Task*>* taskQueue;

void updateContextTaskList(contech::Context &c);
void backgroundQueueTask(contech::Task* t);

#endif