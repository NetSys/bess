#include <time.h>
#include <string.h>

#include <glog/logging.h>

#include "test.h"

static struct testcase *head;
static struct testcase *tail;

static int num_tests;
static int num_forced_tests;

void add_testcase(struct testcase *t) {
  if (!head) head = t;

  if (tail) tail->next = t;

  tail = t;

  num_tests++;
  if (t->forced) num_forced_tests++;
}

void run_tests() {
  int i = 0;
  time_t curr;
  char buf[1024];

  if (num_tests == 0) return;

  time(&curr);
  ctime_r(&curr, buf);
  *strchr(buf, '\n') = '\0';

  LOG(INFO) << "Test started at " << buf << "--------------------------";
  for (struct testcase *ptr = head; ptr; ptr = ptr->next) {
    i++;
    LOG(INFO) << i << "/" << num_tests << ": " << ptr->name;
    ptr->func();
  }

  time(&curr);
  ctime_r(&curr, buf);
  *strchr(buf, '\n') = '\0';

  LOG(INFO) << "Test ended at " << buf << "--------------------------";
}

void run_forced_tests() {
  int i = 0;
  time_t curr;
  char buf[1024];

  if (num_forced_tests == 0) return;

  time(&curr);
  ctime_r(&curr, buf);
  *strchr(buf, '\n') = '\0';

  LOG(INFO) << "Test started at " << buf << "--------------------------";

  for (struct testcase *ptr = head; ptr; ptr = ptr->next) {
    if (!ptr->forced) continue;
    i++;
    LOG(INFO) << i << "/" << num_forced_tests << ": " << ptr->name;
    ptr->func();
  }

  time(&curr);
  ctime_r(&curr, buf);
  *strchr(buf, '\n') = '\0';

  LOG(INFO) << "Test ended at " << buf << "--------------------------";
}
